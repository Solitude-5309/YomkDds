#!/bin/bash
# install_idl.sh —— IDL 消息类型一键 生成/编译/安装（支持 loan 的 msg 类型）
# 用法: bash scripts/install_idl.sh <idl 文件路径> [-p|--prefix 安装前缀]
#       安装前缀推荐用 -p 传入（可 tab 补全已存在路径），缺省交互询问
# 流程:
#   1. 生成：当前目录 ./build_idl_<时间戳> 下生成支持 loan 的类型工程
#      （fastddsgen -python -d . -de final <idl>）；
#      生成工具使用 $YOMK_PREFIX_PATH 安装树内的 fastddsgen（/opt/yomk/scripts/fastddsgen），
#      不使用源码 thirdparty 中的生成工具
#   2. 编译：在 ./build_idl_<时间戳> 内 cmake 配置/编译
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

# -------------sl 2. 解析参数（IDL 文件 + 可选 -p 安装前缀） ----------------
IDL_FILE=""
ARG_PREFIX=""
while [ $# -gt 0 ]; do
    case "$1" in
        -p|--prefix) [ $# -ge 2 ] || fail "-p/--prefix 需要目录参数"
                     ARG_PREFIX="$2"; shift ;;
        -h|--help)   sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           if [ -z "${IDL_FILE}" ]; then IDL_FILE="$1"; else fail "多余参数: $1"; fi ;;
    esac
    shift
done
[ -n "${IDL_FILE}" ] || fail "用法: bash scripts/install_idl.sh <idl 文件路径> [-p|--prefix 安装前缀]"
IDL_FILE="$(_normalize_path "${IDL_FILE}")"
[ -f "${IDL_FILE}" ] || fail "IDL 文件不存在: ${IDL_FILE}"

# 生成工具：仅使用 YOMK_PREFIX_PATH 安装树内的 fastddsgen，不用源码 thirdparty 中的
INSTALL_PREFIX_DEFAULT="${YOMK_PREFIX_PATH:-/opt/yomk}"
FASTDDSGEN="${INSTALL_PREFIX_DEFAULT}/scripts/fastddsgen"
[ -f "${FASTDDSGEN}" ] || fail "未找到生成工具 ${FASTDDSGEN}，请先安装扩展（./build_ubuntu.sh，随库安装 Fast-DDS-Gen）"
echo "-- 生成工具: ${FASTDDSGEN}（安装树内）"
# -------------el 2. 解析 IDL 文件与生成工具 ----------------

# -------------sl 3. 确定安装路径（默认 YOMK_PREFIX_PATH，支持修改） ----------------
if [ -n "${ARG_PREFIX}" ]; then
    # 命令行指定（-p/--prefix）：走 shell 参数传递 + tab 补全（补全只匹配已存在
    # 路径），完全绕开交互 read 输入链路——推荐用法
    INSTALL_PREFIX="${ARG_PREFIX}"
else
    read -r -p "请输入安装路径 [默认: ${INSTALL_PREFIX_DEFAULT}]: " input_prefix || input_prefix=""
    # 调试开关: INSTALL_IDL_DEBUG=1 输出 read 收到的原始字节，
    # 定位输入链路丢字符问题（丢在 tty/粘贴层还是脚本层，用证据说话）
    if [ -n "${INSTALL_IDL_DEBUG:-}" ]; then
        echo "-- 调试: read 原始输入字节:" >&2
        printf '%s' "${input_prefix}" | od -c >&2
    fi
    INSTALL_PREFIX="${input_prefix:-${INSTALL_PREFIX_DEFAULT}}"
fi
INSTALL_PREFIX="$(_normalize_path "${INSTALL_PREFIX}")"
INSTALL_PREFIX="${INSTALL_PREFIX%/}"
[ -z "${INSTALL_PREFIX}" ] && INSTALL_PREFIX="/"
echo "-- 安装路径: ${INSTALL_PREFIX}"

# 前缀目录不存在时：先校验父目录存在（拼错的路径父目录必然不存在，
# 硬失败防止静默创建错误目录），再交互确认
if [ ! -d "${INSTALL_PREFIX}" ]; then
    PARENT_DIR="$(dirname "${INSTALL_PREFIX}")"
    if [ ! -d "${PARENT_DIR}" ]; then
        fail "安装前缀的父目录不存在: ${PARENT_DIR} —— 疑似路径拼写错误，请核对后重试（正确路径的父目录应已存在，或用 -p 参数经 tab 补全输入）"
    fi
    read -r -p "安装前缀目录不存在，将自动创建: ${INSTALL_PREFIX}，确认? [y/N]: " confirm || confirm=""
    case "${confirm}" in
        [yY]*) ;;
        *) fail "已取消（检查安装路径拼写后重试）" ;;
    esac
fi

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

# -------------sl 4. 生成（./build_idl_<时间戳>，支持 loan） ----------------
GENDIR="$(pwd)/build_idl_$(date +%Y%m%d_%H%M%S)"
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

# -------------sl 5. 编译（在 ./build_idl_<时间戳> 内） ----------------
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
# 目的地统一为扩展域 yomk/dds/，与主库同域（导入 yomk.dds.<模块>）；
# 仅搬本次新增条目，不动既有包。
# 注意：自定义前缀若仅含 msg 模块（未完整部署扩展域），与框架同时挂载时，
# 框架侧的 yomk.dds 常规包（带 __init__.py）按 PEP 420 会遮蔽本前缀的
# yomk.dds 命名空间段——验证步骤会检测此情况并给出部署提示
TARGET_BASE="${DDS_DIR}"
IMPORT_NS="yomk.dds"
DOMAIN_REL="${DDS_DIR#"${SITE_PACKAGES}/"}"   # 扩展域相对段，如 yomk/dds
NEW_ENTRIES="$(ls -A "${MODULE_SRC_BASE}" 2>/dev/null | while read -r e; do grep -qxF "${e}" "${SNAP_BEFORE}" || echo "${e}"; done)"
rm -f "${SNAP_BEFORE}"
PLACED=""
for e in ${NEW_ENTRIES}; do
    SRC="${MODULE_SRC_BASE}/${e}"
    DOMAIN_FIRST="${DOMAIN_REL%%/*}"
    if [ "${e}" = "${DOMAIN_FIRST}" ]; then
        # IDL module 声明与扩展域同名（如 module yomk { module dds）时，生成的
        # CMakeLists 会以 module 名为父目录，出现 yomk/dds/yomk/dds 双层——
        # 沿域相对段向内剥离重复组件，类型工程目录直挂 DDS_DIR
        POS="${SRC}"
        REMAIN="${DOMAIN_REL#*/}"
        while [ -n "${REMAIN}" ]; do
            c="${REMAIN%%/*}"
            if [ -d "${POS}/${c}" ]; then
                POS="${POS}/${c}"
                if [ "${REMAIN}" = "${c}" ]; then REMAIN=""; else REMAIN="${REMAIN#*/}"; fi
            else
                break
            fi
        done
        for child in $(ls -A "${POS}" 2>/dev/null); do
            # 剩余 module 层与工程目录（文件名）同名时合并为一层
            # （如 module ... std_idl + 文件 std_idl.idl → std_idl/std_idl）
            if [ -d "${POS}/${child}/${child}" ]; then
                echo "-- 目的地校正(同名层级合并): ${POS}/${child}/${child} -> ${DDS_DIR}/${child}"
                ${SUDO} rm -rf "${DDS_DIR}/${child}"
                ${SUDO} mv "${POS}/${child}/${child}" "${DDS_DIR}/${child}" || fail "目的地校正失败: ${child}"
                ${SUDO} rm -rf "${POS}/${child}"
            else
                echo "-- 目的地校正(域层级去重): ${POS}/${child} -> ${DDS_DIR}/${child}"
                ${SUDO} rm -rf "${DDS_DIR}/${child}"
                ${SUDO} mv "${POS}/${child}" "${DDS_DIR}/${child}" || fail "目的地校正失败: ${child}"
            fi
            PLACED="${PLACED}${child} "
        done
        ${SUDO} rmdir "${POS}" "${SRC}" 2>/dev/null || true
    else
        if [ "${MODULE_SRC_BASE}" != "${TARGET_BASE}" ]; then
            echo "-- 目的地校正: ${SRC} -> ${TARGET_BASE}/${e}"
            ${SUDO} rm -rf "${TARGET_BASE}/${e}"
            ${SUDO} mv "${SRC}" "${TARGET_BASE}/${e}" || fail "目的地校正失败: ${e}"
        fi
        PLACED="${PLACED}${e} "
    fi
done
# 可导入名推导：本次放入 DDS_DIR 的子项中含 __init__.py 的即类型模块
MOVED_IMPORTS=""
for c in ${PLACED}; do
    [ -f "${DDS_DIR}/${c}/__init__.py" ] && MOVED_IMPORTS="${MOVED_IMPORTS}${IMPORT_NS}.${c} "
done
# 清理 local/ 空壳（dist-packages -> python3.10 -> lib -> local，逐级 rmdir，
# 非空目录 rmdir 失败即保留，安全）；仅当源目的地与 site-packages 不同级时需要
if [ "${MODULE_SRC_BASE}" != "${SITE_PACKAGES}" ]; then
    for d in "${MODULE_SRC_BASE}" "${MODULE_SRC_BASE%/*}" "${MODULE_SRC_BASE%/*/*}" "${INSTALL_PREFIX}/local"; do
        ${SUDO} rmdir "${d}" 2>/dev/null || true
    done
fi
[ -n "${PLACED}" ] || echo "-- 警告: 未检测到新增 Python 模块条目，请人工核对 ${MODULE_SRC_BASE}"
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

# -------------sl 9. 安装验证 ----------------
echo "-- 安装验证..."
VERIFY_RC=0
FRAMEWORK_SITE="$(ls -d "${INSTALL_PREFIX_DEFAULT}"/lib/python*/site-packages 2>/dev/null | head -1)"
for m in ${MOVED_IMPORTS}; do
    if [ "${INSTALL_PREFIX}" = "${INSTALL_PREFIX_DEFAULT}" ]; then
        # 与框架同前缀：单一 site-packages 全链导入
        if LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
                PYTHONPATH="${SITE_PACKAGES}${PYTHONPATH:+:${PYTHONPATH}}" \
                python3 -c "import ${m}" 2>/dev/null; then
            echo "-- ${m} 导入通过"
        else
            echo "错误: ${m} 导入失败" >&2
            VERIFY_RC=1
        fi
    else
        # 自定义前缀：全链导入须断言命中安装前缀——框架侧若有同名旧模块，
        # 常规包遮蔽会让 import 命中框架副本，"通过"不能证明本次安装有效
        if LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX_DEFAULT}/lib" \
                PYTHONPATH="${SITE_PACKAGES}:${FRAMEWORK_SITE}" \
                python3 -c "import ${m} as _m, os; assert os.path.realpath(_m.__file__).startswith(os.path.realpath('${INSTALL_PREFIX}'))" 2>/dev/null; then
            echo "-- ${m} 导入通过（命中安装前缀）"
        elif LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX_DEFAULT}/lib" \
                PYTHONPATH="${DDS_DIR}:${FRAMEWORK_SITE}" \
                python3 -c "import ${m#yomk.dds.}" 2>/dev/null; then
            echo "-- ${m##*.} 模块文件校验通过（直挂 yomk/dds）"
            echo "-- 警告: 与框架同时挂载时 ${m} 命中的是框架前缀副本（PEP 420 常规包遮蔽），" >&2
            echo "       本前缀需用 build_ubuntu.sh 完整部署扩展域并将其 site-packages 置于框架之前" >&2
        else
            echo "错误: ${m} 导入失败" >&2
            VERIFY_RC=1
        fi
    fi
done
# -------------el 9. 安装验证 ----------------

echo ""
echo "==========================================="
echo " IDL 类型工程安装完成!"
echo "-------------------------------------------"
echo " 生成/编译目录: ${GENDIR}"
echo " 安装路径:      ${INSTALL_PREFIX}"
echo " Python 模块:   ${DDS_DIR}/{${PLACED% }}"
echo " 导入形态:      ${MOVED_IMPORTS% }"
echo " C++ 类型库:    ${INSTALL_PREFIX}/lib/"
if [ "${INSTALL_PREFIX}" != "${INSTALL_PREFIX_DEFAULT}" ]; then
    echo " 提示: 自定义前缀不在系统 PYTHONPATH 内，使用时请将其 site-packages 挂入"
    echo "       PYTHONPATH 框架之前（否则框架侧 yomk.dds 常规包会遮蔽本前缀扩展域）"
fi
echo "==========================================="
exit ${VERIFY_RC}
