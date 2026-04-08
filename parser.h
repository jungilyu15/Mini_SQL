#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include "models.h"

/* parser 모듈의 뼈대 함수 */
int parse_sql(
    const char *sql,
    Command *out_command,
    char *error_buf,
    size_t error_buf_size
);

#endif
