# YomkDds 扩展 yomk.dds：运行时域子包（PEP 420 命名空间并入框架 yomk）
# 与框架 yomk/server、yomk/ctx 等域平级；扩展仓库不得创建 yomk/__init__.py
# 版本机制（与框架同构）：CMake configure_file 生成 _version.py 注入安装副本，
# 版本号单一来源为 CMakeLists.txt 的 project(VERSION)；此处 ImportError 回退为
# 源树直用（未经安装注入）时的占位值
try:
    from ._version import __version__
except ImportError:
    __version__ = "unknown"

from .DdsService import DdsService
from .builder import DdsBuilder, dds

__all__ = [
    "__version__",
    "dds", "DdsBuilder",
    "DdsService",
]
