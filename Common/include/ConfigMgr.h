//
// Created by adachi on 25-8-8.
//

#ifndef CONFIGMGR_H
#define CONFIGMGR_H

#include "const.h"

struct SectionInfo {
    SectionInfo() {}
    ~SectionInfo() {
        _section_datas.clear();
    }

    // 拷贝构造函数
    SectionInfo(const SectionInfo& src) {
        _section_datas = src._section_datas;
    }

    // 赋值运算符重载
    SectionInfo& operator = (const SectionInfo& src) {
        if (&src == this) {
            return *this;
        }

        this->_section_datas = src._section_datas;
        return *this;
    }

    std::map<std::string, std::string> _section_datas;
    // 重载下标运算符
    std::string operator[](const std::string& key) {
        if (_section_datas.find(key) == _section_datas.end()) {
            return "";
        }

        return _section_datas[key];
    }
};

class ConfigMgr {
public:
    ~ConfigMgr() {
        _config_map.clear();
    }

    SectionInfo operator[](const std::string& section) {
        if (_config_map.find(section) == _config_map.end()) {
            return SectionInfo();
        }

        return _config_map[section];
    }

    static ConfigMgr& Inst() {
        static ConfigMgr cfg_mgr;
        return cfg_mgr;
    }

    // 拷贝构造函数
    ConfigMgr(const ConfigMgr& src) {
        _config_map = src._config_map;
    }
    // 赋值运算符重载
    ConfigMgr& operator = (const ConfigMgr& src) {
        if (&src == this) {
            return *this;
        }

        _config_map = src._config_map;
        return *this;
    }
private:
    ConfigMgr();
    std::map<std::string, SectionInfo> _config_map;
};



#endif //CONFIGMGR_H
