#ifndef APP_LVGL_NET_MANAGER_H
#define APP_LVGL_NET_MANAGER_H

#include <stddef.h>

int net_manager_apply_current_config(void);
int net_manager_get_active_interface(char *iface, size_t iface_size);

#endif /* APP_LVGL_NET_MANAGER_H */
