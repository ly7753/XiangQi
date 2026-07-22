#ifndef UCI_ENGINE_H
#define UCI_ENGINE_H

#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

// 纯净的底层 IPC 引擎包装类，彻底与象棋业务解耦
class UCIEngine {
public:
    static UCIEngine& getInstance();
    
    // 异步启动搜索，结果通过回调传回主线程
    void startSearch(const std::string& fen, int movetime, std::function<void(bool, int, int, int, int)> onMoveReady);
    void stop();
    void teardown();
    void pause();
    void resume();

private:
    UCIEngine() = default;
    ~UCIEngine() = default;
    
    void ensureRunning();
    void searchThread(std::string fen, int movetime, std::function<void(bool, int, int, int, int)> onMoveReady);

    int in_fd = -1;
    int out_fd = -1;
    pid_t pid = -1;
    std::mutex mutex;
    std::atomic<bool> is_searching{false};
};

#endif
