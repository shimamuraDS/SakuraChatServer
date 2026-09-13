#include "MysqlDao.h"
#include "ChatWire.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace {
using Statement = std::unique_ptr<sql::PreparedStatement>;
using Rows = std::unique_ptr<sql::ResultSet>;
// Never return an open transaction or a poisoned connection to the pool.
struct Lease {
    MySqlPool &pool;
    std::unique_ptr<SqlConnection> holder;
    explicit Lease(MySqlPool &p) : pool(p), holder(p.getConnection()) {
        if (!holder) throw std::runtime_error("chat database unavailable");
    }
    ~Lease() {
        try {
            if (!holder->_con->getAutoCommit()) {
                holder->_con->rollback();
                holder->_con->setAutoCommit(true);
            }
            pool.returnConnection(std::move(holder));
        } catch (...) { /* discard rather than reusing an unsafe connection */ }
    }
    sql::Connection &db() { return *holder->_con; }
};
Json::Value result(int error = 0) { Json::Value r; r["error"] = error; return r; }
Statement prepare(sql::Connection &db, const std::string &sql) { return Statement(db.prepareStatement(sql)); }
const std::string columns =
    "SELECT m.message_id,m.thread_id,m.seq,m.client_msg_id,m.sender_uid,m.receiver_uid,"
    "CASE WHEN m.status=0 AND (e.expires_at IS NULL OR e.expires_at>CURRENT_TIMESTAMP(3)) THEN m.content ELSE '' END AS content,m.status,"
    "CASE WHEN e.expires_at<=CURRENT_TIMESTAMP(3) THEN 1 ELSE 0 END AS expired,"
    "CAST(UNIX_TIMESTAMP(m.created_at)*1000 AS UNSIGNED) AS created_ms,"
    "CAST(COALESCE(UNIX_TIMESTAMP(e.expires_at)*1000,0) AS UNSIGNED) AS expires_ms,"
    "CASE WHEN r.status=1 THEN 'read' WHEN r.status=0 THEN 'delivered' ELSE 'accepted' END AS state "
    "FROM chat_message m LEFT JOIN message_expiry e ON e.message_id=m.message_id LEFT JOIN message_receipt r ON r.message_id=m.message_id AND r.user_uid=m.receiver_uid ";
Json::Value row(sql::ResultSet &rs) {
    Json::Value m;
    m["message_id"] = std::to_string(rs.getUInt64("message_id"));
    m["thread_id"] = std::to_string(rs.getUInt64("thread_id"));
    m["seq"] = std::to_string(rs.getUInt64("seq"));
    m["msgid"] = rs.getString("client_msg_id").asStdString();
    m["fromuid"] = rs.getInt("sender_uid");
    m["touid"] = rs.getInt("receiver_uid");
    m["content"] = rs.getString("content").asStdString();
    m["message_status"] = rs.getInt("status");
    if (m["message_status"].asInt() == 0 && rs.getInt("expired") == 1) m["message_status"] = 2;
    m["created_at_ms"] = std::to_string(rs.getUInt64("created_ms"));
    m["expires_at_ms"] = std::to_string(rs.getUInt64("expires_ms"));
    m["state"] = rs.getString("state").asStdString();
    return m;
}
Json::Value lookup(sql::Connection &db, int sender, const std::string &id) {
    auto q = prepare(db, columns + "WHERE m.sender_uid=? AND m.client_msg_id=?");
    q->setInt(1, sender); q->setString(2, id);
    Rows rs(q->executeQuery());
    return rs->next() ? row(*rs) : Json::Value{};
}
void logFailure(const char *operation) { std::cerr << "Chat persistence failed: " << operation << '\n'; }
}

Json::Value MysqlDao::StoredText(int sender, const std::string &id) {
    try {
        Lease lease(*_pool);
        auto r = result(); r["message"] = lookup(lease.db(), sender, id); return r;
    } catch (...) { logFailure("lookup"); return result(ChatDatabaseFailed); }
}

Json::Value MysqlDao::StoreTextMessage(int sender, int receiver, const std::string &id, const std::string &text) {
    if (sender <= 0 || receiver <= 0 || sender == receiver || !ChatWire::Text(id, text))
        return result(ChatDataInvalid);
    for (int attempt = 0; attempt < 3; ++attempt) {
        try {
            Lease lease(*_pool); auto &db = lease.db();
            // An already accepted request stays accepted, even after unfriending.
            auto existing = lookup(db, sender, id);
            if (!existing.isNull()) {
                if (existing["touid"].asInt() != receiver ||
                    (existing["message_status"].asInt() == 0 && existing["content"].asString() != text)) return result(ChatDataInvalid);
                auto r = result(); r["message"] = existing; return r;
            }
            db.setAutoCommit(false);
            auto blocked = prepare(db, "SELECT 1 FROM user_block WHERE (owner_uid=? AND blocked_uid=?) OR (owner_uid=? AND blocked_uid=?) LIMIT 1");
            blocked->setInt(1, sender); blocked->setInt(2, receiver); blocked->setInt(3, receiver); blocked->setInt(4, sender);
            { Rows rs(blocked->executeQuery()); if (rs->next()) return result(ChatNotFriend); }
            auto friends = prepare(db, "SELECT f.friend_uid FROM friend f JOIN user a ON a.uid=f.self_uid "
                "JOIN user b ON b.uid=f.friend_uid WHERE f.self_uid=? AND f.friend_uid=? AND a.status=0 AND b.status=0");
            friends->setInt(1, sender); friends->setInt(2, receiver);
            { Rows rs(friends->executeQuery()); if (!rs->next()) return result(ChatNotFriend); }
            const int a = std::min(sender, receiver), b = std::max(sender, receiver);
            std::uint64_t thread = 0;
            auto find = prepare(db, "SELECT thread_id FROM private_chat WHERE user1_uid=? AND user2_uid=?");
            find->setInt(1, a); find->setInt(2, b);
            { Rows rs(find->executeQuery()); if (rs->next()) thread = rs->getUInt64(1); }
            if (!thread) {
                auto create = prepare(db, "INSERT INTO chat_thread(type) VALUES('private')"); create->executeUpdate();
                auto last = prepare(db, "SELECT LAST_INSERT_ID()");
                { Rows rs(last->executeQuery()); rs->next(); thread = rs->getUInt64(1); }
                auto pair = prepare(db, "INSERT INTO private_chat(thread_id,user1_uid,user2_uid) VALUES(?,?,?)");
                pair->setUInt64(1, thread); pair->setInt(2, a); pair->setInt(3, b); pair->executeUpdate();
            }
            auto lock = prepare(db, "SELECT last_seq,status FROM chat_thread WHERE id=? FOR UPDATE");
            lock->setUInt64(1, thread);
            std::uint64_t seq;
            { Rows rs(lock->executeQuery());
              if (!rs->next() || rs->getInt("status") != 0) return result(ChatDataInvalid);
              seq = rs->getUInt64("last_seq");
              if (seq == std::numeric_limits<std::uint64_t>::max()) return result(ChatDataInvalid);
              ++seq;
            }
            // A locking/current read sees the first request after waiting for the thread lock.
            auto duplicate = prepare(db, "SELECT message_id FROM chat_message WHERE sender_uid=? AND client_msg_id=? FOR UPDATE");
            duplicate->setInt(1, sender); duplicate->setString(2, id);
            bool found;
            { Rows rs(duplicate->executeQuery()); found = rs->next(); }
            if (found) { db.rollback(); db.setAutoCommit(true); continue; }
            auto insert = prepare(db, "INSERT INTO chat_message(thread_id,seq,client_msg_id,sender_uid,receiver_uid,message_type,content) VALUES(?,?,?,?,?,1,?)");
            insert->setUInt64(1, thread); insert->setUInt64(2, seq); insert->setString(3, id);
            insert->setInt(4, sender); insert->setInt(5, receiver); insert->setString(6, text); insert->executeUpdate();
            // No AUTO_INCREMENT in message_expiry, so LAST_INSERT_ID remains the message ID.
            auto expiry = prepare(db, "INSERT INTO message_expiry(message_id,expires_at) "
                "SELECT LAST_INSERT_ID(),TIMESTAMPADD(SECOND,seconds,CURRENT_TIMESTAMP(3)) FROM user_message_retention WHERE uid=? AND seconds>0");
            expiry->setInt(1, sender); expiry->executeUpdate();
            auto update = prepare(db, "UPDATE chat_thread SET last_seq=?,last_message_id=LAST_INSERT_ID() WHERE id=?");
            update->setUInt64(1, seq); update->setUInt64(2, thread); update->executeUpdate();
            db.commit(); db.setAutoCommit(true);
            auto r = result(); r["message"] = lookup(db, sender, id); return r;
        } catch (const sql::SQLException &e) {
            if ((e.getErrorCode() == 1062 || e.getErrorCode() == 1213 || e.getErrorCode() == 1205) && attempt < 2) continue;
            logFailure("store"); return result(ChatDatabaseFailed);
        } catch (...) { logFailure("store"); return result(ChatDatabaseFailed); }
    }
    return result(ChatDatabaseFailed);
}

Json::Value MysqlDao::DeleteMessage(int actor, std::uint64_t messageId, bool everyone) {
    if (actor <= 0 || !messageId) return result(ChatDataInvalid);
    try {
        Lease lease(*_pool); auto &db = lease.db(); db.setAutoCommit(false);
        auto target = prepare(db, "SELECT sender_uid,receiver_uid,client_msg_id FROM chat_message WHERE message_id=? FOR UPDATE");
        target->setUInt64(1, messageId);
        int sender = 0, receiver = 0; std::string clientId;
        { Rows rs(target->executeQuery());
          if (!rs->next()) return result(ChatDataInvalid);
          sender = rs->getInt("sender_uid"); receiver = rs->isNull("receiver_uid") ? 0 : rs->getInt("receiver_uid");
          clientId = rs->getString("client_msg_id").asStdString();
        }
        if ((actor != sender && actor != receiver) || (everyone && actor != sender)) return result(ChatDataInvalid);
        std::vector<int> owners{actor};
        if (everyone && receiver > 0 && receiver != actor) owners.push_back(receiver);
        std::sort(owners.begin(), owners.end());
        // Serialize all event assignment for each owner until commit. AUTO_INCREMENT alone
        // is unsafe here: a later committed event could otherwise hide an earlier transaction.
        for (int owner : owners) {
            auto ensure = prepare(db, "INSERT INTO message_delete_head(owner_uid,last_event) VALUES(?,0) ON DUPLICATE KEY UPDATE owner_uid=owner_uid");
            ensure->setInt(1, owner); ensure->executeUpdate();
            auto head = prepare(db, "SELECT last_event FROM message_delete_head WHERE owner_uid=? FOR UPDATE");
            head->setInt(1, owner); std::uint64_t seq;
            { Rows rs(head->executeQuery()); if (!rs->next()) return result(ChatDatabaseFailed); seq = rs->getUInt64(1); }
            auto exists = prepare(db, "SELECT event_seq FROM message_delete_event WHERE owner_uid=? AND message_id=? FOR UPDATE");
            exists->setInt(1, owner); exists->setUInt64(2, messageId);
            { Rows rs(exists->executeQuery()); if (rs->next()) continue; }
            if (seq == std::numeric_limits<std::uint64_t>::max()) return result(ChatDataInvalid);
            ++seq;
            auto insert = prepare(db, "INSERT INTO message_delete_event(owner_uid,event_seq,message_id,sender_uid,client_msg_id) VALUES(?,?,?,?,?)");
            insert->setInt(1, owner); insert->setUInt64(2, seq); insert->setUInt64(3, messageId);
            insert->setInt(4, sender); insert->setString(5, clientId); insert->executeUpdate();
            auto advance = prepare(db, "UPDATE message_delete_head SET last_event=? WHERE owner_uid=?");
            advance->setUInt64(1, seq); advance->setInt(2, owner); advance->executeUpdate();
        }
        if (everyone) {
            auto erase = prepare(db, "UPDATE chat_message SET content='',extra=NULL,status=2 WHERE message_id=?");
            erase->setUInt64(1, messageId); erase->executeUpdate();
            auto clearExpiry = prepare(db, "DELETE FROM message_expiry WHERE message_id=?");
            clearExpiry->setUInt64(1, messageId); clearExpiry->executeUpdate();
        }
        db.commit(); db.setAutoCommit(true);
        auto r = result(); r["message_id"] = std::to_string(messageId); r["for_everyone"] = everyone; return r;
    } catch (...) { logFailure("delete message"); return result(ChatDatabaseFailed); }
}

bool MysqlDao::ExpireMessages() {
    try {
        std::vector<std::pair<int,std::uint64_t>> due;
        {
            Lease lease(*_pool);
            auto q = prepare(lease.db(), "SELECT m.sender_uid,e.message_id FROM message_expiry e JOIN chat_message m ON m.message_id=e.message_id "
                "WHERE e.expires_at<=CURRENT_TIMESTAMP(3) ORDER BY e.expires_at,e.message_id LIMIT 20");
            Rows rs(q->executeQuery());
            while (rs->next()) due.emplace_back(rs->getInt(1),rs->getUInt64(2));
        }
        // Reuse the same participant authorization, locking, tombstone and body-clearing transaction.
        // Multiple nodes may select the same row; deletion is idempotent.
        bool ok = true;
        for (const auto &item : due) if (DeleteMessage(item.first,item.second,true)["error"].asInt() != 0) ok = false;
        return ok;
    } catch (...) { logFailure("expiry scan"); return false; }
}

Json::Value MysqlDao::DeletionEvents(int actor, std::uint64_t afterEvent) {
    if (actor <= 0) return result(ChatDataInvalid);
    try {
        Lease lease(*_pool);
        auto q = prepare(lease.db(), "SELECT event_seq,message_id,sender_uid,client_msg_id FROM message_delete_event WHERE owner_uid=? AND event_seq>? ORDER BY event_seq LIMIT 21");
        q->setInt(1, actor); q->setUInt64(2, afterEvent);
        Rows rs(q->executeQuery()); auto r = result();
        r["items"] = Json::Value(Json::arrayValue); r["next_event"] = std::to_string(afterEvent); r["has_more"] = false;
        while (rs->next()) {
            Json::Value item; item["event_seq"] = std::to_string(rs->getUInt64("event_seq"));
            item["message_id"] = std::to_string(rs->getUInt64("message_id"));
            item["sender_uid"] = rs->getInt("sender_uid"); item["msgid"] = rs->getString("client_msg_id").asStdString();
            auto candidate = r; candidate["items"].append(item); candidate["next_event"] = item["event_seq"];
            if (r["items"].size() >= 20 || ChatWire::Compact(candidate).size() > 1400) { r["has_more"] = true; break; }
            r = std::move(candidate);
        }
        return r;
    } catch (...) { logFailure("deletion events"); return result(ChatDatabaseFailed); }
}

Json::Value MysqlDao::ChatHistory(int actor, int peer, std::uint64_t afterSeq) {
    try {
        Lease lease(*_pool); auto &db = lease.db();
        auto r = result(); r["messages"] = Json::Value(Json::arrayValue);
        r["next_seq"] = std::to_string(afterSeq); r["has_more"] = false;
        auto historyColumns = columns;
        const auto contentAt = historyColumns.find("m.status=0");
        historyColumns.replace(contentAt, std::string("m.status=0").size(), "m.status=0 AND d.message_id IS NULL");
        const auto statusAt = historyColumns.find("m.status,");
        historyColumns.replace(statusAt, std::string("m.status,").size(), "CASE WHEN d.message_id IS NOT NULL THEN 3 ELSE m.status END AS status,");
        auto q = prepare(db, historyColumns + "LEFT JOIN message_delete_event d ON d.message_id=m.message_id AND d.owner_uid=? JOIN private_chat p ON p.thread_id=m.thread_id "
            "WHERE p.user1_uid=? AND p.user2_uid=? AND m.seq>? ORDER BY m.seq ASC LIMIT 21");
        q->setInt(1, actor); q->setInt(2, std::min(actor, peer)); q->setInt(3, std::max(actor, peer)); q->setUInt64(4, afterSeq);
        Rows rs(q->executeQuery());
        while (rs->next()) {
            auto m = row(*rs);
            // Imported legacy rows may exceed the current text protocol. Keep the
            // original in MySQL, return a placeholder, and do not strand the cursor.
            if (ChatWire::Compact(m).size() > 1250) {
                m["content"] = "[历史消息过长，暂无法显示；原文保留在服务器]";
                m["content_truncated"] = true;
            }
            auto candidate = r; candidate["messages"].append(m); candidate["next_seq"] = m["seq"];
            // Leave room for request_id, peer_uid and the other correlation fields.
            if (r["messages"].size() >= 20 || ChatWire::Compact(candidate).size() > 1450) {
                if (r["messages"].empty()) return result(ChatDataInvalid);
                r["has_more"] = true; break;
            }
            r = std::move(candidate);
        }
        return r;
    } catch (...) { logFailure("history"); return result(ChatDatabaseFailed); }
}

Json::Value MysqlDao::ChatConversations(int actor, std::uint64_t afterThread) {
    try {
        Lease lease(*_pool);
        auto q = prepare(lease.db(), "SELECT p.thread_id,IF(p.user1_uid=?,p.user2_uid,p.user1_uid) AS peer,t.last_seq "
            "FROM private_chat p JOIN chat_thread t ON t.id=p.thread_id "
            "WHERE (p.user1_uid=? OR p.user2_uid=?) AND p.thread_id>? ORDER BY p.thread_id LIMIT 21");
        q->setInt(1, actor); q->setInt(2, actor); q->setInt(3, actor); q->setUInt64(4, afterThread);
        Rows rs(q->executeQuery()); auto r = result(); r["items"] = Json::Value(Json::arrayValue);
        r["next_thread"] = std::to_string(afterThread); r["has_more"] = false;
        while (rs->next()) {
            Json::Value item; item["thread_id"] = std::to_string(rs->getUInt64("thread_id"));
            item["peer_uid"] = rs->getInt("peer"); item["last_seq"] = std::to_string(rs->getUInt64("last_seq"));
            auto candidate = r; candidate["items"].append(item); candidate["next_thread"] = item["thread_id"];
            if (r["items"].size() >= 20 || ChatWire::Compact(candidate).size() > 1450) { r["has_more"] = true; break; }
            r = std::move(candidate);
        }
        return r;
    } catch (...) { logFailure("conversations"); return result(ChatDatabaseFailed); }
}

Json::Value MysqlDao::RecordReceipt(int actor, std::uint64_t messageId, bool read) {
    try {
        const bool suppressed = read && !PrivacyAllows(actor, actor, "read_receipts");
        if (suppressed) read = false;
        Lease lease(*_pool); auto &db = lease.db(); db.setAutoCommit(false);
        auto check = prepare(db, "SELECT sender_uid FROM chat_message WHERE message_id=? AND receiver_uid=? FOR UPDATE");
        check->setUInt64(1, messageId); check->setInt(2, actor);
        { Rows rs(check->executeQuery()); if (!rs->next()) return result(ChatDataInvalid); }
        auto q = prepare(db, "INSERT INTO message_receipt(message_id,user_uid,status,read_at) "
            "VALUES(?,?,?,CASE WHEN ?=1 THEN CURRENT_TIMESTAMP(3) ELSE NULL END) "
            "ON DUPLICATE KEY UPDATE status=GREATEST(status,?),read_at=CASE WHEN ?=1 "
            "THEN COALESCE(read_at,CURRENT_TIMESTAMP(3)) ELSE read_at END");
        q->setUInt64(1, messageId); q->setInt(2, actor);
        for (int i = 3; i <= 6; ++i) q->setInt(i, read ? 1 : 0);
        q->executeUpdate();
        auto state = prepare(db, "SELECT status FROM message_receipt WHERE message_id=? AND user_uid=?");
        state->setUInt64(1, messageId); state->setInt(2, actor);
        auto r = result();
        { Rows rs(state->executeQuery()); rs->next(); r["state"] = rs->getInt(1) == 1 ? "read" : "delivered"; }
        db.commit(); db.setAutoCommit(true); r["message_id"] = std::to_string(messageId);
        r["read_suppressed"] = suppressed;
        return r;
    } catch (...) { logFailure("receipt"); return result(ChatDatabaseFailed); }
}

Json::Value MysqlDao::MessageStates(int actor, const std::vector<std::string> &ids) {
    if (ids.size() > 4) return result(ChatDataInvalid);
    try {
        Lease lease(*_pool); auto r = result(); r["items"] = Json::Value(Json::arrayValue);
        for (const auto &id : ids) {
            auto m = lookup(lease.db(), actor, id);
            if (m.isNull()) { m["msgid"] = id; m["state"] = "not_found"; }
            m.removeMember("content"); r["items"].append(m);
        }
        return r;
    } catch (...) { logFailure("states"); return result(ChatDatabaseFailed); }
}

bool MysqlDao::PrivacyAllows(int owner, int actor, const std::string &action) {
    if (owner <= 0 || actor <= 0) return false;
    try {
        Lease lease(*_pool); auto &db = lease.db();
        if (owner != actor) {
            auto q = prepare(db, "SELECT 1 FROM user_block WHERE (owner_uid=? AND blocked_uid=?) OR (owner_uid=? AND blocked_uid=?) LIMIT 1");
            q->setInt(1, owner); q->setInt(2, actor); q->setInt(3, actor); q->setInt(4, owner);
            Rows rs(q->executeQuery()); if (rs->next()) return false;
        }
        if (action == "message" || (owner == actor && action != "read_receipts")) return true;
        // Only internal, fixed column names may enter the statement.
        if (action != "search_policy" && action != "request_policy" && action != "profile_policy" && action != "read_receipts") return false;
        auto q = prepare(db, "SELECT " + action + " AS policy FROM user_privacy WHERE uid=?");
        q->setInt(1, owner);
        int policy = action == "profile_policy" || action == "read_receipts" ? 1 : 0;
        { Rows rs(q->executeQuery()); if (rs->next()) policy = rs->getInt("policy"); }
        if (action == "read_receipts") return policy != 0;
        if (policy == 0) return true;
        if (policy == 2) return false;
        auto f = prepare(db, "SELECT 1 FROM friend WHERE self_uid=? AND friend_uid=? LIMIT 1");
        f->setInt(1, owner); f->setInt(2, actor);
        Rows rs(f->executeQuery()); return rs->next();
    } catch (...) { logFailure("privacy authorization"); return false; }
}

Json::Value MysqlDao::PrivacyCommand(int actor, const Json::Value &request) {
    if (actor <= 0 || !request["action"].isString()) return result(ChatDataInvalid);
    try {
        Lease lease(*_pool); auto &db = lease.db();
        const auto action = request["action"].asString();
        if (action == "set_retention") {
            if (!request["seconds"].isInt()) return result(ChatDataInvalid);
            const int seconds = request["seconds"].asInt();
            if (seconds != 0 && seconds != 86400 && seconds != 604800 && seconds != 2592000) return result(ChatDataInvalid);
            auto q = prepare(db, "INSERT INTO user_message_retention(uid,seconds) VALUES(?,?) ON DUPLICATE KEY UPDATE seconds=VALUES(seconds)");
            q->setInt(1,actor); q->setInt(2,seconds); q->executeUpdate();
        } else if (action == "set") {
            for (const char *key : {"search_policy", "request_policy", "profile_policy"})
                if (!request[key].isInt() || request[key].asInt() < 0 || request[key].asInt() > 2) return result(ChatDataInvalid);
            if (!request["read_receipts"].isBool()) return result(ChatDataInvalid);
            auto q = prepare(db, "INSERT INTO user_privacy(uid,search_policy,request_policy,profile_policy,read_receipts) VALUES(?,?,?,?,?) "
                "ON DUPLICATE KEY UPDATE search_policy=VALUES(search_policy),request_policy=VALUES(request_policy),profile_policy=VALUES(profile_policy),read_receipts=VALUES(read_receipts)");
            q->setInt(1, actor); q->setInt(2, request["search_policy"].asInt()); q->setInt(3, request["request_policy"].asInt());
            q->setInt(4, request["profile_policy"].asInt()); q->setInt(5, request["read_receipts"].asBool() ? 1 : 0); q->executeUpdate();
        } else if (action == "block" || action == "unblock") {
            if (!request["uid"].isInt() || request["uid"].asInt() <= 0 || request["uid"].asInt() == actor) return result(ChatDataInvalid);
            auto q = prepare(db, action == "block" ?
                "INSERT INTO user_block(owner_uid,blocked_uid) VALUES(?,?) ON DUPLICATE KEY UPDATE blocked_uid=VALUES(blocked_uid)" :
                "DELETE FROM user_block WHERE owner_uid=? AND blocked_uid=?");
            q->setInt(1, actor); q->setInt(2, request["uid"].asInt()); q->executeUpdate();
        } else if (action != "get") return result(ChatDataInvalid);
        auto r = result();
        r["search_policy"] = 0; r["request_policy"] = 0; r["profile_policy"] = 1; r["read_receipts"] = true;
        r["retention_seconds"] = 0;
        auto retention = prepare(db,"SELECT seconds FROM user_message_retention WHERE uid=?"); retention->setInt(1,actor);
        { Rows rs(retention->executeQuery()); if (rs->next()) r["retention_seconds"] = rs->getInt(1); }
        auto q = prepare(db, "SELECT search_policy,request_policy,profile_policy,read_receipts FROM user_privacy WHERE uid=?");
        q->setInt(1, actor);
        { Rows rs(q->executeQuery()); if (rs->next()) {
            for (const char *key : {"search_policy", "request_policy", "profile_policy"}) r[key] = rs->getInt(key);
            r["read_receipts"] = rs->getInt("read_receipts") != 0;
        } }
        const int after = action == "get" && request["after_uid"].isInt() ? request["after_uid"].asInt() : 0;
        if (after < 0) return result(ChatDataInvalid);
        auto blocks = prepare(db, "SELECT blocked_uid FROM user_block WHERE owner_uid=? AND blocked_uid>? ORDER BY blocked_uid LIMIT 33");
        blocks->setInt(1, actor); blocks->setInt(2, after);
        r["blocked"] = Json::Value(Json::arrayValue); r["next_uid"] = after; r["has_more"] = false;
        Rows rs(blocks->executeQuery()); while (rs->next()) {
            if (r["blocked"].size() == 32) { r["has_more"] = true; break; }
            r["blocked"].append(rs->getInt("blocked_uid")); r["next_uid"] = rs->getInt("blocked_uid");
        }
        return r;
    } catch (...) { logFailure("privacy command"); return result(ChatDatabaseFailed); }
}
