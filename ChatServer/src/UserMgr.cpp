//
// Created by adachi on 26-3-12.
//

#include "UserMgr.h"
#include "RedisMgr.h"

UserMgr:: ~ UserMgr(){
    _uid_to_session.clear();
}


std::shared_ptr<CSession> UserMgr::GetSession(int uid)
{
    std::unique_lock<std::mutex> lock(_session_mtx);
    auto iter = _uid_to_session.find(uid);
    if (iter == _uid_to_session.end()) {
        return nullptr;
    }

    auto session = iter->second;
    lock.unlock();
    std::string digest;
    if (!RedisMgr::GetInstance()->Get(std::string(USERTOKENPREFIX) + std::to_string(uid), digest) || digest != session->AuthDigest()) {
        session->Close();
        return nullptr;
    }
    return session;
}

void UserMgr::SetUserSession(int uid, std::shared_ptr<CSession> session)
{
    std::shared_ptr<CSession> previous;
    {
        std::lock_guard<std::mutex> lock(_session_mtx);
        if (session->IsClosed()) return;
        auto &current = _uid_to_session[uid];
        previous = std::move(current);
        current = session;
    }
    // Do not close under the map lock: closing removes the old session from the map.
    if (previous && previous != session) previous->Close();
}

void UserMgr::RmvUserSession(int uid, std::string session_id)
{
    {
        std::lock_guard<std::mutex> lock(_session_mtx);
        auto iter = _uid_to_session.find(uid);
        if (iter == _uid_to_session.end()) {
            return;
        }

        auto session_id_ = iter->second->GetSessionId();
        //不相等说明是其他地方登录了
        if (session_id_ != session_id) {
            return;
        }
        _uid_to_session.erase(uid);
    }

}

UserMgr::UserMgr()
{

}
