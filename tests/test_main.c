#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * src/main.c 안의 static helper까지 함께 검증하기 위해
 * 테스트 번역 단위 안에서 구현 파일을 직접 포함한다.
 * 실제 프로그램의 main 심볼과 충돌하지 않도록 이름만 바꿔 둔다.
 */
#define main mini_sql_program_main
#include "../src/main.c"
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
    if (remove_if_exists("stdin_capture.txt") != 0)
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
static int run_main_with_input_and_capture(
    int argc,
    char **argv,
    const char *stdin_text,
    char *stdout_buffer,
    size_t stdout_buffer_size,
    char *stderr_buffer,
    size_t stderr_buffer_size
)
{
    int saved_stdin = -1;
    int saved_stdout = -1;
    int saved_stderr = -1;
    FILE *stdin_file = NULL;
    FILE *stdout_file = NULL;
    FILE *stderr_file = NULL;
    int result = 0;

    if (stdin_text != NULL)
    {
        stdin_file = fopen("stdin_capture.txt", "w");
        if (stdin_file == NULL)
        {
            return -999;
        }

        if (fputs(stdin_text, stdin_file) == EOF)
        {
            fclose(stdin_file);
            return -999;
        }

        fclose(stdin_file);
        stdin_file = fopen("stdin_capture.txt", "r");
        if (stdin_file == NULL)
        {
            return -999;
        }
    }

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
        if (stdin_file != NULL)
        {
            fclose(stdin_file);
        }
        return -999;
    }

    saved_stdin = dup(fileno(stdin));
    saved_stdout = dup(fileno(stdout));
    saved_stderr = dup(fileno(stderr));
    if (saved_stdin < 0 || saved_stdout < 0 || saved_stderr < 0)
    {
        fclose(stdout_file);
        fclose(stderr_file);
        if (stdin_file != NULL)
        {
            fclose(stdin_file);
        }
        return -999;
    }

    if ((stdin_file != NULL && dup2(fileno(stdin_file), fileno(stdin)) < 0) ||
        dup2(fileno(stdout_file), fileno(stdout)) < 0 ||
        dup2(fileno(stderr_file), fileno(stderr)) < 0)
    {
        fclose(stdout_file);
        fclose(stderr_file);
        if (stdin_file != NULL)
        {
            fclose(stdin_file);
        }
        close(saved_stdin);
        close(saved_stdout);
        close(saved_stderr);
        return -999;
    }

    if (stdin_file != NULL)
    {
        fclose(stdin_file);
    }
    fclose(stdout_file);
    fclose(stderr_file);
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);

    result = mini_sql_program_main(argc, argv);

    fflush(stdout);
    fflush(stderr);

    if (dup2(saved_stdin, fileno(stdin)) < 0 ||
        dup2(saved_stdout, fileno(stdout)) < 0 ||
        dup2(saved_stderr, fileno(stderr)) < 0)
    {
        close(saved_stdin);
        close(saved_stdout);
        close(saved_stderr);
        return -999;
    }

    close(saved_stdin);
    close(saved_stdout);
    close(saved_stderr);
    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);

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

/* 기존 파일 실행 테스트는 stdin 없이 실행하므로 wrapper로 감싼다. */
static int run_main_and_capture(
    int argc,
    char **argv,
    char *stdout_buffer,
    size_t stdout_buffer_size,
    char *stderr_buffer,
    size_t stderr_buffer_size
)
{
    return run_main_with_input_and_capture(
        argc,
        argv,
        NULL,
        stdout_buffer,
        stdout_buffer_size,
        stderr_buffer,
        stderr_buffer_size);
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

/* 작은따옴표가 닫히지 않으면 문장 분리 단계에서 즉시 실패해야 한다. */
static int test_split_sql_statements_unterminated_quote(void)
{
    SqlStatementList list;
    char error_buf[MAX_ERROR_LENGTH];

    if (split_sql_statements(
            "INSERT INTO main_cli VALUES (1, 'kim; SELECT * FROM main_cli;",
            &list,
            error_buf,
            sizeof(error_buf)) == 0)
    {
        free_sql_statement_list(&list);
        return 1;
    }

    return strstr(error_buf, "작은따옴표") == NULL;
}

/* 인자 없이 실행하면 REPL 모드로 진입하고 EOF에서 정상 종료해야 한다. */
static int test_main_enters_repl_without_argument(void)
{
    char stdout_buffer[512];
    char stderr_buffer[512];
    char *argv[] = {(char *)"mini_sql"};
    int exit_code = 0;

    exit_code = run_main_with_input_and_capture(
        1,
        argv,
        "",
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code != 0)
    {
        return 1;
    }

    if (strstr(stdout_buffer, "Mini_SQL REPL") == NULL)
    {
        return 1;
    }

    return strstr(stdout_buffer, "mini_sql> ") == NULL;
}

/* 파일 인자가 두 개 이상이면 사용법을 출력하고 종료해야 한다. */
static int test_main_usage_with_too_many_arguments(void)
{
    char stdout_buffer[512];
    char stderr_buffer[512];
    char *argv[] = {(char *)"mini_sql", (char *)"a.sql", (char *)"b.sql"};
    int exit_code = 0;

    exit_code = run_main_and_capture(
        3,
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

/* 기존 CSV fixture만 읽는 SELECT-only 파일도 표 형태로 정상 출력해야 한다. */
static int test_main_runs_select_only_file(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_select_only.sql"};
    int exit_code = 0;

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

    if (strstr(stdout_buffer, "+") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "| id") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Han") == NULL || strstr(stdout_buffer, "Yoon") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "(2 rows)") == NULL)
    {
        return 1;
    }

    return 0;
}

/* 특정 컬럼 SELECT는 요청한 컬럼만 출력해야 한다. */
static int test_main_runs_select_named_columns_file(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_select_columns.sql"};
    int exit_code = 0;

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

    if (strstr(stdout_buffer, "| name") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "| age") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "| id") != NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Han") == NULL || strstr(stdout_buffer, "Yoon") == NULL)
    {
        return 1;
    }

    return 0;
}

/* SELECT * + WHERE는 조건에 맞는 row만 표 형태로 출력해야 한다. */
static int test_main_runs_select_where_star_file(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_select_where_star.sql"};
    int exit_code = 0;

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

    if (strstr(stdout_buffer, "| id") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Han") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Yoon") != NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "(1 rows)") == NULL)
    {
        return 1;
    }

    return 0;
}

/* 명시적 컬럼 SELECT + WHERE는 필터링 후 projection 결과만 출력해야 한다. */
static int test_main_runs_select_where_named_columns_file(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_select_where_columns.sql"};
    int exit_code = 0;

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

    if (strstr(stdout_buffer, "| name") == NULL || strstr(stdout_buffer, "| age") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "| id") != NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Yoon") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Han") != NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "(1 rows)") == NULL)
    {
        return 1;
    }

    return 0;
}

/* REPL에서는 세미콜론 없이도 기존 parser 정책대로 한 줄 SQL을 실행할 수 있어야 한다. */
static int test_main_repl_runs_insert_and_select_sequence(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[1024];
    char file_buffer[512];
    char *argv[] = {(char *)"mini_sql"};
    int exit_code = 0;

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    exit_code = run_main_with_input_and_capture(
        1,
        argv,
        "\nINSERT INTO main_cli VALUES (3, 'choi', 40)\nSELECT name, age FROM main_cli WHERE id = 3\nquit\n",
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code != 0)
    {
        fprintf(stderr, "%s\n", stderr_buffer);
        return 1;
    }

    if (strstr(stdout_buffer, "Mini_SQL REPL") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "choi") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "(1 rows)") == NULL)
    {
        return 1;
    }
    if (strstr(stderr_buffer, "repl failed") != NULL)
    {
        return 1;
    }

    if (read_file_to_buffer("data/main_cli.csv", file_buffer, sizeof(file_buffer)) != 0)
    {
        return 1;
    }

    if (strcmp(file_buffer, "3,choi,40\n") != 0)
    {
        return 1;
    }

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    return 0;
}

/* REPL에서는 오류가 나도 종료하지 않고 다음 입력을 계속 받아야 한다. */
static int test_main_repl_continues_after_error(void)
{
    char stdout_buffer[4096];
    char stderr_buffer[2048];
    char *argv[] = {(char *)"mini_sql"};
    int exit_code = 0;

    exit_code = run_main_with_input_and_capture(
        1,
        argv,
        "SELECT nope FROM main_select_only\nSELECT name FROM main_select_only WHERE id = 10\nexit\n",
        stdout_buffer,
        sizeof(stdout_buffer),
        stderr_buffer,
        sizeof(stderr_buffer));

    if (exit_code != 0)
    {
        fprintf(stderr, "%s\n", stderr_buffer);
        return 1;
    }

    if (strstr(stderr_buffer, "repl failed: execute failed") == NULL)
    {
        return 1;
    }
    if (strstr(stderr_buffer, "존재하지 않는 컬럼") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "Han") == NULL)
    {
        return 1;
    }
    if (strstr(stdout_buffer, "(1 rows)") == NULL)
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

/* parse 단계 실패 시 몇 번째 문장에서 실패했는지 오류에 포함해야 한다. */
static int test_main_reports_parse_statement_index(void)
{
    char stdout_buffer[1024];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_parse_error.sql"};
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

    if (strstr(stderr_buffer, "statement 2 parse failed") == NULL)
    {
        return 1;
    }

    return strstr(stderr_buffer, "추가 내용") == NULL;
}

/* execute 단계 실패 시 몇 번째 문장에서 실패했는지 오류에 포함해야 한다. */
static int test_main_reports_execute_statement_index(void)
{
    char stdout_buffer[1024];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_execute_error.sql"};
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

    if (exit_code == 0)
    {
        return 1;
    }

    if (strstr(stderr_buffer, "statement 2 execute failed") == NULL)
    {
        return 1;
    }

    if (remove_if_exists("data/main_cli.csv") != 0)
    {
        return 1;
    }

    return strstr(stderr_buffer, "int") == NULL;
}

/* 존재하지 않는 컬럼을 조회하면 execute 단계에서 문장 번호와 함께 실패해야 한다. */
static int test_main_reports_missing_select_column(void)
{
    char stdout_buffer[1024];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_select_missing_column.sql"};
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

    if (strstr(stderr_buffer, "statement 1 execute failed") == NULL)
    {
        return 1;
    }

    return strstr(stderr_buffer, "존재하지 않는 컬럼") == NULL;
}

/* WHERE 컬럼이 없으면 execute 단계에서 문장 번호와 함께 실패해야 한다. */
static int test_main_reports_missing_where_column(void)
{
    char stdout_buffer[1024];
    char stderr_buffer[1024];
    char *argv[] = {(char *)"mini_sql", (char *)"sql/main_select_where_missing_column.sql"};
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

    if (strstr(stderr_buffer, "statement 1 execute failed") == NULL)
    {
        return 1;
    }

    return strstr(stderr_buffer, "WHERE 컬럼") == NULL;
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
    failures += run_test(
        "split sql statements unterminated quote",
        test_split_sql_statements_unterminated_quote);
    failures += run_test("main enters repl without argument", test_main_enters_repl_without_argument);
    failures += run_test("main usage with too many arguments", test_main_usage_with_too_many_arguments);
    failures += run_test("main missing sql file", test_main_missing_sql_file);
    failures += run_test("main runs basic sql file", test_main_runs_basic_sql_file);
    failures += run_test("main runs select only file", test_main_runs_select_only_file);
    failures += run_test("main runs select named columns file", test_main_runs_select_named_columns_file);
    failures += run_test("main runs select where star file", test_main_runs_select_where_star_file);
    failures += run_test(
        "main runs select where named columns file",
        test_main_runs_select_where_named_columns_file);
    failures += run_test(
        "main repl runs insert and select sequence",
        test_main_repl_runs_insert_and_select_sequence);
    failures += run_test("main repl continues after error", test_main_repl_continues_after_error);
    failures += run_test("main ignores empty statements", test_main_ignores_empty_statements);
    failures += run_test("main reports parse statement index", test_main_reports_parse_statement_index);
    failures += run_test("main reports execute statement index", test_main_reports_execute_statement_index);
    failures += run_test("main reports missing select column", test_main_reports_missing_select_column);
    failures += run_test("main reports missing where column", test_main_reports_missing_where_column);

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
