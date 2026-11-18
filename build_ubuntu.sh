#!/bin/bash
# YomkDds 扩展（dds 域）Ubuntu 一键安装脚本（参考 YomkServerPy build_ubuntu.sh）
# 纯 Python 扩展：无编译产物，CMake 仅负责版本注入与落盘安装
# 用法: ./build_ubuntu.sh
# 功能:
#   1. 依赖检查（cmake + python3）
#   2. 交互输入安装路径（默认取 YOMK_PREFIX_PATH，即框架安装前缀——扩展按 PEP 420
#      命名空间并入框架 site-packages/yomk/，必须与 YomkServerPy 同前缀）
#   3. CMake 配置 + 安装：版本号单一来源为 CMakeLists.txt project(VERSION)，
#      configure_file 生成 yomk/dds/_version.py 注入安装副本；
#      install 期校验框架就位（纯文件检查，含命名空间守卫于 configure 期）
#   4. 安装后 import 冒烟（调用者身份执行——sudo 安装时 root 看不见 user-site 的
#      pip 依赖 readerwriterlock，故冒烟不放 install(CODE)，与框架设计一致）

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 依赖检查 ----------------
for cmd in cmake python3; do
    command -v "${cmd}" >/dev/null 2>&1 || fail "缺少依赖: ${cmd}（请手动安装后重试）"
done
echo "-- 依赖检查通过: cmake, python3"
# -------------el 1. 依赖检查 ----------------

# -------------sl 2. 交互确定安装路径 ----------------
DEFAULT_PREFIX="${YOMK_PREFIX_PATH:-/opt/yomk}"
read -r -p "请输入安装路径（须与 YomkServerPy 同前缀）[默认: ${DEFAULT_PREFIX}]: " input_prefix || input_prefix=""
INSTALL_PREFIX="${input_prefix:-${DEFAULT_PREFIX}}"

# 展开 ~ 为 $HOME
INSTALL_PREFIX="${INSTALL_PREFIX/#\~/$HOME}"
# 非绝对路径自动补全为基于当前目录的绝对路径
if [[ "${INSTALL_PREFIX}" != /* ]]; then
    INSTALL_PREFIX="$(pwd)/${INSTALL_PREFIX}"
fi
# 去除末尾的 /（根目录除外）
INSTALL_PREFIX="${INSTALL_PREFIX%/}"
[ -z "${INSTALL_PREFIX}" ] && INSTALL_PREFIX="/"

echo "-- 安装路径: ${INSTALL_PREFIX}"
# -------------el 2. 交互确定安装路径 ----------------

# -------------sl 3. CMake 配置 + 安装 ----------------
echo "-- CMake 配置..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    || fail "cmake 配置失败"

echo "-- CMake 安装..."
SUDO=""
if [ ! -d "${INSTALL_PREFIX}" ] || [ ! -w "${INSTALL_PREFIX}" ]; then
    SUDO="sudo"
fi
${SUDO} cmake --install "${BUILD_DIR}" || fail "cmake 安装失败"
# -------------el 3. CMake 配置 + 安装 ----------------

# -------------sl 3.5 安装后 import 冒烟（调用者身份，非 sudo 环境） ----------------
PY_SITE_DIR="$(ls -d "${INSTALL_PREFIX}"/lib/python*/site-packages 2>/dev/null | head -1)"
[ -n "${PY_SITE_DIR}" ] || fail "安装后未找到 site-packages 目录: ${INSTALL_PREFIX}/lib/pythonX.Y/site-packages"
echo "-- 扩展导入验证（PYTHONPATH=${PY_SITE_DIR}）..."
PYTHONPATH="${PY_SITE_DIR}${PYTHONPATH:+:${PYTHONPATH}}" python3 -c "import yomk.dds, yomk.msgs.dds, yomk.url.dds; print('-- 扩展导入验证通过, 版本:', yomk.dds.__version__)" \
    || fail "扩展安装后导入验证失败（若为依赖缺失，请确认当前用户已安装框架运行时依赖）"
# -------------el 3.5 安装后 import 冒烟 ----------------

# -------------sl 4. 安装成功提示 ----------------
echo ""
echo "==========================================="
echo " YomkDds 扩展安装成功!"
echo "-------------------------------------------"
echo " 安装路径:   ${INSTALL_PREFIX}"
echo " 可运行验证: ${SCRIPT_DIR}/test/run_tests.sh"
echo "==========================================="
echo ""
echo "版本号单一来源为 CMakeLists.txt 的 project(VERSION)，发布时手动 bump 后重跑本脚本"
# -------------el 4. 安装成功提示 ----------------
