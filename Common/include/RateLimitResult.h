#pragma once
struct RateLimitResult {
    enum class State { Allowed, Limited, Unavailable };
    State state = State::Unavailable;
    int retryAfter = 0;
};
