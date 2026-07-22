#include <android/asset_manager_jni.h>
#include <jni.h>
#include <mutex>
#include "chess.h"
#include "audio_engine.h"
#include "render.h"
#include "egl_engine.h"
#include "uci_engine.h"


static bool g_initialized = false;
static int g_window_w = -1;
static int g_window_h = -1;
static jobject g_asset_manager_obj = nullptr;
static AAssetManager* g_asset_manager = nullptr;

// --- 补回原 NativeActivity 中的震动与退出逻辑 ---
static JavaVM* g_vm = nullptr;
static jobject g_main_activity = nullptr;
static jmethodID g_vibrate_method = nullptr;
static jmethodID g_finish_method = nullptr;

void trigger_vibrate(int ms) {
    if (!g_vm || !g_main_activity || !g_vibrate_method) return;
    JNIEnv* env;
    bool attached = false;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        g_vm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    env->CallVoidMethod(g_main_activity, g_vibrate_method, (jlong)ms);
    if (attached) {
        g_vm->DetachCurrentThread();
    }
}

void finish_activity() {
    if (!g_vm || !g_main_activity || !g_finish_method) return;
    JNIEnv* env;
    bool attached = false;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        g_vm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    env->CallVoidMethod(g_main_activity, g_finish_method);
    if (attached) {
        g_vm->DetachCurrentThread();
    }
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_zero_xiangqi_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jobject assetManager, jint width, jint height) {
    std::lock_guard<std::mutex> lock(g_board_mutex);
    
    // 初始化并缓存全局 JNI 引用
    env->GetJavaVM(&g_vm);
    if (g_main_activity) env->DeleteGlobalRef(g_main_activity);
    g_main_activity = env->NewGlobalRef(thiz);
    jclass clazz = env->GetObjectClass(thiz);
    g_vibrate_method = env->GetMethodID(clazz, "triggerVibrate", "(J)V");
    g_finish_method = env->GetMethodID(clazz, "finish", "()V");
    env->DeleteLocalRef(clazz);

    g_window_w = width;
    g_window_h = height;
    
    if (!g_initialized) {
        init_board();
        audio_init();
        g_initialized = true;
    }
    
    if (assetManager != nullptr) {
        if (g_asset_manager_obj) env->DeleteGlobalRef(g_asset_manager_obj);
        g_asset_manager_obj = env->NewGlobalRef(assetManager);
        g_asset_manager = AAssetManager_fromJava(env, g_asset_manager_obj);
        if (g_asset_manager) {
            init_fonts(nullptr, g_asset_manager, g_window_w, g_window_h);
        }
    }
    init_gl_renderer();
}

JNIEXPORT void JNICALL
Java_com_zero_xiangqi_MainActivity_nativeResize(JNIEnv* env, jobject thiz, jint width, jint height) {
    std::lock_guard<std::mutex> lock(g_board_mutex);
    if (width > 0 && height > 0 && (g_window_w != width || g_window_h != height)) {
        g_window_w = width;
        g_window_h = height;
        // 屏幕翻转或分屏时，重新计算棋盘比例和字库缩放
        if (g_asset_manager) {
            init_fonts(nullptr, g_asset_manager, g_window_w, g_window_h);
        }
    }
}

JNIEXPORT void JNICALL
Java_com_zero_xiangqi_MainActivity_nativeRender(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_board_mutex);
    
    bool is_current_ai = (current_turn == 1 && red_is_ai) || (current_turn == -1 && black_is_ai);
    
    // 补回动画保护：确保动画进度完成 (anim_progress >= 1.0f) 后才进行 AI 推理和落子
    if (game_state == 0 && is_current_ai && anim_progress >= 1.0f) {
        if (!ai_is_thinking.load() && !ai_move_ready.load()) {
            start_ai_thread();
        } else if (ai_move_ready.load()) {
            ai_move_ready.store(false);
            if (ai_best_move[0] != -1) {
                cached_ai_move[0] = ai_best_move[0];
                cached_ai_move[1] = ai_best_move[1];
                cached_ai_move[2] = ai_best_move[2];
                cached_ai_move[3] = ai_best_move[3];
                cached_move_valid = true;
            }
            if (ai_best_move[0] != -1 && is_current_ai) {
                if (try_move(ai_best_move[0], ai_best_move[1], ai_best_move[2], ai_best_move[3])) {
                    trigger_vibrate(30);
                    audio_play_drop();
                } else {
                    if (current_turn == 1) red_is_ai = false;
                    else black_is_ai = false;
                }
            }
        }
    }

    if (g_initialized) {
        draw_board_gl(g_window_w, g_window_h);
    }
}

JNIEXPORT void JNICALL
Java_com_zero_xiangqi_MainActivity_nativeTouch(JNIEnv* env, jobject thiz, jint action, jfloat x, jfloat y) {
    std::lock_guard<std::mutex> lock(g_board_mutex);
    
    // 处理弹窗点击
    if (game_state != 0) {
        if (action == 0) { // ACTION_DOWN
            int pop_act = check_popup_click(x, y);
            if (pop_act == 1) { 
                game_state = 0; 
            } else if (pop_act == 2) { 
                if (game_state == 3) { 
                    audio_teardown(); init_board(); audio_init(); 
                } else if (game_state == 1) { 
                    finish_activity(); // 补回通过 JNI 退出 Activity 逻辑
                } else if (game_state == 2) { 
                    winner = -confirming_player; game_state = 3; 
                } else if (game_state == 4) { 
                    game_state = 0;
                    if (confirming_player != current_turn) { undo_move(); } 
                    else { undo_move(); undo_move(); }
                }
            }
        }
        return;
    }

    if (action == 0) { // ACTION_DOWN
        int ui_action = check_ui_click(x, y);
        if (ui_action != 0) {
            int act = ui_action % 10;
            int player = (ui_action / 10 == 1) ? 1 : -1;
            
            // --- 托管按钮 (13: 红方左侧, 23: 黑方右侧) ---
            if (act == 3) {
                trigger_vibrate(20);
                audio_play_click();
                std::atomic<bool>& target_ai = (player == 1) ? red_is_ai : black_is_ai;
                target_ai = !target_ai;
                
                // 若切回 AI 且已有缓存结论，立刻落子
                if (target_ai && cached_move_valid && current_turn == player) {
                    if (try_move(cached_ai_move[0], cached_ai_move[1], cached_ai_move[2], cached_ai_move[3])) {
                        cached_move_valid = false;
                        trigger_vibrate(30);
                        audio_play_drop();
                    }
                }
                return;
            }
            
            // --- 拦截：若该方处于托管状态，禁用其手动的"悔棋/认输" ---
            if ((player == 1 && red_is_ai) || (player == -1 && black_is_ai)) return;
            
            trigger_vibrate(20);
            audio_play_click();
            
            // --- 悔棋按钮 (11: 红方中间, 21: 黑方中间) ---
            if (act == 1) { 
                bool opp_is_ai = (player == 1) ? black_is_ai.load() : red_is_ai.load();
                if (opp_is_ai) {
                    if (player != current_turn) { undo_move(); } 
                    else { undo_move(); undo_move(); }
                } else {
                    game_state = 4; confirming_player = player;
                }
            } 
            // --- 认输按钮 (12: 红方右侧, 22: 黑方左侧) ---
            else if (act == 2) { 
                game_state = 2; confirming_player = player;
            }
            return;
        }

        // 棋盘点击落子逻辑
        bool is_current_ai = (current_turn == 1 && red_is_ai) || (current_turn == -1 && black_is_ai);
        if (is_current_ai) return;

        int row, col;
        if (screen_to_board(x, y, &row, &col)) {
            if (board[row][col] != 0 && color_of(board[row][col]) == current_turn) {
                selected_row = row;
                selected_col = col;
                is_dragging = true;
                drag_x = x;
                drag_y = y;
                trigger_vibrate(15);
                audio_play_click();
            } else if (selected_row != -1) {
                if (try_move(selected_row, selected_col, row, col)) {
                    selected_row = selected_col = -1;
                    trigger_vibrate(30);
                    audio_play_drop();
                } else {
                    selected_row = selected_col = -1;
                }
            }
        } else {
            selected_row = selected_col = -1;
        }
    } else if (action == 2) { // MOVE
        if (is_dragging) {
            drag_x = x;
            drag_y = y;
        }
    } else if (action == 1) { // UP
        if (is_dragging) {
            is_dragging = false;
            int row, col;
            if (screen_to_board(x, y, &row, &col)) {
                if (selected_row != -1 && (row != selected_row || col != selected_col)) {
                    if (try_move(selected_row, selected_col, row, col)) {
                        trigger_vibrate(30);
                        audio_play_drop();
                    }
                }
            }
            // [已自动修复] 删除了 selected_row = selected_col = -1; 保留持续高亮
        }
    }
}

JNIEXPORT jboolean JNICALL
Java_com_zero_xiangqi_MainActivity_nativeToggleGameDialog(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_board_mutex);
    if (game_state == 0) {
        game_state = 1; 
        confirming_player = (current_turn == 1) ? 1 : -1;
    } else {
        game_state = 0; 
    }
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_zero_xiangqi_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz) {
    if (g_asset_manager_obj) {
        env->DeleteGlobalRef(g_asset_manager_obj);
        g_asset_manager_obj = nullptr;
        g_asset_manager = nullptr;
    }
    if (g_main_activity) {
        env->DeleteGlobalRef(g_main_activity);
        g_main_activity = nullptr;
    }
    audio_teardown();
    teardown_ai();
}

}
