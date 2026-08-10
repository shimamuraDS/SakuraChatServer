//
// Created by adachi on 26-5-7.
//

#ifndef CHATSERVICEIMPL_H
#define CHATSERVICEIMPL_H

#include "const.h"
#include "data.h"

class ChatServiceImpl final : public message::ChatService::Service {
public:
    ChatServiceImpl();
    grpc::Status NotifyAddFriend(grpc::ServerContext* context, const message::AddFriendReq* request, message::AddFriendRsp* response) override;
    grpc::Status NotifyAuthFriend(grpc::ServerContext* context, const message::AuthFriendReq* request, message::AuthFriendRsp* response) override;
    grpc::Status NotifyTextChatMsg(grpc::ServerContext* context, const message::TextChatMsgReq* request, message::TextChatMsgRsp* response) override;

    bool GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo>& userinfo);
};



#endif //CHATSERVICEIMPL_H
