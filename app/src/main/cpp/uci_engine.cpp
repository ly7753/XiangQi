#include "uci_engine.h"
#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/prctl.h>


static std::string get_so_dir() {
    Dl_info info;
    if (dladdr((void*)get_so_dir, &info)) {
        std::string path = info.dli_fname; 
        size_t last_slash = path.find_last_of('/');
        if (last_slash != std::string::npos) return path.substr(0, last_slash);
    }
    return "";
}

UCIEngine& UCIEngine::getInstance() {
    static UCIEngine instance;
    return instance;
}

void UCIEngine::ensureRunning() {
    std::lock_guard<std::mutex> lock(mutex);
    if (pid != -1) return;

    std::string so_dir = get_so_dir();
    std::string engine_path = so_dir + "/libpikafish.so";
    std::string nnue_path = so_dir + "/libpikafishnnue.so";
    
    int pipe_in[2], pipe_out[2];
    if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0) {
        return;
    }

    pid = fork();
    if (pid == 0) {
        // [极客优化] 绑定父进程存活状态。若 Android 强杀 App 主进程，内核会自动连带秒杀皮卡鱼，杜绝僵尸进程耗电
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_out[1], STDERR_FILENO); 
        close(pipe_in[1]); close(pipe_out[0]);
        close(pipe_in[0]); close(pipe_out[1]);
        execl(engine_path.c_str(), "pikafish", NULL);
        _exit(1);
    } else if (pid > 0) {
        close(pipe_in[0]);
        close(pipe_out[1]);
        in_fd = pipe_in[1];
        out_fd = pipe_out[0];

        int flags = fcntl(out_fd, F_GETFL, 0);
        fcntl(out_fd, F_SETFL, flags | O_NONBLOCK);

        // [算力解放] 开启 2 个后台线程进行神经网络计算，棋力大幅提升
        std::string cmds = "uci\nsetoption name EvalFile value " + nnue_path + "\nsetoption name Threads value 4\nsetoption name Hash value 256\nisready\n";
        write(in_fd, cmds.c_str(), cmds.length());
        
        char buf[256];
        struct pollfd pfd = { out_fd, POLLIN, 0 };
        while (poll(&pfd, 1, 1500) > 0) {
            ssize_t n = read(out_fd, buf, sizeof(buf)-1);
            if (n <= 0) break;
            buf[n] = '\0';
            if (strstr(buf, "readyok")) break;
        }
    }
}

void UCIEngine::searchThread(std::string fen, int movetime, std::function<void(bool, int, int, int, int)> onMoveReady) {
    ensureRunning();

    if (in_fd < 0 || out_fd < 0) {
        is_searching = false;
        onMoveReady(false, -1, -1, -1, -1);
        return;
    }

    // [优化] 合并指令发送，减少系统调用
    std::string cmds = "position fen " + fen + "\ngo movetime " + std::to_string(movetime) + "\n";
    write(in_fd, cmds.c_str(), cmds.length());

    char buffer[512];
    std::string output;
    output.reserve(1024); // 预分配内存，减少重分配

    int elapsed_ms = 0;
    const int TOTAL_TIMEOUT_MS = 8000;
    struct pollfd pfd = { out_fd, POLLIN, 0 };

    while (is_searching) {
        // [极客优化] 增加进程存活检查
        int status;
        if (pid == -1 || waitpid(pid, &status, WNOHANG) == pid) {
            break;
        }

        int poll_ret = poll(&pfd, 1, 100); // 缩短轮询间隔，提高响应性
        if (poll_ret > 0) {
            ssize_t n = read(out_fd, buffer, sizeof(buffer)-1);
            if (n <= 0) break;
            buffer[n] = '\0';
            output.append(buffer);

            // [极致优化] 使用更快的字符串搜索查找关键信息
            size_t bestmove_pos = output.find("bestmove ");
            if (bestmove_pos != std::string::npos) {
                size_t end_pos = output.find('\n', bestmove_pos);
                if (end_pos != std::string::npos) {
                    std::string line = output.substr(bestmove_pos, end_pos - bestmove_pos);
                    char mv[16];
                    if (sscanf(line.c_str(), "bestmove %15s", mv) == 1) {
                        std::string move_str(mv);
                        if (move_str != "(none)" && move_str.length() >= 4) {
                            // [监控系统] 记录引擎的原始输出和转换后的坐标
                        int c1 = tolower(move_str[0]) - 'a';
                        int r1 = 9 - (move_str[1] - '0');
                        int c2 = tolower(move_str[2]) - 'a';
                        int r2 = 9 - (move_str[3] - '0');
                            onMoveReady(true, r1, c1, r2, c2);
                            is_searching = false;
                            return;
                        }
                    }
                }
            }
        } else if (poll_ret == 0) {
            elapsed_ms += 100;
            if (elapsed_ms > TOTAL_TIMEOUT_MS) break;
        } else {
            if (errno == EINTR) continue;
            break;
        }
    }

    // 容错处理
    if (is_searching) {
        teardown(); // 彻底重置
        onMoveReady(false, -1, -1, -1, -1);
    }
    is_searching = false;
}

void UCIEngine::startSearch(const std::string& fen, int movetime, std::function<void(bool, int, int, int, int)> onMoveReady) {
    if (is_searching) return;
    is_searching = true;
    std::thread(&UCIEngine::searchThread, this, fen, movetime, onMoveReady).detach();
}

void UCIEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex);
    if (pid != -1 && is_searching) {
        write(in_fd, "stop\n", 5);
    }
}

void UCIEngine::teardown() {
    std::lock_guard<std::mutex> lock(mutex);
    if (pid != -1) {
        write(in_fd, "quit\n", 5);
        close(in_fd);
        close(out_fd);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        pid = -1;
        in_fd = -1;
        out_fd = -1;
    }
}

void UCIEngine::pause() {
    std::lock_guard<std::mutex> lock(mutex);
    if (pid != -1) kill(pid, SIGSTOP);
}

void UCIEngine::resume() {
    std::lock_guard<std::mutex> lock(mutex);
    if (pid != -1) kill(pid, SIGCONT);
}
