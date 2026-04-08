#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 공통 오류 메시지를 안전하게 기록한다. */
static void set_error(char *error_buf, size_t error_buf_size, const char *message)
{
    if (error_buf != NULL && error_buf_size > 0)
    {
        snprintf(error_buf, error_buf_size, "%s", message);
    }
}

/* SQL 문자열에서 공백 구간은 token으로 만들지 않고 건너뛴다. */
static const char *skip_spaces(const char *cursor)
{
    while (*cursor != '\0' && isspace((unsigned char)*cursor))
    {
        cursor++;
    }

    return cursor;
}

/*
 * TokenList 뒤에 token 하나를 추가한다.
 * token 배열은 필요한 만큼 realloc으로 늘린다.
 */
static int append_token(
    TokenList *tokens,
    TokenType type,
    const char *start,
    size_t length,
    char *error_buf,
    size_t error_buf_size
)
{
    Token *new_items = NULL;

    if (tokens == NULL || start == NULL)
    {
        set_error(error_buf, error_buf_size, "tokenizer: 잘못된 token 추가 인자입니다");
        return -1;
    }

    if (length >= MAX_VALUE_LENGTH)
    {
        set_error(error_buf, error_buf_size, "tokenizer: token 길이가 너무 깁니다");
        return -1;
    }

    new_items = (Token *)realloc(tokens->items, sizeof(Token) * (tokens->count + 1));
    if (new_items == NULL)
    {
        set_error(error_buf, error_buf_size, "tokenizer: token 목록을 확장할 수 없습니다");
        return -1;
    }

    tokens->items = new_items;
    tokens->items[tokens->count].type = type;
    memcpy(tokens->items[tokens->count].text, start, length);
    tokens->items[tokens->count].text[length] = '\0';
    tokens->count++;
    return 0;
}

/* 식별자나 키워드처럼 문자로 시작하는 token 하나를 읽는다. */
static int tokenize_identifier(
    const char **cursor,
    TokenList *tokens,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *start = *cursor;
    const char *sql = *cursor;

    while (isalnum((unsigned char)*sql) || *sql == '_')
    {
        sql++;
    }

    if (append_token(
            tokens,
            TOKEN_IDENTIFIER,
            start,
            (size_t)(sql - start),
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    *cursor = sql;
    return 0;
}

/*
 * 부호 포함 정수 token 하나를 읽는다.
 * tokenizer 단계에서는 정수 범위 검증까지 하지 않고 모양만 token으로 보관한다.
 */
static int tokenize_number(
    const char **cursor,
    TokenList *tokens,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *start = *cursor;
    const char *sql = *cursor;

    if (*sql == '+' || *sql == '-')
    {
        sql++;
    }

    while (isdigit((unsigned char)*sql))
    {
        sql++;
    }

    if (append_token(
            tokens,
            TOKEN_NUMBER,
            start,
            (size_t)(sql - start),
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    *cursor = sql;
    return 0;
}

/*
 * 작은따옴표 문자열 token을 읽는다.
 * 이번 단계에서는 escape를 해석하지 않고 닫는 작은따옴표까지만 raw token으로 유지한다.
 */
static int tokenize_string(
    const char **cursor,
    TokenList *tokens,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *start = *cursor;
    const char *sql = *cursor + 1;

    while (*sql != '\0' && *sql != '\'')
    {
        sql++;
    }

    if (*sql != '\'')
    {
        set_error(error_buf, error_buf_size, "tokenizer: 닫히지 않은 작은따옴표 문자열이 있습니다");
        return -1;
    }

    sql++;
    if (append_token(
            tokens,
            TOKEN_STRING,
            start,
            (size_t)(sql - start),
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    *cursor = sql;
    return 0;
}

/* 쉼표, 괄호, 비교 연산자 같은 1글자 기호 token을 추가한다. */
static int append_single_char_token(
    TokenList *tokens,
    TokenType type,
    char ch,
    char *error_buf,
    size_t error_buf_size
)
{
    return append_token(tokens, type, &ch, 1, error_buf, error_buf_size);
}

int tokenize_sql(
    const char *sql,
    TokenList *out_tokens,
    char *error_buf,
    size_t error_buf_size
)
{
    const char *cursor = NULL;

    if (sql == NULL || out_tokens == NULL)
    {
        set_error(error_buf, error_buf_size, "tokenizer: 잘못된 인자입니다");
        return -1;
    }

    out_tokens->count = 0;
    out_tokens->items = NULL;
    cursor = sql;

    while (1)
    {
        cursor = skip_spaces(cursor);

        if (*cursor == '\0')
        {
            break;
        }

        if (isalpha((unsigned char)*cursor) || *cursor == '_')
        {
            if (tokenize_identifier(&cursor, out_tokens, error_buf, error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            continue;
        }

        if (*cursor == '\'' )
        {
            if (tokenize_string(&cursor, out_tokens, error_buf, error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            continue;
        }

        if (isdigit((unsigned char)*cursor) ||
            ((*cursor == '+' || *cursor == '-') && isdigit((unsigned char)*(cursor + 1))))
        {
            if (tokenize_number(&cursor, out_tokens, error_buf, error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            continue;
        }

        if (*cursor == ',')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_COMMA,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == '(')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_LPAREN,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == ')')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_RPAREN,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == '*')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_STAR,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == '=')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_EQUAL,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == '>')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_GREATER,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == '<')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_LESS,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        if (*cursor == ';')
        {
            if (append_single_char_token(
                    out_tokens,
                    TOKEN_SEMICOLON,
                    *cursor,
                    error_buf,
                    error_buf_size) != 0)
            {
                free_token_list(out_tokens);
                return -1;
            }
            cursor++;
            continue;
        }

        snprintf(
            error_buf,
            error_buf_size,
            "tokenizer: 지원하지 않는 문자 '%c'가 있습니다",
            *cursor);
        free_token_list(out_tokens);
        return -1;
    }

    return 0;
}

void free_token_list(TokenList *tokens)
{
    if (tokens == NULL)
    {
        return;
    }

    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
}
