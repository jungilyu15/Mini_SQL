#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "schema_manager.h"

static int run_test(const char *name, int (*test_fn)(void)) {
    int result = test_fn();

    if (result == 0) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
    }

    return result;
}

static int expect_successful_users_schema(void) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("users", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(schema.table_name, "users") != 0) {
        return 1;
    }
    if (schema.column_count != 3) {
        return 1;
    }
    if (strcmp(schema.columns[0].name, "id") != 0 || schema.columns[0].type != COL_INT) {
        return 1;
    }
    if (strcmp(schema.columns[1].name, "name") != 0 || schema.columns[1].type != COL_TEXT) {
        return 1;
    }
    if (strcmp(schema.columns[2].name, "age") != 0 || schema.columns[2].type != COL_INT) {
        return 1;
    }

    return 0;
}

static int expect_successful_schema_with_spaces(void) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("with_spaces", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (schema.column_count != 3) {
        return 1;
    }
    if (strcmp(schema.columns[0].name, "id") != 0 || schema.columns[0].type != COL_INT) {
        return 1;
    }
    if (strcmp(schema.columns[1].name, "name") != 0 || schema.columns[1].type != COL_TEXT) {
        return 1;
    }

    return 0;
}

static int expect_missing_file_error(void) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("does_not_exist", &schema, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (strstr(error_buf, "does_not_exist") == NULL) {
        return 1;
    }

    return 0;
}

static int expect_invalid_format_error(const char *table_name, const char *expected_text) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema(table_name, &schema, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (strstr(error_buf, expected_text) == NULL) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    return 0;
}

static int test_missing_colon(void) {
    return expect_invalid_format_error("missing_colon", "형식이 잘못되었습니다");
}

static int test_missing_name(void) {
    return expect_invalid_format_error("missing_name", "빈 column/type");
}

static int test_missing_type(void) {
    return expect_invalid_format_error("missing_type", "빈 column/type");
}

static int test_unsupported_type(void) {
    return expect_invalid_format_error("unsupported_type", "지원하지 않는 타입");
}

static int test_empty_schema(void) {
    return expect_invalid_format_error("empty", "유효한 column이 없습니다");
}

static int test_too_many_columns(void) {
    return expect_invalid_format_error("too_many_columns", "최대치");
}

int main(void) {
    char original_cwd[1024];
    int failures = 0;

    if (getcwd(original_cwd, sizeof(original_cwd)) == NULL) {
        perror("getcwd");
        return 1;
    }

    if (chdir("tests/fixtures") != 0) {
        perror("chdir");
        return 1;
    }

    failures += run_test("valid schema", expect_successful_users_schema);
    failures += run_test("schema with spaces", expect_successful_schema_with_spaces);
    failures += run_test("missing file", expect_missing_file_error);
    failures += run_test("missing colon", test_missing_colon);
    failures += run_test("missing name", test_missing_name);
    failures += run_test("missing type", test_missing_type);
    failures += run_test("unsupported type", test_unsupported_type);
    failures += run_test("empty schema", test_empty_schema);
    failures += run_test("too many columns", test_too_many_columns);

    if (chdir(original_cwd) != 0) {
        perror("chdir");
        return 1;
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all schema_manager tests passed\n");
    return 0;
}
