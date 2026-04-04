// SPDX-License-Identifier: MIT

#ifndef HDR10PLUS_RS_H
#define HDR10PLUS_RS_H


#define HDR10PLUS_MAJOR 2
#define HDR10PLUS_MINOR 1
#define HDR10PLUS_PATCH 5


#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/**
 * Opaque HDR10+ JSON file handle
 *
 * Use `hdr10plus_rs_json_free` to free.
 * It should be freed regardless of whether or not an error occurred.
 */
typedef struct Hdr10PlusRsJsonOpaque Hdr10PlusRsJsonOpaque;

/**
 * Struct representing a data buffer
 */
typedef struct {
    /**
     * Pointer to the data buffer
     */
    const uint8_t *data;
    /**
     * Data buffer size
     */
    size_t len;
} Hdr10PlusRsData;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * # Safety
 * The pointer to the data must be valid.
 *
 * Parse a HDR10+ JSON file from file path.
 * Adds an error if the parsing fails.
 */
Hdr10PlusRsJsonOpaque *hdr10plus_rs_parse_json(const char *path);

/**
 * # Safety
 * The pointer to the opaque struct must be valid.
 *
 * Get the last logged error for the JsonOpaque operations.
 *
 * On invalid parsing, an error is added.
 * The user should manually verify if there is an error, as the parsing does not return an error code.
 */
const char *hdr10plus_rs_json_get_error(const Hdr10PlusRsJsonOpaque *ptr);

/**
 * # Safety
 * The pointer to the opaque struct must be valid.
 *
 * Free the Hdr10PlusJsonOpaque
 */
void hdr10plus_rs_json_free(Hdr10PlusRsJsonOpaque *ptr);

/**
 * # Safety
 * The struct pointer must be valid.
 *
 * Writes the encoded HDR10+ payload as a byte buffer, including country code
 * If an error occurs in the writing, returns null
 */
const Hdr10PlusRsData *hdr10plus_rs_write_av1_metadata_obu_t35_complete(Hdr10PlusRsJsonOpaque *ptr,
                                                                        size_t frame_number);

/**
 * # Safety
 * The data pointer should exist, and be allocated by Rust.
 *
 * Free a Data buffer
 */
void hdr10plus_rs_data_free(const Hdr10PlusRsData *data);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* HDR10PLUS_RS_H */
