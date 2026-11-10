# =============================================================================
# YomkDds 测试树（独立 CMake 工程，与 examples/ 同构；测试程序仅供开发验证，不 install）
#
# 设计原则（用户决策）：
#   1. 只测 YomkDds 自有源码（../src、../include）；第三方（FastDDS/YomkServer/YomkDdsStdMsgs）
#      默认正确，只链接、不插桩、不修改。
#   2. 为仅对自有源码插桩 sanitizer/coverage，测试树从源码重新编译 ../src/*.cpp 为被测目标
#      yomkdds_under_test，而非链接已安装的 libYomkDds.so（后者无法单独插桩）。
#   3. 每个被测主题一个独立可执行程序（单一 TU + 纯 main + CHECK），进程隔离，
#      规避 YomkServer 单进程单次初始化约束。
#
# 构建（在 test/ 下独立配置，勿污染主库构建）：
#   cmake -S test -B test/build -DCMAKE_PREFIX_PATH="/opt/yomk"
#   cmake --build test/build -j
# sanitizer / coverage 开关见下方 CACHE 选项；静态检查用 test/run_static_checks.sh（独立运行器，读仓库根 .clang-tidy）。
# =============================================================================
cmake_minimum_required(VERSION 3.14)
project(YomkDdsTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 导出 compile_commands.json 供 clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# YomkDds 源码根（test/ 的上一级）
get_filename_component(YOMKDDS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# ----------------------------- 依赖定位 -----------------------------
# 版本系列检测：测试树独立配置时同样只接受 YomkServer 3.x.x
include(${YOMKDDS_ROOT}/cmake/YomkServerVersionCheck.cmake)
find_package(YomkServer REQUIRED)
yomk_server_version_check()
message(STATUS "[test] YomkServer version: ${YomkServer_VERSION}")

# FastDDS 随主库 vendored 于 thirdparty/，测试树直接复用，不依赖系统安装
set(THIRD_PARTY_DIR "${YOMKDDS_ROOT}/thirdparty")
list(APPEND CMAKE_PREFIX_PATH "${THIRD_PARTY_DIR}/Fast-DDS-3.6.1/install")
find_package(fastdds 3 REQUIRED)
message(STATUS "[test] fastdds found")

# MC3+ 端到端收发需 YomkDdsStdMsgs 真实类型（MString/MInt32 等 PubSubType）；
# YomkDdsStdMsgs 无导出包，沿用 examples 已验证方式：find_package(YomkDds) 取 lib 目录后裸库名链接。
# 置于 vendored fastdds 之后：仅取 ${YomkDds_LIB_DIR} 供 link_directories，不链接 YomkDds::YomkDds
# 导入目标（被测目标从源码重编），避免其 interface 依赖干扰已解析的 vendored fastdds。
find_package(YomkDds REQUIRED)
link_directories(${YomkDds_LIB_DIR})
message(STATUS "[test] YomkDds lib dir: ${YomkDds_LIB_DIR}")

# ----------------------------- 被测目标 -----------------------------
# 仅编译自有源码六个 TU；第三方以 IMPORTED/已安装库形式链接，不参与插桩
add_library(yomkdds_under_test STATIC
    ${YOMKDDS_ROOT}/src/YomkDdsService.cpp
    ${YOMKDDS_ROOT}/src/YomkDdsDebugService.cpp
    ${YOMKDDS_ROOT}/src/FastDDSNode.cpp
    ${YOMKDDS_ROOT}/src/FastDDSDebugNode.cpp
    ${YOMKDDS_ROOT}/src/YomkDdsBagService.cpp
    ${YOMKDDS_ROOT}/src/FastDDSBagNode.cpp
)
target_include_directories(yomkdds_under_test PUBLIC
    ${YOMKDDS_ROOT}/include/dds
    ${YOMKDDS_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}   # TestCheck.h
)
# 与主库一致的版本宏（getVersion 拼接 "YomkDds v" YOMKDDS_VERSION_STRING）
# MC9：随主库改名 YOMKDDS_VERSION→YOMKDDS_VERSION_STRING，避免与 YomkDdsAPI.h 的 YOMKDDS_VERSION() API 宏冲突
# #5(b) drift-free：从根 ../CMakeLists.txt 的 project(YomkDds VERSION x.y.z) 提取版本，消除硬编码 0.1.0 与根版本脱节
file(STRINGS "${YOMKDDS_ROOT}/CMakeLists.txt" _yomkdds_ver_line
     REGEX "^project\\(YomkDds[ \t]+VERSION[ \t]+[0-9]")
string(REGEX REPLACE "^project\\(YomkDds[ \t]+VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+).*$" "\\1"
       YOMKDDS_SRC_VERSION "${_yomkdds_ver_line}")
target_compile_definitions(yomkdds_under_test PUBLIC YOMKDDS_VERSION_STRING="${YOMKDDS_SRC_VERSION}")
target_link_libraries(yomkdds_under_test PUBLIC YomkServer::YomkServer fastdds)
# bag record：FastDDSBagNode.cpp 编译需 vendored mcap 头与压缩禁用宏（与根 CMakeLists 同源，
# PUBLIC——TestFastDDSBagNodeRecord 读回断言亦 include <mcap/reader.hpp>，需 include 路径
# 与宏定义完全一致以杜绝 ODR/条件编译视图差异）
target_include_directories(yomkdds_under_test PUBLIC ${YOMKDDS_ROOT}/thirdparty/mcap/include)
target_compile_definitions(yomkdds_under_test PUBLIC MCAP_COMPRESSION_NO_LZ4 MCAP_COMPRESSION_NO_ZSTD)
# metadata.json：FastDDSBagNode.cpp 组装 bag 元信息需 vendored nlohmann json（纯 header-only
# 无需实现宏，与根 CMakeLists 同源）
target_include_directories(yomkdds_under_test PUBLIC ${YOMKDDS_ROOT}/thirdparty/nlohmann_json/include)

# ----------------------------- Sanitizer 开关 -----------------------------
# off/asan/tsan；asan 合并 address+undefined（覆盖需求 #3 ASan 与 #4 UBSan），tsan 覆盖 #5。
# 标志以 PUBLIC 附加到被测目标：自有源码编译插桩，且链接期传递给测试 exe；
# 第三方预编译库不受影响（无法也无需插桩），符合"只测自有源码"原则。
set(YOMKDDS_TEST_SANITIZER off CACHE STRING "Sanitizer for tests: off/asan/tsan")
set_property(CACHE YOMKDDS_TEST_SANITIZER PROPERTY STRINGS off asan tsan)
if(NOT YOMKDDS_TEST_SANITIZER STREQUAL "off")
    if(YOMKDDS_TEST_SANITIZER STREQUAL "asan")
        set(_YOMKDDS_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    elseif(YOMKDDS_TEST_SANITIZER STREQUAL "tsan")
        set(_YOMKDDS_SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer -g -O1)
    else()
        message(FATAL_ERROR "Unknown YOMKDDS_TEST_SANITIZER=${YOMKDDS_TEST_SANITIZER}, expected off/asan/tsan")
    endif()
    target_compile_options(yomkdds_under_test PUBLIC ${_YOMKDDS_SAN_FLAGS})
    target_link_options(yomkdds_under_test PUBLIC ${_YOMKDDS_SAN_FLAGS})
    # 插桩构建的预处理标记（PUBLIC 沿 under_test 传播到链接它的测试 exe）：插桩轮的
    # 性能/吞吐类断言可据此降级（插桩与分配器开销使耗时类数据波动为固有属性，无信号）
    target_compile_definitions(yomkdds_under_test PUBLIC YOMKDDS_SANITIZED_TEST_BUILD=1)
    message(STATUS "[test] YOMKDDS_TEST_SANITIZER=${YOMKDDS_TEST_SANITIZER}, flags=${_YOMKDDS_SAN_FLAGS}")
endif()

# ----------------------------- TSan 运行启动器（ASLR 兼容）-----------------------------
# GCC 11.4 libtsan 与高 ASLR 熵内核(Ubuntu 22.04 vm.mmap_rnd_bits 偏高)不兼容：TSan 运行时在
# main 之前 FATAL("unexpected memory mapping")、进程 EXIT=66(非 race、非 YomkDds 缺陷)。
# 非 root 标准解法：setarch <arch> -R 仅对该测试进程禁用 ASLR(每进程级、无系统副作用)。
# 以启动器前缀注入各 add_test；off/asan 模式变量为空 → COMMAND 退化为原 exe 路径(行为零变化)。
set(YOMKDDS_TEST_LAUNCHER "")
if(YOMKDDS_TEST_SANITIZER STREQUAL "tsan")
    find_program(SETARCH_EXE NAMES setarch)
    if(SETARCH_EXE)
        set(YOMKDDS_TEST_LAUNCHER ${SETARCH_EXE} ${CMAKE_SYSTEM_PROCESSOR} -R)
        message(STATUS "[test] tsan launcher(ASLR off): ${YOMKDDS_TEST_LAUNCHER}")
    else()
        message(WARNING "[test] setarch not found; tsan tests may FATAL on high-ASLR kernels")
    endif()
endif()

# ----------------------------- Coverage 开关 -----------------------------
# 仅对自有源码统计覆盖率；生成报告时用 gcov 解析并限定 ../src/*.cpp，
# 不可达的第三方失败分支出具书面豁免（见测试报告），覆盖率上限据此标注。
set(YOMKDDS_TEST_COVERAGE OFF CACHE BOOL "Enable gcov coverage instrumentation (own sources only)")
if(YOMKDDS_TEST_COVERAGE)
    target_compile_options(yomkdds_under_test PUBLIC --coverage -g -O0)
    target_link_options(yomkdds_under_test PUBLIC --coverage)
    message(STATUS "[test] YOMKDDS_TEST_COVERAGE=ON")
endif()

# ----------------------------- 压力/soak 旋钮（MC7）-----------------------------
# STRESS_ITERS：廉价 op（publish/loan/discard）的量级，可上调至 10 万级做 soak；
# STRESS_CYCLES：node 生命周期 churn 轮数，每轮含 discovery 周期（秒级），故有界、非 10 万级。
# 经 compile definitions 注入 stress exe；改值需重配置+重编 stress 目标（仅 2 个 TU，快）。
set(STRESS_ITERS 5000 CACHE STRING "Stress iterations for cheap ops (publish/loan); raise to 100000 for soak")
set(STRESS_CYCLES 30 CACHE STRING "Stress node create/delete churn cycles (discovery-bound, not 100k-scale)")
message(STATUS "[test] STRESS_ITERS=${STRESS_ITERS}, STRESS_CYCLES=${STRESS_CYCLES}")

# ----------------------------- 测试主题子目录 -----------------------------
enable_testing()
# MC0：基座自检（零 DDS，验证 CHECK/testReport 与被测目标链接）
add_subdirectory(harness)
# MC1：服务层契约（DDS-free，验证 invoke 白盒入口与 8 端点输入校验/错误码）
add_subdirectory(yomk_dds_service)
# fast_dds_debug_node：类型无关调试订阅节点（守卫用例 + 发现→动态类型→订阅→JSON 输出端到端）
add_subdirectory(fast_dds_debug_node)
# yomk_dds_debug_service：调试服务两层测试（契约 DDS-free + 生命周期真实 DDS）
add_subdirectory(yomk_dds_debug_service)
# fast_dds_bag_node：bag 录制节点两层测试（启动校验 + 录制收尾落盘真实 DDS）
add_subdirectory(fast_dds_bag_node)
# yomk_dds_bag_service：bag 服务两层测试（契约 DDS-free + 生命周期真实 DDS）
add_subdirectory(yomk_dds_bag_service)
# ros2_interop：真机互通回归判定程序（run_ros2_interop.sh 驱动，不 add_test——需 ROS2 环境）
add_subdirectory(ros2_interop)
