//
// Created by adachi on 25-9-2.
//

#include "../include/MysqlDao.h"
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
            // std::cout << "execute timer alive query, cur is " << timestamp << std::endl;
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

MysqlDao::MysqlDao() {
    auto& cfg = ConfigMgr::Inst();
    const auto& host = cfg["MySQL"]["Host"];
    const auto& port = cfg["MySQL"]["Port"];
    const auto& user = cfg["MySQL"]["User"];
    const auto& pwd = cfg["MySQL"]["Password"];
    const auto& schema = cfg["MySQL"]["Schema"];
    _pool.reset(new MySqlPool(host + ":" + port, user, pwd, schema, 5));
}

MysqlDao::~MysqlDao() {
    _pool->Close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd) {
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

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
    auto con = _pool->getConnection();
    try {
        if (con == nullptr) {
            _pool->returnConnection(std::move(con));
            return false;
        }

        // 准备查询语句
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT email FROM user WHERE name = ?"));

        // 绑定参数
        pstmt->setString(1, name);

        // 执行查询
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        // 遍历结果集
        while (res->next()) {
            std::cout << "Check Email: " << res->getString("email") << std::endl;
            if (email != res->getString("email")) {
                _pool->returnConnection(std::move(con));
                return false;
            }
            _pool->returnConnection(std::move(con));
            return true;
        }
    } catch (sql::SQLException& e) {
        _pool->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
    auto con = _pool->getConnection();
    try {
        if (con == nullptr) {
            _pool->returnConnection(std::move(con));
            return false;
        }

        // 准备查询语句
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?"));

        // 绑定参数
        pstmt->setString(2, name);
        pstmt->setString(1, newpwd);

        // 执行更新
        int updateCount = pstmt->executeUpdate();

        std::cout << "Updated rows: " << updateCount << std::endl;
        _pool->returnConnection(std::move(con));
        return true;
    } catch (sql::SQLException& e) {
        _pool->returnConnection(std::move(con));
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo) {
    auto con = _pool->getConnection();
    if (con == nullptr) {
        return false;
    }
    Defer defer([this, &con]() {
        _pool->returnConnection(std::move(con));
    });
    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT * FROM user WHERE email = ?"));
        pstmt->setString(1, email);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        std::string origin_pwd = "";
        bool b_find = false;

        // 【关键修改】：必须在 while 循环内部把 name、uid 等字段一并提取！
        while (res->next()) {
            origin_pwd = res->getString("pwd");
            std::cout << "Password: " << origin_pwd << std::endl;
            userInfo.name = res->getString("name");
            userInfo.email = email;
            userInfo.uid = res->getInt("uid");
            userInfo.pwd = origin_pwd;
            b_find = true;
            break;
        }

        if (!b_find || pwd != origin_pwd) {
            return false;
        }

        return true;
    } catch (sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << "(MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << ")" << std::endl;
        return false;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid) {
    auto con = _pool->getConnection();
    if (con == nullptr) {
        return nullptr;
    }

    Defer defer([this, &con]() {
        _pool->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT uid, name, email, nick, `desc`, gender, icon FROM user WHERE uid = ? AND status = 0"));
        pstmt->setInt(1, uid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        std::shared_ptr<UserInfo> user_ptr = nullptr;

        if (res->next()) {
            user_ptr = std::make_shared<UserInfo>();

            user_ptr->uid = res->getInt("uid");
            user_ptr->name = res->getString("name");
            user_ptr->email = res->getString("email");
            user_ptr->nick = res->getString("nick");
            user_ptr->desc = res->getString("desc");
            user_ptr->gender = res->getInt("gender");
            user_ptr->icon = res->getString("icon");
        }
        return user_ptr;
    } catch (sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return nullptr;
    }
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(std::string name) {
    auto con = _pool->getConnection();
    if (con == nullptr) {
        return nullptr;
    }

    Defer defer([this, &con]() {
        _pool->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
        "SELECT uid, name, email, nick, `desc`, gender, icon FROM user WHERE name = ? AND status = 0"));
        pstmt->setString(1, name);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        std::shared_ptr<UserInfo> user_ptr = nullptr;

        if (res->next()) {
            user_ptr = std::make_shared<UserInfo>();

            user_ptr->uid = res->getInt("uid");
            user_ptr->name = res->getString("name");
            user_ptr->email = res->getString("email");
            user_ptr->nick = res->getString("nick");
            user_ptr->desc = res->getString("desc");
            user_ptr->gender = res->getInt("gender");
            user_ptr->icon = res->getString("icon");
        }
        return user_ptr;
    } catch (sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return nullptr;
    }
}

bool MysqlDao::FriendExists(int selfUid, int friendUid)
{
    if (selfUid <= 0 || friendUid <= 0 || selfUid == friendUid)
        return false;

    auto con = _pool->getConnection();
    if (!con)
        return false;

    Defer giveBack([this, &con]() {
        _pool->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->_con->prepareStatement("SELECT 1 FROM friend WHERE self_uid = ? AND friend_uid = ? LIMIT 1"));

        pstmt->setInt(1, selfUid);
        pstmt->setInt(2, friendUid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        // 查到记录，说明已经是好友
        return res->next();
    } catch (const sql::SQLException &e) {
        std::cerr << "FriendExists failed, code="
                  << e.getErrorCode() << std::endl;
        return false;
    }
}

FriendApplyResult MysqlDao::AddFriendApply(
    int fromUid, int toUid,
    const std::string &descs,
    const std::string &backName)
{
    FriendApplyResult output;
    auto con = _pool->getConnection();
    if (!con)
        return output;

    Defer giveBack([this, &con] {
        _pool->returnConnection(std::move(con));
    });

    try {
        {
            auto call = std::unique_ptr<sql::PreparedStatement>(
                con->_con->prepareStatement(
                    "CALL apply_friend(?,?,?,?,@result)"));
            call->setInt(1, fromUid);
            call->setInt(2, toUid);
            call->setString(3, descs);
            call->setString(4, backName);
            call->execute();
        }

        auto resultStmt = std::unique_ptr<sql::PreparedStatement>(
            con->_con->prepareStatement(
                "SELECT @result AS result, "
                "COALESCE((SELECT id FROM friend_apply "
                "WHERE from_uid=? AND to_uid=?), 0) AS apply_id"));
        resultStmt->setInt(1, fromUid);
        resultStmt->setInt(2, toUid);

        auto resultSet = std::unique_ptr<sql::ResultSet>(
            resultStmt->executeQuery());
        if (resultSet->next()) {
            output.result = resultSet->getInt("result");
            output.applyId = static_cast<std::int64_t>(
                resultSet->getUInt64("apply_id"));
        }
    } catch (const sql::SQLException &e) {
        std::cerr << "AddFriendApply failed, code="
                  << e.getErrorCode() << std::endl;
    }
    return output;
}

std::vector<PendingFriendApplyInfo> MysqlDao::GetPendingFriendApplies(int toUid, std::int64_t afterId, int limit) {
    std::vector<PendingFriendApplyInfo> applications;

    if (toUid <= 0 || afterId < 0)
        return applications;

    limit = std::clamp(limit, 1, 200);

    auto con = _pool->getConnection();
    if (!con)
        return applications;

    Defer giveBack([this, &con]() {
        _pool->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(
            con->_con->prepareStatement(
                "SELECT a.id AS apply_id, a.from_uid, a.status, a.descs, u.name, u.nick, u.gender, u.icon "
                "FROM friend_apply a JOIN user u ON u.uid = a.from_uid WHERE a.to_uid = ? AND a.status = 0 "
                "AND a.id > ? ORDER BY a.id ASC LIMIT ?"));

        // 分别对应 SQL 中的三个问号
        stmt->setInt(1, toUid);
        stmt->setUInt64(2, static_cast<std::uint64_t>(afterId));
        stmt->setInt(3, limit);

        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());

        while (res->next()) {
            PendingFriendApplyInfo item;
            item.applyId = res->getInt64("apply_id");
            item.uid = res->getInt("from_uid");
            item.status = res->getInt("status");
            item.descs = res->getString("descs");
            item.name = res->getString("name");
            item.nick = res->getString("nick");
            item.gender = res->getInt("gender");
            item.icon = res->getString("icon");

            applications.push_back(std::move(item));
        }
    } catch (const sql::SQLException &e) {
        std::cerr << "GetPendingFriendApplies failed, code="
                  << e.getErrorCode() << std::endl;

        // 出错时不返回只读取了一部分的结果
        applications.clear();
    }

    return applications;
}

ResolveFriendApplyResult MysqlDao::ResolveFriendApply(std::int64_t applyId, int actorUid, bool agree) {
    ResolveFriendApplyResult output;  // 默认 result = -1

    if (applyId <= 0 || actorUid <= 0)
        return output;

    auto con = _pool->getConnection();
    if (!con)
        return output;

    // 所有查询使用同一个连接，退出时归还连接池
    Defer giveBack([this, &con]() {
        _pool->returnConnection(std::move(con));
    });

    try {
        int fromUid = 0;
        int toUid = 0;

        // 1. 查询申请双方，供审核成功后的通知使用
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                con->_con->prepareStatement(
                    "SELECT from_uid, to_uid "
                    "FROM friend_apply "
                    "WHERE id = ? AND to_uid = ?"));

            stmt->setUInt64(
                1, static_cast<std::uint64_t>(applyId));
            stmt->setInt(2, actorUid);

            std::unique_ptr<sql::ResultSet> res(
                stmt->executeQuery());

            if (!res->next())
                return output;

            fromUid = res->getInt("from_uid");
            toUid = res->getInt("to_uid");
        }

        // 2. 调用存储过程，由数据库执行权限校验和事务更新
        {
            std::unique_ptr<sql::PreparedStatement> call(
                con->_con->prepareStatement(
                    "CALL resolve_friend_apply("
                    "?,?,?,@resolve_friend_result)"));

            call->setUInt64(
                1, static_cast<std::uint64_t>(applyId));
            call->setInt(2, actorUid);
            call->setBoolean(3, agree);

            call->execute();
            call->close();
        }

        // 3. 读取存储过程的 OUT 参数
        {
            std::unique_ptr<sql::Statement> stmt(
                con->_con->createStatement());

            std::unique_ptr<sql::ResultSet> res(
                stmt->executeQuery(
                    "SELECT @resolve_friend_result AS result"));

            if (!res->next() || res->isNull("result"))
                return output;

            output.result = res->getInt("result");
        }

        // 4. 只有数据库确认成功，才提供通知所需的双方 UID
        if (output.result == 0) {
            output.fromUid = fromUid;
            output.toUid = toUid;
        }
    } catch (const sql::SQLException &e) {
        std::cerr << "ResolveFriendApply failed, code="
                  << e.getErrorCode() << std::endl;
    }

    return output;
}