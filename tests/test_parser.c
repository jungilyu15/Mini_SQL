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

/* parse_sql은 현재 INSERT와 SELECT *를 분기해 지원해야 한다. */
static int test_parse_select_basic(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT * FROM users;", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (!command.select_all)
    {
        return 1;
    }
    if (strcmp(command.table_name, "users") != 0)
    {
        return 1;
    }
    if (command.column_count != 1)
    {
        return 1;
    }
    if (strcmp(command.columns[0], "*") != 0)
    {
        return 1;
    }

    return 0;
}

/* 키워드 대소문자와 공백이 들쭉날쭉해도 SELECT *를 읽을 수 있어야 한다. */
static int test_parse_select_case_and_spaces(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("  seLEct   *   FrOm   users   ; ", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(command.table_name, "users") != 0)
    {
        return 1;
    }
    if (!command.select_all)
    {
        return 1;
    }

    return 0;
}

/* 마지막 세미콜론이 없어도 한 문장으로 끝나면 허용해야 한다. */
static int test_parse_select_without_semicolon(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT * FROM users", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(command.columns[0], "*") != 0)
    {
        return 1;
    }

    return 0;
}

/* 명시적 컬럼 목록도 순서대로 읽을 수 있어야 한다. */
static int test_parse_select_named_columns(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT name, age FROM users;", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.select_all)
    {
        return 1;
    }
    if (command.column_count != 2)
    {
        return 1;
    }
    if (strcmp(command.columns[0], "name") != 0)
    {
        return 1;
    }
    if (strcmp(command.columns[1], "age") != 0)
    {
        return 1;
    }

    return 0;
}

/* FROM이 빠지면 실패해야 한다. */
static int test_parse_select_missing_from(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT * users;", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "FROM") == NULL;
}

/* SELECT * 뒤에 단일 WHERE 조건 하나를 읽을 수 있어야 한다. */
static int test_parse_select_where_star(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT * FROM users WHERE id = 1;", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (!command.select_all)
    {
        return 1;
    }
    if (!command.where.has_where)
    {
        return 1;
    }
    if (strcmp(command.where.column, "id") != 0)
    {
        return 1;
    }
    if (strcmp(command.where.value.raw, "1") != 0)
    {
        return 1;
    }

    return 0;
}

/* 명시적 컬럼 SELECT에도 단일 WHERE 조건을 함께 붙일 수 있어야 한다. */
static int test_parse_select_where_named_columns(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT name, age FROM users WHERE name = 'kim';", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.select_all)
    {
        return 1;
    }
    if (command.column_count != 2)
    {
        return 1;
    }
    if (!command.where.has_where)
    {
        return 1;
    }
    if (strcmp(command.where.column, "name") != 0)
    {
        return 1;
    }
    if (strcmp(command.where.value.raw, "'kim'") != 0)
    {
        return 1;
    }

    return 0;
}

/* WHERE는 "=" 하나만 지원하므로 다른 비교 연산자는 실패해야 한다. */
static int test_parse_select_where_invalid_operator(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT * FROM users WHERE age > 10;", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "=") == NULL;
}

/* WHERE 뒤에 AND 같은 추가 조건이 오면 최소 범위를 벗어나므로 실패해야 한다. */
static int test_parse_select_where_extra_condition_not_supported(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT * FROM users WHERE age = 10 AND id = 1;", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "추가 내용") == NULL;
}

/* 컬럼 목록에 빈 슬롯이 있으면 실패해야 한다. */
static int test_parse_select_invalid_column_list(void)
{
    SelectCommand command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_select("SELECT name, FROM users;", &command, error_buf, sizeof(error_buf)) == 0)
    {
        return 1;
    }

    return strstr(error_buf, "컬럼") == NULL;
}

/* parse_sql은 현재 INSERT와 SELECT *를 둘 다 분기해 지원해야 한다. */
static int test_parse_sql_insert_and_select(void)
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

    if (parse_sql("SELECT * FROM users;", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.type != CMD_SELECT)
    {
        return 1;
    }
    if (!command.as.select.select_all)
    {
        return 1;
    }
    if (strcmp(command.as.select.table_name, "users") != 0)
    {
        return 1;
    }

    return 0;
}

/* parse_sql은 명시적 컬럼 SELECT도 함께 분기해야 한다. */
static int test_parse_sql_named_select(void)
{
    Command command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_sql("SELECT name, age FROM users;", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.type != CMD_SELECT)
    {
        return 1;
    }
    if (command.as.select.select_all)
    {
        return 1;
    }
    if (command.as.select.column_count != 2)
    {
        return 1;
    }
    if (strcmp(command.as.select.columns[0], "name") != 0)
    {
        return 1;
    }
    if (strcmp(command.as.select.columns[1], "age") != 0)
    {
        return 1;
    }

    return 0;
}

/* parse_sql도 WHERE가 포함된 SELECT를 정상 분기해야 한다. */
static int test_parse_sql_select_with_where(void)
{
    Command command;
    char error_buf[MAX_ERROR_LENGTH];

    if (parse_sql("SELECT name, age FROM users WHERE name = 'kim';", &command, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (command.type != CMD_SELECT)
    {
        return 1;
    }
    if (!command.as.select.where.has_where)
    {
        return 1;
    }
    if (strcmp(command.as.select.where.column, "name") != 0)
    {
        return 1;
    }
    if (strcmp(command.as.select.where.value.raw, "'kim'") != 0)
    {
        return 1;
    }

    return 0;
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
    failures += run_test("parse select basic", test_parse_select_basic);
    failures += run_test("parse select case and spaces", test_parse_select_case_and_spaces);
    failures += run_test("parse select without semicolon", test_parse_select_without_semicolon);
    failures += run_test("parse select named columns", test_parse_select_named_columns);
    failures += run_test("parse select missing from", test_parse_select_missing_from);
    failures += run_test("parse select where star", test_parse_select_where_star);
    failures += run_test("parse select where named columns", test_parse_select_where_named_columns);
    failures += run_test("parse select where invalid operator", test_parse_select_where_invalid_operator);
    failures += run_test(
        "parse select where extra condition not supported",
        test_parse_select_where_extra_condition_not_supported);
    failures += run_test("parse select invalid column list", test_parse_select_invalid_column_list);
    failures += run_test("parse sql insert and select", test_parse_sql_insert_and_select);
    failures += run_test("parse sql named select", test_parse_sql_named_select);
    failures += run_test("parse sql select with where", test_parse_sql_select_with_where);

    if (failures != 0)
    {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all parser tests passed\n");
    return 0;
}
