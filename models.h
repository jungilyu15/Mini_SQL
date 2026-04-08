#ifndef MODELS_H
#define MODELS_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_NAME_LENGTH 64
#define MAX_COLUMNS 32
#define MAX_VALUE_LENGTH 256
#define MAX_SQL_TEXT_LENGTH 4096
#define MAX_ERROR_LENGTH 256

typedef enum {
    CMD_INSERT,
    CMD_SELECT
} CommandType;

typedef enum {
    COL_INT,
    COL_TEXT
} ColumnType;

typedef struct {
    char name[MAX_NAME_LENGTH];
    ColumnType type;
} ColumnDef;

typedef struct {
    char table_name[MAX_NAME_LENGTH];
    size_t column_count;
    ColumnDef columns[MAX_COLUMNS];
} TableSchema;

typedef struct {
    /* SQL 값 원본 문자열 보관용 필드 */
    char raw[MAX_VALUE_LENGTH];
} SqlValue;

typedef struct {
    char table_name[MAX_NAME_LENGTH];
    size_t value_count;
    SqlValue values[MAX_COLUMNS];
} InsertCommand;

typedef struct {
    char table_name[MAX_NAME_LENGTH];
    bool select_all;
} SelectCommand;

typedef struct {
    CommandType type;
    union {
        InsertCommand insert;
        SelectCommand select;
    } as;
} Command;

#endif
