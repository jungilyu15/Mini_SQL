#include "storage.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * storage.c 내부 helper는 아래 역할로 나눈다.
 * - 오류/경로 처리 helper
 * - StorageRow <-> CSV 한 줄 변환 helper -> append_row
 * - row list 동적 확장 helper ->  조회에서 realloc
 *
 * 공개 API는 append_row / read_all_rows / free_storage_row_list 세 개만 유지한다.
 */

/*
 * 공통 오류 메시지 기록 helper.
 * error_buf가 NULL이어도 안전하게 호출할 수 있도록 방어적으로 처리한다.
 */
static void set_error(char *error_buf, size_t error_buf_size, const char *message)
{
    if (error_buf != NULL && error_buf_size > 0)
    {
        snprintf(error_buf, error_buf_size, "%s", message);
    }
}

/*
 * 가변 인자를 받아 형식화된 오류 메시지를 기록한다.
 * 같은 패턴의 snprintf 코드를 반복하지 않도록 별도 helper로 분리한다.
 */
static void set_errorf(char *error_buf, size_t error_buf_size, const char *format, ...)
{
    va_list args;

    if (error_buf == NULL || error_buf_size == 0 || format == NULL)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(error_buf, error_buf_size, format, args);
    va_end(args);
}

/*
 * 함수 시작 시 결과 구조체를 항상 예측 가능한 상태로 맞춘다.
 * 실패 후에도 caller가 free_storage_row_list()를 안전하게 호출할 수 있어야 한다.
 */
static void init_row_list(StorageRowList *row_list)
{
    if (row_list != NULL)
    {
        row_list->row_count = 0;
        row_list->rows = NULL;
    }
}

/*
 * 이번 단계에서는 data 디렉터리가 미리 존재한다고 가정한다.
 * 따라서 디렉터리가 없으면 자동 생성하지 않고 명확한 오류로 처리한다.
 */
static int ensure_data_directory_exists(char *error_buf, size_t error_buf_size)
{
    struct stat st;

    if (stat("data", &st) != 0)
    {
        set_error(error_buf, error_buf_size, "storage: data 디렉터리를 찾을 수 없습니다");
        return -1;
    }

    if (!S_ISDIR(st.st_mode))
    {
        set_error(error_buf, error_buf_size, "storage: 'data' 경로가 디렉터리가 아닙니다");
        return -1;
    }

    return 0;
}

/*
 * table 이름으로 data/<table_name>.csv 경로를 만든다.
 * 경로 버퍼를 넘기지 않도록 길이도 함께 검사한다.
 */
static int build_data_path(
    const char *table_name,
    char *out_path,
    size_t out_path_size,
    char *error_buf,
    size_t error_buf_size)
{
    int written = 0;

    if (table_name == NULL || table_name[0] == '\0' || out_path == NULL || out_path_size == 0)
    {
        set_error(error_buf, error_buf_size, "storage: 잘못된 table_name/path 인자입니다");
        return -1;
    }

    written = snprintf(out_path, out_path_size, "data/%s.csv", table_name);
    if (written < 0 || (size_t)written >= out_path_size)
    {
        set_error(error_buf, error_buf_size, "storage: 데이터 파일 경로가 너무 깁니다");
        return -1;
    }

    return 0;
}

/*
 * TEXT 값에 단순 CSV 범위를 벗어나는 문자가 있는지 검사한다.
 * 이번 단계에서는 쉼표, 개행, 큰따옴표가 들어간 문자열을 지원하지 않는다.
 */
static int validate_text_value(
    const char *text,
    char *error_buf,
    size_t error_buf_size)
{
    const char *cursor = NULL;

    if (text == NULL)
    {
        set_error(error_buf, error_buf_size, "storage: TEXT 값이 NULL입니다");
        return -1;
    }

    if (strlen(text) >= MAX_VALUE_LENGTH)
    {
        set_error(error_buf, error_buf_size, "storage: TEXT 값 길이가 너무 깁니다");
        return -1;
    }

    cursor = text;
    while (*cursor != '\0')
    {
        if (*cursor == ',' || *cursor == '\n' || *cursor == '\r' || *cursor == '"')
        {
            set_error(
                error_buf,
                error_buf_size,
                "storage: TEXT 값에는 쉼표, 개행, 큰따옴표를 포함할 수 없습니다");
            return -1;
        }
        cursor++;
    }

    return 0;
}

/*
 * 정수 문자열을 엄격하게 검증하면서 int로 변환한다.
 * 숫자 전체가 정수 형태여야 하며, 범위를 벗어나면 실패시킨다.
 */
static int parse_int_strict(
    const char *text,
    int *out_value,
    char *error_buf,
    size_t error_buf_size)
{
    char *end_ptr = NULL;
    long parsed = 0;

    if (text == NULL || out_value == NULL || text[0] == '\0')
    {
        set_error(error_buf, error_buf_size, "storage: 비어 있는 int 값을 읽을 수 없습니다");
        return -1;
    }

    errno = 0;
    parsed = strtol(text, &end_ptr, 10);
    if (errno != 0 || end_ptr == text || *end_ptr != '\0')
    {
        set_errorf(error_buf, error_buf_size, "storage: '%s'은(는) 유효한 int 값이 아닙니다", text);
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX)
    {
        set_errorf(error_buf, error_buf_size, "storage: '%s'은(는) int 범위를 벗어납니다", text);
        return -1;
    }

    *out_value = (int)parsed;
    return 0;
}

/*
 * schema와 row의 컬럼 수 및 타입 일치 여부를 검사한다.
 * StorageValue.type은 런타임 보조 정보이며, schema 타입과 다르면 바로 오류로 본다.
 */
static int validate_row_against_schema(
    const TableSchema *schema,
    const StorageRow *row,
    char *error_buf,
    size_t error_buf_size)
{
    size_t i = 0;

    if (schema == NULL || row == NULL)
    {
        set_error(error_buf, error_buf_size, "storage: schema 또는 row가 NULL입니다");
        return -1;
    }

    /*
     * schema_manager가 정상 schema를 넘겨 준다고 가정하지만,
     * storage 단에서도 최소한의 방어 검사는 해 둔다.
     */
    if (schema->column_count == 0 || schema->column_count > MAX_COLUMNS)
    {
        set_error(error_buf, error_buf_size, "storage: schema column 수가 올바르지 않습니다");
        return -1;
    }

    if (row->value_count != schema->column_count)
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "storage: row 값 개수(%zu)와 schema column 수(%zu)가 다릅니다",
            row->value_count,
            schema->column_count);
        return -1;
    }

    for (i = 0; i < schema->column_count; i++)
    {
        if (row->values[i].type != schema->columns[i].type)
        {
            set_errorf(
                error_buf,
                error_buf_size,
                "storage: '%s' column의 row 타입과 schema 타입이 다릅니다",
                schema->columns[i].name);
            return -1;
        }

        if (row->values[i].type == COL_TEXT &&
            validate_text_value(row->values[i].as.string_value, error_buf, error_buf_size) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/*
 * StorageRow 하나를 단순 CSV 한 줄로 직렬화한다.
 * 이번 단계에서는 quoting/escaping 없이 schema 순서대로만 저장한다.
 */
static int serialize_row_to_csv_line(
    const TableSchema *schema,
    const StorageRow *row,
    char *out_line,
    size_t out_line_size,
    char *error_buf,
    size_t error_buf_size)
{
    size_t i = 0;
    size_t used = 0;

    if (validate_row_against_schema(schema, row, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (out_line == NULL || out_line_size == 0)
    {
        set_error(error_buf, error_buf_size, "storage: CSV 직렬화 버퍼가 올바르지 않습니다");
        return -1;
    }

    out_line[0] = '\0';

    for (i = 0; i < schema->column_count; i++)
    {
        int written = 0;

        if (i > 0)
        {
            if (used + 1 >= out_line_size)
            {
                set_error(error_buf, error_buf_size, "storage: CSV 한 줄 길이가 너무 깁니다");
                return -1;
            }
            out_line[used++] = ',';
            out_line[used] = '\0';
        }

        if (schema->columns[i].type == COL_INT)
        {
            written = snprintf(
                out_line + used,
                out_line_size - used,
                "%d",
                row->values[i].as.int_value);
            if (written < 0 || (size_t)written >= out_line_size - used)
            {
                set_error(error_buf, error_buf_size, "storage: CSV 한 줄 길이가 너무 깁니다");
                return -1;
            }
            used += (size_t)written;
        }
        else
        {
            size_t text_length = strlen(row->values[i].as.string_value);

            if (used + text_length >= out_line_size)
            {
                set_error(error_buf, error_buf_size, "storage: CSV 한 줄 길이가 너무 깁니다");
                return -1;
            }

            memcpy(out_line + used, row->values[i].as.string_value, text_length);
            used += text_length;
            out_line[used] = '\0';
        }
    }

    /*
     * 실제 파일에는 마지막에 개행을 붙여 저장하므로
     * 직렬화 결과 길이 + 1(개행)도 MAX_LINE_LENGTH 안에 들어야 한다.
     */
    if (used + 1 > MAX_LINE_LENGTH)
    {
        set_error(error_buf, error_buf_size, "storage: CSV 한 줄 길이가 MAX_LINE_LENGTH를 초과합니다");
        return -1;
    }

    return 0;
}

/*
 * fgets로 읽은 문자열 끝의 개행 문자를 제거한다.
 * CRLF 환경도 고려해 '\n', '\r'을 모두 떼어낸다.
 */
static void strip_line_endings(char *line)
{
    size_t length = 0;

    if (line == NULL)
    {
        return;
    }

    length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
    {
        line[length - 1] = '\0';
        length--;
    }
}

/*
 * 단순 CSV 한 줄을 schema 기준으로 StorageRow 하나로 복원한다.
 * quoting/escaping은 지원하지 않으므로 쉼표 기준으로만 필드를 나눈다.
 */
static int parse_csv_line_to_row(
    const char *line,
    const TableSchema *schema,
    StorageRow *out_row,
    char *error_buf,
    size_t error_buf_size)
{
    const char *cursor = NULL;
    size_t i = 0;

    if (line == NULL || schema == NULL || out_row == NULL)
    {
        set_error(error_buf, error_buf_size, "storage: CSV 파싱 인자가 올바르지 않습니다");
        return -1;
    }

    if (schema->column_count == 0 || schema->column_count > MAX_COLUMNS)
    {
        set_error(error_buf, error_buf_size, "storage: schema column 수가 올바르지 않습니다");
        return -1;
    }

    memset(out_row, 0, sizeof(*out_row));
    out_row->value_count = schema->column_count;
    cursor = line;

    for (i = 0; i < schema->column_count; i++)
    {
        const char *separator = strchr(cursor, ',');
        char field[MAX_VALUE_LENGTH];
        size_t field_length = 0;

        if (i < schema->column_count - 1)
        {
            if (separator == NULL)
            {
                set_error(error_buf, error_buf_size, "storage: CSV 필드 개수가 schema와 다릅니다");
                return -1;
            }
            field_length = (size_t)(separator - cursor);
        }
        else
        {
            if (separator != NULL)
            {
                set_error(error_buf, error_buf_size, "storage: CSV 필드 개수가 schema와 다릅니다");
                return -1;
            }
            field_length = strlen(cursor);
        }

        if (field_length >= sizeof(field))
        {
            set_error(error_buf, error_buf_size, "storage: CSV 필드 길이가 너무 깁니다");
            return -1;
        }

        memcpy(field, cursor, field_length);
        field[field_length] = '\0';

        out_row->values[i].type = schema->columns[i].type;
        if (schema->columns[i].type == COL_INT)
        {
            if (parse_int_strict(field, &out_row->values[i].as.int_value, error_buf, error_buf_size) != 0)
            {
                return -1;
            }
        }
        else
        {
            if (strlen(field) >= sizeof(out_row->values[i].as.string_value))
            {
                set_error(error_buf, error_buf_size, "storage: 문자열 필드 길이가 너무 깁니다");
                return -1;
            }
            memcpy(out_row->values[i].as.string_value, field, field_length + 1);
        }

        if (separator != NULL)
        {
            cursor = separator + 1;
        }
    }

    return 0;
}

/*
 * row 수를 미리 알 수 없기 때문에 읽으면서 동적 배열을 늘린다.
 * CSV 파일의 전체 row 수는 읽기 전에는 알 수 없어서 동적 배열이 필요하다.
 * 현재 설계에서는 읽으면서 realloc으로 배열을 확장하는 방식이 가장 자연스럽다.
 *
 * 대안으로는 고정 배열, 파일을 두 번 읽는 2-pass, linked list가 가능하지만
 * 지금 단계의 목표인 단순성과 디버깅 용이성에는 realloc 방식이 가장 잘 맞는다.
 */
static int append_row_to_list(
    StorageRowList *row_list,
    const StorageRow *row,
    char *error_buf,
    size_t error_buf_size)
{
    StorageRow *resized_rows = NULL;

    if (row_list == NULL || row == NULL)
    {
        set_error(error_buf, error_buf_size, "storage: row list 추가 인자가 올바르지 않습니다");
        return -1;
    }

    resized_rows = realloc(row_list->rows, sizeof(StorageRow) * (row_list->row_count + 1));
    if (resized_rows == NULL)
    {
        set_error(error_buf, error_buf_size, "storage: row 배열 메모리 할당에 실패했습니다");
        return -1;
    }

    row_list->rows = resized_rows;
    row_list->rows[row_list->row_count] = *row;
    row_list->row_count++;
    return 0;
}

int append_row(
    const char *table_name,
    const TableSchema *schema,
    const StorageRow *row,
    char *error_buf,
    size_t error_buf_size)
{
    char path[256];
    char line[MAX_LINE_LENGTH + 1];
    FILE *file = NULL;

    if (table_name == NULL || schema == NULL || row == NULL)
    {
        set_error(error_buf, error_buf_size, "append_row: 잘못된 인자입니다");
        return -1;
    }

    if (ensure_data_directory_exists(error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (build_data_path(table_name, path, sizeof(path), error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (serialize_row_to_csv_line(schema, row, line, sizeof(line), error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    file = fopen(path, "a");
    if (file == NULL)
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "append_row: 데이터 파일 '%s'을(를) 열 수 없습니다: %s",
            path,
            strerror(errno));
        return -1;
    }

    if (fprintf(file, "%s\n", line) < 0)
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "append_row: 데이터 파일 '%s'에 쓰는 중 오류가 발생했습니다",
            path);
        fclose(file);
        return -1;
    }

    if (fclose(file) != 0)
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "append_row: 데이터 파일 '%s'을(를) 닫는 중 오류가 발생했습니다",
            path);
        return -1;
    }

    return 0;
}

int read_all_rows(
    const char *table_name,
    const TableSchema *schema,
    StorageRowList *out_row_list,
    char *error_buf,
    size_t error_buf_size)
{
    char path[256];
    char line[MAX_LINE_LENGTH + 1];
    FILE *file = NULL;

    init_row_list(out_row_list);

    if (table_name == NULL || schema == NULL || out_row_list == NULL)
    {
        set_error(error_buf, error_buf_size, "read_all_rows: 잘못된 인자입니다");
        return -1;
    }

    if (ensure_data_directory_exists(error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (build_data_path(table_name, path, sizeof(path), error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL)
    {
        if (errno == ENOENT)
        {
            /*
             * CSV 파일이 아직 없다는 것은 "데이터가 한 줄도 없는 테이블"로 해석한다.
             * 이 경우 오류로 보지 않고 빈 결과를 그대로 반환한다.
             */
            return 0;
        }

        set_errorf(
            error_buf,
            error_buf_size,
            "read_all_rows: 데이터 파일 '%s'을(를) 열 수 없습니다: %s",
            path,
            strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        StorageRow row;

        if (strchr(line, '\n') == NULL && !feof(file))
        {
            set_errorf(
                error_buf,
                error_buf_size,
                "read_all_rows: 데이터 파일 '%s'에 MAX_LINE_LENGTH를 넘는 줄이 있습니다",
                path);
            fclose(file);
            free_storage_row_list(out_row_list);
            return -1;
        }

        strip_line_endings(line);
        if (line[0] == '\0')
        {
            continue;
        }

        if (parse_csv_line_to_row(line, schema, &row, error_buf, error_buf_size) != 0)
        {
            fclose(file);
            free_storage_row_list(out_row_list);
            return -1;
        }

        if (append_row_to_list(out_row_list, &row, error_buf, error_buf_size) != 0)
        {
            fclose(file);
            free_storage_row_list(out_row_list);
            return -1;
        }
    }

    if (ferror(file))
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "read_all_rows: 데이터 파일 '%s'을(를) 읽는 중 오류가 발생했습니다",
            path);
        fclose(file);
        free_storage_row_list(out_row_list);
        return -1;
    }

    fclose(file);
    return 0;
}

void free_storage_row_list(StorageRowList *row_list)
{
    if (row_list == NULL)
    {
        return;
    }

    /*
     * rows만 동적으로 할당되므로 여기서 한 번만 해제하면 충분하다.
     * 해제 후 구조체를 초기 상태로 되돌려, 중복 호출에도 안전하게 만든다.
     */
    free(row_list->rows);
    row_list->rows = NULL;
    row_list->row_count = 0;
}
