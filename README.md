# SakuraChat Server

SakuraChat 的 C++ 服务端与 Node.js 验证码服务。客户端仓库：[SakuraChat](https://github.com/shimamuraDS/SakuraChat)。

## 目录

- `Common/`：共享协议、数据库访问、Redis、配置及基础组件。
- `GateServer/`：HTTP 账号接口与隐私聊天密文转发接口。
- `ChatServer/`：云端聊天服务程序；多个实例使用 `configs/chat1/`、`configs/chat2/` 的独立配置。
- `StatusServer/`：聊天节点分配；`VarifyServer/`：验证码服务。
- `database/`：初始化与增量升级 SQL，执行前阅读 [数据库说明](database/README.md)。
- `docs/`：部署与安全文档；`docs/project/`：跨端设计和学习教程。
- `tests/`、`tools/`：测试代码及运行脚本；`deploy/`：部署辅助配置。
- `cmake-build-debug/`：CLion 本地构建输出，不提交。

## 构建与运行

使用 CLion 打开根 `CMakeLists.txt`，配置 Boost、gRPC / Protobuf、MySQL Connector/C++、hiredis、OpenSSL、libsodium 等项目依赖。保留各服务独立运行配置；聊天服务只构建一个程序，通过不同工作目录和配置运行多个实例。

不要把数据库、Redis、SMTP 密码或证书私钥提交到仓库。生产凭据注入和配置要求见 [配置说明](docs/SECRET_CONFIGURATION.md)。不执行数据库测试脚本到生产库；端到端与数据库测试需要隔离的测试环境。

## 文档

- [安全实现](docs/SECURITY_IMPLEMENTATION.md)
- [聊天记录与已读回执](docs/CHAT_HISTORY_READ_RECEIPTS.md)
- [消息删除](docs/MESSAGE_DELETION.md) · [自动删除](docs/AUTO_DELETE.md)
- [隐私聊天数据库与部署](database/PRIVATE_CHAT.md)
- [跨端设计与教程索引](docs/project/README.md)

`database/schema.sql` 会删除并重建数据库，只能用于已确认可重建的环境；现有数据库按对应增量脚本升级，先备份并检查兼容性。
