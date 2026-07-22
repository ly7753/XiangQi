#include <atomic>
#include <mutex>
#ifndef CHESS_H
#define CHESS_H


#define COLS 9
#define ROWS 10

enum Piece {
    EMPTY = 0,
    R_KING = 1, R_ADVISOR = 2, R_ELEPHANT = 3, R_HORSE = 4, R_ROOK = 5, R_CANNON = 6, R_PAWN = 7,
    B_KING = -1, B_ADVISOR = -2, B_ELEPHANT = -3, B_HORSE = -4, B_ROOK = -5, B_CANNON = -6, B_PAWN = -7
};

extern int board[ROWS][COLS];
extern std::atomic<int> current_turn;
extern int selected_row, selected_col;

// 动画与高亮状态
extern bool is_ai_searching;
extern int last_move_from_r, last_move_from_c, last_move_to_r, last_move_to_c;
extern float anim_progress;
extern std::atomic<bool> red_is_ai;
extern std::atomic<bool> black_is_ai;

// 游戏状态系统
extern int game_state; // 0:正常对局 1:确认退出 2:确认认输 3:游戏结束(胜利面板)
extern int winner;     // 1:红方胜利 -1:黑方胜利
extern int confirming_player;

// 拖拽交互状态
extern float drag_x, drag_y;
extern bool is_dragging; // 记录是谁发起了退出或认输
extern int no_capture_count;

inline int color_of(int piece) {
    if (piece == 0) return 0;
    return (piece > 0) ? 1 : -1;
}
inline int in_board(int r, int c) {
    return (unsigned int)r < ROWS && (unsigned int)c < COLS;
}

void init_board();
int generate_moves(int row, int col, int moves[ROWS*COLS][2]);
void make_move(int from_r, int from_c, int to_r, int to_c);
int try_move(int from_r, int from_c, int to_r, int to_c);
int side_has_legal_move(int color); // 困毙检测：该方是否还有任意一步合法棋可走
void undo_move();
// 全局互斥锁：保护 board/game_state/winner/no_capture_count 等共享状态，
// 避免 AI 后台搜索线程与渲染/输入主线程之间产生数据竞争(data race)。
// 这是"误报和棋 / 胜负弹窗闪现"等一系列诡异 bug 的根源。
extern std::mutex g_board_mutex;
extern std::atomic<bool> ai_is_thinking;
extern std::atomic<bool> ai_move_ready;
extern int ai_best_move[4];
extern int cached_ai_move[4];
extern bool cached_move_valid;
void start_ai_thread();
void teardown_ai();
void pause_ai();
void resume_ai(); // 物理超度引擎

#endif
