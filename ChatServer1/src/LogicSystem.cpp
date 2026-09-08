//
// Created by adachi on 26-3-10.
//

#include "LogicSystem.h"

#include <charconv>

#include "ChatGrpcClient.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "UserMgr.h"

LogicSystem::LogicSystem():_b_stop(false) {
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

void LogicSystem::RegisterCallBacks() {
    _fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::ResolveFriendApply, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::DealMsg() {
    for (;;) {
        std::unique_lock<std::mutex> unique_lk(_mutex);
        while (_msg_que.empty() && !_b_stop) {
            _consume.wait(unique_lk);
        }

        if (_b_stop) {
            while (!_msg_que.empty()) {
                auto msg_node = _msg_que.front();
                std::cout << "recv_msg id is " << msg_node->_recvnode->_msg_id << std::endl;
                auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
                if (call_back_iter != _fun_callbacks.end()) {
                    _msg_que.pop();
                    continue;
                }
                call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
                _msg_que.pop();
            }
            break;
        }

        auto msg_node = _msg_que.front();
        std::cout << "recv_msg id is " << msg_node->_recvnode->_msg_id << std::endl;
        auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
        if (call_back_iter == _fun_callbacks.end()) {
            _msg_que.pop();
            std::cout << "msg id [" << msg_node->_recvnode->_msg_id << "] handler not found" << std::endl;
            continue;
        }
        call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
        _msg_que.pop();
    }
}

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);
    auto uid = root["uid"].asInt();
    auto token = root["token"].asString();
    std::cout << "user login uid is  " << uid << " user token  is "
        << token << std::endl;

    // auto rsp = StatusGrpcClient::GetInstance()->Login(uid, root["token"].asString());
    Json::Value rtvalue;
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, MSG_CHAT_LOGIN_RSP);
    });

    // rtvalue["error"] = rsp.error();
    // if (rsp.error() != ErrorCodes::Success) {
    //     return;
    // }

    std::string uid_str = std::to_string(uid);
    std::string token_key = std::string(USERTOKENPREFIX) + uid_str;
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    if (!success) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }
    if (token_value != token) {
        rtvalue["error"] = ErrorCodes::TokenInvalid;
        return;
    }
    rtvalue["error"] = ErrorCodes::Success;

    std::string base_key = std::string(USER_BASE_INFO) + uid_str;
    auto user_info = std::make_shared<UserInfo>();
    bool b_base = GetBaseInfo(base_key, uid, user_info);
    if (!b_base) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }
    rtvalue["uid"] = uid;
    rtvalue["pwd"] = user_info->pwd;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["gender"] = user_info->gender;
    rtvalue["icon"] = user_info->icon;

    auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");
    auto rd_res = RedisMgr::GetInstance()->HGet(std::string(LOGIN_COUNT), server_name);
    int count = 0;
    if (!rd_res.empty()) {
        count = std::stoi(rd_res);
    }
    count++;

    auto count_str = std::to_string(count);
    RedisMgr::GetInstance()->HSet(std::string(LOGIN_COUNT), server_name, count_str);

    session->SetUserId(uid);

    // 为用户设置登陆ip server名字
    std::string ipkey = std::string(USERIPPREFIX) + uid_str;
    RedisMgr::GetInstance()->Set(ipkey, server_name);
    // 记录当前用户的连接
    UserMgr::GetInstance()->SetUserSession(uid, session);

    // 查询该用户收到的待处理申请
    const auto applyList = MysqlMgr::GetInstance()
        ->GetPendingFriendApplies(uid, 0, 200);

    // 即使没有申请，也返回 JSON 数组 []，而不是 null
    rtvalue["apply_list"] = Json::Value(Json::arrayValue);

    for (const auto &apply : applyList) {
        Json::Value item;
        item["apply_id"] = Json::Int64(apply.applyId);
        item["uid"] = apply.uid;
        item["name"] = apply.name;
        item["nick"] = apply.nick;
        item["gender"] = apply.gender;
        item["icon"] = apply.icon;
        item["message"] = apply.descs;
        item["status"] = apply.status;

        rtvalue["apply_list"].append(item);
    }
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        userinfo->uid = root["uid"].asInt();
        userinfo->pwd = root["pwd"].asString();
        userinfo->name = root["name"].asString();
        userinfo->email = root["email"].asString();
        userinfo->nick = root["nick"].asString();
        userinfo->desc = root["desc"].asString();
        userinfo->gender = root["gender"].asInt();
        userinfo->icon = root["icon"].asString();
        std::cout << "user login uid is " << userinfo->uid << " user name is " << userinfo->name << " email is " << userinfo->email << " pwd is " << userinfo->pwd << std::endl;
    } else {
        std::shared_ptr<UserInfo> user_info = nullptr;
        user_info = MysqlMgr::GetInstance()->GetUser(uid);
        if (user_info == nullptr) {
            return false;
        }
        userinfo = user_info;

        Json::Value redis_root;
        redis_root["uid"] = userinfo->uid;
        redis_root["pwd"] = userinfo->pwd;
        redis_root["name"] = userinfo->name;
        redis_root["email"] = userinfo->email;
        redis_root["nick"] = userinfo->nick;
        redis_root["desc"] = userinfo->desc;
        redis_root["gender"] = userinfo->gender;
        redis_root["icon"] = userinfo->icon;
        RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
    }
    return true;
}

LogicSystem::~LogicSystem() {
    _b_stop = true;
    _consume.notify_one();
    _worker_thread.join();
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> unique_lk(_mutex);
    _msg_que.push(msg);
    if (_msg_que.size() == 1) {
        unique_lk.unlock();
        _consume.notify_one();
    }
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session,
                             const short &,
                             const std::string &msgData)
{
    Json::Value request;
    Json::Value response;
    Json::Reader reader;

    Defer reply([&] {
        session->Send(response.toStyledString(), ID_SEARCH_USER_RSP);
    });

    if (!reader.parse(msgData, request) ||
        !request.isMember("keyword") ||
        !request["keyword"].isString()) {
        response["error"] = ErrorCodes::Error_Json;
        return;
        }

    const std::string keyword = request["keyword"].asString();
    if (keyword.empty() || keyword.size() > 64) {
        response["error"] = ErrorCodes::Error_Json;
        return;
    }

    std::shared_ptr<UserInfo> user;

    const bool digits = !keyword.empty() &&
        std::all_of(keyword.begin(), keyword.end(),
                    [](unsigned char c) { return std::isdigit(c); });

    if (digits) {
        int uid = 0;
        const auto result = std::from_chars(
            keyword.data(), keyword.data() + keyword.size(), uid);
        if (result.ec != std::errc{} ||
            result.ptr != keyword.data() + keyword.size()) {
            response["error"] = ErrorCodes::UidInvalid;
            return;
            }
        user = MysqlMgr::GetInstance()->GetUser(uid);
    } else {
        user = MysqlMgr::GetInstance()->GetUser(keyword);
    }

    response["error"] = ErrorCodes::Success;
    response["found"] = user != nullptr;
    if (!user)
        return;

    // 只返回公开资料，绝不能把 pwd、email、token 发给搜索者。
    response["uid"] = user->uid;
    response["name"] = user->name;
    response["nick"] = user->nick;
    response["desc"] = user->desc;
    response["gender"] = user->gender;
    response["icon"] = user->icon;
    response["is_friend"] =
        session->GetUserId() != user->uid &&
        MysqlMgr::GetInstance()->FriendExists(session->GetUserId(), user->uid);
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short &, const std::string &msgData) {
    Json::Value request;
    Json::Value response;
    Json::Reader reader;

    Defer reply([&] {
        session->Send(response.toStyledString(), ID_ADD_FRIEND_RSP);
    });

    if (!reader.parse(msgData, request) ||
        !request["touid"].isInt() ||
        !request["descs"].isString() ||
        !request["back_name"].isString()) {
        response["error"] = ErrorCodes::Error_Json;
        response["result"] = -1;
        response["apply_id"] = Json::Int64(0);
        return;
        }

    const int fromUid = session->GetUserId();
    const int toUid = request["touid"].asInt();
    const std::string descs = request["descs"].asString();
    const std::string backName = request["back_name"].asString();

    if (fromUid <= 0 || toUid <= 0 || fromUid == toUid ||
        descs.size() > 255 || backName.size() > 64) {
        response["error"] = ErrorCodes::UidInvalid;
        response["result"] = -1;
        response["apply_id"] = Json::Int64(0);
        return;
        }

    const auto dbResult = MysqlMgr::GetInstance()->AddFriendApply(
        fromUid, toUid, descs, backName);

    response["error"] = ErrorCodes::Success;
    response["result"] = dbResult.result;
    response["apply_id"] = Json::Int64(dbResult.applyId);

    if (dbResult.result != 0)
        return;

    // 数据库成功后再执行在线通知；通知失败不回滚申请。
    NotifyFriendApplication(fromUid, toUid, dbResult.applyId, descs);
}

void LogicSystem::NotifyFriendApplication(
    int fromUid, int toUid, std::int64_t applyId,
    const std::string &descs)
{
    const auto applicant = MysqlMgr::GetInstance()->GetUser(fromUid);
    if (!applicant)
        return;

    Json::Value notify;
    notify["error"] = ErrorCodes::Success;
    notify["apply_id"] = Json::Int64(applyId);
    notify["applyuid"] = fromUid;
    notify["name"] = applicant->name;
    notify["nick"] = applicant->nick;
    notify["desc"] = applicant->desc;
    notify["icon"] = applicant->icon;
    notify["gender"] = applicant->gender;
    notify["message"] = descs;
    const std::string notificationJson = notify.toStyledString();

    const std::string routeKey =
        std::string(USERIPPREFIX) + std::to_string(toUid);
    std::string targetServer;
    if (!RedisMgr::GetInstance()->Get(routeKey, targetServer))
        return; // 对方离线，等待下次登录同步

    const auto selfServer =
        ConfigMgr::Inst().GetValue("SelfServer", "Name");
    if (targetServer == selfServer) {
        if (auto targetSession =
                UserMgr::GetInstance()->GetSession(toUid)) {
            targetSession->Send(
                notificationJson, ID_NOTIFY_ADD_FRIEND_REQ);
                }
        return;
    }

    message::AddFriendReq rpcRequest;
    rpcRequest.set_applyuid(fromUid);
    rpcRequest.set_touid(toUid);
    rpcRequest.set_apply_id(applyId);
    rpcRequest.set_name(applicant->name);
    rpcRequest.set_nick(applicant->nick);
    rpcRequest.set_desc(descs);
    rpcRequest.set_icon(applicant->icon);
    rpcRequest.set_gender(applicant->gender);

    ChatGrpcClient::GetInstance()->NotifyAddFriend(
        targetServer, rpcRequest);
}

void LogicSystem::ResolveFriendApply(
    std::shared_ptr<CSession> session,
    const short &,
    const std::string &msgData)
{
    Json::Value request;
    Json::Value response;
    Json::Reader reader;

    Defer reply([&] {
        session->Send(response.toStyledString(), ID_AUTH_FRIEND_RSP);
    });

    if (!reader.parse(msgData, request) ||
        !request["apply_id"].isIntegral() ||
        !request["agree"].isBool()) {
        response["error"] = ErrorCodes::Error_Json;
        response["result"] = -1;
        response["apply_id"] = Json::Int64(0);
        response["agree"] = false;
        return;
        }

    const auto applyId = request["apply_id"].asInt64();
    const bool agree = request["agree"].asBool();
    const int actorUid = session->GetUserId();

    const auto dbResult = MysqlMgr::GetInstance()->ResolveFriendApply(
        applyId, actorUid, agree);

    response["error"] = ErrorCodes::Success;
    response["result"] = dbResult.result;
    response["apply_id"] = Json::Int64(applyId);
    response["agree"] = agree;

    if (dbResult.result == 0) {
        NotifyFriendResolution(
            dbResult.fromUid, actorUid, applyId, agree);
    }
}

void LogicSystem::NotifyFriendResolution(
    int applicantUid, int actorUid,
    std::int64_t applyId, bool agree)
{
    const std::string routeKey =
        std::string(USERIPPREFIX) + std::to_string(applicantUid);
    std::string targetServer;
    if (!RedisMgr::GetInstance()->Get(routeKey, targetServer))
        return;

    Json::Value notify;
    notify["error"] = ErrorCodes::Success;
    notify["result"] = 0;
    notify["apply_id"] = Json::Int64(applyId);
    notify["agree"] = agree;
    notify["peer_uid"] = actorUid;

    const auto selfServer =
        ConfigMgr::Inst().GetValue("SelfServer", "Name");
    if (targetServer == selfServer) {
        if (auto targetSession =
                UserMgr::GetInstance()->GetSession(applicantUid)) {
            targetSession->Send(
                notify.toStyledString(), ID_NOTIFY_AUTH_FRIEND_REQ);
                }
        return;
    }

    message::AuthFriendReq rpcRequest;
    rpcRequest.set_fromuid(actorUid);
    rpcRequest.set_touid(applicantUid);
    rpcRequest.set_apply_id(applyId);
    rpcRequest.set_agree(agree);
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(
        targetServer, rpcRequest);
}