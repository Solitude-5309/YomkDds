from yomk.msgs.server import Response


class DdsBuilder:
    def version(self) -> Response: ...


def dds() -> DdsBuilder: ...
