#!/bin/bash
# scripts/yomkdds_build_idl.sh —— IDL 消息类型一键 生成/编译/安装（支持 loan 的 msg 类型）
# 用法: bash scripts/yomkdds_build_idl.sh <idl 文件路径> [-p|--prefix 安装前缀]
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
#      C++ 类型库落 <前缀>/lib，SWIG Python 模块并入类型域 yomk/dds_idl/（与服务域 yomk/dds 并列）
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
[ -n "${IDL_FILE}" ] || fail "用法: bash scripts/yomkdds_build_idl.sh <idl 文件路径> [-p|--prefix 安装前缀]"
IDL_FILE="$(_normalize_path "${IDL_FILE}")"
[ -f "${IDL_FILE}" ] || fail "IDL 文件不存在: ${IDL_FILE}"
IDL_STEM="$(basename "${IDL_FILE}")"
IDL_STEM="${IDL_STEM%.idl}"

# 生成工具：优先 -p 指定的安装前缀（build_ubuntu 调用场景，Fast-DDS-Gen 已随
# 主安装落入前缀），回退 $YOMK_PREFIX_PATH；均不用源码 thirdparty 中的
INSTALL_PREFIX_DEFAULT="${YOMK_PREFIX_PATH:-/opt/yomk}"
FASTDDSGEN=""
for _gen_base in "${ARG_PREFIX}" "${INSTALL_PREFIX_DEFAULT}"; do
    if [ -n "${_gen_base}" ] && [ -f "${_gen_base}/scripts/fastddsgen" ]; then
        FASTDDSGEN="${_gen_base}/scripts/fastddsgen"
        break
    fi
done
[ -n "${FASTDDSGEN}" ] || fail "未找到生成工具 fastddsgen（查找路径: ${ARG_PREFIX:-无}, ${INSTALL_PREFIX_DEFAULT}），请先安装扩展（./build_ubuntu.sh，随库安装 Fast-DDS-Gen）"
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

# 特权边界检测：前缀不可写时目录创建/安装走 sudo（须在第 3 节目录创建前确定）
SUDO=""
if [ ! -d "${INSTALL_PREFIX}" ] || [ ! -w "${INSTALL_PREFIX}" ]; then
    SUDO="sudo"
fi

# site-packages 不存在时自动生成（安装前缀可自定义，不必预先存在）
SITE_PACKAGES="$(ls -d "${INSTALL_PREFIX}"/lib/python*/site-packages 2>/dev/null | head -1)"
if [ -z "${SITE_PACKAGES}" ]; then
    PY_VER="$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")"
    SITE_PACKAGES="${INSTALL_PREFIX}/lib/python${PY_VER}/site-packages"
    echo "-- 自动生成 site-packages: ${SITE_PACKAGES}"
    ${SUDO} mkdir -p "${SITE_PACKAGES}" || fail "site-packages 创建失败: ${SITE_PACKAGES}"
fi
DDS_DIR="${SITE_PACKAGES}/yomk/dds_idl"
${SUDO} mkdir -p "${DDS_DIR}" || fail "扩展域目录创建失败: ${DDS_DIR}"
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
# Python 模块源目的地（生成的 CMakeLists 经 sysconfig purelib(vars={'base': 前缀}) 计算）
MODULE_SRC_BASE="$(python3 -c "from sysconfig import get_path; print(get_path('purelib', vars={'base': '${INSTALL_PREFIX}'}))")"
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
echo "-- 安装到 ${INSTALL_PREFIX} ..."
${SUDO} cmake --install "${GENDIR}/build" || fail "msg 类型工程安装失败"

# C++ 类型头文件随库安装（<前缀>/include/yomk/dds_idl/，与 IDL namespace yomk::dds_idl 对应；
# fastddsgen 生成于 GENDIR：<文件名>.hpp + <文件名>PubSubTypes.hpp）
HDR_DIR="${INSTALL_PREFIX}/include/yomk/dds_idl"
echo "-- 安装 C++ 头文件: ${IDL_STEM}.hpp / ${IDL_STEM}PubSubTypes.hpp -> ${HDR_DIR}"
${SUDO} mkdir -p "${HDR_DIR}"
${SUDO} cp -f "${GENDIR}/${IDL_STEM}.hpp" "${GENDIR}/${IDL_STEM}PubSubTypes.hpp" "${HDR_DIR}/" \
    || fail "C++ 头文件安装失败"
# -------------el 6. 安装 ----------------

# -------------sl 7. 目的地校正（确定性路径，幂等） ----------------
# 生成的 CMakeLists 安装到 <sysconfig purelib>/<IDL module 链>/<文件名>：
# Debian posix_local 下 purelib = <前缀>/local/.../dist-packages（sys.path 外不可
# 导入），且该目录可能带历次运行残留——不用快照差集，按 IDL 推导的确定性路径
# 直接定位搬移（幂等，重装自愈）。统一搬移模块目录 →
# <site-packages>/yomk/dds_idl/<文件名>/（类型域，与服务域 yomk/dds 并列；单层，导入 yomk.dds_idl.<文件名>；
# IDL 的 module 声明层级由目的地本身表达，不重复落盘）
IMPORT_NS="yomk.dds_idl"
MODULE_CHAIN="$(sed -nE 's/^[[:space:]]*module[[:space:]]+([A-Za-z_][A-Za-z0-9_]*).*/\1/p' "${IDL_FILE}" | paste -sd/ -)"
EXPECTED_REL="${MODULE_CHAIN:+${MODULE_CHAIN}/}${IDL_STEM}"
MODULE_DIR="${MODULE_SRC_BASE}/${EXPECTED_REL}"
TARGET_MODULE_DIR="${DDS_DIR}/${IDL_STEM}"
echo "-- 目的地校正: ${MODULE_DIR} -> ${TARGET_MODULE_DIR}"
if [ ! -f "${MODULE_DIR}/__init__.py" ]; then
    fail "未在 ${MODULE_SRC_BASE} 找到生成的模块目录 ${EXPECTED_REL}/__init__.py，请人工核对生成产物"
fi
# 非 Debian 布局下 MODULE_DIR 可能与目标同址（purelib == site-packages 且
# module 链 == yomk/dds），realpath 相同则跳过搬移
if [ "$(realpath "${MODULE_DIR}" 2>/dev/null)" != "$(realpath "${TARGET_MODULE_DIR}" 2>/dev/null)" ]; then
    ${SUDO} rm -rf "${TARGET_MODULE_DIR}"
    ${SUDO} mv "${MODULE_DIR}" "${TARGET_MODULE_DIR}" || fail "目的地校正失败: ${EXPECTED_REL}"
fi
PLACED="${IDL_STEM} "
MOVED_IMPORTS="${IMPORT_NS}.${IDL_STEM}"
# 空目录清理：模块链与 local/ 空壳自底向上逐级 rmdir（非空即停，安全）；
# 仅当源目的地与 site-packages 不同级时需要
if [ "${MODULE_SRC_BASE}" != "${SITE_PACKAGES}" ]; then
    if [ -n "${MODULE_CHAIN}" ]; then
        d="${MODULE_SRC_BASE}/${MODULE_CHAIN}"
        while [ "${d}" != "${MODULE_SRC_BASE}" ] && [ -n "${d}" ] && [ "${d}" != "/" ]; do
            ${SUDO} rmdir "${d}" 2>/dev/null || break
            d="$(dirname "${d}")"
        done
    fi
    d="${MODULE_SRC_BASE}"
    while [ -n "${d}" ] && [ "${d}" != "${INSTALL_PREFIX}" ] && [ "${d}" != "/" ]; do
        ${SUDO} rmdir "${d}" 2>/dev/null || break
        d="$(dirname "${d}")"
    done
fi
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
                python3 -c "import ${m#${IMPORT_NS}.}" 2>/dev/null; then
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
echo " C++ 头文件:    ${INSTALL_PREFIX}/include/yomk/dds_idl/"
if [ "${INSTALL_PREFIX}" != "${INSTALL_PREFIX_DEFAULT}" ]; then
    echo " 提示: 自定义前缀不在系统 PYTHONPATH 内，使用时请将其 site-packages 挂入"
    echo "       PYTHONPATH 框架之前（否则框架侧 yomk.dds 常规包会遮蔽本前缀扩展域）"
fi
echo "==========================================="
exit ${VERIFY_RC}
