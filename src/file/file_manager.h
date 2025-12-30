#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stdbool.h>

// 文件管理器回调
typedef void (*file_list_callback_t)(const char **files, int count);

// 初始化文件管理器
int file_manager_init(void);

// 刷新文件列表
int file_manager_refresh(void);

// 切换目录
int file_manager_change_dir(const char *path);

// 上传文件
int file_manager_upload(const char *local_path, const char *remote_path);

// 下载文件
int file_manager_download(const char *remote_path, const char *local_path);

// 删除文件
int file_manager_delete(const char *path);

// 设置回调
void file_manager_set_callback(file_list_callback_t callback);

// 获取当前路径
const char* file_manager_get_current_path(void);

#endif // FILE_MANAGER_H

