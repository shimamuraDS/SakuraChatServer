//
// Created by adachi on 26-3-10.
//

#include "LogicSystem.h"

#include <charconv>

#include "ChatGrpcClient.h"
#include "ChatWire.h"
#include "ConfigMgr.h"
#include "RedisMgr.h"
#include "PasswordSecurity.h"
#include "UserMgr.h"

namespace {
void redactProfile(Json::Value &item, int owner, int actor) {
    if (!MysqlMgr::GetInstance()->PrivacyAllows(owner, actor, "profile_policy")) {
        item["icon"] = ""; item["nick"] = ""; item["gender"] = 0;
        if (item.isMember("desc")) item["desc"] = "";
    }
}
}

LogicSystem::LogicSystem():_b_stop(false) {
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

void LogicSystem::RegisterCallBacks() {
    _fun_callbacks[1042] = [](std::shared_ptr<CSession> session, const short &, const std::string &) {
        RedisMgr::GetInstance()->DeleteIfEqual(std::string(USERTOKENPREFIX) + std::to_string(session->GetUserId()), session->AuthDigest());
        session->Close();
    };
    _fun_callbacks[1040] = [](std::shared_ptr<CSession> session, const short &, const std::string &body) {
        Json::Value request; Json::Reader reader;
        if (!reader.parse(body, request) || !request.isObject() || !request["request_id"].isString() || request["request_id"].asString().size() > 64) { session->Close(); return; }
        auto result = MysqlMgr::GetInstance()->PrivacyCommand(session->GetUserId(), request);
        result["request_id"] = request["request_id"];
        session->Send(ChatWire::Compact(result), 1041);
    };
    for (short id : {ID_CHAT_HISTORY_REQ, ID_MESSAGE_RECEIPT_REQ, ID_MESSAGE_STATUS_REQ, ID_CONVERSATION_LIST_REQ, ID_MESSAGE_DELETE_REQ, ID_DELETION_EVENTS_REQ})
        _fun_callbacks[id] = std::bind(&LogicSystem::ChatSync, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::ResolveFriendApply, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_FRIEND_LIST_REQ] = std::bind(&LogicSystem::GetFriendList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    _fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
}

void LogicSystem::DealMsg() {
    for (;;) {
        std::unique_lock<std::mutex> unique_lk(_mutex);
        _consume.wait(unique_lk, [this] { return _b_stop || !_msg_que.empty(); });
        if (_b_stop && _msg_que.empty()) break;
        auto msg_node = _msg_que.front();
        _msg_que.pop();
        unique_lk.unlock();
        // A flushed logout may already be followed by TCP EOF. Still revoke its token.
        if (msg_node->_session->IsClosed() && msg_node->_recvnode->_msg_id != 1042) continue;
        auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
        const bool login = msg_node->_recvnode->_msg_id == MSG_CHAT_LOGIN;
        if (call_back_iter == _fun_callbacks.end() ||
            (login ? msg_node->_session->GetUserId() != 0 : msg_node->_session->GetUserId() <= 0)) {
            msg_node->_session->Close();
            continue;
        }
        try {
            if (!login) {
                std::string expected;
                if (!RedisMgr::GetInstance()->Get(std::string(USERTOKENPREFIX) + std::to_string(msg_node->_session->GetUserId()), expected) ||
                    expected != msg_node->_session->AuthDigest()) {
                    msg_node->_session->Close();
                    continue;
                }
            }
            call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
                                  std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
        } catch (...) {
            std::cerr << "Chat request rejected" << std::endl;
            msg_node->_session->Close();
        }
    }
}

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session, const short& msg_id, const std::string& msg_data) {
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(msg_data, root) || !root.isObject() || !root["uid"].isInt() ||
        root["uid"].asInt() <= 0 || !root["token"].isString() ||
        root["token"].asString().empty() || root["token"].asString().size() > 256) {
        session->Close();
        return;
    }
    auto uid = root["uid"].asInt();
    auto token = root["token"].asString();

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
    if (token_value != PasswordSecurity::Digest(token)) {
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
    rtvalue["chat_protocol_version"] = 2;
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

    session->SetAuthDigest(token_value);
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
        redactProfile(item, apply.uid, uid);

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
        userinfo->name = root["name"].asString();
        userinfo->email = root["email"].asString();
        userinfo->nick = root["nick"].asString();
        userinfo->desc = root["desc"].asString();
        userinfo->gender = root["gender"].asInt();
        userinfo->icon = root["icon"].asString();
    } else {
        std::shared_ptr<UserInfo> user_info = nullptr;
        user_info = MysqlMgr::GetInstance()->GetUser(uid);
        if (user_info == nullptr) {
            return false;
        }
        userinfo = user_info;

        Json::Value redis_root;
        redis_root["uid"] = userinfo->uid;
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
    { std::lock_guard<std::mutex> lock(_mutex); _b_stop = true; }
    _consume.notify_one();
    _worker_thread.join();
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg) {
    std::unique_lock<std::mutex> unique_lk(_mutex);
    if (_b_stop || _msg_que.size() >= MAX_RECVQUE) {
        unique_lk.unlock();
        msg->_session->Close();
        return;
    }
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
    if (user && !MysqlMgr::GetInstance()->PrivacyAllows(user->uid, session->GetUserId(), "search_policy")) user.reset();
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
    if (!MysqlMgr::GetInstance()->PrivacyAllows(user->uid, session->GetUserId(), "profile_policy")) {
        response["icon"] = ""; response["desc"] = ""; response["nick"] = ""; response["gender"] = 0;
    }
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

    if (!MysqlMgr::GetInstance()->PrivacyAllows(toUid, fromUid, "request_policy")) {
        response["error"] = ErrorCodes::ChatNotFriend;
        response["result"] = -1;
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
    redactProfile(notify, fromUid, toUid);
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

void LogicSystem::GetFriendList(std::shared_ptr<CSession> session,
                               const short &, const std::string &body)
{
    Json::Value request, response;
    response["error"] = ErrorCodes::ChatDataInvalid;
    response["friend_list"] = Json::Value(Json::arrayValue);
    response["has_more"] = false;
    Defer reply([&] {
        session->Send(ChatWire::Compact(response), ID_FRIEND_LIST_RSP);
    });
    Json::Reader reader;
    if (body.size() > ChatWire::BodyLimit ||
        !reader.parse(body, request) || !request.isObject() ||
        !request["request_id"].isString() ||
        !ChatWire::Uuid(request["request_id"].asString()) ||
        !request["after_uid"].isInt())
        return;

    const int after = request["after_uid"].asInt();
    response["request_id"] = request["request_id"];
    response["after_uid"] = after;
    response["next_uid"] = after;
    const int uid = session->GetUserId();
    if (uid <= 0 || after < 0) return;

    const auto page = MysqlMgr::GetInstance()->GetFriendPage(uid, after, 10);
    if (!page.ok) {
        response["error"] = ErrorCodes::ChatDatabaseFailed;
        return;
    }
    response["error"] = ErrorCodes::Success;
    bool more = page.hasMore;
    for (const auto &f : page.items) {
        Json::Value item;
        item["uid"] = f.uid;
        item["name"] = f.name;
        item["nick"] = f.nick;
        item["icon"] = f.icon;
        item["remark"] = f.remark;
        item["gender"] = f.gender;
        redactProfile(item, f.uid, uid);

        Json::Value candidate = response;
        candidate["friend_list"].append(item);
        candidate["next_uid"] = f.uid;
        if (ChatWire::Compact(candidate).size() > ChatWire::BodyLimit) {
            if (response["friend_list"].empty())
                response["error"] = ErrorCodes::ChatDataInvalid;
            else
                more = true;
            break;
        }
        response = std::move(candidate);
    }
    response["has_more"] = response["error"].asInt() == 0 && more;
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session,
                                const short &, const std::string &body)
{
    Json::Value request, response;
    const int fromUid = session->GetUserId();
    response["error"] = ErrorCodes::ChatDataInvalid;
    response["fromuid"] = fromUid;
    response["touid"] = 0;
    response["msgid"] = "";
    auto reply = [&] { session->Send(ChatWire::Compact(response), ID_TEXT_CHAT_MSG_RSP); };
    Json::Reader reader;
    if (fromUid <= 0 || body.size() > ChatWire::BodyLimit ||
        !reader.parse(body, request) || !request.isObject() ||
        !request["touid"].isInt() || !request["text_array"].isArray() ||
        request["text_array"].size() != 1) { reply(); return; }
    const auto &item = request["text_array"][0];
    if (!item.isObject() || !item["msgid"].isString() || !item["content"].isString()) { reply(); return; }
    const auto id = item["msgid"].asString(), content = item["content"].asString();
    const int toUid = request["touid"].asInt();
    response["touid"] = toUid;
    if (ChatWire::Uuid(id)) response["msgid"] = id;
    // Reserve metadata and pagination overhead, including worst-case JSON escaping.
    Json::Value encoded; encoded["content"] = content;
    if (toUid <= 0 || toUid == fromUid || !ChatWire::Text(id, content) ||
        ChatWire::Compact(encoded).size() > 850) { reply(); return; }
    auto saved = MysqlMgr::GetInstance()->StoreTextMessage(fromUid, toUid, id, content);
    response["error"] = saved["error"];
    if (saved["error"].asInt() != 0 || !saved["message"].isObject()) { reply(); return; }
    response = saved["message"];
    response.removeMember("content");
    response["error"] = 0;
    reply(); // The commit, not the RPC result, defines successful submission.
    try {
        Json::Value notification;
        notification["error"] = 0;
        notification["message"] = saved["message"];
        const auto wire = ChatWire::Compact(notification);
        std::string targetServer;
        if (!RedisMgr::GetInstance()->Get(std::string(USERIPPREFIX) + std::to_string(toUid), targetServer)) return;
        if (targetServer == ConfigMgr::Inst().GetValue("SelfServer", "Name")) {
            const auto target = UserMgr::GetInstance()->GetSession(toUid);
            if (target) target->Send(wire, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
            return;
        }
        message::TextChatMsgReq rpc;
        rpc.set_fromuid(fromUid); rpc.set_touid(toUid);
        auto *text = rpc.add_textmsgs(); text->set_msgid(id); text->set_msgcontent(content);
        ChatGrpcClient::GetInstance()->NotifyTextChatMsg(targetServer, rpc, notification);
    } catch (...) {
        // History synchronization recovers missed notifications; never send a second reply.
        std::cerr << "Online chat notification unavailable\n";
    }
}

void LogicSystem::ChatSync(std::shared_ptr<CSession> session, const short &id, const std::string &body)
{
    Json::Value req, rsp; rsp["error"] = ChatDataInvalid;
    const int actor = session->GetUserId();
    Defer reply([&] { session->Send(ChatWire::Compact(rsp), static_cast<short>(id + 1)); });
    Json::Reader reader;
    if (actor <= 0 || body.size() > ChatWire::BodyLimit || !reader.parse(body, req) ||
        !req.isObject() || !req["request_id"].isString() ||
        !ChatWire::Uuid(req["request_id"].asString())) return;
    const std::string requestId = req["request_id"].asString();
    rsp["request_id"] = requestId;
    auto number = [](const Json::Value &value, std::uint64_t &out) {
        if (!value.isString()) return false;
        const auto s = value.asString();
        if (s.empty() || s.size() > 20) return false;
        const auto parsed = std::from_chars(s.data(), s.data() + s.size(), out);
        return parsed.ec == std::errc{} && parsed.ptr == s.data() + s.size();
    };
    std::uint64_t cursor = 0;
    auto mgr = MysqlMgr::GetInstance();
    if (id == ID_MESSAGE_DELETE_REQ) {
        if (!number(req["message_id"], cursor) || !cursor || !req["for_everyone"].isBool()) return;
        rsp = mgr->DeleteMessage(actor, cursor, req["for_everyone"].asBool());
    } else if (id == ID_DELETION_EVENTS_REQ) {
        if (!number(req["after_event"], cursor)) return;
        rsp = mgr->DeletionEvents(actor, cursor);
        rsp["after_event"] = req["after_event"];
    } else if (id == ID_CHAT_HISTORY_REQ) {
        if (!req["peer_uid"].isInt() || req["peer_uid"].asInt() <= 0 ||
            req["peer_uid"].asInt() == actor || !number(req["after_seq"], cursor)) return;
        rsp = mgr->ChatHistory(actor, req["peer_uid"].asInt(), cursor);
        rsp["peer_uid"] = req["peer_uid"]; rsp["after_seq"] = req["after_seq"];
    } else if (id == ID_CONVERSATION_LIST_REQ) {
        if (!number(req["after_thread"], cursor)) return;
        rsp = mgr->ChatConversations(actor, cursor);
        rsp["after_thread"] = req["after_thread"];
    } else if (id == ID_MESSAGE_RECEIPT_REQ) {
        if (!number(req["message_id"], cursor) || cursor == 0 || !req["receipt"].isString()) return;
        const auto state = req["receipt"].asString();
        if (state != "delivered" && state != "read") return;
        rsp = mgr->RecordReceipt(actor, cursor, state == "read");
    } else if (id == ID_MESSAGE_STATUS_REQ) {
        if (!req["msgids"].isArray() || req["msgids"].empty() || req["msgids"].size() > 4) return;
        std::vector<std::string> ids;
        for (const auto &v : req["msgids"]) {
            if (!v.isString() || v.asString().empty() || v.asString().size() > 64) return;
            ids.push_back(v.asString());
        }
        rsp = mgr->MessageStates(actor, ids);
    }
    rsp["request_id"] = requestId;
    if (ChatWire::Compact(rsp).size() > ChatWire::BodyLimit) {
        rsp = Json::Value{}; rsp["error"] = ChatDataInvalid; rsp["request_id"] = requestId;
    }
}
