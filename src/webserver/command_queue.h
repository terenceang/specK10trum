#pragma once
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstdint>

// Enum for all possible commands from HTTP/WebSocket handlers to the emulator
enum class WebCommandType {
    None,
    Reset,
    LoadFile,
    ChangeModel,
    TapeCmd,
    TapeInstantLoad,
    TapeSetMode,
    KeyboardInput,
    // ... add more as needed
};

struct WebCommand {
    WebCommandType type = WebCommandType::None;
    std::string arg1;
    std::string arg2;
    int int_arg1 = 0;
    int int_arg2 = 0;
};

class WebCommandQueue {
public:
    void push(const WebCommand& cmd) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(cmd);
        cond_.notify_one();
    }
    bool pop(WebCommand& cmd) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        cmd = queue_.front();
        queue_.pop();
        return true;
    }
    void wait_and_pop(WebCommand& cmd) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]{ return !queue_.empty(); });
        cmd = queue_.front();
        queue_.pop();
    }
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
private:
    mutable std::mutex mutex_;
    std::queue<WebCommand> queue_;
    std::condition_variable cond_;
};
