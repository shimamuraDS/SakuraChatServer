//
// Created by adachi on 25-9-11.
//

#ifndef MYSQLMGR_H
#define MYSQLMGR_H

#include "const.h"
#include "MysqlDao.h"

class MysqlMgr : public Singleton<MysqlMgr> {
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    bool CheckEmail(const std::string& name, const std::string& email);
    bool UpdatePwd(const std::string& name, const std::string& pwd);
private:
    MysqlMgr();
    MysqlDao _dao;
};



#endif //MYSQLMGR_H
