# 好友认证和聊天通信：C++/QML 适配教程

修订日期：2026-09-08。接续 [好友查询与申请教程](FRIEND_SEARCH_AND_APPLICATION_QML_TUTORIAL.md)。

本文根据提供的 Widgets 原教程和当前工作区改写，只输出教程，不修改源码、不生成 proto、不执行 SQL、不构建或测试。代码块是供你逐步应用的目标实现，不能当作已经验证通过的补丁。

## 1. 先明确这一篇完成什么

本篇实现两条主线：

1. 同意好友申请后，双方刷新联系人；重新登录后仍能从 MySQL 加载好友。
2. 按目标 UID 打开会话，在线发送纯文本，同节点直接转发、跨节点通过 gRPC 转发，QML 展示按会话隔离的消息和发送状态。

本篇不是离线消息系统：不接入 chat_message、回执或 Outbox，不支持图片、文件、已读、自动重发与进程重启后的聊天记录恢复。目标离线时返回明确结果；转发超时不自动重试，避免制造重复消息。

### 1.1 当前源码核对结果

- 1007～1014 已用于搜索、申请、审核及通知；继续使用上一课的 apply_id/agree，不增加另一套 AuthFriendApply/AddFriend 裸 SQL。
- 两个节点已有 ResolveFriendApply、NotifyFriendResolution 和 ChatServiceImpl::NotifyAuthFriend。
- 两个 ChatGrpcClient::NotifyAuthFriend 和 NotifyTextChatMsg 仍为空；文本 RPC 接收端也为空。
- 当前工作区已经补上 ChatServer1::AddFriendApply，但尚未提交。这与上一份“按提交基线”的状态文档不同，不要再重复粘贴。
- ContactUserList 和 ChatUserList 的旧模型缺少稳定 UID，ChatView 只存单个本地 ListModel；不能按用户名或列表索引路由消息。
- 完整协议在 Server/VarifyServer/message.proto；Common 只提供当前 C++ 构建使用的生成文件。
- Session 的 _user_id 当前未初始化，客户端 readyRead 解析还存在修改底层缓冲区时继续使用旧流位置的风险；本篇给出必要修订。

### 1.2 与 Widgets 原教程的替换关系

| 原教程 | 本项目做法 |
| --- | --- |
| AuthFriendApply(fromuid,touid,back) | 保留 ResolveFriendApply(applyId, Session.uid, agree) 及事务过程 |
| 两次独立写库 | 保留 resolve_friend_apply 的行锁与原子双向关系 |
| AuthRsp/AuthInfo 和 QWidget 槽 | 现有 TcpMgr 信号触发好友列表重新同步 |
| UserMgr::_friend_list、_chat_items_added | 新增 TcpMgr 持有的 ChatStore，按 UID 和 msgid 管理状态 |
| QListWidgetItem/new ChatUserWid | 现有 ChatUserWid.qml 与 ListView delegate |
| ChatItemBase/TextBubble | 现有 MessageBubble.qml，数据源换成当前会话消息 |
| sex、back | 数据库 gender、friend.remark |
| 登录回包塞入全部好友 | 登录成功后自动分页同步，避免大包与截断 |

ChatStore 使用 QObject 属性和 QVariantList，是便于逐步接入的小规模方案，不是高性能大历史模型。旧 ChatUserList/ContactUserList 文件可保留，但本篇会移除聊天页对它们的实例化，避免演示数据和真实数据同时驱动 UI。

## 2. 文件改动地图与应用顺序

以下路径相对 E:/SakuraChatProject。所有“双节点”步骤均要应用到 ChatServer1 和 ChatServer2，已有定义应替换，不要重复增加。

| 顺序 | 文件 | 操作 |
| --- | --- | --- |
| 1 | SakuraChat/src/global.h、Server/Common/include/const.h | 在原枚举追加 ID；不要新建第二个客户端枚举 |
| 2 | 两个 ChatServer/include/CSession.h | 初始化未登录 UID |
| 3 | Common/include/data.h、MysqlDao.h、src/MysqlDao.cpp；两个 MysqlMgr.h/.cpp | 新增分页好友 DTO、查询和转发 |
| 3a | Server/Common/include/ChatWire.h（新增） | 共用的紧凑 JSON、UUID 与文本大小校验 |
| 4 | 两个 LogicSystem.h/.cpp | 新增好友分页与文本处理器，保留原审核事务 |
| 5 | 两个 ChatGrpcClient.cpp、ChatServiceImpl.cpp | 补齐审核与文本转发；不改 proto 字段号 |
| 6 | SakuraChat/src/chatstore.h（新增） | 长生命周期 UI 状态仓库 |
| 7 | SakuraChat/src/tcpmgr.h/.cpp | 解析修订、分页同步、文本发送和回包处理 |
| 8 | SakuraChat/qml/ChatDialog.qml、ChatView.qml、MessageBubble.qml | 按 UID 选择会话，绑定消息和状态 |
| 9 | SakuraChat/CMakeLists.txt | 仅在 SOURCES 登记一次新头文件 |

## 3. 消息契约：保留旧 ID，增加 1015～1019

在客户端 ReqId 和服务端 MSG_IDS 的 1014 后加逗号，再追加：

```cpp
ID_TEXT_CHAT_MSG_REQ = 1015,
ID_TEXT_CHAT_MSG_RSP = 1016,
ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1017,
ID_FRIEND_LIST_REQ = 1018,
ID_FRIEND_LIST_RSP = 1019
```

当前源码尚未占用这些 ID。它们是本教程新增契约，不是说现在代码已经支持。

### 3.1 审核保持不变

- 1012 请求：apply_id、agree，不传 fromuid/actorUid。
- 1013 响应：error、result、apply_id、agree。
- 1014 通知：error、result、apply_id、agree、peer_uid。
- error=0 且 result=0 且 agree=true 时触发好友同步；拒绝仍更新申请页状态，但不新增联系人。

### 3.2 纯文本消息

第一版每包只允许一条文本，避免原教程批量拆分导致空数组或部分成功语义不明确。

```json
{
  "touid": 1002,
  "text_array": [{"msgid": "客户端生成的UUID", "content": "你好"}]
}
```

1016 回包字段：error、fromuid、touid、msgid。1017 通知字段：error、fromuid、touid、text_array。发送者必须取 Session.GetUserId()，服务端不能信任客户端自报的 fromuid。

发送状态：

- pending：本地已提交发送，等待服务器回包。
- forward_attempted：服务器调用了目标 Session 的 Send；不等于送达或已读。
- failed：明确校验失败、不是好友或目标当前不在线。
- unknown：断线、RPC 失败或回包超时；可能已经被对方收到，禁止自动重发。

现有 CSession::Send 返回 void，队列满时会直接丢弃，因此本教程刻意不用 delivered/sent 表示端到端成功。未来应增加入队结果、消息落库和接收端 ACK 后再升级语义。

### 3.3 好友分页

1018 请求：

```json
{"request_id":"本轮同步UUID","after_uid":0}
```

1019 响应：

```json
{
  "error":0,
  "request_id":"原样回传",
  "after_uid":0,
  "next_uid":1002,
  "has_more":false,
  "friend_list":[
    {"uid":1002,"name":"bob","nick":"Bob","icon":"","remark":"","gender":0}
  ]
}
```

按 friend_uid 升序分页。客户端先收集整轮结果，成功到最后一页后再替换旧列表；查询失败不清空旧联系人。不是跨页事务快照，并发新增可能需要下一轮同步，审核期间发生的新刷新请求会合并为下一轮。

### 3.4 新增错误码与大小约束

只在服务端 Common/include/const.h 的现有 ErrorCodes 枚举追加，不要替换已有错误码：

```cpp
ChatNotFriend = 1101,
ChatTargetOffline = 1102,
ChatDataInvalid = 1103,
ChatDatabaseFailed = 1104
```

客户端保留现有 ErrorCodes，只读取服务端整数，无需另造同名枚举。

本篇请求和通知 body 限制为 1800 字节，低于服务端现有 MAX_LENGTH=2048；单条原文 UTF-8 限制为 512 字节。仍要检查 JSON 序列化后的实际字节数，因为引号、换行等会转义。不要用 QString.length() 判断线上长度。

## 4. 先修正 Session 身份默认值

两个 include/CSession.h 中将原成员：

```cpp
int _user_id;
```

替换为：

```cpp
int _user_id = 0;
```

新增处理器都会拒绝 uid<=0，登录成功之后仍沿用 session->SetUserId(uid)。否则未登录连接读取未初始化整数不能构成身份校验。本篇不引入多设备或重复登录状态机。

## 5. 好友查询：共享 DAO，不重复复制 SQL

### 5.1 DTO 与声明

Server/Common/include/data.h 顶部直接包含 <cstdint>、<vector>。在已有结构体之后、#endif 之前新增：

```cpp
struct FriendInfo {
    int uid = 0;
    std::string name, nick, icon, remark;
    int gender = 0;
};

struct FriendPageResult {
    bool ok = false;
    bool hasMore = false;
    std::vector<FriendInfo> items;
};
```

Common/include/MysqlDao.h 和两个 include/MysqlMgr.h 的 public 区各新增同一声明：

```cpp
FriendPageResult GetFriendPage(int selfUid, int afterUid, int limit);
```

### 5.2 MysqlDao.cpp 完整新增定义

位置：Server/Common/src/MysqlDao.cpp，与 GetPendingFriendApplies 同级；直接包含 <algorithm>、<memory>、<utility>。

```cpp
FriendPageResult MysqlDao::GetFriendPage(int selfUid, int afterUid, int limit)
{
    FriendPageResult out;
    if (selfUid <= 0 || afterUid < 0)
        return out;
    limit = std::clamp(limit, 1, 10);
    auto con = _pool->getConnection();
    if (!con)
        return out;
    Defer giveBack([this, &con] {
        _pool->returnConnection(std::move(con));
    });
    try {
        std::unique_ptr<sql::PreparedStatement> stmt(
            con->_con->prepareStatement(
                "SELECT u.uid,u.name,u.nick,u.icon,u.gender,f.remark "
                "FROM friend f JOIN user u ON u.uid=f.friend_uid "
                "WHERE f.self_uid=? AND f.friend_uid>? AND u.status=0 "
                "ORDER BY f.friend_uid ASC LIMIT ?"));
        stmt->setInt(1, selfUid);
        stmt->setInt(2, afterUid);
        stmt->setInt(3, limit + 1);
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
        while (res->next()) {
            if (static_cast<int>(out.items.size()) == limit) {
                out.hasMore = true;
                break;
            }
            FriendInfo item;
            item.uid = res->getInt("uid");
            item.name = res->getString("name");
            item.nick = res->getString("nick");
            item.icon = res->getString("icon");
            item.gender = res->getInt("gender");
            item.remark = res->getString("remark");
            out.items.push_back(std::move(item));
        }
        out.ok = true;
    } catch (const sql::SQLException &e) {
        std::cerr << "GetFriendPage failed, code="
                  << e.getErrorCode() << std::endl;
        out = FriendPageResult{};
    }
    return out;
}
```

这里 ok=false 与 ok=true/items.empty() 分别代表失败和真正没有好友，不能都显示“没有联系人”。

两个 src/MysqlMgr.cpp 各增加：

```cpp
FriendPageResult MysqlMgr::GetFriendPage(
    int selfUid, int afterUid, int limit)
{
    return _dao.GetFriendPage(selfUid, afterUid, limit);
}
```

不执行 schema.sql。当前 friend 表已有 self_uid/friend_uid/remark，本篇不改变数据库结构，更不能为新增查询重新删除 skrchat。

## 6. 新增共用协议小工具

新增 Server/Common/include/ChatWire.h，仅头文件 inline 实现。两个 LogicSystem.cpp 和两个 ChatServiceImpl.cpp 顶部包含 "ChatWire.h"；使用 std::move 的 cpp 直接包含 <utility>。它不是 protoc 生成文件，也不需要把它写入 proto。

```cpp
#pragma once
#include "const.h"
#include <cstddef>
#include <string>

namespace ChatWire {
inline constexpr std::size_t BodyLimit = 1800;

inline std::string Compact(const Json::Value &value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

inline bool Uuid(const std::string &id)
{
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return false;
        } else {
            const char c = id[i];
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) return false;
        }
    }
    return true;
}

inline bool Text(const std::string &id, const std::string &content)
{
    return Uuid(id) && !content.empty() && content.size() <= 512;
}

inline Json::Value Notification(int fromUid, int toUid,
                                const std::string &id,
                                const std::string &content)
{
    Json::Value root;
    root["error"] = ErrorCodes::Success;
    root["fromuid"] = fromUid;
    root["touid"] = toUid;
    root["text_array"] = Json::Value(Json::arrayValue);
    Json::Value item;
    item["msgid"] = id;
    item["content"] = content;
    root["text_array"].append(item);
    return root;
}
}
```

Common 的 include 目录已供聊天节点使用。若 IDE 不显示头文件，可在 Common 的头文件清单登记一次；头文件的 inline 定义不需要再复制到 cpp。

## 7. 两个 LogicSystem 新增两个请求处理器

### 7.1 声明与注册

两个 include/LogicSystem.h 的 private 区新增：

```cpp
void GetFriendList(std::shared_ptr<CSession>, const short &, const std::string &);
void DealChatTextMsg(std::shared_ptr<CSession>, const short &, const std::string &);
```

两个 src/LogicSystem.cpp 的 RegisterCallBacks() 内追加，保留原有注册：

```cpp
_fun_callbacks[ID_FRIEND_LIST_REQ] =
    std::bind(&LogicSystem::GetFriendList, this,
              std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
_fun_callbacks[ID_TEXT_CHAT_MSG_REQ] =
    std::bind(&LogicSystem::DealChatTextMsg, this,
              std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
```

### 7.2 好友分页完整定义

两个 src/LogicSystem.cpp 的类外各增加：

```cpp
void LogicSystem::GetFriendList(std::shared_ptr<CSession> session,
                               const short &, const std::string &body)
{
    Json::Value request, response;
    response["error"] = ErrorCodes::ChatDataInvalid;
    response["friend_list"] = Json::Value(Json::arrayValue);
    response["has_more"] = false;
    Defer reply([&] {
        session->Send(ChatWire::Compact(response), ID_FRIEND_LIST_RSP);
    });
    Json::Reader reader;
    if (body.size() > ChatWire::BodyLimit ||
        !reader.parse(body, request) || !request.isObject() ||
        !request["request_id"].isString() ||
        !ChatWire::Uuid(request["request_id"].asString()) ||
        !request["after_uid"].isInt())
        return;

    const int after = request["after_uid"].asInt();
    response["request_id"] = request["request_id"];
    response["after_uid"] = after;
    response["next_uid"] = after;
    const int uid = session->GetUserId();
    if (uid <= 0 || after < 0) return;

    const auto page = MysqlMgr::GetInstance()->GetFriendPage(uid, after, 10);
    if (!page.ok) {
        response["error"] = ErrorCodes::ChatDatabaseFailed;
        return;
    }
    response["error"] = ErrorCodes::Success;
    bool more = page.hasMore;
    for (const auto &f : page.items) {
        Json::Value item;
        item["uid"] = f.uid;
        item["name"] = f.name;
        item["nick"] = f.nick;
        item["icon"] = f.icon;
        item["remark"] = f.remark;
        item["gender"] = f.gender;

        Json::Value candidate = response;
        candidate["friend_list"].append(item);
        candidate["next_uid"] = f.uid;
        if (ChatWire::Compact(candidate).size() > ChatWire::BodyLimit) {
            if (response["friend_list"].empty())
                response["error"] = ErrorCodes::ChatDataInvalid;
            else
                more = true;
            break;
        }
        response = std::move(candidate);
    }
    response["has_more"] = response["error"].asInt() == 0 && more;
}
```

分页同时按条数和字节数限制。若某条资料单独就超出一包，返回错误而不是静默跳过；需要修正头像地址/资料长度或另做分块协议，不能显示“已同步全部”。next_uid 必须是本包最后实际返回的好友 UID。

### 7.3 在线文本处理器完整定义

位置同上。代码使用已有 FriendExists 作为“失败时拒绝”的发送权限检查；DAO 错误会被归为不是好友，后续可细分错误，但不能在失败时放行。

```cpp
void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session,
                                const short &, const std::string &body)
{
    Json::Value request, response;
    response["error"] = ErrorCodes::ChatDataInvalid;
    response["fromuid"] = session->GetUserId();
    response["touid"] = 0;
    response["msgid"] = "";
    Defer reply([&] {
        session->Send(ChatWire::Compact(response), ID_TEXT_CHAT_MSG_RSP);
    });

    Json::Reader reader;
    if (body.size() > ChatWire::BodyLimit ||
        !reader.parse(body, request) || !request.isObject() ||
        !request["touid"].isInt() ||
        !request["text_array"].isArray() ||
        request["text_array"].size() != 1)
        return;

    const auto &item = request["text_array"][0];
    if (!item.isObject() || !item["msgid"].isString() ||
        !item["content"].isString()) return;
    const auto id = item["msgid"].asString();
    const auto content = item["content"].asString();
    const int fromUid = session->GetUserId();
    const int toUid = request["touid"].asInt();
    response["touid"] = toUid;
    // 只回显合法长度的 UUID，避免错误回包被恶意大字段撑爆。
    if (ChatWire::Uuid(id)) response["msgid"] = id;
    if (fromUid <= 0 || toUid <= 0 || fromUid == toUid ||
        !ChatWire::Text(id, content)) return;

    if (!MysqlMgr::GetInstance()->FriendExists(fromUid, toUid)) {
        response["error"] = ErrorCodes::ChatNotFriend;
        return;
    }
    const auto notification = ChatWire::Notification(fromUid, toUid, id, content);
    const auto wire = ChatWire::Compact(notification);
    if (wire.size() > ChatWire::BodyLimit) return;

    std::string targetServer;
    if (!RedisMgr::GetInstance()->Get(
            std::string(USERIPPREFIX) + std::to_string(toUid), targetServer)) {
        response["error"] = ErrorCodes::ChatTargetOffline;
        return;
    }
    const auto selfServer = ConfigMgr::Inst().GetValue("SelfServer", "Name");
    if (targetServer == selfServer) {
        const auto target = UserMgr::GetInstance()->GetSession(toUid);
        if (!target) {
            response["error"] = ErrorCodes::ChatTargetOffline;
            return;
        }
        target->Send(wire, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
        response["error"] = ErrorCodes::Success; // 仅表示执行了转发尝试
        return;
    }

    message::TextChatMsgReq rpc;
    rpc.set_fromuid(fromUid);
    rpc.set_touid(toUid);
    auto *text = rpc.add_textmsgs(); // 所有权归 rpc，不能 delete
    text->set_msgid(id);
    text->set_msgcontent(content);
    const auto rsp = ChatGrpcClient::GetInstance()->NotifyTextChatMsg(
        targetServer, rpc, notification);
    response["error"] = rsp.error();
}
```

路由是 Redis 保存的服务器名称，不是客户端传入的 IP。现有 Send 队列丢弃、陈旧 Session、Redis 失败与离线混淆等限制仍存在；本篇不伪造已送达保证。

## 8. 补齐跨节点 RPC，保留现有 proto

### 8.1 不要覆盖原教程的完整 proto

原文 AddFriendReq 把 touid 改成字段 7，而当前工程 touid=4、apply_id=5、icon=6、nick=7、gender=8。直接替换会破坏现有字段的类型和编号。

当前 VarifyServer/message.proto 已有：

```proto
message AuthFriendReq {
  int32 fromuid = 1;
  int32 touid = 2;
  int64 apply_id = 3;
  bool agree = 4;
}
message TextChatData {
  string msgid = 1;
  string msgcontent = 2;
}
message TextChatMsgReq {
  int32 fromuid = 1;
  int32 touid = 2;
  repeated TextChatData textmsgs = 3;
}
```

以及 ChatService.NotifyAuthFriend、NotifyTextChatMsg 和响应类型。本篇不改变任何 proto 字段，不需要仅为实现函数而重新生成文件。

若你的生成文件未同步：从 VarifyServer 目录运行现有 start.bat，使用本机匹配版本的 protoc/plugin，再同步到 Common/include 的两个 .h 和 Common/src 的两个 .cc。start.bat 不会自动复制；不要照抄原作者 D:/cppsoft 路径，不要用旧 Common/message.proto 生成覆盖。

### 8.2 替换两个 ChatGrpcClient.cpp 的审核方法

保留现有头文件签名：std::string 按值，const message::AuthFriendReq&。cpp 直接包含 <chrono>。

```cpp
message::AuthFriendRsp ChatGrpcClient::NotifyAuthFriend(
    std::string serverName, const message::AuthFriendReq &request)
{
    message::AuthFriendRsp response;
    response.set_error(ErrorCodes::RPCFailed);
    auto it = _pools.find(serverName);
    if (it == _pools.end()) return response;
    auto &pool = it->second;
    auto stub = pool->getConnection();
    if (!stub) return response;
    Defer giveBack([&] { pool->returnConnection(std::move(stub)); });
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    const auto status = stub->NotifyAuthFriend(&context, request, &response);
    if (!status.ok()) response.set_error(ErrorCodes::RPCFailed);
    return response;
}
```

保留上一课已实现的两个 ChatServiceImpl::NotifyAuthFriend，以及 LogicSystem::NotifyFriendResolution；不要改回从客户端读取 fromuid/touid 的旧审核流程。审批成功后即便通知失败，关系仍已提交；联系人重新同步负责读取数据库事实。

### 8.3 替换两个 ChatGrpcClient.cpp 的文本方法

第三个 Json::Value 参数保留以匹配现有头文件，当前实现不用它：

```cpp
message::TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(
    std::string serverName, const message::TextChatMsgReq &request,
    const Json::Value &)
{
    message::TextChatMsgRsp response;
    response.set_error(ErrorCodes::RPCFailed);
    auto it = _pools.find(serverName);
    if (it == _pools.end()) return response;
    auto &pool = it->second;
    auto stub = pool->getConnection();
    if (!stub) return response;
    Defer giveBack([&] { pool->returnConnection(std::move(stub)); });
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    const auto status = stub->NotifyTextChatMsg(&context, request, &response);
    if (!status.ok()) response.set_error(ErrorCodes::RPCFailed);
    return response;
}
```

不能在找不到 pool 时返回默认 error=0。deadline 只约束 RPC，不包含前面的连接池等待。

### 8.4 替换两个 ChatServiceImpl.cpp 的文本接收方法

cpp 包含 "ChatWire.h"、"MysqlMgr.h"；UserMgr 已由现有头文件引入，可再直接包含 "UserMgr.h"。

```cpp
grpc::Status ChatServiceImpl::NotifyTextChatMsg(
    grpc::ServerContext *, const message::TextChatMsgReq *request,
    message::TextChatMsgRsp *response)
{
    response->set_error(ErrorCodes::ChatDataInvalid);
    response->set_fromuid(request->fromuid());
    response->set_touid(request->touid());
    if (request->fromuid() <= 0 || request->touid() <= 0 ||
        request->fromuid() == request->touid() || request->textmsgs_size() != 1)
        return grpc::Status::OK;
    const auto &text = request->textmsgs(0);
    if (!ChatWire::Text(text.msgid(), text.msgcontent()))
        return grpc::Status::OK;
    if (!MysqlMgr::GetInstance()->FriendExists(request->fromuid(), request->touid())) {
        response->set_error(ErrorCodes::ChatNotFriend);
        return grpc::Status::OK;
    }
    const auto target = UserMgr::GetInstance()->GetSession(request->touid());
    if (!target) {
        response->set_error(ErrorCodes::ChatTargetOffline);
        return grpc::Status::OK;
    }
    const auto wire = ChatWire::Compact(ChatWire::Notification(
        request->fromuid(), request->touid(), text.msgid(), text.msgcontent()));
    if (wire.size() > ChatWire::BodyLimit) return grpc::Status::OK;
    target->Send(wire, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
    response->set_error(ErrorCodes::Success);
    return grpc::Status::OK;
}
```

这里不依赖返回 textmsgs 副本，调用方只读取 error；也不打印消息正文。当前内网 RPC 没有身份认证，以上参数检查不能阻止恶意服务伪造 fromuid，正式部署仍需 mTLS/服务认证。

## 9. 新增客户端 ChatStore：按 UID 隔离消息，不保存 Widget 指针

### 9.1 完整新增 src/chatstore.h

它是头文件内实现的 QObject，由 TcpMgr 创建并作为子对象持有。客户端 UI 可以销毁重建而不丢失该登录周期的内存数据；换账号或重新认证时显式 reset。它不是磁盘聊天记录。

```cpp
#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QList>
#include <QString>
#include <QDateTime>
#include <QtQml/qqml.h>

class ChatStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("由 TcpMgr 持有，通过 tcpMgr.chatStore 使用")
    Q_PROPERTY(QVariantList friends READ friends NOTIFY stateChanged)
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY stateChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY stateChanged)
    Q_PROPERTY(int activeUid READ activeUid NOTIFY stateChanged)
    Q_PROPERTY(QString activeName READ activeName NOTIFY stateChanged)
public:
    explicit ChatStore(QObject *parent = nullptr) : QObject(parent) {}
    QVariantList friends() const { return _friends; }
    QVariantList messages() const { return _history.value(_active); }
    int activeUid() const { return _active; }
    QString activeName() const { return displayName(_active); }
    bool hasFriend(int uid) const { return !friendInfo(uid).isEmpty(); }

    void reset(int uid, const QString &name) {
        _self = uid; _selfName = name; _active = 0;
        _friends.clear(); _history.clear(); _order.clear(); _unread.clear();
        emit stateChanged();
    }

    void replaceFriends(const QVariantList &rows) {
        _friends.clear();
        for (const auto &v : rows) {
            auto row = v.toMap();
            const int uid = row.value("uid").toInt();
            if (uid <= 0 || uid == _self || hasFriend(uid)) continue;
            QString name = row.value("remark").toString();
            if (name.isEmpty()) name = row.value("nick").toString();
            if (name.isEmpty()) name = row.value("name").toString();
            if (name.isEmpty()) name = QString::number(uid);
            row["displayName"] = name;
            _friends.append(row);
            if (!_order.contains(uid)) _order.append(uid);
        }
        if (_active && !hasFriend(_active)) _active = 0;
        emit stateChanged();
    }

    Q_INVOKABLE bool openConversation(int uid) {
        if (!hasFriend(uid)) return false;
        _active = uid; _unread[uid] = 0;
        touch(uid);
        emit stateChanged();
        return true;
    }

    QVariantList conversations() const {
        QVariantList rows;
        for (int uid : _order) {
            // 已不在好友表中的旧会话暂不提供发送入口。
            if (!hasFriend(uid)) continue;
            const auto history = _history.value(uid);
            const auto last = history.isEmpty() ? QVariantMap{} : history.last().toMap();
            rows.append(QVariantMap{
                {"uid", uid}, {"name", displayName(uid)},
                {"head", friendInfo(uid).value("icon")},
                {"lastMsg", last.value("messageText", "")},
                {"time", last.value("timestamp", "")},
                {"unread", _unread.value(uid)}
            });
        }
        return rows;
    }

    void appendOutgoing(int peer, const QString &id, const QString &text) {
        append(peer, _self, id, text, true);
    }
    void appendIncoming(int peer, const QString &id, const QString &text) {
        append(peer, peer, id, text, false);
    }

    void mark(int peer, const QString &id, const QString &status) {
        auto it = _history.find(peer);
        if (it == _history.end()) return;
        for (auto &v : it.value()) {
            auto m = v.toMap();
            if (m.value("msgid").toString() == id &&
                m.value("isSentByMe").toBool()) {
                m["status"] = status; v = m;
                emit stateChanged();
                return;
            }
        }
    }

    void disconnectPending() {
        for (auto it = _history.begin(); it != _history.end(); ++it) {
            for (auto &v : it.value()) {
                auto m = v.toMap();
                if (m.value("status").toString() == "pending") {
                    m["status"] = "unknown"; v = m;
                }
            }
        }
        emit stateChanged();
    }

signals:
    void stateChanged();

private:
    QVariantMap friendInfo(int uid) const {
        for (const auto &v : _friends)
            if (v.toMap().value("uid").toInt() == uid) return v.toMap();
        return {};
    }
    QString displayName(int uid) const {
        const auto info = friendInfo(uid);
        return info.value("displayName", QString::number(uid)).toString();
    }
    void touch(int uid) {
        _order.removeAll(uid);
        _order.prepend(uid);
    }
    void append(int peer, int sender, const QString &id,
                const QString &text, bool mine) {
        auto &rows = _history[peer];
        // 去重键同时包含发送者，避免双方碰巧使用同一个 msgid。
        for (const auto &v : rows) {
            const auto m = v.toMap();
            if (m.value("msgid").toString() == id &&
                m.value("senderUid").toInt() == sender) return;
        }
        rows.append(QVariantMap{
            {"msgid", id}, {"senderUid", sender}, {"messageText", text},
            {"isSentByMe", mine}, {"imageSource", ""},
            {"senderName", mine ? _selfName : displayName(peer)},
            {"avatarSource", mine ? QString{} : friendInfo(peer).value("icon").toString()},
            {"timestamp", QDateTime::currentDateTime().toString("hh:mm")},
            {"status", mine ? "pending" : "received"}
        });
        // 教学版只保留每个会话最近 200 条；不是完整历史。
        while (rows.size() > 200) rows.removeFirst();
        if (!mine && peer != _active) ++_unread[peer];
        touch(peer);
        emit stateChanged();
    }
    int _self = 0, _active = 0;
    QString _selfName;
    QVariantList _friends;
    QHash<int, QVariantList> _history;
    QList<int> _order;
    QHash<int, int> _unread;
};
```

去重只覆盖当前内存窗口，超过 200 条后旧记录被移除，重启后也不存在。它不能替代服务端消息幂等；若等待回包的消息已被移出窗口，其状态不会再显示。本篇不做自动重发。

### 9.2 CMake 登记

在 SakuraChat/CMakeLists.txt 的 qt_add_qml_module 的现有 SOURCES 列表加入一次：

```cmake
src/chatstore.h
```

不创建第二个 qt_add_qml_module，不添加不存在的 chatstore.cpp。既有 ReviewFriendApplication.qml 不动，不要再次登记。无需在 main.cpp 手工注册同名类型。

## 10. TcpMgr 接入状态、好友分页和文本发送

本节不替换整个 TcpMgr，保留已有搜索、申请和登录接口。严格按声明、构造、方法、处理器四部分应用，不能只复制方法体。

### 10.1 tcpmgr.h 增量声明

顶部新增：

```cpp
#include "chatstore.h"
#include <QTimer>
#include <QHash>
```

类体原 Q_PROPERTY 后追加：

```cpp
Q_PROPERTY(ChatStore* chatStore READ chatStore CONSTANT)
Q_PROPERTY(bool chatReady READ chatReady NOTIFY chatReadyChanged)
Q_PROPERTY(bool friendSyncBusy READ friendSyncBusy NOTIFY friendSyncBusyChanged)
```

public 区追加：

```cpp
ChatStore *chatStore() const { return _chatStore; }
bool chatReady() const { return _chatReady; }
bool friendSyncBusy() const { return _friendSyncBusy; }
Q_INVOKABLE void refreshFriends();
Q_INVOKABLE QString sendTextMessage(int toUid, const QString &text);
```

signals 区追加：

```cpp
void chatReadyChanged();
void friendSyncBusyChanged();
void chatError(QString message);
```

private 区追加，注意不重复声明已有 _socket、resetBusinessPending：

```cpp
ChatStore *_chatStore = nullptr; // QObject 子对象，由 TcpMgr 自动销毁
bool _chatReady = false;
bool _friendSyncBusy = false;
bool _friendSyncAgain = false;
QString _friendRequestId;
int _friendCursor = 0;
QVariantList _friendStaging;
QTimer _friendTimer;
QHash<QString, int> _pendingTexts; // msgid -> 接收者 UID

bool sendSmallJson(ReqId id, const QJsonObject &object);
void requestFriendPage();
void failFriendSync(const QString &reason);
void onFriendPage(const QByteArray &data);
void onTextReply(const QByteArray &data);
void onTextNotify(const QByteArray &data);
void handleTransportLoss();
```

tcpmgr.cpp 顶部直接包含 <QJsonArray>、<QJsonDocument>、<QJsonParseError>、<QDataStream>、<QIODevice>、<QUuid>、<QTimer>，保留 "usermgr.h"。

### 10.2 构造函数：先创建状态，再注册处理器

TcpMgr 构造函数开头追加以下内容，放在原 initHandlers() 之前：

```cpp
_chatStore = new ChatStore(this);
_friendTimer.setSingleShot(true);
_friendTimer.setInterval(5000);
connect(&_friendTimer, &QTimer::timeout, this, [this] {
    failFriendSync("联系人同步超时；保留旧列表，可手动刷新");
});
```

这是 Qt 父子对象所有权，不是无主裸指针。所有 TcpMgr/ChatStore 方法应在 Qt GUI 线程运行，本篇不把它们搬到后台线程。

### 10.3 完整替换原 readyRead 连接

现有解析器在一个 QDataStream 还在使用时反复修改 _buffer，下一轮可能从错误位置读帧头。本篇新增多页/多条消息后必须处理连续帧。

在构造函数中找到唯一的 readyRead connect，整段替换为：

```cpp
connect(&_socket, &QTcpSocket::readyRead, this, [this] {
    _buffer.append(_socket.readAll());
    while (_buffer.size() >= 4) {
        const QByteArray header = _buffer.left(4);
        QDataStream stream(header);
        stream.setByteOrder(QDataStream::BigEndian);
        quint16 id = 0, length = 0;
        stream >> id >> length;
        // 接收仍兼容现有登录回包；服务端内部长度为 short。
        if (length == 0 || length > 32763) {
            emit chatError("收到非法长度帧");
            _socket.abort();
            return;
        }
        if (_buffer.size() < 4 + length) return; // 半包继续等待
        const QByteArray body = _buffer.mid(4, length);
        _buffer.remove(0, 4 + length);
        handleMsg(static_cast<ReqId>(id), length, body);
    }
});
```

不在半包时移除头部；每轮重新读取独立 header。旧 _b_recv_pending/_message_id/_message_len 字段可以暂留但不再参与这段逻辑，后续清理时同步改构造列表。

### 10.4 断线与重新登录

将原 errorOccurred/disconnected 只打印日志的两个连接分别替换为：

```cpp
connect(&_socket, &QTcpSocket::errorOccurred, this,
        [this](QAbstractSocket::SocketError) { handleTransportLoss(); });
connect(&_socket, &QTcpSocket::disconnected, this,
        [this] { handleTransportLoss(); });
```

新增类外定义：

```cpp
void TcpMgr::handleTransportLoss()
{
    const bool wasReady = _chatReady;
    _chatReady = false;
    if (wasReady) emit chatReadyChanged();
    _friendTimer.stop();
    _friendRequestId.clear();
    _friendStaging.clear();
    _friendSyncAgain = false;
    if (_friendSyncBusy) {
        _friendSyncBusy = false;
        emit friendSyncBusyChanged();
    }
    _pendingTexts.clear();
    _chatStore->disconnectPending();
    resetBusinessPending(); // 上一课已有定义，现在实际接入
    _buffer.clear();
    _b_recv_pending = false;
    if (wasReady) emit chatError("连接已断开，未确认消息状态未知");
}
```

在现有 slot_tcp_connect(ServerInfo si) 的最前面加：

```cpp
handleTransportLoss();
_chatStore->reset(0, QString{});
```

保留后面的关闭旧 socket、设置 host/port、connectToHost。这里重新登录视为新内存会话，不跨账号保留上个用户的联系人或聊天记录。

在 initHandlers() 原 ID_CHAT_LOGIN_RSP 成功分支中，UserMgr 信息和已有申请快照保存之后、emit sig_switch_chatlg() 之前增加：

```cpp
_chatStore->reset(UserMgr::GetInstance()->GetUid(),
                  UserMgr::GetInstance()->GetName());
_chatReady = true;
emit chatReadyChanged();
refreshFriends();
```

不要新增第二个登录处理器，不删除上一课 apply_list 的处理。聊天页可以先显示，联系人异步填充；首次为空不等于已经同步完成。

### 10.5 新请求发送辅助方法

保留原 slot_send_data(ReqId, QString) 给旧业务使用，不照抄原教程改成 QByteArray 签名。新增方法专供本篇的小帧请求：

```cpp
bool TcpMgr::sendSmallJson(ReqId id, const QJsonObject &object)
{
    if (!_chatReady || _socket.state() != QAbstractSocket::ConnectedState)
        return false;
    const auto body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > 1800 || _socket.bytesToWrite() > 65536)
        return false;
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint16>(id) << static_cast<quint16>(body.size());
    frame.append(body);
    if (_socket.write(frame) != frame.size()) {
        _socket.abort(); // 不在流中继续拼接下一条请求
        return false;
    }
    return true;
}
```

这里的 bytesToWrite 检查只是客户端提交水位控制，不是整个系统的背压方案。

### 10.6 好友分页方法完整定义

```cpp
void TcpMgr::refreshFriends()
{
    if (!_chatReady) {
        emit chatError("请先完成聊天服务器登录");
        return;
    }
    if (_friendSyncBusy) {
        _friendSyncAgain = true;
        return;
    }
    _friendSyncBusy = true;
    _friendSyncAgain = false;
    _friendCursor = 0;
    _friendStaging.clear();
    _friendRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    emit friendSyncBusyChanged();
    requestFriendPage();
}

void TcpMgr::requestFriendPage()
{
    const QJsonObject request{
        {"request_id", _friendRequestId}, {"after_uid", _friendCursor}
    };
    if (!sendSmallJson(ID_FRIEND_LIST_REQ, request)) {
        failFriendSync("联系人请求提交失败；旧列表未变");
        return;
    }
    _friendTimer.start();
}

void TcpMgr::failFriendSync(const QString &reason)
{
    if (!_friendSyncBusy) return;
    _friendTimer.stop();
    _friendStaging.clear();
    _friendRequestId.clear();
    _friendSyncAgain = false;
    _friendSyncBusy = false;
    emit friendSyncBusyChanged();
    emit chatError(reason);
}

void TcpMgr::onFriendPage(const QByteArray &data)
{
    if (!_chatReady || !_friendSyncBusy) return;
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        failFriendSync("联系人回包格式错误");
        return;
    }
    const auto root = doc.object();
    // 迟到的上一轮回包不能污染这一轮。
    if (root["request_id"].toString() != _friendRequestId ||
        root["after_uid"].toInt(-1) != _friendCursor) return;
    if (root["error"].toInt(-1) != 0) {
        failFriendSync("联系人查询失败：" + QString::number(root["error"].toInt(-1)));
        return;
    }
    if (!root["friend_list"].isArray() || !root["has_more"].isBool()) {
        failFriendSync("联系人分页字段错误");
        return;
    }
    QVariantList rows;
    int last = _friendCursor;
    for (const auto &v : root["friend_list"].toArray()) {
        if (!v.isObject()) { failFriendSync("联系人条目错误"); return; }
        const auto o = v.toObject();
        const int uid = o["uid"].toInt(-1);
        if (uid <= last || !o["name"].isString() || !o["nick"].isString() ||
            !o["icon"].isString() || !o["remark"].isString()) {
            failFriendSync("联系人字段或游标错误");
            return;
        }
        last = uid;
        rows.append(o.toVariantMap());
    }
    const bool more = root["has_more"].toBool();
    if (root["next_uid"].toInt(-1) != last || (more && last <= _friendCursor)) {
        failFriendSync("联系人分页未前进");
        return;
    }
    _friendTimer.stop();
    _friendStaging.append(rows);
    _friendCursor = last;
    if (more) {
        requestFriendPage();
        return;
    }
    _chatStore->replaceFriends(_friendStaging);
    _friendStaging.clear();
    _friendSyncBusy = false;
    emit friendSyncBusyChanged();
    const bool again = _friendSyncAgain;
    _friendSyncAgain = false;
    if (again) refreshFriends();
}
```

接收顺序保证、request_id、after_uid 是应用层控制，不可把错误页继续追加。刷新过程中 UI 继续使用上一次完整列表，首次登录则显示加载状态。

### 10.7 文本发送方法完整定义

```cpp
QString TcpMgr::sendTextMessage(int toUid, const QString &text)
{
    const QString content = text.trimmed();
    if (!_chatReady || !_chatStore->hasFriend(toUid) ||
        toUid == UserMgr::GetInstance()->GetUid()) {
        emit chatError("未登录或未选择有效好友");
        return {};
    }
    if (content.isEmpty() || content.toUtf8().size() > 512) {
        emit chatError("正文需为 1～512 个 UTF-8 字节");
        return {};
    }
    if (_pendingTexts.size() >= 32) {
        emit chatError("待确认消息过多，请稍后再发");
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonArray array;
    array.append(QJsonObject{{"msgid", id}, {"content", content}});
    const QJsonObject request{{"touid", toUid}, {"text_array", array}};
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > 1800) {
        emit chatError("转义后的消息包过大");
        return {};
    }

    _chatStore->appendOutgoing(toUid, id, content);
    _pendingTexts.insert(id, toUid);
    if (!sendSmallJson(ID_TEXT_CHAT_MSG_REQ, request)) {
        _pendingTexts.remove(id);
        _chatStore->mark(toUid, id, "unknown");
        emit chatError("发送未确认，请勿自动重发");
        return id;
    }
    QTimer::singleShot(10000, this, [this, id] {
        auto it = _pendingTexts.find(id);
        if (it == _pendingTexts.end()) return;
        const int peer = it.value();
        _pendingTexts.erase(it);
        _chatStore->mark(peer, id, "unknown");
        emit chatError("消息回包超时，结果未知");
    });
    return id;
}
```

返回空字符串表示本地拒绝，UI 保留输入；返回 UUID 表示已建立本地消息条目，UI 可清空输入，具体结果由状态字段呈现。不是返回 UUID 就表示成功。

### 10.8 新处理器方法及注册

两个文本处理方法：

```cpp
void TcpMgr::onTextReply(const QByteArray &data)
{
    if (!_chatReady) return;
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return; // 关联不到请求，保留 pending 等超时
    const auto root = doc.object();
    const QString id = root["msgid"].toString();
    auto it = _pendingTexts.find(id);
    if (it == _pendingTexts.end()) return;
    const int peer = it.value();
    if (root["fromuid"].toInt() != UserMgr::GetInstance()->GetUid() ||
        root["touid"].toInt() != peer) return;
    _pendingTexts.erase(it);
    const int error = root["error"].toInt(-1);
    const QString state = error == 0 ? "forward_attempted"
        : (error == 1101 || error == 1102 || error == 1103) ? "failed" : "unknown";
    _chatStore->mark(peer, id, state);
    if (error != 0) emit chatError("消息处理结果：" + QString::number(error));
}

void TcpMgr::onTextNotify(const QByteArray &data)
{
    if (!_chatReady || data.size() > 1800) return;
    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    const auto root = doc.object();
    if (root["error"].toInt(-1) != 0 ||
        root["touid"].toInt() != UserMgr::GetInstance()->GetUid() ||
        !root["text_array"].isArray()) return;
    const int sender = root["fromuid"].toInt();
    const auto array = root["text_array"].toArray();
    if (sender <= 0 || sender == UserMgr::GetInstance()->GetUid() || array.size() != 1)
        return;
    const auto item = array.first().toObject();
    const QString id = item["msgid"].toString();
    const QString content = item["content"].toString();
    if (id.size() != 36 || QUuid(id).isNull() ||
        content.isEmpty() || content.toUtf8().size() > 512) return;
    _chatStore->appendIncoming(sender, id, content);
    if (!_chatStore->hasFriend(sender)) refreshFriends();
}
```

过期回包忽略，不把已经显示 unknown 的记录追溯改为“已读”。暂时不认识的发送者先用 UID 保存消息，随后同步好友资料；不得因为窗口未打开就丢弃消息。

在 initHandlers() 函数内部、原处理器之后注册一次：

```cpp
_handlers.insert(ID_FRIEND_LIST_RSP,
    [this](ReqId, int, const QByteArray &data) { onFriendPage(data); });
_handlers.insert(ID_TEXT_CHAT_MSG_RSP,
    [this](ReqId, int, const QByteArray &data) { onTextReply(data); });
_handlers.insert(ID_NOTIFY_TEXT_CHAT_MSG_REQ,
    [this](ReqId, int, const QByteArray &data) { onTextNotify(data); });
```

### 10.9 审核完成后让双方刷新好友

在现有 ID_AUTH_FRIEND_RSP lambda 中，确认 doc.isObject()、取得 root 后，emit sig_friend_apply_resolved 之前插入：

```cpp
if (root["error"].toInt(-1) == 0 &&
    root["result"].toInt(-1) == 0 && root["agree"].toBool(false)) {
    refreshFriends();
}
```

在现有 ID_NOTIFY_AUTH_FRIEND_REQ lambda 中，原 error/result 成功检查之后、emit sig_friend_auth_notified 之前插入：

```cpp
if (root["agree"].toBool(false))
    refreshFriends();
```

刷新在 C++ 处理器完成，不依赖页面 Connections 是否已创建。保留申请页现有 setStatus(applyId, agree ? 1 : 2) 和红点；不再次创建审核弹窗、不用 uid 代替 applyId。

## 11. ChatDialog.qml：替换左侧两种列表，保留申请页

### 11.1 根对象新增状态和函数

在 id: chatDialog 后新增：

```qml
property string chatErrorMessage: ""

function openPeer(uid) {
    if (tcpMgr.chatStore.openConversation(uid)) {
        chatDialog.activeTab = "chat"
        chatDialog.chatErrorMessage = ""
    }
}
```

现有 target: tcpMgr 的 Connections 中新增一个函数，不再增加同名 Connections 处理器：

```qml
function onChatError(message) {
    chatDialog.chatErrorMessage = message
}
```

联系人和消息由 C++ 属性保存，不需要 Component.onCompleted 手工 connect 或再次复制列表。

### 11.2 完整替换 leftContentStack 这一整块

位置：contactListPanel → ColumnLayout → id: leftContentStack 的 StackLayout。只替换这个 StackLayout，保留上面的搜索栏和后面的 searchPanel。

新块不再实例化 ChatUserList/ContactUserList，不引用旧 chatModel/contactModel，也不调用 loadMoreItems 的演示分页：

```qml
StackLayout {
    id: leftContentStack
    Layout.fillWidth: true
    Layout.fillHeight: true
    currentIndex: chatDialog.activeTab === "contact" ? 1
                  : chatDialog.activeTab === "apply" ? 2 : 0

    ListView {
        id: chatListView
        clip: true
        model: tcpMgr.chatStore.conversations
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: ChatUserWid {
            required property var modelData
            width: chatListView.width
            userName: modelData.name
            headImg: modelData.head || ""
            lastMsg: (modelData.unread > 0
                      ? "[" + modelData.unread + " 条未读] " : "")
                     + modelData.lastMsg
            msgTime: modelData.time
            onClicked: chatDialog.openPeer(modelData.uid)
        }
    }

    ListView {
        id: contactListView
        clip: true
        model: tcpMgr.chatStore.friends
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        header: Row {
            width: contactListView.width
            spacing: 8
            Button {
                text: tcpMgr.friendSyncBusy ? "同步中…" : "刷新联系人"
                enabled: tcpMgr.chatReady && !tcpMgr.friendSyncBusy
                onClicked: tcpMgr.refreshFriends()
            }
            BusyIndicator {
                width: 32
                height: 32
                running: tcpMgr.friendSyncBusy
                visible: running
            }
        }

        delegate: ContactItem {
            required property var modelData
            width: contactListView.width
            contactName: modelData.displayName
            // 当前 ContactItem 显示首字母，不把 URL 当作首字母传入。
            contactHead: modelData.displayName.slice(0, 1)
            groupLabel: ""
            onItemClicked: function(name) {
                chatDialog.openPeer(modelData.uid)
            }
        }
    }

    ApplyFriendPage {
        id: applyFriendPage
    }
}
```

这里 QVariantList 的对象通过 modelData 访问，不继续使用旧 QAbstractListModel 的 model.uid/role 序号。required property var modelData 是复合对象，不能为了消除性能提示改成 int/real。

保留现有申请红点绑定 applyFriendPage.pendingCount。旧模型源码可以保留，不要为了停用演示数据而删除整个文件或改别的页面。

### 11.3 右侧聊天页绑定当前 UID

找到右侧 id: mainStack 的 StackLayout，替换 currentIndex：

```qml
currentIndex: tcpMgr.chatStore.activeUid > 0 ? 1 : 0
```

不要再给 mainStack.currentIndex 命令式赋值，否则会覆盖这个绑定。上面的新 delegate 已改为 openPeer(uid)。

在聊天页顶部：

- 显示 Alice 的 Text 改为 text: tcpMgr.chatStore.activeName。
- 头像中固定 A 改为 text: tcpMgr.chatStore.activeName.slice(0, 1)。
- 固定“在线”改为 text: tcpMgr.chatReady ? "已连接服务器" : "服务器连接已断开"。这不是对方在线状态。
- 对显示来自服务器的姓名、正文等 Text 使用 textFormat: Text.PlainText，避免 AutoText 把内容解释为富文本。

在 mainStack 的页面 1 ColumnLayout 中、原顶部栏 Rectangle 之前新增错误提示：

```qml
Label {
    Layout.fillWidth: true
    Layout.margins: 8
    visible: chatDialog.chatErrorMessage.length > 0
    text: chatDialog.chatErrorMessage
    textFormat: Text.PlainText
    color: "#d14343"
    wrapMode: Text.Wrap
}
```

### 11.4 发送按钮和函数

保留现有 messageInput（TextArea）及回车/Shift+Enter 逻辑。给现有 SendBtn 增加：

```qml
enabled: tcpMgr.chatReady && tcpMgr.chatStore.activeUid > 0
```

在根对象里完整替换原 sendMessage()，不要先调用 chatView.appendMessage：

```qml
function sendMessage() {
    var id = tcpMgr.sendTextMessage(tcpMgr.chatStore.activeUid, messageInput.text)
    if (id.length > 0)
        messageInput.clear()
}
```

删除原 currentUserName/currentUserIcon 两个无来源属性，以及旧 receiveMessage(senderName,avatarPath,text) 函数。接收消息现在直接进入 ChatStore，不允许只按显示名追加到当前窗口。

原 sendImageMessage 替换为明确提示，避免图片仅在本地显示却被误认为已发送：

```qml
function sendImageMessage(imagePath) {
    chatDialog.chatErrorMessage = "本篇只支持纯文本，图片和文件尚未接入"
}
```

附件/图片按钮不应宣称可发送；可以先禁用对应入口。表情若只是输入 Unicode 文字，可以作为普通文本发送。

### 11.5 ChatView 实例绑定消息列表

现有实例改成：

```qml
ChatView {
    id: chatView
    Layout.fillWidth: true
    Layout.fillHeight: true
    messageRows: tcpMgr.chatStore.messages
}
```

这不是把所有会话消息汇到同一列表：ChatStore.messages 只返回 activeUid 对应的数据。

## 12. 完整替换 ChatView.qml，给气泡显示状态

### 12.1 ChatView.qml 完整内容

```qml
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var messageRows: []

    function stateText(status) {
        switch (status) {
        case "pending": return "等待服务器响应"
        case "forward_attempted": return "服务器已尝试转发"
        case "failed": return "处理失败"
        case "unknown": return "结果未知"
        default: return ""
        }
    }

    onMessageRowsChanged: {
        Qt.callLater(function() { listView.positionViewAtEnd() })
    }

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        spacing: 4
        model: root.messageRows
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: MessageBubble {
            required property var modelData
            width: listView.width
            messageText: modelData.messageText
            imageSource: ""
            isSentByMe: modelData.isSentByMe
            senderName: modelData.senderName
            avatarSource: modelData.avatarSource || ""
            timestamp: modelData.timestamp
                       + (modelData.isSentByMe
                          ? " · " + root.stateText(modelData.status) : "")
        }
    }
}
```

移除原 ListModel、appendMessage、prependMessage，不能保留两份消息来源。全局搜索 chatView.appendMessage 后，应已没有调用点。本篇只渲染纯文本。

这是小规模教学方案，整表属性变化会重建部分 delegate，并可能触发自动滚动；更大历史应改为 QAbstractListModel 的行级通知、分页历史和“用户在底部才跟随滚动”，不把当前方案描述为性能优化。

### 12.2 MessageBubble.qml 的最小修改

不用修改现有属性名字。找到现有正文 Text（id: textMsg），添加：

```qml
textFormat: Text.PlainText
```

同样给 nameLabel、timeLabel 以及 ChatUserWid 中显示联系人姓名/最后消息的 Text 添加 PlainText。不是增加一个新的 Text，而是修改原有组件。不能把正文 HTML 当作网络图片/文件上传能力。

## 13. 登录、审核和文本通信时序

### 13.1 登录加载好友

1. 原 1005/1006 完成 Token 校验，保留原申请快照。
2. TcpMgr 设置 chatReady，reset 本登录周期的 ChatStore，然后发送 1018。
3. 服务端按 Session UID 查询自己的 friend 视图，返回字节数受限的一页。
4. TcpMgr 按 request_id/after_uid 校验并暂存，has_more=true 则继续。
5. 最后一页成功后一次性 replaceFriends，联系人列表与会话列表同时刷新。

本篇不在 LoginHandler 添加原文中的 friend_list 大数组，也不改 UserMgr::SetUserInfo/AppendFriendList，因为你的 UserMgr 目前没有这些接口。旧登录 apply_list 的 200 条与帧大小限制仍需单独治理，本篇的好友分页不会自动修复它。

### 13.2 B 同意 A 的申请

1. B 发 1012，actorUid 从 B 的 Session 取得。
2. resolve_friend_apply 事务写入双向好友关系。
3. B 收到 1013 成功，更新申请状态并刷新好友。
4. A 在线时收到 1014 成功同意通知，刷新好友；跨节点先通过 NotifyAuthFriend。
5. A 离线或通知失败时，下次登录的好友同步读到关系；也可用“刷新联系人”主动同步。

拒绝不会新增联系人。好友表才是关系事实来源，1014 不是包含完整用户资料的 AuthInfo，本篇不重复扩展通知携带资料。

### 13.3 A 给 B 发文本

1. A 选中 B 的 UID，TcpMgr 生成 UUID 和 pending 本地记录。
2. A 发 1015；服务端校验 UID、好友关系、正文长度和序列化长度。
3. 同节点调用 B 的 Session.Send；跨节点通过 NotifyTextChatMsg。
4. A 收到 1016：只更新对应 msgid/peer 的状态，不再追加一个气泡。
5. B 收到 1017：写入 A 对应的会话；未打开该会话也保存并增加未读计数。
6. B 点击 A 时切换 activeUid，读取该会话记录并清零其未读数。

未读数是本地 UI 状态，不会回传已读回执。ACK、在线转发和已读三个概念不能混为一谈。

## 14. 静态自查与常见报错

以下是你应用教程时的核对清单，不表示本次已运行编译或测试。

| 现象 | 优先核对 |
| --- | --- |
| QMap::insert 无匹配重载 | 客户端只有 ReqId，lambda 为 (ReqId,int,const QByteArray&) |
| ChatStore unknown type | chatstore.h 已在 SOURCES 登记一次，QML_ELEMENT 存在，页面 import SakuraChat，重新运行 CMake |
| chatStore 是 null | TcpMgr 构造函数先 new ChatStore(this)，再使用或连接回调 |
| Cannot assign to non-existent property messageRows | ChatView.qml 已按第 12 节替换并保存 |
| model.uid / model.name undefined | 新列表是 QVariantList，delegate 使用 modelData.uid/modelData.name |
| 同意成功但列表未更新 | 1013/1014 都加了 refreshFriends；RPC 客户端不是空函数；分页处理器已注册 |
| 获取一页后不再继续 | next_uid 是实际返回的最后 UID，has_more 没有错误复用条数判断 |
| 一次收到多帧后解析错乱 | readyRead 是否保留了旧 QDataStream 与 _buffer 改动混用的循环 |
| 发送显示两次 | QML 不再手工 append，回包只 mark，不再次 append |
| 切换联系人后消息串到别人窗口 | activeUid 是 UID；不要用名字、索引或当前页面直接接收全部消息 |
| 看得到气泡但对方未收到 | forward_attempted 不保证送达，核对路由、Session、RPC、发送队列；尚无端到端 ACK |
| 下次登录好友在、聊天记录没了 | 预期行为：好友在 MySQL，聊天只在本次内存窗口 |

此外逐一核对：

- 两个节点的方法声明、返回类型、参数 const/引用完全一致。
- ChatWire.h 是 Common/include 下的新增普通头文件，命名不要和生成代码混淆。
- 不重复注册 1012 处理器，不把 ResolveFriendApply 改成原教程的 AuthFriendApply。
- 不新增客户端可控的发送者 UID，不输出正文、Token 或密码到日志。
- 新文本逻辑限于一个 text_array 元素，原教程的任意批量包不能直接发送。
- 每个 QML 文件只登记一次；不修改 .rcc/qmlcache 下的生成代码。
- 不导入 schema.sql；它会删除并重建 skrchat，与你现在增加查询/收发代码不是一件事。

## 15. 后续要做，但不能写成当前已经完成

本篇先把在线通信与 UI 状态接通。正式消息系统至少还需要：

1. 基于 chat_message/client_msg_id 的持久化幂等，先入库后响应，不依赖客户端窗口内去重。
2. 服务端消息 ID、会话序号、分页历史、断线缺口同步。
3. 接收端投递 ACK 和单独的已读回执；把 Send 入队结果、发送完成与终端消费分开。
4. Outbox 消费者、有限重试与去重，处理 RPC 超时但对方已经处理的情况。
5. 状态租约和断线清理，防止陈旧 Redis 路由；明确单设备/多设备策略。
6. GUI Model 的行级通知、历史容量控制、消息增量更新；不长期复制整个 QVariantList。
7. gRPC 服务认证、TLS、输入限流、脱敏日志及可观测性。
8. 修复现有 LogicSystem::DealMsg 退出分支、CSession 生命周期/队列边界等旧问题，不能把本篇新增处理器当作已有基础设施无缺陷的证明。

数据库已有相关表不等于应用已实现消息落库；不能把只调用 TCP/RPC 的本篇示例描述成“可靠离线消息系统”。

## 16. 与上一课的衔接和提交说明

- 上一课继续负责搜索、提交申请、审核事务、待处理申请列表及红点。
- 本篇新增好友列表同步和在线纯文本；不另起一套好友关系模型。
- 原教程中 Widgets 代码、复制错的语言标签、HTML 转义符和作者本机路径均不应复制进源码。
- 本文只有教程内容写入文档。业务源码、CMake、数据库、proto 及生成文件没有因本教程生成而改变。
- 根目录 docs 不属于 SakuraChat/Server 两个 Git 仓库。之前“提交并推送”只执行了检查，没有完成提交或推送，本教程不包含在那次操作中。
