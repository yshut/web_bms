#!/bin/bash
# UDS日志批量修复脚本
# 将所有uds_log调用分类为ERROR/PROGRESS/INFO/DEBUG

cd logic || exit 1

echo "备份原文件..."
cp uds_handler.c uds_handler.c.backup

echo "开始批量替换日志级别..."

# ===== 错误日志 =====
echo "1. 替换ERROR日志..."
sed -i 's/uds_log("\[ERROR\]/uds_log_error("[ERROR/g' uds_handler.c
sed -i 's/uds_log(err_msg)/uds_log_error(err_msg)/g' uds_handler.c
sed -i 's/uds_log(final_err)/uds_log_error(final_err)/g' uds_handler.c
sed -i 's/uds_log(exit_err)/uds_log_error(exit_err)/g' uds_handler.c

# ===== 进度日志 =====
echo "2. 替换PROGRESS日志..."
sed -i 's/uds_log("\[START\]/uds_log_progress("[START/g' uds_handler.c
sed -i 's/uds_log("\[DONE\]/uds_log_progress("[DONE/g' uds_handler.c
sed -i 's/uds_log("\[PROGRESS\]/uds_log_progress("[PROGRESS/g' uds_handler.c
sed -i 's/uds_log(progress_msg)/uds_log_progress(progress_msg)/g' uds_handler.c

# ===== 调试日志（不显示）=====
echo "3. 替换DEBUG日志..."
sed -i 's/uds_log("\[DEBUG\]/uds_log_debug("[DEBUG/g' uds_handler.c
sed -i 's/uds_log("\[BLOCK\]/uds_log_debug("[BLOCK/g' uds_handler.c
sed -i 's/uds_log(block_msg)/uds_log_debug(block_msg)/g' uds_handler.c
sed -i 's/uds_log(pending_msg)/uds_log_debug(pending_msg)/g' uds_handler.c
sed -i 's/uds_log(debug_msg)/uds_log_debug(debug_msg)/g' uds_handler.c
sed -i 's/uds_log(dbg)/uds_log_debug(dbg)/g' uds_handler.c
sed -i 's/uds_log(tx_msg)/uds_log_debug(tx_msg)/g' uds_handler.c
sed -i 's/uds_log(peek_msg)/uds_log_debug(peek_msg)/g' uds_handler.c
sed -i 's/uds_log(wait_msg)/uds_log_debug(wait_msg)/g' uds_handler.c
sed -i 's/uds_log(retry_msg)/uds_log_debug(retry_msg)/g' uds_handler.c
sed -i 's/uds_log("\[WARN\]/uds_log_debug("[WARN/g' uds_handler.c

# ===== 信息日志 =====
echo "4. 替换INFO日志..."
sed -i 's/uds_log("\[INFO\]/uds_log_info("[INFO/g' uds_handler.c
sed -i 's/uds_log("\[TX\]/uds_log_info("[TX/g' uds_handler.c
sed -i 's/uds_log("\[RX\]/uds_log_info("[RX/g' uds_handler.c
sed -i 's/uds_log("\[SEG /uds_log_info("[SEG /g' uds_handler.c
sed -i 's/uds_log(msg)/uds_log_info(msg)/g' uds_handler.c
sed -i 's/uds_log(seg_msg)/uds_log_info(seg_msg)/g' uds_handler.c
sed -i 's/uds_log(key_str)/uds_log_info(key_str)/g' uds_handler.c
sed -i 's/uds_log("\[STOP\]/uds_log_info("[STOP/g' uds_handler.c
sed -i 's/uds_log(routine_msg)/uds_log_info(routine_msg)/g' uds_handler.c

echo "完成！"
echo "备份文件: uds_handler.c.backup"
echo "请检查修改后的文件并编译测试"

