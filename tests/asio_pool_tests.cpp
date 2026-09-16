#include "AsioIOServicePool.h"

int main() {
    auto pool = AsioIOServicePool::GetInstance();
    pool->Stop();
    pool->Stop();
    // Singleton destruction must also tolerate an already stopped pool.
    return 0;
}
