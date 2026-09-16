set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS thread system filesystem)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
find_path(MYSQL_INCLUDE_DIR mysql_driver.h PATH_SUFFIXES jdbc REQUIRED)
find_library(MYSQL_LIBRARY mysqlcppconn REQUIRED)

set(proto "${CMAKE_CURRENT_SOURCE_DIR}/VarifyServer/message.proto")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated}")
add_custom_command(
    OUTPUT "${generated}/message.pb.cc" "${generated}/message.pb.h"
           "${generated}/message.grpc.pb.cc" "${generated}/message.grpc.pb.h"
    COMMAND protobuf::protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/VarifyServer
        --cpp_out=${generated} --grpc_out=${generated}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin> ${proto}
    DEPENDS "${proto}" VERBATIM)
add_library(Common STATIC
    Common/src/ConfigMgr.cpp Common/src/AsioIOServicePool.cpp
    Common/src/StatusGrpcClient.cpp Common/src/MysqlDao.cpp
    Common/src/ChatPersistence.cpp Common/src/PrivateChatDao.cpp Common/src/RedisMgr.cpp
    "${generated}/message.pb.cc" "${generated}/message.grpc.pb.cc")
target_include_directories(Common PUBLIC "${generated}" Common/include "${MYSQL_INCLUDE_DIR}")
target_link_libraries(Common PUBLIC Boost::thread Boost::system Boost::filesystem
    gRPC::grpc++ protobuf::libprotobuf PkgConfig::JSONCPP PkgConfig::HIREDIS
    PkgConfig::SODIUM "${MYSQL_LIBRARY}" Threads::Threads)

add_executable(GateServer GateServer/GateServer.cpp GateServer/src/CServer.cpp
    GateServer/src/HttpConnection.cpp GateServer/src/LogicSystem.cpp
    GateServer/src/PrivateChatHttp.cpp GateServer/src/VerifyGrpcClient.cpp GateServer/src/MysqlMgr.cpp)
add_executable(StatusServer StatusServer/StatusServer.cpp
    StatusServer/src/StatusServiceImpl.cpp StatusServer/src/ChatGrpcClient.cpp)
add_executable(ChatServer ChatServer/ChatServer.cpp ChatServer/src/CServer.cpp
    ChatServer/src/CSession.cpp ChatServer/src/LogicSystem.cpp ChatServer/src/MsgNode.cpp
    ChatServer/src/UserMgr.cpp ChatServer/src/MysqlMgr.cpp
    ChatServer/src/ChatGrpcClient.cpp ChatServer/src/ChatServiceImpl.cpp)
foreach(service GateServer StatusServer ChatServer)
    target_include_directories(${service} PRIVATE "${service}/include")
    target_link_libraries(${service} PRIVATE Common)
    install(TARGETS ${service} RUNTIME DESTINATION bin)
endforeach()
enable_testing()
add_executable(privatechat_wire_tests tests/privatechat_wire_tests.cpp)
target_include_directories(privatechat_wire_tests PRIVATE Common/include)
target_link_libraries(privatechat_wire_tests PRIVATE PkgConfig::JSONCPP)
add_test(NAME privatechat_wire COMMAND privatechat_wire_tests)
add_executable(asio_pool_tests tests/asio_pool_tests.cpp)
target_link_libraries(asio_pool_tests PRIVATE Common)
add_test(NAME asio_pool_shutdown COMMAND asio_pool_tests)
add_executable(client_address_tests tests/client_address_tests.cpp)
target_include_directories(client_address_tests PRIVATE GateServer/include)
target_link_libraries(client_address_tests PRIVATE Boost::system)
add_test(NAME trusted_proxy_address COMMAND client_address_tests)
