#include <stdio.h>

static void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <sql-file>\n", program_name);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    (void)argv;
    /*
     * TODO:
     * - SQL 파일을 읽는다.
     * - parser에 SQL 문자열을 전달한다.
     * - executor를 호출한다.
     */
    fprintf(stderr, "mini_sql: 아직 구현되지 않았습니다\n");
    return 1;
}
