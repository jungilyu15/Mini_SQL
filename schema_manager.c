#include "schema_manager.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 공통 오류 메시지 기록 helper.
 * error_buf가 없더라도 호출 측이 크래시하지 않도록 방어적으로 처리한다.
 */
static void set_error(char *error_buf, size_t error_buf_size, const char *message) {
    if (error_buf != NULL && error_buf_size > 0) {
        snprintf(error_buf, error_buf_size, "%s", message);
    }
}

/*
 * 문자열 양 끝의 공백을 제거한다.
 * schema 포맷에서 "id : int" 같은 입력도 허용하기 위해
 * column 이름과 type 이름을 분리한 뒤 이 함수를 다시 호출한다.
 */
static void trim_in_place(char *text) {
    char *start = text;
    char *end = NULL;

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    if (text[0] == '\0') {
        return;
    }

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

/*
 * 한 줄 안에 특정 문자가 몇 번 등장하는지 센다.
 * 이번 단계에서는 구분자 ':'가 정확히 하나만 있어야 하므로
 * 단순하고 읽기 쉬운 검증 함수로 분리해 둔다.
 */
static int count_char(const char *text, char target) {
    int count = 0;

    while (*text != '\0') {
        if (*text == target) {
            count++;
        }
        text++;
    }

    return count;
}

/*
 * schema 파일의 타입 문자열을 내부 enum으로 변환한다.
 * 현재는 요구사항에 맞춰 int, string 두 타입만 지원한다.
 */
static int parse_column_type(const char *type_name, ColumnType *out_type) {
    if (type_name == NULL || out_type == NULL) {
        return -1;
    }

    if (strcmp(type_name, "int") == 0) {
        *out_type = COL_INT;
        return 0;
    }

    if (strcmp(type_name, "string") == 0) {
        *out_type = COL_TEXT;
        return 0;
    }

    return -1;
}

/*
 * 정수 raw token을 엄격하게 검사하면서 int로 변환한다.
 * 부호가 붙은 10진 정수는 허용하지만, 숫자 이외 문자가 섞이면 실패시킨다.
 */
static int parse_int_token(
    const char *raw_value,
    int *out_value,
    char *error_buf,
    size_t error_buf_size
) {
    char *end_ptr = NULL;
    long parsed = 0;

    if (raw_value == NULL || out_value == NULL || raw_value[0] == '\0') {
        set_error(error_buf, error_buf_size, "cast_value: 비어 있는 int 값을 변환할 수 없습니다");
        return -1;
    }

    errno = 0;
    parsed = strtol(raw_value, &end_ptr, 10);
    if (errno != 0 || end_ptr == raw_value || *end_ptr != '\0') {
        snprintf(
            error_buf,
            error_buf_size,
            "cast_value: '%s'은(는) 유효한 int raw token이 아닙니다",
            raw_value
        );
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        snprintf(
            error_buf,
            error_buf_size,
            "cast_value: '%s'은(는) int 범위를 벗어납니다",
            raw_value
        );
        return -1;
    }

    *out_value = (int)parsed;
    return 0;
}

/*
 * string raw token은 SQL 토큰 기준으로 작은따옴표를 포함한 형태만 받는다.
 * 예: 'Alice'
 *
 * 이번 단계에서는 escape 처리와 내부 따옴표 해석을 지원하지 않으므로,
 * 바깥 따옴표를 제외한 본문 안에 작은따옴표가 다시 나오면 오류로 본다.
 */
static int parse_string_token(
    const char *raw_value,
    char *out_string,
    size_t out_string_size,
    char *error_buf,
    size_t error_buf_size
) {
    size_t raw_length = 0;
    size_t content_length = 0;
    size_t i = 0;

    if (raw_value == NULL || out_string == NULL || out_string_size == 0) {
        set_error(error_buf, error_buf_size, "cast_value: 잘못된 string 변환 인자입니다");
        return -1;
    }

    raw_length = strlen(raw_value);
    if (raw_length < 2 || raw_value[0] != '\'' || raw_value[raw_length - 1] != '\'') {
        snprintf(
            error_buf,
            error_buf_size,
            "cast_value: string 값 '%s'은(는) 작은따옴표로 감싸져 있어야 합니다",
            raw_value
        );
        return -1;
    }

    content_length = raw_length - 2;
    if (content_length >= out_string_size) {
        set_error(error_buf, error_buf_size, "cast_value: string 값 길이가 너무 깁니다");
        return -1;
    }

    for (i = 1; i < raw_length - 1; i++) {
        if (raw_value[i] == '\'') {
            set_error(
                error_buf,
                error_buf_size,
                "cast_value: 내부 작은따옴표나 escape string은 아직 지원하지 않습니다"
            );
            return -1;
        }
    }

    memcpy(out_string, raw_value + 1, content_length);
    out_string[content_length] = '\0';
    return 0;
}

int load_schema(
    const char *table_name,
    TableSchema *out_schema,
    char *error_buf,
    size_t error_buf_size
) {
    char path[256];
    char line[512];
    FILE *file = NULL;
    size_t line_number = 0;

    /* 호출 인자가 잘못되면 즉시 실패시켜 이후 로직을 단순하게 유지한다. */
    if (table_name == NULL || table_name[0] == '\0' || out_schema == NULL) {
        set_error(error_buf, error_buf_size, "load_schema: 잘못된 인자입니다");
        return -1;
    }

    /* 테이블 이름은 최종적으로 TableSchema 안에 그대로 저장되므로 길이도 먼저 확인한다. */
    if (strlen(table_name) >= sizeof(out_schema->table_name)) {
        set_error(error_buf, error_buf_size, "load_schema: 테이블 이름이 너무 깁니다");
        return -1;
    }

    /*
     * 출력 구조체를 먼저 0으로 초기화해 두면
     * 중간 실패 시에도 쓰레기 값이 남지 않는다.
     */
    memset(out_schema, 0, sizeof(*out_schema));
    snprintf(out_schema->table_name, sizeof(out_schema->table_name), "%s", table_name);
    snprintf(path, sizeof(path), "schema/%s.schema", table_name);

    /* 경로는 이번 단계 요구사항대로 내부에서 고정한다. */
    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(
            error_buf,
            error_buf_size,
            "load_schema: schema 파일 '%s'을(를) 열 수 없습니다: %s",
            path,
            strerror(errno)
        );
        return -1;
    }

    /*
     * 파일을 한 줄씩 읽으면서 column:type 형식을 검증한다.
     * 빈 줄은 무시하고, 유효한 줄만 column으로 집계한다.
     */
    while (fgets(line, sizeof(line), file) != NULL) {
        char *separator = NULL;
        char *type_name = NULL;
        ColumnDef *column = NULL;

        line_number++;

        /*
         * 버퍼에 개행 문자가 없고 아직 EOF도 아니라면
         * 한 줄이 현재 버퍼보다 길다는 뜻이므로 형식 오류로 본다.
         */
        if (strchr(line, '\n') == NULL && !feof(file)) {
            snprintf(
                error_buf,
                error_buf_size,
                "load_schema: %s:%zu 줄이 너무 깁니다",
                path,
                line_number
            );
            fclose(file);
            return -1;
        }

        /* 줄 전체의 앞뒤 공백을 제거한 뒤 빈 줄이면 건너뛴다. */
        trim_in_place(line);
        if (line[0] == '\0') {
            continue;
        }

        /*
         * 요구 포맷은 column:type 하나뿐이므로 ':'는 정확히 한 번만 나와야 한다.
         * 0개이거나 2개 이상이면 잘못된 schema로 처리한다.
         */
        if (count_char(line, ':') != 1) {
            snprintf(
                error_buf,
                error_buf_size,
                "load_schema: %s:%zu 줄 형식이 잘못되었습니다",
                path,
                line_number
            );
            fclose(file);
            return -1;
        }

        separator = strchr(line, ':');
        *separator = '\0';
        type_name = separator + 1;

        /* "id : int" 같은 입력도 허용하기 위해 양쪽을 각각 다시 trim한다. */
        trim_in_place(line);
        trim_in_place(type_name);

        /* column 이름이나 타입 이름이 비어 있으면 잘못된 형식이다. */
        if (line[0] == '\0' || type_name[0] == '\0') {
            snprintf(
                error_buf,
                error_buf_size,
                "load_schema: %s:%zu 줄에 빈 column/type 값이 있습니다",
                path,
                line_number
            );
            fclose(file);
            return -1;
        }

        /* 현재 고정 길이 배열을 사용하므로 각 column 이름 길이를 제한한다. */
        if (strlen(line) >= MAX_NAME_LENGTH) {
            snprintf(
                error_buf,
                error_buf_size,
                "load_schema: %s:%zu column 이름이 너무 깁니다",
                path,
                line_number
            );
            fclose(file);
            return -1;
        }

        /* 동적 할당 없이 MAX_COLUMNS까지 저장하는 정책이므로 개수 초과도 즉시 막는다. */
        if (out_schema->column_count >= MAX_COLUMNS) {
            snprintf(
                error_buf,
                error_buf_size,
                "load_schema: %s column 수가 최대치(%d)를 초과합니다",
                path,
                MAX_COLUMNS
            );
            fclose(file);
            return -1;
        }

        /* 검증이 끝난 뒤에야 다음 빈 slot에 column 정보를 기록한다. */
        column = &out_schema->columns[out_schema->column_count];
        snprintf(column->name, sizeof(column->name), "%s", line);

        /* 지원하지 않는 타입 문자열은 명확히 실패시킨다. */
        if (parse_column_type(type_name, &column->type) != 0) {
            snprintf(
                error_buf,
                error_buf_size,
                "load_schema: %s:%zu 지원하지 않는 타입 '%s'입니다",
                path,
                line_number,
                type_name
            );
            fclose(file);
            return -1;
        }

        out_schema->column_count++;
    }

    /* fgets 루프가 끝난 뒤 실제 읽기 오류가 있었는지 한 번 더 확인한다. */
    if (ferror(file)) {
        snprintf(
            error_buf,
            error_buf_size,
            "load_schema: schema 파일 '%s'을(를) 읽는 중 오류가 발생했습니다",
            path
        );
        fclose(file);
        return -1;
    }

    fclose(file);

    /* 빈 줄만 있던 파일도 유효한 schema는 아니므로 실패시킨다. */
    if (out_schema->column_count == 0) {
        snprintf(
            error_buf,
            error_buf_size,
            "load_schema: schema 파일 '%s'에 유효한 column이 없습니다",
            path
        );
        return -1;
    }

    /* 여기까지 왔으면 schema가 정상적으로 로드된 것이다. */
    return 0;
}

int validate_values(
    const TableSchema *schema,
    const SqlValue *values,
    size_t value_count,
    char *error_buf,
    size_t error_buf_size
) {
    (void)values;

    /*
     * 이번 단계의 validate_values는 "개수 검증"만 맡는다.
     * values 자체의 개별 타입 해석은 아직 cast_value 호출 측 책임으로 둔다.
     */
    if (schema == NULL) {
        set_error(error_buf, error_buf_size, "validate_values: schema가 NULL입니다");
        return -1;
    }

    if (schema->column_count > 0 && values == NULL) {
        set_error(error_buf, error_buf_size, "validate_values: values가 NULL입니다");
        return -1;
    }

    if (schema->column_count != value_count) {
        snprintf(
            error_buf,
            error_buf_size,
            "validate_values: schema column 수(%zu)와 입력 value 수(%zu)가 다릅니다",
            schema->column_count,
            value_count
        );
        return -1;
    }

    return 0;
}

int cast_value(
    const char *type_name,
    const char *raw_value,
    StorageValue *out_value,
    char *error_buf,
    size_t error_buf_size
) {
    ColumnType parsed_type;

    if (type_name == NULL || raw_value == NULL || out_value == NULL) {
        set_error(error_buf, error_buf_size, "cast_value: 잘못된 인자입니다");
        return -1;
    }

    /*
     * 반환 구조체를 먼저 0으로 초기화해 두면
     * 중간 실패 시에도 쓰레기 값이 남지 않는다.
     */
    memset(out_value, 0, sizeof(*out_value));

    if (parse_column_type(type_name, &parsed_type) != 0) {
        snprintf(
            error_buf,
            error_buf_size,
            "cast_value: 지원하지 않는 타입 '%s'입니다",
            type_name
        );
        return -1;
    }

    out_value->type = parsed_type;

    if (parsed_type == COL_INT) {
        return parse_int_token(raw_value, &out_value->as.int_value, error_buf, error_buf_size);
    }

    return parse_string_token(
        raw_value,
        out_value->as.string_value,
        sizeof(out_value->as.string_value),
        error_buf,
        error_buf_size
    );
}
