//
// Created by adachi on 26-5-7.
//

#include "ChatServiceImpl.h"

#include "ChatWire.h"
#include "MysqlMgr.h"

ChatServiceImpl::ChatServiceImpl() {
}

grpc::Status ChatServiceImpl::NotifyAddFriend(grpc::ServerContext *, const message::AddFriendReq *request, message::AddFriendRsp *response) {
    response->set_applyuid(request->applyuid());
    response->set_touid(request->touid());
    response->set_error(ErrorCodes::Success);

    auto targetSession = UserMgr::GetInstance()->GetSession(request->touid());
    if (!targetSession)
        return grpc::Status::OK;

    Json::Value notify;
    notify["error"] = ErrorCodes::Success;
    notify["apply_id"] = Json::Int64(request->apply_id());
    notify["applyuid"] = request->applyuid();
    notify["name"] = request->name();
    notify["nick"] = request->nick();
    notify["desc"] = request->desc();
    notify["icon"] = request->icon();
    notify["gender"] = request->gender();
    notify["message"] = request->desc();

    targetSession->Send(notify.toStyledString(), ID_NOTIFY_ADD_FRIEND_REQ);
    return grpc::Status::OK;
}

grpc::Status ChatServiceImpl::NotifyAuthFriend(grpc::ServerContext *, const message::AuthFriendReq *request,
    message::AuthFriendRsp *response) {
    response->set_error(ErrorCodes::Success);
    response->set_fromuid(request->fromuid());
    response->set_touid(request->touid());

    const auto targetSession =
        UserMgr::GetInstance()->GetSession(request->touid());
    if (!targetSession)
        return grpc::Status::OK;

    Json::Value notify;
    notify["error"] = ErrorCodes::Success;
    notify["result"] = 0;
    notify["apply_id"] = Json::Int64(request->apply_id());
    notify["agree"] = request->agree();
    notify["peer_uid"] = request->fromuid();
    targetSession->Send(
        notify.toStyledString(), ID_NOTIFY_AUTH_FRIEND_REQ);
    return grpc::Status::OK;
}

grpc::Status ChatServiceImpl::NotifyTextChatMsg( grpc::ServerContext *, const message::TextChatMsgReq *request,
    message::TextChatMsgRsp *response)
{
    response->set_error(ErrorCodes::ChatDataInvalid);
    response->set_fromuid(request->fromuid());
    response->set_touid(request->touid());
    if (request->fromuid() <= 0 || request->touid() <= 0 ||
        request->fromuid() == request->touid() || request->textmsgs_size() != 1)
        return grpc::Status::OK;
    const auto &text = request->textmsgs(0);
    if (!ChatWire::Text(text.msgid(), text.msgcontent()))
        return grpc::Status::OK;
    if (!MysqlMgr::GetInstance()->FriendExists(request->fromuid(), request->touid())) {
        response->set_error(ErrorCodes::ChatNotFriend);
        return grpc::Status::OK;
    }
    const auto target = UserMgr::GetInstance()->GetSession(request->touid());
    if (!target) {
        response->set_error(ErrorCodes::ChatTargetOffline);
        return grpc::Status::OK;
    }
    const auto wire = ChatWire::Compact(ChatWire::Notification(
        request->fromuid(), request->touid(), text.msgid(), text.msgcontent()));
    if (wire.size() > ChatWire::BodyLimit) return grpc::Status::OK;
    target->Send(wire, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
    response->set_error(ErrorCodes::Success);
    return grpc::Status::OK;
}

bool ChatServiceImpl::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo) {
    return true;
}
