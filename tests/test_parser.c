#include <stdio.h>
#include <string.h>

#include "parser.h"

/*
 * 개별 테스트를 공통 형식으로 실행한다.
 * 실패 사유는 각 테스트가 필요할 때 stderr에 추가로 남긴다.
 */
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

/* 가장 기본적인 INSERT 문을 raw token 기준으로 정상 파싱하는지 확인한다. */
static int test_parse_insert_basic(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT INTO users VALUES (1, 'kim', 24);", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(command.table_name, "users") != 0)
    {
        return 1;
    }
    if (command.value_count != 3)
    {
        return 1;
    }
    if (strcmp(command.values[0].raw, "1") != 0)
    {
        return 1;
    }
    if (strcmp(command.values[1].raw, "'kim'") != 0)
    {
        return 1;
    }
    if (strcmp(command.values[2].raw, "24") != 0)
    {
        return 1;
    }

    return 0;
}

/* 키워드 대소문자와 공백이 들쭉날쭉해도 INSERT를 읽을 수 있어야 한다. */
static int test_parse_insert_case_and_spaces(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("  inSeRt   InTo   users   values (  1  ,  'kim' ,   24  ) ; ", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(command.table_name, "users") != 0)
    {
        return 1;
    }
    if (command.value_count != 3)
    {
        return 1;
    }
    if (strcmp(command.values[1].raw, "'kim'") != 0)
    {
        return 1;
    }

    return 0;
}

/* 세미콜론이 없어도 한 문장으로 끝나면 허용하는 정책을 확인한다. */
static int test_parse_insert_without_semicolon(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT INTO users VALUES (1, 'kim', 24)", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.value_count != 3)
    {
        return 1;
    }

    return 0;
}

/* 작은따옴표 문자열 내부 공백은 raw token으로 유지되어야 한다. */
static int test_parse_insert_string_with_spaces(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT INTO users VALUES (1, 'kim min', 24);", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(command.values[1].raw, "'kim min'") != 0)
    {
        return 1;
    }

    return 0;
}

/* INTO가 빠진 문법은 실패해야 한다. */
static int test_parse_insert_missing_into(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT users VALUES (1, 'kim', 24);", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "INTO") == NULL;
}

/* VALUES가 빠진 문법은 실패해야 한다. */
static int test_parse_insert_missing_values(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT INTO users (1, 'kim', 24);", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "VALUES") == NULL;
}

/* 닫히지 않은 문자열은 실패해야 한다. */
static int test_parse_insert_unterminated_string(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT INTO users VALUES (1, 'kim, 24);", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "작은따옴표") == NULL;
}

/* 빈 값 슬롯은 허용하지 않는다. */
static int test_parse_insert_empty_value(void)
{
    InsertCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_insert("INSERT INTO users VALUES (1, , 24);", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "빈 값") == NULL;
}

/* parse_sql은 현재 INSERT만 감싸서 지원하고 SELECT는 아직 거부해야 한다. */
static int test_parse_sql_insert_only(void)
{
    Command command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_sql("INSERT INTO users VALUES (1, 'kim', 24);", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.type != CMD_INSERT)
    {
        return 1;
    }
    if (strcmp(command.as.insert.table_name, "users") != 0)
    {
        return 1;
    }

    if (parse_sql("SELECT * FROM users;", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "SELECT는 아직") == NULL;
}

int main(void)
{
    int failures = 0;

    failures += run_test("parse insert basic", test_parse_insert_basic);
    failures += run_test("parse insert case and spaces", test_parse_insert_case_and_spaces);
    failures += run_test("parse insert without semicolon", test_parse_insert_without_semicolon);
    failures += run_test("parse insert string with spaces", test_parse_insert_string_with_spaces);
    failures += run_test("parse insert missing into", test_parse_insert_missing_into);
    failures += run_test("parse insert missing values", test_parse_insert_missing_values);
    failures += run_test("parse insert unterminated string", test_parse_insert_unterminated_string);
    failures += run_test("parse insert empty value", test_parse_insert_empty_value);
    failures += run_test("parse sql insert only", test_parse_sql_insert_only);

    if (failures != 0)
    {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all parser tests passed\n");
    return 0;
}
