//
// Created by adachi on 26-9-10.
//

#ifndef CHATWIRE_H
#define CHATWIRE_H

#pragma once
#include "const.h"
#include <cstddef>
#include <string>

namespace ChatWire {
    inline constexpr std::size_t BodyLimit = 1800;

    inline std::string Compact(const Json::Value &value) {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, value);
    }

    inline bool Uuid(const std::string &id) {
        if (id.size() != 36) return false;
        for (std::size_t i = 0; i < id.size(); ++i) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (id[i] != '-') return false;
            } else {
                const char c = id[i];
                if (!((c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) return false;
            }
        }
        return true;
    }

    inline bool Text(const std::string &id, const std::string &content) {
        return Uuid(id) && !content.empty() && content.size() <= 512;
    }

    inline Json::Value Notification(int fromUid, int toUid, const std::string &id, const std::string &content) {
        Json::Value root;
        root["error"] = ErrorCodes::Success;
        root["fromuid"] = fromUid;
        root["touid"] = toUid;
        root["text_array"] = Json::Value(Json::arrayValue);
        Json::Value item;
        item["msgid"] = id;
        item["content"] = content;
        root["text_array"].append(item);
        return root;
    }
}

#endif //CHATWIRE_H
