#include "chess.h"
#include <string.h>
#include <stdio.h> // [修复] sscanf 支持
#include <stdlib.h>
#include <thread>
#include <atomic>

int board[ROWS][COLS];
std::atomic<int> current_turn(1);
int selected_row = -1, selected_col = -1;

bool is_ai_searching = false;
int last_move_from_r = -1, last_move_from_c = -1, last_move_to_r = -1, last_move_to_c = -1;
float anim_progress = 1.0f;
std::atomic<bool> red_is_ai(false);
std::atomic<bool> black_is_ai(true); // 默认黑方为 AI

std::atomic<bool> ai_is_thinking(false);
std::atomic<bool> ai_move_ready(false);
int ai_best_move[4] = {-1, -1, -1, -1};
int cached_ai_move[4] = {-1, -1, -1, -1};
bool cached_move_valid = false;
std::mutex g_board_mutex;

int game_state = 0;
int winner = 0;
int confirming_player = 0;

float drag_x = 0.0f, drag_y = 0.0f;
int no_capture_count = 0; // 连续不吃子步数
bool is_dragging = false;

struct MoveRecord { int board[ROWS][COLS]; int turn; int no_capture; };
MoveRecord move_history[4096];
int history_count = 0;

void init_board() {
    memset(board, 0, sizeof(board));
    board[9][0] = R_ROOK;    board[9][1] = R_HORSE;  board[9][2] = R_ELEPHANT;
    board[9][3] = R_ADVISOR; board[9][4] = R_KING;   board[9][5] = R_ADVISOR;
    board[9][6] = R_ELEPHANT;board[9][7] = R_HORSE;  board[9][8] = R_ROOK;
    board[7][1] = R_CANNON;  board[7][7] = R_CANNON;
    board[6][0] = R_PAWN;    board[6][2] = R_PAWN;   board[6][4] = R_PAWN;
    board[6][6] = R_PAWN;    board[6][8] = R_PAWN;
    board[0][0] = B_ROOK;    board[0][1] = B_HORSE;  board[0][2] = B_ELEPHANT;
    board[0][3] = B_ADVISOR; board[0][4] = B_KING;   board[0][5] = B_ADVISOR;
    board[0][6] = B_ELEPHANT;board[0][7] = B_HORSE;  board[0][8] = B_ROOK;
    board[2][1] = B_CANNON;  board[2][7] = B_CANNON;
    board[3][0] = B_PAWN;    board[3][2] = B_PAWN;   board[3][4] = B_PAWN;
    board[3][6] = B_PAWN;    board[3][8] = B_PAWN;
    current_turn = 1;
    selected_row = selected_col = -1;
    history_count = 0;
    no_capture_count = 0;
    game_state = 0;
    winner = 0;
    confirming_player = 0;
    cached_move_valid = false; // [安全修复] 初始化时清空缓存
}

int generate_moves(int row, int col, int moves[ROWS*COLS][2]) {
    int piece = board[row][col];
    if (piece == 0) return 0;
    int color = color_of(piece);
    int count = 0;
    int abs_p = abs(piece);

    auto add_move = [&](int r, int c) {
        if (in_board(r, c) && color_of(board[r][c]) != color) { moves[count][0] = r; moves[count][1] = c; count++; }
    };

    if (abs_p == 1) {
        int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (int i=0; i<4; i++) {
            int nr = row + dirs[i][0], nc = col + dirs[i][1];
            if (in_board(nr, nc) && color_of(board[nr][nc]) != color) {
                if (color == 1) { if (nr>=7 && nr<=9 && nc>=3 && nc<=5) add_move(nr,nc); }
                else            { if (nr>=0 && nr<=2 && nc>=3 && nc<=5) add_move(nr,nc); }
            }
        }
        int forward = (color == 1) ? -1 : 1;
        int r = row + forward;
        while(in_board(r, col)) {
            if (board[r][col] != 0) {
                if (abs(board[r][col]) == 1) add_move(r, col);
                break;
            }
            r += forward;
        }
    }
    else if (abs_p == 2) {
        int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        for (int i=0; i<4; i++) {
            int nr = row + dirs[i][0], nc = col + dirs[i][1];
            if (in_board(nr, nc) && color_of(board[nr][nc]) != color) {
                if (color == 1) { if (nr>=7 && nr<=9 && nc>=3 && nc<=5) add_move(nr,nc); }
                else            { if (nr>=0 && nr<=2 && nc>=3 && nc<=5) add_move(nr,nc); }
            }
        }
    }
    else if (abs_p == 3) {
        int dirs[4][2] = {{-2,-2},{-2,2},{2,-2},{2,2}};
        for (int i=0; i<4; i++) {
            int nr = row + dirs[i][0], nc = col + dirs[i][1];
            int br = row + dirs[i][0]/2, bc = col + dirs[i][1]/2;
            if (in_board(nr, nc) && board[br][bc] == 0 && color_of(board[nr][nc]) != color) {
                if (color == 1) { if (nr >= 5) add_move(nr,nc); }
                else            { if (nr <= 4) add_move(nr,nc); }
            }
        }
    }
    else if (abs_p == 4) {
        int dirs[8][4] = {{-2,-1,-1,0}, {-2,1,-1,0}, {2,-1,1,0}, {2,1,1,0}, {-1,-2,0,-1}, {-1,2,0,1}, {1,-2,0,-1}, {1,2,0,1}};
        for (int i=0; i<8; i++) {
            int nr = row + dirs[i][0], nc = col + dirs[i][1];
            int br = row + dirs[i][2], bc = col + dirs[i][3];
            if (in_board(nr, nc) && board[br][bc] == 0 && color_of(board[nr][nc]) != color) add_move(nr,nc);
        }
    }
    else if (abs_p == 5 || abs_p == 6) { // 车、炮标准规则实现
        int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (int i=0; i<4; i++) {
            int nr = row + dirs[i][0], nc = col + dirs[i][1];
            
            if (abs_p == 5) {
                // --- 车：直线无阻挡，可走空格，遇敌可吃 ---
                while (in_board(nr, nc)) {
                    int target = board[nr][nc];
                    if (target == 0) {
                        add_move(nr, nc);
                    } else {
                        if (color_of(target) != color) add_move(nr, nc);
                        break;
                    }
                    nr += dirs[i][0];
                    nc += dirs[i][1];
                }
            } else {
                // --- 炮：不吃子时路径必须全空；吃子时必须且只能翻越一个子（炮架） ---
                int has_met_piece = 0;
                while (in_board(nr, nc)) {
                    int target = board[nr][nc];
                    if (has_met_piece == 0) {
                        if (target == 0) {
                            add_move(nr, nc); // 未遇子前，走空位合法
                        } else {
                            has_met_piece = 1; // 遇到第一个子，作为炮架
                        }
                    } else {
                        // 已经遇到炮架，继续寻找下一个子（即要吃的棋子）
                        if (target != 0) {
                            if (color_of(target) != color) {
                                add_move(nr, nc); // 翻过炮架后遇到敌方棋子，可以吃！
                            }
                            break; // 遇到第二个子无论敌我必须停止
                        }
                    }
                    nr += dirs[i][0];
                    nc += dirs[i][1];
                }
            }
        }
    }
    else if (abs_p == 7) {
        int forward = (color == 1) ? -1 : 1;
        if ((color == 1 && row > 4) || (color == -1 && row < 5)) {
            int nr = row + forward;
            if (in_board(nr, col) && color_of(board[nr][col]) != color) add_move(nr, col);
        } else {
            int dirs[3][2] = {{forward,0},{0,-1},{0,1}};
            for (int i=0; i<3; i++) {
                int nr = row + dirs[i][0], nc = col + dirs[i][1];
                if (in_board(nr, nc) && color_of(board[nr][nc]) != color) add_move(nr, nc);
            }
        }
    }
    return count;
}

int kings_facing() {
    int k1_c = -1, k1_r = -1, k2_r = -1;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 3; c <= 5; c++) {
            if (abs(board[r][c]) == 1) {
                if (k1_c == -1) { k1_r = r; k1_c = c; }
                else if (k1_c == c) { k2_r = r; }
            }
        }
    }
    if (k2_r != -1) {
        int pieces_between = 0;
        for (int r = k1_r + 1; r < k2_r; r++) { if (board[r][k1_c] != 0) pieces_between++; }
        if (pieces_between == 0) return 1;
    }
    return 0;
}


// [规则补丁] 检测指定颜色方是否处于被将军状态
int is_in_check(int color) {
    int kr = -1, kc = -1;
    // 1. 找到己方老将的精确坐标
    for (int r = 0; r < ROWS; r++) {
        for (int c = 3; c <= 5; c++) {
            if (board[r][c] == (color == 1 ? R_KING : B_KING)) {
                kr = r; kc = c; break;
            }
        }
        if (kr != -1) break;
    }
    if (kr == -1) return 0; // 老将已被吃（正常流程不会发生）

    // 2. 遍历全盘，看是否有任何敌方子力的攻击范围覆盖老将
    int enemy = -color;
    int moves[ROWS*COLS][2];
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (board[r][c] != 0 && color_of(board[r][c]) == enemy) {
                int cnt = generate_moves(r, c, moves);
                for (int i = 0; i < cnt; i++) {
                    if (moves[i][0] == kr && moves[i][1] == kc) return 1; // 被将了！
                }
            }
        }
    }
    return 0;
}

// 困毙/无子可走检测：给定颜色是否还有至少一步合法棋可走。
// 合法性判断和 try_move() 保持一致，过滤掉会导致"王见王"的假合法走法。
int side_has_legal_move(int color) {
    int moves[ROWS * COLS][2];
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (board[r][c] == 0 || color_of(board[r][c]) != color) continue;
            int cnt = generate_moves(r, c, moves);
            for (int i = 0; i < cnt; i++) {
                int to_r = moves[i][0], to_c = moves[i][1];
                int target = board[to_r][to_c];
                board[to_r][to_c] = board[r][c];
                board[r][c] = 0;
                int illegal = kings_facing() || is_in_check(color);
                board[r][c] = board[to_r][to_c];
                board[to_r][to_c] = target;
                if (!illegal) return 1;
            }
        }
    }
    return 0;
}

void make_move(int from_r, int from_c, int to_r, int to_c) {
    // 记录历史，用于悔棋
    if (history_count < (int)(sizeof(move_history) / sizeof(move_history[0]))) {
        move_history[history_count].no_capture = no_capture_count;
        memcpy(move_history[history_count].board, board, sizeof(board));
        move_history[history_count].turn = current_turn;
        history_count++;
    }
    
    // 绝杀检测：如果吃掉的是老将，触发胜利状态
    if (abs(board[to_r][to_c]) == 1) {
        game_state = 3;
        winner = current_turn;
    }
    
    // 自然限着规则更新
    if (board[to_r][to_c] != 0) {
        no_capture_count = 0;
    } else {
        no_capture_count++;
    }
    
    // 120 步 (60回合) 不吃子，判为和局 (国标)
    if (no_capture_count >= 120) {
        game_state = 3;
        winner = 0;
        last_move_from_r = -1; last_move_from_c = -1;
        last_move_to_r = -1; last_move_to_c = -1;
    }

    if (!is_ai_searching) {
        last_move_from_r = from_r; last_move_from_c = from_c;
        last_move_to_r = to_r; last_move_to_c = to_c;
        anim_progress = 0.0f; // 触发滑动动画
    }
    
    board[to_r][to_c] = board[from_r][from_c];
    board[from_r][from_c] = 0;
    current_turn = -current_turn;
    selected_row = selected_col = -1;

    
    // [规则补丁] 局面重复三次检测 (长将判负 / 常捉判和)
    if (game_state == 0) {
        int rep_count = 0;
        for (int i = 0; i < history_count; i++) {
            // 只比对该方将要走棋时的历史局面
            if (move_history[i].turn == current_turn) {
                if (memcmp(move_history[i].board, board, sizeof(board)) == 0) {
                    rep_count++;
                }
            }
        }
        if (rep_count >= 2) { // 加上当前即将成型的局面，共计 3 次重复
            game_state = 3;
            if (is_in_check(current_turn)) {
                // 如果即将走棋的一方正被将军，说明刚落子的一方在“长将”
                winner = current_turn; // 长将者违规判负，被将方直接获胜！
            } else {
                // 闲着重复或长捉，统一作和局处理
                winner = 0; 
            }
        }
    }

    // 困毙检测：如果棋局还没有因为吃老将/和棋而结束，且轮到的一方
    // 一步合法棋都走不出，直接判负，避免游戏卡死在原地或 AI 空转搜索。
    if (game_state == 0 && !side_has_legal_move(current_turn)) {
        game_state = 3;
        winner = -current_turn;
    }
}

void undo_move() {
    if (history_count > 0) {
        history_count--;
        memcpy(board, move_history[history_count].board, sizeof(board));
        current_turn = move_history[history_count].turn;
        no_capture_count = move_history[history_count].no_capture;
        last_move_from_r = -1; // 悔棋清除残影
        selected_row = selected_col = -1;
        cached_move_valid = false; // [安全修复] 悔棋后缓存立刻作废
    }
}

int try_move(int from_r, int from_c, int to_r, int to_c) {
    int piece = board[from_r][from_c];
    if (piece == 0 || color_of(piece) != current_turn) return 0;
    int moves[ROWS*COLS][2];
    int cnt = generate_moves(from_r, from_c, moves);
    
    for (int i = 0; i < cnt; i++) {
        if (moves[i][0] == to_r && moves[i][1] == to_c) {
            int target_piece = board[to_r][to_c];
            
            // 暂时走子检查是否送将 (Kings Facing)
            board[to_r][to_c] = board[from_r][from_c];
            board[from_r][from_c] = 0;
            
            if (kings_facing() || is_in_check(current_turn)) {
                // 送将，恢复原状并返回非法
                board[from_r][from_c] = board[to_r][to_c];
                board[to_r][to_c] = target_piece;
                return 0;
            }
            
            // 合法走法，恢复原状，交给真正的 make_move 执行落子与动画
            board[from_r][from_c] = board[to_r][to_c];
            board[to_r][to_c] = target_piece;
            make_move(from_r, from_c, to_r, to_c);
            return 1;
        }
    }
    return 0;
}

// ==========================================
// 强袭皮卡鱼 (Pikafish UCI IPC 外挂引擎) - 架构重构版
// ==========================================
#include "uci_engine.h"

static std::string generate_fen() {
    std::string fen = "";
    char pchars[] = {'?', 'K', 'A', 'B', 'N', 'R', 'C', 'P'};
    for (int r = 0; r < ROWS; r++) {
        int empty = 0;
        for (int c = 0; c < COLS; c++) {
            int p = board[r][c];
            if (p == 0) empty++;
            else {
                if (empty > 0) { fen += std::to_string(empty); empty = 0; }
                char pc = pchars[abs(p)];
                if (p < 0) pc += 32;
                fen += pc;
            }
        }
        if (empty > 0) fen += std::to_string(empty);
        if (r < ROWS - 1) fen += "/";
    }
    fen += (current_turn == 1) ? " w - - 0 1" : " b - - 0 1";
    return fen;
}

void start_ai_thread() {
    if (ai_is_thinking.load() || is_ai_searching) return;
    ai_is_thinking.store(true);
    ai_move_ready.store(false);
    
    std::string fen;
    // [核心修复] 解除死锁：渲染管线外层已持有锁，此处去除嵌套锁避免 GL 线程挂起
    fen = generate_fen();
    
    is_ai_searching = true;
    UCIEngine::getInstance().startSearch(fen, 4000, [](bool success, int r1, int c1, int r2, int c2){
        if (success && r1 != -1) {
            ai_best_move[0] = r1;
            ai_best_move[1] = c1;
            ai_best_move[2] = r2;
            ai_best_move[3] = c2;
        } else {
            ai_best_move[0] = -1;
            cached_ai_move[0] = -1;
            cached_move_valid = false;
        }
        is_ai_searching = false;
        ai_move_ready = true;
        ai_is_thinking = false;
    });
}

void teardown_ai() { UCIEngine::getInstance().teardown(); }
void pause_ai() { UCIEngine::getInstance().pause(); }
void resume_ai() { UCIEngine::getInstance().resume(); }
