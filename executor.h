#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stddef.h>
#include <stdio.h>

#include "models.h"

/* executor 모듈의 뼈대 함수 */
int execute_command(
    const Command *command,
    const char *schema_dir,
    const char *data_dir,
    FILE *out_stream,
    char *error_buf,
    size_t error_buf_size
);

#endif
