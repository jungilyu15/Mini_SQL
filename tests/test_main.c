#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * main.c 안의 static helper까지 함께 검증하기 위해
 * 테스트 번역 단위 안에서 main.c를 직접 포함한다.
 * 실제 프로그램의 main 심볼과 충돌하지 않도록 이름만 바꿔 둔다.
 */
#define main mini_sql_program_main
#include "../main.c"
#undef main

/* 각 테스트 함수를 공통 형식으로 실행한다. */
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

/* 파일이 존재하면 지우고, 원래 없으면 정상으로 본다. */
static int remove_if_exists(const char *path)
{
    if (unlink(path) != 0 && access(path, F_OK) == 0)
    {
        perror("unlink");
        return -1;
    }

    return 0;
}

/* main 테스트에서 생성하는 산출물을 정리한다. */
static int cleanup_generated_main_files(void)
{
    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return -1;
    }
    if (remove_if_exists("stdout_capture.txt") != 0)
    {
        return -1;
    }
    if (remove_if_exists("stderr_capture.txt") != 0)
    {
        return -1;
    }

    return 0;
}

/* 파일 전체를 읽어 문자열 버퍼에 담는다. */
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

/*
 * stdout / stderr를 임시 파일로 바꿔 놓고 mini_sql_program_main을 실행한다.
 * CLI 통합 테스트에서 실제 사용자 출력 문자열을 검증할 때 사용한다.
 */
static int run_main_and_capture(
    int argc,
    char **argv,
    char *stdout_buffer,
    size_t stdout_buffer_size,
    char *stderr_buffer,
    size_t stderr_buffer_size
)
{
    int saved_stdout = -1;
    int saved_stderr = -1;
    FILE *stdout_file = NULL;
    FILE *stderr_file = NULL;
    int result = 0;

    fflush(stdout);
    fflush(stderr);

    stdout_file = fopen("stdout_capture.txt", "w");
    stderr_file = fopen("stderr_capture.txt", "w");
    if (stdout_file == NULL || stderr_file == NULL)
    {
        if (stdout_file != NULL)
        {
            fclose(stdout_file);
        }
        if (stderr_file != NULL)
        {
            fclose(stderr_file);
        }
        return -999;
    }

    saved_stdout = dup(fileno(stdout));
    saved_stderr = dup(fileno(stderr));
    if (saved_stdout < 0 || saved_stderr < 0)
    {
        fclose(stdout_file);
        fclose(stderr_file);
        return -999;
    }

    if (dup2(fileno(stdout_file), fileno(stdout)) < 0 ||
        dup2(fileno(stderr_file), fileno(stderr)) < 0)
    {
        fclose(stdout_file);
        fclose(stderr_file);
        close(saved_stdout);
        close(saved_stderr);
        return -999;
    }

    fclose(stdout_file);
    fclose(stderr_file);

    result = mini_sql_program_main(argc, argv);

    fflush(stdout);
    fflush(stderr);

    if (dup2(saved_stdout, fileno(stdout)) < 0 ||
        dup2(saved_stderr, fileno(stderr)) < 0)
    {
        close(saved_stdout);
        close(saved_stderr);
        return -999;
    }

    close(saved_stdout);
    close(saved_stderr);

    if (stdout_buffer != NULL && stdout_buffer_size > 0)
    {
        if (read_file_to_buffer("stdout_capture.txt", stdout_buffer, stdout_buffer_size) != 0)
        {
            return -999;
        }
    }

    if (stderr_buffer != NULL && stderr_buffer_size > 0)
    {
        if (read_file_to_buffer("stderr_capture.txt", stderr_buffer, stderr_buffer_size) != 0)
        {
            return -999;
        }
    }

    return result;
}

/* split_sql_statements가 기본적인 세미콜론 분리를 수행하는지 확인한다. */
static int test_split_sql_statements_basic(void)
{
    SqlStatementList list;
    char error_buf[MAX_ERROR_LENGTH];

    if (split_sql_statements(
            "INSERT INTO main_cli VALUES (1, 'kim', 24); SELECT * FROM main_cli;",
            &list,
            error_buf,
            sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (list.count != 2)
    {
        free_sql_statement_list(&list);
        return 1;
    }

    if (strcmp(list.items[0], "INSERT INTO main_cli VALUES (1, 'kim', 24)") != 0)
    {
        free_sql_statement_list(&list);
        return 1;
    }

    if (strcmp(list.items[1], "SELECT * FROM main_cli") != 0)
    {
        free_sql_statement_list(&list);
        return 1;
    }

    free_sql_statement_list(&list);
    return 0;
}

/* 빈 문장은 버리고, 작은따옴표 문자열 안의 세미콜론은 유지해야 한다. */
static int test_split_sql_statements_ignores_empty_and_keeps_quote_semicolon(void)
{
    SqlStatementList list;
    char error_buf[MAX_ERROR_LENGTH];

    if (split_sql_statements(
            " ; INSERT INTO main_cli VALUES (1, 'a;b', 24); ;; SELECT * FROM main_cli ; ",
            &list,
            error_buf,
            sizeof(error_buf)) != 0)
    {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (list.count != 2)
    {
        free_sql_statement_list(&list);
        return 1;
    }

    if (strstr(list.items[0], "'a;b'") == NULL)
    {
        free_sql_statement_list(&list);
        return 1;
    }

    free_sql_statement_list(&list);
    return 0;
}

/* 인자가 없으면 사용법을 출력하고 non-zero로 종료해야 한다. */
static int test_main_usage_without_argument(void)
{
    char stdout_buffer[512];
    char stderr_buffer[512];
    char *argv[] = {(char *)"mini_sql"};
    int exit_code = 0;

    exit_code = run_main_and_capture(
        1,
        argv,
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code == 0)
    {
        return 1;
    }

    return strstr(stderr_buffer, "Usage:") == NULL;
}

/* 없는 SQL 파일을 넘기면 명확한 오류가 나와야 한다. */
static int test_main_missing_sql_file(void)
{
    char stdout_buffer[512];
    char stderr_buffer[512];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/missing.sql"};
    int exit_code = 0;

    exit_code = run_main_and_capture(
        2,
        argv,
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code == 0)
    {
        return 1;
    }

    return strstr(stderr_buffer, "열 수 없습니다") == NULL;
}

/* SQL 파일 안의 INSERT -> SELECT 흐름이 end-to-end로 동작하는지 확인한다. */
static int test_main_runs_basic_sql_file(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char file_buffer[512];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_basic.sql"};
    int exit_code = 0;

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    exit_code = run_main_and_capture(
        2,
        argv,
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code != 0)
    {
        fprintf(stderr, "%s\n", stderr_buffer);
        return 1;
    }

    if (strstr(stdout_buffer, "| id |") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "kim") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "(1 rows)") == NULL)
    {
        return 1;
    }

    if (read_file_to_buffer("data/main_cli.csv", file_buffer, sizeof(file_buffer)) != 0)
    {
        return 1;
    }

    if (strcmp(file_buffer, "1,kim,24\n") != 0)
    {
        return 1;
    }

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    return 0;
}

/* 파일 안의 빈 문장들은 무시하고 나머지 SQL만 순서대로 실행해야 한다. */
static int test_main_ignores_empty_statements(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char file_buffer[512];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_empty.sql"};
    int exit_code = 0;

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    exit_code = run_main_and_capture(
        2,
        argv,
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code != 0)
    {
        fprintf(stderr, "%s\n", stderr_buffer);
        return 1;
    }

    if (strstr(stdout_buffer, "lee") == NULL)
    {
        return 1;
    }

    if (read_file_to_buffer("data/main_cli.csv", file_buffer, sizeof(file_buffer)) != 0)
    {
        return 1;
    }

    if (strcmp(file_buffer, "2,lee,31\n") != 0)
    {
        return 1;
    }

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    return 0;
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

    if (cleanup_generated_main_files() != 0)
    {
        return 1;
    }

    failures += run_test("split sql statements basic", test_split_sql_statements_basic);
    failures += run_test(
        "split sql statements ignores empty and keeps quote semicolon",
        test_split_sql_statements_ignores_empty_and_keeps_quote_semicolon);
    failures += run_test("main usage without argument", test_main_usage_without_argument);
    failures += run_test("main missing sql file", test_main_missing_sql_file);
    failures += run_test("main runs basic sql file", test_main_runs_basic_sql_file);
    failures += run_test("main ignores empty statements", test_main_ignores_empty_statements);

    if (cleanup_generated_main_files() != 0)
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

    printf("all main tests passed\n");
    return 0;
}
