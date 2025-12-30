#include "file_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

// 文件管理器状态
typedef struct {
    char current_path[256];
    file_list_callback_t callback;
} file_manager_state_t;

static file_manager_state_t g_file_manager = {
    .current_path = "/",
    .callback = NULL
};

int file_manager_init(void) {
    strcpy(g_file_manager.current_path, "/");
    return 0;
}

int file_manager_refresh(void) {
    DIR *dir;
    struct dirent *entry;
    char **files = NULL;
    int count = 0;
    int capacity = 100;
    
    files = (char**)malloc(capacity * sizeof(char*));
    if (!files) {
        return -1;
    }
    
    dir = opendir(g_file_manager.current_path);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            // 跳过 . 和 ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            if (count >= capacity) {
                capacity *= 2;
                files = (char**)realloc(files, capacity * sizeof(char*));
            }
            
            files[count++] = strdup(entry->d_name);
        }
        closedir(dir);
    }
    
    // 调用回调
    if (g_file_manager.callback) {
        g_file_manager.callback((const char**)files, count);
    }
    
    // 释放内存
    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
    free(files);
    
    return count;
}

int file_manager_change_dir(const char *path) {
    if (!path) {
        return -1;
    }
    
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncpy(g_file_manager.current_path, path, sizeof(g_file_manager.current_path) - 1);
        return file_manager_refresh();
    }
    
    return -1;
}

int file_manager_upload(const char *local_path, const char *remote_path) {
    // TODO: 实现文件上传（通过网络或其他方式）
    printf("Upload: %s -> %s\n", local_path, remote_path);
    return 0;
}

int file_manager_download(const char *remote_path, const char *local_path) {
    // TODO: 实现文件下载
    printf("Download: %s -> %s\n", remote_path, local_path);
    return 0;
}

int file_manager_delete(const char *path) {
    if (!path) {
        return -1;
    }
    
    return remove(path);
}

void file_manager_set_callback(file_list_callback_t callback) {
    g_file_manager.callback = callback;
}

const char* file_manager_get_current_path(void) {
    return g_file_manager.current_path;
}

