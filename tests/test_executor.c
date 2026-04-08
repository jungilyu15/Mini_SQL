#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "executor.h"

/*
 * 각 테스트 함수를 공통 형식으로 실행한다.
 * 실패 이유가 필요하면 각 테스트가 stderr에 추가로 출력한다.
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

/*
 * 이전 테스트 실행에서 남았을 수 있는 CSV 산출물을 지운다.
 * 파일이 원래 없으면 정상으로 간주한다.
 */
static int remove_if_exists(const char *path)
{
    if (unlink(path) != 0 && access(path, F_OK) == 0)
    {
        perror("unlink");
        return -1;
    }

    return 0;
}

/*
 * executor 테스트가 생성하는 CSV 파일을 정리한다.
 * fixture로 저장된 입력 파일은 건드리지 않는다.
 */
static int cleanup_generated_executor_files(void)
{
    return remove_if_exists("data/executor_insert.csv");
}

/*
 * 파일 내용을 그대로 읽어서 저장 결과를 확인한다.
 * INSERT 실행 경로가 실제로 storage까지 연결되는지 검증할 때 사용한다.
 */
static int read_file_to_buffer(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file = fopen(path, "r");
    size_t bytes_read = 0;

    if (file == NULL)
    {
        perror("fopen");
        return -1;
    }

    bytes_read = fread(buffer, 1, buffer_size - 1, file);
    if (ferror(file))
    {
        fclose(file);
        return -1;
    }

    buffer[bytes_read] = '\0';
    fclose(file);
    return 0;
}

/* INSERT command를 빠르게 만드는 helper다. */
static Command make_insert_command(const char *table_name, const char *v1, const char *v2, const char *v3)
{
    Command command;

    memset(&command, 0, sizeof(command));
    command.type = CMD_INSERT;
    snprintf(command.as.insert.table_name, sizeof(command.as.insert.table_name), "%s", table_name);
    command.as.insert.value_count = 3;
    snprintf(command.as.insert.values[0].raw, sizeof(command.as.insert.values[0].raw), "%s", v1);
    snprintf(command.as.insert.values[1].raw, sizeof(command.as.insert.values[1].raw), "%s", v2);
    snprintf(command.as.insert.values[2].raw, sizeof(command.as.insert.values[2].raw), "%s", v3);

    return command;
}

/* SELECT * command를 빠르게 만드는 helper다. */
static Command make_select_all_command(const char *table_name)
{
    Command command;

    memset(&command, 0, sizeof(command));
    command.type = CMD_SELECT;
    snprintf(command.as.select.table_name, sizeof(command.as.select.table_name), "%s", table_name);
    command.as.select.select_all = true;

    return command;
}

/* INSERT 실행이 schema 검증, 타입 캐스팅, storage append까지 연결되는지 확인한다. */
static int test_execute_insert_success(void)
{
    Command command = make_insert_command("executor_insert", "1", "'kim'", "24");
    ExecutionResult result;
    char error_buf[MAX_ERROR_LENGTH];
    char buffer[512];

    if (remove_if_exists("data/executor_insert.csv") != 0)
    {
        return 1;
    }

    if (execute_command(&command, &result, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (result.has_rows)
    {
        free_execution_result(&result);
        return 1;
    }

    if (strcmp(result.schema.table_name, "executor_insert") != 0)
    {
        free_execution_result(&result);
        return 1;
    }

    if (read_file_to_buffer("data/executor_insert.csv", buffer, sizeof(buffer)) != 0)
    {
        free_execution_result(&result);
        return 1;
    }

    if (strcmp(buffer, "1,kim,24\n") != 0)
    {
        free_execution_result(&result);
        return 1;
    }

    free_execution_result(&result);

    if (remove_if_exists("data/executor_insert.csv") != 0)
    {
        return 1;
    }

    return 0;
}

/* INSERT value 개수가 schema와 다르면 validate_values 단계에서 실패해야 한다. */
static int test_execute_insert_value_count_mismatch(void)
{
    Command command;
    ExecutionResult result;
    char error_buf[MAX_ERROR_LENGTH];

    memset(&command, 0, sizeof(command));
    command.type = CMD_INSERT;
    snprintf(command.as.insert.table_name, sizeof(command.as.insert.table_name), "%s", "executor_insert");
    command.as.insert.value_count = 2;
    snprintf(command.as.insert.values[0].raw, sizeof(command.as.insert.values[0].raw), "%s", "1");
    snprintf(command.as.insert.values[1].raw, sizeof(command.as.insert.values[1].raw), "%s", "'kim'");

    if (execute_command(&command, &result, error_buf, sizeof(error_buf)) == 0)
    {
        free_execution_result(&result);
        return 1;
    }

    if (result.rows.row_count != 0 || result.rows.rows != NULL)
    {
        return 1;
    }

    return strstr(error_buf, "value 수") == NULL;
}

/* INSERT raw token이 schema 타입으로 캐스팅될 수 없으면 실패해야 한다. */
static int test_execute_insert_cast_failure(void)
{
    Command command = make_insert_command("executor_insert", "'oops'", "'kim'", "24");
    ExecutionResult result;
    char error_buf[MAX_ERROR_LENGTH];

    if (execute_command(&command, &result, error_buf, sizeof(error_buf)) == 0)
    {
        free_execution_result(&result);
        return 1;
    }

    if (result.rows.row_count != 0 || result.rows.rows != NULL)
    {
        return 1;
    }

    return strstr(error_buf, "int") == NULL;
}

/* SELECT * 실행 시 schema와 전체 row 목록을 결과로 돌려줘야 한다. */
static int test_execute_select_success(void)
{
    Command command = make_select_all_command("executor_select");
    ExecutionResult result;
    char error_buf[MAX_ERROR_LENGTH];

    if (execute_command(&command, &result, error_buf, sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (!result.has_rows)
    {
        free_execution_result(&result);
        return 1;
    }

    if (strcmp(result.schema.table_name, "executor_select") != 0)
    {
        free_execution_result(&result);
        return 1;
    }

    if (result.rows.row_count != 2)
    {
        free_execution_result(&result);
        return 1;
    }

    if (result.rows.rows[0].values[0].as.int_value != 1)
    {
        free_execution_result(&result);
        return 1;
    }

    if (strcmp(result.rows.rows[1].values[1].as.string_value, "Lee") != 0)
    {
        free_execution_result(&result);
        return 1;
    }

    free_execution_result(&result);
    if (result.rows.row_count != 0 || result.rows.rows != NULL)
    {
        return 1;
    }

    return 0;
}

/* 현재는 SELECT *만 지원하므로 select_all이 false면 실패해야 한다. */
static int test_execute_select_rejects_non_star(void)
{
    Command command;
    ExecutionResult result;
    char error_buf[MAX_ERROR_LENGTH];

    memset(&command, 0, sizeof(command));
    command.type = CMD_SELECT;
    snprintf(command.as.select.table_name, sizeof(command.as.select.table_name), "%s", "executor_select");
    command.as.select.select_all = false;
    command.as.select.column_count = 1;
    snprintf(command.as.select.columns[0], sizeof(command.as.select.columns[0]), "%s", "name");

    if (execute_command(&command, &result, error_buf, sizeof(error_buf)) == 0)
    {
        free_execution_result(&result);
        return 1;
    }

    if (result.rows.row_count != 0 || result.rows.rows != NULL)
    {
        return 1;
    }

    return strstr(error_buf, "SELECT *") == NULL;
}

int main(void)
{
    char original_cwd[1024];
    int failures = 0;

    if (getcwd(original_cwd, sizeof(original_cwd)) == NULL)
    {
        perror("getcwd");
        return 1;
    }

    if (chdir("tests/fixtures") != 0)
    {
        perror("chdir");
        return 1;
    }

    if (cleanup_generated_executor_files() != 0)
    {
        return 1;
    }

    failures += run_test("execute insert success", test_execute_insert_success);
    failures += run_test("execute insert value count mismatch", test_execute_insert_value_count_mismatch);
    failures += run_test("execute insert cast failure", test_execute_insert_cast_failure);
    failures += run_test("execute select success", test_execute_select_success);
    failures += run_test("execute select rejects non star", test_execute_select_rejects_non_star);

    if (cleanup_generated_executor_files() != 0)
    {
        return 1;
    }

    if (chdir(original_cwd) != 0)
    {
        perror("chdir");
        return 1;
    }

    if (failures != 0)
    {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all executor tests passed\n");
    return 0;
}
