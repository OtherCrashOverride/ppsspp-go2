#pragma once

#include <stdbool.h>

#include <EGL/egl.h>

typedef struct
{
    void* display;
    int width;
    int height;


    EGLDisplay eglDisplay;
    EGLSurface eglSurface;
    EGLContext eglContext;
} KmsWindow;

KmsWindow* kms_window_create();
void kms_window_destory(KmsWindow* window);
void kms_window_swap_buffers(KmsWindow* window);
void kms_window_swap_buffers2(KmsWindow* window);

