# LVGL应用Makefile for Tina-Linux

# 交叉编译工具链
# 可在命令行传入：make CROSS_COMPILE=/home/yst/Tina-Linux/prebuilt/gcc/linux-x86/arm/toolchain-sunxi-musl/toolchain/bin/arm-openwrt-linux-
CROSS_COMPILE ?= arm-openwrt-linux-
CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
AR := $(CROSS_COMPILE)ar
STRIP := $(CROSS_COMPILE)strip

# 目标程序名
TARGET := lvgl_app

# 无屏幕模式：只编译 logic / utils / drivers（不含 ui/ 和 LVGL）
SRC_DIRS := . logic drivers utils src/wifi
INC_DIRS := . logic drivers utils src/wifi

# 查找所有源文件（排除测试程序和 UI 文件）
SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
SRCS := $(filter-out ./touch_test.c touch_test.c, $(SRCS))
# 排除依赖 LVGL 的文件（由 headless_stubs.c 提供空实现）
SRCS := $(filter-out logic/app_manager.c, $(SRCS))
SRCS := $(filter-out logic/ui_remote_control.c, $(SRCS))
SRCS := $(filter-out logic/ws_command_handler.c, $(SRCS))
# 排除显示/触摸驱动（无屏幕模式不需要）
SRCS := $(filter-out drivers/display_drv.c, $(SRCS))
SRCS := $(filter-out drivers/touch_drv.c, $(SRCS))
SRCS := $(filter-out drivers/touch_drv_alt.c, $(SRCS))
# 排除字体管理器（依赖 LVGL）
SRCS := $(filter-out utils/font_manager.c, $(SRCS))
OBJS := $(SRCS:.c=.o)

# 编译标志（不再需要 LVGL / FreeType）
CFLAGS := -Wall -Wextra -O2 -g
CFLAGS += $(foreach dir,$(INC_DIRS),-I$(dir))
CFLAGS += -DNO_DISPLAY

# 链接标志
LDFLAGS := -lpthread -lm -lrt
LDFLAGS += -ljson-c

# 默认目标
all: $(TARGET)

# 链接目标程序（无 LVGL 对象）
$(TARGET): $(OBJS)
	@echo "链接 $@"
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "编译完成: $(TARGET)"

# 编译.c文件
%.o: %.c
	@echo "编译 $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	@echo "清理编译产物"
	rm -f $(OBJS) $(TARGET)
	rm -f touch_test.o
	@echo "注意: touch_test 由单独的 Makefile.touch_test 管理"

# 安装到目标设备
install: $(TARGET)
	@echo "安装到目标设备..."
	scp $(TARGET) root@192.168.1.100:/usr/bin/
	@echo "安装完成"

# 运行（在目标设备上）
run:
	ssh root@192.168.1.100 "killall $(TARGET) 2>/dev/null || true; /usr/bin/$(TARGET)"

# 帮助信息
help:
	@echo "可用目标:"
	@echo "  all      - 编译程序（默认）"
	@echo "  clean    - 清理编译产物"
	@echo "  install  - 安装到目标设备"
	@echo "  run      - 在目标设备上运行"
	@echo ""
	@echo "变量:"
	@echo "  CROSS_COMPILE - 交叉编译工具链前缀（默认: arm-openwrt-linux-）"
	@echo "  TARGET        - 目标程序名（默认: lvgl_app）"

.PHONY: all clean install run help
