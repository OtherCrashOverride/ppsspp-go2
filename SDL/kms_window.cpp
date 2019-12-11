#if 0
/*
*
* Copyright (C) 2016 OtherCrashOverride@users.noreply.github.com.
* All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2, as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
*/

#include "kms_window.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>


// #include "Egl.h"
// #include "GL.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/select.h>

#include <pthread.h>
#include <semaphore.h>

#include "RgaApi.h"
#include <drm/drm_fourcc.h>


static struct {
	struct gbm_device *dev;
	struct gbm_surface *surface;
} gbm;

static struct {
	int fd;
	drmModeModeInfo *mode;
	uint32_t crtc_id;
	uint32_t connector_id;
} drm;

struct drm_fb {
	struct gbm_bo *bo;
	uint32_t fb_id;
};


//static fd_set fds;
static volatile int waiting_for_flip = 1;
static drmEventContext evctx;

static sem_t render_sem;
static sem_t render_done_sem;
static pthread_t render_thread;


static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
				      const drmModeEncoder *encoder) {
	int i;

	for (i = 0; i < resources->count_crtcs; i++) {
		/* possible_crtcs is a bitmask as described here:
		 * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
		 */
		const uint32_t crtc_mask = 1 << i;
		const uint32_t crtc_id = resources->crtcs[i];
		if (encoder->possible_crtcs & crtc_mask) {
			return crtc_id;
		}
	}

	/* no match found */
	return -1;
}

static uint32_t find_crtc_for_connector(const drmModeRes *resources,
					const drmModeConnector *connector) {
	int i;

	for (i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoder_id = connector->encoders[i];
		drmModeEncoder *encoder = drmModeGetEncoder(drm.fd, encoder_id);

		if (encoder) {
			const uint32_t crtc_id = find_crtc_for_encoder(resources, encoder);

			drmModeFreeEncoder(encoder);
			if (crtc_id != 0) {
				return crtc_id;
			}
		}
	}

	/* no match found */
	return -1;
}

static int init_drm(void)
{
	drmModeRes *resources;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	int i, area;

	drm.fd = open("/dev/dri/card0", O_RDWR);

	if (drm.fd < 0) {
		printf("could not open drm device\n");
		return -1;
	}

	resources = drmModeGetResources(drm.fd);
	if (!resources) {
		printf("drmModeGetResources failed: %s\n", strerror(errno));
		return -1;
	}

	/* find a connected connector: */
	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(drm.fd, resources->connectors[i]);
		if (connector->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
		drmModeFreeConnector(connector);
		connector = NULL;
	}

	if (!connector) {
		/* we could be fancy and listen for hotplug events and wait for
		 * a connector..
		 */
		printf("no connected connector!\n");
		return -1;
	}

	/* find prefered mode or the highest resolution mode: */
	for (i = 0, area = 0; i < connector->count_modes; i++) {
		drmModeModeInfo *current_mode = &connector->modes[i];

#if 1
		if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
			drm.mode = current_mode;
		}

		int current_area = current_mode->hdisplay * current_mode->vdisplay;
		if (current_area > area) {
			drm.mode = current_mode;
			area = current_area;
		}
#else
		if (current_mode->hdisplay == 1280 && current_mode->vdisplay == 720 && current_mode->vrefresh == 60)
		{
			drm.mode = current_mode;
			break;
		}
#endif

	}

	if (!drm.mode) {
		printf("could not find mode!\n");
		return -1;
	}

	/* find encoder: */
	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm.fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id)
			break;
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (encoder) {
		drm.crtc_id = encoder->crtc_id;
	} else {
		uint32_t crtc_id = find_crtc_for_connector(resources, connector);
		if (crtc_id == 0) {
			printf("no crtc found!\n");
			return -1;
		}

		drm.crtc_id = crtc_id;
	}

	drm.connector_id = connector->connector_id;
	gbm.dev = gbm_create_device(drm.fd);

	// FD_ZERO(&fds);
	// FD_SET(0, &fds);
	// FD_SET(drm.fd, &fds);


	// memset(&evctx, 0, sizeof(evctx));

	// evctx.version = DRM_EVENT_CONTEXT_VERSION;
	// evctx.page_flip_handler = page_flip_handler;


	return 0;
}

static int init_gbm(int visual_id, int width, int height)
{

	gbm.surface = gbm_surface_create(gbm.dev,
			width, height,
			/*GBM_FORMAT_XRGB8888*/ visual_id,
			GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!gbm.surface) {
		printf("failed to create gbm surface\n");
		return -1;
	}

	return 0;
}

static void drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = (struct drm_fb *)data;
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	if (fb->fb_id)
		drmModeRmFB(drm.fd, fb->fb_id);

	free(fb);
}

static struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
	struct drm_fb *fb = (struct drm_fb *)gbm_bo_get_user_data(bo);
	uint32_t width, height, stride, handle;
	int ret;

	if (fb)
		return fb;

	fb = (struct drm_fb *)calloc(1, sizeof *fb);
	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	stride = gbm_bo_get_stride(bo);
	handle = gbm_bo_get_handle(bo).u32;

	ret = drmModeAddFB(drm.fd, width, height, 24, 32, stride, handle, &fb->fb_id);
	if (ret) {
		printf("failed to create fb: %s\n", strerror(errno));
		free(fb);
		return NULL;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

	return fb;
}

// static void page_flip_handler(int fd, unsigned int frame,
// 		  unsigned int sec, unsigned int usec, void *data)
// {
// 	int *waiting_for_flip = (int*)data;
// 	*waiting_for_flip = 0;
// }



static void Egl_CheckError()
{
	EGLint error = eglGetError();
	if (error != EGL_SUCCESS)
	{
		printf("eglGetError failed: 0x%x\n", error);
		exit(1);
	}
}

static EGLDisplay Egl_Intialize(NativeDisplayType display)
{
	// EGLDisplay eglDisplay = eglGetDisplay(display);
	// if (eglDisplay == EGL_NO_DISPLAY)
	// {
	// 	printf("eglGetDisplay failed.\n");
    //     exit(1);
	// }
	printf("Egl::Initialize: display=%p\n", display);

	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;
	get_platform_display =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
	assert(get_platform_display != NULL);

	EGLDisplay eglDisplay = get_platform_display(EGL_PLATFORM_GBM_KHR, display, NULL);
	Egl_CheckError();
	
	if (eglDisplay == EGL_NO_DISPLAY)
	{
		printf("eglGetDisplay failed.\n");
		exit(1);
	}


	// Initialize EGL
	EGLint major;
	EGLint minor;
	EGLBoolean success = eglInitialize(eglDisplay, &major, &minor);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}

	printf("EGL: major=%d, minor=%d\n", major, minor);
	printf("EGL: Vendor=%s\n", eglQueryString(eglDisplay, EGL_VENDOR));
	printf("EGL: Version=%s\n", eglQueryString(eglDisplay, EGL_VERSION));
	printf("EGL: ClientAPIs=%s\n", eglQueryString(eglDisplay, EGL_CLIENT_APIS));
	printf("EGL: Extensions=%s\n", eglQueryString(eglDisplay, EGL_EXTENSIONS));
	printf("EGL: ClientExtensions=%s\n", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
	printf("\n");

	return eglDisplay;
}

static EGLConfig Egl_FindConfig(EGLDisplay eglDisplay, int redBits, int greenBits, int blueBits, int alphaBits, int depthBits, int stencilBits)
{
	EGLint configAttributes[] =
	{
		EGL_RED_SIZE,            redBits,
		EGL_GREEN_SIZE,          greenBits,
		EGL_BLUE_SIZE,           blueBits,
		EGL_ALPHA_SIZE,          alphaBits,

		EGL_DEPTH_SIZE,          depthBits,
		EGL_STENCIL_SIZE,        stencilBits,

		EGL_SURFACE_TYPE,        EGL_WINDOW_BIT,

		EGL_NONE
	};


	int num_configs;
	EGLBoolean success = eglChooseConfig(eglDisplay, configAttributes, NULL, 0, &num_configs);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}


	//EGLConfig* configs = new EGLConfig[num_configs];
    EGLConfig* configs = (EGLConfig*)malloc(sizeof(EGLConfig) * num_configs);
    if (!configs)
    {
        printf("malloc failed.\n");
        exit(1);
    }

	success = eglChooseConfig(eglDisplay, configAttributes, configs, num_configs, &num_configs);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}


	EGLConfig match = 0;

	for (int i = 0; i < num_configs; ++i)
	{
		EGLint configRedSize;
		EGLint configGreenSize;
		EGLint configBlueSize;
		EGLint configAlphaSize;
		EGLint configDepthSize;
		EGLint configStencilSize;

		eglGetConfigAttrib(eglDisplay, configs[i], EGL_RED_SIZE, &configRedSize);
		eglGetConfigAttrib(eglDisplay, configs[i], EGL_GREEN_SIZE, &configGreenSize);
		eglGetConfigAttrib(eglDisplay, configs[i], EGL_BLUE_SIZE, &configBlueSize);
		eglGetConfigAttrib(eglDisplay, configs[i], EGL_ALPHA_SIZE, &configAlphaSize);
		eglGetConfigAttrib(eglDisplay, configs[i], EGL_DEPTH_SIZE, &configDepthSize);
		eglGetConfigAttrib(eglDisplay, configs[i], EGL_STENCIL_SIZE, &configStencilSize);

		//printf("Egl::FindConfig: index=%d, red=%d, green=%d, blue=%d, alpha=%d\n",
		//	i, configRedSize, configGreenSize, configBlueSize, configAlphaSize);

		if (configRedSize == redBits &&
			configBlueSize == blueBits &&
			configGreenSize == greenBits &&
			configAlphaSize == alphaBits &&
			configDepthSize == depthBits &&
			configStencilSize == stencilBits)
		{
			match = configs[i];
			break;
		}
	}

	return match;
}

// ---------

static void *render_loop(void *handle_);

KmsWindow* kms_window_create()
{
	KmsWindow* result = (KmsWindow*)malloc(sizeof(KmsWindow));
    if (!result)
    {
        printf("malloc failed.");
        exit(1);
    }

    memset(result, 0, sizeof(*result));


	init_drm();
	result->display = gbm.dev;

	// swap for rotation
	result->width = drm.mode->vdisplay;
	result->height = drm.mode->hdisplay;
	printf("X11Window: width=%d, height=%d\n", result->width, result->height);


	// Egl
	result->eglDisplay = Egl_Intialize((NativeDisplayType)gbm.dev);

	//EGLConfig eglConfig = Egl_FindConfig(result->eglDisplay, 8, 8, 8, 8, 0, 0);
	EGLConfig eglConfig = Egl_FindConfig(result->eglDisplay, 8, 8, 8, 8, 24, 8);
	if (eglConfig == 0)
	{
        printf("Compatible EGL config not found.\n");
        abort();
    }


	// Get the native visual id associated with the config
	int xVisual;
	eglGetConfigAttrib(result->eglDisplay, eglConfig, EGL_NATIVE_VISUAL_ID, &xVisual);

	init_gbm(xVisual, result->width, result->height);



	result->eglSurface = eglCreateWindowSurface(result->eglDisplay, eglConfig, (EGLNativeWindowType)gbm.surface, NULL);	
	if (result->eglSurface == EGL_NO_SURFACE)
	{
		Egl_CheckError();
	}


	// Create a context
	eglBindAPI(EGL_OPENGL_ES_API);

	EGLint contextAttributes[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE };

	result->eglContext = eglCreateContext(result->eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttributes);
	if (result->eglContext == EGL_NO_CONTEXT)
	{
		Egl_CheckError();
	}

	EGLBoolean success = eglMakeCurrent(result->eglDisplay, result->eglSurface, result->eglSurface, result->eglContext);
	if (success != EGL_TRUE)
	{
		Egl_CheckError();
	}

	printf("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
	printf("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	printf("GL_EXTENSIONS: %s\n", glGetString(GL_EXTENSIONS));
	printf("\n");


	sem_init(&render_sem, 0, 0);
	sem_init(&render_done_sem, 0, 1);

	pthread_create(&render_thread, NULL, render_loop, NULL);

    return result;
}

void kms_window_destory(KmsWindow* window)
{
	// XDestroyWindow(display, xwin);
	// XFree(visInfoArray);
	// XCloseDisplay(display);

    free(window);
}


// void X11Window::WaitForMessage()
// {
// 	// XEvent xev;
// 	// XPeekEvent(display, &xev);
// }

bool kms_window_process_messages(KmsWindow* window)
{
	bool run = true;

#if 0
	// Use XPending to prevent XNextEvent from blocking
	while (XPending(display) != 0)
	{
		XEvent xev;
		XNextEvent(display, &xev);

		switch (xev.type)
		{
		case ConfigureNotify:
		{
			XConfigureEvent* xConfig = (XConfigureEvent*)&xev;

			int xx;
			int yy;
			Window child;
			XTranslateCoordinates(display, xwin, root,
				0, 0,
				&xx,
				&yy,
				&child);

			glViewport(0, 0, xConfig->width, xConfig->height);

			break;
		}

		case ClientMessage:
		{
			XClientMessageEvent* xclient = (XClientMessageEvent*)&xev;

			if (xclient->data.l[0] == (long)wm_delete_window)
			{
				printf("X11Window: Window closed.\n");
				run = false;
			}
		}
		break;
		}
	}
#endif

	return run;
}


static uint32_t current_fb_id, next_fb_id;
static struct gbm_bo *current_bo, *next_bo;

void page_flip_handler(int fd, unsigned int frame,
		  unsigned int sec, unsigned int usec, void *data)
{
	// int *waiting_for_flip = (int*)data;
	waiting_for_flip = 0;

	//size_t tmp = (size_t)data;

	if (current_fb_id)
	{
    	drmModeRmFB(fd, current_fb_id);
	}

	current_fb_id = next_fb_id;
   	next_fb_id = 0;

 	if (current_bo)
	{
     	gbm_surface_release_buffer(gbm.surface, current_bo);
	}

	current_bo = next_bo;
	next_bo = NULL;

	//fprintf(stderr, "page_flip_handler=%d\n", (uint32_t)tmp);
}

bool first_frame_flag = true;

void kms_window_swap_buffers(KmsWindow* window)
{
	//eglSwapBuffers(EglDisplay(), Surface());
	//Egl::CheckError();
	//fd_set fds;
	//FD_ZERO(&fds);
	// FD_SET(0, &fds);
	//FD_SET(drm.fd, &fds);

	struct gbm_bo *bo;
	struct drm_fb *fb;
	struct gbm_bo *front_bo;
	//int waiting_for_flip = 1;

	// drmEventContext evctx;
	// memset(&evctx, 0, sizeof(evctx));

	// evctx.version = DRM_EVENT_CONTEXT_VERSION;
	// evctx.page_flip_handler = page_flip_handler;



	if (eglSwapBuffers(window->eglDisplay, window->eglSurface) == EGL_FALSE)
	{
		printf("eglSwapBuffers failed.\n");
		return;
	}

	
	next_bo = gbm_surface_lock_front_buffer(gbm.surface);
	if (!next_bo)
	{
         fprintf(stderr, "failed to lock front buffer: %m\n");
		 return;
	}


	//fb = drm_fb_get_from_bo(next_bo);

	/*
		* Here you could also update drm plane layers if you want
		* hw composition
		*/

	// int ret = 1;
	// waiting_for_flip = 1;

	// //while (ret)
	// {
	// 	ret = drmModePageFlip(drm.fd, drm.crtc_id, fb->fb_id,
	// 		DRM_MODE_PAGE_FLIP_EVENT, (void*)&waiting_for_flip);
	// if (ret) {
	// 	printf("failed to queue page flip: %s\n", strerror(errno));
	// 	return; // -1;
	// }
	// }

	//printf("frame\n");
 
    uint32_t handle = gbm_bo_get_handle(next_bo).u32;
    uint32_t stride = gbm_bo_get_stride(next_bo);
	//uint32_t fb_id = 0;

    int ret = drmModeAddFB(drm.fd, window->width, window->height, 24, 32, stride, handle, &next_fb_id);
	if (ret)
	{
		fprintf(stderr, "failed to create fb\n");
		return;
    }

	if (first_frame_flag)
	{
		ret = drmModeSetCrtc(drm.fd, drm.crtc_id, next_fb_id, 0, 0, &drm.connector_id, 1, drm.mode);
		if (ret) {
			printf("failed to set mode: %s\n", strerror(errno));
			//return ret;
		}

		first_frame_flag = false;

		//gbm_surface_release_buffer(gbm.surface, front_bo);
		return;
	}
	else
	{
		ret = drmModePageFlip(drm.fd, drm.crtc_id, next_fb_id, DRM_MODE_PAGE_FLIP_EVENT, (void*)next_fb_id);
		if (ret)
		{
			fprintf(stderr, "failed to page flip: %m\n");
			abort(); //return;
		}
	}


	// while (waiting_for_flip) {
	// 	ret = select(drm.fd + 1, &fds, NULL, NULL, NULL);
	// 	if (ret < 0) {
	// 		printf("select err: %s\n", strerror(errno));
	// 		return; // ret;
	// 	} else if (ret == 0) {
	// 		printf("select timeout!\n");
	// 		return; // -1;
	// 	} else if (FD_ISSET(0, &fds)) {
	// 		printf("user interrupted!\n");
	// 		break;
	// 	}
	// 	drmHandleEvent(drm.fd, &evctx);
	// }

	fd_set fds;
	FD_ZERO(&fds);
    FD_SET(drm.fd, &fds);

	waiting_for_flip = 1;
	//while(waiting_for_flip)
	{
    	while (select(drm.fd + 1, &fds, NULL, NULL, NULL) == -1)
		{	
		}
	}


	memset(&evctx, 0, sizeof(evctx));

	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.page_flip_handler = page_flip_handler;

	drmHandleEvent(drm.fd, &evctx);


	/* release last buffer to render on again: */
	gbm_surface_release_buffer(gbm.surface, front_bo);
	//bo = next_bo;	
}



// ---



typedef struct swap_buffer
{
	struct gbm_bo *bo;
	uint32_t drm_framebuffer;
	int prime_fd;
} swap_buffer_t;


#define SWAP_BUFFERS_MAX (4)
swap_buffer_t swapBuffers[SWAP_BUFFERS_MAX] = {0};
uint32_t swapBufferCount = 0;
swap_buffer_t* currentSwapBuffer = NULL;


static swap_buffer_t* render_swapbuffer;

typedef struct
{
	struct gbm_bo* dst_bo;
	uint32_t dst_fb;
	int prime_fd;
} blit_buffer_t;

#define BLIT_BUFFER_COUNT (2)

static void *render_loop(void *handle_)
{
	int32_t sample;
	blit_buffer_t blit_buffers[BLIT_BUFFER_COUNT];
	int current_blit_buffer = 0;
	int ret;
	
	ret = c_RkRgaInit();
	if (ret)
	{
		printf("c_RkRgaInit failed.\n");
		abort();
	}

	// bo_t bo_dst;
	// ret = c_RkRgaGetAllocBuffer(&bo_dst, 320, 480, 32);
	// if (ret)
	// {
	// 	printf("c_RkRgaGetAllocBuffer failed.\n");
	// 	abort();
	// }

	// ret = c_RkRgaGetAllocBuffer(&bo_dst, 320, 480, 32);
	// if (ret)
	// {
	// 	printf("c_RkRgaGetAllocBuffer failed.\n");
	// 	abort();
	// }


	// ret = c_RkRgaGetMmap(&bo_dst);
	// if (ret)
	// {
	// 	printf("c_RkRgaGetMmap failed.\n");
	// 	abort();
	// }

	// memset(bo_dst.ptr, 0xff, bo_dst.size);

	//ret = rkRga.RkRgaGetAllocBuffer(&bo_dst, dstWidth, dstHeight, 32);
	
	for (int i = 0; i < BLIT_BUFFER_COUNT; ++i)
	{
		blit_buffers[i].dst_bo = gbm_bo_create(gbm.dev, drm.mode->hdisplay, drm.mode->vdisplay, DRM_FORMAT_ARGB8888, 0);
		if (!blit_buffers[i].dst_bo)
		{
			printf("gbm_bo_create failed.\n");
			abort();
		}

		ret = drmModeAddFB(drm.fd, drm.mode->hdisplay, drm.mode->vdisplay, 24, 32,
						   gbm_bo_get_stride(blit_buffers[i].dst_bo),
						   gbm_bo_get_handle(blit_buffers[i].dst_bo).u32,
						   &blit_buffers[i].dst_fb);
		if (ret)
		{
			fprintf(stderr, "drmModeAddFB failed\n");
			abort();
		}

		blit_buffers[i].prime_fd = gbm_bo_get_fd(blit_buffers[i].dst_bo);
		if (blit_buffers[i].prime_fd < 0)
		{
			fprintf(stderr, "drmModeAddFB failed\n");
			abort();
		}
	}

	//printf("NOTE: CREATED FB: %d\n", dst_fb);

	


	for(;;)
	{
		sem_wait(&render_sem);


    	rga_info_t dst = { 0 };
		dst.fd = blit_buffers[current_blit_buffer].prime_fd;
		dst.mmuFlag = 1;
		//dst.format = RK_FORMAT_RGBA_8888;
		//dst.rotation = HAL_TRANSFORM_ROT_90;
		dst.rect.xoffset = 0;
		dst.rect.yoffset = 0;
		dst.rect.width = drm.mode->hdisplay;
		dst.rect.height = drm.mode->vdisplay;
		dst.rect.wstride = dst.rect.width;
		dst.rect.hstride = dst.rect.height;
		dst.rect.format = RK_FORMAT_RGBA_8888;

   		rga_info_t src = { 0 };
		src.fd = render_swapbuffer->prime_fd;
		src.mmuFlag = 1;
		//src.format = RK_FORMAT_RGBA_8888;
		src.rotation = HAL_TRANSFORM_ROT_270;
		src.rect.xoffset = 0;
		src.rect.yoffset = 0;
		src.rect.width = drm.mode->vdisplay; // rotated
		src.rect.height = drm.mode->hdisplay; // rotated
		src.rect.wstride = src.rect.width;
		src.rect.hstride = src.rect.height;
		src.rect.format = RK_FORMAT_RGBA_8888;


#if 1
		ret = c_RkRgaBlit(&src, &dst, NULL);
		if (ret)
		{
			printf("c_RkRgaBlit failed.\n");
			abort();
		}
#endif

		gbm_surface_release_buffer(gbm.surface, render_swapbuffer->bo);

		if (!currentSwapBuffer)
		{
			//int ret = drmModeSetCrtc(drm.fd, drm.crtc_id, render_swapbuffer->drm_framebuffer, 0, 0, &drm.connector_id, 1, drm.mode);
			int ret = drmModeSetCrtc(drm.fd, drm.crtc_id, blit_buffers[current_blit_buffer].dst_fb, 0, 0, &drm.connector_id, 1, drm.mode);
			if (ret)
			{
				printf("failed to set mode: %s\n", strerror(errno));
				//return ret;
			}
		}
		else
		{
			// ret = drmModePageFlip(drm.fd, drm.crtc_id, swapBuffer->drm_framebuffer, DRM_MODE_PAGE_FLIP_EVENT, (void*)next_fb_id);
			// if (ret)
			// {
			// 	fprintf(stderr, "failed to page flip: %m\n");
			// 	abort(); //return;
			// }

			//int ret = drmModeSetCrtc(drm.fd, drm.crtc_id, render_swapbuffer->drm_framebuffer, 0, 0, &drm.connector_id, 1, drm.mode);
			int ret = drmModeSetCrtc(drm.fd, drm.crtc_id, blit_buffers[current_blit_buffer].dst_fb, 0, 0, &drm.connector_id, 1, drm.mode);
			if (ret)
			{
				printf("failed to set mode: %s\n", strerror(errno));
				//return ret;
			}

			
		}

		currentSwapBuffer = render_swapbuffer;

		++current_blit_buffer;
		if (current_blit_buffer >= BLIT_BUFFER_COUNT)
			current_blit_buffer = 0;

		sem_post(&render_done_sem);
	}

	return NULL;
}


void kms_window_swap_buffers2(KmsWindow* window)
{
	sem_wait(&render_done_sem);

	if (eglSwapBuffers(window->eglDisplay, window->eglSurface) == EGL_FALSE)
	{
		printf("eglSwapBuffers failed.\n");
		return;
	}

	
	struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm.surface);
	if (!bo)
	{
         fprintf(stderr, "failed to lock front buffer: %m\n");
		 return;
	}

	
	swap_buffer_t* swapBuffer = NULL;

	for (int i = 0; i < swapBufferCount; ++i)
	{
		if (swapBuffers[i].bo == bo)
		{
			swapBuffer = &swapBuffers[i];
			break;
		}
	}

	if (!swapBuffer)
	{
		if (swapBufferCount >= SWAP_BUFFERS_MAX)
		{
			fprintf(stderr, "swap buffers count exceeded.\n");
		 	return;
		}

		swapBuffer = &swapBuffers[swapBufferCount++];
		swapBuffer->bo = bo;

		uint32_t gem_handle = gbm_bo_get_handle(bo).u32;
		uint32_t stride = gbm_bo_get_stride(bo);

		int ret = drmModeAddFB(drm.fd, window->width, window->height, 24, 32, stride, gem_handle, &swapBuffer->drm_framebuffer);
		if (ret)
		{
			fprintf(stderr, "failed to create fb\n");
			return;
		}

		swapBuffer->prime_fd = gbm_bo_get_fd(bo);

		printf("added swapbuffer - bo=%p, prime_fd=%d, count=%d\n", bo, swapBuffer->prime_fd, swapBufferCount);
	}

#if 0
	if (!currentSwapBuffer)
	{
#if 0
		int ret = drmModeSetCrtc(drm.fd, drm.crtc_id, swapBuffer->drm_framebuffer, 0, 0, &drm.connector_id, 1, drm.mode);
		if (ret)
		{
			printf("failed to set mode: %s\n", strerror(errno));
			//return ret;
		}
#endif
	}
	else
	{
		// ret = drmModePageFlip(drm.fd, drm.crtc_id, swapBuffer->drm_framebuffer, DRM_MODE_PAGE_FLIP_EVENT, (void*)next_fb_id);
		// if (ret)
		// {
		// 	fprintf(stderr, "failed to page flip: %m\n");
		// 	abort(); //return;
		// }

#if 0
		int ret = drmModeSetCrtc(drm.fd, drm.crtc_id, swapBuffer->drm_framebuffer, 0, 0, &drm.connector_id, 1, drm.mode);
		if (ret)
		{
			printf("failed to set mode: %s\n", strerror(errno));
			//return ret;
		}
#endif
		gbm_surface_release_buffer(gbm.surface, currentSwapBuffer->bo);
	}

	currentSwapBuffer = swapBuffer;
#else

	render_swapbuffer = swapBuffer;
	sem_post(&render_sem);

#endif
}
#endif
