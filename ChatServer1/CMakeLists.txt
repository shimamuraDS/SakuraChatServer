cmake_minimum_required(VERSION 3.20)
add_definitions(-D_WIN32_WINNT=0x0601)
cmake_policy(SET CMP0167 NEW)
cmake_policy(SET CMP0074 NEW)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

project(ChatServer1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories("D:/environment/Redis/deps/hiredis")
link_directories("D:/environment/Redis/lib")

# MySQL Connector/C++
set(MYSQLCPP_ROOT "D:/environment/mysql-connector/mysql-connector-c++-9.4.0-winx64")

link_directories(${MYSQLCPP_ROOT}/lib64/debug/vs14)

add_executable(ChatServer1
        ChatServer.cpp
        src/CServer.cpp
        include/CServer.h
        src/CSession.cpp
        include/CSession.h
        src/LogicSystem.cpp
        include/LogicSystem.h
        src/MsgNode.cpp
        include/MsgNode.h
        src/UserMgr.cpp
        include/UserMgr.h
        src/MysqlMgr.cpp
        include/MysqlMgr.h
        src/ChatGrpcClient.cpp
        include/ChatGrpcClient.h
        src/ChatServiceImpl.cpp
        include/ChatServiceImpl.h
)

target_include_directories(ChatServer1 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../Common/include
)

target_link_libraries(ChatServer1 PRIVATE
        Common
        hiredis
        Win32_Interop
        mysqlcppconn
)

add_custom_command(TARGET ChatServer1 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "D:/environment/mysql-connector/mysql-connector-c++-9.4.0-winx64/lib64/debug/mysqlcppconn-10-vs14.dll"
        "D:/environment/mysql-connector/mysql-connector-c++-9.4.0-winx64/lib64/debug/mysqlcppconnx-2-vs14.dll"
        $<TARGET_FILE_DIR:ChatServer1>
)