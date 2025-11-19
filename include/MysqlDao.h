//
// Created by adachi on 25-9-2.
//

#ifndef MYSQLDAO_H
#define MYSQLDAO_H

#include "const.h"
#include <jdbc/mysql_driver.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/prepared_statement.h>

class SqlConnection {
public:
    SqlConnection(sql::Connection* con, int64_t lasttime) : _con(con), _last_oper_time(lasttime) {}
    std::unique_ptr<sql::Connection> _con;
    int64_t _last_oper_time;
};

class MySqlPool {
public:
    MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize);
    ~MySqlPool();
    void checkConnection();
    std::unique_ptr<SqlConnection> getConnection();
    void returnConnection(std::unique_ptr<SqlConnection> con);
    void Close();
private:
    std::string _url;
    std::string _user;
    std::string _pass;
    std::string _schema;
    int _poolSize;
    std::queue<std::unique_ptr<SqlConnection>> _pool;
    std::mutex _mutex;
    std::condition_variable _cond;
    std::atomic<bool> _b_stop;
    std::thread _check_thread;
};

struct UserInfo {
    std::string name;
    std::string pwd;
    int uid;
    std::string email;
};

class MysqlDao {
public:
    MysqlDao();
    ~MysqlDao();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& pwd);
    bool CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userinfo);
private:
    std::unique_ptr<MySqlPool> _pool;
};


#endif //MYSQLDAO_H
