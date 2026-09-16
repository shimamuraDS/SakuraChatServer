# SakuraChat 接口设计文档

## 1. 文档信息

| 项目 | 内容 |
| --- | --- |
| 文档名称 | SakuraChat 接口设计文档 |
| 文档版本 | 1.1 |
| 编制日期 | 2026-08-08 |
| 更新日期 | 2026-09-08 |
| 覆盖范围 | 客户端与 GateServer 的 HTTP 接口、客户端与 ChatServer 的 TCP 协议、服务间 gRPC 接口、QML/C++ 桥接接口 |
| 代码基线 | 当前工作区源码 |
| 关联文档 | `docs/SYSTEM_ARCHITECTURE_AND_TECHNICAL_DESIGN.md`、`docs/DATA_DESIGN.md` |

### 1.1 状态定义

- **已实现**：调用链及结果处理已经落地。
- **部分实现**：契约存在，但处理不完整或调用方未接入。
- **预留**：仅有协议、生成代码、空方法或 UI 占位。

本文记录当前实际行为。标为“整改建议”的内容不是现有接口承诺。

本次按客户端 `7fc7faf`、服务端 `40950c6` 静态核对好友功能，未构建或运行测试。ChatServer1 缺少 AddFriendApply 定义，跨节点审核 RPC 客户端仍为空；完整状态与代码位置见 [好友功能现状](FRIEND_FEATURE_STATUS.md)。

## 2. 接口全景

| 接口类型 | 调用方 | 提供方 | 编码/协议 | 当前用途 |
| --- | --- | --- | --- | --- |
| HTTP | Qt 客户端 | GateServer | HTTP/1.x + JSON | 验证码、注册、重置、账户登录 |
| TCP | Qt 客户端 | ChatServer | 自定义 4 字节帧头 + UTF-8 JSON | 聊天节点二次登录、用户搜索、好友申请与审核；文本消息预留 |
| gRPC | GateServer | VarifyServer | gRPC + Protobuf | 邮箱验证码 |
| gRPC | GateServer/ChatServer | StatusServer | gRPC + Protobuf | 聊天节点分配、令牌校验 |
| gRPC | ChatServer | ChatServer | gRPC + Protobuf | 跨节点申请通知已接入；审核通知部分实现，文本消息预留 |
| Qt Meta-Object | QML | C++ | `Q_INVOKABLE`、signals/slots | UI 与控制器、网络层、Model 交互 |

## 3. 通用约定

### 3.1 HTTP 基础约定

| 项目 | 当前约定 |
| --- | --- |
| Base URL | 客户端从应用目录 `config.ini` 的 `[GateServer] host/port` 拼接 |
| 版本前缀 | 无 |
| 业务方法 | 除测试接口外均为 POST |
| 请求类型 | `application/json` |
| 响应类型 | 业务处理器设置为 `text/json`；建议改为 `application/json` |
| 字符编码 | JSON 按 UTF-8 处理 |
| Keep-Alive | 关闭，每次请求后服务端关闭发送方向 |
| 成功 HTTP 状态 | `200 OK` |
| 业务失败 HTTP 状态 | 仍返回 `200 OK`，通过 JSON `error` 区分 |
| 未知路由 | `404 Not Found`，正文为 `url not found` |
| 超时 | 每个 `HttpConnection` 创建 60 秒截止计时器 |
| 认证头 | 无 |
| 请求追踪 ID | 无 |

GateServer 只显式处理 GET 和 POST。其他 HTTP 方法当前不会生成规范错误响应，连接最终由截止计时器关闭。

### 3.2 JSON 字段约定

- 字段名区分大小写。
- 用户 ID 使用 JSON 整数。
- 端口在登录响应中为字符串。
- 所有业务响应都应包含整数 `error`；当前没有统一 `message`、`data` 或 `requestId` 包装层。
- 服务端除 `/get_varifycode` 外未完整检查必填字段是否存在、类型是否正确。
- 示例中的密码、令牌和验证码均为占位符，不代表真实数据。

### 3.3 推荐的目标响应包络

当前接口不使用以下统一结构；后续版本建议采用：

```json
{
  "code": 0,
  "message": "success",
  "requestId": "<trace-id>",
  "data": {}
}
```

采用新包络应通过 `/api/v1` 或显式协议版本发布，避免直接破坏现有客户端。

## 4. HTTP 接口

### 4.1 接口清单

| 方法 | 路径 | 用途 | 客户端请求 ID | 状态 |
| --- | --- | --- | --- | --- |
| GET | `/get_test` | 网关连通及 Query 解析测试 | 无 | 已实现，仅开发用途 |
| POST | `/get_varifycode` | 发送邮箱验证码 | `1001` | 已实现 |
| POST | `/user_register` | 注册用户 | `1002` | 已实现 |
| POST | `/reset_pwd` | 重置密码 | `1003` | 已实现 |
| POST | `/user_login` | 账户登录并分配聊天节点 | `1004` | 已实现 |

### 4.2 GET `/get_test`

用途：开发期验证 GateServer 路由和 Query 参数解析。

请求示例：

```http
GET /get_test?name=test&lang=zh HTTP/1.1
Host: <gate-host>
```

响应：纯文本，按内部 `unordered_map` 遍历顺序回显参数，顺序不稳定。

| HTTP 状态 | 含义 |
| --- | --- |
| 200 | 路由存在并完成回显 |
| 404 | 路由不存在 |

生产环境建议移除或改造成不回显输入的健康检查接口。

### 4.3 POST `/get_varifycode`

用途：请求 VarifyServer 生成或复用验证码，并向指定邮箱发送邮件。

请求字段：

| 字段 | 类型 | 必填 | 当前服务端校验 | 说明 |
| --- | --- | --- | --- | --- |
| `email` | string | 是 | 仅检查字段存在，不验证格式或长度 | 接收验证码的邮箱 |

请求示例：

```json
{
  "email": "user@example.com"
}
```

成功响应：

```json
{
  "error": 0,
  "email": "user@example.com"
}
```

失败响应：

```json
{
  "error": 1001
}
```

可能错误：

| `error` | 来源 | 含义 |
| --- | --- | --- |
| `1001` | GateServer | JSON 无法解析或缺少 `email` |
| `1002` | GateServer gRPC 客户端 | gRPC 调用失败 |
| `1` | VarifyServer | Redis 写入失败 |
| `2` | VarifyServer | 邮件发送等处理抛出异常 |

实现说明：

- VarifyServer 对同一邮箱在验证码有效期内复用原值。
- Redis TTL 为 600 秒；邮件正文当前写为 3 分钟，二者不一致。
- 当前没有邮箱/IP 限流、验证码尝试次数和防机器人机制。

### 4.4 POST `/user_register`

用途：校验验证码及两次密码，调用 MySQL 存储过程创建用户。

请求字段：

| 字段 | 类型 | 必填 | 当前服务端校验 | 说明 |
| --- | --- | --- | --- | --- |
| `username` | string | 是 | 未显式检查存在、长度和字符集 | 登录名 |
| `email` | string | 是 | 未显式验证格式 | 邮箱 |
| `varifycode` | string | 是 | 与 Redis 中验证码做字符串比较 | 验证码 |
| `password` | string | 是 | 仅与 `confirm` 比较 | 密码 |
| `confirm` | string | 是 | 仅与 `password` 比较 | 确认密码 |

请求示例：

```json
{
  "username": "demo_user",
  "email": "user@example.com",
  "varifycode": "<code>",
  "password": "<password>",
  "confirm": "<password>"
}
```

当前成功响应字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `error` | int | `0` |
| `uid` | int | 存储过程返回的用户 ID |
| `username` | string | 原请求用户名 |
| `email` | string | 原请求邮箱 |
| `password` | string | 当前实现原样返回；必须删除 |
| `confirm` | string | 当前实现原样返回；必须删除 |
| `varifycode` | string | 当前实现原样返回；必须删除 |

脱敏示例：

```json
{
  "error": 0,
  "uid": 10001,
  "username": "demo_user",
  "email": "user@example.com",
  "password": "<must-not-return>",
  "confirm": "<must-not-return>",
  "varifycode": "<must-not-return>"
}
```

可能错误：

| `error` | 含义 |
| --- | --- |
| `1001` | JSON 解析失败 |
| `1003` | Redis 中未找到验证码，按过期处理 |
| `1004` | 验证码不匹配 |
| `1005` | 用户名或邮箱已存在，或存储过程返回 `0/-1` |
| `1006` | 两次密码不一致 |

当前注册成功后不会删除验证码，因此有效期内仍可能被重复使用；应改为原子校验并消费。

### 4.5 POST `/reset_pwd`

用途：校验邮箱验证码、用户名与邮箱关系，然后更新密码。

请求字段：

| 字段 | 类型 | 必填 | 当前服务端校验 | 说明 |
| --- | --- | --- | --- | --- |
| `user` | string | 是 | 用于按用户名查询邮箱 | 用户名 |
| `email` | string | 是 | 与数据库邮箱比较 | 邮箱 |
| `passwd` | string | 是 | 未做强度及格式校验 | 新密码 |
| `varifycode` | string | 是 | 与 Redis 值比较 | 验证码 |

请求示例：

```json
{
  "user": "demo_user",
  "email": "user@example.com",
  "passwd": "<new-password>",
  "varifycode": "<code>"
}
```

当前成功响应字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `error` | int | `0` |
| `user` | string | 用户名 |
| `email` | string | 邮箱 |
| `passwd` | string | 当前原样返回新密码；必须删除 |
| `varifycode` | string | 当前原样返回验证码；必须删除 |

可能错误：

| `error` | 含义 |
| --- | --- |
| `1001` | JSON 解析失败 |
| `1003` | 验证码不存在或过期 |
| `1004` | 验证码错误 |
| `1007` | 用户名与邮箱不匹配 |
| `1008` | 密码更新失败 |

数据一致性注意事项：密码更新后没有删除验证码，也没有失效 `_utoken<uid>` 或 `_ubaseinfo<uid>` 缓存。

### 4.6 POST `/user_login`

用途：校验账户密码，向 StatusServer 申请 ChatServer 地址和临时令牌。

请求字段：

| 字段 | 类型 | 必填 | 当前服务端校验 | 说明 |
| --- | --- | --- | --- | --- |
| `email` | string | 是 | 用于数据库查询，未验证格式 | 登录邮箱 |
| `passwd` | string | 是 | 与数据库值直接比较 | 密码 |

请求示例：

```json
{
  "email": "user@example.com",
  "passwd": "<password>"
}
```

成功响应字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `error` | int | `0` |
| `email` | string | 登录邮箱 |
| `uid` | int | 用户 ID |
| `host` | string | 被分配 ChatServer 的 TCP 主机 |
| `port` | string | 被分配 ChatServer 的 TCP 端口 |
| `token` | string | StatusServer 生成的 UUID 字符串 |

响应示例：

```json
{
  "error": 0,
  "email": "user@example.com",
  "uid": 10001,
  "host": "<chat-host>",
  "port": "<chat-port>",
  "token": "<uuid-token>"
}
```

可能错误：

| `error` | 含义 |
| --- | --- |
| `1001` | JSON 解析失败 |
| `1002` | 调用 StatusServer 失败或 StatusServer 返回错误 |
| `1009` | 邮箱不存在或密码不匹配 |

StatusServer 当前从无序节点容器中取 `begin()`，没有真正执行按连接数负载均衡，因此不能依赖稳定的节点选择顺序。

## 5. 错误码

### 5.1 服务端业务错误码

| 代码 | 枚举 | 当前含义 | 典型接口 |
| --- | --- | --- | --- |
| `0` | `Success` | 成功 | 全部 |
| `1001` | `Error_Json` | JSON 解析失败或关键 JSON 字段缺失 | HTTP |
| `1002` | `RPCFailed` | gRPC 调用失败 | 验证码、登录 |
| `1003` | `VarifyExpired` | 验证码不存在/过期 | 注册、重置 |
| `1004` | `VarifyCodeErr` | 验证码错误 | 注册、重置 |
| `1005` | `UserExist` | 用户名或邮箱已存在 | 注册 |
| `1006` | `PasswdErr` | 两次密码不一致 | 注册 |
| `1007` | `EmailNotMatch` | 用户名与邮箱不匹配 | 重置 |
| `1008` | `PasswdUpFailed` | 更新密码失败 | 重置 |
| `1009` | `PasswdInvalid` | 登录密码无效或用户不存在 | 登录 |
| `1010` | `TokenInvalid` | Chat 登录 token 不匹配 | TCP 登录、Status Login |
| `1011` | `UidInvalid` | token key 不存在或用户资料无法取得 | TCP 登录、Status Login |

### 5.2 VarifyServer 内部错误码

| 代码 | 含义 |
| --- | --- |
| `0` | 成功 |
| `1` | Redis 写入错误 |
| `2` | 处理异常，如邮件发送失败 |

这些代码会被 `/get_varifycode` 原样放入 `error`，与客户端本地错误码发生数值重叠。

### 5.3 客户端本地错误码

| 代码 | 枚举 | 含义 |
| --- | --- | --- |
| `0` | `SUCCESS` | 客户端处理成功 |
| `1` | `ERR_JSON` | 客户端解析 JSON 失败 |
| `2` | `ERR_NETWORK` | Qt 网络层错误 |

建议建立单一错误码命名空间，至少按 `HTTP业务/聊天协议/客户端本地/服务内部` 分段。

## 6. TCP 接口

### 6.1 建连方式

1. 客户端先调用 `/user_login`。
2. 从响应取得 ChatServer `host`、`port`、`token` 和 `uid`。
3. `QTcpSocket.connectToHost(host, port)` 建立长连接。
4. 连接成功后发送消息 `1005` 完成二次认证。

当前未实现 TLS、心跳、自动重连、协议握手、版本协商和压缩。

### 6.2 帧格式

```text
Offset  Length  Type      Byte order  Field
0       2       uint16    Big Endian  message_id
2       2       uint16    Big Endian  body_length
4       N       bytes     -           UTF-8 JSON body
```

约束：

| 项目 | 当前值 |
| --- | --- |
| 固定帧头 | 4 字节 |
| 消息 ID 长度 | 2 字节 |
| Body 长度字段 | 2 字节 |
| 服务端允许的消息体上限 | 2048 字节 |
| 单会话发送队列上限 | 1000 条；超出后丢弃新消息 |
| 入站队列常量 | 10000，但当前没有实施容量检查 |
| Body 编码 | UTF-8 JSON |

客户端和服务端均处理 TCP 粘包、半包。服务端会将完整消息体投递到 `LogicSystem` 单线程业务队列。

### 6.3 消息 `1005`：聊天节点登录

方向：Client → ChatServer。

Body：

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `uid` | int | 是 | `/user_login` 返回的用户 ID |
| `token` | string | 是 | `/user_login` 返回的 UUID token |

示例：

```json
{
  "uid": 10001,
  "token": "<uuid-token>"
}
```

处理逻辑：

1. 从 Redis 读取 `_utoken<uid>`。
2. key 不存在返回 `1011`。
3. token 不一致返回 `1010`。
4. 读取 `_ubaseinfo<uid>`，未命中则查询 MySQL 并回填。
5. 注册本地会话、用户节点位置和节点连接计数。

当前未检查 JSON 解析结果和字段类型；缺失 `uid` 会被解释为 `0`，缺失 `token` 会被解释为空字符串。

### 6.4 消息 `1006`：聊天节点登录响应

方向：ChatServer → Client。

失败响应：

```json
{
  "error": 1010
}
```

成功响应当前字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `error` | int | `0` |
| `uid` | int | 用户 ID |
| `name` | string | 用户名 |
| `email` | string | 邮箱 |
| `pwd` | string | 当前实现返回密码；必须删除 |
| `nick` | string | 昵称，GetUser 已读取；旧缓存可能陈旧 |
| `desc` | string | 用户简介 |
| `gender` | int | 新库字段为 gender，不是 sex |
| `icon` | string | 头像 |
| `apply_list` | array | 收到的待处理申请，按申请 ID 升序，最多 200 条；无记录返回 [] |

客户端 `TcpMgr` 还尝试从此响应读取 `token`，但 ChatServer 当前不返回该字段，因此客户端存储的 token 会为空。后续应明确是保留原 token、返回轮换后的 token，还是不在客户端 UserMgr 中保存。

### 6.5 TCP 好友消息契约

客户端继续扩展 ReqId，服务端扩展 MSG_IDS，线上数值必须一致。以下为当前代码字段，不使用旧课程的编号草案。

| ID | 消息名 | 方向 | JSON 字段 |
| --- | --- | --- | --- |
| 1005 | ID_CHAT_LOGIN / MSG_CHAT_LOGIN | 客户端 → 服务端 | uid、token |
| 1006 | ID_CHAT_LOGIN_RSP / MSG_CHAT_LOGIN_RSP | 服务端 → 客户端 | error、用户资料、apply_list |
| 1007 | ID_SEARCH_USER_REQ | 客户端 → 服务端 | keyword:string |
| 1008 | ID_SEARCH_USER_RSP | 服务端 → 客户端 | error:int、found:bool；找到时带 uid/name/nick/desc/gender/icon/is_friend |
| 1009 | ID_ADD_FRIEND_REQ | 客户端 → 服务端 | touid:int、descs:string、back_name:string |
| 1010 | ID_ADD_FRIEND_RSP | 服务端 → 申请人 | error:int、result:int、apply_id:int64 |
| 1011 | ID_NOTIFY_ADD_FRIEND_REQ | 服务端 → 接收人 | error、apply_id、applyuid、name、nick、icon、gender、message；同节点还带 desc |
| 1012 | ID_AUTH_FRIEND_REQ | 客户端 → 服务端 | apply_id:int64、agree:bool |
| 1013 | ID_AUTH_FRIEND_RSP | 服务端 → 审核人 | error:int、result:int、apply_id:int64、agree:bool |
| 1014 | ID_NOTIFY_AUTH_FRIEND_REQ | 服务端 → 申请人 | error:int、result:int、apply_id:int64、agree:bool、peer_uid:int |

搜索为精确 UID 或用户名查询；纯数字关键字按 UID 解释，不是模糊搜索。服务端 keyword 限制为 1～64 个 UTF-8 字节；公开结果不返回 pwd/email/token。查询无记录返回 error=0、found=false；DAO 错误也可能表现为未找到，尚未细分。

申请的 descs 最多 255 字节、back_name 最多 64 字节；发送者和审核者 UID 取自 Session，不由请求提供。ChatServer2 的 1009 处理器已定义，ChatServer1 仅有声明与注册，缺少定义。

申请 result=0 表示已保存，1 表示已经是好友，-1 表示失败；审核 result=0 表示成功，-1 表示失败或不允许处理。error=0 不能代替 result 检查。重复审核不会再建关系，但当前存储过程返回 -1，不是幂等成功响应。数字相同的 error 和消息 ID 属于不同命名空间。

apply_list 每项为 apply_id:int64、uid:int、name:string、nick:string、gender:int、icon:string、message:string、status:int；这里只查 status=0。登录调用固定 afterId=0、limit=200，无后续分页协议或 hasMore。DAO 失败也返回空数组语义，尚不能保证完整同步。

当前没有联系人同步、文本消息或消息 ACK 的正式接入 ID。1012/1013 已用于审核，不能再用于申请列表分页。

## 7. gRPC 接口

### 7.1 通用约定

- Package：`message`。
- 调用模型：Unary RPC。
- 当前使用 `InsecureChannelCredentials` / `InsecureServerCredentials`。
- C++ gRPC Stub 池默认大小为 5。
- NotifyAddFriend 的 C++ 调用已设置 3 秒 deadline；不能由此推断其他 RPC 均有超时。当前尚无统一 metadata、认证、重试和 trace context。
- 业务错误通过响应 `error` 表达；传输错误通过 gRPC Status 表达。

### 7.2 `VarifyService.GetVarifyCode`

调用：GateServer → VarifyServer。

请求 `GetVarifyReq`：

| 字段号 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | `email` | string | 接收邮箱 |

响应 `GetVarifyRsp`：

| 字段号 | 字段 | 类型 | 当前行为 |
| --- | --- | --- | --- |
| 1 | `error` | int32 | `0/1/2` 或 Gate 侧映射的 `1002` |
| 2 | `email` | string | 原请求邮箱 |
| 3 | `code` | string | 协议定义但服务实现不返回 |

状态：已实现。

### 7.3 `StatusService.GetChatServer`

调用：GateServer → StatusServer。

请求 `GetChatServerReq`：

| 字段号 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | `uid` | int32 | 用户 ID |

响应 `GetChatServerRsp`：

| 字段号 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 1 | `error` | int32 | 业务错误码 |
| 2 | `host` | string | ChatServer TCP 主机 |
| 3 | `port` | string | ChatServer TCP 端口 |
| 4 | `token` | string | 新生成的 UUID token |

状态：已实现，但负载均衡逻辑被注释，节点选择不稳定且不反映实际负载。

### 7.4 `StatusService.Login`

调用设计：ChatServer → StatusServer。

请求 `LoginReq`：`uid:int32`、`token:string`。

响应 `LoginRsp`：`error:int32`、`uid:int32`、`token:string`。

状态：服务端已实现 Redis token 校验，但 ChatServer 当前绕过此 RPC，直接读取 Redis。

### 7.5 `ChatService`

协议中定义：

| RPC | 请求 | 响应 | 当前运行状态 |
| --- | --- | --- | --- |
| `NotifyAddFriend` | `AddFriendReq` | `AddFriendRsp` | 客户端调用及对端 Session 推送实现存在；对端离线仍可返回 OK，不等于客户端已收到 |
| `ReplyAddFriend` | `ReplyFriendReq` | `ReplyFriendRsp` | 当前 `ChatServiceImpl` 未覆盖，按生成基类行为返回未实现 |
| `SendChatMsg` | `SendChatMsgReq` | `SendChatMsgRsp` | 当前 `ChatServiceImpl` 未覆盖，按生成基类行为返回未实现 |
| `NotifyAuthFriend` | `AuthFriendReq` | `AuthFriendRsp` | 接收端已填充响应并推送 1014；两个 RPC 客户端仍为空实现，跨节点链路未闭合 |
| `NotifyTextChatMsg` | `TextChatMsgReq` | `TextChatMsgRsp` | 服务端覆盖方法但仅返回 OK，不填充响应；客户端空实现 |

消息字段：

| Message | 字段 |
| --- | --- |
| `AddFriendReq` | `applyuid:int32(1)`, `name:string(2)`, `desc:string(3)`, `touid:int32(4)`, `apply_id:int64(5)`, `icon:string(6)`, `nick:string(7)`, `gender:int32(8)`；desc 在此用于申请附言 |
| `AddFriendRsp` | `error:int32`, `applyuid:int32`, `touid:int32` |
| `ReplyFriendReq` | `replyuid:int32`, `agree:bool`, `touid:int32` |
| `ReplyFriendRsp` | `error:int32`, `replyuid:int32`, `touid:int32` |
| `SendChatMsgReq` | `fromuid:int32`, `touid:int32`, `message:string` |
| `SendChatMsgRsp` | `error:int32`, `fromuid:int32`, `touid:int32` |
| `AuthFriendReq` | `fromuid:int32(1)`, `touid:int32(2)`, `apply_id:int64(3)`, `agree:bool(4)`；fromuid 是审核者，touid 是申请人 |
| `AuthFriendRsp` | `error:int32`, `fromuid:int32`, `touid:int32` |
| `TextChatData` | `msgid:string`, `msgcontent:string` |
| `TextChatMsgReq` | `fromuid:int32`, `touid:int32`, `textmsgs:repeated TextChatData` |
| `TextChatMsgRsp` | `error:int32`, `fromuid:int32`, `touid:int32`, `textmsgs:repeated TextChatData` |

### 7.6 协议源漂移

当前存在以下不一致：

- `Server/Common/message.proto` 只包含 VarifyService 和 StatusService。
- `Server/VarifyServer/message.proto` 还包含 ChatService 及全部聊天消息。
- `Server/Common/include/message*.pb.h` 的生成代码包含 ChatService。
- GateServer 目录还存在另一份 `message.proto`。

当前应以完整的 `Server/VarifyServer/message.proto` 维护协议，Node 的 proto.js 也加载它。从 VarifyServer 目录运行 start.bat 只生成到当前目录，不会同步 Common；C++ 使用 Common/include 和 Common/src 的四个生成文件，需一起同步。不要用旧 Common/message.proto 重新生成并覆盖 ChatService。统一构建生成入口仍是待办，旧副本尚未删除。

## 8. QML/C++ 桥接接口

### 8.1 控制器

| 类型 | QML 可调用方法 | 结果信号 |
| --- | --- | --- |
| `RegisterController` | `getVerifyCode(email)`、`registerUser(username,email,varifyCode,password,confirm)` | `verifyCodeResult(success,message)`、`registerResult(success,message)` |
| `ResetController` | `getVerifyCode(email)`、`resetPassword(user,email,password,verifyCode)` | `verifyCodeResult(success,message)`、`resetResult(success,message)` |
| `LoginController` | `loginUser(userData)`，其中 `userData` 需要 `email/passwd` | `loginResult(success,error,message,user)` |

### 8.2 `TcpMgr`

`TcpMgr` 是 QML 单例，并提供：

| 接口 | 方向 | 说明 |
| --- | --- | --- |
| `slot_tcp_connect(ServerInfo)` | 控制器 → TcpMgr | 连接 ChatServer |
| `slot_send_data(ReqId, QString)` | 内部/控制器 → TcpMgr | 按 TCP 帧格式发送 JSON |
| `sig_con_success(bool)` | TcpMgr → 控制器 | TCP 建连结果；当前只在 connected 时发 true，错误时不发 false |
| `sig_switch_chatlg()` | TcpMgr → QML | 二次登录成功后切换聊天界面 |
| `sig_login_failed(int)` | TcpMgr → 控制器 | 二次登录失败 |
| searchUser(QString) | QML → TcpMgr | 发送 1007；searchPending 防重复请求 |
| applyFriend(int,QString,QString) | QML → TcpMgr | 发送 1009；applyPending |
| resolveFriendApply(qint64,bool) | QML → TcpMgr | 发送 1012；reviewPending |
| sig_user_search(QVariantList) | TcpMgr → QML | 搜索结果；is_friend 转为 isFriend |
| sig_search_failed(int,QString) | TcpMgr → QML | 查询错误 |
| sig_friend_apply_result(int error,int result,qint64 applyId) | TcpMgr → QML | 提交结果，三个参数不可省略 |
| sig_friend_apply(QVariantMap) | TcpMgr → QML | 新申请；模型字段 applyId/uid/name/head/message/status |
| sig_friend_apply_resolved(int error,int result,qint64 applyId,bool agree) | TcpMgr → QML | 审核回包，成功后才更新状态 |
| sig_friend_auth_notified(qint64 applyId,bool agree,int peerUid) | TcpMgr → QML | 审核通知；目前 QML 未消费 |
| friendApplySnapshot | TcpMgr → QML | QVariantList 属性；登录回包保存后发 friendApplySnapshotChanged |

### 8.3 QML 数据模型方法

| 类型 | QML 方法 | 说明 |
| --- | --- | --- |
| `ChatUserList` | `addItem`、`clear`、`loadMoreItems`、`isLoading` | 最近会话列表；当前主要为演示数据 |
| `ContactUserList` | `addItem`、`clear` | 联系人列表 |
| `ApplyFriendList` | `upsertItem(QVariantMap)`、`setStatus(qint64,int)`、`replaceAll(QVariantList)`、`clear()` | 按 applyId 去重，提供 pendingCount 属性；clear 当前缺少数量变化通知 |
| `ApplyFriendModel` | `addTag`、`removeTag`、`toggleTag`、`confirmApply`、`cancelApply`、`initDemoTags` | 好友申请标签和附言的本地交互模型 |

这些接口是进程内 UI 契约，不是服务端 API。申请 Model 角色为 applyId/uid/name/head/message/status，旧 isAdded 已移除。登录快照不会随实时申请或审核回写；resetBusinessPending 虽有定义，尚未在断线/错误回调接入，不能宣称自动恢复。

## 9. 接口安全与可靠性要求

当前接口用于开发验证，生产化前必须完成：

1. HTTP 改为 HTTPS，TCP 启用 TLS，内部 gRPC 启用 mTLS 或等价身份认证。
2. 密码仅使用强哈希校验，禁止任何响应、日志和缓存返回明文密码。
3. 验证码使用限流、尝试次数、哈希存储和一次性消费。
4. token 增加短 TTL、用途/设备绑定、撤销和登录成功后的轮换或消费。
5. 所有接口增加字段存在性、类型、长度、枚举和字符集校验。
6. gRPC 调用设置 deadline；HTTP 客户端设置超时、取消和有限重试。
7. 统一错误包络和服务端错误码，不向客户端暴露内部异常细节。
8. 增加 request ID、结构化脱敏日志、指标和链路追踪上下文。
9. TCP 增加协议版本、心跳、ACK、消息 ID、幂等和断线恢复。
10. 对消息长度、队列深度和并发连接实施明确背压策略。

## 10. 接口变更规则

- HTTP 不兼容变更必须发布新版本路径，例如 `/api/v2`。
- Protobuf 已发布字段号不得复用；删除字段使用 `reserved`。
- TCP 消息 ID 一经发布不得改变语义，应建立集中注册表。
- 新字段默认应向后兼容，接收方必须忽略未知字段。
- 每次发布应记录新增、变更、废弃接口及兼容窗口。
- 协议定义、生成代码、接口文档和契约测试必须在同一变更中更新。

## 11. 待确认事项

- HTTP API 是否对互联网开放，以及正式域名和 TLS 终止位置。
- 用户名、邮箱、密码和好友附言的正式长度与字符集规则。
- token 有效期、单设备/多设备登录策略和踢下线规则。
- 联系人、会话、消息、已读回执和分页同步的正式消息 ID；1007～1014 已用于搜索、申请与审核。
- ChatService 是仅用于节点间推送，还是同时承担消息持久化编排。
- 统一错误码、API 版本及客户端最低兼容版本策略。
