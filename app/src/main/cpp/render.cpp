#include "render.h"
#include "chess.h"
#include <cstdlib>
#include <cmath>
#include <string.h>
#include <vector>
#include <cstddef>
#include <GLES3/gl3.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// --- 棋盘背景图全局变量 ---
static GLuint gl_bg_tex = 0;
static GLuint gl_piece_bg_tex = 0;
static int bg_img_w = 0, bg_img_h = 0;
static float bg_avg_r = 0.90f, bg_avg_g = 0.93f, bg_avg_b = 0.91f;
static bool bg_color_extracted = false;

// =========================================
// 极致优化: VBO 动态批处理 (Batching) 管线配置
// =========================================
static GLuint gl_program, gl_vao, gl_vbo;
static GLint u_res, u_tex;

struct Vertex {
    float x, y, u, v;
    float type;      // 0:矩形/线, 1:实心圆, 2:空心圆环, 3:软阴影, 4:文字图集, 5:FBO背景
    float r, g, b, a;
    float radius, ring;
};
static std::vector<Vertex> batch_vertices;

static GLuint gl_fbo = 0, gl_fbo_tex = 0;
static GLuint gl_atlas_tex = 0;
static int fbo_w = -1, fbo_h = -1, fbo_cell_px = -1;

static const char* vs_source = 
    "#version 300 es\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in float a_type;\n"
    "layout(location = 3) in vec4 a_color;\n"
    "layout(location = 4) in vec2 a_params;\n"
    "out vec2 v_uv;\n"
    "out float v_type;\n"
    "out vec4 v_color;\n"
    "out vec2 v_params;\n"
    "uniform vec2 u_res;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_type = a_type;\n"
    "    v_color = a_color;\n"
    "    v_params = a_params;\n"
    "    gl_Position = vec4(a_pos.x / u_res.x * 2.0 - 1.0, 1.0 - a_pos.y / u_res.y * 2.0, 0.0, 1.0);\n"
    "}\n";
    
static const char* fs_source = 
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 v_uv;\n"
    "in float v_type;\n"
    "in vec4 v_color;\n"
    "in vec2 v_params;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "    int type = int(v_type + 0.5);\n"
    "    if (type == 0) {\n"
    "        fragColor = v_color;\n"
    "    } else if (type >= 1 && type <= 3) {\n"
    "        float d = length(v_uv);\n"
    "        float fw = 1.0 / max(1.0, v_params.x);\n"
    "        float alpha = 1.0;\n"
    "        if (type == 1) alpha = 1.0 - smoothstep(1.0 - fw, 1.0, d);\n"
    "        else if (type == 2) alpha = (1.0 - smoothstep(1.0 - fw, 1.0, d)) * smoothstep(1.0 - v_params.y - fw, 1.0 - v_params.y, d);\n"
    "        else if (type == 3) alpha = 1.0 - smoothstep(0.4, 1.0, d);\n"
    "        if (alpha <= 0.0) discard;\n"
    "        fragColor = vec4(v_color.rgb, v_color.a * alpha);\n"
    "    } else if (type == 4) {\n"
    "        float a = texture(u_tex, v_uv).a;\n"
    "        fragColor = vec4(v_color.rgb, v_color.a * a);\n"
    "    } else if (type == 5) {\n"
    "        fragColor = texture(u_tex, v_uv);\n"
    "    } else if (type == 6) {\n"
    "        vec2 uv = v_uv * 4.0;\n"
    "        float n = sin(uv.x*4.0 + sin(uv.y*3.0)) * 0.5 + 0.5;\n"
    "        n = (n + sin(uv.y*2.5 - cos(uv.x*2.0))*0.5)*0.5;\n"
    "        vec3 darkJade = vec3(0.02, 0.15, 0.08);\n"
    "        vec3 lightJade = vec3(0.12, 0.35, 0.20);\n"
    "        fragColor = vec4(mix(darkJade, lightJade, smoothstep(0.2, 0.8, n)), 1.0);\n"
    "    }\n"
    "}\n";

void init_gl_renderer() {
    auto compile_shader = [](GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        return shader;
    };
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    gl_program = glCreateProgram();
    glAttachShader(gl_program, vs);
    glAttachShader(gl_program, fs);
    glLinkProgram(gl_program);

    u_res = glGetUniformLocation(gl_program, "u_res");
    u_tex = glGetUniformLocation(gl_program, "u_tex");

    glGenVertexArrays(1, &gl_vao);
    glGenBuffers(1, &gl_vbo);
    glBindVertexArray(gl_vao);
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);
    
    size_t stride = sizeof(Vertex);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, type));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(Vertex, radius));
    glBindVertexArray(0);

    batch_vertices.reserve(4096);
    gl_fbo = 0;
    fbo_w = -1;
    fbo_h = -1;
    fbo_cell_px = -1;
}

static void flush_batch(GLuint tex_id) {
    if (batch_vertices.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);
    glBufferData(GL_ARRAY_BUFFER, batch_vertices.size() * sizeof(Vertex), batch_vertices.data(), GL_DYNAMIC_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glUniform1i(u_tex, 0);
    glDrawArrays(GL_TRIANGLES, 0, batch_vertices.size());
    batch_vertices.clear();
}

static void push_quad(float x, float y, float w, float h, float u1, float v1, float u2, float v2, float type, uint32_t argb, float radius, float ring) {
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8)  & 0xFF) / 255.0f;
    float b = ((argb >> 0)  & 0xFF) / 255.0f;
    Vertex vt0 = {x, y,       u1, v1, type, r, g, b, a, radius, ring};
    Vertex vt1 = {x, y+h,     u1, v2, type, r, g, b, a, radius, ring};
    Vertex vt2 = {x+w, y,     u2, v1, type, r, g, b, a, radius, ring};
    Vertex vt3 = {x+w, y+h,   u2, v2, type, r, g, b, a, radius, ring};
    batch_vertices.push_back(vt0); batch_vertices.push_back(vt1); batch_vertices.push_back(vt2);
    batch_vertices.push_back(vt2); batch_vertices.push_back(vt1); batch_vertices.push_back(vt3);
}

static int win_width, win_height;
static int cell_px, board_left, board_top;

struct CharUV { float u1, v1, u2, v2; int width, height; };
static CharUV atlas_chars[43];

static const int char_codepoints[43] = {
    0x5E05, 0x5C06, 0x4ED5, 0x58EB, 0x76F8, 0x8C61, 0x9A6C, 0x8F66, 0x70AE, 0x7832, 0x5175, 0x5352,
    0x695A, 0x6CB3, 0x6C49, 0x754C, 0x6094, 0x68CB, 0x91CD, 0x5F00, 0x6258, 0x7BA1, 0x8BA4, 0x8F93, 
    0x786E, 0x5B9A, 0x53D6, 0x6D88, 0x7EA2, 0x9ED1, 0x65B9, 0x80DC, 0x548C, 0x5C40, 0x540C, 0x610F,
    0x5BF9, 0x624B, 0x62D2, 0x7EDD, 0x9000, 0x51FA, 0xFF1F
};

static std::vector<unsigned char> font_buffer;
static stbtt_fontinfo font_info;
static bool font_loaded = false;

static int get_font_index(int piece) {
    int abs_p = abs(piece);
    switch(abs_p) {
        case 1: return piece > 0 ? 0 : 1;
        case 2: return piece > 0 ? 2 : 3;
        case 3: return piece > 0 ? 4 : 5;
        case 4: return 6; case 5: return 7; case 6: return 8;
        case 7: return piece > 0 ? 10 : 11;
        default: return -1;
    }
}

void reload_piece_texture(AAssetManager* assetManager) {
    if (gl_piece_bg_tex != 0) {
        glDeleteTextures(1, &gl_piece_bg_tex);
        gl_piece_bg_tex = 0;
    }
    AAsset* asset = AAssetManager_open(assetManager, "piece_bg.png", AASSET_MODE_BUFFER);
    if (asset) {
        off_t length = AAsset_getLength(asset);
        std::vector<unsigned char> img_buf(length);
        AAsset_read(asset, img_buf.data(), length);
        AAsset_close(asset);
        int w_p, h_p, channels_p;
        unsigned char* img_data = stbi_load_from_memory(img_buf.data(), length, &w_p, &h_p, &channels_p, 4);
        if (img_data) {
            glGenTextures(1, &gl_piece_bg_tex);
            glBindTexture(GL_TEXTURE_2D, gl_piece_bg_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_p, h_p, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
            stbi_image_free(img_data);
        }
    }
}

void init_fonts(android_app* app, AAssetManager* assetManager, int w, int h) {
    reload_piece_texture(assetManager);
    win_width = w; win_height = h;
    gl_bg_tex = 0;
    int max_cell_w = (w - w * 0.1f) / 8;
    int max_cell_h = (h - h * 0.1f) / 13; 
    cell_px = (max_cell_w < max_cell_h) ? max_cell_w : max_cell_h;
    board_left = (w - cell_px * 8) / 2;
    board_top = (h - cell_px * 9) / 2;

    int font_size = (int)(cell_px * 0.52f);

    if (!font_loaded) {
        AAsset* asset = AAssetManager_open(assetManager, "ZCOOLXiaoWei-Regular.ttf", AASSET_MODE_BUFFER);
        if (asset) {
            off_t length = AAsset_getLength(asset);
            font_buffer.resize(length);
            AAsset_read(asset, font_buffer.data(), length);
            AAsset_close(asset);
            if (stbtt_InitFont(&font_info, font_buffer.data(), stbtt_GetFontOffsetForIndex(font_buffer.data(), 0))) font_loaded = true;
        }
    }
    
    if (font_loaded) {
        float scale = stbtt_ScaleForPixelHeight(&font_info, font_size);
        int atlas_size = 1024;
        int cell_s = 85;
        std::vector<uint32_t> atlas_pixels(atlas_size * atlas_size, 0);

        for (int i = 0; i < 43; i++) {
            int width, height, xoff, yoff;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(&font_info, scale, scale, char_codepoints[i], &width, &height, &xoff, &yoff);
            if (bitmap) {
                int col = i % 6, row = i / 6;
                int start_x = col * cell_s, start_y = row * cell_s;
                atlas_chars[i].width = width; atlas_chars[i].height = height;
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        atlas_pixels[(start_y + y) * atlas_size + (start_x + x)] = (bitmap[y * width + x] << 24) | 0xFFFFFF; 
                    }
                }
                atlas_chars[i].u1 = (float)start_x / atlas_size;
                atlas_chars[i].v1 = (float)start_y / atlas_size;
                atlas_chars[i].u2 = (float)(start_x + width) / atlas_size;
                atlas_chars[i].v2 = (float)(start_y + height) / atlas_size;
                stbtt_FreeBitmap(bitmap, nullptr);
            }
        }

        if (!gl_piece_bg_tex) {
            AAsset* asset = AAssetManager_open(assetManager, "piece_bg.png", AASSET_MODE_BUFFER);
            if (asset) {
                off_t length = AAsset_getLength(asset);
                std::vector<unsigned char> img_buf(length);
                AAsset_read(asset, img_buf.data(), length);
                AAsset_close(asset);
                int w_p, h_p, channels_p;
                unsigned char* img_data = stbi_load_from_memory(img_buf.data(), length, &w_p, &h_p, &channels_p, 4);
                if (img_data) {
                    glGenTextures(1, &gl_piece_bg_tex);
                    glBindTexture(GL_TEXTURE_2D, gl_piece_bg_tex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_p, h_p, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
                    stbi_image_free(img_data);
                }
            }
        }
        
        if (!gl_bg_tex) {
            AAsset* asset = AAssetManager_open(assetManager, "board_bg.png", AASSET_MODE_BUFFER);
            if (asset) {
                off_t length = AAsset_getLength(asset);
                std::vector<unsigned char> img_buf(length);
                AAsset_read(asset, img_buf.data(), length);
                AAsset_close(asset);

                int channels;
                unsigned char* img_data = stbi_load_from_memory(img_buf.data(), length, &bg_img_w, &bg_img_h, &channels, 4);
                if (img_data) {
                    unsigned long long r_sum = 0, g_sum = 0, b_sum = 0;
                    long total_pixels = bg_img_w * bg_img_h;
                    long sample_step = (total_pixels > 10000) ? (total_pixels / 5000) : 1;
                    long sample_count = 0;
                    for (long i = 0; i < total_pixels; i += sample_step) {
                        r_sum += img_data[i * 4 + 0];
                        g_sum += img_data[i * 4 + 1];
                        b_sum += img_data[i * 4 + 2];
                        sample_count++;
                    }
                    if (sample_count > 0) {
                        bg_avg_r = (float)(r_sum / sample_count) / 255.0f;
                        bg_avg_g = (float)(g_sum / sample_count) / 255.0f;
                        bg_avg_b = (float)(b_sum / sample_count) / 255.0f;
                        bg_color_extracted = true;
                    }
                    glGenTextures(1, &gl_bg_tex);
                    glBindTexture(GL_TEXTURE_2D, gl_bg_tex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bg_img_w, bg_img_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
                    stbi_image_free(img_data);
                }
            }
        }

        if (gl_atlas_tex) glDeleteTextures(1, &gl_atlas_tex);
        glGenTextures(1, &gl_atlas_tex);
        glBindTexture(GL_TEXTURE_2D, gl_atlas_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_size, atlas_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas_pixels.data());
    }
}

static void draw_line_gl(float x1, float y1, float x2, float y2, float thickness, uint32_t argb) {
    float dx = x2 - x1, dy = y2 - y1;
    float L = std::sqrt(dx*dx + dy*dy);
    if(L == 0.0f) return;
    float nx = -(dy / L) * (thickness / 2.0f);
    float ny =  (dx / L) * (thickness / 2.0f);

    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8)  & 0xFF) / 255.0f;
    float b = ((argb >> 0)  & 0xFF) / 255.0f;

    Vertex v0 = {x1+nx, y1+ny, 0, 0, 0, r, g, b, a, 0, 0};
    Vertex v1 = {x1-nx, y1-ny, 0, 0, 0, r, g, b, a, 0, 0};
    Vertex v2 = {x2+nx, y2+ny, 0, 0, 0, r, g, b, a, 0, 0};
    Vertex v3 = {x2-nx, y2-ny, 0, 0, 0, r, g, b, a, 0, 0};
    batch_vertices.push_back(v0); batch_vertices.push_back(v1); batch_vertices.push_back(v2);
    batch_vertices.push_back(v2); batch_vertices.push_back(v1); batch_vertices.push_back(v3);
}

static void draw_rect_gl(float x, float y, float w, float h, uint32_t argb, int fill, float thickness = 2.0f) {
    if (fill) push_quad(x, y, w, h, 0,0,0,0, 0, argb, 0, 0);
    else {
        draw_line_gl(x, y, x+w, y, thickness, argb);
        draw_line_gl(x+w, y, x+w, y+h, thickness, argb);
        draw_line_gl(x+w, y+h, x, y+h, thickness, argb);
        draw_line_gl(x, y+h, x, y, thickness, argb);
    }
}

static void draw_circle_gl(float cx, float cy, float radius, uint32_t argb, int mode, float ring_thickness=0.1f) {
    push_quad(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, -1.0f, -1.0f, 1.0f, 1.0f, mode, argb, radius, ring_thickness);
}

static void draw_char_gl(int idx, float cx, float cy, uint32_t argb, bool flip) {
    if (idx < 0 || idx >= 43 || !gl_atlas_tex) return;
    const auto& c = atlas_chars[idx];
    float x = cx - c.width / 2.0f, y = cy - c.height / 2.0f;
    if (flip) push_quad(x, y, c.width, c.height, c.u2, c.v2, c.u1, c.v1, 4, argb, 0, 0);
    else push_quad(x, y, c.width, c.height, c.u1, c.v1, c.u2, c.v2, 4, argb, 0, 0);
}

static void build_static_background(int w, int h) {
    if (!gl_fbo) {
        glGenFramebuffers(1, &gl_fbo);
        glGenTextures(1, &gl_fbo_tex);
    }
    glBindTexture(GL_TEXTURE_2D, gl_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_fbo_tex, 0);

    glViewport(0, 0, w, h);
    glClearColor(bg_avg_r, bg_avg_g, bg_avg_b, 1.0f); 
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(gl_program);
    glUniform2f(u_res, (float)w, (float)h);
    glBindVertexArray(gl_vao);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    uint32_t line_color = 0xFFF2D05C, btn_bg = 0xBB000000;
    
    if (gl_bg_tex) {
        float margin_px = cell_px * 0.8f;
        float draw_w = 8.0f * cell_px + margin_px * 2.0f;
        float draw_h = 9.0f * cell_px + margin_px * 2.0f;
        float draw_left = board_left - margin_px;
        float draw_top = board_top - margin_px;
        
        float board_ratio = draw_w / draw_h;
        float img_ratio = (float)bg_img_w / bg_img_h;
        
        float u1 = 0.0f, v1 = 0.0f, u2 = 1.0f, v2 = 1.0f;
        if (img_ratio > board_ratio) {
            float crop_w = board_ratio / img_ratio;
            float margin = (1.0f - crop_w) / 2.0f;
            u1 = margin; u2 = 1.0f - margin;
        } else {
            float crop_h = img_ratio / board_ratio;
            float margin = (1.0f - crop_h) / 2.0f;
            v1 = margin; v2 = 1.0f - margin;
        }
        
        push_quad(draw_left, draw_top, draw_w, draw_h, u1, v1, u2, v2, 5, 0xFFFFFFFF, 0, 0);
        flush_batch(gl_bg_tex);
    } else {
        float margin_px = cell_px * 0.8f;
        push_quad(board_left - margin_px, board_top - margin_px, 8.0f * cell_px + margin_px * 2.0f, 9.0f * cell_px + margin_px * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0xFFF5EFE6, 0, 0);
    }

    // 动态亮度检测，仅作用于棋盘线条和楚河汉界，保留原版 line_color 给按钮等控件
    float board_luminance = bg_avg_r * 0.299f + bg_avg_g * 0.587f + bg_avg_b * 0.114f;
    uint32_t board_line_color = (board_luminance > 0.6f) ? 0xFF5E4322 : line_color;

    for (int r = 0; r < ROWS; r++)
        draw_line_gl(board_left, board_top + r * cell_px, board_left + 8 * cell_px, board_top + r * cell_px, 2.5f, board_line_color);
    for (int c = 0; c < COLS; c++) {
        if (c > 0 && c < 8) {
            draw_line_gl(board_left + c * cell_px, board_top, board_left + c * cell_px, board_top + 4 * cell_px, 2.5f, board_line_color);
            draw_line_gl(board_left + c * cell_px, board_top + 5 * cell_px, board_left + c * cell_px, board_top + 9 * cell_px, 2.5f, board_line_color);
        } else {
            draw_line_gl(board_left + c * cell_px, board_top, board_left + c * cell_px, board_top + 9 * cell_px, 2.5f, board_line_color);
        }
    }
    draw_line_gl(board_left + 3 * cell_px, board_top, board_left + 5 * cell_px, board_top + 2 * cell_px, 2.5f, board_line_color);
    draw_line_gl(board_left + 5 * cell_px, board_top, board_left + 3 * cell_px, board_top + 2 * cell_px, 2.5f, board_line_color);
    draw_line_gl(board_left + 3 * cell_px, board_top + 7 * cell_px, board_left + 5 * cell_px, board_top + 9 * cell_px, 2.5f, board_line_color);
    draw_line_gl(board_left + 5 * cell_px, board_top + 7 * cell_px, board_left + 3 * cell_px, board_top + 9 * cell_px, 2.5f, board_line_color);

    int r_y = board_top + 4.5 * cell_px;
    draw_char_gl(13, board_left + 1.5 * cell_px, r_y, board_line_color, true);
    draw_char_gl(12, board_left + 2.5 * cell_px, r_y, board_line_color, true);
    draw_char_gl(14, board_left + 5.5 * cell_px, r_y, board_line_color, false);
    draw_char_gl(15, board_left + 6.5 * cell_px, r_y, board_line_color, false);

    int btn_w = cell_px * 2.2, btn_h = cell_px * 0.9, spacing = btn_w + 15;
    int b_btn_y = board_top + 9 * cell_px + cell_px * 1.6, t_btn_y = board_top - cell_px * 1.6;

    auto draw_button = [&](int cx, int cy, int t1, int t2, bool flip) {
        draw_rect_gl(cx - btn_w/2, cy - btn_h/2, btn_w, btn_h, btn_bg, 1);
        draw_rect_gl(cx - btn_w/2, cy - btn_h/2, btn_w, btn_h, line_color, 0, 2.0f);
        draw_char_gl(t1, cx - cell_px/2 + 5, cy, line_color, flip);
        draw_char_gl(t2, cx + cell_px/2 - 5, cy, line_color, flip);
    };

    draw_button(w/2 - spacing, b_btn_y, 20, 21, false); draw_button(w/2, b_btn_y, 16, 17, false); draw_button(w/2 + spacing, b_btn_y, 22, 23, false);
    draw_button(w/2 + spacing, t_btn_y, 21, 20, true);  draw_button(w/2, t_btn_y, 17, 16, true);  draw_button(w/2 - spacing, t_btn_y, 23, 22, true);

    flush_batch(gl_atlas_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

#include <chrono>

void draw_board_gl(int w, int h) {
    if (w <= 0 || h <= 0) return;

    static auto last_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_time).count();
    last_time = now;
    if (dt > 0.05f) dt = 0.05f;

    static float anim_time = 0.0f; 
    anim_time += dt * 6.0f;
    
    if (anim_progress < 1.0f) { 
        anim_progress += dt * 5.0f;
        if (anim_progress > 1.0f) anim_progress = 1.0f; 
    }

    if (fbo_w != w || fbo_h != h || fbo_cell_px != cell_px) {
        build_static_background(w, h);
        fbo_w = w; fbo_h = h; fbo_cell_px = cell_px;
    }

    glViewport(0, 0, w, h);
    glClearColor(0.90f, 0.93f, 0.91f, 1.0f); 
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(gl_program);
    glUniform2f(u_res, (float)w, (float)h);
    glBindVertexArray(gl_vao);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    push_quad(0, 0, w, h, 0, 1, 1, 0, 5, 0xFFFFFFFF, 0, 0); 
    flush_batch(gl_fbo_tex);

    const uint32_t bg_color = 0xFFF5EFE6, line_color = 0xFFF2D05C, btn_bg = 0xBB000000;
    const int btn_w = (int)(cell_px * 2.2f), spacing = btn_w + 15;

    uint32_t c_turn_color = (current_turn == 1) ? 0xFFD32F2F : 0xFF191919;
    int b_btn_y = board_top + 9 * cell_px + (int)(cell_px * 1.6f);
    int t_btn_y = board_top - (int)(cell_px * 1.6f);

    if (last_move_from_r >= 0) {
        float h_px = cell_px;
        draw_rect_gl(board_left + last_move_from_c * cell_px - h_px/2, board_top + last_move_from_r * cell_px - h_px/2, h_px, h_px, 0x55FFCC00, 1);
        draw_rect_gl(board_left + last_move_from_c * cell_px - h_px/2, board_top + last_move_from_r * cell_px - h_px/2, h_px, h_px, 0xFFFFB700, 0, 2.5f);
        draw_rect_gl(board_left + last_move_to_c * cell_px - h_px/2, board_top + last_move_to_r * cell_px - h_px/2, h_px, h_px, 0x55FFCC00, 1);
        draw_rect_gl(board_left + last_move_to_c * cell_px - h_px/2, board_top + last_move_to_r * cell_px - h_px/2, h_px, h_px, 0xFFFFB700, 0, 2.5f);
    }

    auto draw_piece = [&](int piece, float cx, float cy, bool dragging) {
        uint32_t c_color = (piece > 0) ? 0xFFD32F2F : 0xFF191919;
        float radius = cell_px / 2.0f - (cell_px * 0.05f);
        if (dragging) radius += (cell_px * 0.05f);
        
        float shadow_radius = radius * (dragging ? 1.25f : 1.12f);
        draw_circle_gl(cx, cy, shadow_radius, dragging ? 0x25000000 : 0x40000000, 3);
        
        flush_batch(gl_atlas_tex);

        float uv_m = 0.1484375f;
        push_quad(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, uv_m, uv_m, 1.0f - uv_m, 1.0f - uv_m, 5, 0xFFFFFFFF, 0, 0);
        flush_batch(gl_piece_bg_tex);

        draw_char_gl(get_font_index(piece), cx, cy, c_color, piece < 0);
        flush_batch(gl_atlas_tex);
    };

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int piece = board[r][c];
            if (piece == 0) continue;
            if (is_dragging && r == selected_row && c == selected_col) continue;
            
            float cx = board_left + c * cell_px, cy = board_top + r * cell_px;
            if (r == last_move_to_r && c == last_move_to_c && anim_progress < 1.0f) {
                float p = anim_progress;
                float ease = (p >= 1.0f) ? 1.0f : (1.0f - std::pow(2.0f, -10.0f * p) * std::cos(p * 3.14159265f * 3.0f));
                float sx = board_left + last_move_from_c * cell_px, sy = board_top + last_move_from_r * cell_px;
                draw_piece(piece, sx + (cx - sx) * ease, sy + (cy - sy) * ease, true);
            } else {
                draw_piece(piece, cx, cy, false);
            }
        }
    }

    if (selected_row >= 0 && selected_col >= 0) {
        float cx = board_left + selected_col * cell_px, cy = board_top + selected_row * cell_px;
        if (!is_dragging) {
            int alpha = 80 + (int)(40 * std::sin(anim_time));
            uint32_t hl_color = (alpha << 24) | 0x00FF33; 
            draw_circle_gl(cx, cy, cell_px / 2.0f - 2.0f, hl_color, 1); 
        }
        
        int moves[ROWS*COLS][2];
        int cnt = generate_moves(selected_row, selected_col, moves);
        for (int i=0; i<cnt; i++) {
            float mcx = board_left + moves[i][1] * cell_px;
            float mcy = board_top + moves[i][0] * cell_px;
            int target_piece = board[moves[i][0]][moves[i][1]];
            
            if (target_piece != 0) {
                draw_circle_gl(mcx, mcy, cell_px / 2.2f, 0x88000000, 1);
                draw_circle_gl(mcx, mcy, cell_px / 2.3f, 0xFFFF3333, 2, 0.15f);
            } else {
                draw_circle_gl(mcx, mcy, cell_px / 4.5f, 0x66000000, 1); 
                draw_circle_gl(mcx, mcy, cell_px / 6.0f, 0xEEFFCC00, 1); 
            }
        }
    }
    if (is_dragging && selected_row >= 0 && selected_col >= 0) {
        draw_piece(board[selected_row][selected_col], drag_x, drag_y, true);
    }
    
    if (red_is_ai) draw_circle_gl(w/2 - spacing + btn_w/2.0f - 12, b_btn_y, 5, 0xFF00CC22, 1);
    if (black_is_ai) draw_circle_gl(w/2 + spacing - btn_w/2.0f + 12, t_btn_y, 5, 0xFF00CC22, 1);

    {
        float bg_margin = cell_px * 0.8f;
        float bg_bottom_edge = board_top + (9.0f * cell_px) + bg_margin;
        float bottom_buttons_top = b_btn_y - (cell_px * 0.45f);
        float gap_bottom_y = bg_bottom_edge + (bottom_buttons_top - bg_bottom_edge) / 2.0f;

        float bg_top_edge = board_top - bg_margin;
        float top_buttons_bottom = t_btn_y + (cell_px * 0.45f);
        float gap_top_y = bg_top_edge - (bg_top_edge - top_buttons_bottom) / 2.0f;

        float target_gap_y = (current_turn == 1) ? gap_bottom_y : gap_top_y;

        float base_r = ai_is_thinking ? (10.0f + 4.0f * std::sin(anim_time * 6.0f)) : (7.0f + 2.0f * std::sin(anim_time * 3.0f));
        int alpha_base = ai_is_thinking ? 240 : 180;
        uint32_t main_color = (alpha_base << 24) | (c_turn_color & 0x00FFFFFF);

        draw_circle_gl(w / 2.0f, target_gap_y, base_r * 2.5f, main_color & 0x33FFFFFF, 3);
        draw_circle_gl(w / 2.0f, target_gap_y, base_r * 1.6f, main_color & 0x88FFFFFF, 2, 0.15f);
        draw_circle_gl(w / 2.0f, target_gap_y, base_r, main_color, 1);
        draw_circle_gl(w / 2.0f, target_gap_y, base_r * 0.35f, 0xFFFFFFFF, 1);
    }

    if (game_state != 0) {
        draw_rect_gl(0, 0, w, h, 0x88000000, 1);
        
        float pw = cell_px * 5.2f, ph = cell_px * 3.6f;
        
        bool is_black_side = (game_state == 4) ? (confirming_player == 1) : (confirming_player == -1);
        uint32_t dialog_border_color = is_black_side ? 0xFF333333 : line_color;
        
        float bx = w/2.0f - pw/2.0f;
        float by = h/2.0f - ph/2.0f;
        
        if (is_black_side) {
            by = h/2.0f - ph/2.0f - cell_px * 0.8f; 
        } else {
            by = h/2.0f - ph/2.0f + cell_px * 0.8f;
        }

        draw_rect_gl(bx - 4, by - 4, pw + 8, ph + 8, 0x33000000, 1);
        draw_rect_gl(bx, by, pw, ph, btn_bg, 1); 
        draw_rect_gl(bx, by, pw, ph, dialog_border_color, 0, 2.5f);
        draw_rect_gl(bx + 3, by + 3, pw - 6, ph - 6, dialog_border_color, 0, 1.0f);

        float text_y, b_y;
        if (is_black_side) {
            text_y = by + cell_px * 2.5f; 
            b_y = by + cell_px * 0.35f;
        } else {
            text_y = by + cell_px * 1.1f;
            b_y = by + ph - cell_px * 1.3f;
        }

        if (game_state == 3) {
            if (winner == 0) {
                draw_char_gl(32, w/2.0f - cell_px*0.5f, text_y, line_color, is_black_side);
                draw_char_gl(33, w/2.0f + cell_px*0.5f, text_y, line_color, is_black_side);
            } else {
                draw_char_gl((winner == 1) ? 28 : 29, w/2.0f - cell_px*0.9f, text_y, line_color, is_black_side);
                draw_char_gl(30, w/2.0f,               text_y, line_color, is_black_side);
                draw_char_gl(31, w/2.0f + cell_px*0.9f, text_y, line_color, is_black_side);
            }
            
            float b_w = cell_px * 2.8f;
            float b_x = w/2.0f - b_w / 2.0f;
            
            draw_rect_gl(b_x, b_y, b_w, cell_px * 0.95f, 0xDDBB0000, 1, 0.0f); 
            draw_rect_gl(b_x, b_y, b_w, cell_px * 0.95f, line_color, 0, 2.5f);             
            draw_rect_gl(b_x + 2, b_y + 2, b_w - 4, cell_px * 0.95f - 4, line_color, 0, 1.0f); 
            
            draw_char_gl(24, w/2.0f - cell_px*0.5f, b_y + cell_px*0.48f, line_color, is_black_side);
            draw_char_gl(25, w/2.0f + cell_px*0.5f, b_y + cell_px*0.48f, line_color, is_black_side);
        } else {
            auto draw_dialog_title = [&](std::vector<int> chars) {
                int n = chars.size();
                float spacing = cell_px * 0.85f;
                float start_x = w/2.0f - (n - 1) * spacing / 2.0f;
                for (int i = 0; i < n; i++) {
                    int c = is_black_side ? chars[n - 1 - i] : chars[i];
                    draw_char_gl(c, start_x + i * spacing, text_y, line_color, is_black_side);
                }
            };

            if (game_state == 4) {
                draw_dialog_title({36, 37, 16, 17}); 
            } else if (game_state == 1) {
                draw_dialog_title({24, 25, 40, 41}); 
            } else {
                draw_dialog_title({24, 25, 22, 23}); 
            }

            auto draw_btn2 = [&](float bx2, int t1, int t2) {
                float bw2 = cell_px * 2.1f;
                draw_rect_gl(bx2, b_y, bw2, cell_px * 0.95f, 0xDD1A1A1A, 1, 0.0f);
                draw_rect_gl(bx2, b_y, bw2, cell_px * 0.95f, line_color, 0, 2.5f);
                draw_rect_gl(bx2 + 2, b_y + 2, bw2 - 4, cell_px * 0.95f - 4, line_color, 0, 1.0f);
                
                draw_char_gl(is_black_side ? t2 : t1, bx2 + bw2/2.0f - cell_px*0.35f, b_y + cell_px*0.48f, line_color, is_black_side);
                draw_char_gl(is_black_side ? t1 : t2, bx2 + bw2/2.0f + cell_px*0.35f, b_y + cell_px*0.48f, line_color, is_black_side);
            };
            
            int ok_1 = (game_state == 4) ? 34 : 24; 
            int ok_2 = (game_state == 4) ? 35 : 25; 
            int cancel_1 = (game_state == 4) ? 38 : 26; 
            int cancel_2 = (game_state == 4) ? 39 : 27; 

            if (is_black_side) {
                draw_btn2(bx + cell_px * 0.4f, ok_1, ok_2);             
                draw_btn2(bx + pw - cell_px * 0.4f - cell_px * 2.1f, cancel_1, cancel_2); 
            } else {
                draw_btn2(bx + cell_px * 0.4f, cancel_1, cancel_2);             
                draw_btn2(bx + pw - cell_px * 0.4f - cell_px * 2.1f, ok_1, ok_2); 
            }
        }
    }

    flush_batch(gl_atlas_tex);
}

int screen_to_board(float x, float y, int* row, int* col) {
    // 严谨计算：基于 board_left 和 board_top，计算当前坐标距离哪个棋盘交叉点最近
    float rel_x = x - board_left;
    float rel_y = y - board_top;
    
    int c = (int)(rel_x / cell_px + 0.5f);
    int r = (int)(rel_y / cell_px + 0.5f);
    
    // 必须在 0~8 列，0~9 行范围内
    if (c >= 0 && c < COLS && r >= 0 && r < ROWS) {
        // 进一步进行距离校验，防止点在棋盘空白边缘也被误认为选中
        float cx = board_left + c * cell_px;
        float cy = board_top + r * cell_px;
        float dist = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
        
        // 【修复】将判定阈值从 0.55f 扩大到 0.85f，允许玩家有更大容错，彻底解决选不上、拖不动的问题
        if (dist <= cell_px * 0.85f) {
            *row = r;
            *col = c;
            return 1;
        }
    }
    return 0;
}

int check_ui_click(float x, float y) {
    int btn_w = (int)(cell_px * 2.2f);
    int btn_h = (int)(cell_px * 0.9f);
    int spacing = btn_w + 15;
    int b_btn_y = board_top + 9 * cell_px + (int)(cell_px * 1.6f);
    int t_btn_y = board_top - (int)(cell_px * 1.6f);
    
    // 顶部按钮行 (黑方：托管(23)、认输(21)、退出(22))
    if (y > t_btn_y - btn_h/2 && y < t_btn_y + btn_h/2) {
        if (std::abs(x - (win_width/2 + spacing)) < btn_w/2) { return 23; }
        if (std::abs(x - (win_width/2)) < btn_w/2) return 21;
        if (std::abs(x - (win_width/2 - spacing)) < btn_w/2) return 22;
    }
    // 底部按钮行 (红方：退(13)、和/重置(11)、托管/AI(12))
    if (y > b_btn_y - btn_h/2 && y < b_btn_y + btn_h/2) {
        if (std::abs(x - (win_width/2 - spacing)) < btn_w/2) return 13;
        if (std::abs(x - (win_width/2)) < btn_w/2) return 11;
        if (std::abs(x - (win_width/2 + spacing)) < btn_w/2) { return 12; }
    }
    
    return 0;
}

int check_popup_click(float x, float y) {
    if (game_state == 0) return 0;
    float ph = cell_px * 3.6f;
    bool is_black_side = (game_state == 4) ? (confirming_player == 1) : (confirming_player == -1);
    float by = win_height/2.0f - ph/2.0f;
    if (is_black_side) by = win_height/2.0f - ph/2.0f - cell_px * 0.8f;
    else by = win_height/2.0f - ph/2.0f + cell_px * 0.8f;

    float b_y;
    if (is_black_side) {
        b_y = by + cell_px * 0.35f;
    } else {
        b_y = by + ph - cell_px * 1.3f;
    }
    
    if (y > b_y - cell_px*0.5f && y < b_y + cell_px*1.0f) {
        if (game_state == 3) {
            float b_w = cell_px * 2.8f;
            if (std::abs(x - win_width/2.0f) < b_w / 2.0f) return 2;
        } else {
            float pw = cell_px * 5.2f;
            float bx = win_width/2.0f - pw/2.0f;
            float bw2 = cell_px * 2.1f;
            
            if (is_black_side) {
                if (x >= bx + cell_px * 0.4f && x <= bx + cell_px * 0.4f + bw2) return 2;
                if (x >= bx + pw - cell_px * 0.4f - bw2 && x <= bx + pw - cell_px * 0.4f) return 1;
            } else {
                if (x >= bx + cell_px * 0.4f && x <= bx + cell_px * 0.4f + bw2) return 1;
                if (x >= bx + pw - cell_px * 0.4f - bw2 && x <= bx + pw - cell_px * 0.4f) return 2;
            }
        }
    }
    return 0;
}
