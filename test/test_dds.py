#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_dds.py —— YomkDds 扩展（dds 域）的运行级验证

流程：框架 init（内置 5 服务）→ 显式注册扩展运行时服务
（server().srv(DdsService).create()，无隐式发现）→ 统一请求接口打 ECHO
（扩展消息 DdsRequest 作请求包，强类型 DdsResponse 承载回包）→
dds 域 builder 版本查询终结（dds().version()）→ shutdown。
退出码 0 = 通过。
"""

import sys

from yomk.server import __version__, server
from yomk.msgs.server import ResStatus
from yomk.msgs.dds import DdsRequest, DdsResponse
from yomk.dds import DdsService, dds, __version__ as dds_version
from yomk.url.dds import ECHO   # URL 常量池（并入框架 yomk.url 命名空间段）
from yomk.log import log


def main():
    server().async_thread(1).init()
    try:
        log().tag("test_dds").info(f"version: framework={__version__} dds={dds_version}")
        # 扩展运行时服务：显式注册（框架不做隐式发现）
        ret = server().srv(DdsService).create()
        assert ret == 0, f"srv(DdsService).create() == {ret}"

        # 统一请求接口（推荐优先）：扩展消息作请求包直发，端点经 yomk.url.dds 常量池
        resp = server().url(ECHO).msg(DdsRequest("hello dds")).request()
        assert resp.status == ResStatus.eOk, (resp.status, resp.msg)
        data = resp.data
        assert isinstance(data, DdsResponse) and data.msg == "echo" \
            and data.data == "hello dds", data

        log().tag("test_dds").info("dds smoke OK:", data)

        # dds 域 builder 版本查询终结
        ver = dds().version()
        assert ver.status == ResStatus.Succeeded, (ver.status, ver.msg)
        assert ver.data == f"dds v{dds_version}", ver
        log().tag("test_dds").info("dds version OK:", ver.data)
        return 0
    finally:
        server().shutdown()


if __name__ == "__main__":
    sys.exit(main())
