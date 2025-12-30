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

# 源文件目录
SRC_DIRS := . ui logic drivers utils
INC_DIRS := . ui logic drivers utils

# LVGL路径（使用LVGL 8.x版本）
LVGL_DIR := ../package/gui/littlevgl-8/lvgl
LVGL_INC := $(LVGL_DIR)

# 查找所有源文件（排除测试程序）
SRCS := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
SRCS := $(filter-out ./touch_test.c touch_test.c, $(SRCS))
OBJS := $(SRCS:.c=.o)

# 编译标志
CFLAGS := -Wall -Wextra -O2 -g
# 重要：项目的LVGL路径要放在最前面，避免使用系统安装的LVGL
CFLAGS += -I$(LVGL_INC)
CFLAGS += $(foreach dir,$(INC_DIRS),-I$(dir))
CFLAGS += -DLV_CONF_INCLUDE_SIMPLE

# FreeType 头文件路径（Tina-Linux 交叉编译环境）
# 使用相对路径，从 app_lvgl 目录到 staging_dir
FT_INC_DIR := ../out/t113-mq_r/staging_dir/target/usr/include/freetype2
FT_LIB_DIR := ../out/t113-mq_r/staging_dir/target/usr/lib
CFLAGS += -I$(FT_INC_DIR)

# 链接标志
LDFLAGS := -lpthread -lm -lrt
LDFLAGS += -L$(FT_LIB_DIR) -lfreetype -lz -lbz2
LDFLAGS += -ljson-c

# LVGL库文件（如果已经预编译）
# LDFLAGS += -L$(LVGL_DIR)/lib -llvgl

# 或者直接链接LVGL对象文件
LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name '*.c')
LVGL_OBJS := $(LVGL_SRCS:.c=.o)

# 默认目标
all: $(TARGET)

# 链接目标程序
$(TARGET): $(OBJS) $(LVGL_OBJS)
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
	rm -f $(LVGL_OBJS)
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

