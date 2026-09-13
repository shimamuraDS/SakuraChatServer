# 文本消息持久化与回执（协议版本 2）

## 部署前提

- 本功能用于一对一文本聊天。两个节点运行同一份 ChatServer，使用不同的节点配置，但连接同一个 MySQL 数据库。
- 客户端和聊天节点必须一起升级。登录成功回包包含 `chat_protocol_version: 2`；新版客户端拒绝旧聊天协议，避免把“转发尝试”当成“已保存”。
- 需要 `chat_thread`、`private_chat`、`chat_message`、`message_receipt` 四张表，结构与项目根目录原有 database/schema.sql 一致。
- 如果没有这些表，先备份并选择正确数据库，再手工运行本仓库 `database/002_chat_history.sql`。它只创建缺少的表，不删除或覆盖数据。已有同名但不同结构的表不会自动升级，需要逐一核对字段、索引、外键。
- 不要重新执行根目录完整 schema.sql 来升级；它包含 DROP TABLE。本次开发未连接或修改运行中的数据库。

## 保存与幂等

`Common/src/ChatPersistence.cpp` 实现 DAO；ChatServer/MysqlMgr 只转发。新发送路径不调用旧 create_chat_message 过程，也不向尚无消费者的 chat_outbox 插入记录。

1. 校验会话身份、UUID、正文、序列化长度。
2. `(sender_uid, client_msg_id)` 已存在时核对接收人及正文，返回原记录。
3. 验证好友与账号状态，在事务中创建/查找 private_chat，锁住 chat_thread 行分配 seq。
4. 插入消息并更新 last_seq、last_message_id，然后提交。唯一键竞争、死锁、锁等待超时最多重试三次。
5. 提交成功即回复；接收人离线、Redis 路由失效、RPC 转发失败都不会把已保存消息改成发送失败。
6. 在线通知是加速通道；客户端定期扫描会话、按 seq 补拉，恢复漏通知或离线消息。未实现事务 outbox 消费器，不声称推送本身可靠。

数据库提交结果不确定时返回 1104，客户端保留 unknown 并查询，不当成确定失败。所有新 SQL 使用参数绑定。消息主键和序号在 JSON 中使用十进制字符串。

## 协议

既有 1015/1016 仍为发送请求/回包，1017 为通知。发送请求仍为 `{touid,text_array:[{msgid,content}]}`。

新消息对象字段：`msgid, message_id, thread_id, seq, fromuid, touid, content, message_status, created_at_ms, state`。发送回包在消息对象上加 error 并省略 content；1017 为 `{error:0,message:{...}}`。`message_status` 是正常/撤回/删除，不是送达状态。

| 请求/响应 | 用途 | 请求参数（另带 UUID request_id） |
| --- | --- | --- |
| 1020/1021 | 顺序补拉历史 | peer_uid、after_seq 字符串 |
| 1022/1023 | 接收方回执 | message_id 字符串、receipt=delivered/read |
| 1024/1025 | 发送方查询状态 | msgids 数组，最多 4 个 |
| 1026/1027 | 会话列表分页 | after_thread 字符串 |

历史响应为 `{error,request_id,peer_uid,after_seq,next_seq,has_more,messages:[]}`，按 seq 升序。与之前说明中的“最近页/before_seq”设计不同，本版先顺序补齐本地 SQLite，再由本地分页展示更早记录，游标只有在当前页全部保存后才前进。

会话响应为 `{error,request_id,after_thread,next_thread,has_more,items:[{thread_id,peer_uid,last_seq}]}`。会话索引每轮从零扫描，避免把自增主键误当全局事务提交顺序。会话内 seq 在同一行锁事务中分配；实时通知不会提升同步游标。

回执响应为 `{error,request_id,message_id,state}`，state 来自数据库合并结果。状态查询响应为 `{error,request_id,items:[{msgid,message_id,...,state}]}`，不带正文；不存在返回 `{msgid,state:'not_found'}`。not_found 不代表请求不可能随后提交。

所有响应不超过 1800 字节。历史每页最多 20 条，还会按序列化字节数提前结束。历史中超过协议上限的旧记录返回占位文本，原文仍在 MySQL，不会因为某一条旧消息过大卡住分页。

## 回执与授权

- 没有 receipt：accepted；receipt.status=0：delivered；receipt.status=1：read。
- 只有 `session->GetUserId() == chat_message.receiver_uid` 能提交回执。
- 重复/乱序回执使用 GREATEST 合并，read 不会退回 delivered。
- 客户端收到并保存消息后排队送达回执，消息前台可见停留 800ms 后排队已读回执；仅入库或收到 RPC 不算已读。
- 发送者通过 1024 查询自己的消息状态，前台约每两秒一批四条；不是实时 RPC 已读推送。大量待确认消息会增加轮询延迟。
- 这是按账号的一对一回执，不是多设备逐设备送达，也不证明用户实际理解了消息。

## 已知边界与后续

- MySQL 连接池、聊天业务队列仍沿用现有实现；本次未重做整个线程/连接池生命周期。
- 本地未缓存大量历史时，从头补齐需时间；内存默认只展示最近 200 条，可继续加载。
- 暂无图片/文件/群聊持久化、撤回操作、端到端加密、多设备阅读同步、隐私开关、消息清理和 outbox 后台消费者。
- 被旧内存实现清除且从未入库的历史无法恢复。
- 完成编译检查，不代表数据库和双端运行流程已验证；按用户要求没有运行测试或应用程序。
