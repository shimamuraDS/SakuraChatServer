# GateServer 函数文档

## 目录
- [核心服务类](#核心服务类)
- [HTTP连接处理](#http连接处理)
- [逻辑系统](#逻辑系统)
- [数据库操作](#数据库操作)
- [Redis缓存](#redis缓存)
- [gRPC客户端](#grpc客户端)
- [配置管理](#配置管理)
- [工具函数](#工具函数)

---

## 核心服务类

### CServer 类

#### `CServer(net::io_context& ioc, unsigned short port)`
- **功能**: 构造函数，初始化服务器
- **参数**:
    - `ioc`: Boost.Asio IO上下文
    - `port`: 监听端口号
- **返回**: 无

#### `void Start()`
- **功能**: 启动服务器，开始监听连接
- **参数**: 无
- **返回**: 无
- **异常**: 可能抛出网络相关异常

#### `void do_accept()`
- **功能**: 异步接受新的连接请求
- **参数**: 无
- **返回**: 无
- **说明**: 内部循环调用，处理客户端连接

---

## HTTP连接处理

### HttpConnection 类

#### `HttpConnection(tcp::socket socket)`
- **功能**: 构造函数，创建HTTP连接对象
- **参数**:
    - `socket`: TCP套接字对象
- **返回**: 无

#### `void Start()`
- **功能**: 开始处理HTTP连接
- **参数**: 无
- **返回**: 无
- **说明**: 启动异步读取HTTP请求

#### `void CheckDeadline()`
- **功能**: 检查连接超时
- **参数**: 无
- **返回**: 无
- **说明**: 定时器回调函数，处理连接超时

#### `void WriteResponse()`
- **功能**: 向客户端写入HTTP响应
- **参数**: 无
- **返回**: 无
- **说明**: 异步发送响应数据

#### `void HandleReq()`
- **功能**: 处理HTTP请求
- **参数**: 无
- **返回**: 无
- **说明**: 解析请求并调用相应的处理逻辑

#### `unsigned char ToHex(unsigned char x)`
- **功能**: 字符转十六进制
- **参数**:
    - `x`: 输入字符
- **返回**: `unsigned char` - 十六进制值

#### `unsigned char FromHex(unsigned char x)`
- **功能**: 十六进制转字符
- **参数**:
    - `x`: 十六进制值
- **返回**: `unsigned char` - 对应字符

#### `std::string UrlDecode(std::string str)`
- **功能**: URL解码
- **参数**:
    - `str`: 需要解码的字符串
- **返回**: `std::string` - 解码后的字符串

---

## 逻辑系统

### LogicSystem 类

#### `LogicSystem* GetInstance()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `LogicSystem*` - 单例指针

#### `bool HandleGet(std::string path, std::shared_ptr<HttpConnection> con)`
- **功能**: 处理GET请求
- **参数**:
    - `path`: 请求路径
    - `con`: HTTP连接对象指针
- **返回**: `bool` - 处理是否成功

#### `bool HandlePost(std::string path, std::shared_ptr<HttpConnection> con)`
- **功能**: 处理POST请求
- **参数**:
    - `path`: 请求路径
    - `con`: HTTP连接对象指针
- **返回**: `bool` - 处理是否成功

#### `void RegGet(std::string url, HttpHandler handler)`
- **功能**: 注册GET请求处理器
- **参数**:
    - `url`: 请求URL路径
    - `handler`: 处理函数指针
- **返回**: 无

#### `void RegPost(std::string url, HttpHandler handler)`
- **功能**: 注册POST请求处理器
- **参数**:
    - `url`: 请求URL路径
    - `handler`: 处理函数指针
- **返回**: 无

### 请求处理函数

#### `void user_register(std::shared_ptr<HttpConnection> connection)`
- **功能**: 处理用户注册请求
- **参数**:
    - `connection`: HTTP连接对象指针
- **返回**: 无
- **说明**: 验证用户输入，调用验证服务和数据库存储

#### `void user_login(std::shared_ptr<HttpConnection> connection)`
- **功能**: 处理用户登录请求
- **参数**:
    - `connection`: HTTP连接对象指针
- **返回**: 无
- **说明**: 验证用户凭据，生成登录令牌

#### `void reset_pwd(std::shared_ptr<HttpConnection> connection)`
- **功能**: 处理重置密码请求
- **参数**:
    - `connection`: HTTP连接对象指针
- **返回**: 无
- **说明**: 验证验证码，更新用户密码

#### `void get_varifycode(std::shared_ptr<HttpConnection> connection)`
- **功能**: 处理获取验证码请求
- **参数**:
    - `connection`: HTTP连接对象指针
- **返回**: 无
- **说明**: 调用验证服务获取邮箱验证码

---

## 数据库操作

### MysqlDao 类

#### `MysqlDao* GetInstance()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `MysqlDao*` - 单例指针

#### `int RegUser(const std::string& name, const std::string& email, const std::string& pwd)`
- **功能**: 注册新用户
- **参数**:
    - `name`: 用户名
    - `email`: 邮箱地址
    - `pwd`: 密码
- **返回**: `int` - 新用户ID（成功时>0，失败时≤0）

#### `bool CheckEmail(const std::string& name, const std::string& email)`
- **功能**: 检查邮箱和用户名匹配
- **参数**:
    - `name`: 用户名
    - `email`: 邮箱地址
- **返回**: `bool` - 是否匹配

#### `bool UpdatePwd(const std::string& name, const std::string& newpwd)`
- **功能**: 更新用户密码
- **参数**:
    - `name`: 用户名
    - `newpwd`: 新密码
- **返回**: `bool` - 更新是否成功

#### `bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo)`
- **功能**: 验证用户密码
- **参数**:
    - `email`: 邮箱地址
    - `pwd`: 密码
    - `userInfo`: 用户信息对象引用
- **返回**: `bool` - 验证是否成功

### MysqlMgr 类

#### `MysqlMgr* GetInstance()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `MysqlMgr*` - 单例指针

#### `std::unique_ptr<sql::Connection> getConnection()`
- **功能**: 获取数据库连接
- **参数**: 无
- **返回**: `std::unique_ptr<sql::Connection>` - 数据库连接智能指针

#### `void returnConnection(std::unique_ptr<sql::Connection> con)`
- **功能**: 归还数据库连接到连接池
- **参数**:
    - `con`: 数据库连接智能指针
- **返回**: 无

#### `void Close()`
- **功能**: 关闭所有数据库连接
- **参数**: 无
- **返回**: 无

---

## Redis缓存

### RedisMgr 类

#### `RedisMgr* GetInstance()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `RedisMgr*` - 单例指针

#### `bool Get(const std::string& key, std::string& value)`
- **功能**: 获取键值
- **参数**:
    - `key`: 键名
    - `value`: 值的引用（输出参数）
- **返回**: `bool` - 操作是否成功

#### `bool Set(const std::string& key, const std::string& value)`
- **功能**: 设置键值对
- **参数**:
    - `key`: 键名
    - `value`: 值
- **返回**: `bool` - 操作是否成功

#### `bool Auth(const std::string& password)`
- **功能**: Redis认证
- **参数**:
    - `password`: Redis密码
- **返回**: `bool` - 认证是否成功

#### `bool LPush(const std::string& key, const std::string& value)`
- **功能**: 列表左侧推入元素
- **参数**:
    - `key`: 列表键名
    - `value`: 要推入的值
- **返回**: `bool` - 操作是否成功

#### `bool LPop(const std::string& key, std::string& value)`
- **功能**: 列表左侧弹出元素
- **参数**:
    - `key`: 列表键名
    - `value`: 弹出值的引用（输出参数）
- **返回**: `bool` - 操作是否成功

#### `bool RPop(const std::string& key, std::string& value)`
- **功能**: 列表右侧弹出元素
- **参数**:
    - `key`: 列表键名
    - `value`: 弹出值的引用（输出参数）
- **返回**: `bool` - 操作是否成功

#### `bool HSet(const std::string& key, const std::string& hkey, const std::string& value)`
- **功能**: 设置哈希表字段
- **参数**:
    - `key`: 哈希表键名
    - `hkey`: 字段名
    - `value`: 字段值
- **返回**: `bool` - 操作是否成功

#### `std::string HGet(const std::string& key, const std::string& hkey)`
- **功能**: 获取哈希表字段值
- **参数**:
    - `key`: 哈希表键名
    - `hkey`: 字段名
- **返回**: `std::string` - 字段值（空字符串表示不存在）

#### `bool Del(const std::string& key)`
- **功能**: 删除键
- **参数**:
    - `key`: 要删除的键名
- **返回**: `bool` - 操作是否成功

#### `bool ExistsKey(const std::string& key)`
- **功能**: 检查键是否存在
- **参数**:
    - `key`: 键名
- **返回**: `bool` - 键是否存在

#### `void Close()`
- **功能**: 关闭Redis连接
- **参数**: 无
- **返回**: 无

---

## gRPC客户端

### VerifyGrpcClient 类

#### `GetVarifyRsp GetVarifyCode(std::string email)`
- **功能**: 获取邮箱验证码
- **参数**:
    - `email`: 邮箱地址
- **返回**: `GetVarifyRsp` - 验证码响应对象

#### `VerifyGrpcClient* GetInstance()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `VerifyGrpcClient*` - 单例指针

---

## 配置管理

### ConfigMgr 类

#### `ConfigMgr& Inst()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `ConfigMgr&` - 单例引用

#### `SectionInfo& operator[](const std::string& section)`
- **功能**: 访问配置节
- **参数**:
    - `section`: 配置节名称
- **返回**: `SectionInfo&` - 配置节引用

### SectionInfo 类

#### `std::string operator[](const std::string& key)`
- **功能**: 获取配置项值
- **参数**:
    - `key`: 配置项键名
- **返回**: `std::string` - 配置项值

---

## 工具函数

### AsioIOServicePool 类

#### `AsioIOServicePool* GetInstance()`
- **功能**: 获取单例实例
- **参数**: 无
- **返回**: `AsioIOServicePool*` - 单例指针

#### `boost::asio::io_context& GetIOService()`
- **功能**: 获取IO服务对象
- **参数**: 无
- **返回**: `boost::asio::io_context&` - IO上下文引用

#### `void Stop()`
- **功能**: 停止所有IO服务
- **参数**: 无
- **返回**: 无

### 测试函数

#### `void TestRedis()`
- **功能**: 测试Redis连接和基本操作
- **参数**: 无
- **返回**: 无
- **说明**: 用于验证Redis服务是否正常工作

#### `void TestRedisMgr()`
- **功能**: 测试RedisMgr类的各种操作
- **参数**: 无
- **返回**: 无
- **说明**: 单元测试函数，验证Redis管理器功能

### 主函数

#### `int main()`
- **功能**: 程序入口点
- **参数**: 无
- **返回**: `int` - 程序退出码
- **说明**:
    - 初始化配置管理器
    - 创建并启动HTTP服务器
    - 处理系统信号（SIGINT, SIGTERM）
    - 运行IO事件循环

---

## 数据结构

### UserInfo 结构体
```cpp
struct UserInfo {
    std::string name;     // 用户名
    std::string pwd;      // 密码
    int uid;              // 用户ID
    std::string email;    // 邮箱地址
};
```

### 常量定义
- `SUCCESS`: 操作成功标识
- `VARIFY_CODE_EXPIRED`: 验证码过期
- `VARIFY_CODE_ERROR`: 验证码错误
- `USER_EXIST`: 用户已存在
- `PASSWD_ERROR`: 密码错误
- `EMAIL_NOT_MATCH`: 邮箱不匹配
- `PASSWD_UPDATE_FAILED`: 密码更新失败
- `PASSWD_INVALID`: 密码无效

---

## 异常处理

所有函数都应该正确处理以下类型的异常：
- **网络异常**: 连接断开、超时等
- **数据库异常**: 连接失败、SQL执行错误等
- **Redis异常**: 连接失败、命令执行错误等
- **JSON解析异常**: 格式错误、字段缺失等
- **gRPC异常**: 服务不可用、调用超时等

建议在调用这些函数时使用try-catch块进行异常捕获和处理。