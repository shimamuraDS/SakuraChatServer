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
    Json::Value PrivacyCommand(int actor, const Json::Value &r) { return _dao.PrivacyCommand(actor, r); }
    Json::Value DeleteMessage(int actor, std::uint64_t id, bool everyone) { return _dao.DeleteMessage(actor, id, everyone); }
    Json::Value DeletionEvents(int actor, std::uint64_t after) { return _dao.DeletionEvents(actor, after); }
    bool ExpireMessages() { return _dao.ExpireMessages(); }
    bool PrivacyAllows(int owner, int actor, const std::string &action) { return _dao.PrivacyAllows(owner, actor, action); }
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
    FriendPageResult GetFriendPage(int selfUid, int afterUid, int limit);
    Json::Value StoreTextMessage(int a, int b, const std::string &id, const std::string &text) { return _dao.StoreTextMessage(a, b, id, text); }
    Json::Value StoredText(int a, const std::string &id) { return _dao.StoredText(a, id); }
    Json::Value ChatHistory(int a, int b, std::uint64_t seq) { return _dao.ChatHistory(a, b, seq); }
    Json::Value ChatConversations(int a, std::uint64_t cursor) { return _dao.ChatConversations(a, cursor); }
    Json::Value RecordReceipt(int a, std::uint64_t id, bool read) { return _dao.RecordReceipt(a, id, read); }
    Json::Value MessageStates(int a, const std::vector<std::string> &ids) { return _dao.MessageStates(a, ids); }
private:
    MysqlMgr();
    MysqlDao _dao;
};



#endif //MYSQLMGR_H
