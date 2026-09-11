//
// Created by adachi on 26-3-10.
//

#ifndef LOGICSYSTEM_H
#define LOGICSYSTEM_H
#include "const.h"
#include "CSession.h"
#include "data.h"
#include "MysqlMgr.h"
#include <functional>
#include <cstdint>

class CServer;
typedef std::function<void(std::shared_ptr<CSession>, const short &msg_id, const std::string &msg_data)> FunCallback;
class LogicSystem : public Singleton<LogicSystem> {
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);
private:
    LogicSystem();
    void RegisterCallBacks();
    void DealMsg();
    void LoginHandler(std::shared_ptr<CSession>, const short &msg_id, const std::string &msg_data);
    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo> &user_info);
    void SearchInfo(std::shared_ptr<CSession>, const short &, const std::string &);
    void AddFriendApply(std::shared_ptr<CSession>, const short &, const std::string &);
    void ResolveFriendApply(std::shared_ptr<CSession>, const short &, const std::string &);
    void NotifyFriendApplication(int fromUid, int toUid, std::int64_t applyId, const std::string &descs);
    void NotifyFriendResolution(int applicantUid, int actorUid, std::int64_t applyId, bool agree);
    void GetFriendList(std::shared_ptr<CSession>, const short &, const std::string &);
    void DealChatTextMsg(std::shared_ptr<CSession>, const short &, const std::string &);
    std::thread _worker_thread;
    std::queue<std::shared_ptr<LogicNode>> _msg_que;
    std::mutex _mutex;
    std::condition_variable _consume;
    bool _b_stop;
    std::map<short, FunCallback> _fun_callbacks;
};



#endif //LOGICSYSTEM_H
