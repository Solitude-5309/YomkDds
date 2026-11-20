# /YomkDdsHandler/* YomkDds 核心通信 URL 常量（对齐 C++ main 分支 YomkDdsHandler 服务名；
# 与框架 src/yomk/url/<域>/ 同构）。yomk.url 为纯命名空间层（与 yomk.msgs 同构，无 __init__）：
# 扩展自带 yomk/url/dds/ 叶子子包即可并入框架常量池，与 yomk/url/ctx 等平级共存。
#
# 常量命名对齐 C++ 套件总则：归组于 yomk.url.dds 子命名空间，不带 dds 前缀。
#
# 使用方式（推荐优先）：dds 域 builder（dds().version() 等）为模块自有便捷入口，
# 统一请求形态（server 域链式，屏蔽 URL 之下的路由细节）：
#   server().url(GET_VERSION).msg(None).request()
#
# 端点登记进度（对齐 C++ main 的 9 端点，每步一个接口）：
#   [x] GET_VERSION —— 版本查询：纯查询不碰 DDS，忽略载荷；data 位为 "YomkDds v<版本>" 字符串
#   [ ] /load_xml /create_node /delete_node /register_pub_topic /register_sub_topic
#       /publish /loan /discard_loan —— 常量随各接口计划段逐步登记
__all__ = [
    "GET_VERSION",
]

GET_VERSION = "/YomkDdsHandler/get_version"
