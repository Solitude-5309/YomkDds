#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_yomk_dds_handler_contract.py —— YomkDdsHandler 服务层契约测试
（DDS-free，对齐 C++ main 分支 test/yomk_dds_handler/TestYomkDdsHandlerContract.cpp 的 T1 组）

覆盖：
  T1  /get_version 正常路径（Succeeded + "ok" + 版本串前缀 "YomkDds v" + 不含 "(WIP)"）；
  T1b /get_version 忽略载荷（None / 异类包均同 T1）；
  T3b 未知 URL Failed（框架路由回归）。

DDS-free 保证：/get_version 纯查询不碰 DDS（不创建 participant）；本测试不向后续端点
（create_node 等）传合法载荷——那会创建真实 DDS 资源，归后续接口的测试。

风格：与 test_dds.py / test_fastdds.py 同构——check 函数 + 失败计数，
逐用例输出 [PASS]/[FAIL]，退出码 0 = 全部通过。
"""

import os
import sys

# 安装树导入引导：固定从 $YOMK_PREFIX_PATH（缺省 /opt/yomk）site-packages 导入
PREFIX = os.environ.get("YOMK_PREFIX_PATH", "/opt/yomk")
PY_VER = f"{sys.version_info.major}.{sys.version_info.minor}"
SITE = os.path.join(PREFIX, f"lib/python{PY_VER}/site-packages")
if SITE not in sys.path:
    sys.path.insert(0, SITE)

from yomk.server import server                          # noqa: E402
from yomk.msgs.server import ResStatus                 # noqa: E402
from yomk.dds import YomkDdsHandler, dds               # noqa: E402
from yomk.dds import __version__ as dds_version        # noqa: E402
from yomk.url.dds import GET_VERSION                   # noqa: E402

FAILURES = []


def check(name, ok, detail=""):
    tag = "PASS" if ok else "FAIL"
    line = f"[{tag}] {name}"
    if detail:
        line += f" — {detail}"
    print(line)
    if not ok:
        FAILURES.append(name)


def main():
    server().async_thread(1).init()
    try:
        ret = server().srv(YomkDdsHandler).create()
        assert ret == 0, f"srv(YomkDdsHandler).create() == {ret}"

        # ---- T1：/get_version 正常路径（builder 链式入口）----
        ver = dds().version()
        check("T1 status == Succeeded", ver.status == ResStatus.Succeeded, str(ver.status))
        check("T1 msg == ok", ver.msg == "ok", repr(ver.msg))
        expect = f"YomkDds v{dds_version}"
        check("T1 data 前缀 YomkDds v",
              isinstance(ver.data, str) and ver.data.startswith("YomkDds v"),
              repr(ver.data))
        check("T1 data 精确值（版本单一来源 _version.py 注入）", ver.data == expect,
              f"expect {expect!r}")
        check("T1 版本串不含 (WIP)", "(WIP)" not in str(ver.data))

        # ---- T1b：/get_version 忽略载荷（统一请求接口，None / 异类包）----
        ver_none = server().url(GET_VERSION).msg(None).request()
        check("T1b 传 None 同 T1",
              ver_none.status == ResStatus.Succeeded and ver_none.msg == "ok"
              and ver_none.data == expect, str(ver_none))

        class FakePkg:
            pass  # 异类包：非 dds 域载荷（该端点无载荷结构体，任意包均忽略）

        ver_fake = server().url(GET_VERSION).msg(FakePkg()).request()
        check("T1b 传异类包同 T1",
              ver_fake.status == ResStatus.Succeeded and ver_fake.msg == "ok"
              and ver_fake.data == expect, str(ver_fake))

        # ---- T3b：未知 URL Failed（框架路由回归）----
        from yomk.server import _facade
        resp_miss = _facade.api_request("/YomkDdsHandler/not_registered", None)
        check("T3b 未知 URL Failed", resp_miss.status == ResStatus.Failed, str(resp_miss))

        if FAILURES:
            print(f"\n共 {len(FAILURES)} 个用例失败: {FAILURES}")
            return 1
        print("\n契约测试全部通过")
        return 0
    finally:
        server().shutdown()


if __name__ == "__main__":
    sys.exit(main())
