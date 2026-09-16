# SakuraChat 数据库

本目录包含 MySQL 8.0 基础结构和增量升级脚本。原始 `llfc.sql`、`llfc2.sql` 导出含测试数据，仅保留在工作区 `.local-backups/legacy-database/`，不分发。

以下命令路径以服务端仓库为工作目录；重定向示例使用支持 `<` 的命令行，而非 PowerShell。已有数据库先备份，按功能文档检查并应用 `002` 至 `006` 对应升级脚本，不能重新运行 `schema.sql`。隐私聊天见 [部署说明](PRIVATE_CHAT.md)。

## 文件说明

| 文件 | 用途 |
| --- | --- |
| `schema.sql` | 直接重建项目现有的 `skrchat` 数据库表、索引、外键和存储过程；不包含测试用户 |
| `migrate_from_llfc2.sql` | 将已导入 `llfc` 数据库的 llfc2 数据迁移到 `skrchat`；不删除源数据 |
| `002` 至 `006` 编号 SQL | 聊天历史、隐私、删除、保留期限和 Signal 密文存储升级 |

## 主要改进

- 保留当前 C++ DAO 依赖的 `user`、`user_id` 和 `reg_user` 契约。
- 用户名和邮箱使用唯一约束，UID 分配改为行锁保护，避免并发注册重复。
- 好友和申请增加外键、状态约束、收件箱索引及审计时间。
- 私聊用户对固定按 UID 排序，从数据库层阻止重复会话。
- 消息增加会话内序号、客户端幂等键、消息类型和逐用户回执。
- 群成员增加角色、状态、禁言和最后已读序号。
- 增加附件元数据和 Outbox 表，为可靠跨节点投递预留基础。
- 统一 `utf8mb4_0900_ai_ci`、毫秒时间戳和 UTC 时区。
- 删除原始 dump 中的测试数据初始化，避免继续传播邮箱和明文密码。

## 全新安装

`schema.sql` 会先执行 `DROP DATABASE IF EXISTS skrchat`，然后完整重建 `skrchat`。它会删除旧库中的全部表和数据，只能用于已经备份且确认可以直接替换的环境。数据库名与当前各服务 `config.ini` 保持一致，因此无需修改项目配置。

```powershell
mysql --host=<host> --port=<port> --user=<user> -p < database/schema.sql
```

各服务现有 `config.ini` 已使用以下配置，无需修改：

```ini
[MySQL]
Schema=skrchat
```

不要把数据库、Redis 或 SMTP 密码写入提交到版本库的配置文件。

## 从 llfc2 迁移

迁移脚本不会删除 `llfc` 中的任何表，但正式执行前仍必须备份。

1. 在隔离环境导入历史 dump：

```powershell
mysql --host=<host> --port=<port> --user=<user> -p -e "CREATE DATABASE IF NOT EXISTS llfc CHARACTER SET utf8mb4"
mysql --host=<host> --port=<port> --user=<user> -p llfc < ../.local-backups/legacy-database/llfc2.sql
```

2. 创建新库：

```powershell
mysql --host=<host> --port=<port> --user=<user> -p < database/schema.sql
```

3. 迁移数据：

```powershell
mysql --host=<host> --port=<port> --user=<user> -p < database/migrate_from_llfc2.sql
```

4. 检查脚本末尾输出的用户、好友、会话和消息数量，并执行业务抽样验证。
5. 备份新库，修改应用 Schema 配置，再进行灰度切换。

## 密码迁移限制

历史 dump 的 `user.pwd` 是明文或不可确认格式。旧迁移脚本原样复制该列，不能使旧账号满足当前认证要求。

正确升级方式是：

1. 当前注册和重置流程使用 Argon2id。
2. 新密码直接写入哈希，不保留明文。
3. 旧账号通过邮箱重置密码升级，不提供明文密码登录回退。
4. 升级后清理日志、Redis 用户缓存及备份中的明文密码暴露面。

仅修改 SQL、直接对未知明文批量哈希会使当前应用无法登录，因此本次没有擅自执行。

## 存储过程

| 过程 | 作用 | 兼容性 |
| --- | --- | --- |
| `reg_user` | 并发安全地分配 UID 并注册用户 | 保持当前 DAO 的四参数和结果语义 |
| `apply_friend` | 新建或重新发起好友申请 | MysqlDao::AddFriendApply 已调用，并按方向唯一键查回 applyId |
| `resolve_friend_apply` | 同意/拒绝申请；同意时原子写入双向好友关系 | MysqlDao::ResolveFriendApply 已调用；成功后才使用双方 UID 通知 |
| `create_private_chat` | 取得或创建唯一私聊会话 | 为消息链路提供 |
| `create_chat_message` | 分配会话序号、幂等写消息并产生 Outbox 事件 | 为可靠推送提供 |

好友相关两个过程已接入 Common 的共享 DAO；create_private_chat、create_chat_message 仍未接入真实消息业务。GetPendingFriendApplies 直接查询 friend_apply JOIN user，按 to_uid/status=0/id>afterId 过滤，登录时最多返回 200 条，没有后续分页协议。

## 上线前检查

- 使用 MySQL 8.0.27 或更高版本。
- 在数据库副本完整演练 schema、迁移、回滚和恢复。
- 确认应用账户对 `skrchat` 只有所需权限。
- 确认所有用户、好友和消息数量与源库一致或差异可解释。
- 检查孤儿好友、孤儿群成员、无效消息发送者和重复邮箱。
- 完成密码哈希、TLS、日志脱敏和 Redis 缓存清理改造。
- 为 `chat_outbox` 实现消费者后再依赖其进行跨节点投递。
- 建立自动备份、binlog、恢复演练和 schema 版本管理。

## 与当前代码的字段映射

| C++ / 协议字段 | 数据库字段 |
| --- | --- |
| `UserInfo.uid` | `user.uid` |
| `UserInfo.name` | `user.name` |
| `UserInfo.email` | `user.email` |
| `UserInfo.pwd` | `user.pwd` |
| `UserInfo.nick` | `user.nick` |
| `UserInfo.desc` | `user.desc` |
| `UserInfo.gender` | `user.gender` |
| `UserInfo.icon` | `user.icon` |
| applyId / JSON apply_id | friend_apply.id |
| PendingFriendApplyInfo.descs / JSON message | friend_apply.descs |
| PendingFriendApplyInfo.status / QML status | friend_apply.status |
| `TextChatData.msgid`（目标映射，尚未接入） | `chat_message.message_id` 转为字符串 |
| `TextChatData.msgcontent`（目标映射，尚未接入） | `chat_message.content` |

旧 dump 中的 `user.sex` 在迁移时映射到 `user.gender`；`friend.self_id/friend_id/back` 映射到 `friend.self_uid/friend_uid/remark`；`group_chat_member.user_id` 映射到 `user_uid`。
