# 🚀 SakuraChat Server Backend

![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)
![Boost.Asio](https://img.shields.io/badge/Boost.Asio-1.88-00599C)
![gRPC](https://img.shields.io/badge/gRPC-latest-4285F4?logo=grpc&logoColor=white)
![Node.js](https://img.shields.io/badge/Node.js-18%2B-339933?logo=node.js&logoColor=white)
![Redis](https://img.shields.io/badge/Redis-6%2B-DC382D?logo=redis&logoColor=white)
![MySQL](https://img.shields.io/badge/MySQL-8.0-4479A1?logo=mysql&logoColor=white)

SakuraChat Server 是 SakuraChat 即时通讯系统的分布式后端微服务系统。基于 **C++20 / Boost.Asio / gRPC** 构建高并发网络层，结合 **Node.js** 验证码服务、**Redis** 高速缓存与 **MySQL** 持久化存储。

---

## 当前好友功能进度（2026-09-08）

服务端基线 40950c6，仅静态核对，未构建、运行测试或执行 SQL。

- Common/MysqlDao 已接入好友申请保存、审核存储过程及待处理申请查询；两个 MysqlMgr 只转发到共享 DAO。
- 登录回包 1006 增加 apply_list，最多 200 条；不是全量分页同步。
- NotifyAddFriend RPC 调用和接收代码存在；NotifyAuthFriend 只有接收端落地，两个 RPC 客户端仍为空。
- ChatServer1 声明并注册 AddFriendApply，但缺少定义；不能据此宣称两个节点都能成功构建或完成申请链路。
- 完整协议位于 VarifyServer/message.proto；Node 直接加载它。start.bat 从当前工作目录生成文件，不自动同步 Common。C++ 构建使用 Common/include 与 Common/src 中的四个生成文件，旧 Common/message.proto 不能直接覆盖生成代码。
- 详细缺口及导航见 [好友功能状态](../docs/FRIEND_FEATURE_STATUS.md)，契约见 [接口文档](../docs/INTERFACE_DESIGN.md)。这些根目录文档不包含在单独的 Server 仓库中。

## 🏗️ 系统微服务架构

```text
               +-----------------------+
               |   SakuraChat Client   |
               +-----------------------+
                 /                   \
        HTTP POST                   TCP (Long-lived)
               /                       \
              v                         v
     +-----------------+       +-------------------+
     |   GateServer    |       | ChatServer1 / 2   |
     |   (Port 8081)   |       | (Port 8090/8091)  |
     +-----------------+       +-------------------+
        /           \                    ^
     gRPC           gRPC                 |
      v               v                  |
+--------------+  +--------------+       |
| VarifyServer |  | StatusServer |-------+ (Redis Token/IP Session)
| (Port 50051) |  | (Port 50052) |
+--------------+  +--------------+
```

### 微服务职责明细

1. **`GateServer` (HTTP 网关服务 - Port: 8081)**
   - 负责客户端 HTTP 接口接入：注册 `/user_reg`、登录 `/user_login`、获取验证码 `/get_varifycode`、重置密码等。
   - 通过 gRPC 分别与 `VarifyServer`（发送邮件验证码）及 `StatusServer`（请求聊天节点与 Token）通信。

2. **`StatusServer` (状态与调度服务 - Port: 50052 - gRPC)**
   - 负载均衡调度：计算各 `ChatServer` 在线连接数（`LOGIN_COUNT`），分配最空闲的聊天服务器。
   - 身份凭证生成：为成功登录的用户生成唯一的 UUID Token 存储至 Redis（`_utoken{uid}`），校验 TCP 登录安全。

3. **`ChatServer1` & `ChatServer2` (TCP 聊天服务器 - Port: 8090/8091 - Boost.Asio)**
   - 高并发异步网络模型：基于 Boost.Asio 异步 IO 线程池及事件循环（`AsioIOServicePool`）。
   - 持续监听处理：`CServer::StartAccept()` 迭代异步接收新套接字连接；`CSession` 负责解析 16-bit Header（ID + Length）并分发逻辑响应。
   - 消息路由与推送：通过 Redis 用户节点映射查找目标节点，同节点直接发送，跨节点调用 gRPC；当前不是 Redis 发布订阅方案。

4. **`VarifyServer` (验证码服务 - Port: 50051 - Node.js gRPC)**
   - 使用 Node.js + Nodemailer 异步发送邮箱验证码，并写入 Redis 进行防刷控制与超时失效。

5. **`Common` (底层公共静态库)**
   - 集中封装 `RedisMgr` (hiredis/redis-plus-plus/sw::redis)、`MysqlDao` / `MysqlMgr` 线程池、`ConfigMgr` (INI解析) 及 Protobuf/gRPC 协议桩文件。

---

## 🔐 完整登录鉴权与连接时序

```text
Client                GateServer            StatusServer            ChatServer
  |-- 1. POST /user_login ->|                    |                      |
  |                         |-- 2. GetChatServer ->|                    |
  |                         |   (Generate Token) |-- Set _utoken{uid} -> [Redis]
  |                         |<- 3. Return (Host,Port,Token) --|         |
  |<- 4. Return JSON -------|                    |                      |
  |                                                                     |
  |---------------------- 5. TCP Connect (Port 8090/8091) ------------->|
  |---------------------- 6. Send ID_CHAT_LOGIN {uid, token} ---------->|
  |                                                                     |-- Get _utoken{uid} -> [Redis]
  |                                                                     |-- Compare Token
  |<--------------------- 7. Return MSG_CHAT_LOGIN_RSP {error: 0} ------|
```

---

## 🛠️ 项目目录结构

```text
Server/
├── CMakeLists.txt              # 全局 CMake 构建配置
├── .gitignore                  # Git 忽略文件（忽略编译中间件及 IDE 配置）
├── GateServer/                 # HTTP 网关服务 (Boost.Beast + Boost.Asio)
├── StatusServer/               # 调度与状态服务 (gRPC)
├── ChatServer1/                # 聊天节点服务 1 (TCP Boost.Asio)
├── ChatServer2/                # 聊天节点服务 2 (TCP Boost.Asio)
├── VarifyServer/               # 邮件验证码服务 (Node.js + gRPC)
└── Common/                     # 共享静态库 (MySQL/Redis 线程池, Config, Proto)
```

---

## ⚙️ 环境依赖与编译

### 前置要求
- **编译器**: GCC 10+ / MSVC 2019+ / Clang 12+ (支持 C++20)
- **依赖库**:
  - Boost 1.80+ (Beast, Asio, System, Filesystem)
  - gRPC & Protocol Buffers (v1.50+)
  - MySQL Server 8.0+ & MySQL Connector/C++
  - Redis 6.0+ & hiredis
  - Node.js 18+ (用于 VarifyServer)

### 编译步骤 (Linux / Windows MinGW / MSVC)

```bash
# 1. 克隆代码仓库
git clone https://github.com/yourusername/Server.git
cd Server

# 2. 安装 Node.js 依赖 (VarifyServer)
cd VarifyServer && npm install && cd ..

# 3. 创建 CMake 构建目录并编译 C++ 服务
mkdir build && cd build
cmake ..
cmake --build . --config Debug -j4
```

---

## 📝 配置文件说明 (`config.ini`)

每个微服务目录下包含各自的 `config.ini`。例如 `StatusServer/config.ini`：

```ini
[GateServer]
Port = 8081

[StatusServer]
Host = 127.0.0.1
Port = 50052

[MySQL]
Host = 127.0.0.1
Port = 3306
User = root
Password = your_password
Schema = skrchat

[Redis]
Host = 127.0.0.1
Port = 6379
Password = your_redis_password

[ChatServers]
Name = ChatServer1,ChatServer2

[ChatServer1]
Name = ChatServer1
Host = 127.0.0.1
Port = 8090

[ChatServer2]
Name = ChatServer2
Host = 127.0.0.1
Port = 8091
```

---

## 📄 开源协议

本项目采用 [MIT License](LICENSE) 开源协议。
