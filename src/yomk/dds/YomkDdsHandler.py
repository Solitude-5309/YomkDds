# YomkDds 核心通信服务（对齐 C++ main 分支 YomkDdsHandler，逐端点渐进对齐）
# 纪律：扩展服务不做隐式发现——使用方须显式 server().srv(YomkDdsHandler).create()
# 注册（见 test/test_dds.py）；框架只拉起 5 个内置服务。
# 调用方推荐优先使用 dds 域 builder（dds().version() 等）：builder 终结内部走
# _facade.api_request 统一请求链路，屏蔽 URL 之下的路由细节。
# 端点登记进度（对齐 C++ main 的 9 端点，每步一个接口）：
#   [x] /get_version   —— 版本查询（纯查询，不碰 DDS）
#   [ ] /load_xml /create_node /delete_node /register_pub_topic /register_sub_topic
#       /publish /loan /discard_loan —— 待后续接口计划段
from yomk.server import YomkService
from yomk.msgs.server import ResStatus, Response
from yomk.log import log


class YomkDdsHandler(YomkService):
    def __init__(self, server):
        super().__init__(server)
        self.set_name("/YomkDdsHandler")

    def init(self):
        self.install_func("/get_version", self.get_version)
        log().tag("YomkDdsHandler::init").info(
            "install func [ /get_version ] to ", self.get_name())
        return 0

    def get_version(self, pkg):
        # 版本查询终结（对齐 C++ /get_version）：纯查询不碰 DDS，忽略载荷
        # （None / 任意包均成功）；data 为 "YomkDds v<版本号>" 字符串。
        # 版本号单一来源 yomk.dds.__init__.__version__，调用期延迟导入避开循环 import
        from yomk.dds import __version__
        return Response(ResStatus.Succeeded, "ok", f"YomkDds v{__version__}")
