#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test_fastdds.py —— fastdds 绑定可用性验证（发布者/订阅者同进程互通）

用例：
  1. 导入来源校验：fastdds 与 msg 类型模块必须来自 $YOMK_PREFIX_PATH 安装前缀
     （缺省 /opt/yomk）——测试安装的正确性
  2. MString（msg 基本类型）发布/订阅互通
  3. MHeader（msg 嵌套类型）发布/订阅互通

约定：所有导入均从安装前缀 site-packages 导入；退出码 0 = 全部通过；
驱动侧 print，逐用例输出 [PASS]/[FAIL]。
"""

import os
import sys
import threading

# 安装树导入引导：固定从 $YOMK_PREFIX_PATH（缺省 /opt/yomk）site-packages 导入
PREFIX = os.environ.get("YOMK_PREFIX_PATH", "/opt/yomk")
PY_VER = f"{sys.version_info.major}.{sys.version_info.minor}"
SITE = os.path.join(PREFIX, f"lib/python{PY_VER}/site-packages")
if SITE not in sys.path:
    sys.path.insert(0, SITE)

import fastdds                              # noqa: E402  # 安装树导入
import yomk.dds_idl.std_msgs as YomkDdsStd  # noqa: E402  # idl/std_msgs.idl 生成的类型模块

DOMAIN = 77  # 独立测试域，避免与机器上其他 DDS 应用串扰
FAILURES = []


def check(name, ok, detail=""):
    tag = "PASS" if ok else "FAIL"
    line = f"[{tag}] {name}"
    if detail:
        line += f" — {detail}"
    print(line)
    if not ok:
        FAILURES.append(name)


class ReaderListener(fastdds.DataReaderListener):
    """订阅监听：take 样本进集合并通知主线程"""

    def __init__(self, data_type):
        super().__init__()
        self._data_type = data_type
        self.samples = []
        self.cv = threading.Condition()

    def on_data_available(self, reader):
        info = fastdds.SampleInfo()
        data = self._data_type()
        reader.take_next_sample(data, info)
        with self.cv:
            self.samples.append(data)
            self.cv.notify()

    def on_subscription_matched(self, datareader, info):
        pass


class WriterListener(fastdds.DataWriterListener):
    """发布监听：匹配计数并通知"""

    def __init__(self):
        super().__init__()
        self.matched = 0
        self.cv = threading.Condition()

    def on_publication_matched(self, datawriter, info):
        with self.cv:
            self.matched += info.current_count_change
            self.cv.notify()


class Endpoint:
    """同进程一对 发布者/订阅者（各自独立 participant，经默认发现互通）"""

    def __init__(self, topic_name, data_type, pubsub_type):
        factory = fastdds.DomainParticipantFactory.get_instance()

        # 订阅侧
        self.sub_participant = factory.create_participant(DOMAIN, fastdds.DomainParticipantQos())
        self.sub_type = pubsub_type()
        self.sub_type.set_name(topic_name + "_Type")
        self.sub_support = fastdds.TypeSupport(self.sub_type)
        self.sub_participant.register_type(self.sub_support)
        self.sub_topic = self.sub_participant.create_topic(
            topic_name, self.sub_type.get_name(), fastdds.TopicQos())
        self.subscriber = self.sub_participant.create_subscriber(fastdds.SubscriberQos())
        self.reader_listener = ReaderListener(data_type)
        self.reader = self.subscriber.create_datareader(
            self.sub_topic, fastdds.DataReaderQos(), self.reader_listener)

        # 发布侧
        self.pub_participant = factory.create_participant(DOMAIN, fastdds.DomainParticipantQos())
        self.pub_type = pubsub_type()
        self.pub_type.set_name(topic_name + "_Type")
        self.pub_support = fastdds.TypeSupport(self.pub_type)
        self.pub_participant.register_type(self.pub_support)
        self.pub_topic = self.pub_participant.create_topic(
            topic_name, self.pub_type.get_name(), fastdds.TopicQos())
        self.publisher = self.participant_create_publisher(self.pub_participant)
        self.writer_listener = WriterListener()
        self.writer = self.publisher.create_datawriter(
            self.pub_topic, fastdds.DataWriterQos(), self.writer_listener)

    @staticmethod
    def participant_create_publisher(participant):
        publisher_qos = fastdds.PublisherQos()
        participant.get_default_publisher_qos(publisher_qos)
        return participant.create_publisher(publisher_qos)

    def wait_match(self, timeout=10.0):
        with self.writer_listener.cv:
            return self.writer_listener.cv.wait_for(
                lambda: self.writer_listener.matched > 0, timeout)

    def wait_count(self, n, timeout=10.0):
        with self.reader_listener.cv:
            return self.reader_listener.cv.wait_for(
                lambda: len(self.reader_listener.samples) >= n, timeout)

    def teardown(self):
        self.sub_participant.delete_contained_entities()
        self.pub_participant.delete_contained_entities()
        factory = fastdds.DomainParticipantFactory.get_instance()
        factory.delete_participant(self.sub_participant)
        factory.delete_participant(self.pub_participant)


def case_import_origin():
    """用例 1：所有导入必须来自安装前缀（测试安装的正确性）"""
    name = "导入来源校验（fastdds / msg 类型均来自安装前缀）"
    ok = (fastdds.__file__.startswith(PREFIX + os.sep)
          and YomkDdsStd.__file__.startswith(PREFIX + os.sep))
    check(name, ok,
          f"fastdds={fastdds.__file__} msg={YomkDdsStd.__file__} 期望前缀={PREFIX}")


def write_and_collect(ep, make_samples, n):
    """发现匹配 → 逐条发布 → 等待收齐，返回 (ok, recv)"""
    if not ep.wait_match():
        return False, f"发现匹配超时（{ep.writer_listener.matched} 匹配）"
    for data in make_samples:
        ep.writer.write(data)
    if not ep.wait_count(n):
        return False, f"接收超时：仅收到 {len(ep.reader_listener.samples)}/{n} 条"
    return True, ep.reader_listener.samples


def case_mstring():
    """用例 2：MString（msg 基本类型）发布/订阅互通"""
    name = "MString 发布/订阅互通（msg 基本类型）"
    ep = Endpoint("yomkdds_test_mstring", YomkDdsStd.MString, YomkDdsStd.MStringPubSubType)
    try:
        payload = [f"yomkdds-msg-{i}" for i in range(5)]

        def make():
            for p in payload:
                data = YomkDdsStd.MString()
                data.data(p)
                yield data

        ok, recv = write_and_collect(ep, make(), len(payload))
        if ok:
            got = [d.data() for d in recv]
            check(name, sorted(got) == sorted(payload), f"期望 {payload}，实收 {got}")
        else:
            check(name, False, recv)
    finally:
        ep.teardown()


def case_multarraydim():
    """用例 3：MultiArrayDimension（msg 多字段类型 label/size/stride）发布/订阅互通"""
    name = "MultiArrayDimension 发布/订阅互通（msg 多字段类型）"
    ep = Endpoint("yomkdds_test_madim", YomkDdsStd.MultiArrayDimension, YomkDdsStd.MultiArrayDimensionPubSubType)
    try:
        def make():
            for i in range(3):
                data = YomkDdsStd.MultiArrayDimension()
                data.label(f"dim-{i}")
                data.size(i + 1)
                data.stride(i * 2)
                yield data

        ok, recv = write_and_collect(ep, make(), 3)
        if ok:
            got = sorted((d.label(), d.size(), d.stride()) for d in recv)
            expected = sorted((f"dim-{i}", i + 1, i * 2) for i in range(3))
            check(name, got == expected, f"期望 {expected}，实收 {got}")
        else:
            check(name, False, recv)
    finally:
        ep.teardown()


def main():
    print(f"安装前缀: {PREFIX}")
    print(f"site-packages: {SITE}")
    case_import_origin()
    case_mstring()
    case_multarraydim()
    total = 3
    passed = total - len(FAILURES)
    print(f"Test Summary: {passed}/{total} 通过")
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())
