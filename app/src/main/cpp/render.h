#ifndef RENDER_H
#define RENDER_H

// #include <android_native_app_glue.h>
#include <android/asset_manager.h>

void init_fonts(struct android_app* app, AAssetManager* assetManager, int w, int h);
void init_gl_renderer();
void draw_board_gl(int w, int h);
int screen_to_board(float x, float y, int* row, int* col);
int check_ui_click(float x, float y);
int check_popup_click(float x, float y);

#endif
