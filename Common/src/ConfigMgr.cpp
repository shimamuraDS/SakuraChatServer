//
// Created by adachi on 25-8-8.
//

#include "ConfigMgr.h"
#include <cstdlib>
#include <stdexcept>

ConfigMgr::ConfigMgr() {
    const char *mode = std::getenv("SAKURA_SECURITY_MODE");
    const std::string securityMode = mode ? mode : "development";
    if (securityMode != "development" && securityMode != "production") throw std::runtime_error("Invalid SAKURA_SECURITY_MODE");
    _production = securityMode == "production";
    boost::filesystem::path current_path = boost::filesystem::current_path();
    boost::filesystem::path config_path = current_path / "config.ini";
    if (const char *configured = std::getenv("SAKURA_CONFIG_PATH")) {
        if (!*configured) throw std::runtime_error("Empty SAKURA_CONFIG_PATH");
        config_path = configured;
    }
    std::cout << "Config path : " << config_path << std::endl;

    boost::property_tree::ptree pt;
    try { boost::property_tree::read_ini(config_path.string(), pt); }
    catch (...) { throw std::runtime_error("Cannot read configuration INI; check path and syntax"); }

    // 遍历配置文件的每个section
    for (const auto& section_pair : pt) {
        const ::std::string& section_name = section_pair.first;
        const boost::property_tree::ptree& section_tree = section_pair.second;
        std::map<std::string, std::string> section_config;
        for (const auto& key_value_pair : section_tree) {
            const std::string& key = key_value_pair.first;
            const std::string& value = key_value_pair.second.get_value<std::string>();
            section_config[key] = value;
        }

        // 将section配置存入_config_map
        SectionInfo sectionInfo;
        sectionInfo._section_datas = section_config;
        _config_map[section_name] = sectionInfo;
    }

    auto inject = [this](const char *section, const char *key, const char *variable, bool secret) {
        const char *value = std::getenv(variable);
        if (value) {
            if (!*value) throw std::runtime_error(std::string("Empty environment variable: ") + variable);
            _config_map[section]._section_datas[key] = value;
        } else if (_production && secret) {
            // No production fallback to credentials stored in the repository.
            _config_map[section]._section_datas[key].clear();
        }
    };
    inject("MySQL", "User", "SAKURA_MYSQL_USER", true);
    inject("MySQL", "Password", "SAKURA_MYSQL_PASSWORD", true);
    inject("MySQL", "Host", "SAKURA_MYSQL_HOST", false);
    inject("MySQL", "Port", "SAKURA_MYSQL_PORT", false);
    inject("MySQL", "Schema", "SAKURA_MYSQL_SCHEMA", false);
    inject("Redis", "Password", "SAKURA_REDIS_PASSWORD", true);
    inject("Redis", "Host", "SAKURA_REDIS_HOST", false);
    inject("Redis", "Port", "SAKURA_REDIS_PORT", false);
    if (_production && GetValue("Redis", "Password").empty()) throw std::runtime_error("Missing SAKURA_REDIS_PASSWORD");

    // Log names only, never values (including environment overrides).
    for (const auto& section_entry : _config_map) {
        const std::string& section_name = section_entry.first;
        SectionInfo section_config = section_entry.second;
        std::cout << "[" << section_name << "]" << std::endl;
        for (const auto& key_value_pair : section_config._section_datas) {
            std::cout << key_value_pair.first << "=<configured>" << std::endl;
        }
    }
}

void ConfigMgr::RequireDatabaseCredentials() {
    if (_production && (GetValue("MySQL", "User").empty() || GetValue("MySQL", "Password").empty()))
        throw std::runtime_error("Missing SAKURA_MYSQL_USER or SAKURA_MYSQL_PASSWORD");
}

std::string ConfigMgr::GetValue(const std::string& section, const std::string& key) {
    if (_config_map.find(section) == _config_map.end()) {
        return "";
    }

    return _config_map[section].GetValue(key);
}
