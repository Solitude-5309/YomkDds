#!/bin/bash
# install_idl.sh —— IDL 消息类型一键 生成/编译/安装（支持 loan 的 msg 类型）
# 用法: bash scripts/install_idl.sh <idl 文件路径>
# 流程:
#   1. 生成：当前目录 ./msg_<时间戳> 下生成支持 loan 的类型工程
#      （fastddsgen -python -d . -de final <idl>）；
#      生成工具使用 $YOMK_PREFIX_PATH 安装树内的 fastddsgen（/opt/yomk/scripts/fastddsgen），
#      不使用源码 thirdparty 中的生成工具
#   2. 编译：在 ./msg_<时间戳> 内 cmake 配置/编译
#      （CMAKE_PREFIX_PATH 指向安装前缀，find_package(fastcdr/fastdds) 经前缀内
#      Fast-DDS 安装树解析）
#   3. 安装：默认 $YOMK_PREFIX_PATH（交互询问，支持修改），与现有安装一致：
#      C++ 类型库落 <前缀>/lib，SWIG Python 模块并入扩展域 yomk/dds/
#      （Debian posix_local scheme 使 sysconfig purelib 算出 <前缀>/local/.../dist-packages，
#      安装后自动校正搬移并清 local/ 空壳），随后刷新动态库缓存

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fail() {
    echo "错误: $1" >&2
    exit 1
}

# 路径规范化：展开 ~ 、相对路径补全
_normalize_path() {
    local p="$1"
    p="${p/#\~/$HOME}"
    if [[ -n "${p}" && "${p}" != /* ]]; then
        p="$(pwd)/${p}"
    fi
    echo "${p}"
}

# -------------sl 1. 依赖检查 ----------------
for cmd in cmake python3 g++; do
    command -v "${cmd}" >/dev/null 2>&1 || fail "缺少依赖: ${cmd}（请手动安装后重试）"
done
# SWIG（生成的类型工程编译需要，4.0.x；Ubuntu noble 可执行名可能为 swig4.1）
command -v swig >/dev/null 2>&1 || command -v swig4.1 >/dev/null 2>&1 \
    || fail "缺少依赖: swig（sudo apt install swig，需 4.0.x）"
# Python 开发头文件（Python.h，SWIG 绑定编译必需）
python3 -c "import sysconfig, os; assert os.path.exists(os.path.join(sysconfig.get_paths()['include'], 'Python.h'))" 2>/dev/null \
    || fail "缺少依赖: python3-dev（Python.h，sudo apt install python3-dev）"
# java（fastddsgen 运行时需要 java 17）
java -version >/dev/null 2>&1 || fail "缺少依赖: java（sudo apt install openjdk-17-jre）"
echo "-- 依赖检查通过: cmake, python3, g++, swig, python3-dev, java"
# -------------el 1. 依赖检查 ----------------

# -------------sl 2. 解析 IDL 文件与生成工具 ----------------
[ $# -ge 1 ] || fail "用法: bash scripts/install_idl.sh <idl 文件路径>"
IDL_FILE="$(_normalize_path "$1")"
[ -f "${IDL_FILE}" ] || fail "IDL 文件不存在: ${IDL_FILE}"

# 生成工具：仅使用 YOMK_PREFIX_PATH 安装树内的 fastddsgen，不用源码 thirdparty 中的
INSTALL_PREFIX_DEFAULT="${YOMK_PREFIX_PATH:-/opt/yomk}"
FASTDDSGEN="${INSTALL_PREFIX_DEFAULT}/scripts/fastddsgen"
[ -f "${FASTDDSGEN}" ] || fail "未找到生成工具 ${FASTDDSGEN}，请先安装扩展（./build_ubuntu.sh，随库安装 Fast-DDS-Gen）"
echo "-- 生成工具: ${FASTDDSGEN}（安装树内）"
# -------------el 2. 解析 IDL 文件与生成工具 ----------------

# -------------sl 3. 交互确定安装路径（默认 YOMK_PREFIX_PATH，支持修改） ----------------
read -r -p "请输入安装路径 [默认: ${INSTALL_PREFIX_DEFAULT}]: " input_prefix || input_prefix=""
INSTALL_PREFIX="${input_prefix:-${INSTALL_PREFIX_DEFAULT}}"
INSTALL_PREFIX="$(_normalize_path "${INSTALL_PREFIX}")"
INSTALL_PREFIX="${INSTALL_PREFIX%/}"
[ -z "${INSTALL_PREFIX}" ] && INSTALL_PREFIX="/"
echo "-- 安装路径: ${INSTALL_PREFIX}"

# site-packages 不存在时自动生成（安装前缀可自定义，不必预先存在）
SITE_PACKAGES="$(ls -d "${INSTALL_PREFIX}"/lib/python*/site-packages 2>/dev/null | head -1)"
if [ -z "${SITE_PACKAGES}" ]; then
    PY_VER="$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")"
    SITE_PACKAGES="${INSTALL_PREFIX}/lib/python${PY_VER}/site-packages"
    echo "-- 自动生成 site-packages: ${SITE_PACKAGES}"
    mkdir -p "${SITE_PACKAGES}" || fail "site-packages 创建失败: ${SITE_PACKAGES}"
fi
DDS_DIR="${SITE_PACKAGES}/yomk/dds"
mkdir -p "${DDS_DIR}" || fail "扩展域目录创建失败: ${DDS_DIR}"
# -------------el 3. 交互确定安装路径 ----------------

# -------------sl 4. 生成（./msg_<时间戳>，支持 loan） ----------------
GENDIR="$(pwd)/msg_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${GENDIR}"
echo "-- 生成目录: ${GENDIR}"
echo "-- 生成 loan 支持 msg: fastddsgen -python -d . -de final ${IDL_FILE}"
# 生成器包装脚本未带执行位，经 sh 调起（不改安装树文件）；
# cwd 为 GENDIR，-d . 相对 cwd 落在 GENDIR 本层
cd "${GENDIR}"
sh "${FASTDDSGEN}" -python -d . -de final "${IDL_FILE}" || fail "fastddsgen 生成失败"
[ -f "${GENDIR}/CMakeLists.txt" ] || fail "生成产物缺少 CMakeLists.txt: ${GENDIR}"
# 记录安装前 Python 模块目的地已有条目（用于安装后只搬移本次新增，不动既有包）
MODULE_SRC_BASE="$(python3 -c "from sysconfig import get_path; print(get_path('purelib', vars={'base': '${INSTALL_PREFIX}'}))")"
SNAP_BEFORE="$(mktemp)"
[ -d "${MODULE_SRC_BASE}" ] && ls -A "${MODULE_SRC_BASE}" > "${SNAP_BEFORE}" || : > "${SNAP_BEFORE}"
# -------------el 4. 生成 ----------------

# -------------sl 5. 编译（在 ./msg_<时间戳> 内） ----------------
# 依赖解析：安装前缀优先，回退框架前缀（与生成工具同源）——
# 自定义前缀可能尚无 Fast-DDS 安装树，find_package(fastcdr/fastdds) 需回退解析
echo "-- 编译 msg 类型工程..."
cmake -S "${GENDIR}" -B "${GENDIR}/build" \
    -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX};${INSTALL_PREFIX_DEFAULT}" \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release \
    || fail "msg 类型工程 cmake 配置失败"
cmake --build "${GENDIR}/build" -j"$(nproc)" || fail "msg 类型工程编译失败"
# -------------el 5. 编译 ----------------

# -------------sl 6. 安装（默认 YOMK_PREFIX_PATH） ----------------
SUDO=""
if [ ! -d "${INSTALL_PREFIX}" ] || [ ! -w "${INSTALL_PREFIX}" ]; then
    SUDO="sudo"
fi
echo "-- 安装到 ${INSTALL_PREFIX} ..."
${SUDO} cmake --install "${GENDIR}/build" || fail "msg 类型工程安装失败"
# -------------el 6. 安装 ----------------

# -------------sl 7. 目的地校正 ----------------
# 生成的 CMakeLists 经 sysconfig purelib(vars={'base': 前缀}) 计算模块目的地，
# Debian/Ubuntu 系统 Python 为 posix_local scheme，落在 <前缀>/local/.../dist-packages
# （sys.path 外不可导入）；非 Debian 布局则直接落在 site-packages 顶层。
# 目的地按前缀分派（仅搬本次新增条目，不动既有包）：
# - 默认前缀（与框架同前缀）：搬入扩展域 yomk/dds/ 与主库同域，导入 yomk.dds.<模块>
# - 自定义前缀：搬至该前缀 site-packages 顶层（保持生成布局）——框架侧的 yomk.dds
#   是常规包（带 __init__.py），按 PEP 420 会遮蔽自定义前缀的 yomk.dds 命名空间段，
#   子挂 yomk/dds 在自定义前缀下不可导入
if [ "${INSTALL_PREFIX}" = "${INSTALL_PREFIX_DEFAULT}" ]; then
    TARGET_BASE="${DDS_DIR}"
    IMPORT_NS="yomk.dds"
else
    TARGET_BASE="${SITE_PACKAGES}"
    IMPORT_NS=""
fi
NEW_ENTRIES="$(ls -A "${MODULE_SRC_BASE}" 2>/dev/null | while read -r e; do grep -qxF "${e}" "${SNAP_BEFORE}" || echo "${e}"; done)"
rm -f "${SNAP_BEFORE}"
MOVED=""
MOVED_IMPORTS=""
for e in ${NEW_ENTRIES}; do
    if [ "${MODULE_SRC_BASE}" != "${TARGET_BASE}" ]; then
        echo "-- 目的地校正: ${MODULE_SRC_BASE}/${e} -> ${TARGET_BASE}/${e}"
        ${SUDO} rm -rf "${TARGET_BASE}/${e}"
        ${SUDO} mv "${MODULE_SRC_BASE}/${e}" "${TARGET_BASE}/${e}" || fail "目的地校正失败: ${e}"
    fi
    MOVED="${MOVED}${e} "
    # 推导可导入名：条目自带 __init__.py 则整条即模块；否则枚举其含 __init__.py 的子包
    if [ -f "${TARGET_BASE}/${e}/__init__.py" ]; then
        MOVED_IMPORTS="${MOVED_IMPORTS}${IMPORT_NS:+${IMPORT_NS}.}${e} "
    else
        for sub in $(find "${TARGET_BASE}/${e}" -mindepth 1 -maxdepth 1 -type d 2>/dev/null); do
            [ -f "${sub}/__init__.py" ] && MOVED_IMPORTS="${MOVED_IMPORTS}${IMPORT_NS:+${IMPORT_NS}.}${e}.${sub##*/} "
        done
    fi
done
# 清理 local/ 空壳（dist-packages -> python3.10 -> lib -> local，逐级 rmdir，
# 非空目录 rmdir 失败即保留，安全）；仅当源目的地与 site-packages 不同级时需要
if [ "${MODULE_SRC_BASE}" != "${SITE_PACKAGES}" ]; then
    for d in "${MODULE_SRC_BASE}" "${MODULE_SRC_BASE%/*}" "${MODULE_SRC_BASE%/*/*}" "${INSTALL_PREFIX}/local"; do
        ${SUDO} rmdir "${d}" 2>/dev/null || true
    done
fi
[ -n "${MOVED}" ] || echo "-- 警告: 未检测到新增 Python 模块条目，请人工核对 ${MODULE_SRC_BASE}"
# -------------el 7. 目的地校正 ----------------

# -------------sl 8. 刷新动态库缓存（C++ 类型库 lib<工程名>.so 落 <前缀>/lib） ----------------
if sudo -n true 2>/dev/null; then
    YOMK_LDCONF_FILE="/etc/ld.so.conf.d/yomk.conf"
    if ! grep -qxF "${INSTALL_PREFIX}/lib" "${YOMK_LDCONF_FILE}" 2>/dev/null; then
        echo "${INSTALL_PREFIX}/lib" | sudo tee -a "${YOMK_LDCONF_FILE}" >/dev/null || true
    fi
    echo "-- 刷新动态库缓存 (ldconfig)..."
    sudo ldconfig || echo "-- 警告: ldconfig 执行失败，请手动执行: sudo ldconfig"
else
    echo "-- 提示: 无免密 sudo，请手动执行注册动态库缓存: sudo ldconfig"
fi
# -------------el 8. 刷新动态库缓存 ----------------

# -------------sl 9. 安装验证（扩展域内导入） ----------------
# 类型模块运行时依赖 fastdds 绑定与 Fast-DDS 动态库：自定义前缀无这些产物时，
# 验证环境需回退挂载框架前缀（与生成工具同源）的 site-packages 与 lib
echo "-- 安装验证..."
VERIFY_RC=0
VERIFY_SITE="${SITE_PACKAGES}"
FRAMEWORK_SITE="$(ls -d "${INSTALL_PREFIX_DEFAULT}"/lib/python*/site-packages 2>/dev/null | head -1)"
if [ "${INSTALL_PREFIX}" != "${INSTALL_PREFIX_DEFAULT}" ] && [ -n "${FRAMEWORK_SITE}" ]; then
    VERIFY_SITE="${SITE_PACKAGES}:${FRAMEWORK_SITE}"
fi
for m in ${MOVED_IMPORTS}; do
    LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX_DEFAULT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
        PYTHONPATH="${VERIFY_SITE}${PYTHONPATH:+:${PYTHONPATH}}" \
        python3 -c "import ${m}" \
        || { echo "错误: ${m} 导入失败" >&2; VERIFY_RC=1; }
    [ ${VERIFY_RC} -eq 0 ] && echo "-- ${m} 导入通过"
done
# -------------el 9. 安装验证 ----------------

echo ""
echo "==========================================="
echo " IDL 类型工程安装完成!"
echo "-------------------------------------------"
echo " 生成/编译目录: ${GENDIR}"
echo " 安装路径:      ${INSTALL_PREFIX}"
echo " Python 模块:   ${TARGET_BASE}/{${MOVED% }}"
echo " 导入形态:      ${MOVED_IMPORTS% }"
echo " C++ 类型库:    ${INSTALL_PREFIX}/lib/"
if [ "${INSTALL_PREFIX}" != "${INSTALL_PREFIX_DEFAULT}" ]; then
    echo " 提示: 自定义前缀不在系统 PYTHONPATH 内，使用时请将其 site-packages 挂入 PYTHONPATH"
fi
echo "==========================================="
exit ${VERIFY_RC}
