# 隐私对话服务端部署

先备份并选择目标数据库，再手动执行 `006_private_chat.sql`。依赖已有的 `user`、`friend` 和 `user_block` 表。程序不自动迁移生产库。

## 接口

`POST /private/v1`，JSON 请求上限 96 KiB。所有操作携带现有登录账号的 `uid`、`token`，以及 UUID `request_id` 和本设备公开 `identity` 字节数组。服务端验证令牌并绑定操作者，不接受客户端指定任意发送者。

| op | 额外字段 | 作用 |
| --- | --- | --- |
| register | 无 | 注册单设备身份；同身份重试成功，不覆盖不同身份 |
| publish | bundle | 发布一个公开预密钥包，相同 ID 只能重试相同内容 |
| identity | peer | 获取好友公开身份，供安全码核对 |
| claim | peer | 事务领取一个一次性预密钥包；重试必须保持 request_id 不变 |
| send | peer、peer_identity、id、kind、ciphertext | 保存密文；ciphertext 为 Base64；kind 为 2 或 3 |
| poll | 无 | 返回一条尚未确认的密文 envelope，无消息则不包含 envelope |
| ack | sequence | 收件人本地解密及事务提交成功后确认，服务端清除密文正文 |

所有响应含 `error`；有效请求的响应携带相同 `request_id`。私钥、协议状态、明文等非协议字段会被拒绝。

错误码：1001 输入错误；1010 登录无效；1101 非好友、拉黑或对象不可用；1104 存储或依赖故障；1201 未注册设备；1202 身份不一致；1203 幂等键冲突；1204 请求限流；1205 无可用预密钥；1207 Redis 限流检查不可用；1208 预密钥存储已满；1209 收件队列已满。客户端传输失败使用本地错误 1206。

1204 响应附带 `retry_after`（秒），来自限流键的剩余有效时间。Redis 连接失败或命令响应异常返回 1207，不混同于超限。客户端在冷却结束后重试原请求；临时连接故障使用最大 60 秒的指数退避，容量错误不自动重试。旧布尔限流接口仍采用失败时拒绝请求的策略。

## 安全与运行边界

- 发布和传输需 HTTPS 入口。GateServer 本身仍是内部 HTTP 服务，应通过可信 TLS 反向代理暴露，不能将内部 HTTP 端口直接开放公网。开发客户端只允许回环地址使用 HTTP。
- 服务器知道通信账号、时间和公开身份，但不知道消息正文。端到端身份真实性仍依赖客户端安全码核验。
- 当前为单设备身份，服务器拒绝不同设备身份覆盖。设备丢失后的重置流程未开放，不能直接覆盖数据库中的身份字段。
- 每账户最多保留 100 条预密钥记录；每收件人最多积压 1000 条未确认密文。耗尽时返回错误，不静默覆盖。
- 确认后的密文正文置空，去重摘要、消息 ID、账号元数据保留。预密钥与队列的自动过期清理尚未提供，正式运行前需完成保留策略和管理流程。
- 好友及拉黑检查适用于身份查询、预密钥领取和发送，收件轮询排除已拉黑双方的消息。
- 事务使用设备行锁串行化预密钥领取及配额检查。死锁/存储故障返回可重试错误，客户端必须复用请求 ID 和已有密文。

## 隔离数据库验证

`privatechat_wire_tests` 不依赖数据库，验证公开字段和长度边界。

`privatechat_database_tests` 使用实际 DAO 和 MySQL，创建随机名称的 `sakura_private_test_*` 数据库，建立最小用户/好友/拉黑表并执行原始 `006_private_chat.sql` 两次。不会复制或修改配置中的业务数据库；结束时仅删除本次成功创建的测试库。需要配置用户具有创建和删除数据库权限。

```powershell
cmake --build cmake-build-debug --target privatechat_database_tests
$env:SAKURA_CONFIG_PATH = '<GateServer config.ini 的绝对路径>'
./cmake-build-debug/Common/privatechat_database_tests.exe --run-isolated
```

测试覆盖身份覆盖拒绝、预密钥发布幂等、相同请求并发领取、一次性密钥耗尽、并发重复发送、消息 ID 冲突、离线密文读取、越权确认无效、确认后重试不重新投递、拉黑/非好友限制以及停用账号拒绝。测试中的密钥和密文是格式正确的固定夹具，只验证服务器存储语义；密码学验证由客户端 libsignal 测试负责。

领取幂等记录、消息去重记录和配额统计使用锁定读，避免 MySQL REPEATABLE READ 的旧快照在等待设备锁后遗漏已提交记录。

该测试还通过随机回环端口发送 HTTP 请求，调用生产 `PrivateChatHttp` 鉴权处理器，再访问隔离 DAO。覆盖预密钥发布/领取、密文重试/轮询/确认、错误令牌、账号冒用、会话失效、限流和异常响应的请求编号保留。令牌摘要查询与限流结果使用进程内受控夹具，不连接真实 Redis；生产入口默认仍连接 Redis 与 MysqlMgr，测试不会写入现有会话或限流键。

这个回环 HTTP 测试使用独立监听器。HTTP 依赖接口允许将鉴权基础设施与请求解析、密文持久化解耦。

## 真实服务端端到端测试

`tools/test-private-chat-e2e.ps1`（PowerShell 7）启动独立 Redis、真实 GateServer、隔离数据库夹具和客户端 `privatechat_e2e_tests`。Redis 仅监听回环、关闭持久化并使用随机密码；网关使用独立端口；本次创建的数据库在测试结束后删除。现有数据库、Redis 会话和服务进程不参与测试。

先构建服务器 `GateServer`、`privatechat_database_tests` 和客户端 `privatechat_e2e_tests`，将桥接 DLL 放到客户端构建目录。然后执行：

```powershell
./tools/test-private-chat-e2e.ps1 `
  -ServerBuild '<服务器构建目录>' `
  -ClientBuild '<客户端构建目录>' `
  -RedisServer '<redis-server.exe 路径>' `
  -QtBin '<Qt 6.8.3 mingw_64/bin>' `
  -ConfigPath '<GateServer/config.ini 绝对路径>'
```

测试使用正式客户端协议引擎与 HTTP 传输模块，覆盖真实 Signal 预密钥生成/领取、安全码匹配、错误账号令牌拒绝、双向加密正文收发、重复提交、确认以及重新打开本地会话后回复。为隔离账号生命周期，夹具直接预置有效期 120 秒的测试会话，不执行注册或登录；也不覆盖 QML 点击交互或 TLS 反向代理部署。

GateServer 从 `[GateServer] Port` 读取监听端口，允许通过 `SAKURA_GATE_PORT` 覆盖；仅接受 1～65535，内部服务仍仅绑定回环地址。
