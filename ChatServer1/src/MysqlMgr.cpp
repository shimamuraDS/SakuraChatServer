//
// Created by adachi on 26-3-16.
//

#include "MysqlMgr.h"


MysqlMgr::~MysqlMgr() {

}

MysqlMgr::MysqlMgr() {
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(int uid)
{
    return _dao.GetUser(uid);
}

std::shared_ptr<UserInfo> MysqlMgr::GetUser(std::string name)
{
    return nullptr;
    return _dao.GetUser(name);
}
