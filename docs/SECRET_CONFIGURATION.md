# 生产凭据注入与 CLion 配置

## 本轮变化

C++ ConfigMgr 与 Node 验证服务现在支持环境变量覆盖配置。development 模式保留原本机配置兼容；production 模式不回退到配置文件中的数据库、Redis、SMTP 凭据。显式设置为空的覆盖变量会报错，而不是悄悄采用旧值。

GateServer、ChatServer 在开始监听前检查数据库环境凭据；所有 C++ 服务检查 Redis 环境密码。StatusServer 不要求 MySQL 环境凭据。验证码服务不再导出或使用原先多余的 MySQL 配置值。

旧配置文件没有自动改写，仓库历史没有重写，远端凭据没有轮换。删除了网关源码中两个未启用的 Redis 演示函数，其中包含硬编码密码和直接写测试数据的旧逻辑。这不等于撤销已泄露的凭据。

## 每个进程应得到哪些变量

| 变量 | GateServer | ChatServer | StatusServer | VarifyServer |
|---|---|---|---|---|
| SAKURA_SECURITY_MODE=production | 必需 | 必需 | 必需 | 必需 |
| SAKURA_REDIS_PASSWORD | 必需 | 必需 | 必需 | 必需 |
| SAKURA_MYSQL_USER / SAKURA_MYSQL_PASSWORD | 必需 | 必需 | 不提供 | 不提供 |
| SAKURA_SMTP_USER / SAKURA_SMTP_PASSWORD | 不提供 | 不提供 | 不提供 | 必需 |
| SAKURA_VERIFY_SERVICE_KEY | 必需 | 不提供 | 不提供 | 必需且与网关相同 |
| SAKURA_RPC_CA / SAKURA_RPC_CERT / SAKURA_RPC_KEY | 必需 | 必需 | 必需 | 必需 |

TLS 变量和服务证书角色规则沿用 [安全实施记录](SECURITY_IMPLEMENTATION.md)，不是有密码就可以省略证书。

可选地址覆盖：SAKURA_REDIS_HOST、SAKURA_REDIS_PORT；Gate/Chat 另支持 SAKURA_MYSQL_HOST、SAKURA_MYSQL_PORT、SAKURA_MYSQL_SCHEMA。Node 支持 SAKURA_SMTP_HOST，当前固定 SMTPS 465，校验证书且最低 TLS 1.2，未加入明文 SMTP 模式。

SAKURA_CONFIG_PATH 可指定本进程配置文件的完整路径；C++ 读取 INI，Node 读取 JSON。不设置时仍从工作目录读取 config.ini/config.json。配置文件可以保留地址等非敏感信息，但生产流程应逐步移除其中实际密码。

## CLion 一次配置、以后点击运行

1. 打开 Run → Edit Configurations，分别选择 GateServer、ChatServer1、ChatServer2 和 StatusServer 配置。
2. 保持原 Target；两个聊天实例仍使用同一个 ChatServer Target，各自使用原 Working directory，或分别设置 SAKURA_CONFIG_PATH 指向其 INI。
3. 在各自 Environment variables 中填入上表本进程需要的变量。不要把真实值放到程序参数、CMake 编译参数或共享运行配置里；不要把编辑器截图发到公开渠道。
4. 验证服务的 Node 运行配置同样设置环境变量，Working directory 指向 VarifyServer，脚本为 server.js。
5. 保存为本机运行配置，不勾选把含凭据配置存入项目/共享给 Git。以后启动仍可直接点击运行。

本轮没有替你设置系统环境变量或 IDE 配置。环境变量不是秘密管理器：同权限调试、进程转储和父子进程继承仍可能暴露值。正式部署应由受控服务管理器或秘密管理服务注入，限制配置读取权限，并为服务分配最小权限账号。

## 日志、超时与遗留风险

- 配置日志只显示字段名，不打印环境覆盖值；INI/JSON 解析失败只报告固定错误，不回显配置内容。
- SMTP 连接/问候上限 10 秒、socket 上限 15 秒；不输出 SMTP 响应或原始错误对象。
- Node Redis 连接和命令超时 5 秒，不排队积累离线命令；错误时允许驱动重连，不输出可能携带命令参数的错误对象。
- .gitignore 增加 .env、secrets/、*.local.ini、*.local.json 等规则，但不能保护已经被 Git 跟踪的文件，也不会自动加载 .env。
- 既有配置和 Git 历史中的真实凭据仍需轮换。不要先删掉唯一可用配置再准备新凭据；轮换、验证、移除旧值应分步骤进行。
- 数据库/Redis 的跨机器传输 TLS、ACL 用户支持、凭据自动轮换和集中秘密管理仍未完整实施。环境注入不能替代传输加密与网络隔离。
- 本轮只做 C++ 编译与 Node --check 语法检查，没有启动服务、连接数据库/Redis、发送邮件或验证生产启动失败路径。
