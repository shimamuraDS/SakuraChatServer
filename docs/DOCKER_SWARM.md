# Ubuntu / Debian 单机 Swarm 部署与自动更新

## 运行方式

本地提交到 GitHub `master` → Actions 构建 Linux amd64 镜像并运行测试 → 推送 GHCR → 所有镜像成功后发布一个原子 release 清单 → 服务器每五分钟检查清单 → 按顺序更新 verify、status、gate、chat1、chat2。

不使用 Watchtower。更新器直接管理 Swarm service；镜像使用不可变 `sha256` 摘要，不让不同组件各自追踪 `latest`。PR 和非 master 分支仅验证构建，不发布生产清单。

目录：

- `deploy/docker/`：Linux 镜像、运行时配置生成、存活检查。
- `deploy/swarm/stack.yml`：应用、MySQL、Redis、TLS 入口。
- `deploy/swarm/update.py`：检查、顺序更新、失败回退。
- `.github/workflows/server-images.yml`：CI / GHCR 发布。

Windows / CLion 原构建路径不变；容器不读取仓库中的开发账号、密码和 `config.ini`。

### 边界

- 单机不是高可用。机器故障、MySQL/Redis 停机仍会中断整体服务。
- GateServer 两副本采用 start-first；每个 ChatServer 保持单副本并采用 stop-first，避免同名节点覆盖 Redis 注册数据。聊天节点依次更新，其上的 TCP 连接会断开，客户端需重连。**不保证零丢连接。**
- 自动更新只改应用镜像。Nginx、MySQL、Redis、配置、证书、数据库迁移需要人工维护。
- 容器存活检查不等同于邮件可发送、SQL 结构完整或端到端业务正常。首次部署必须执行本文验收。
- 不把 Docker socket 挂进第三方更新容器，不向 GitHub 开放服务器 SSH。主机上的更新器有 Docker 管理权限，脚本必须由 root 管理。
- 公网 HTTPS 和聊天 TLS 在 Nginx 终止；内部 RPC 使用双向 TLS。数据库和 Redis 位于同机私有 overlay 网络，不发布端口。默认云端聊天仍不是端到端加密；隐私对话保持原 Signal 路径。
- Nginx 采用 host 模式发布端口以保留客户端 IP。Gate 仅信任专用 proxy 子网里的 `X-Real-IP`，该头由 Nginx 覆盖，防止共享代理 IP 导致所有用户共用单个 IP 的限流额度。Nginx 单副本自身维护使用 stop-first，会短暂中断入口。

## 1. GitHub 设置与首次构建

1. 将部署代码提交到分支，创建 PR。观察 **Server images** 的镜像编译及测试均成功后再合并 `master`（此仓库实际默认分支）。
2. Actions 使用仓库 `GITHUB_TOKEN` 推送 GHCR，不需要将服务器密码放入 GitHub secrets。组织若禁用了 packages 写入，需管理员允许此工作流。
3. 成功运行的 Summary 显示 `server`、`verify` 的镜像摘要。三个包为 `sakurachatserver-server`、`sakurachatserver-verify`、`sakurachatserver-release`。
4. 公共仓库不意味着 GHCR 包自动公开。可将包设为 public；保持 private 时，在服务器 root 的 Docker 登录中使用具有 `read:packages` 权限的 token。私有镜像更新用 `--with-registry-auth` 将凭证提供给 Swarm。

```bash
sudo -i
docker login ghcr.io -u YOUR_GITHUB_USER
# Password 提示中输入只读 token；不要把 token 写在命令参数或 Git 文件中。
```

仅可信 master 提交可以触发自动部署。建议开启 master 分支保护、PR 审核，限制仓库和 GHCR 写权限。

## 2. 服务器准备

建议至少 2 核 / 4 GB 内存，8 GB 更稳妥，并预留数据库和镜像空间。按 Docker 官方文档安装 Docker Engine、CLI、Buildx：

- Ubuntu：https://docs.docker.com/engine/install/ubuntu/
- Debian：https://docs.docker.com/engine/install/debian/

安装 `git python3 openssl`。以下命令均在服务器执行，不是在 Windows PowerShell 中执行。

```bash
sudo -i
apt-get update
apt-get install -y git python3 openssl
docker version
docker swarm init --advertise-addr YOUR_SERVER_IP
git clone https://github.com/shimamuraDS/SakuraChatServer.git /opt/sakura
cd /opt/sakura
git checkout master
install -d -m 700 /etc/sakura
cp deploy/swarm/deploy.env.example /etc/sakura/deploy.env
chmod 600 /etc/sakura/deploy.env
```

已加入 Swarm 的机器不要再次 init。这里将服务约束在 manager 上，且仅支持一个 manager；不要直接扩成多节点并期待本地数据卷自动迁移。

安全组只放行客户端所需 TCP **443、8090、8091**；SSH 仅放行管理 IP。不公开 3306、6379、50051～50056，也不向公网开放 Swarm 2377/7946/4789。Docker 发布端口可能绕过 UFW，使用云安全组及 Docker 适用的防火墙规则验证。

## 3. 域名、证书和 secrets

准备一个指向服务器的域名，例如 `chat.example.com`，及其可信 CA 签发的 PEM fullchain 与无交互私钥。443 是 HTTPS 网关，8090/8091 是 **TLS TCP，不是 WebSocket**。同一域名可共用证书。

不能把当前测试版的“HTTPS + 明文 TCP”配置直接用于这个部署。客户端必须启用生产 TLS，网关设为 `https://chat.example.com`；状态服务将返回同一域名的 8090/8091。

若还没有证书，先将域名 A 记录指向服务器；在端口 80 未被占用时，可用 Certbot 的 HTTP 验证申请：

```bash
apt-get install -y certbot
# 云安全组/防火墙暂时允许 80/tcp，且 DNS 已正确解析。
certbot certonly --standalone -d chat.example.com
```

若 80 已被其他服务占用，不要盲目停止现有业务，改用对应 DNS 服务商的 DNS 验证方式。Standalone 后续续期同样需要 80 可达；无论采用哪种验证方式，都必须将续期证书更新到 Swarm secrets，不能只依赖 Certbot 更新磁盘文件。

编辑 `/etc/sakura/deploy.env`：填写 `SAKURA_PUBLIC_HOST`、SMTP 主机及 CI Summary 给出的两个完整镜像摘要；不要保留 `REPLACE`。不要写入密码。
`SAKURA_PROXY_SUBNET` 默认 `10.72.20.0/24`，若与服务器已有网络冲突，改为未占用的私有 IPv4 /24；这个变量同时配置 Docker 子网与 Gate 信任范围，不要扩大为全网。

只对**新部署**运行一次初始化：

```bash
python3 /opt/sakura/deploy/swarm/init-secrets.py \
  --public-host chat.example.com \
  --public-cert /etc/letsencrypt/live/chat.example.com/fullchain.pem \
  --public-key /etc/letsencrypt/live/chat.example.com/privkey.pem
```

程序交互询问 SMTP 账号和授权码，生成随机数据库/Redis/服务密钥，创建内部 RPC CA 与五个角色证书，并导入 Swarm secrets。不会打印密码。若已有 Sakura secrets 会拒绝重复初始化；中途失败应检查已创建的具体 secrets 和文件，不能盲目清库重试。

妥善加密备份 `/etc/sakura/secrets`，尤其数据库密码；内部 CA 私钥移至离线安全存储。内部服务证书有效期一年，提前安排续签。公网证书由外部证书工具申请和续期；**Swarm secret 不会跟随原文件自动更新**。续期时创建 `_v2` secret，修改 stack 中对应 `name` 后重新部署，确认成功再移除不再引用的旧 secret。更换 Redis/MySQL密码还需要同步数据库实际账号，不能只替换文件。

## 4. 启动与数据库初始化

```bash
docker volume create sakura_mysql
docker volume create sakura_redis
set -a
. /etc/sakura/deploy.env
set +a
cd /opt/sakura/deploy/swarm
docker stack config -c stack.yml > /tmp/sakura-stack-resolved.yml
docker stack deploy --with-registry-auth --resolve-image always -c stack.yml sakura
docker stack services sakura
docker service logs --tail 80 sakura_mysql
```

首次 MySQL 初始化需要时间；Swarm 不按 `depends_on` 等待依赖，应用允许重启后重试。MySQL/Redis 数据保存在显式命名的外部卷；不要执行 `docker volume prune` 或删除这些卷。

**数据库结构必须单独初始化/迁移，定时器不会代做。**

- 新空库：先审阅 `database/schema.sql`。该文件含破坏性建库语句，**仅能用于确认无数据的新库**。然后按数据库文档检查 `002`～`006` 是否已纳入基线；只执行尚未应用的增量。
- 已有数据库：先备份并验证恢复，再根据 `database/README.md` 进行迁移。不要导入破坏性基线覆盖现有聊天数据。
- 数据库不能只建普通消息表；隐私会话、保留策略和删除同步等表也必须完整。

找到本机 MySQL 容器并打开 SQL 控制台（密码从已挂载 secret 读取，不出现在命令参数中）：

```bash
MYSQL_CONTAINER=$(docker ps --filter label=com.docker.swarm.service.name=sakura_mysql -q)
docker exec -it "$MYSQL_CONTAINER" sh -c \
  'export MYSQL_PWD="$(cat /run/secrets/mysql_root_password)"; exec mysql -uroot skrchat'
```

按审阅后的迁移文件执行 SQL，可先 `docker cp` 对应文件到该容器 `/tmp/`，再于 SQL 控制台使用 `source /tmp/文件名.sql`。不要把账号密码导出到仓库。

## 5. 验收后再启用自动更新

```bash
docker stack services sakura
docker service ps --no-trunc sakura_gate
docker service logs --tail 100 sakura_gate
docker service logs --tail 100 sakura_chat1
curl --fail https://chat.example.com/healthz
openssl s_client -connect chat.example.com:8090 -servername chat.example.com -verify_return_error </dev/null
openssl s_client -connect chat.example.com:8091 -servername chat.example.com -verify_return_error </dev/null
```

用两个生产 TLS 客户端验证：验证码、注册、登录、好友申请、跨 ChatServer 普通消息、历史同步、隐私消息、退出重登。确认日志没有数据库/RPC/TLS错误。`healthz` 只说明 GateServer 存活，不能替代这些检查。

更新器不带 `--apply` 时只拉取并显示已验证镜像，不改运行服务：

```bash
python3 /opt/sakura/deploy/swarm/update.py
# 手动演练一次，确认更新与重连正常：
python3 /opt/sakura/deploy/swarm/update.py --apply
install -m 644 /opt/sakura/deploy/swarm/sakura-update.service /etc/systemd/system/
install -m 644 /opt/sakura/deploy/swarm/sakura-update.timer /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now sakura-update.timer
systemctl list-timers sakura-update.timer
journalctl -u sakura-update.service -n 100 --no-pager
```

之后本地推送并合并 master 即触发构建；镜像全部成功后服务器约五分钟内开始应用。清单标签是 `sakurachatserver-release:stable`，状态文件为 `/var/lib/sakura-updater/state.json`。服务脚本通过文件锁避免并发，拒绝错误仓库/可变 tag，并在修改前检查所有服务标签与聊天节点副本策略。

GHCR 清单只包含镜像，不发布宿主机脚本、证书和 stack 配置。更新这些内容需要人工审核 `git pull --ff-only` 后的差异，再重新部署/安装；修改 Swarm config 时应使用版本化 config 名称，避免原对象不可变导致更新失败。

## 6. 故障、回滚与运维

```bash
# 暂停下一次自动更新；若当前正在执行，先观察它完成，不要直接杀更新器。
systemctl stop sakura-update.timer
systemctl status sakura-update.service
journalctl -u sakura-update.service -n 200 --no-pager
docker service ps --no-trunc sakura_chat1
```

更新失败会尝试按反序恢复已尝试的服务原镜像；Swarm 也配置了单服务失败回滚。相同失败清单会被阻止，不会每五分钟反复破坏现场。修复依赖问题并检查服务状态后才能执行：

```bash
python3 /opt/sakura/deploy/swarm/update.py --apply --retry-failed
```

若回滚也失败，日志列出需人工恢复的服务。暂停 timer，再用已记录的旧摘要明确恢复：

```bash
docker service update --with-registry-auth --detach=false \
  --image ghcr.io/shimamurads/sakurachatserver-server@sha256:KNOWN_GOOD_DIGEST sakura_chat1
```

镜像回滚不能回滚数据库；后续涉及 SQL 的发布必须采用向后兼容迁移并安排维护窗口。基础设施镜像版本升级、数据备份/恢复、证书轮换和磁盘告警由管理员维护。定期检查 `systemctl --failed`、更新日志、证书到期时间与磁盘剩余空间。没有配置外部告警通道时，更新失败不会自动发送邮件。

手动恢复旧版本后不要立即重启 timer；先在 GitHub 发布修复版或明确保持暂停。重新执行 stack deploy 前也要更新 `/etc/sakura/deploy.env` 的摘要，避免把服务降回首次部署版本。

## 参考

- Docker Swarm rolling update：https://docs.docker.com/engine/swarm/swarm-tutorial/rolling-update/
- Docker secrets：https://docs.docker.com/engine/swarm/secrets/
- GitHub 发布容器镜像：https://docs.github.com/en/actions/use-cases-and-examples/publishing-packages/publishing-docker-images
