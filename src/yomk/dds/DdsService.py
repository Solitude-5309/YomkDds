# YomkDds 扩展运行时服务
# 纪律：扩展服务不做隐式发现——使用方须显式 server().srv(DdsService).create()
# 注册（见 test/test_dds.py）；框架只拉起 5 个内置服务。
# 调用方推荐优先使用框架封装好的统一请求接口访问本服务：
#   server().url(ECHO).msg(DdsRequest("hello dds")).request()
from yomk.server import YomkService
from yomk.msgs.server import ResStatus, Response
from yomk.msgs.dds import DdsRequest, DdsResponse
from yomk.log import log


class DdsService(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/DdsService")

    def init(self):
        self.install_func("/echo", self.echo)
        self.install_func("/version", self.version)
        log().tag("DdsService::init").info(
            "install func [ /echo /version ] to ", self.get_name())
        return 0

    def echo(self, pkg):
        # 入参 pkg 为注册消息类型的实例（消息包/持有语义：框架不拷贝）；
        # 返回框架 Response，强类型回包以 DdsResponse 承载在 data 位
        if not isinstance(pkg, DdsRequest):
            return Response(ResStatus.eNo, "pkg type mismatch, DdsRequest expected")
        return Response(ResStatus.eOk, "echo",
                        DdsResponse("ok", "echo", pkg.payload))

    def version(self, pkg):
        # 版本查询终结：版本号单一来源为 yomk.dds.__init__.__version__，
        # 调用期延迟导入避开循环 import
        from yomk.dds import __version__
        return Response(ResStatus.Succeeded, "ok", f"dds v{__version__}")
