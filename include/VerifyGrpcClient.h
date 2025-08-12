//
// Created by adachi on 25-8-8.
//

#ifndef VERIFYGRPCCLIENT_H
#define VERIFYGRPCCLIENT_H

#include <grpcpp/grpcpp.h>
#include "Singleton.h"
#include "const.h"
#include "message.grpc.pb.h"

using grpc::ClientContext;
using grpc::Status;
using grpc::Channel;

using message::GetVarifyRsp;
using message::GetVarifyReq;
using message::VarifyService;

class VerifyGrpcClient:public Singleton<VerifyGrpcClient>{
    friend class Singleton<VerifyGrpcClient>;
public:
    GetVarifyRsp GetVarifyCode(std::string email) {
        ClientContext context;
        GetVarifyRsp reply;
        GetVarifyReq request;
        request.set_email(email);

        Status status = stub_->GetVarifyCode(&context, request, &reply);
        if (status.ok()) {
            return reply;
        } else {
            std::cerr << "gRPC call failed: " << status.error_message() << std::endl;
            reply.set_error(ErrorCodes::RPCFailed);
            return reply;
        }
    }

private:
    VerifyGrpcClient() {
        std::shared_ptr<Channel> channel = grpc::CreateChannel("127.0.0.1:50051", grpc::InsecureChannelCredentials());
        stub_ = VarifyService::NewStub(channel);
    }
    std::unique_ptr<VarifyService::Stub> stub_;
};



#endif //VERIFYGRPCCLIENT_H
