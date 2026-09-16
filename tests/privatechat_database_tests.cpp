#include "MysqlDao.h"
#include "ConfigMgr.h"
#include <fstream>
#include <sstream>
#include <future>
#include <random>
#include "PrivateChatHttp.h"
#include "PasswordSecurity.h"
#include "ChatWire.h"
#include "RedisMgr.h"

namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
Json::Value bytes(int count, int value) { Json::Value result(Json::arrayValue); while (count--) result.append(value); return result; }
Json::Value request(const char *op, int actor) {
    Json::Value r; r["op"] = op; r["identity"] = bytes(33, actor);
    r["request_id"] = "d987030a-e41c-4fde-bca9-71c2ce109111";
    return r;
}
Json::Value publish(int actor) {
    auto r = request("publish", actor); auto &b = r["bundle"];
    b["identity"] = r["identity"]; b["registration"] = 1; b["id"] = 1;
    b["prekey"] = bytes(33, 5); b["signed"] = bytes(33, 5); b["signature"] = bytes(64, 5);
    b["kyber"] = bytes(1569, 5); b["kyber_signature"] = bytes(64, 5);
    return r;
}
void success(const Json::Value &r) {
    if (r["error"].asInt() != 0) throw std::runtime_error("DAO operation rejected, code " + std::to_string(r["error"].asInt()));
}
Json::Value httpRequest(const Json::Value &body, const PrivateChatHttp::Dependencies &dependencies) {
    net::io_context context;
    tcp::acceptor listener(context, {net::ip::address_v4::loopback(), 0});
    tcp::socket client(context);
    client.connect(listener.local_endpoint());
    auto serving = std::async(std::launch::async, [&] {
        tcp::socket socket(context); listener.accept(socket);
        beast::flat_buffer buffer;
        http::request_parser<http::string_body> parser; parser.body_limit(96 * 1024);
        http::read(socket, buffer, parser);
        const auto request = parser.release();
        check(request.method() == http::verb::post && request.target() == "/private/v1", "private HTTP endpoint");
        const auto result = PrivateChatHttp::Handle(request.body(), socket.remote_endpoint().address().to_string(), dependencies);
        http::response<http::string_body> response{http::status::ok, 11};
        response.set(http::field::content_type, "application/json");
        response.body() = ChatWire::Compact(result); response.prepare_payload();
        http::write(socket, response);
    });
    http::request<http::string_body> request{http::verb::post, "/private/v1", 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::content_type, "application/json");
    request.body() = ChatWire::Compact(body); request.prepare_payload();
    http::write(client, request);
    beast::flat_buffer buffer; http::response<http::string_body> response;
    http::read(client, buffer, response); serving.get();
    Json::Reader reader; Json::Value result;
    check(reader.parse(response.body(), result), "HTTP response JSON"); return result;
}
void authenticationTests(MysqlDao &dao) {
    int databaseCalls = 0;
    bool allowed = true, sessionExists = true;
    PrivateChatHttp::Dependencies dependencies;
    dependencies.allowRequest = [&](const std::string &, int, int) {
        return RateLimitResult{allowed ? RateLimitResult::State::Allowed : RateLimitResult::State::Limited, 17};
    };
    dependencies.tokenDigest = [&](int actor, std::string &digest) {
        if (!sessionExists || (actor != 1 && actor != 2)) return false;
        digest = PasswordSecurity::Digest(std::string(64, actor == 2 ? 'a' : 'b')); return true;
    };
    dependencies.command = [&](int actor, const Json::Value &command) {
        ++databaseCalls;
        check(!command.isMember("uid") && !command.isMember("token"), "credentials stripped before DAO");
        return dao.PrivateChatCommand(actor, command);
    };
    auto authenticated = request("register", 2);
    authenticated["uid"] = 2; authenticated["token"] = std::string(64, 'a');
    auto call = [&](const Json::Value &body) { return httpRequest(body, dependencies); };
    const auto registered = call(authenticated); success(registered);
    check(databaseCalls == 1 && registered["request_id"] == authenticated["request_id"], "authenticated request reaches isolated DAO");
    auto bad = authenticated; bad["token"] = std::string(64, 'b');
    check(call(bad)["error"].asInt() == 1010, "invalid token denied");
    bad = authenticated; bad["uid"] = 3;
    check(call(bad)["error"].asInt() == 1010, "token bound to account");
    sessionExists = false;
    check(call(authenticated)["error"].asInt() == 1010, "expired session denied");
    sessionExists = true; allowed = false;
    const auto limited = call(authenticated);
    check(limited["error"].asInt() == 1204 && limited["retry_after"].asInt() == 17, "rate limiting carries cooldown");
    const auto limiter = dependencies.allowRequest;
    dependencies.allowRequest = [](const std::string &, int, int) { return RateLimitResult{}; };
    check(call(authenticated)["error"].asInt() == 1207, "Redis failure is not rate limiting");
    dependencies.allowRequest = limiter;
    check(databaseCalls == 1, "rejected authentication never reaches DAO");
    allowed = true;
    bad = authenticated; bad["plaintext"] = "must not be stored";
    check(call(bad)["error"].asInt() == 1001, "plaintext rejected at protocol boundary");
    auto authorized = [&](Json::Value command, int actor) {
        command["uid"] = actor; command["token"] = std::string(64, actor == 2 ? 'a' : 'b'); return call(command);
    };
    auto published = publish(2); published["bundle"]["id"] = 2;
    success(authorized(published, 2));
    auto claimed = request("claim", 1); claimed["peer"] = 2;
    claimed["request_id"] = "d987030a-e41c-4fde-bca9-71c2ce109113";
    const auto bundle = authorized(claimed, 1); success(bundle);
    check(bundle["bundle"]["id"].asInt() == 2, "HTTP authenticated prekey claim");
    auto sent = request("send", 2); sent["peer"] = 1; sent["peer_identity"] = bytes(33, 1);
    sent["id"] = "d987030a-e41c-4fde-bca9-71c2ce109114"; sent["kind"] = 3; sent["ciphertext"] = "BAUG";
    success(authorized(sent, 2)); success(authorized(sent, 2));
    const auto inbox = authorized(request("poll", 1), 1); success(inbox);
    check(inbox["envelope"]["id"] == sent["id"] && inbox["envelope"]["ciphertext"] == sent["ciphertext"], "HTTP ciphertext arrives unchanged");
    auto confirmed = request("ack", 1); confirmed["sequence"] = inbox["envelope"]["sequence"];
    success(authorized(confirmed, 1));
    check(!authorized(request("poll", 1), 1).isMember("envelope"), "HTTP acknowledged inbox empty");
    dependencies.command = [](int, const Json::Value &) -> Json::Value { throw std::runtime_error("dependency failed"); };
    const auto failed = call(authenticated);
    check(failed["error"].asInt() == 1104 && failed["request_id"] == authenticated["request_id"], "dependency failure retains request correlation");
    std::cout << "Private authentication and isolated DAO tests passed\n";
}
}
int main(int argc, char **argv) {
    const bool provision = argc == 2 && std::string(argv[1]) == "--provision-isolated";
    if (argc != 2 || (!provision && std::string(argv[1]) != "--run-isolated")) {
        std::cerr << "Pass --run-isolated to create and remove a temporary test database.\n"; return 2;
    }
    std::unique_ptr<sql::Connection> admin;
    std::string schema;
    bool created = false;
    auto cleanup = [&] {
        if (!created) return;
        // Only the database successfully created by this process is eligible for removal.
        std::unique_ptr<sql::Statement> statement(admin->createStatement());
        statement->execute("DROP DATABASE `" + schema + "`"); created = false;
        std::cout << "Removed isolated test database: " << schema << std::endl;
    };
    try {
        auto &cfg = ConfigMgr::Inst(); cfg.RequireDatabaseCredentials();
        const auto url = cfg["MySQL"]["Host"] + ":" + cfg["MySQL"]["Port"];
        const auto user = cfg["MySQL"]["User"], password = cfg["MySQL"]["Password"];
        admin.reset(sql::mysql::get_mysql_driver_instance()->connect(url, user, password));
        schema = "sakura_private_test_" + std::to_string(std::random_device{}()) + "_" + std::to_string(std::random_device{}());
        std::unique_ptr<sql::Statement> sql(admin->createStatement());
        sql->execute("CREATE DATABASE `" + schema + "`"); created = true; admin->setSchema(schema);
        std::cout << "Created isolated test database: " << schema << '\n';
        sql->execute("CREATE TABLE user(uid INT UNSIGNED PRIMARY KEY,status INT NOT NULL DEFAULT 0) ENGINE=InnoDB");
        sql->execute("CREATE TABLE friend(self_uid INT UNSIGNED,friend_uid INT UNSIGNED,PRIMARY KEY(self_uid,friend_uid)) ENGINE=InnoDB");
        sql->execute("CREATE TABLE user_block(owner_uid INT UNSIGNED,blocked_uid INT UNSIGNED,PRIMARY KEY(owner_uid,blocked_uid)) ENGINE=InnoDB");
        std::ifstream migration(PRIVATE_MIGRATION_PATH); check(bool(migration), "migration file unavailable");
        std::stringstream source; source << migration.rdbuf();
        for (int pass = 0; pass < 2; ++pass) {
            std::stringstream statements(source.str()); std::string statement;
            while (std::getline(statements, statement, ';')) if (statement.find("CREATE TABLE") != std::string::npos) sql->execute(statement);
        }
        sql->execute("INSERT INTO user(uid) VALUES(1),(2),(3)");
        sql->execute("INSERT INTO friend VALUES(1,2),(2,1)");
        if (provision) {
            check(ConfigMgr::Inst()["Redis"]["Host"] == "127.0.0.1", "fixture requires isolated loopback Redis");
            check(std::getenv("SAKURA_TEST_ISOLATED_REDIS") != nullptr, "explicit isolated Redis fixture required");
            {
                const auto host = cfg["Redis"]["Host"], secret = cfg["Redis"]["Password"];
                RedisConPool pool(1, host.c_str(), std::stoi(cfg["Redis"]["Port"]), secret.c_str());
                auto *connection = pool.getConnection(); check(connection != nullptr, "connect isolated Redis");
                Defer returned([&] { pool.returnConnction(connection); });
                for (int uid = 1; uid <= 2; ++uid) {
                    const auto key = std::string(USERTOKENPREFIX) + std::to_string(uid);
                    const auto digest = PasswordSecurity::Digest(std::string(64, uid == 1 ? 'a' : 'b'));
                    auto *reply = static_cast<redisReply *>(redisCommand(connection, "SET %b %b EX 120", key.data(), key.size(), digest.data(), digest.size()));
                    const bool saved = reply && reply->type == REDIS_REPLY_STATUS;
                    if (reply) freeReplyObject(reply);
                    check(saved, "seed isolated test session");
                }
            }
            std::cout << "READY_SCHEMA=" << schema << std::endl;
            std::string finish; std::getline(std::cin, finish);
            cleanup(); return 0;
        }
        {
            MysqlDao dao(std::make_unique<MySqlPool>(url, user, password, schema, 5));
            std::cout << "Testing device registration\n";
            success(dao.PrivateChatCommand(1, request("register", 1)));
            success(dao.PrivateChatCommand(2, request("register", 2)));
            success(dao.PrivateChatCommand(3, request("register", 3)));
            auto wrong = request("register", 1); wrong["identity"] = bytes(33, 8);
            check(dao.PrivateChatCommand(1, wrong)["error"].asInt() == 1202, "identity overwrite rejected");
            success(dao.PrivateChatCommand(2, publish(2)));
            std::cout << "Testing concurrent prekey claims\n";
            check(dao.PrivateChatCommand(2, publish(2))["available_prekeys"].asInt() == 1, "publish idempotent");
            auto claim = request("claim", 1); claim["peer"] = 2;
            auto parallel = std::async(std::launch::async, [&] { return dao.PrivateChatCommand(1, claim); });
            const auto first = dao.PrivateChatCommand(1, claim), second = parallel.get();
            success(first); success(second); check(first["bundle"] == second["bundle"], "concurrent claim idempotent");
            claim["request_id"] = "d987030a-e41c-4fde-bca9-71c2ce109112";
            check(dao.PrivateChatCommand(1, claim)["error"].asInt() == 1205, "one-time prekey exhausted");
            auto send = request("send", 1); send["peer"] = 2; send["peer_identity"] = bytes(33, 2);
            std::cout << "Testing concurrent ciphertext delivery\n";
            send["id"] = send["request_id"]; send["kind"] = 3; send["ciphertext"] = "AQID";
            auto sendFuture = std::async(std::launch::async, [&] { return dao.PrivateChatCommand(1, send); });
            success(dao.PrivateChatCommand(1, send)); success(sendFuture.get());
            auto conflict = send; conflict["ciphertext"] = "BAUG";
            check(dao.PrivateChatCommand(1, conflict)["error"].asInt() == 1203, "message id conflict rejected");
            const auto polled = dao.PrivateChatCommand(2, request("poll", 2)); success(polled);
            std::cout << "Testing acknowledgement ownership\n";
            check(polled["envelope"]["ciphertext"] == "AQID", "offline ciphertext retained");
            auto ack = request("ack", 3); ack["sequence"] = polled["envelope"]["sequence"];
            success(dao.PrivateChatCommand(3, ack));
            check(dao.PrivateChatCommand(2, request("poll", 2))["envelope"] == polled["envelope"], "foreign acknowledgement cannot remove message");
            ack["identity"] = bytes(33, 2); success(dao.PrivateChatCommand(2, ack));
            success(dao.PrivateChatCommand(1, send));
            check(!dao.PrivateChatCommand(2, request("poll", 2)).isMember("envelope"), "retry after acknowledgement does not redeliver");
            sql->execute("INSERT INTO user_block VALUES(2,1)");
            check(dao.PrivateChatCommand(1, send)["error"].asInt() == 1101, "blocked sender denied");
            auto stranger = request("identity", 3); stranger["peer"] = 2;
            check(dao.PrivateChatCommand(3, stranger)["error"].asInt() == 1101, "non-friend denied");
            sql->execute("UPDATE user SET status=1 WHERE uid=1");
            check(dao.PrivateChatCommand(1, request("register", 1))["error"].asInt() == 1010, "disabled account denied");
            std::unique_ptr<sql::ResultSet> persisted(sql->executeQuery("SELECT COUNT(*) FROM private_envelope WHERE acknowledged=1 AND ciphertext IS NULL"));
            check(persisted->next() && persisted->getInt(1) == 1, "one deduplication record with ciphertext erased");
            sql->execute("UPDATE user SET status=0 WHERE uid=1");
            sql->execute("DELETE FROM user_block WHERE owner_uid=2 AND blocked_uid=1");
            authenticationTests(dao);
        }
        cleanup();
        std::cout << "Private database integration tests passed\n"; return 0;
    } catch (const sql::SQLException &error) {
        std::cerr << "Database test failed, SQL error code: " << error.getErrorCode() << '\n';
    } catch (const std::exception &error) { std::cerr << "Database test failed: " << error.what() << '\n'; }
    try { cleanup(); } catch (...) { std::cerr << "Cleanup failed; isolated database retained: " << schema << '\n'; }
    return 1;
}
