#ifndef EGL_ENGINE_H
#define EGL_ENGINE_H

// #include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

struct EglEngine {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int width, height;
    bool initialized;
};

bool egl_init(EglEngine* engine, ANativeWindow* window);
void egl_swap(EglEngine* engine);
void egl_terminate(EglEngine* engine);

#endif
