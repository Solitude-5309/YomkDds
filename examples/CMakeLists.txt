cmake_minimum_required(VERSION 3.14)
project(ExampleYomkDds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 版本系列检测：示例独立配置时同样只接受 YomkServer 3.x.x
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/YomkServerVersionCheck.cmake)
find_package(YomkServer REQUIRED)
yomk_server_version_check()
find_package(YomkDds REQUIRED)

message(STATUS "YomkServer version: ${YomkServer_VERSION}")
message(STATUS "YomkServer include dirs: ${YomkServer_INCLUDE_DIRS}")
message(STATUS "YomkServer lib dir: ${YomkServer_LIB_DIR}")
message(STATUS "YomkServer libraries: ${YomkServer_LIBRARIES}")
message(STATUS "YomkDds version: ${YomkDds_VERSION}")
message(STATUS "YomkDds include dirs: ${YomkDds_INCLUDE_DIRS}")
message(STATUS "YomkDds lib dir: ${YomkDds_LIB_DIR}")
message(STATUS "YomkDds libraries: ${YomkDds_LIBRARIES}")

# 导出 target 不含库搜索路径是设计如此，此处由扩展 Config 提供的 lib 目录补齐：
# YomkDdsStdMsgs 无导出包、以裸库名链接，仅能靠 -L 解析（fastdds 已由 Config 的 find_dependency 解析为全路径）
link_directories(${YomkDds_LIB_DIR})

# 单进程示例：完整发布订阅流程（ExampleYomkDdsTopic）与 loan 借出机制（ExampleYomkDdsTopicLoan）
add_executable(ExampleYomkDdsTopic ExampleYomkDdsTopic.cpp)
target_link_libraries(ExampleYomkDdsTopic PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

add_executable(ExampleYomkDdsTopicLoan ExampleYomkDdsTopicLoan.cpp)
target_link_libraries(ExampleYomkDdsTopicLoan PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

# 跨进程发布/订阅双示例：ExampleYomkDdsPub（发布端）与 ExampleYomkDdsSub（订阅端）各编译为独立可执行程序
add_executable(ExampleYomkDdsPub ExampleYomkDdsPub.cpp)
target_link_libraries(ExampleYomkDdsPub PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

add_executable(ExampleYomkDdsSub ExampleYomkDdsSub.cpp)
target_link_libraries(ExampleYomkDdsSub PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

# ROS2 互通示例（闭环 6）：发布端与订阅端独立二进制（无命令行参数），set_name 类型名直改，
# 与 ROS2 Humble 同域互通（手动回归脚本 test/run_ros2_interop.sh 驱动，ctest 树不含）
add_executable(ExampleRos2InteropPub ExampleRos2InteropPub.cpp)
target_link_libraries(ExampleRos2InteropPub PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

add_executable(ExampleRos2InteropSub ExampleRos2InteropSub.cpp)
target_link_libraries(ExampleRos2InteropSub PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

# 发现服务器示例（跨域互通）：发布端域 100 / 订阅端域 200 异域经同一发现服务器互通
# （serverLocator 直配 CLIENT，域号在 DS 模式下不参与匹配），可选首参传 "ip[:port]" 覆盖；
# 配套 LaunchDiscoverServer.sh 启动发现服务器（运行方式见 README 5.8）
add_executable(ExampleDiscoverServerPub ExampleDiscoverServerPub.cpp)
target_link_libraries(ExampleDiscoverServerPub PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

add_executable(ExampleDiscoverServerSub ExampleDiscoverServerSub.cpp)
target_link_libraries(ExampleDiscoverServerSub PRIVATE YomkDds::YomkDds YomkServer::YomkServer YomkDdsStdMsgs)

# 命令行工具：yomkdds topic print/list/info 与 node list/info（观察任意 DDS 主题 / 列出域内主题 /
# 查询主题详情 / 列出域内节点 / 查询节点详情；类型无关，无需 YomkDdsStdMsgs）
add_executable(yomkdds yomkdds.cpp)
target_link_libraries(yomkdds PRIVATE YomkDds::YomkDds YomkServer::YomkServer)
# --qos-file 解析依赖 nlohmann/json（vendored header-only；主库 PRIVATE include 不导出，
# 此处单独配路径）
target_include_directories(yomkdds PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/nlohmann_json/include)

# 示例程序随扩展一并安装到 <安装路径>/bin（运行方式见 README）
# 示例程序可能有多个，此处显式列出全部示例目标名（不能用 ${PROJECT_NAME} 占位）
install(TARGETS
    ExampleYomkDdsTopic
    ExampleYomkDdsTopicLoan
    ExampleYomkDdsPub
    ExampleYomkDdsSub
    ExampleRos2InteropPub
    ExampleRos2InteropSub
    ExampleDiscoverServerPub
    ExampleDiscoverServerSub
    yomkdds
    RUNTIME DESTINATION bin
)

# 发现服务器启动脚本随示例一并安装到 <安装路径>/bin
install(PROGRAMS LaunchDiscoverServer.sh DESTINATION bin)
