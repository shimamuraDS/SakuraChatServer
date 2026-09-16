#include "MysqlDao.h"
#include "PrivateChatWire.h"
#include "ChatWire.h"
#include "PasswordSecurity.h"

namespace {
auto prepare(sql::Connection &db, const std::string &sql, std::initializer_list<std::string> args = {}) {
    std::unique_ptr<sql::PreparedStatement> statement(db.prepareStatement(sql));
    unsigned index = 1;
    for (const auto &arg : args) statement->setString(index++, arg);
    return statement;
}
Json::Value parse(const std::string &text) {
    Json::Value value; Json::Reader reader;
    if (!reader.parse(text, value)) throw std::runtime_error("invalid private record");
    return value;
}
Json::Value error(int code) { Json::Value result; result["error"] = code; return result; }
}

Json::Value MysqlDao::PrivateChatCommand(int actor, const Json::Value &request) {
    if (actor <= 0 || !PrivateChatWire::valid(request)) return error(1001);
    auto connection = _pool->getConnection();
    if (!connection) return error(1104);
    Defer release([&] { _pool->returnConnection(std::move(connection)); });
    auto &db = *connection->_con;
    try {
        db.setAutoCommit(false);
        Defer transaction([&] { try { db.rollback(); db.setAutoCommit(true); } catch (...) {} });
        const auto uid = std::to_string(actor);
        const auto identity = ChatWire::Compact(request["identity"]);
        const auto op = request["op"].asString();
        auto active = prepare(db, "SELECT 1 FROM user WHERE uid=? AND status=0", {uid});
        std::unique_ptr<sql::ResultSet> account(active->executeQuery());
        if (!account->next()) return error(1010);
        auto device = prepare(db, "SELECT identity_key FROM private_device WHERE uid=? FOR UPDATE", {uid});
        std::unique_ptr<sql::ResultSet> row(device->executeQuery());
        if (!row->next()) {
            if (op != "register") return error(1201);
            auto insert = prepare(db, "INSERT INTO private_device(uid,identity_key) VALUES(?,?)", {uid, identity});
            insert->executeUpdate();
        } else if (row->getString(1) != identity) return error(1202);
        Json::Value result = error(0);
        const auto peer = request.isMember("peer") ? std::to_string(request["peer"].asInt()) : std::string();
        std::string peerIdentity;
        if (!peer.empty()) {
            if (peer == uid) return error(1001);
            auto allowed = prepare(db, "SELECT 1 FROM friend f JOIN user u ON u.uid=f.friend_uid WHERE f.self_uid=? AND f.friend_uid=? AND u.status=0 AND NOT EXISTS(SELECT 1 FROM user_block WHERE (owner_uid=? AND blocked_uid=?) OR (owner_uid=? AND blocked_uid=?))", {uid, peer, uid, peer, peer, uid});
            std::unique_ptr<sql::ResultSet> permission(allowed->executeQuery());
            if (!permission->next()) return error(1101);
            auto lookup = prepare(db, "SELECT identity_key FROM private_device WHERE uid=? FOR UPDATE", {peer});
            std::unique_ptr<sql::ResultSet> target(lookup->executeQuery());
            if (!target->next()) return error(1201);
            peerIdentity = target->getString(1);
        }
        if (op == "register") {}
        else if (op == "publish") {
            const auto id = std::to_string(request["bundle"]["id"].asUInt());
            const auto body = ChatWire::Compact(request["bundle"]);
            auto existing = prepare(db, "SELECT bundle FROM private_prekey WHERE owner_uid=? AND key_id=? FOR UPDATE", {uid, id});
            std::unique_ptr<sql::ResultSet> saved(existing->executeQuery());
            if (saved->next()) { if (saved->getString(1) != body) return error(1203); }
            else {
                auto count = prepare(db, "SELECT COUNT(*) FROM private_prekey WHERE owner_uid=? FOR UPDATE", {uid});
                std::unique_ptr<sql::ResultSet> number(count->executeQuery()); number->next();
                if (number->getInt(1) >= 100) return error(1208);
                prepare(db, "INSERT INTO private_prekey(owner_uid,key_id,bundle) VALUES(?,?,?)", {uid, id, body})->executeUpdate();
            }
        } else if (op == "identity") result["identity"] = parse(peerIdentity);
        else if (op == "claim") {
            const auto requestId = request["request_id"].asString();
            // A locking read observes the committed result after waiting for device locks,
            // rather than an older REPEATABLE READ snapshot created by permission checks.
            auto previous = prepare(db, "SELECT bundle FROM private_prekey WHERE owner_uid=? AND claimant_uid=? AND claim_id=? FOR UPDATE", {peer, uid, requestId});
            std::unique_ptr<sql::ResultSet> claimed(previous->executeQuery());
            if (claimed->next()) result["bundle"] = parse(claimed->getString(1));
            else {
                auto available = prepare(db, "SELECT key_id,bundle FROM private_prekey WHERE owner_uid=? AND claimant_uid IS NULL ORDER BY key_id LIMIT 1 FOR UPDATE", {peer});
                std::unique_ptr<sql::ResultSet> key(available->executeQuery());
                if (!key->next()) return error(1205);
                result["bundle"] = parse(key->getString(2));
                prepare(db, "UPDATE private_prekey SET claimant_uid=?,claim_id=? WHERE owner_uid=? AND key_id=?", {uid, requestId, peer, key->getString(1)})->executeUpdate();
            }
        } else if (op == "send") {
            if (ChatWire::Compact(request["peer_identity"]) != peerIdentity) return error(1202);
            const auto id = request["id"].asString(), cipher = request["ciphertext"].asString();
            const auto kind = std::to_string(request["kind"].asInt());
            const auto digest = PasswordSecurity::Digest(peer + ":" + identity + ":" + peerIdentity + ":" + kind + ":" + cipher);
            auto existing = prepare(db, "SELECT digest FROM private_envelope WHERE sender_uid=? AND message_id=? FOR UPDATE", {uid, id});
            std::unique_ptr<sql::ResultSet> duplicate(existing->executeQuery());
            if (duplicate->next()) { if (duplicate->getString(1) != digest) return error(1203); }
            else {
                auto count = prepare(db, "SELECT COUNT(*) FROM private_envelope WHERE recipient_uid=? AND acknowledged=0 FOR UPDATE", {peer});
                std::unique_ptr<sql::ResultSet> number(count->executeQuery()); number->next();
                if (number->getInt(1) >= 1000) return error(1209);
                prepare(db, "INSERT INTO private_envelope(sender_uid,recipient_uid,message_id,sender_identity,recipient_identity,kind,ciphertext,digest) VALUES(?,?,?,?,?,?,?,?)", {uid, peer, id, identity, peerIdentity, kind, cipher, digest})->executeUpdate();
            }
        } else if (op == "poll") {
            auto next = prepare(db, "SELECT e.sequence,e.sender_uid,e.message_id,e.sender_identity,e.kind,e.ciphertext FROM private_envelope e WHERE e.recipient_uid=? AND e.acknowledged=0 AND e.recipient_identity=? AND NOT EXISTS(SELECT 1 FROM user_block WHERE (owner_uid=e.sender_uid AND blocked_uid=e.recipient_uid) OR (owner_uid=e.recipient_uid AND blocked_uid=e.sender_uid)) ORDER BY e.sequence LIMIT 1", {uid, identity});
            std::unique_ptr<sql::ResultSet> message(next->executeQuery());
            if (message->next()) {
                auto &item = result["envelope"];
                item["sequence"] = message->getString(1).asStdString(); item["sender"] = message->getInt(2);
                item["id"] = message->getString(3).asStdString(); item["identity"] = parse(message->getString(4));
                item["kind"] = message->getInt(5); item["ciphertext"] = message->getString(6).asStdString();
            }
        } else if (op == "ack") {
            prepare(db, "UPDATE private_envelope SET acknowledged=1,ciphertext=NULL WHERE sequence=? AND recipient_uid=? AND recipient_identity=?", {request["sequence"].asString(), uid, identity})->executeUpdate();
        }
        if (op == "register" || op == "publish") {
            auto count = prepare(db, "SELECT COUNT(*) FROM private_prekey WHERE owner_uid=? AND claimant_uid IS NULL FOR UPDATE", {uid});
            std::unique_ptr<sql::ResultSet> available(count->executeQuery());
            if (!available->next()) return error(1104);
            result["available_prekeys"] = available->getInt(1);
        }
        db.commit();
        return result;
    } catch (...) { return error(1104); }
}
