# YomkDds 扩展消息子包（并入框架 yomk.msgs 命名空间段，与框架 msgs/<域>/ 同构）。
# 载荷命名对齐总则：请求 = 端点驼峰 + Request（Debug/Bag 前缀）、响应 = Info/Result；
# msg 守卫用 kName 类属性（对齐 C++ kName 解包守卫）。
# 端点登记进度（对齐 C++ main 的 9 端点，每步一个接口）：
#   [x] get_version —— 无载荷端点（忽略载荷，不设专用结构体）
#   [ ] load_xml / create_node / delete_node / register_pub_topic / register_sub_topic
#       / publish / loan / discard_loan —— 载荷结构随各接口计划段逐步登记
__all__ = []
