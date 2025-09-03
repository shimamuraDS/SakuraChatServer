//
// Created by adachi on 25-9-2.
//

#include "MysqlDao.h"
#include "ConfigMgr.h"

MySqlPool::MySqlPool(const std::string& url, const std::string& user, const std::string& pass,
                     const std::string& schema, int poolSize) : _url(url), _user(user), _pass(pass), _schema(schema),
                                                                _poolSize(poolSize), _b_stop(false) {
    try {
        for (int i = 0; i < _poolSize; i++) {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
            auto* con = driver->connect(_url, _user, _pass);
            con->setSchema(_schema);
            // 获取当前时间戳，单位为秒
            auto currentTime = std::chrono::system_clock::now().time_since_epoch();
            // 将时间戳转换为秒数
            long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
            _pool.push(std::make_unique<SqlConnection>(con, timestamp));
        }

        _check_thread = std::thread([this]() {
            while (!_b_stop) {
                checkConnection();
                std::this_thread::sleep_for(std::chrono::seconds(60));
            }
        });
        _check_thread.detach();
    } catch (sql::SQLException& e) {
        std::cout << "mysql pool init failed, error is " << e.what() << std::endl;
    }
}

MySqlPool::~MySqlPool() {
    std::unique_lock<std::mutex> lock(_mutex);
    while (!_pool.empty()) {
        _pool.pop();
    }
}

void MySqlPool::checkConnection() {
    std::lock_guard<std::mutex> lock(_mutex);
    int poolsize = _pool.size();
    // 获取当前时间戳
    auto currentTime = std::chrono::system_clock::now().time_since_epoch();
    // 将时间戳转换为秒数
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
    for (int i = 0; i < poolsize; i++) {
        auto con = std::move(_pool.front());
        _pool.pop();
        Defer defer([this, &con]() {
            _pool.push(std::move(con));
        });

        if (timestamp - con -> _last_oper_time < 5) {
            continue;
        }

        try {
            std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
            stmt->executeQuery("SELECT 1");
            con->_last_oper_time = timestamp;
            std::cout << "execute timer alive query, cur is " << timestamp << std::endl;
        } catch (sql::SQLException& e) {
            std::cout << "Error keeping connection alive: " << e.what() << std::endl;
            // 连接失效，重新连接
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
            auto* newcon = driver->connect(_url, _user, _pass);
            newcon->setSchema(_schema);
            con->_con.reset(newcon);
            con->_last_oper_time = timestamp;
        }
    }
}

std::unique_ptr<SqlConnection> MySqlPool::getConnection() {
    std::unique_lock<std::mutex> lock(_mutex);
    _cond.wait(lock, [this] {
        if (_b_stop) {
            return true;
        }
        return !_pool.empty();
    });
    if (_b_stop) {
        return nullptr;
    }
    std::unique_ptr<SqlConnection> con(std::move(_pool.front()));
    _pool.pop();
    return con;
}

void MySqlPool::returnConnection(std::unique_ptr<SqlConnection> con) {
    std::unique_lock<std::mutex> lock(_mutex);
    if (_b_stop) {
        return;
    }
    _pool.push(std::move(con));
    _cond.notify_one();
}

void MySqlPool::Close() {
    _b_stop = true;
    _cond.notify_all();
}

MySqlDao::MySqlDao() {
    auto& cfg = ConfigMgr::Inst();
    const auto& host = cfg["Mysql"]["Host"];
    const auto& port = cfg["Mysql"]["Port"];
    const auto& user = cfg["Mysql"]["User"];
    const auto& pwd = cfg["Mysql"]["Password"];
    const auto& schema = cfg["Mysql"]["Schema"];
    _pool.reset(new MySqlPool(host + ":" + port, user, pwd, schema, 5));
}

MySqlDao::~MySqlDao() {
    _pool->Close();
}

int MySqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
    auto con = _pool->getConnection();
    try {
        if (con == nullptr) {
            return false;
        }
        // 准备调用存储过程
        std::unique_ptr <sql::PreparedStatement> stmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
        // 设置输入参数
        stmt->setString(1, name);
        stmt->setString(2, email);
        stmt->setString(3, pwd);
        // 执行存储过程
        stmt->execute();
        std::unique_ptr<sql::Statement> stmtResult(con->_con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));
        if (res->next()) {
            int result = res->getInt("result");
            std::cout << "Result: " << result << std::endl;
            _pool->returnConnection(std::move(con));
            return result;
        }
        _pool->returnConnection(std::move(con));
        return -1;
    } catch (sql::SQLException& e) {
        _pool->returnConnection(std::move(con));
        std::cerr << "SQKException: " << e.what();
        std::cerr << "(MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " ) " << std::endl;
        return -1;
    }
}

