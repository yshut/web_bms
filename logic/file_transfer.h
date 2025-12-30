/**
 * @file file_transfer.h
 * @brief 文件传输模块 - Base64编解码和文件操作
 */

#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Base64编码
 * @param data 输入数据
 * @param len 输入数据长度
 * @param out_len 输出编码后的长度（可选）
 * @return 编码后的字符串（需要调用者free）
 */
char* base64_encode(const uint8_t *data, size_t len, size_t *out_len);

/**
 * @brief Base64解码
 * @param b64 Base64字符串
 * @param out_len 输出解码后的长度（可选）
 * @return 解码后的数据（需要调用者free）
 */
uint8_t* base64_decode(const char *b64, size_t *out_len);

/**
 * @brief 文件信息结构
 */
typedef struct {
    char name[256];
    size_t size;
    time_t mtime;
    bool is_dir;
} file_info_t;

/**
 * @brief 写入文件
 * @param path 文件路径
 * @param data 数据
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int file_write(const char *path, const uint8_t *data, size_t len);

/**
 * @brief 读取整个文件
 * @param path 文件路径
 * @param out_len 输出文件大小
 * @return 文件数据（需要调用者free），失败返回NULL
 */
uint8_t* file_read(const char *path, size_t *out_len);

/**
 * @brief 分块读取文件
 * @param path 文件路径
 * @param offset 起始偏移
 * @param length 读取长度
 * @param out_len 实际读取长度
 * @param eof 是否到达文件末尾
 * @return 文件数据（需要调用者free），失败返回NULL
 */
uint8_t* file_read_range(const char *path, size_t offset, size_t length, size_t *out_len, bool *eof);

/**
 * @brief 分块写入文件（支持偏移写）
 * @param path 文件路径
 * @param offset 写入起始偏移
 * @param data 数据
 * @param len 数据长度
 * @param truncate 若为 true 且 offset==0，则先清空/重建文件
 * @return 0成功，-1失败
 */
int file_write_range(const char *path, size_t offset, const uint8_t *data, size_t len, bool truncate);

/**
 * @brief 获取文件信息
 * @param path 文件路径
 * @param info 输出文件信息
 * @return 0成功，-1失败
 */
int file_get_info(const char *path, file_info_t *info);

/**
 * @brief 递归删除文件或目录
 * @param path 文件或目录路径
 * @return 0成功，-1失败
 */
int file_delete_recursive(const char *path);

/**
 * @brief 递归创建目录
 * @param path 目录路径
 * @return 0成功，-1失败
 */
int file_mkdir_recursive(const char *path);

/**
 * @brief 重命名文件
 * @param old_path 旧路径
 * @param new_name 新文件名（不是完整路径）
 * @return 0成功，-1失败
 */
int file_rename(const char *old_path, const char *new_name);

/**
 * @brief 列出目录内容（返回JSON格式）
 * @param path 目录路径
 * @return JSON字符串（需要调用者free），失败返回NULL
 */
char* file_list_directory_json(const char *path);

/**
 * @brief 列出S19文件（返回JSON格式）
 * @param dir 目录路径
 * @return JSON字符串（需要调用者free），失败返回NULL
 */
char* list_s19_files_json(const char *dir);

#ifdef __cplusplus
}
#endif

#endif // FILE_TRANSFER_H

