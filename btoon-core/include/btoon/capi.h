//  ██████╗ ████████╗ ██████╗  ██████╗ ███╗   ██╗
//  ██╔══██╗╚══██╔══╝██╔═══██╗██╔═══██╗████╗  ██║
//  ██████╔╝   ██║   ██║   ██║██║   ██║██╔██╗ ██║
//  ██╔══██╗   ██║   ██║   ██║██║   ██║██║╚██╗██║
//  ██████╔╝   ██║   ╚██████╔╝╚██████╔╝██║ ╚████║
//  ╚═════╝    ╚═╝    ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝
//
//  BTOON Core
//  Version 0.0.1
//  https://btoon.net //  https://btoon.net //  https://btoon.net //  https://btoon.net //  https://btoon.net & https://github.com/BTOON-project/btoon-coren// https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core https://github.com/BTOON-project/btoon-core
//
// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT


/**
 * @file capi.h
 * @brief C API for BTOON - Stable ABI for language bindings
 */
#ifndef BTOON_CAPI_H
#define BTOON_CAPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
    #ifdef BTOON_BUILD_SHARED
        #define BTOON_API __declspec(dllexport)
    #else
        #define BTOON_API
    #endif
#else
    #define BTOON_API __attribute__((visibility("default")))
#endif

// --- Value Representation ---

typedef enum {
    BTOON_TYPE_NIL,
    BTOON_TYPE_BOOL,
    BTOON_TYPE_INT,
    BTOON_TYPE_UINT,
    BTOON_TYPE_FLOAT,
    BTOON_TYPE_STRING,
    BTOON_TYPE_BINARY,
    BTOON_TYPE_ARRAY,
    BTOON_TYPE_MAP,
    BTOON_TYPE_TIMESTAMP,
    BTOON_TYPE_DATE,
    BTOON_TYPE_DATETIME,
    BTOON_TYPE_BIGINT,
    BTOON_TYPE_VECTOR_FLOAT,
    BTOON_TYPE_VECTOR_DOUBLE,
    BTOON_TYPE_EXTENSION
} btoon_type_t;

typedef struct btoon_value btoon_value_t;

typedef struct {
    const char* ptr;
    size_t len;
} btoon_string_t;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} btoon_binary_t;

typedef struct {
    btoon_value_t** elements;
    size_t len;
} btoon_array_t;

typedef struct {
    btoon_string_t* keys;
    btoon_value_t** values;
    size_t len;
} btoon_map_t;

typedef struct {
    int8_t type;
    const uint8_t* ptr;
    size_t len;
} btoon_extension_t;

typedef struct {
    int64_t seconds;
} btoon_timestamp_t;

typedef struct {
    int64_t milliseconds;
} btoon_date_t;

typedef struct {
    int64_t nanoseconds;
} btoon_datetime_t;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} btoon_bigint_t;

typedef struct {
    const float* ptr;
    size_t len;
} btoon_vector_float_t;

typedef struct {
    const double* ptr;
    size_t len;
} btoon_vector_double_t;

struct btoon_value {
    btoon_type_t type;
    union {
        bool b;
        int64_t i;
        uint64_t u;
        double f;
        btoon_string_t s;
        btoon_binary_t bin;
        btoon_array_t a;
        btoon_map_t m;
        btoon_timestamp_t ts;
        btoon_date_t date;
        btoon_datetime_t dt;
        btoon_bigint_t bi;
        btoon_vector_float_t vf;
        btoon_vector_double_t vd;
        btoon_extension_t ext;
    } as;
};

// --- Value Management ---

BTOON_API btoon_value_t* btoon_value_create_nil();
BTOON_API btoon_value_t* btoon_value_create_bool(bool value);
BTOON_API btoon_value_t* btoon_value_create_int(int64_t value);
BTOON_API btoon_value_t* btoon_value_create_uint(uint64_t value);
BTOON_API btoon_value_t* btoon_value_create_float(double value);
BTOON_API btoon_value_t* btoon_value_create_string(const char* str, size_t len);
BTOON_API btoon_value_t* btoon_value_create_binary(const uint8_t* data, size_t len);
BTOON_API btoon_value_t* btoon_value_create_array(const btoon_value_t** elements, size_t len);
BTOON_API btoon_value_t* btoon_value_create_map(const btoon_string_t* keys, const btoon_value_t** values, size_t len);
BTOON_API btoon_value_t* btoon_value_create_timestamp(int64_t seconds);
BTOON_API btoon_value_t* btoon_value_create_extension(int8_t type, const uint8_t* data, size_t len);

BTOON_API void btoon_value_destroy(btoon_value_t* value);

// --- Encoding and Decoding ---

typedef enum {
    BTOON_COMPRESSION_ZLIB = 0,
    BTOON_COMPRESSION_LZ4 = 1,
    BTOON_COMPRESSION_ZSTD = 2
} btoon_compression_t;

typedef struct {
    bool compress;
    btoon_compression_t compression_algorithm;
    int compression_level;
    bool auto_tabular;
} btoon_encode_options_t;

typedef struct {
    bool decompress;
    bool strict;
} btoon_decode_options_t;

typedef struct {
    uint8_t* data;
    size_t size;
    char* error;
} btoon_result_t;

BTOON_API btoon_result_t btoon_encode(const btoon_value_t* value, const btoon_encode_options_t* options);
BTOON_API btoon_value_t* btoon_decode(const uint8_t* data, size_t size, const btoon_decode_options_t* options, char** error);

BTOON_API void btoon_free_result(btoon_result_t result);

// --- Version Info ---
BTOON_API const char* btoon_version(void);

#ifdef __cplusplus
}
#endif

#endif // BTOON_CAPI_H

