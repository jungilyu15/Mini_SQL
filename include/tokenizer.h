#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

#include "models.h"

/*
 * tokenizer는 SQL 문자열을 의미 없는 "token 조각"으로만 나눈다.
 * INSERT/SELECT 같은 문법적 의미 해석은 parser가 맡는다.
 */
typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_STAR,
    TOKEN_EQUAL,
    TOKEN_GREATER,
    TOKEN_LESS,
    TOKEN_SEMICOLON
} TokenType;

typedef struct {
    /*
     * token 문자열은 raw SQL 표현을 최대한 유지한다.
     * 예를 들어 문자열은 작은따옴표를 포함한 채로 보관한다.
     */
    TokenType type;
    char text[MAX_VALUE_LENGTH];
} Token;

typedef struct {
    /*
     * SQL 문장을 token 단위로 담는 동적 배열이다.
     * caller는 사용 후 free_token_list()로 정리한다.
     */
    size_t count;
    Token *items;
} TokenList;

/*
 * SQL 한 문장을 token 목록으로 나눈다.
 *
 * 정책:
 * - 공백은 token으로 만들지 않고 무시한다
 * - 키워드는 별도 type으로 바꾸지 않고 IDENTIFIER로 둔다
 * - 숫자는 부호 포함 정수 token까지만 지원한다
 * - 문자열은 작은따옴표를 포함한 raw token 그대로 보관한다
 */
int tokenize_sql(
    const char *sql,
    TokenList *out_tokens,
    char *error_buf,
    size_t error_buf_size
);

/* tokenize_sql이 만든 token 목록을 정리한다. */
void free_token_list(TokenList *tokens);

#endif
