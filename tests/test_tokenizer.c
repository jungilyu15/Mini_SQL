#include <stdio.h>
#include <string.h>

#include "tokenizer.h"

/* 개별 테스트를 공통 형식으로 실행한다. */
static int run_test(const char *name, int (*test_fn)(void))
{
    int result = test_fn();

    if (result == 0)
    {
        printf("[PASS] %s\n", name);
    }
    else
    {
        printf("[FAIL] %s\n", name);
    }

    return result;
}

/* 가장 기본적인 INSERT 문이 token 목록으로 안정적으로 나뉘는지 확인한다. */
static int test_tokenize_insert_basic(void)
{
    TokenList tokens;
    char error_buf[MAX_ERROR_LENGTH];

    if (tokenize_sql("INSERT INTO users VALUES (1, 'kim', 24);", &tokens, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (tokens.count != 12)
    {
        free_token_list(&tokens);
        return 1;
    }

    if (tokens.items[0].type != TOKEN_IDENTIFIER || strcmp(tokens.items[0].text, "INSERT") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }
    if (tokens.items[4].type != TOKEN_LPAREN || strcmp(tokens.items[4].text, "(") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }
    if (tokens.items[5].type != TOKEN_NUMBER || strcmp(tokens.items[5].text, "1") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }
    if (tokens.items[7].type != TOKEN_STRING || strcmp(tokens.items[7].text, "'kim'") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }
    if (tokens.items[11].type != TOKEN_SEMICOLON || strcmp(tokens.items[11].text, ";") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }

    free_token_list(&tokens);
    return 0;
}

/* 공백은 버리고 token 문자열 자체는 유지해야 한다. */
static int test_tokenize_ignores_spaces(void)
{
    TokenList tokens;
    char error_buf[MAX_ERROR_LENGTH];

    if (tokenize_sql("  SELECT   name ,  age   FROM users ; ", &tokens, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (tokens.count != 7)
    {
        free_token_list(&tokens);
        return 1;
    }

    if (strcmp(tokens.items[0].text, "SELECT") != 0 ||
        strcmp(tokens.items[1].text, "name") != 0 ||
        strcmp(tokens.items[2].text, ",") != 0 ||
        strcmp(tokens.items[3].text, "age") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }

    free_token_list(&tokens);
    return 0;
}

/* 작은따옴표 문자열 내부 공백은 token 안에 그대로 남아야 한다. */
static int test_tokenize_string_keeps_quotes(void)
{
    TokenList tokens;
    char error_buf[MAX_ERROR_LENGTH];

    if (tokenize_sql("SELECT * FROM users WHERE name = 'kim min';", &tokens, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(tokens.items[7].text, "'kim min'") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }

    free_token_list(&tokens);
    return 0;
}

/* parser가 '=' 미지원 메시지를 낼 수 있도록 '>' token도 따로 분리해야 한다. */
static int test_tokenize_greater_operator(void)
{
    TokenList tokens;
    char error_buf[MAX_ERROR_LENGTH];

    if (tokenize_sql("SELECT * FROM users WHERE age > 10;", &tokens, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (tokens.items[6].type != TOKEN_GREATER || strcmp(tokens.items[6].text, ">") != 0)
    {
        free_token_list(&tokens);
        return 1;
    }

    free_token_list(&tokens);
    return 0;
}

/* 닫히지 않은 작은따옴표 문자열은 tokenizer 단계에서 실패해야 한다. */
static int test_tokenize_unterminated_string(void)
{
    TokenList tokens;
    char error_buf[MAX_ERROR_LENGTH];

    if (tokenize_sql("INSERT INTO users VALUES (1, 'kim);", &tokens, error_buf, sizeof(error_buf)) == 0)
    {
        free_token_list(&tokens);
        return 1;
    }

    return strstr(error_buf, "작은따옴표") == NULL;
}

/* 이번 단계에서 지원하지 않는 문자가 나오면 즉시 실패해야 한다. */
static int test_tokenize_unsupported_character(void)
{
    TokenList tokens;
    char error_buf[MAX_ERROR_LENGTH];

    if (tokenize_sql("SELECT @ FROM users;", &tokens, error_buf, sizeof(error_buf)) == 0)
    {
        free_token_list(&tokens);
        return 1;
    }

    return strstr(error_buf, "지원하지 않는 문자") == NULL;
}

int main(void)
{
    int failures = 0;

    failures += run_test("tokenize insert basic", test_tokenize_insert_basic);
    failures += run_test("tokenize ignores spaces", test_tokenize_ignores_spaces);
    failures += run_test("tokenize string keeps quotes", test_tokenize_string_keeps_quotes);
    failures += run_test("tokenize greater operator", test_tokenize_greater_operator);
    failures += run_test("tokenize unterminated string", test_tokenize_unterminated_string);
    failures += run_test("tokenize unsupported character", test_tokenize_unsupported_character);

    if (failures != 0)
    {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all tokenizer tests passed\n");
    return 0;
}
