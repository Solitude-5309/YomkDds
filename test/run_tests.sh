#!/bin/bash
# YomkDds 扩展全量测试运行器（与 YomkServerPy test/run_tests.sh 同构）
# 用法:
#   ./test/run_tests.sh              运行全部测试
#   ./test/run_tests.sh --prefix DIR 测试产物目录（缺省 $YOMK_PREFIX_PATH）
#   ./test/run_tests.sh --timeout N  单测试超时秒数（默认 300）
#   ./test/run_tests.sh -h|--help    打印本说明
# 测试产物目录：消费扩展安装树（PEP 420 命名空间并入框架后的 site-packages），
#   默认取 YOMK_PREFIX_PATH（build_ubuntu.sh 安装后写入的环境变量）；--prefix 可
#   显式指定其他安装前缀。产物缺失时报错并给出安装命令，不静默回退系统路径
#   （避免测试跑在未安装或过期产物上）
# 行为:
#   1. 自动探测 lib/pythonX.Y/site-packages 并追加式设置 PYTHONPATH（保留用户环境）
#   2. 逐个运行 TESTS 清单（快->慢，stress 类压尾），每个测试在独立临时工作目录运行
#   3. 任一测试失败（退出码非 0 / 超时）-> 立即停止，输出 [FAIL] 行摘要与日志路径
#   4. 日志落盘: test/test_logs/<时间戳>/<测试名>.log + summary.log
#   5. 测试临时产物统一落 test/test_logs/tmp；启动时整目录重建（兜底上次崩溃遗留），
#      退出（含失败/中断/超时）时整目录删除，不留任何临时产物
# 测试程序风格约定: 退出码 0=全部通过，非 0=存在失败用例（[FAIL] 行输出）

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 测试文件定位：TESTS 项带模块子目录前缀（dds/TestXxx 形态），完整路径 = test/<TESTS 项>.py
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

PREFIX=""
TIMEOUT_SECS=300

usage() {
    sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

fail() {
    echo "错误: $1" >&2
    exit 1
}

# -------------sl 1. 解析参数 ----------------
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)  [ $# -ge 2 ] || fail "--prefix 需要目录参数"
                   PREFIX="$2"; shift ;;
        --timeout) [ $# -ge 2 ] || fail "--timeout 需要秒数参数"
                   TIMEOUT_SECS="$2"; shift ;;
        -h|--help) usage ;;
        *) fail "未知参数: $1（用法: ./test/run_tests.sh [--prefix DIR] [--timeout N]）" ;;
    esac
    shift
done
# -------------el 1. 解析参数 ----------------

# 测试临时根（固定共享目录，test_logs 已 gitignore）：启动整目录重建 + 导出注入，
# 退出整目录删除（幂等；失败/超时/中断路径经 EXIT trap 全覆盖）
TEST_TMP_ROOT="${SCRIPT_DIR}/test_logs/tmp"
ensure_test_tmp_root() {
    rm -rf "${TEST_TMP_ROOT}"
    mkdir -p "${TEST_TMP_ROOT}"
    export YOMK_TEST_TMP_ROOT="${TEST_TMP_ROOT}"
}

# EXIT/INT/TERM 兜底清理临时工作目录与整个测试临时根（幂等）
CURRENT_WORKDIR=""
cleanup_workdir() {
    [ -n "${CURRENT_WORKDIR}" ] && rm -rf "${CURRENT_WORKDIR}"
    CURRENT_WORKDIR=""
}
cleanup_all() {
    cleanup_workdir
    rm -rf "${TEST_TMP_ROOT}"
}
trap cleanup_all EXIT
trap 'echo ""; echo "被用户中断"; exit 130' INT TERM

# 启动即整目录重建测试临时根（兜底上次崩溃遗留），并注入测试进程
ensure_test_tmp_root

# -------------sl 2. 探测测试产物目录并设置 PYTHONPATH ----------------
# 只消费扩展安装树（默认 YOMK_PREFIX_PATH，--prefix 可覆盖），
# 不静默回退系统路径——避免测试跑在未安装或过期产物上；
# 产物缺失时报错并给出安装命令（先安装后测试）
install_hint() {
    echo "./build_ubuntu.sh（或手动: cmake -S ${REPO_DIR} -B ${REPO_DIR}/build -DCMAKE_INSTALL_PREFIX=<框架安装前缀> && cmake --install ${REPO_DIR}/build）"
}

# 默认固定安装前缀（--prefix 可覆盖）
if [ -z "${PREFIX}" ]; then
    [ -n "${YOMK_PREFIX_PATH:-}" ] || fail "未检测到 YOMK_PREFIX_PATH（build_ubuntu.sh 安装后写入），请先安装扩展: $(install_hint)，或用 --prefix 指定安装前缀"
    PREFIX="${YOMK_PREFIX_PATH}"
fi
PREFIX="${PREFIX/#\~/$HOME}"

PY_SITE_DIR=$(ls -d "${PREFIX}"/lib/python*[0-9].[0-9]*/site-packages 2>/dev/null | head -1)
[ -n "${PY_SITE_DIR}" ] \
    || fail "未找到 ${PREFIX}/lib/pythonX.Y/site-packages，请先安装扩展: $(install_hint)（或用 --prefix 指定安装前缀）"

# 产物校验：框架就位 + 扩展三域就位（含版本注入产物 _version.py）+ 第三方 fastdds 绑定 + msg 类型库
for f in yomk/server/__init__.py \
         yomk/dds/__init__.py yomk/dds/_version.py yomk/dds/DdsService.py \
         yomk/msgs/dds/__init__.py yomk/url/dds/__init__.py \
         fastdds/__init__.py yomk/dds/YomkDdsStdMsgs/__init__.py; do
    [ -f "${PY_SITE_DIR}/${f}" ] || fail "未找到 ${PY_SITE_DIR}/${f}，请先安装扩展: $(install_hint)"
done

export PYTHONPATH="${PY_SITE_DIR}${PYTHONPATH:+:${PYTHONPATH}}"
# -------------el 2. 探测安装产物并设置 PYTHONPATH ----------------

# 测试清单：按模块分组、快->慢，stress 类压尾（项带模块子目录前缀，test/<模块>/<名>.py；
# 新增测试在此登记；模块轮测试追加对应模块分组与条目）
# 当前登记总数: 2 项（数组闭合后有自检守卫，声明与清单漂移即报错退出，
# 作为登记数漂移的结构性防御）
TESTS=(
    # dds 模块（YomkDds 扩展运行时服务）
    test_dds
    # fastdds 模块（绑定可用性：msg 类型发布/订阅互通）
    fastdds/test_fastdds
)

# -------------sl 2b. 总数自检守卫 ----------------
DECLARED_TEST_COUNT=2
if [ "${#TESTS[@]}" -ne "${DECLARED_TEST_COUNT}" ]; then
    echo "错误: TESTS 登记数 ${#TESTS[@]} 与声明总数 ${DECLARED_TEST_COUNT} 不一致，登记新测试时须同步更新本文件声明"
    exit 1
fi
# -------------el 2b. 总数自检守卫 ----------------

# -------------sl 3. 门禁检查 ----------------
MISSING=()
for t in "${TESTS[@]}"; do
    [ -f "${SCRIPT_DIR}/${t}.py" ] || MISSING+=("${t}.py")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "错误: ${SCRIPT_DIR} 缺少以下测试文件（共 ${#MISSING[@]} 个）:"
    for t in "${MISSING[@]}"; do echo "   - ${t}"; done
    exit 1
fi
# -------------el 3. 门禁检查 ----------------

# -------------sl 4. 运行测试（失败即停） ----------------
# 日志根保持在脚本目录（test/）下
LOG_ROOT="${SCRIPT_DIR}/test_logs/$(date +%Y%m%d_%H%M%S)"
mkdir -p "${LOG_ROOT}"
SUMMARY="${LOG_ROOT}/summary.log"
printf "[MODE] prefix=%s\n" "${PREFIX}" >> "${SUMMARY}"

echo "-- PYTHONPATH: ${PY_SITE_DIR}"
echo "-- 日志目录:   ${LOG_ROOT}"
echo "-- 测试总数:   ${#TESTS[@]}"
echo ""

TOTAL=${#TESTS[@]}
PASS=0
IDX=0
START_TS=${SECONDS}

for t in "${TESTS[@]}"; do
    IDX=$((IDX + 1))
    # 显示名/日志名取纯测试名，路径按模块前缀定位
    T_NAME="${t##*/}"
    log="${LOG_ROOT}/${T_NAME}.log"
    CURRENT_WORKDIR="$(mktemp -d -p "${TEST_TMP_ROOT}")"
    printf "[%2d/%2d] %-32s " "${IDX}" "${TOTAL}" "${T_NAME}"
    T0=${SECONDS}
    # stdin 喂入换行：兼容演示型测试中的 input() 人工暂停交互（无人值守时自动继续）
    ( cd "${CURRENT_WORKDIR}" && printf '\n\n' | timeout "${TIMEOUT_SECS}" python3 "${SCRIPT_DIR}/${t}.py" ) > "${log}" 2>&1
    RC=$?
    ELAPSED=$((SECONDS - T0))
    cleanup_workdir
    if [ ${RC} -eq 0 ]; then
        echo "PASS (${ELAPSED}s)"
        printf "[PASS] %-32s %ds\n" "${T_NAME}" "${ELAPSED}" >> "${SUMMARY}"
        PASS=$((PASS + 1))
    else
        REASON="退出码 ${RC}"
        [ ${RC} -eq 124 ] && REASON="超时(>${TIMEOUT_SECS}s)"
        echo "FAIL (${REASON}, ${ELAPSED}s)"
        printf "[FAIL] %-32s %s\n" "${T_NAME}" "${REASON}" >> "${SUMMARY}"
        echo "-------------------------------------------"
        echo "测试 ${t} 失败: ${REASON}"
        echo "完整日志: ${log}"
        FAIL_COUNT=$(grep -c "\[FAIL\]" "${log}" 2>/dev/null || true)
        if [ -n "${FAIL_COUNT}" ] && [ "${FAIL_COUNT}" -gt 0 ]; then
            echo "失败用例（共 ${FAIL_COUNT} 行 [FAIL]，摘要如下）:"
            grep "\[FAIL\]" "${log}" | head -20
        else
            echo "（无 [FAIL] 行，可能为崩溃/超时，请查看完整日志）"
            tail -30 "${log}"
        fi
        echo "-------------------------------------------"
        exit 1
    fi
done
# -------------el 4. 运行测试（失败即停） ----------------

# -------------sl 5. 结果摘要 ----------------
TOTAL_ELAPSED=$((SECONDS - START_TS))
echo "YomkDds 扩展全量测试通过: ${PASS}/${TOTAL}" >> "${SUMMARY}"

echo ""
echo "==========================================="
echo " YomkDds 扩展全量测试通过: ${PASS}/${TOTAL}"
echo " 总耗时: ${TOTAL_ELAPSED}s"
echo " 日志目录: ${LOG_ROOT}"
echo "==========================================="
exit 0
# -------------el 5. 结果摘要 ----------------
