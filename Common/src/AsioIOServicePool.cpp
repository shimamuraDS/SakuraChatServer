//
// Created by adachi on 25-8-11.
//

#include "AsioIOServicePool.h"
#include <algorithm>

AsioIOServicePool::AsioIOServicePool(std::size_t size): _ioServices(std::max(size, std::size_t{1})), _works(_ioServices.size()), _nextIOService(0) {
    for (std::size_t i = 0; i < _ioServices.size(); i++) {
        _works[i] = std::make_unique<Work>(boost::asio::make_work_guard(_ioServices[i]));
    }

    // 遍历多个ioservice，创建多个线程，每个线程内部启动ioservice
    for (std::size_t i = 0; i < _ioServices.size(); i++) {
        _threads.emplace_back([this, i]() {
            _ioServices[i].run();
        });
    }
}

AsioIOServicePool::~AsioIOServicePool() {
    Stop();
    std::cout << "AsioIOServicePool destruct" << std::endl;
}

AsioIOServicePool::IOService& AsioIOServicePool::GetIOService() {
    auto& service = _ioServices[_nextIOService++];
    if (_nextIOService == _ioServices.size()) {
        _nextIOService = 0;
    }
    return service;
}

void AsioIOServicePool::Stop() {
    std::call_once(_stopOnce, [this] {
        for (auto& work : _works) {
            work->get_executor().context().stop();
            work.reset();
        }
        for (auto& t : _threads) {
            t.join();
        }
    });
}
