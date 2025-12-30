#!/bin/bash
# 部署脚本 - 将LVGL应用推送到T113-S3开发板

set -e  # 遇到错误立即退出

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 默认配置（可以通过命令行参数覆盖）
BOARD_IP="${1:-192.168.100.100}"
BOARD_USER="${2:-root}"
TARGET_DIR="${3:-/mnt/UDISK}"
APP_NAME="lvgl_app"

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}LVGL应用部署脚本${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo "开发板IP: ${BOARD_IP}"
echo "用户名: ${BOARD_USER}"
echo "目标目录: ${TARGET_DIR}"
echo ""

# 检查可执行文件是否存在
if [ ! -f "${APP_NAME}" ]; then
    echo -e "${RED}错误: ${APP_NAME} 不存在！${NC}"
    echo "请先运行 'make' 编译应用"
    exit 1
fi

echo -e "${YELLOW}步骤1: 检查开发板连接...${NC}"
if ping -c 1 -W 2 "${BOARD_IP}" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ 开发板在线${NC}"
else
    echo -e "${RED}✗ 无法连接到开发板 ${BOARD_IP}${NC}"
    echo "请检查："
    echo "  1. 开发板是否开机"
    echo "  2. 网络连接是否正常"
    echo "  3. IP地址是否正确"
    exit 1
fi

echo ""
echo -e "${YELLOW}步骤2: 停止旧的应用进程...${NC}"
ssh "${BOARD_USER}@${BOARD_IP}" "killall ${APP_NAME} 2>/dev/null || true"
echo -e "${GREEN}✓ 已停止旧进程${NC}"

echo ""
echo -e "${YELLOW}步骤3: 传输可执行文件...${NC}"
echo "文件大小: $(ls -lh ${APP_NAME} | awk '{print $5}')"
scp "${APP_NAME}" "${BOARD_USER}@${BOARD_IP}:${TARGET_DIR}/"
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ 传输成功${NC}"
else
    echo -e "${RED}✗ 传输失败${NC}"
    exit 1
fi

echo ""
echo -e "${YELLOW}步骤4: 设置执行权限...${NC}"
ssh "${BOARD_USER}@${BOARD_IP}" "chmod +x ${TARGET_DIR}/${APP_NAME}"
echo -e "${GREEN}✓ 权限设置完成${NC}"

echo ""
echo -e "${YELLOW}步骤5: 传输配置文件（如果需要）...${NC}"
# 如果有配置文件，取消下面的注释
# if [ -f "lv_conf.h" ]; then
#     scp lv_conf.h "${BOARD_USER}@${BOARD_IP}:/etc/"
# fi
echo -e "${GREEN}✓ 配置文件已跳过（如需要请修改脚本）${NC}"

echo ""
echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}部署完成！${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo "在开发板上运行应用："
echo "  ssh ${BOARD_USER}@${BOARD_IP}"
echo "  ${TARGET_DIR}/${APP_NAME}"
echo ""
echo "或者直接远程运行："
echo "  ssh ${BOARD_USER}@${BOARD_IP} '${TARGET_DIR}/${APP_NAME}'"
echo ""
echo -e "${YELLOW}提示:${NC}"
echo "  - 确保 /dev/fb0 和 /dev/input/event0 设备存在"
echo "  - 如需CAN功能，确保 can0 接口可用"
echo "  - 可能需要root权限运行"

