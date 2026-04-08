#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "executor.h"
#include "parser.h"

/*
 * split_sql_statements가 분리한 SQL 문장 목록을 담는 구조체다.
 * 이번 단계에서는 main.c 내부에서만 사용하므로 공개 헤더로 내보내지 않는다.
 */
typedef struct {
    size_t count;
    char **items;
} SqlStatementList;

/* 사용법을 한 줄로 안내한다. */
static void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s <sql-file>\n", program_name);
}

/*
 * 공통 오류 메시지를 기록한다.
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
 * 문자열 양 끝의 공백을 제거한 구간을 계산한다.
 * split 단계에서 빈 문장을 걸러내기 위해 사용한다.
 */
static void trim_range(const char **start, const char **end)
{
    while (*start < *end &&
           (**start == ' ' || **start == '\t' || **start == '\n' || **start == '\r'))
    {
        (*start)++;
    }

    while (*end > *start &&
           ((*(*end - 1) == ' ') || (*(*end - 1) == '\t') || (*(*end - 1) == '\n') || (*(*end - 1) == '\r')))
    {
        (*end)--;
    }
}

/*
 * SQL 파일 전체를 메모리로 읽는다.
 * caller는 성공 시 out_text를 free()해야 한다.
 */
static int read_text_file(
    const char *path,
    char **out_text,
    char *error_buf,
    size_t error_buf_size
)
{
    FILE *file = NULL;
    long file_size = 0;
    size_t bytes_read = 0;
    char *buffer = NULL;

    if (path == NULL || out_text == NULL)
    {
        set_error(error_buf, error_buf_size, "main: 잘못된 파일 읽기 인자입니다");
        return -1;
    }

    *out_text = NULL;

    file = fopen(path, "rb");
    if (file == NULL)
    {
        snprintf(error_buf, error_buf_size, "main: SQL 파일 '%s'을(를) 열 수 없습니다", path);
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        set_error(error_buf, error_buf_size, "main: SQL 파일 크기를 확인할 수 없습니다");
        return -1;
    }

    file_size = ftell(file);
    if (file_size < 0)
    {
        fclose(file);
        set_error(error_buf, error_buf_size, "main: SQL 파일 크기를 확인할 수 없습니다");
        return -1;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        set_error(error_buf, error_buf_size, "main: SQL 파일 읽기 위치를 초기화할 수 없습니다");
        return -1;
    }

    buffer = (char *)malloc((size_t)file_size + 1);
    if (buffer == NULL)
    {
        fclose(file);
        set_error(error_buf, error_buf_size, "main: SQL 파일 버퍼를 할당할 수 없습니다");
        return -1;
    }

    bytes_read = fread(buffer, 1, (size_t)file_size, file);
    if (bytes_read != (size_t)file_size && ferror(file))
    {
        free(buffer);
        fclose(file);
        set_error(error_buf, error_buf_size, "main: SQL 파일을 읽는 중 오류가 발생했습니다");
        return -1;
    }

    buffer[bytes_read] = '\0';
    fclose(file);
    *out_text = buffer;
    return 0;
}

/*
 * SQL 문장 문자열 하나를 statement list에 추가한다.
 * list는 realloc으로 확장하며, 각 문장은 독립적인 문자열로 복사해 보관한다.
 */
static int append_statement(
    SqlStatementList *list,
    const char *start,
    const char *end,
    char *error_buf,
    size_t error_buf_size
)
{
    char *statement = NULL;
    char **new_items = NULL;
    size_t length = 0;

    if (list == NULL || start == NULL || end == NULL)
    {
        set_error(error_buf, error_buf_size, "main: SQL 문장 추가 인자가 올바르지 않습니다");
        return -1;
    }

    length = (size_t)(end - start);
    statement = (char *)malloc(length + 1);
    if (statement == NULL)
    {
        set_error(error_buf, error_buf_size, "main: SQL 문장 버퍼를 할당할 수 없습니다");
        return -1;
    }

    memcpy(statement, start, length);
    statement[length] = '\0';

    new_items = (char **)realloc(list->items, sizeof(char *) * (list->count + 1));
    if (new_items == NULL)
    {
        free(statement);
        set_error(error_buf, error_buf_size, "main: SQL 문장 목록을 확장할 수 없습니다");
        return -1;
    }

    list->items = new_items;
    list->items[list->count] = statement;
    list->count++;
    return 0;
}

/*
 * SQL 텍스트 전체를 세미콜론 기준으로 문장 단위로 나눈다.
 * 단, 작은따옴표 문자열 안의 세미콜론은 문장 구분자로 취급하지 않는다.
 *
 * 정책:
 * - 빈 문장은 무시한다
 * - 마지막 세미콜론이 없어도 마지막 문장을 하나로 본다
 * - 닫히지 않은 작은따옴표 문자열이 있으면 오류로 처리한다
 */
static int split_sql_statements(
    const char *sql_text,
    SqlStatementList *out_list,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *cursor = NULL;
    const char *statement_start = NULL;
    int in_single_quote = 0;

    if (sql_text == NULL || out_list == NULL)
    {
        set_error(error_buf, error_buf_size, "main: SQL 분리 인자가 올바르지 않습니다");
        return -1;
    }

    out_list->count = 0;
    out_list->items = NULL;

    cursor = sql_text;
    statement_start = sql_text;

    while (*cursor != '\0')
    {
        if (*cursor == '\'')
        {
            in_single_quote = !in_single_quote;
        }
        else if (*cursor == ';' && !in_single_quote)
        {
            const char *trimmed_start = statement_start;
            const char *trimmed_end = cursor;

            trim_range(&trimmed_start, &trimmed_end);
            if (trimmed_start < trimmed_end)
            {
                if (append_statement(out_list, trimmed_start, trimmed_end, error_buf, error_buf_size) != 0)
                {
                    return -1;
                }
            }

            statement_start = cursor + 1;
        }

        cursor++;
    }

    if (in_single_quote)
    {
        set_error(error_buf, error_buf_size, "main: 닫히지 않은 작은따옴표 문자열이 있습니다");
        return -1;
    }

    {
        const char *trimmed_start = statement_start;
        const char *trimmed_end = cursor;

        trim_range(&trimmed_start, &trimmed_end);
        if (trimmed_start < trimmed_end)
        {
            if (append_statement(out_list, trimmed_start, trimmed_end, error_buf, error_buf_size) != 0)
            {
                return -1;
            }
        }
    }

    return 0;
}

/* split_sql_statements가 만든 동적 문장 목록을 해제한다. */
static void free_sql_statement_list(SqlStatementList *list)
{
    size_t i = 0;

    if (list == NULL)
    {
        return;
    }

    for (i = 0; i < list->count; i++)
    {
        free(list->items[i]);
    }

    free(list->items);
    list->items = NULL;
    list->count = 0;
}

/*
 * StorageValue 하나를 사람이 읽을 수 있는 문자열로 바꾼다.
 * SELECT 결과 출력에서 열 너비 계산과 실제 출력에 함께 사용한다.
 */
static int format_storage_value(
    const StorageValue *value,
    char *buffer,
    size_t buffer_size,
    char *error_buf,
    size_t error_buf_size
)
{
    int written = 0;

    if (value == NULL || buffer == NULL || buffer_size == 0)
    {
        set_error(error_buf, error_buf_size, "main: 출력용 값 포맷 인자가 올바르지 않습니다");
        return -1;
    }

    if (value->type == COL_INT)
    {
        written = snprintf(buffer, buffer_size, "%d", value->as.int_value);
    }
    else
    {
        written = snprintf(buffer, buffer_size, "%s", value->as.string_value);
    }

    if (written < 0 || (size_t)written >= buffer_size)
    {
        set_error(error_buf, error_buf_size, "main: 출력용 값 문자열이 너무 깁니다");
        return -1;
    }

    return 0;
}

/* 표 경계선을 출력한다. */
static void print_table_border(FILE *out_stream, const size_t *widths, size_t column_count)
{
    size_t i = 0;
    size_t j = 0;

    fputc('+', out_stream);
    for (i = 0; i < column_count; i++)
    {
        for (j = 0; j < widths[i] + 2; j++)
        {
            fputc('-', out_stream);
        }
        fputc('+', out_stream);
    }
    fputc('\n', out_stream);
}

/*
 * SELECT 결과를 간단한 표 형태로 출력한다.
 * 현재는 SELECT *만 지원하므로 schema의 모든 컬럼을 그대로 보여준다.
 */
static int print_select_result(
    const ExecutionResult *result,
    FILE *out_stream,
    char *error_buf,
    size_t error_buf_size
)
{
    size_t widths[MAX_COLUMNS];
    size_t i = 0;
    size_t row_index = 0;
    char value_buffer[MAX_VALUE_LENGTH + 32];

    if (result == NULL || out_stream == NULL)
    {
        set_error(error_buf, error_buf_size, "main: SELECT 출력 인자가 올바르지 않습니다");
        return -1;
    }

    if (result->schema.column_count == 0)
    {
        fprintf(out_stream, "(0 rows)\n");
        return 0;
    }

    for (i = 0; i < result->schema.column_count; i++)
    {
        widths[i] = strlen(result->schema.columns[i].name);
    }

    for (row_index = 0; row_index < result->rows.row_count; row_index++)
    {
        for (i = 0; i < result->schema.column_count; i++)
        {
            size_t value_length = 0;

            if (format_storage_value(
                    &result->rows.rows[row_index].values[i],
                    value_buffer,
                    sizeof(value_buffer),
                    error_buf,
                    error_buf_size) != 0)
            {
                return -1;
            }

            value_length = strlen(value_buffer);
            if (value_length > widths[i])
            {
                widths[i] = value_length;
            }
        }
    }

    print_table_border(out_stream, widths, result->schema.column_count);
    fprintf(out_stream, "|");
    for (i = 0; i < result->schema.column_count; i++)
    {
        fprintf(out_stream, " %-*s |", (int)widths[i], result->schema.columns[i].name);
    }
    fprintf(out_stream, "\n");
    print_table_border(out_stream, widths, result->schema.column_count);

    for (row_index = 0; row_index < result->rows.row_count; row_index++)
    {
        fprintf(out_stream, "|");
        for (i = 0; i < result->schema.column_count; i++)
        {
            if (format_storage_value(
                    &result->rows.rows[row_index].values[i],
                    value_buffer,
                    sizeof(value_buffer),
                    error_buf,
                    error_buf_size) != 0)
            {
                return -1;
            }

            fprintf(out_stream, " %-*s |", (int)widths[i], value_buffer);
        }
        fprintf(out_stream, "\n");
    }

    print_table_border(out_stream, widths, result->schema.column_count);
    fprintf(out_stream, "(%zu rows)\n", result->rows.row_count);
    return 0;
}

/*
 * SQL 문장 목록을 순서대로 실행한다.
 * 각 문장은 parse_sql -> execute_command 순서로 흘러가며,
 * SELECT 결과만 콘솔에 표 형태로 출력한다.
 */
static int run_sql_file(
    const SqlStatementList *statements,
    FILE *out_stream,
    char *error_buf,
    size_t error_buf_size
)
{
    size_t i = 0;

    if (statements == NULL || out_stream == NULL)
    {
        set_error(error_buf, error_buf_size, "main: SQL 실행 인자가 올바르지 않습니다");
        return -1;
    }

    for (i = 0; i < statements->count; i++)
    {
        Command command;
        ExecutionResult result;
        char statement_error[MAX_ERROR_LENGTH];

        if (parse_sql(statements->items[i], &command, statement_error, sizeof(statement_error)) != 0)
        {
            snprintf(
                error_buf,
                error_buf_size,
                "statement %zu parse failed: %s",
                i + 1,
                statement_error
            );
            return -1;
        }

        if (execute_command(&command, &result, statement_error, sizeof(statement_error)) != 0)
        {
            snprintf(
                error_buf,
                error_buf_size,
                "statement %zu execute failed: %s",
                i + 1,
                statement_error
            );
            return -1;
        }

        if (result.has_rows)
        {
            if (print_select_result(&result, out_stream, error_buf, error_buf_size) != 0)
            {
                free_execution_result(&result);
                return -1;
            }
        }

        free_execution_result(&result);
    }

    return 0;
}

int main(int argc, char **argv)
{
    char *sql_text = NULL;
    SqlStatementList statements;
    char error_buf[MAX_ERROR_LENGTH];

    statements.count = 0;
    statements.items = NULL;

    if (argc != 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    if (read_text_file(argv[1], &sql_text, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (split_sql_statements(sql_text, &statements, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        free_sql_statement_list(&statements);
        free(sql_text);
        return 1;
    }

    if (run_sql_file(&statements, stdout, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        free_sql_statement_list(&statements);
        free(sql_text);
        return 1;
    }

    free_sql_statement_list(&statements);
    free(sql_text);
    return 0;
}
