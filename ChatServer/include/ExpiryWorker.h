#pragma once
#include "MysqlMgr.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

// Own and join the worker; never execute expiry work on a socket/GUI/logic queue thread.
class ExpiryWorker {
public:
    ExpiryWorker() {
        auto database = MysqlMgr::GetInstance();
        _thread = std::thread([this, database] {
            std::unique_lock<std::mutex> lock(_mutex);
            auto delay = std::chrono::seconds(5);
            while (!_wake.wait_for(lock, delay, [this] { return _stop; })) {
                lock.unlock();
                bool ok = false;
                try { ok = database->ExpireMessages(); } catch (...) {}
                delay = std::chrono::seconds(ok ? 5 : 30);
                lock.lock();
            }
        });
    }
    ~ExpiryWorker() {
        { std::lock_guard<std::mutex> lock(_mutex); _stop = true; }
        _wake.notify_all();
        if (_thread.joinable()) _thread.join();
    }
    ExpiryWorker(const ExpiryWorker &) = delete;
    ExpiryWorker &operator=(const ExpiryWorker &) = delete;
private:
    std::mutex _mutex;
    std::condition_variable _wake;
    bool _stop = false;
    std::thread _thread;
};
