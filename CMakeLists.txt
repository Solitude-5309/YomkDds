cmake_minimum_required(VERSION 3.14)

# 编译器预选模块：g++ 优先，回退 clang++（必须在 project() 之前 include）
include(cmake/AbiInfo.cmake)
# YomkServer 版本检测模块：find_package(YomkServer) 之后调用 yomk_server_version_check()
include(cmake/YomkServerVersionCheck.cmake)

project(YomkDds VERSION 2.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ABI 环境指纹：采集/打印编译环境信息并生成 abi_report.txt（project() 之后才可调用）
yomk_abi_report()
# compile_commands.json 导出到源码根目录，供 clangd 使用：
# 构建目录可任意（仓库外亦可），避免用户因 build 目录位置不同而 IDE 解析失败
add_custom_target(export_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

find_package(YomkServer REQUIRED)
# 版本检测：YomkDds 要求 YomkServer 3.6.18 及以上，版本过低直接报错终止配置
yomk_server_version_check()
message(STATUS "YomkServer version: ${YomkServer_VERSION}")  
message(STATUS "YomkServer include dirs: ${YomkServer_INCLUDE_DIRS}")  
message(STATUS "YomkServer lib dir: ${YomkServer_LIB_DIR}")  
message(STATUS "YomkServer libraries: ${YomkServer_LIBRARIES}") 
include_directories(${YomkServer_INCLUDE_DIRS})
link_directories(${YomkServer_LIB_DIR})

# Set third-party directory
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/thirdparty")
set(CMAKE_PREFIX_PATH
  "${THIRD_PARTY_DIR}/Fast-DDS-3.6.1/install"
)
find_package(fastdds 3 REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/dds)

# 编译为动态库
add_library(${PROJECT_NAME} SHARED
    src/YomkDdsService.cpp
    src/YomkDdsDebugService.cpp
    src/FastDDSNode.cpp
    src/FastDDSDebugNode.cpp
    src/YomkDdsBagService.cpp
    src/FastDDSBagNode.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE YomkServer::YomkServer)
target_link_libraries(${PROJECT_NAME} PUBLIC fastdds)
# MC9：版本号对象宏改名 YOMKDDS_VERSION→YOMKDDS_VERSION_STRING，消除与 YomkDdsAPI.h 公共 API 宏
# YOMKDDS_VERSION() 的同名冲突（clang-tidy 报 clang-diagnostic-macro-redefined）。
target_compile_definitions(${PROJECT_NAME} PRIVATE YOMKDDS_VERSION_STRING="${PROJECT_VERSION}")
# bag record：vendored mcap 头库（仅 FastDDSBagNode.cpp 定义 MCAP_IMPLEMENTATION，其余 TU 只
# include 头链接库内实现）；压缩算法禁用 LZ4/ZSTD（ZSTD 依赖缺失，LZ4 无需求，
# 未定义 MCAP_COMPRESSION_NO_ZSTD 时 mcap 头文件内直接编译报错）
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mcap/include)
target_compile_definitions(${PROJECT_NAME} PRIVATE MCAP_COMPRESSION_NO_LZ4 MCAP_COMPRESSION_NO_ZSTD)
# metadata.json：vendored nlohmann json 头库（纯 header-only 无需实现宏），FastDDSBagNode.cpp
# 组装 bag 元信息使用
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann_json/include)

# 安装规则
# 对齐 YomkServer template/extension：源码树 include/dds/ 分域携带扩展名层级，DESTINATION 固定
# include/yomk（头文件物理安装位置 include/yomk/dds/），对外引用统一 <yomk/dds/...>，
# 导出 target 与 YomkServer::YomkServer 共用同一 include 根
set(INCLUDE_INSTALL_DIR "include")
set(LIB_INSTALL_DIR "lib")

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    INCLUDES DESTINATION ${INCLUDE_INSTALL_DIR}
)
install(DIRECTORY include/ DESTINATION include/yomk)

# ABI 环境指纹：随库安装，供下游比对编译环境
install(FILES ${CMAKE_BINARY_DIR}/abi_report.txt
        DESTINATION share/${PROJECT_NAME})

# CMake export 导出配置
include(CMakePackageConfigHelpers)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION lib/cmake/${PROJECT_NAME}
    PATH_VARS INCLUDE_INSTALL_DIR LIB_INSTALL_DIR
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION lib/cmake/${PROJECT_NAME}
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/msg/YomkDdsStdMsgs.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/msg/YomkDdsStdMsgsPubSubTypes.hpp"
    DESTINATION include/yomk/dds
)

install(DIRECTORY thirdparty/Fast-DDS-3.6.1/install/
    DESTINATION .
)

install(DIRECTORY thirdparty/Fast-DDS-Gen-4.3.0/
    DESTINATION .
)
