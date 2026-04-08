#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "tokenizer.h"

/*
 * parser 단계에서는 값의 실제 타입을 해석하지 않는다.
 * tokenizer가 만든 raw token 목록을 읽어 문법 구조만 해석하고,
 * 숫자/문자열의 실제 타입 판정은 이후 schema/cast 단계로 넘긴다.
 */

typedef struct {
    const TokenList *tokens;
    size_t index;
} TokenCursor;

/* 공통 오류 메시지를 기록하는 helper다. */
static void set_error(char *error_buf, size_t error_buf_size, const char *message)
{
    if (error_buf != NULL && error_buf_size > 0)
    {
        snprintf(error_buf, error_buf_size, "%s", message);
    }
}

/* 현재 parser가 바라보고 있는 token 하나를 돌려준다. */
static const Token *peek_token(const TokenCursor *cursor)
{
    if (cursor == NULL || cursor->tokens == NULL || cursor->index >= cursor->tokens->count)
    {
        return NULL;
    }

    return &cursor->tokens->items[cursor->index];
}

/*
 * tokenizer는 키워드를 별도 type으로 바꾸지 않는다.
 * 따라서 parser가 IDENTIFIER token 문자열을 보고 INSERT/SELECT/FROM 같은
 * 키워드인지 직접 판정한다.
 */
static int token_is_keyword(const Token *token, const char *keyword)
{
    size_t i = 0;

    if (token == NULL || keyword == NULL || token->type != TOKEN_IDENTIFIER)
    {
        return 0;
    }

    while (keyword[i] != '\0' && token->text[i] != '\0')
    {
        if (toupper((unsigned char)token->text[i]) != (unsigned char)keyword[i])
        {
            return 0;
        }
        i++;
    }

    return keyword[i] == '\0' && token->text[i] == '\0';
}

/* 현재 token이 특정 키워드면 소비하고 성공을 돌려준다. */
static int match_keyword(TokenCursor *cursor, const char *keyword)
{
    const Token *token = peek_token(cursor);

    if (!token_is_keyword(token, keyword))
    {
        return 0;
    }

    cursor->index++;
    return 1;
}

/* 현재 token이 특정 type이면 소비하고 성공을 돌려준다. */
static int match_token_type(TokenCursor *cursor, TokenType type)
{
    const Token *token = peek_token(cursor);

    if (token == NULL || token->type != type)
    {
        return 0;
    }

    cursor->index++;
    return 1;
}

/* 현재 token 하나를 out 버퍼로 복사한다. */
static int copy_token_text(
    const Token *token,
    char *out_text,
    size_t out_text_size,
    char *error_buf,
    size_t error_buf_size,
    const char *context_name,
    const char *missing_message
)
{
    size_t length = 0;

    if (token == NULL)
    {
        set_error(error_buf, error_buf_size, missing_message);
        return -1;
    }

    length = strlen(token->text);
    if (length >= out_text_size)
    {
        snprintf(error_buf, error_buf_size, "%s: token 길이가 너무 깁니다", context_name);
        return -1;
    }

    memcpy(out_text, token->text, length + 1);
    return 0;
}

/* 테이블 이름이나 컬럼 이름처럼 식별자 token 하나를 읽는다. */
static int parse_identifier(
    TokenCursor *cursor,
    char *out_name,
    size_t out_name_size,
    char *error_buf,
    size_t error_buf_size,
    const char *context_name,
    const char *missing_message
)
{
    const Token *token = peek_token(cursor);

    if (token == NULL || token->type != TOKEN_IDENTIFIER)
    {
        set_error(error_buf, error_buf_size, missing_message);
        return -1;
    }

    if (copy_token_text(
            token,
            out_name,
            out_name_size,
            error_buf,
            error_buf_size,
            context_name,
            missing_message) != 0)
    {
        return -1;
    }

    cursor->index++;
    return 0;
}

/*
 * INSERT 값이나 WHERE 값으로 사용할 raw token 하나를 읽는다.
 * tokenizer가 문자열/숫자/식별자를 구분해 두므로 parser는 허용 가능한 type만 통과시킨다.
 */
static int parse_value_token(
    TokenCursor *cursor,
    char *out_token,
    size_t out_token_size,
    char *error_buf,
    size_t error_buf_size,
    const char *context_name,
    const char *missing_message
)
{
    const Token *token = peek_token(cursor);

    if (token == NULL)
    {
        set_error(error_buf, error_buf_size, missing_message);
        return -1;
    }

    if (token->type != TOKEN_IDENTIFIER &&
        token->type != TOKEN_NUMBER &&
        token->type != TOKEN_STRING)
    {
        set_error(error_buf, error_buf_size, missing_message);
        return -1;
    }

    if (copy_token_text(
            token,
            out_token,
            out_token_size,
            error_buf,
            error_buf_size,
            context_name,
            missing_message) != 0)
    {
        return -1;
    }

    cursor->index++;
    return 0;
}

/*
 * SELECT의 컬럼 목록 하나를 읽어 columns 배열에 추가한다.
 * 현재 단계에서는 단순 식별자 컬럼명만 허용한다.
 */
static int append_select_column(
    TokenCursor *cursor,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    if (out_command->column_count >= MAX_COLUMNS)
    {
        set_error(error_buf, error_buf_size, "parse_select: 컬럼 개수가 최대치를 초과합니다");
        return -1;
    }

    if (parse_identifier(
            cursor,
            out_command->columns[out_command->column_count],
            sizeof(out_command->columns[out_command->column_count]),
            error_buf,
            error_buf_size,
            "parse_select",
            "parse_select: 잘못된 컬럼 이름입니다") != 0)
    {
        return -1;
    }

    out_command->column_count++;
    return 0;
}

/*
 * SELECT 뒤의 컬럼 목록을 읽는다.
 * "*" 또는 "name, age" 같은 단순 명시적 컬럼 목록만 지원한다.
 */
static int parse_select_columns(
    TokenCursor *cursor,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    if (match_token_type(cursor, TOKEN_STAR))
    {
        out_command->select_all = true;
        out_command->column_count = 1;
        snprintf(out_command->columns[0], sizeof(out_command->columns[0]), "%s", "*");
        return 0;
    }

    out_command->select_all = false;
    out_command->column_count = 0;

    while (1)
    {
        if (append_select_column(cursor, out_command, error_buf, error_buf_size) != 0)
        {
            return -1;
        }

        if (!match_token_type(cursor, TOKEN_COMMA))
        {
            break;
        }

        if (peek_token(cursor) == NULL || peek_token(cursor)->type == TOKEN_COMMA)
        {
            set_error(error_buf, error_buf_size, "parse_select: 잘못된 컬럼 목록입니다");
            return -1;
        }
    }

    return 0;
}

/*
 * VALUES (...) 안의 값 목록을 읽는다.
 * tokenizer가 이미 문자열/기호를 token으로 분리했으므로 parser는 token 순서만 검증한다.
 */
static int parse_values(
    TokenCursor *cursor,
    InsertCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    if (!match_token_type(cursor, TOKEN_LPAREN))
    {
        set_error(error_buf, error_buf_size, "parse_insert: VALUES 뒤에 '('가 필요합니다");
        return -1;
    }

    if (match_token_type(cursor, TOKEN_RPAREN))
    {
        set_error(error_buf, error_buf_size, "parse_insert: 빈 VALUES 목록은 허용하지 않습니다");
        return -1;
    }

    while (1)
    {
        if (out_command->value_count >= MAX_COLUMNS)
        {
            set_error(error_buf, error_buf_size, "parse_insert: 값 개수가 최대치를 초과합니다");
            return -1;
        }

        if (parse_value_token(
                cursor,
                out_command->values[out_command->value_count].raw,
                sizeof(out_command->values[out_command->value_count].raw),
                error_buf,
                error_buf_size,
                "parse_insert",
                "parse_insert: 빈 값은 허용하지 않습니다") != 0)
        {
            return -1;
        }

        out_command->value_count++;

        if (match_token_type(cursor, TOKEN_RPAREN))
        {
            return 0;
        }

        if (!match_token_type(cursor, TOKEN_COMMA))
        {
            set_error(error_buf, error_buf_size, "parse_insert: 닫히지 않은 VALUES 목록입니다");
            return -1;
        }

        if (peek_token(cursor) == NULL ||
            peek_token(cursor)->type == TOKEN_COMMA ||
            peek_token(cursor)->type == TOKEN_RPAREN)
        {
            set_error(error_buf, error_buf_size, "parse_insert: 빈 값은 허용하지 않습니다");
            return -1;
        }
    }
}

/*
 * 마지막 세미콜론은 선택적으로 허용한다.
 * 대신 세미콜론 뒤에 다른 문장이 이어지는 것은 아직 지원하지 않는다.
 */
static int expect_end_or_semicolon(
    TokenCursor *cursor,
    const char *context_name,
    char *error_buf,
    size_t error_buf_size
)
{
    match_token_type(cursor, TOKEN_SEMICOLON);

    if (peek_token(cursor) != NULL)
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

/*
 * SELECT의 optional WHERE 절을 읽는다.
 * 현재 범위에서는 "WHERE <column> = <value>" 단일 조건만 허용한다.
 */
static int parse_select_where_clause(
    TokenCursor *cursor,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    out_command->where.has_where = false;

    if (!match_keyword(cursor, "WHERE"))
    {
        return 0;
    }

    out_command->where.has_where = true;

    if (parse_identifier(
            cursor,
            out_command->where.column,
            sizeof(out_command->where.column),
            error_buf,
            error_buf_size,
            "parse_select",
            "parse_select: WHERE 뒤에 컬럼 이름이 필요합니다") != 0)
    {
        return -1;
    }

    if (!match_token_type(cursor, TOKEN_EQUAL))
    {
        set_error(error_buf, error_buf_size, "parse_select: WHERE는 '=' 비교만 지원합니다");
        return -1;
    }

    if (parse_value_token(
            cursor,
            out_command->where.value.raw,
            sizeof(out_command->where.value.raw),
            error_buf,
            error_buf_size,
            "parse_select",
            "parse_select: WHERE 값이 필요합니다") != 0)
    {
        return -1;
    }

    return 0;
}

/* tokenize된 token 목록으로 INSERT 문 하나를 해석한다. */
static int parse_insert_tokens(
    const TokenList *tokens,
    InsertCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    TokenCursor cursor;

    if (tokens == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_insert: 잘못된 인자입니다");
        return -1;
    }

    memset(out_command, 0, sizeof(*out_command));
    cursor.tokens = tokens;
    cursor.index = 0;

    if (!match_keyword(&cursor, "INSERT"))
    {
        set_error(error_buf, error_buf_size, "parse_insert: INSERT 문이 아닙니다");
        return -1;
    }

    if (!match_keyword(&cursor, "INTO"))
    {
        set_error(error_buf, error_buf_size, "parse_insert: INSERT 뒤에는 INTO가 와야 합니다");
        return -1;
    }

    if (parse_identifier(
            &cursor,
            out_command->table_name,
            sizeof(out_command->table_name),
            error_buf,
            error_buf_size,
            "parse_insert",
            "parse_insert: INTO 뒤에 테이블 이름이 필요합니다") != 0)
    {
        return -1;
    }

    if (!match_keyword(&cursor, "VALUES"))
    {
        set_error(error_buf, error_buf_size, "parse_insert: 테이블 이름 뒤에는 VALUES가 와야 합니다");
        return -1;
    }

    if (parse_values(&cursor, out_command, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    return expect_end_or_semicolon(&cursor, "parse_insert", error_buf, error_buf_size);
}

/* tokenize된 token 목록으로 SELECT 문 하나를 해석한다. */
static int parse_select_tokens(
    const TokenList *tokens,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    TokenCursor cursor;

    if (tokens == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_select: 잘못된 인자입니다");
        return -1;
    }

    memset(out_command, 0, sizeof(*out_command));
    cursor.tokens = tokens;
    cursor.index = 0;

    if (!match_keyword(&cursor, "SELECT"))
    {
        set_error(error_buf, error_buf_size, "parse_select: SELECT 문이 아닙니다");
        return -1;
    }

    if (parse_select_columns(&cursor, out_command, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (!match_keyword(&cursor, "FROM"))
    {
        set_error(error_buf, error_buf_size, "parse_select: 컬럼 목록 뒤에는 FROM이 와야 합니다");
        return -1;
    }

    if (parse_identifier(
            &cursor,
            out_command->table_name,
            sizeof(out_command->table_name),
            error_buf,
            error_buf_size,
            "parse_select",
            "parse_select: FROM 뒤에 테이블 이름이 필요합니다") != 0)
    {
        return -1;
    }

    if (parse_select_where_clause(&cursor, out_command, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    return expect_end_or_semicolon(&cursor, "parse_select", error_buf, error_buf_size);
}

int parse_insert(
    const char *sql,
    InsertCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    TokenList tokens;
    int result = 0;

    if (sql == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_insert: 잘못된 인자입니다");
        return -1;
    }

    if (tokenize_sql(sql, &tokens, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    result = parse_insert_tokens(&tokens, out_command, error_buf, error_buf_size);
    free_token_list(&tokens);
    return result;
}

int parse_select(
    const char *sql,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    TokenList tokens;
    int result = 0;

    if (sql == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_select: 잘못된 인자입니다");
        return -1;
    }

    if (tokenize_sql(sql, &tokens, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    result = parse_select_tokens(&tokens, out_command, error_buf, error_buf_size);
    free_token_list(&tokens);
    return result;
}

int parse_sql(
    const char *sql,
    Command *out_command,
    char *error_buf,
    size_t error_buf_size
)
{
    TokenList tokens;
    int result = -1;

    if (sql == NULL || out_command == NULL)
    {
        set_error(error_buf, error_buf_size, "parse_sql: 잘못된 인자입니다");
        return -1;
    }

    memset(out_command, 0, sizeof(*out_command));

    if (tokenize_sql(sql, &tokens, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (token_is_keyword(peek_token(&(TokenCursor){.tokens = &tokens, .index = 0}), "INSERT"))
    {
        InsertCommand insert_command;

        result = parse_insert_tokens(&tokens, &insert_command, error_buf, error_buf_size);
        if (result == 0)
        {
            out_command->type = CMD_INSERT;
            out_command->as.insert = insert_command;
        }

        free_token_list(&tokens);
        return result;
    }

    if (token_is_keyword(peek_token(&(TokenCursor){.tokens = &tokens, .index = 0}), "SELECT"))
    {
        SelectCommand select_command;

        result = parse_select_tokens(&tokens, &select_command, error_buf, error_buf_size);
        if (result == 0)
        {
            out_command->type = CMD_SELECT;
            out_command->as.select = select_command;
        }

        free_token_list(&tokens);
        return result;
    }

    free_token_list(&tokens);
    set_error(error_buf, error_buf_size, "parse_sql: 지원하지 않는 SQL 문입니다");
    return -1;
}
