#include "schema_manager.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
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
