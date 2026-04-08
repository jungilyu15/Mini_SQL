#include "executor.h"

#include <stdio.h>

int execute_command(
    const Command *command,
    const char *schema_dir,
    const char *data_dir,
    FILE *out_stream,
    char *error_buf,
    size_t error_buf_size
) {
    (void)command;
    (void)schema_dir;
    (void)data_dir;
    (void)out_stream;

    /*
     * TODO:
     * - Command 타입에 따라 INSERT / SELECT로 분기한다.
     * - schema_manager와 storage를 호출한다.
     */
    snprintf(error_buf, error_buf_size, "execute_command: 아직 구현되지 않았습니다");
    return -1;
}
