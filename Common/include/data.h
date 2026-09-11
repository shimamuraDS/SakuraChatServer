//
// Created by adachi on 26-3-12.
//

#ifndef DATA_H
#define DATA_H

#include <string>

struct UserInfo {
    UserInfo():name(""), pwd(""),uid(0),email(""),nick(""),desc(""),gender(0), icon(""), back("") {}
    std::string name;
    std::string pwd;
    int uid;
    std::string email;
    std::string nick;
    std::string desc;
    int gender;
    std::string icon;
    std::string back;
};

struct ApplyInfo {
    ApplyInfo(int uid, std::string name, std::string desc, std::string icon, std::string nick, int gender, int status)
        :_uid(uid),_name(name),_desc(desc), _icon(icon),_nick(nick),_gender(gender),_status(status){}

    int _uid;
    std::string _name;
    std::string _desc;
    std::string _icon;
    std::string _nick;
    int _gender;
    int _status;
};

struct FriendApplyResult {
    int result = -1;
    std::int64_t applyId = 0;
};

struct PendingFriendApplyInfo {
    std::int64_t applyId = 0;
    int uid = 0;              // 申请人 UID
    std::string name;
    std::string nick;
    std::string descs;
    std::string icon;
    int gender = 0;
    int status = 0;
};

struct ResolveFriendApplyResult {
    int result = -1;
    int fromUid = 0;
    int toUid = 0;
};

struct FriendInfo {
    int uid = 0;
    std::string name, nick, icon, remark;
    int gender = 0;
};

struct FriendPageResult {
    bool ok = false;
    bool hasMore = false;
    std::vector<FriendInfo> items;
};

#endif //DATA_H
