#ifndef MODELS_H
#define MODELS_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_NAME_LENGTH 64
#define MAX_COLUMNS 32
#define MAX_VALUE_LENGTH 256
#define MAX_SQL_TEXT_LENGTH 4096
#define MAX_ERROR_LENGTH 256
/*
 * CSV 한 줄 전체를 읽을 때 사용하는 최대 길이다.
 * 모든 컬럼이 최대 길이 문자열이라고 가정한 보수적인 상한으로 잡는다.
 */
#define MAX_LINE_LENGTH ((MAX_COLUMNS * MAX_VALUE_LENGTH) + MAX_COLUMNS + 2)

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
    /*
     * schema 타입과의 일치 여부를 확인하기 위한 런타임 보조 정보다.
     * 실제 값의 최종 의미 해석은 항상 TableSchema 기준으로 한다.
     */
    ColumnType type;
    union {
        int int_value;
        char string_value[MAX_VALUE_LENGTH];
    } as;
} StorageValue;

typedef struct {
    /*
     * row는 schema 컬럼 순서와 1:1로 대응하는 값 배열만 가진다.
     * 컬럼 이름은 중복 보관하지 않고 TableSchema가 책임진다.
     */
    size_t value_count;
    StorageValue values[MAX_COLUMNS];
} StorageRow;

typedef struct {
    /*
     * 읽은 CSV row들을 동적으로 모아 두는 결과 구조체다.
     * caller는 사용 후 free_storage_row_list()로 해제한다.
     */
    size_t row_count;
    StorageRow *rows;
} StorageRowList;

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
