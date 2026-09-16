# 好友功能当前状态与代码导航

更新日期：2026-09-08。代码基线：客户端 `7fc7faf`、服务端 `40950c6`，均位于 `feature/user-search`。

本文依据源码静态核对；没有执行构建、运行测试或数据库导入。“代码已落地”不等于已经联调通过。此前课程中的“当前状态”是课程编写时的历史快照，好友功能现状以本文和接口文档为准。

## 1. 已落地与尚缺环节

| 环节 | 代码状态 | 主要位置 |
| --- | --- | --- |
| 用户搜索 | QML 防抖、TCP 请求/响应、DAO 精确查询已接入 | `SakuraChat/qml/ChatDialog.qml`、`SakuraChat/src/tcpmgr.cpp`、两个 `LogicSystem.cpp` |
| 提交申请 | 客户端、共享 DAO 和 ChatServer2 处理器存在；ChatServer1 缺少处理器定义 | `Server/ChatServer2/src/LogicSystem.cpp`、`Server/Common/src/MysqlDao.cpp` |
| 在线申请通知 | 同节点发送、跨节点 NotifyAddFriend 调用与接收实现存在 | 两个 `ChatGrpcClient.cpp`、`ChatServiceImpl.cpp` |
| 离线待处理申请 | 两个节点在登录响应 1006 中附带最多 200 条 apply_list | 两个 `LoginHandler()`、共享 DAO |
| 审核申请 | 1012/1013 处理器、DAO 存储过程调用、审核弹窗存在 | 两个 `LogicSystem.cpp`、`SakuraChat/qml/ReviewFriendApplication.qml` |
| 审核结果通知 | 同节点发送及跨节点接收端存在；跨节点 RPC 客户端仍为空 | 两个 `ChatGrpcClient::NotifyAuthFriend()` |
| 申请列表与红点 | 按 applyId 去重、成功后改状态、pendingCount 属性绑定已接入 | `applyfriendlist.*`、`ApplyFriendPage.qml`、`ChatDialog.qml` |
| 联系人同步、真实聊天 | 未形成完整链路 | 不应由好友申请代码的落地推断为已完成 |

## 2. 代码放在哪里

路径均相对 `E:/SakuraChatProject`；两个聊天节点的业务变更需分别核对，不能假定完全相同。

| 内容 | 文件与插入范围 |
| --- | --- |
| 客户端消息 ID | `SakuraChat/src/global.h` 的现有 `ReqId`，不要另加 `MSG_IDS` |
| 服务端消息 ID | `Server/Common/include/const.h` 的 `MSG_IDS`；线上数值与客户端一致 |
| 登录申请快照 | `SakuraChat/src/tcpmgr.cpp` → `initHandlers()` → `ID_CHAT_LOGIN_RSP` 成功分支，保存用户信息后、`sig_switch_chatlg()` 前 |
| 搜索结果 Connections | `SakuraChat/qml/ChatDialog.qml` 根对象下；合并同名处理器，错误信息用根属性 `searchError` |
| 搜索资料与申请弹窗 | `ChatDialog.qml` 根对象中的 `FindSuccessDialog`、`ApplyFriend` 实例；delegate 给资料弹窗的 `userId/userName` 赋值 |
| ApplyInfo 和角色声明 | `SakuraChat/src/applyfriendlist.h`；`ApplyInfo` 在类外，角色/方法在类内 public 区，属性在类体内 |
| 模型方法定义 | `SakuraChat/src/applyfriendlist.cpp`；不用旧 `addItem/setAdded/isAdded` 契约 |
| 单行审核事件 | `SakuraChat/qml/ApplyFriendItem.qml` 根对象的属性/信号，按钮发出 `reviewClicked` |
| 审核弹窗实例 | `SakuraChat/qml/ApplyFriendPage.qml` 根 Item 下，与 Connections、ColumnLayout 同级；不要放进 delegate |
| 红点 | `ChatDialog.qml` 中 tooltipText 为“好友申请”的 SidebarIconBtn 内；绑定 `applyFriendPage.pendingCount` |
| 服务端 DTO | `Server/Common/include/data.h` 类/结构体外、头文件保护结束前 |
| DAO 声明及实现 | `Server/Common/include/MysqlDao.h` public 区声明；`Server/Common/src/MysqlDao.cpp` 类外定义 |
| 两份 MysqlMgr | `Server/ChatServer1`、`Server/ChatServer2` 的 include/MysqlMgr.h 声明，src/MysqlMgr.cpp 转发 `_dao`，不要复制 SQL |
| 登录回包 | 两个 `LogicSystem.cpp` 的 `LoginHandler()`：`SetUserSession(uid, session)` 后填充 `rtvalue["apply_list"]`，保留原 Defer 统一发送 |

## 3. 状态与生命周期边界

- 数据库申请主键 `friend_apply.id` → JSON `apply_id` → C++/QML `applyId`。实时通知的申请人字段为 `applyuid`，登录快照为 `uid`，TcpMgr 均转为模型的 `uid`。
- 状态为 0 待处理、1 同意、2 拒绝、3 撤销；当前没有撤销请求接口。`pendingCount` 统计模型中状态为 0 的条目。
- 页面监听实时申请并 upsert；创建时读取 `TcpMgr.friendApplySnapshot`，后续快照变化则 replaceAll；只在审核响应 `error == 0 && result == 0` 时 setStatus。
- `friendApplySnapshot` 目前只是最近一次登录快照。实时新增和审核更新仅修改页面模型，没有回写快照；页面销毁重建可能丢掉新增条目或恢复旧状态，不能宣称已解决所有页面重建问题。
- `applyId` 的 C++ 类型是 qint64；QML var 不保证任意 64 位整数无损，JavaScript Number 对超出安全整数范围的值可能丢精度。完整的大 ID 支持需要字符串端到端契约，当前未实施。

## 4. 待修复清单（不是本次文档更新已修复的代码）

1. **ChatServer1 缺少函数定义**：`LogicSystem.h` 声明并注册了 `AddFriendApply`，但对应 cpp 没有定义。需补齐才能避免相关链接问题。
2. **跨节点审核通知未发送**：两个 `ChatGrpcClient::NotifyAuthFriend()` 只创建并返回默认响应，未调用 stub；默认 error 为 0 不能证明通知成功。
3. **断线 pending 未复位**：`TcpMgr::resetBusinessPending()` 已定义但没有调用点；errorOccurred/disconnected 目前只打印日志，也没有业务请求超时。
4. **申请人侧未刷新联系人**：TcpMgr 发出 `sig_friend_auth_notified`，当前 QML 未接入该信号，好友列表同步接口也未完成。离线待处理申请快照不负责补回申请人的审核结果。
5. **快照不是实时状态仓库**：见上一节；另 `ApplyFriendList::clear()` 未发出 pendingCountChanged，清空后红点通知不完整。
6. **离线同步有容量边界**：登录固定 `afterId=0, limit=200`，无 hasMore 或下一页接口；第 201 条以后无法通过当前登录快照获取。DAO 查询错误与真正空列表均返回空 vector；两者尚不能区分。
7. **帧容量不能只看条数**：TCP 长度字段为 16 位，服务端另有 MAX_LENGTH=2048 的入站限制；申请资料组成的登录回包也需单独限制字节数，200 条不等于安全的单帧大小。
8. **公共资料查询不等于全局脱敏**：GetUser 不再读取 pwd，但 LoginHandler/GetBaseInfo 仍构造或读取 pwd，旧 Redis 缓存、HTTP 登录和敏感日志仍需治理。

## 5. QML 构建与 proto 维护

- `SakuraChat/CMakeLists.txt` 的 `qt_add_qml_module` 中，每个 QML 文件只登记一次。`ReviewFriendApplication.qml` 已在主 QML_FILES 列表中，不能再在末尾添加重复项。
- 若出现 `appSakuraChat_qmlcache_loader.cpp` 的 CachedQmlUnit/unit 重定义，先检查重复登记，修改后保存并重新运行 CMake，再构建；不要编辑生成文件。
- `ApplyFriendPage.qml` 使用 `import SakuraChat` 访问 `QML_ELEMENT` 类型。M16 属性错误需先核对 ApplyFriendItem 根对象的 applyId/status/reviewClicked 声明，再排查未保存文件或 IDE 旧类型信息。
- 现有完整协议位于 `Server/VarifyServer/message.proto`；`proto.js` 运行时加载它。`start.bat` 使用当前工作目录且只输出到该目录，应从 VarifyServer 目录运行，不会自动更新 Common。
- C++ 实际编译 Common/src 下的 message.pb.cc、message.grpc.pb.cc，并包含 Common/include 下对应两个头文件。生成后要同步这四个文件，并核对 include 路径、字段号和类型。
- Common/message.proto 与 GateServer/message.proto 仍是旧副本；不得直接用旧副本覆盖当前生成代码，也不要把删除这些副本写成已经执行的操作。

## 6. 文档与版本管理

- 实施步骤：[好友查询与申请教程](FRIEND_SEARCH_AND_APPLICATION_QML_TUTORIAL.md)。
- 契约：[接口设计](INTERFACE_DESIGN.md)、[数据设计](DATA_DESIGN.md)。
- 总体设计：[系统架构](SYSTEM_ARCHITECTURE_AND_TECHNICAL_DESIGN.md)。
- `SakuraChat/` 与 `Server/` 是两个独立 Git 仓库；根目录 docs、database 不在这两个仓库中。只提交客户端和服务端不会包含这些根目录文档。
- 数据库脚本未在本次更新中执行；`database/schema.sql` 会删除并重建 skrchat，不是普通增量升级脚本。
