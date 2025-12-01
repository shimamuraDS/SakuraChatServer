# GateServer 网关服务器接口文档

## 项目概述

GateServer 是一个基于 C++ 开发的网关服务器，使用 Boost.Beast 处理 HTTP 请求，集成了 Redis 缓存、MySQL 数据库和 gRPC 客户端，主要负责用户认证、请求转发等功能。

## 服务配置

### 服务端口
- **HTTP 服务端口**: 8081
- **验证服务端口**: 50051 (gRPC)

### 依赖服务
- **Redis**: 127.0.0.1:6379
- **MySQL**: 127.0.0.1:3306
- **验证服务**: 127.0.0.1:50051

## API 接口

### 1. 用户注册

#### 接口信息
- **URL**: `/user_register`
- **方法**: POST
- **Content-Type**: application/json

#### 请求参数
```json
{
    "user": "用户名",
    "email": "邮箱地址", 
    "passwd": "密码"
}
```

#### 响应格式
```json
{
    "error": 0,          // 错误码：0-成功，其他-失败
    "uid": 12345,        // 用户ID（成功时返回）
    "email": "user@example.com",  // 用户邮箱
    "user": "username",  // 用户名
    "passwd": "password", // 密码
    "confirm": "验证码",  // 验证码（如需要）
    "varify": "验证结果"  // 验证状态
}
```

#### 错误码说明
- **0**: 注册成功
- **-1**: 服务器内部错误
- **1**: 用户名或邮箱已存在
- **2**: 参数格式错误
- **3**: 验证服务失败

### 2. 获取验证码

#### 接口信息
- **URL**: `/get_varifycode`
- **方法**: GET/POST
- **Content-Type**: application/json

#### 请求参数
```json
{
    "email": "用户邮箱地址"
}
```

#### 响应格式
```json
{
    "error": 0,           // 错误码
    "email": "user@example.com",  // 邮箱地址
    "code": "123456"      // 验证码
}
```

### 3. 重置密码

#### 接口信息
- **URL**: `/reset_pwd`
- **方法**: POST
- **Content-Type**: application/json

#### 请求参数
```json
{
    "user": "用户名",
    "email": "邮箱地址",
    "passwd": "新密码",
    "varifycode": "验证码"
}
```

#### 响应格式
```json
{
    "error": 0,           // 错误码
    "email": "user@example.com",
    "user": "username",
    "passwd": "new_password",
    "varifycode": "123456"
}
```

### 4. 用户登录

#### 接口信息
- **URL**: `/user_login`
- **方法**: POST
- **Content-Type**: application/json

#### 请求参数
```json
{
    "email": "邮箱地址",
    "passwd": "密码"
}
```

#### 响应格式
```json
{
    "error": 0,           // 错误码
    "uid": 12345,         // 用户ID
    "email": "user@example.com",
    "token": "jwt_token_string"  // 登录令牌
}
```

## 数据库接口

### 用户注册存储过程

#### 存储过程名称
`reg_user`

#### 参数说明
- **IN new_name**: 用户名
- **IN new_email**: 邮箱地址
- **IN new_pwd**: 密码
- **OUT result**: 返回结果

#### 返回值说明
- **> 0**: 成功，返回新用户的 UID
- **0**: 用户名或邮箱已存在
- **-1**: 数据库操作失败

## Redis 缓存接口

### 支持的操作
- **SET/GET**: 键值对操作
- **HSET/HGET**: 哈希表操作
- **LPUSH/LPOP/RPOP**: 列表操作
- **EXISTS**: 键存在性检查
- **DEL**: 删除键

### 使用示例
```cpp
// 设置键值对
RedisMgr::GetInstance()->Set("key", "value");

// 获取值
std::string value;
RedisMgr::GetInstance()->Get("key", value);

// 哈希操作
RedisMgr::GetInstance()->HSet("hash_key", "field", "value");
std::string hash_value = RedisMgr::GetInstance()->HGet("hash_key", "field");
```

## 错误处理

### HTTP 状态码
- **200**: 请求成功
- **400**: 请求参数错误
- **500**: 服务器内部错误

### 业务错误码
- **0**: 操作成功
- **1**: 参数验证失败
- **2**: 用户不存在或密码错误
- **3**: 验证码错误或过期
- **4**: 用户名或邮箱已存在
- **5**: 服务暂时不可用

## 安全说明

### 数据传输
- 所有 API 支持 HTTPS（需配置 SSL 证书）
- 敏感数据建议加密传输

### 身份验证
- 登录成功后返回 JWT token
- 后续请求需携带 token 进行身份验证

### 数据验证
- 所有输入参数进行格式验证
- 防止 SQL 注入和 XSS 攻击

## 部署说明

### 环境要求
- Windows 操作系统
- Visual Studio 2019+
- CMake 3.20+

### 依赖库
- Boost 1.88.0
- Redis (hiredis)
- MySQL Connector/C++ 9.4.0
- gRPC + Protobuf
- JsonCpp 1.9.6

### 启动命令
```bash
./server
```

服务器将在端口 8081 上启动 HTTP 服务。