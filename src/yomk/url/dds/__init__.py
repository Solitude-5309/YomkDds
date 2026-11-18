# /DdsService/* YomkDds 扩展 URL 常量（与框架 src/yomk/url/<域>/ 同构）。
# yomk.url 为纯命名空间层（与 yomk.msgs 同构，无 __init__）：扩展自带 yomk/url/dds/
# 叶子子包即可并入框架常量池，与 yomk/url/ctx 等平级共存。
#
# 常量命名对齐 C++ 套件总则：归组于 yomk.url.dds 子命名空间，不带 dds 前缀。
#
# 使用方式（推荐优先）：统一请求接口（server 域链式形态，屏蔽 URL 之下的路由细节）
#   server().url(ECHO).msg(DdsRequest("hello dds")).request()
# dds 域 builder（dds().version() 等）为可选件，只封装模块自有接口；
# 不提供 builder 的扩展须在此处写明统一请求形态与各常量功能作用。
__all__ = [
    "ECHO",
    "GET_VERSION",
]

ECHO = "/DdsService/echo"        # 回显端点：请求包 DdsRequest，回包 DdsResponse（data 位）
GET_VERSION = "/DdsService/version"  # 版本查询端点：data 位为 "dds v<版本>" 字符串
