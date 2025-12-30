/**
 * @file s19_parser.c
 * @brief S19 file parser implementation
 */

#include "s19_parser.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Helper: Convert hex char to nibble */
static int hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Helper: Convert 2 hex chars to byte */
static int hex_to_byte(const char *hex)
{
    int high = hex_to_nibble(hex[0]);
    int low = hex_to_nibble(hex[1]);
    if (high < 0 || low < 0) return -1;
    return (high << 4) | low;
}

/* Helper: Convert hex string to bytes */
static int hex_string_to_bytes(const char *hex, uint8_t *out, int max_len)
{
    int len = strlen(hex);
    if (len % 2 != 0 || len / 2 > max_len) return -1;
    
    for (int i = 0; i < len / 2; i++) {
        int byte = hex_to_byte(&hex[i * 2]);
        if (byte < 0) return -1;
        out[i] = (uint8_t)byte;
    }
    
    return len / 2;
}

s19_file_t* s19_parse(const char *filepath)
{
    if (!filepath) return NULL;
    
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        log_error("Failed to open S19 file: %s", filepath);
        return NULL;
    }
    
    s19_file_t *s19 = calloc(1, sizeof(s19_file_t));
    if (!s19) {
        fclose(fp);
        return NULL;
    }
    
    /* Allocate initial segment array */
    int max_segments = 16;
    s19->segments = calloc(max_segments, sizeof(s19_segment_t));
    if (!s19->segments) {
        free(s19);
        fclose(fp);
        return NULL;
    }
    
    char line[1024];
    int line_num = 0;
    s19_segment_t current_seg = {0};
    bool has_current = false;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        
        /* Remove trailing newline/whitespace */
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || 
                          line[len-1] == ' ' || line[len-1] == '\t')) {
            line[--len] = '\0';
        }
        
        if (len == 0) continue;
        
        /* Check record type */
        if (line[0] != 'S') {
            log_warn("Line %d: Invalid S-record format", line_num);
            continue;
        }
        
        char record_type = line[1];
        
        /* Only process S1, S2, S3 (data records) */
        if (record_type != '1' && record_type != '2' && record_type != '3') {
            continue;
        }
        
        /* Parse byte count */
        if (len < 4) continue;
        int byte_count = hex_to_byte(&line[2]);
        if (byte_count < 0) continue;
        
        /* Determine address length */
        int addr_len = (record_type == '1') ? 2 : (record_type == '2') ? 3 : 4;
        int addr_chars = addr_len * 2;
        
        if (len < 4 + addr_chars) continue;
        
        /* Parse address */
        uint32_t address = 0;
        for (int i = 0; i < addr_len; i++) {
            int byte = hex_to_byte(&line[4 + i * 2]);
            if (byte < 0) goto next_line;
            address = (address << 8) | byte;
        }
        
        /* Calculate data length (exclude address and checksum) */
        int data_len = byte_count - addr_len - 1;
        if (data_len <= 0 || data_len > 255) continue;
        
        /* Parse data bytes */
        int data_start = 4 + addr_chars;
        if (len < data_start + data_len * 2) continue;
        
        uint8_t data_bytes[256];
        for (int i = 0; i < data_len; i++) {
            int byte = hex_to_byte(&line[data_start + i * 2]);
            if (byte < 0) goto next_line;
            data_bytes[i] = (uint8_t)byte;
        }
        
        /* Check if this continues the current segment */
        if (has_current && address == current_seg.address + current_seg.size) {
            /* Extend current segment */
            uint8_t *new_data = realloc(current_seg.data, current_seg.size + data_len);
            if (new_data) {
                current_seg.data = new_data;
                memcpy(current_seg.data + current_seg.size, data_bytes, data_len);
                current_seg.size += data_len;
            }
        } else {
            /* Save previous segment and start new one */
            if (has_current) {
                /* Expand segment array if needed */
                if (s19->segment_count >= max_segments) {
                    max_segments *= 2;
                    s19_segment_t *new_segs = realloc(s19->segments, 
                                                      max_segments * sizeof(s19_segment_t));
                    if (!new_segs) goto error;
                    s19->segments = new_segs;
                }
                
                s19->segments[s19->segment_count++] = current_seg;
                s19->total_bytes += current_seg.size;
            }
            
            /* Start new segment */
            current_seg.address = address;
            current_seg.size = data_len;
            current_seg.data = malloc(data_len);
            if (!current_seg.data) goto error;
            memcpy(current_seg.data, data_bytes, data_len);
            has_current = true;
        }
        
next_line:
        continue;
    }
    
    /* Save last segment */
    if (has_current) {
        if (s19->segment_count >= max_segments) {
            max_segments++;
            s19_segment_t *new_segs = realloc(s19->segments, 
                                              max_segments * sizeof(s19_segment_t));
            if (!new_segs) goto error;
            s19->segments = new_segs;
        }
        s19->segments[s19->segment_count++] = current_seg;
        s19->total_bytes += current_seg.size;
    }
    
    fclose(fp);
    
    log_info("S19 parsed: %d segments, %u total bytes", 
             s19->segment_count, s19->total_bytes);
    
    return s19;
    
error:
    if (has_current && current_seg.data) free(current_seg.data);
    s19_free(s19);
    fclose(fp);
    return NULL;
}

void s19_free(s19_file_t *s19)
{
    if (!s19) return;
    
    if (s19->segments) {
        for (int i = 0; i < s19->segment_count; i++) {
            if (s19->segments[i].data) {
                free(s19->segments[i].data);
            }
        }
        free(s19->segments);
    }
    
    free(s19);
}

void s19_print_info(const s19_file_t *s19)
{
    if (!s19) return;
    
    log_info("=== S19 File Info ===");
    log_info("Segments: %d", s19->segment_count);
    log_info("Total bytes: %u", s19->total_bytes);
    
    for (int i = 0; i < s19->segment_count; i++) {
        log_info("Segment %d: addr=0x%08X size=%u bytes", 
                 i + 1, s19->segments[i].address, s19->segments[i].size);
    }
}

