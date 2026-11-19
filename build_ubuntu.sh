#!/bin/bash
# YomkDds 扩展（dds 域）Ubuntu 一键安装脚本（参考 YomkServerPy build_ubuntu.sh）
# 纯 Python 扩展：无编译产物，CMake 仅负责版本注入与落盘安装
# 用法: ./build_ubuntu.sh
# 功能:
#   1. 依赖检查（cmake/python3/g++/swig/python3-dev）
#   2. 交互输入安装路径（默认取 YOMK_PREFIX_PATH，即框架安装前缀——扩展按 PEP 420
#      命名空间并入框架 site-packages/yomk/，必须与 YomkServerPy 同前缀）
#   3. CMake 配置 + 安装：版本号单一来源为 CMakeLists.txt project(VERSION)，
#      configure_file 生成 yomk/dds/_version.py 注入安装副本；
#      install 期校验框架就位（纯文件检查，含命名空间守卫于 configure 期）
#   3.1 编译安装 msg 类型库（经 scripts/build_idl.sh 从 idl/*.idl 直接生成/编译/安装）
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
for cmd in cmake python3 g++; do
    command -v "${cmd}" >/dev/null 2>&1 || fail "缺少依赖: ${cmd}（请手动安装后重试）"
done
# SWIG（msg SWIG Python 绑定编译需要，4.0.x；Ubuntu noble 可执行名可能为 swig4.1）
command -v swig >/dev/null 2>&1 || command -v swig4.1 >/dev/null 2>&1 \
    || fail "缺少依赖: swig（sudo apt install swig，需 4.0.x）"
# Python 开发头文件（Python.h，SWIG 绑定编译必需）
python3 -c "import sysconfig, os; assert os.path.exists(os.path.join(sysconfig.get_paths()['include'], 'Python.h'))" 2>/dev/null \
    || fail "缺少依赖: python3-dev（Python.h，sudo apt install python3-dev）"
echo "-- 依赖检查通过: cmake, python3, g++, swig, python3-dev"
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

# -------------sl 3.1 编译安装 msg 类型库（经 scripts/build_idl.sh 从 idl/*.idl 直编） ----------------
# idl/ 仅存 .idl 源文件（生成源码不入库）：逐个经 build_idl.sh 完成
# fastddsgen 生成（loan 支持）→ 编译 → 安装（含 posix_local 目的地校正、
# yomk/dds 域层级去重与同名层合并）。Fast-DDS 已随主安装落入前缀，
# build_idl.sh 的 find_package 经 CMAKE_PREFIX_PATH（安装前缀 + 框架前缀回退）解析；
# 编译以调用者身份执行、仅 install 走特权（与主安装相同的特权边界）
for idl_file in "${SCRIPT_DIR}"/idl/*.idl; do
    [ -f "${idl_file}" ] || continue
    echo "-- 构建 msg 类型库: $(basename "${idl_file}")"
    bash "${SCRIPT_DIR}/scripts/build_idl.sh" "${idl_file}" -p "${INSTALL_PREFIX}" \
        || fail "msg 类型库构建失败: ${idl_file}"
done
# -------------el 3.1 编译安装 msg 类型库 ----------------

# -------------sl 3.2 注册动态库搜索路径（对齐 C++ YomkDds build_ubuntu.sh，幂等追加） ----------------
# 第三方 Fast-DDS 动态库（libfastdds/libfastcdr 等）随库落入 ${INSTALL_PREFIX}/lib，
# fastdds 绑定模块（_fastdds_python.so）无 RPATH，需注册 ldconfig 方可解析依赖
YOMK_LDCONF_FILE="/etc/ld.so.conf.d/yomk.conf"
if ! grep -qxF "${INSTALL_PREFIX}/lib" "${YOMK_LDCONF_FILE}" 2>/dev/null; then
    echo "-- 注册动态库搜索路径: ${YOMK_LDCONF_FILE}"
    echo "${INSTALL_PREFIX}/lib" | sudo tee -a "${YOMK_LDCONF_FILE}" >/dev/null \
        || fail "动态库搜索路径注册失败"
fi
# 刷新动态库缓存：新增的 so 不会自动进入 ld.so.cache，必须重新执行 ldconfig
echo "-- 刷新动态库缓存 (ldconfig)..."
sudo ldconfig || fail "ldconfig 执行失败"
# -------------el 3.2 注册动态库搜索路径 ----------------

# -------------sl 3.5 安装后 import 冒烟（调用者身份，非 sudo 环境） ----------------
PY_SITE_DIR="$(ls -d "${INSTALL_PREFIX}"/lib/python*/site-packages 2>/dev/null | head -1)"
[ -n "${PY_SITE_DIR}" ] || fail "安装后未找到 site-packages 目录: ${INSTALL_PREFIX}/lib/pythonX.Y/site-packages"
echo "-- 扩展导入验证（PYTHONPATH=${PY_SITE_DIR}）..."
PYTHONPATH="${PY_SITE_DIR}${PYTHONPATH:+:${PYTHONPATH}}" python3 -c "import yomk.dds, yomk.msgs.dds, yomk.url.dds, fastdds, yomk.dds_idl.std_msgs; print('-- 扩展导入验证通过, 版本:', yomk.dds.__version__, ', fastdds 绑定:', fastdds.__file__ is not None, ', msg 类型库:', yomk.dds_idl.std_msgs.__file__ is not None)" \
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
