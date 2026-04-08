#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * parser 단계에서는 값의 실제 타입을 해석하지 않는다.
 * 숫자와 문자열 모두 이후 schema/cast 단계로 넘길 수 있도록 raw token으로 보관한다.
 */

/* 공통 오류 메시지를 기록하는 helper다. */
static void set_error(char *error_buf, size_t error_buf_size, const char *message)
{
    if (error_buf != NULL && error_buf_size > 0)
    {
        snprintf(error_buf, error_buf_size, "%s", message);
    }
}

/* 현재 위치부터 연속된 공백을 건너뛴다. */
static const char *skip_spaces(const char *cursor)
{
    while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
        cursor++;
    }
    return cursor;
}

/*
 * SQL 키워드를 대소문자 구분 없이 비교한다.
 * keyword 뒤에 식별자 문자가 이어지면 부분 일치로 보지 않고 실패시킨다.
 */
static int match_keyword(const char **cursor, const char *keyword)
{
    const char *sql = *cursor;
    size_t i = 0;

    while (keyword[i] != '\0')
    {
        if (toupper((unsigned char)sql[i]) != (unsigned char)keyword[i])
        {
            return 0;
        }
        i++;
    }

    if (isalnum((unsigned char)sql[i]) || sql[i] == '_')
    {
        return 0;
    }

    *cursor = sql + i;
    return 1;
}

/* 테이블 이름처럼 단순 식별자 하나를 읽는다. */
static int parse_identifier(const char **cursor, char *out_name, size_t out_name_size)
{
    const char *sql = *cursor;
    size_t length = 0;

    if (!(isalpha((unsigned char)*sql) || *sql == '_'))
    {
        return 0;
    }

    while (isalnum((unsigned char)sql[length]) || sql[length] == '_')
    {
        length++;
    }

    if (length >= out_name_size)
    {
        return 0;
    }

    memcpy(out_name, sql, length);
    out_name[length] = '\0';
    *cursor = sql + length;
    return 1;
}

/*
 * 값 토큰 앞뒤의 공백을 제거해 복사한다.
 * 문자열 토큰은 작은따옴표까지 포함한 raw SQL token 형태를 그대로 유지한다.
 */
static int copy_trimmed_token(
    const char *start,
    const char *end,
    char *out_token,
    size_t out_token_size,
    char *error_buf,
    size_t error_buf_size
)
{
    size_t token_length = 0;

    while (start < end && isspace((unsigned char)*start))
    {
        start++;
    }

    while (end > start && isspace((unsigned char)*(end - 1)))
    {
        end--;
    }

    token_length = (size_t)(end - start);
    if (token_length == 0)
    {
        set_error(error_buf, error_buf_size, "parse_insert: 빈 값은 허용하지 않습니다");
        return -1;
    }

    if (token_length >= out_token_size)
    {
        set_error(error_buf, error_buf_size, "parse_insert: 값 token 길이가 너무 깁니다");
        return -1;
    }

    memcpy(out_token, start, token_length);
    out_token[token_length] = '\0';
    return 0;
}

/*
 * VALUES (...) 안의 값 목록을 읽는다.
 * 쉼표는 작은따옴표 바깥에서만 구분자로 취급한다.
 */
static int parse_values(
    const char **cursor,
    InsertCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *sql = skip_spaces(*cursor);
    const char *token_start = NULL;
    int in_single_quote = 0;

    if (*sql != '(')
    {
        set_error(error_buf, error_buf_size, "parse_insert: VALUES 뒤에 '('가 필요합니다");
        return -1;
    }

    sql++;
    sql = skip_spaces(sql);
    if (*sql == ')')
    {
        set_error(error_buf, error_buf_size, "parse_insert: 빈 VALUES 목록은 허용하지 않습니다");
        return -1;
    }

    token_start = sql;

    while (*sql != '\0')
    {
        if (*sql == '\'')
        {
            in_single_quote = !in_single_quote;
        }
        else if (!in_single_quote && (*sql == ',' || *sql == ')'))
        {
            if (out_command->value_count >= MAX_COLUMNS)
            {
                set_error(error_buf, error_buf_size, "parse_insert: 값 개수가 최대치를 초과합니다");
                return -1;
            }

            if (copy_trimmed_token(
                    token_start,
                    sql,
                    out_command->values[out_command->value_count].raw,
                    sizeof(out_command->values[out_command->value_count].raw),
                    error_buf,
                    error_buf_size) != 0)
            {
                return -1;
            }

            out_command->value_count++;

            if (*sql == ')')
            {
                *cursor = sql + 1;
                return 0;
            }

            sql++;
            sql = skip_spaces(sql);
            if (*sql == ',' || *sql == ')')
            {
                set_error(error_buf, error_buf_size, "parse_insert: 빈 값은 허용하지 않습니다");
                return -1;
            }

            token_start = sql;
            continue;
        }

        sql++;
    }

    if (in_single_quote)
    {
        set_error(error_buf, error_buf_size, "parse_insert: 닫히지 않은 작은따옴표 문자열이 있습니다");
        return -1;
    }

    set_error(error_buf, error_buf_size, "parse_insert: 닫히지 않은 VALUES 목록입니다");
    return -1;
}

/*
 * 마지막 세미콜론은 선택적으로 허용한다.
 * 대신 세미콜론 뒤에 다른 문장이 이어지는 것은 아직 지원하지 않는다.
 */
static int expect_end_or_semicolon(
    const char *cursor,
    const char *context_name,
    char *error_buf,
    size_t error_buf_size
)
{
    cursor = skip_spaces(cursor);

    if (*cursor == ';')
    {
        cursor = skip_spaces(cursor + 1);
    }

    if (*cursor != '\0')
    {
        snprintf(
            error_buf,
            error_buf_size,
            "%s: 문장 뒤에 지원하지 않는 추가 내용이 있습니다",
            context_name
        );
        return -1;
    }

    return 0;
}

int parse_insert(
    const char *sql,
    InsertCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *cursor = NULL;

    if (sql == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_insert: 잘못된 인자입니다");
        return -1;
    }

    memset(out_command, 0, sizeof(*out_command));
    cursor = skip_spaces(sql);

    if (!match_keyword(&cursor, "INSERT"))
    {
        set_error(error_buf, error_buf_size, "parse_insert: INSERT 문이 아닙니다");
        return -1;
    }

    cursor = skip_spaces(cursor);
    if (!match_keyword(&cursor, "INTO"))
    {
        set_error(error_buf, error_buf_size, "parse_insert: INSERT 뒤에는 INTO가 와야 합니다");
        return -1;
    }

    cursor = skip_spaces(cursor);
    if (!parse_identifier(&cursor, out_command->table_name, sizeof(out_command->table_name)))
    {
        set_error(error_buf, error_buf_size, "parse_insert: INTO 뒤에 테이블 이름이 필요합니다");
        return -1;
    }

    cursor = skip_spaces(cursor);
    if (!match_keyword(&cursor, "VALUES"))
    {
        set_error(error_buf, error_buf_size, "parse_insert: 테이블 이름 뒤에는 VALUES가 와야 합니다");
        return -1;
    }

    if (parse_values(&cursor, out_command, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    return expect_end_or_semicolon(cursor, "parse_insert", error_buf, error_buf_size);
}

int parse_select(
    const char *sql,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *cursor = NULL;

    if (sql == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_select: 잘못된 인자입니다");
        return -1;
    }

    memset(out_command, 0, sizeof(*out_command));
    cursor = skip_spaces(sql);

    if (!match_keyword(&cursor, "SELECT"))
    {
        set_error(error_buf, error_buf_size, "parse_select: SELECT 문이 아닙니다");
        return -1;
    }

    cursor = skip_spaces(cursor);
    if (*cursor != '*')
    {
        set_error(error_buf, error_buf_size, "parse_select: 현재는 SELECT * 만 지원합니다");
        return -1;
    }

    out_command->select_all = true;
    out_command->column_count = 1;
    snprintf(out_command->columns[0], sizeof(out_command->columns[0]), "%s", "*");
    cursor++;

    cursor = skip_spaces(cursor);
    if (!match_keyword(&cursor, "FROM"))
    {
        set_error(error_buf, error_buf_size, "parse_select: SELECT * 뒤에는 FROM이 와야 합니다");
        return -1;
    }

    cursor = skip_spaces(cursor);
    if (!parse_identifier(&cursor, out_command->table_name, sizeof(out_command->table_name)))
    {
        set_error(error_buf, error_buf_size, "parse_select: FROM 뒤에 테이블 이름이 필요합니다");
        return -1;
    }

    return expect_end_or_semicolon(cursor, "parse_select", error_buf, error_buf_size);
}

int parse_sql(
    const char *sql,
    Command *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *cursor = NULL;
    InsertCommand insert_command;
    SelectCommand select_command;

    if (sql == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_sql: 잘못된 인자입니다");
        return -1;
    }

    memset(out_command, 0, sizeof(*out_command));
    cursor = skip_spaces(sql);

    if (match_keyword(&cursor, "INSERT"))
    {
        if (parse_insert(sql, &insert_command, error_buf, error_buf_size) != 0)
        {
            return -1;
        }

        out_command->type = CMD_INSERT;
        out_command->as.insert = insert_command;
        return 0;
    }

    if (match_keyword(&cursor, "SELECT"))
    {
        if (parse_select(sql, &select_command, error_buf, error_buf_size) != 0)
        {
            return -1;
        }

        out_command->type = CMD_SELECT;
        out_command->as.select = select_command;
        return 0;
    }

    set_error(error_buf, error_buf_size, "parse_sql: 지원하지 않는 SQL 문입니다");
    return -1;
}
