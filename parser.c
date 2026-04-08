#include "parser.h"

#include <stdio.h>

int parse_sql(
    const char *sql,
    Command *out_command,
    char *error_buf,
    size_t error_buf_size
) {
    /*
     * TODO:
     * - SQL 문자열을 해석한다.
     * - INSERT / SELECT 명령을 구분한다.
     * - out_command를 채운다.
     */
    (void)sql;
    (void)out_command;
    snprintf(error_buf, error_buf_size, "parse_sql: 아직 구현되지 않았습니다");
    return -1;
}
