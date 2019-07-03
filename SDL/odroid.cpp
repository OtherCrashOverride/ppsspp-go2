#include "odroid.h"

#include <EGL/egl.h>

#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>


const int DEFAULT_WIDTH = 1280;
const int DEFAULT_HEIGHT = 720;
const char* WINDOW_TITLE = "X11Window";

const float quad[] =
{
	-1,  1, 0,
	-1, -1, 0,
	1, -1, 0,

	1, -1, 0,
	1,  1, 0,
	-1,  1, 0
};

float quadUV[] =
{
	0, 0,
	0, 1,
	1, 1,

	1, 1,
	1, 0,
	0, 0
};


const char* vertexSource = "\n \
attribute mediump vec4 Attr_Position;\n \
attribute mediump vec2 Attr_TexCoord0;\n \
\n \
uniform mat4 WorldViewProjection;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
\n \
  gl_Position = Attr_Position * WorldViewProjection;\n \
  TexCoord0 = Attr_TexCoord0;\n \
}\n \
\n \
 ";

const char* fragmentSource = "\n \
uniform lowp sampler2D DiffuseMap;\n \
\n \
varying mediump vec2 TexCoord0;\n \
\n \
void main()\n \
{\n \
  mediump vec4 rgba = texture2D(DiffuseMap, TexCoord0);\n \
\n \
  gl_FragColor = rgba;\n \
}\n \
\n \
";

struct timeval startTime;
struct timeval endTime;
double elapsed = 0;
int isRunning = 0;

void Stopwatch_Start()
{
	gettimeofday(&startTime, NULL);
	isRunning = 1;
}

void Stopwatch_Stop()
{
	gettimeofday(&endTime, NULL);

	isRunning = 0;
	elapsed = Stopwatch_Elapsed();
}

void Stopwatch_Reset()
{
	elapsed = 0;

	gettimeofday(&startTime, NULL);
	endTime = startTime;
}

double Stopwatch_Elapsed()
{
	if (isRunning)
	{
		gettimeofday(&endTime, NULL);
	}

	double seconds = (endTime.tv_sec - startTime.tv_sec);
	double milliseconds = ((double)(endTime.tv_usec - startTime.tv_usec)) / 1000000.0;

	return elapsed + seconds + milliseconds;
}



// static void Egl_CheckError()
// {
// 	EGLint error = eglGetError();
// 	if (error != EGL_SUCCESS)
// 	{
// 		printf("eglGetError failed: 0x%x\n", error);
// 		exit(1);
// 	}
// }

// static EGLDisplay Egl_Intialize(NativeDisplayType display)
// {
// 	EGLDisplay eglDisplay = eglGetDisplay(display);
// 	if (eglDisplay == EGL_NO_DISPLAY)
// 	{
// 		printf("eglGetDisplay failed.\n");
//         exit(1);
// 	}


// 	// Initialize EGL
// 	EGLint major;
// 	EGLint minor;
// 	EGLBoolean success = eglInitialize(eglDisplay, &major, &minor);
// 	if (success != EGL_TRUE)
// 	{
// 		Egl_CheckError();
// 	}

// 	printf("EGL: major=%d, minor=%d\n", major, minor);
// 	printf("EGL: Vendor=%s\n", eglQueryString(eglDisplay, EGL_VENDOR));
// 	printf("EGL: Version=%s\n", eglQueryString(eglDisplay, EGL_VERSION));
// 	printf("EGL: ClientAPIs=%s\n", eglQueryString(eglDisplay, EGL_CLIENT_APIS));
// 	printf("EGL: Extensions=%s\n", eglQueryString(eglDisplay, EGL_EXTENSIONS));
// 	printf("EGL: ClientExtensions=%s\n", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
// 	printf("\n");

// 	return eglDisplay;
// }

// static EGLConfig Egl_FindConfig(EGLDisplay eglDisplay, int redBits, int greenBits, int blueBits, int alphaBits, int depthBits, int stencilBits)
// {
// 	EGLint configAttributes[] =
// 	{
// 		EGL_RED_SIZE,            redBits,
// 		EGL_GREEN_SIZE,          greenBits,
// 		EGL_BLUE_SIZE,           blueBits,
// 		EGL_ALPHA_SIZE,          alphaBits,

// 		EGL_DEPTH_SIZE,          depthBits,
// 		EGL_STENCIL_SIZE,        stencilBits,

// 		EGL_SURFACE_TYPE,        EGL_WINDOW_BIT,

// 		EGL_NONE
// 	};


// 	int num_configs;
// 	EGLBoolean success = eglChooseConfig(eglDisplay, configAttributes, NULL, 0, &num_configs);
// 	if (success != EGL_TRUE)
// 	{
// 		Egl_CheckError();
// 	}


// 	//EGLConfig* configs = new EGLConfig[num_configs];
//     EGLConfig* configs = malloc(sizeof(EGLConfig) * num_configs);
//     if (!configs)
//     {
//         printf("malloc failed.\n");
//         exit(1);
//     }

// 	success = eglChooseConfig(eglDisplay, configAttributes, configs, num_configs, &num_configs);
// 	if (success != EGL_TRUE)
// 	{
// 		Egl_CheckError();
// 	}


// 	EGLConfig match = 0;

// 	for (int i = 0; i < num_configs; ++i)
// 	{
// 		EGLint configRedSize;
// 		EGLint configGreenSize;
// 		EGLint configBlueSize;
// 		EGLint configAlphaSize;
// 		EGLint configDepthSize;
// 		EGLint configStencilSize;

// 		eglGetConfigAttrib(eglDisplay, configs[i], EGL_RED_SIZE, &configRedSize);
// 		eglGetConfigAttrib(eglDisplay, configs[i], EGL_GREEN_SIZE, &configGreenSize);
// 		eglGetConfigAttrib(eglDisplay, configs[i], EGL_BLUE_SIZE, &configBlueSize);
// 		eglGetConfigAttrib(eglDisplay, configs[i], EGL_ALPHA_SIZE, &configAlphaSize);
// 		eglGetConfigAttrib(eglDisplay, configs[i], EGL_DEPTH_SIZE, &configDepthSize);
// 		eglGetConfigAttrib(eglDisplay, configs[i], EGL_STENCIL_SIZE, &configStencilSize);

// 		//printf("Egl::FindConfig: index=%d, red=%d, green=%d, blue=%d, alpha=%d\n",
// 		//	i, configRedSize, configGreenSize, configBlueSize, configAlphaSize);

// 		if (configRedSize == redBits &&
// 			configBlueSize == blueBits &&
// 			configGreenSize == greenBits &&
// 			configAlphaSize == alphaBits &&
// 			configDepthSize == depthBits &&
// 			configStencilSize == stencilBits)
// 		{
// 			match = configs[i];
// 			break;
// 		}
// 	}

// 	return match;
// }

// ---------

// X11Window* X11Window_Create()
// {
//     X11Window* result = malloc(sizeof(X11Window));
//     if (!result)
//     {
//         printf("malloc failed.");
//         exit(1);
//     }

//     memset(result, 0, sizeof(*result));


// 	XInitThreads();


// 	result->display = XOpenDisplay(NULL);
// 	if (result->display == NULL)
// 	{
// 		printf("XOpenDisplay failed.\n");
//         exit(1);
// 	}

// 	result->width = XDisplayWidth(result->display, 0);
// 	result->height = XDisplayHeight(result->display, 0);
// 	printf("X11Window: width=%d, height=%d\n", result->width, result->height);


// 	// Egl
// 	result->eglDisplay = Egl_Intialize((NativeDisplayType)result->display);

// 	EGLConfig eglConfig = Egl_FindConfig(result->eglDisplay, 8, 8, 8, 8, 24, 8);
// 	//EGLConfig eglConfig = Egl::FindConfig(eglDisplay, 8, 8, 8, 0, 24, 8);
// 	if (eglConfig == 0)
//     {
// 		printf("Compatible EGL config not found.\n");
//         exit(1);
//     }

// 	// Get the native visual id associated with the config
// 	int xVisual;
// 	eglGetConfigAttrib(result->eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &xVisual);


// 	// Window
// 	result->root = XRootWindow(result->display, XDefaultScreen(result->display));


// 	XVisualInfo visTemplate;
// 	visTemplate.visualid = xVisual;
// 	//visTemplate.depth = 32;	// Alpha required


// 	int num_visuals;
// 	result->visInfoArray = XGetVisualInfo(result->display,
// 		VisualIDMask, //VisualDepthMask,
// 		&visTemplate,
// 		&num_visuals);

// 	if (num_visuals < 1 || result->visInfoArray == NULL)
// 	{
// 		printf("XGetVisualInfo failed.\n");
//         exit(1);
// 	}

// 	XVisualInfo visInfo = result->visInfoArray[0];


// 	XSetWindowAttributes attr = { 0 };
// 	attr.background_pixel = 0;
// 	attr.border_pixel = 0;
// 	attr.colormap = XCreateColormap(result->display,
// 		result->root,
// 		visInfo.visual,
// 		AllocNone);
// 	attr.event_mask = (StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask | PointerMotionMask | ButtonPressMask | ButtonReleaseMask);

// 	unsigned long mask = (CWBackPixel | CWBorderPixel | CWColormap | CWEventMask);


// 	result->xwin = XCreateWindow(result->display,
// 		result->root,
// 		0,
// 		0,
// 		DEFAULT_WIDTH, //width,
// 		DEFAULT_HEIGHT, //height,
// 		0,
// 		visInfo.depth,
// 		InputOutput,
// 		visInfo.visual,
// 		mask,
// 		&attr);

// 	if (result->xwin == 0)
//     {
// 		printf("XCreateWindow failed.\n");
//         exit(1);
//     }

// 	printf("X11Window: xwin = %lu\n", result->xwin);


// 	//XWMHints hints = { 0 };
// 	//XSizeHints* hints = XAllocSizeHints();
// 	//hints.input = true;
// 	//hints.flags = InputHint;

// 	//XSetWMHints(display, xwin, &hints);


// 	// Set the window name
// 	XStoreName(result->display, result->xwin, WINDOW_TITLE);

// 	// Show the window
// 	XMapRaised(result->display, result->xwin);



// 	// Register to be notified when window is closed
// 	result->wm_delete_window = XInternAtom(result->display, "WM_DELETE_WINDOW", 0);
// 	XSetWMProtocols(result->display, result->xwin, &result->wm_delete_window, 1);





// 	EGLint windowAttr[] = {
// 		EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
// 		EGL_NONE };

// 	result->surface = eglCreateWindowSurface(result->eglDisplay, eglConfig, (NativeWindowType)result->xwin, windowAttr);

// 	if (result->surface == EGL_NO_SURFACE)
// 	{
// 		//Egl_CheckError();
//         printf("eglCreateWindowSurface failed.\n");
//         exit(1);
// 	}


// 	// Create a context
// 	eglBindAPI(EGL_OPENGL_ES_API);

// 	EGLint contextAttributes[] = {
// 		EGL_CONTEXT_CLIENT_VERSION, 2,
// 		EGL_NONE };

// 	result->context = eglCreateContext(result->eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttributes);
// 	if (result->context == EGL_NO_CONTEXT)
// 	{
// 		//Egl::CheckError();
//         printf("eglCreateContext failed.\n");
//         exit(1);
// 	}

// 	EGLBoolean success = eglMakeCurrent(result->eglDisplay, result->surface, result->surface, result->context);
// 	if (success != EGL_TRUE)
// 	{
// 		//Egl::CheckError();
//         printf("eglMakeCurrent failed.\n");
//         exit(1);
// 	}

//     return result;
// }

// void X11Window_Destroy(X11Window* x11Window)
// {
// 	XDestroyWindow(x11Window->display, x11Window->xwin);
// 	XFree(x11Window->visInfoArray);
// 	XCloseDisplay(x11Window->display);

//     free(x11Window);
// }


// void X11Window_WaitForMessage(X11Window* x11Window)
// {
// 	XEvent xev;
// 	XPeekEvent(x11Window->display, &xev);
// }

// int X11Window_ProcessMessages(X11Window* x11Window)
// {
// 	int run = 1;

// 	// Use XPending to prevent XNextEvent from blocking
// 	while (XPending(x11Window->display) != 0)
// 	{
// 		XEvent xev;
// 		XNextEvent(x11Window->display, &xev);

// 		switch (xev.type)
// 		{
// 		case ConfigureNotify:
// 		{
// 			XConfigureEvent* xConfig = (XConfigureEvent*)&xev;

// 			int xx;
// 			int yy;
// 			Window child;
// 			XTranslateCoordinates(x11Window->display, x11Window->xwin, x11Window->root,
// 				0, 0,
// 				&xx,
// 				&yy,
// 				&child);

// 			glViewport(0, 0, xConfig->width, xConfig->height);

// 			break;
// 		}

// 		case ClientMessage:
// 		{
// 			XClientMessageEvent* xclient = (XClientMessageEvent*)&xev;

// 			if (xclient->data.l[0] == (long)x11Window->wm_delete_window)
// 			{
// 				printf("X11Window: Window closed.\n");
// 				run = 0;
// 			}
// 		}
// 		break;
// 		}
// 	}

// 	return run;
// }

// void X11Window_SwapBuffers(X11Window* x11Window)
// {
// 	eglSwapBuffers(x11Window->eglDisplay, x11Window->surface);
// 	Egl_CheckError();
// }

// void X11Window_HideMouse(X11Window* x11Window)
// {
// 	static char bitmap[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
// 	Pixmap pixmap = XCreateBitmapFromData(x11Window->display, x11Window->xwin, bitmap, 8, 8);

// 	XColor black = { 0 };
// 	Cursor cursor = XCreatePixmapCursor(x11Window->display,
// 		pixmap,
// 		pixmap,
// 		&black,
// 		&black,
// 		0,
// 		0);

// 	XDefineCursor(x11Window->display, x11Window->xwin, cursor);

// 	XFreeCursor(x11Window->display, cursor);
// 	XFreePixmap(x11Window->display, pixmap);
// }

// void X11Window_UnHideMouse(X11Window* x11Window)
// {
// 	XUndefineCursor(x11Window->display, x11Window->xwin);
// }

// void X11Window_SetFullscreen(X11Window* x11Window, int value)
// {
// 	// Fullscreen
// 	Atom wm_state = XInternAtom(x11Window->display, "_NET_WM_STATE", 0);
// 	Atom fullscreen = XInternAtom(x11Window->display, "_NET_WM_STATE_FULLSCREEN", 0);

// 	XClientMessageEvent xcmev = { 0 };
// 	xcmev.type = ClientMessage;
// 	xcmev.window = x11Window->xwin;
// 	xcmev.message_type = wm_state;
// 	xcmev.format = 32;
// 	xcmev.data.l[0] = value ? 1 : 0;
// 	xcmev.data.l[1] = fullscreen;

// 	XSendEvent(x11Window->display,
// 		x11Window->root,
// 		0,
// 		(SubstructureRedirectMask | SubstructureNotifyMask),
// 		(XEvent*)&xcmev);


// 	//HideMouse();

// }

// -----------

void GL_CheckError()
{
	int error = glGetError();
	if (error != GL_NO_ERROR)
	{
		printf("GL error: error=0x%x\n", error);
        exit(1);
	}
}


Blit* Blit_Create()
{
    Blit* result = (Blit*)malloc(sizeof(Blit));
    if (!result)
    {
        printf("malloc failed.\n");
        exit(1);
    }

    memset(result, 0, sizeof(*result));


    // Texture
	//result->texture2D;
	glGenTextures(1, &result->texture2D);
	GL_CheckError();

	glActiveTexture(GL_TEXTURE0);
	GL_CheckError();

	glBindTexture(GL_TEXTURE_2D, result->texture2D);
	GL_CheckError();

#if 0
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	GL_CheckError();

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	GL_CheckError();
#else
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	GL_CheckError();

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	GL_CheckError();
#endif

	//glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
	//GL::CheckError();


	// Shader
	//GLuint vertexShader = 0;
	//GLuint fragmentShader = 0;

	for (int i = 0; i < 2; ++i)
	{
		GLuint shaderType;
		const char* sourceCode;

		if (i == 0)
		{
			shaderType = GL_VERTEX_SHADER;
			sourceCode = vertexSource;
		}
		else
		{
			shaderType = GL_FRAGMENT_SHADER;
			sourceCode = fragmentSource;
		}

		GLuint openGLShaderID = glCreateShader(shaderType);
		GL_CheckError();

		const char* glSrcCode[1] = { sourceCode };
		const int lengths[1] = { -1 }; // Tell OpenGL the string is NULL terminated

		glShaderSource(openGLShaderID, 1, glSrcCode, lengths);
		GL_CheckError();

		glCompileShader(openGLShaderID);
		GL_CheckError();


		GLint param;

		glGetShaderiv(openGLShaderID, GL_COMPILE_STATUS, &param);
		GL_CheckError();

		if (param == GL_FALSE)
		{
			printf("Shader Compilation Failed. (i=%d)", i);
            exit(1);
		}

		if (i == 0)
		{
			result->vertexShader = openGLShaderID;
		}
		else
		{
			result->fragmentShader = openGLShaderID;
		}
	}


	// Program
	result->openGLProgramID = glCreateProgram();
	GL_CheckError();

	glAttachShader(result->openGLProgramID, result->vertexShader);
	GL_CheckError();

	glAttachShader(result->openGLProgramID, result->fragmentShader);
	GL_CheckError();


	// Bind
	glEnableVertexAttribArray(0);
	GL_CheckError();

	glBindAttribLocation(result->openGLProgramID, 0, "Attr_Position");
	GL_CheckError();

	glEnableVertexAttribArray(1);
	GL_CheckError();

	glBindAttribLocation(result->openGLProgramID, 1, "Attr_TexCoord0");
	GL_CheckError();

	glLinkProgram(result->openGLProgramID);
	GL_CheckError();

	glUseProgram(result->openGLProgramID);
	GL_CheckError();


	// Get program uniform(s)
	result->wvpUniformLocation = glGetUniformLocation(result->openGLProgramID, "WorldViewProjection");
	GL_CheckError();

	if (result->wvpUniformLocation < 0)
    {
		printf("glGetUniformLocation failed.\n");
        exit(1);
    }


	// Setup OpenGL
	glClearColor(1, 0, 0, 1);	// RED for diagnostic use
	//glClearColor(0, 0, 0, 0);	// Transparent Black
	GL_CheckError();

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GL_CheckError();

	glEnable(GL_CULL_FACE);
	GL_CheckError();

	glCullFace(GL_BACK);
	GL_CheckError();

	glFrontFace(GL_CCW);
	GL_CheckError();


    return result;
}


Matrix4 Matrix4_CreateTranspose(Matrix4* matrix)
{
	Matrix4 result = {matrix->M11, matrix->M21, matrix->M31, matrix->M41,
		matrix->M12, matrix->M22, matrix->M32, matrix->M42,
		matrix->M13, matrix->M23, matrix->M33, matrix->M43,
		matrix->M14, matrix->M24, matrix->M34, matrix->M44};


	return result;
}

Matrix4 Matrix4_CreateScale(float x, float y, float z)
{
	Matrix4 result = {
		x, 0, 0, 0,
		0, y, 0, 0,
		0, 0, z, 0,
		0, 0, 0, 1};

    return result;
}

void Blit_Draw(Blit* blit, Matrix4* matrix, float s, float t, float u, float v)
{
    glClearColor(0.03125, 0.03125, 0.03125, 1);
    glClear(GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT |
        GL_STENCIL_BUFFER_BIT);


    float tempUV[] =
	{
		s, t,
		s, v,
		u, v,

		u, v,
		u, t,
		s, t
	};

	// for (int i = 0; i < 12; ++i)
	// 	quadUV[i] = tempUV[i];

    // Quad

    // Set the quad vertex data
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * 4, quad);
    GL_CheckError();

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * 4, tempUV);
    GL_CheckError();


    // Set the matrix
    //Matrix4 transpose = Matrix4::CreateTranspose(Matrix4::Identity);
    Matrix4 transpose = Matrix4_CreateTranspose(matrix);
    float* wvpValues = &transpose.M11;

    glUniformMatrix4fv(blit->wvpUniformLocation, 1, GL_FALSE, wvpValues);
    GL_CheckError();


    // Draw
    glDrawArrays(GL_TRIANGLES, 0, 3 * 2);
    GL_CheckError();

}
