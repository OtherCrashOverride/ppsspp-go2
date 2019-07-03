#pragma once

// #include <EGL/egl.h>

// #define GL_GLEXT_PROTOTYPES
// #include <GLES2/gl2.h>

// #include <X11/Xlib.h>
// #include <X11/Xutil.h>




// typedef struct
// {
//     Display* display;
//     int width;
//     int height;
//     //XVisualInfo* visInfoArray ;
//     Window root;
//     Window xwin;
//     Atom wm_delete_window;

//     EGLDisplay eglDisplay;
//     EGLSurface surface;
//     EGLContext context;
// } X11Window;

typedef unsigned int GLuint;

typedef struct
{
    GLuint texture2D;
    GLuint vertexShader;
    GLuint fragmentShader;
    GLuint openGLProgramID;
    GLuint wvpUniformLocation;
} Blit;

typedef struct
{
	float M11;   //0
	float M12;   //1
	float M13;   //2
	float M14;   //3

	float M21;   //4
	float M22;   //5
	float M23;   //6
	float M24;   //7

	float M31;   //8
	float M32;   //9
	float M33;   //10
	float M34;   //11

	float M41;   //12
	float M42;   //13
	float M43;   //14
	float M44;   //15
} Matrix4;


void Stopwatch_Start();
void Stopwatch_Stop();
void Stopwatch_Reset();
double Stopwatch_Elapsed();


// X11Window* X11Window_Create();
// void X11Window_Destroy(X11Window* x11Window);
// void X11Window_WaitForMessage(X11Window* x11Window);
// int X11Window_ProcessMessages(X11Window* x11Window);
// void X11Window_SwapBuffers(X11Window* x11Window);
// void X11Window_HideMouse(X11Window* x11Window);
// void X11Window_UnHideMouse(X11Window* x11Window);
// void X11Window_SetFullscreen(X11Window* x11Window, int value);


void GL_CheckError();
Blit* Blit_Create();

Matrix4 Matrix4_CreateTranspose(Matrix4* matrix);
Matrix4 Matrix4_CreateScale(float x, float y, float z);

//void Blit_Draw(Blit* blit, Matrix4* matrix, float u, float v);
void Blit_Draw(Blit* blit, Matrix4* matrix, float s, float t, float u, float v);

