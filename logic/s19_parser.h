/**
 * @file s19_parser.h
 * @brief S19 file parser for UDS flashing
 */

#ifndef S19_PARSER_H
#define S19_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/* S19 segment structure */
typedef struct {
    uint32_t address;
    uint8_t *data;
    uint32_t size;
} s19_segment_t;

/* S19 file structure */
typedef struct {
    s19_segment_t *segments;
    int segment_count;
    uint32_t total_bytes;
} s19_file_t;

/**
 * Parse S19 file
 * @param filepath Path to S19 file
 * @return Pointer to s19_file_t structure, or NULL on error
 * @note Caller must free returned structure using s19_free()
 */
s19_file_t* s19_parse(const char *filepath);

/**
 * Free S19 file structure
 * @param s19 Pointer to s19_file_t structure
 */
void s19_free(s19_file_t *s19);

/**
 * Print S19 file info (for debugging)
 * @param s19 Pointer to s19_file_t structure
 */
void s19_print_info(const s19_file_t *s19);

#endif /* S19_PARSER_H */

