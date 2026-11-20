# dds 域 builder（与框架各域 builder.py 同构）。
# 扩展 API 形态纪律：
#   1. builder 是模块自己的 API——只封装本模块自有接口，不重复框架通用请求链路；
#   2. builder 为可选件，可以不写；不提供时须在本域 __init__ 或 url 常量池注释中
#      写明统一请求调用形态，并说明各 URL 常量的功能作用，保证使用方无 builder
#      也有文档可循；
#   3. 推荐优先使用框架封装好的统一请求接口（屏蔽 URL 之下的路由细节）：
#        server().url(GET_VERSION).msg(None).request()
#      builder 终结仅作模块自有便捷入口。
# 终结方法随各接口计划段逐步追加（内部仍走 _facade.api_request）：
#   version()                    —— 版本查询（/get_version，纯查询不碰 DDS）
from yomk.server import _facade
from yomk.url.dds import GET_VERSION


class DdsBuilder:
    def version(self):
        # dds 版本查询终结（对齐 C++ yomk::dds().version()）：
        # 纯查询不碰 DDS，返回 Response，data 位为 "YomkDds v<版本>" 字符串
        return _facade.api_request(GET_VERSION, None)


def dds():
    # dds 域链式入口（对齐 C++ yomk::dds() 公开入口；builder 禁止缓存复用）
    return DdsBuilder()
