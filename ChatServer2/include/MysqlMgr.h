//
// Created by adachi on 26-3-16.
//

#ifndef MYSQLMGR_H
#define MYSQLMGR_H

#include "Singleton.h"
#include "data.h"
#include "MysqlDao.h"


class MysqlMgr: public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;
public:
    ~MysqlMgr();
    int RegUser(const std::string& name, const std::string& email,  const std::string& pwd);
    bool CheckEmail(const std::string& name, const std::string & email);
    bool UpdatePwd(const std::string& name, const std::string& email);
    bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);
    std::shared_ptr<UserInfo> GetUser(std::string name);
    bool FriendExists(int selfUid, int friendUid);
    FriendApplyResult AddFriendApply(int fromUid, int toUid, const std::string &descs, const std::string &backName);
    std::vector<PendingFriendApplyInfo> GetPendingFriendApplies(int toUid, std::int64_t afterId, int limit);
    ResolveFriendApplyResult ResolveFriendApply(std::int64_t applyId, int actorUid, bool agree);
private:
    MysqlMgr();
    MysqlDao _dao;
};



#endif //MYSQLMGR_H
