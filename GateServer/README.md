```markdown
# HTTP 服务器项目

基于 Boost.Beast 构建的 C++ HTTP 服务器，支持 GET 和 POST 请求处理，具备 JSON 数据解析功能。

## 功能特性

- **HTTP 请求处理**：支持 GET 和 POST 请求
- **路由系统**：基于 URL 路径的请求路由
- **JSON 支持**：内置 JSON 数据解析和响应
- **参数处理**：支持 GET 请求参数解析
- **异步处理**：基于 Boost.Beast 的异步 I/O

## 依赖项

- **Boost.Beast**：HTTP 和 WebSocket 库
- **Boost.Asio**：异步 I/O 库
- **JsonCpp**：JSON 解析库
- **C++标准**：C++11 或更高版本

## 项目结构

```
src/
├── LogicSystem.h          # 逻辑系统头文件
├── LogicSystem.cpp        # 逻辑系统实现
├── HttpConnection.h       # HTTP 连接头文件
├── HttpConnection.cpp     # HTTP 连接实现
└── main.cpp              # 主程序入口
```

## API 端点

### GET 请求

- **`/get_test`**
  - 描述：测试 GET 请求处理
  - 响应：返回请求参数信息

### POST 请求

- **`/get_varifycode`**
  - 描述：获取验证码
  - 请求体：JSON 格式，需包含 `email` 字段
  - 响应：JSON 格式的验证结果

## 使用示例

### GET 请求示例
```bash
curl "http://localhost:8080/get_test?param1=value1&param2=value2"
```

### POST 请求示例
```bash
curl -X POST http://localhost:8080/get_varifycode \
  -H "Content-Type: application/json" \
  -d '{"email": "user@example.com"}'
```

## 编译说明

确保已安装所需依赖项，然后使用 CMake 或您的首选构建系统：

```bash
# 使用 CMake 示例
mkdir build
cd build
cmake ..
make
```

## 错误处理

项目包含完善的错误处理机制：
- JSON 解析错误处理
- HTTP 连接错误处理
- 参数验证错误处理

## 注意事项

- 确保 Boost 库版本兼容（推荐 1.75 或更高版本）
- 项目采用异步处理模式，适合高并发场景
- 所有 JSON 响应都包含错误码字段用于状态判断
```

这个 README 文档涵盖了项目的主要功能、使用方法和技术细节，方便其他开发者快速了解和使用你的 HTTP 服务器项目。