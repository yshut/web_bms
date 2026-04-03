/**
 * @file file_transfer.c
 * @brief 文件传输模块实现
 */

#include "file_transfer.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>

/* Base64编码表 */
static const char base64_table[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64解码表 */
static const unsigned char base64_decode_table[256] = {
    ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,  ['E'] = 4,  ['F'] = 5,
    ['G'] = 6,  ['H'] = 7,  ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11,
    ['M'] = 12, ['N'] = 13, ['O'] = 14, ['P'] = 15, ['Q'] = 16, ['R'] = 17,
    ['S'] = 18, ['T'] = 19, ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23,
    ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29,
    ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35,
    ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
    ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47,
    ['w'] = 48, ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, ['1'] = 53,
    ['2'] = 54, ['3'] = 55, ['4'] = 56, ['5'] = 57, ['6'] = 58, ['7'] = 59,
    ['8'] = 60, ['9'] = 61, ['+'] = 62, ['/'] = 63, ['='] = 0
};

/**
 * @brief Base64编码
 */
char* base64_encode(const uint8_t *data, size_t len, size_t *out_len)
{
    if (!data || len == 0) {
        return NULL;
    }
    
    size_t encoded_len = 4 * ((len + 2) / 3);
    char *encoded = (char*)malloc(encoded_len + 1);
    if (!encoded) {
        return NULL;
    }
    
    size_t i = 0, j = 0;
    
    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        encoded[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded[j++] = base64_table[triple & 0x3F];
    }
    
    /* 添加填充 */
    size_t padding = (3 - (len % 3)) % 3;
    for (size_t p = 0; p < padding; p++) {
        encoded[encoded_len - 1 - p] = '=';
    }
    
    encoded[encoded_len] = '\0';
    
    if (out_len) {
        *out_len = encoded_len;
    }
    
    return encoded;
}

/**
 * @brief Base64解码
 */
uint8_t* base64_decode(const char *b64, size_t *out_len)
{
    if (!b64) {
        return NULL;
    }
    
    size_t len = strlen(b64);
    if (len % 4 != 0) {
        return NULL;
    }
    
    size_t padding = 0;
    if (len >= 2 && b64[len - 1] == '=') padding++;
    if (len >= 2 && b64[len - 2] == '=') padding++;
    
    size_t decoded_len = (len / 4) * 3 - padding;
    uint8_t *decoded = (uint8_t*)malloc(decoded_len);
    if (!decoded) {
        return NULL;
    }
    
    size_t i = 0, j = 0;
    
    while (i < len) {
        uint32_t sextet_a = base64_decode_table[(unsigned char)b64[i++]];
        uint32_t sextet_b = base64_decode_table[(unsigned char)b64[i++]];
        uint32_t sextet_c = base64_decode_table[(unsigned char)b64[i++]];
        uint32_t sextet_d = base64_decode_table[(unsigned char)b64[i++]];
        
        uint32_t triple = (sextet_a << 18) + (sextet_b << 12) + (sextet_c << 6) + sextet_d;
        
        if (j < decoded_len) decoded[j++] = (triple >> 16) & 0xFF;
        if (j < decoded_len) decoded[j++] = (triple >> 8) & 0xFF;
        if (j < decoded_len) decoded[j++] = triple & 0xFF;
    }
    
    if (out_len) {
        *out_len = decoded_len;
    }
    
    return decoded;
}

/**
 * @brief 写入文件
 */
int file_write(const char *path, const uint8_t *data, size_t len)
{
    if (!path || !data) {
        return -1;
    }
    
    /* 确保目录存在 */
    char *path_copy = strdup(path);
    char *dir = dirname(path_copy);
    file_mkdir_recursive(dir);
    free(path_copy);
    
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        log_error("无法打开文件: %s (%s)", path, strerror(errno));
        return -1;
    }
    
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        log_error("写入文件失败: %s", path);
        return -1;
    }
    
    log_info("文件写入成功: %s (%zu 字节)", path, len);
    return 0;
}

/**
 * @brief 读取整个文件
 */
uint8_t* file_read(const char *path, size_t *out_len)
{
    if (!path) {
        return NULL;
    }
    
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        log_error("无法打开文件: %s (%s)", path, strerror(errno));
        return NULL;
    }
    
    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    
    uint8_t *data = (uint8_t*)malloc(size);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    
    size_t read_size = fread(data, 1, size, fp);
    fclose(fp);
    
    if (read_size != (size_t)size) {
        free(data);
        return NULL;
    }
    
    if (out_len) {
        *out_len = size;
    }
    
    return data;
}

/**
 * @brief 分块读取文件
 */
uint8_t* file_read_range(const char *path, size_t offset, size_t length, size_t *out_len, bool *eof)
{
    if (!path) {
        return NULL;
    }
    
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        log_error("无法打开文件: %s (%s)", path, strerror(errno));
        return NULL;
    }
    
    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    
    /* 检查偏移 */
    if (offset >= (size_t)size) {
        fclose(fp);
        if (eof) *eof = true;
        if (out_len) *out_len = 0;
        return NULL;
    }
    
    /* 定位到偏移位置 */
    fseek(fp, offset, SEEK_SET);
    
    /* 计算实际读取长度 */
    size_t remaining = size - offset;
    size_t to_read = (length < remaining) ? length : remaining;
    
    uint8_t *data = (uint8_t*)malloc(to_read);
    if (!data) {
        fclose(fp);
        return NULL;
    }
    
    size_t read_size = fread(data, 1, to_read, fp);
    
    /* 检查是否到达文件末尾 */
    long current_pos = ftell(fp);
    if (eof) {
        *eof = (current_pos >= size);
    }
    
    fclose(fp);
    
    if (out_len) {
        *out_len = read_size;
    }
    
    return data;
}

/**
 * @brief 分块写入文件（支持偏移写）
 */
int file_write_range(const char *path, size_t offset, const uint8_t *data, size_t len, bool truncate)
{
    if (!path || (!data && len > 0)) {
        return -1;
    }

    /* 确保目录存在 */
    char *path_copy = strdup(path);
    if (path_copy) {
        char *dir = dirname(path_copy);
        file_mkdir_recursive(dir);
        free(path_copy);
    }

    /* truncate 仅在 offset==0 时生效 */
    const bool do_trunc = (truncate && offset == 0);

    FILE *fp = NULL;
    if (do_trunc) {
        fp = fopen(path, "wb");
    } else {
        fp = fopen(path, "r+b");
        if (!fp) {
            /* 文件可能不存在，创建 */
            fp = fopen(path, "wb+");
        }
    }
    if (!fp) {
        log_error("无法打开文件(分块写): %s (%s)", path, strerror(errno));
        return -1;
    }

    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
        log_error("分块写定位失败: %s offset=%zu (%s)", path, offset, strerror(errno));
        fclose(fp);
        return -1;
    }

    size_t written = 0;
    if (len > 0) {
        written = fwrite(data, 1, len, fp);
    }
    fflush(fp);
    fclose(fp);

    if (written != len) {
        log_error("分块写入失败: %s offset=%zu len=%zu written=%zu", path, offset, len, written);
        return -1;
    }

    return 0;
}

/**
 * @brief 获取文件信息
 */
int file_get_info(const char *path, file_info_t *info)
{
    if (!path || !info) {
        return -1;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    
    /* 提取文件名 */
    char *path_copy = strdup(path);
    char *name = basename(path_copy);
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    free(path_copy);
    
    info->size = st.st_size;
    info->mtime = st.st_mtime;
    info->is_dir = S_ISDIR(st.st_mode);
    
    return 0;
}

/**
 * @brief 递归删除文件或目录
 */
int file_delete_recursive(const char *path)
{
    if (!path) {
        return -1;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    
    if (S_ISDIR(st.st_mode)) {
        /* 删除目录 */
        DIR *dir = opendir(path);
        if (!dir) {
            return -1;
        }
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            file_delete_recursive(full_path);
        }
        
        closedir(dir);
        return rmdir(path);
    } else {
        /* 删除文件 */
        return unlink(path);
    }
}

/**
 * @brief 递归创建目录
 */
int file_mkdir_recursive(const char *path)
{
    if (!path) {
        return -1;
    }
    
    char tmp[1024];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (access(tmp, F_OK) != 0) {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
            *p = '/';
        }
    }
    
    if (access(tmp, F_OK) != 0) {
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief 重命名文件
 */
int file_rename(const char *old_path, const char *new_name)
{
    if (!old_path || !new_name) {
        return -1;
    }
    
    /* 构造新路径 */
    char *path_copy = strdup(old_path);
    char *dir = dirname(path_copy);
    
    char new_path[1024];
    snprintf(new_path, sizeof(new_path), "%s/%s", dir, new_name);
    free(path_copy);
    
    return rename(old_path, new_path);
}

/**
 * @brief 列出目录内容（返回JSON格式）
 */
char* file_list_directory_json(const char *path)
{
    if (!path) {
        return NULL;
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        log_error("无法打开目录: %s (%s)", path, strerror(errno));
        return NULL;
    }
    
    /* 预分配缓冲区 */
    size_t buffer_size = 4096;
    char *json = (char*)malloc(buffer_size);
    if (!json) {
        closedir(dir);
        return NULL;
    }
    
    size_t pos = 0;
    pos += snprintf(json + pos, buffer_size - pos, "{\"items\":[");
    
    bool first = true;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        /* 检查是否需要扩展缓冲区 */
        if (pos + 512 > buffer_size) {
            buffer_size *= 2;
            char *new_json = (char*)realloc(json, buffer_size);
            if (!new_json) {
                free(json);
                closedir(dir);
                return NULL;
            }
            json = new_json;
        }
        
        if (!first) {
            pos += snprintf(json + pos, buffer_size - pos, ",");
        }
        first = false;
        
        pos += snprintf(json + pos, buffer_size - pos,
                       "{\"name\":\"%s\",\"is_dir\":%s,\"size\":%ld,\"mtime\":%ld}",
                       entry->d_name,
                       S_ISDIR(st.st_mode) ? "true" : "false",
                       (long)st.st_size,
                       (long)st.st_mtime);
    }
    
    closedir(dir);
    
    pos += snprintf(json + pos, buffer_size - pos, "]}");
    
    return json;
}

/**
 * @brief 列出S19文件（返回JSON格式）
 */
static int append_s19_files_recursive(const char *base_dir,
                                      const char *cur_dir,
                                      char **json,
                                      size_t *buffer_size,
                                      size_t *pos,
                                      bool *first)
{
    DIR *d = NULL;
    struct dirent *entry = NULL;

    if (!base_dir || !cur_dir || !json || !*json || !buffer_size || !pos || !first) {
        return -1;
    }

    d = opendir(cur_dir);
    if (!d) {
        return -1;
    }

    while ((entry = readdir(d)) != NULL) {
        char full_path[PATH_MAX];
        struct stat st;
        const char *name = entry->d_name;
        size_t name_len = 0;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s/%s", cur_dir, name);
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            append_s19_files_recursive(base_dir, full_path, json, buffer_size, pos, first);
            continue;
        }

        name_len = strlen(name);
        if (!(name_len > 4 &&
              (strcasecmp(name + name_len - 4, ".s19") == 0 ||
               strcasecmp(name + name_len - 4, ".mot") == 0))) {
            continue;
        }

        if (*pos + strlen(full_path) + 16 > *buffer_size) {
            char *new_json = NULL;
            *buffer_size *= 2;
            new_json = (char*)realloc(*json, *buffer_size);
            if (!new_json) {
                closedir(d);
                return -1;
            }
            *json = new_json;
        }

        if (!*first) {
            *pos += snprintf(*json + *pos, *buffer_size - *pos, ",");
        }
        *first = false;

        if (strncmp(full_path, base_dir, strlen(base_dir)) == 0) {
            const char *rel = full_path + strlen(base_dir);
            while (*rel == '/') rel++;
            *pos += snprintf(*json + *pos, *buffer_size - *pos, "\"%s\"", rel);
        } else {
            *pos += snprintf(*json + *pos, *buffer_size - *pos, "\"%s\"", full_path);
        }
    }

    closedir(d);
    return 0;
}

char* list_s19_files_json(const char *dir)
{
    if (!dir) {
        return NULL;
    }
    
    /* 预分配缓冲区 */
    size_t buffer_size = 4096;
    char *json = (char*)malloc(buffer_size);
    if (!json) {
        return NULL;
    }
    
    size_t pos = 0;
    pos += snprintf(json + pos, buffer_size - pos, "{\"files\":[");
    
    /* 递归搜索S19文件 */
    bool first = true;
    if (append_s19_files_recursive(dir, dir, &json, &buffer_size, &pos, &first) != 0) {
        free(json);
        return NULL;
    }

    pos += snprintf(json + pos, buffer_size - pos, "]}");
    
    return json;
}

