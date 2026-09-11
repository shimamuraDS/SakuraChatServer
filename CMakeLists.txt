cmake_minimum_required(VERSION 3.20)
project(Server)

# 指定 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 打印日志
message(STATUS "Protobuf include dirs: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "gRPC include dirs: ${gRPC_INCLUDE_DIRS}")

# 添加子目录
add_subdirectory(Common)
add_subdirectory(StatusServer)
add_subdirectory(GateServer)
add_subdirectory(ChatServer)