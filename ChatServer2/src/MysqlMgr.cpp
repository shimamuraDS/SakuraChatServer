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
    return _dao.GetUser(std::move(name));
}

bool MysqlMgr::FriendExists(int selfUid, int friendUid)
{
    return _dao.FriendExists(selfUid, friendUid);
}

FriendApplyResult MysqlMgr::AddFriendApply(int fromUid, int toUid, const std::string &descs, const std::string &backName) {
    return _dao.AddFriendApply(fromUid, toUid, descs, backName);
}

std::vector<PendingFriendApplyInfo> MysqlMgr::GetPendingFriendApplies(int toUid, std::int64_t afterId, int limit) {
    return _dao.GetPendingFriendApplies(toUid, afterId, limit);
}

ResolveFriendApplyResult MysqlMgr::ResolveFriendApply(std::int64_t applyId, int actorUid, bool agree) {
    return _dao.ResolveFriendApply(applyId, actorUid, agree);
}