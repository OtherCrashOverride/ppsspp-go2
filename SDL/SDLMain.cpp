// SDL/EGL implementation of the framework.
// This is quite messy due to platform-specific implementations and #ifdef's.
// If your platform is not supported, it is suggested to use Qt instead.

#include <unistd.h>
#include <pwd.h>

#include "SDL.h"
#include "SDL/SDLJoystick.h"
SDLJoystick *joystick = NULL;

#if PPSSPP_PLATFORM(RPI)
#include <bcm_host.h>
#endif

#include <atomic>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>
#include <locale>

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "ext/glslang/glslang/Public/ShaderLang.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "net/resolve.h"
#include "base/NKCodeFromSDL.h"
#include "util/const_map.h"
#include "util/text/utf8.h"
#include "math/math_util.h"
#include "thin3d/GLRenderManager.h"
#include "thread/threadutil.h"
#include "math.h"

#if !defined(__APPLE__)
#include "SDL_syswm.h"
#endif

#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#endif

#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Common/GraphicsContext.h"
#include "SDLGLGraphicsContext.h"
#include "SDLVulkanGraphicsContext.h"

#include <go2/input.h>
#include <go2/display.h>
#include <drm/drm_fourcc.h>


GlobalUIState lastUIState = UISTATE_MENU;
GlobalUIState GetUIState();

static bool g_ToggleFullScreenNextFrame = false;
static int g_ToggleFullScreenType;
static int g_QuitRequested = 0;

static int g_DesktopWidth = 0;
static int g_DesktopHeight = 0;

int getDisplayNumber(void) {
	int displayNumber = 0;
	char * displayNumberStr;

	//get environment
	displayNumberStr=getenv("SDL_VIDEO_FULLSCREEN_HEAD");

	if (displayNumberStr) {
		displayNumber = atoi(displayNumberStr);
	}

	return displayNumber;
}

// Simple implementations of System functions


void SystemToast(const char *text) {
#ifdef _WIN32
	std::wstring str = ConvertUTF8ToWString(text);
	MessageBox(0, str.c_str(), L"Toast!", MB_ICONINFORMATION);
#else
	puts(text);
#endif
}

void ShowKeyboard() {
	// Irrelevant on PC
}

void Vibrate(int length_ms) {
	// Ignore on PC
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "toggle_fullscreen")) {
		g_ToggleFullScreenNextFrame = true;
		if (strcmp(parameter, "1") == 0) {
			g_ToggleFullScreenType = 1;
		} else if (strcmp(parameter, "0") == 0) {
			g_ToggleFullScreenType = 0;
		} else {
			// Just toggle.
			g_ToggleFullScreenType = -1;
		}
	} else if (!strcmp(command, "finish")) {
		// Do a clean exit
		g_QuitRequested = true;
	} else if (!strcmp(command, "graphics_restart")) {
		// Not sure how we best do this, but do a clean exit, better than being stuck in a bad state.
		g_QuitRequested = true;
	} else if (!strcmp(command, "setclipboardtext")) {
		SDL_SetClipboardText(parameter);
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_DENIED; }

void OpenDirectory(const char *path) {
#if defined(_WIN32)
	PIDLIST_ABSOLUTE pidl = ILCreateFromPath(ConvertUTF8ToWString(ReplaceAll(path, "/", "\\")).c_str());
	if (pidl) {
		SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
		ILFree(pidl);
	}
#endif
}

void LaunchBrowser(const char *url) {
#if defined(MOBILE_DEVICE)
	ILOG("Would have gone to %s but LaunchBrowser is not implemented on this platform", url);
#elif defined(_WIN32)
	std::wstring wurl = ConvertUTF8ToWString(url);
	ShellExecute(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", url)
	}
#endif
}

void LaunchMarket(const char *url) {
#if defined(MOBILE_DEVICE)
	ILOG("Would have gone to %s but LaunchMarket is not implemented on this platform", url);
#elif defined(_WIN32)
	std::wstring wurl = ConvertUTF8ToWString(url);
	ShellExecute(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", url)
	}
#endif
}

void LaunchEmail(const char *email_address) {
#if defined(MOBILE_DEVICE)
	ILOG("Would have opened your email client for %s but LaunchEmail is not implemented on this platform", email_address);
#elif defined(_WIN32)
	std::wstring mailto = std::wstring(L"mailto:") + ConvertUTF8ToWString(email_address);
	ShellExecute(NULL, L"open", mailto.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open mailto:") + email_address;
	system(command.c_str());
#else
	std::string command = std::string("xdg-email ") + email_address;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", email_address)
	}
#endif
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
#ifdef _WIN32
		return "SDL:Windows";
#elif __linux__
		return "SDL:Linux";
#elif __APPLE__
		return "SDL:OSX";
#else
		return "SDL:";
#endif
	case SYSPROP_LANGREGION: {
		// Get user-preferred locale from OS
		setlocale(LC_ALL, "");
		std::string locale(setlocale(LC_ALL, NULL));
		// Set c and c++ strings back to POSIX
		std::locale::global(std::locale("POSIX"));
		if (!locale.empty()) {
			if (locale.find("_", 0) != std::string::npos) {
				if (locale.find(".", 0) != std::string::npos) {
					return locale.substr(0, locale.find(".",0));
				} else {
					return locale;
				}
			}
		}
		return "en_US";
	}
	case SYSPROP_CLIPBOARD_TEXT:
		return SDL_HasClipboardText() ? SDL_GetClipboardText() : "";
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return 44100;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
	case SYSPROP_DEVICE_TYPE:
#if defined(MOBILE_DEVICE)
		return DEVICE_TYPE_MOBILE;
#else
		return DEVICE_TYPE_DESKTOP;
#endif
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	default:
		return false;
	}
}

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
}

// returns -1 on failure
static int parseInt(const char *str) {
	int val;
	int retval = sscanf(str, "%d", &val);
	printf("%i = scanf %s\n", retval, str);
	if (retval != 1) {
		return -1;
	} else {
		return val;
	}
}

static float parseFloat(const char *str) {
	float val;
	int retval = sscanf(str, "%f", &val);
	printf("%i = sscanf %s\n", retval, str);
	if (retval != 1) {
		return -1.0f;
	} else {
		return val;
	}
}

void ToggleFullScreenIfFlagSet(SDL_Window *window) {
	if (g_ToggleFullScreenNextFrame) {
		g_ToggleFullScreenNextFrame = false;

		Uint32 window_flags = SDL_GetWindowFlags(window);
		if (g_ToggleFullScreenType == -1) {
			window_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
		} else if (g_ToggleFullScreenType == 1) {
			window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		} else {
			window_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		SDL_SetWindowFullscreen(window, window_flags);
	}
}

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};

static std::thread emuThread;
static std::atomic<int> emuThreadState((int)EmuThreadState::DISABLED);

static void EmuThreadFunc(GraphicsContext *graphicsContext) {
	setCurrentThreadName("Emu");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;

	NativeInitGraphics(graphicsContext);

	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		UpdateRunLoop();
	}
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();
	graphicsContext->StopThread();
}

static void EmuThreadStart(GraphicsContext *context) {
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc, context);
}

static void EmuThreadStop() {
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

static void EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
}

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return 0;
		}
	}

	glslang::InitializeProcess();

// #if PPSSPP_PLATFORM(RPI)
// 	bcm_host_init();
// #endif
// 	putenv((char*)"SDL_VIDEO_CENTERED=1");
// 	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

// 	if (VulkanMayBeAvailable()) {
// 		printf("DEBUG: Vulkan might be available.\n");
// 	} else {
// 		printf("DEBUG: Vulkan is not available, not using Vulkan.\n");
// 	}

// 	int set_xres = -1;
// 	int set_yres = -1;
// 	int w = 0, h = 0;
// 	bool portrait = false;
// 	bool set_ipad = false;
// 	float set_dpi = 1.0f;
// 	float set_scale = 1.0f;

// 	// Produce a new set of arguments with the ones we skip.
 	int remain_argc = 1;
 	const char *remain_argv[256] = { argv[0] };

	Uint32 mode = 0;
	for (int i = 1; i < argc; i++) {
		// if (!strcmp(argv[i],"--fullscreen"))
		// 	//mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		// else if (set_xres == -2)
		// 	//set_xres = parseInt(argv[i]);
		// else if (set_yres == -2)
		// 	//set_yres = parseInt(argv[i]);
		// else if (set_dpi == -2)
		// 	//set_dpi = parseFloat(argv[i]);
		// else if (set_scale == -2)
		// 	//set_scale = parseFloat(argv[i]);
		// else if (!strcmp(argv[i],"--xres"))
		// 	//set_xres = -2;
		// else if (!strcmp(argv[i],"--yres"))
		// 	//set_yres = -2;
		// else if (!strcmp(argv[i],"--dpi"))
		// 	//set_dpi = -2;
		// else if (!strcmp(argv[i],"--scale"))
		// 	//set_scale = -2;
		// else if (!strcmp(argv[i],"--ipad"))
		// 	//set_ipad = true;
		// else if (!strcmp(argv[i],"--portrait"))
		// 	//portrait = true;
		// else {
 			remain_argv[remain_argc++] = argv[i];
		//}
	}

	//remain_argv[remain_argc++] = argv[1];


	std::string app_name;
	std::string app_name_nice;
	std::string version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape, &version);

	bool joystick_enabled = false;
	// if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
	// 	fprintf(stderr, "Failed to initialize SDL with joystick support. Retrying without.\n");
	// 	joystick_enabled = false;
	// 	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
	// 		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
	// 		return 1;
	// 	}
	// }

	if (SDL_Init(SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	go2_display_t* display = go2_display_create();
	
	go2_context_attributes_t attr;
	attr.major = 2;
	attr.minor = 0;
	attr.red_bits = 8;
	attr.green_bits = 8;
	attr.blue_bits = 8;
	attr.alpha_bits = 8;
	attr.depth_bits = 24;
	attr.stencil_bits = 0;
	
	go2_context_t* context = go2_context_create(display, 480, 320, &attr);
	go2_context_make_current(context);

	go2_presenter_t* presenter = go2_presenter_create(display, DRM_FORMAT_RGB565, 0xff080808);


	go2_input_t* input = go2_input_create();

	//joystick_enabled = false;

	// TODO: How do we get this into the GraphicsContext?
// #ifdef USING_EGL
// 	if (EGL_Open())
// 		return 1;
// #endif

// 	// Get the video info before doing anything else, so we don't get skewed resolution results.
// 	// TODO: support multiple displays correctly
// 	SDL_DisplayMode displayMode;
// 	int should_be_zero = SDL_GetCurrentDisplayMode(0, &displayMode);
// 	if (should_be_zero != 0) {
// 		fprintf(stderr, "Could not get display mode: %s\n", SDL_GetError());
// 		return 1;
// 	}
	g_DesktopWidth = 480; //displayMode.w;
	g_DesktopHeight = 320; //displayMode.h;

// 	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
// 	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
// 	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
// 	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
// 	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
// 	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
// 	SDL_GL_SetSwapInterval(1);

// 	// Force fullscreen if the resolution is too low to run windowed.
// 	if (g_DesktopWidth < 480 * 2 && g_DesktopHeight < 272 * 2) {
// 		mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
// 	}

// 	// If we're on mobile, don't try for windowed either.
// #if defined(MOBILE_DEVICE)
// 	mode |= SDL_WINDOW_FULLSCREEN;
// #elif defined(USING_FBDEV)
// 	mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
// #else
// 	mode |= SDL_WINDOW_RESIZABLE;
// #endif

// 	if (mode & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		pixel_xres = g_DesktopWidth;
		pixel_yres = g_DesktopHeight;
		g_Config.bFullScreen = true;
// 	} else {
// 		// set a sensible default resolution (2x)
// 		pixel_xres = 480 * 2 * set_scale;
// 		pixel_yres = 272 * 2 * set_scale;
// 		if (portrait) {
// 			std::swap(pixel_xres, pixel_yres);
// 		}
// 		g_Config.bFullScreen = false;
// 	}

// 	set_dpi = 1.0f / set_dpi;

// 	if (set_ipad) {
// 		pixel_xres = 1024;
// 		pixel_yres = 768;
// 	}
// 	if (!landscape) {
// 		std::swap(pixel_xres, pixel_yres);
// 	}

// 	if (set_xres > 0) {
// 		pixel_xres = set_xres;
// 	}
// 	if (set_yres > 0) {
// 		pixel_yres = set_yres;
// 	}
 	float dpi_scale = 2.0f;
// 	if (set_dpi > 0) {
// 		dpi_scale = set_dpi;
// 	}

 	dp_xres = (float)pixel_xres * dpi_scale;
 	dp_yres = (float)pixel_yres * dpi_scale;

	// Mac / Linux
	char path[2048];
	const char *the_path = getenv("HOME");
	if (!the_path) {
		struct passwd* pwd = getpwuid(getuid());
		if (pwd)
			the_path = pwd->pw_dir;
	}
	strcpy(path, the_path);
	if (path[strlen(path)-1] != '/')
		strcat(path, "/");

	NativeInit(remain_argc, (const char **)remain_argv, path, "/tmp", nullptr);

	// // Use the setting from the config when initing the window.
	// if (g_Config.bFullScreen)
	// 	mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	// int x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(getDisplayNumber());
	// int y = SDL_WINDOWPOS_UNDEFINED;

	pixel_in_dps_x = (float)pixel_xres / dp_xres;
	pixel_in_dps_y = (float)pixel_yres / dp_yres;
	g_dpi_scale_x = dp_xres / (float)pixel_xres;
	g_dpi_scale_y = dp_yres / (float)pixel_yres;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;

	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);

	GraphicsContext *graphicsContext = nullptr;
	SDL_Window *sdlwindow = nullptr;

	std::string error_message;
//	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		SDLGLGraphicsContext *ctx = new SDLGLGraphicsContext();
		if (ctx->Init(sdlwindow, 0, 0, 0, &error_message) != 0) {
			printf("GL init error '%s'\n", error_message.c_str());
		}
		graphicsContext = ctx;
	// } else if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
	// 	SDLVulkanGraphicsContext *ctx = new SDLVulkanGraphicsContext();
	// 	if (!ctx->Init(window, x, y, mode, &error_message)) {
	// 		printf("Vulkan init error '%s' - falling back to GL\n", error_message.c_str());
	// 		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	// 		SetGPUBackend((GPUBackend)g_Config.iGPUBackend);
	// 		delete ctx;
	// 		SDLGLGraphicsContext *glctx = new SDLGLGraphicsContext();
	// 		glctx->Init(window, x, y, mode, &error_message);
	// 		graphicsContext = glctx;
	// 	} else {
	// 		graphicsContext = ctx;
	// 	}
	// }

	bool useEmuThread = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	//SDL_SetWindowTitle(window, (app_name_nice + " " + PPSSPP_GIT_VERSION).c_str());

	// Since we render from the main thread, there's nothing done here, but we call it to avoid confusion.
	if (!graphicsContext->InitFromRenderThread(&error_message)) {
		printf("Init from thread error: '%s'\n", error_message.c_str());
	}

// #ifdef MOBILE_DEVICE
// 	SDL_ShowCursor(SDL_DISABLE);
// #endif

	if (!useEmuThread) {
		NativeInitGraphics(graphicsContext);
		NativeResized();
	}

	SDL_AudioSpec fmt, ret_fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 2048;
	fmt.callback = &mixaudio;
	fmt.userdata = (void *)0;

	if (SDL_OpenAudio(&fmt, &ret_fmt) < 0) {
		ELOG("Failed to open audio: %s", SDL_GetError());
	} else {
		if (ret_fmt.samples != fmt.samples) // Notify, but still use it
			ELOG("Output audio samples: %d (requested: %d)", ret_fmt.samples, fmt.samples);
		if (ret_fmt.freq != fmt.freq || ret_fmt.format != fmt.format || ret_fmt.channels != fmt.channels) {
			ELOG("Sound buffer format does not match requested format.");
			ELOG("Output audio freq: %d (requested: %d)", ret_fmt.freq, fmt.freq);
			ELOG("Output audio format: %d (requested: %d)", ret_fmt.format, fmt.format);
			ELOG("Output audio channels: %d (requested: %d)", ret_fmt.channels, fmt.channels);
			ELOG("Provided output format does not match requirement, turning audio off");
			SDL_CloseAudio();
		}
	}

	// Audio must be unpaused _after_ NativeInit()
	SDL_PauseAudio(0);
	if (joystick_enabled) {
		joystick = new SDLJoystick();
	} else {
		joystick = nullptr;
	}
	EnableFZ();

	int framecount = 0;
	bool mouseDown = false;

	if (useEmuThread) {
		EmuThreadStart(graphicsContext);
	}
	graphicsContext->ThreadStart();

	bool windowHidden = false;

	int frame = 0;
    double totalElapsed = 0.0;

    // Stopwatch_Reset();
    // Stopwatch_Start();


	go2_gamepad_state_t gamepad_previous = {0};
	go2_gamepad_state_t gamepad_curent = {0};

	bool isRunning = true;
	while (isRunning) 
	{
		double startTime = time_now_d();


		go2_input_gamepad_read(input, &gamepad_curent);

		if (gamepad_curent.buttons.f1)
			isRunning = false;

		if (gamepad_curent.dpad.left != gamepad_previous.dpad.left)
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_DPAD_LEFT, gamepad_curent.dpad.left ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.dpad.right != gamepad_previous.dpad.right)
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_DPAD_RIGHT, gamepad_curent.dpad.right ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.dpad.up != gamepad_previous.dpad.up)
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_DPAD_UP, gamepad_curent.dpad.up ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.dpad.down != gamepad_previous.dpad.down)
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_DPAD_DOWN, gamepad_curent.dpad.down ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}

#if 0
	case SDL_CONTROLLER_BUTTON_A:
		return NKCODE_BUTTON_2;
	case SDL_CONTROLLER_BUTTON_B:
		return NKCODE_BUTTON_3;
	case SDL_CONTROLLER_BUTTON_X:
		return NKCODE_BUTTON_4;
	case SDL_CONTROLLER_BUTTON_Y:
		return NKCODE_BUTTON_1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		return NKCODE_BUTTON_5;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		return NKCODE_BUTTON_6;
	case SDL_CONTROLLER_BUTTON_START:
		return NKCODE_BUTTON_10;
	case SDL_CONTROLLER_BUTTON_BACK:
		return NKCODE_BUTTON_9; // select button
	case SDL_CONTROLLER_BUTTON_GUIDE:
		return NKCODE_BACK; // pause menu
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:
		return NKCODE_BUTTON_THUMBL;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
		return NKCODE_BUTTON_THUMBR;
#endif

		if (gamepad_curent.buttons.a != gamepad_previous.buttons.a) // circle
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_3, gamepad_curent.buttons.a ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.b != gamepad_previous.buttons.b) // cross
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_2, gamepad_curent.buttons.b ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.x != gamepad_previous.buttons.x) // triangle
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_1, gamepad_curent.buttons.x ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.y != gamepad_previous.buttons.y) // square
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_4, gamepad_curent.buttons.y ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.top_left != gamepad_previous.buttons.top_left)
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_7, gamepad_curent.buttons.top_left ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.top_right != gamepad_previous.buttons.top_right)
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_8, gamepad_curent.buttons.top_right ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.f3 != gamepad_previous.buttons.f3) // SELECT
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_9, gamepad_curent.buttons.f3 ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.f4 != gamepad_previous.buttons.f4) // START
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BUTTON_10, gamepad_curent.buttons.f4 ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}
		if (gamepad_curent.buttons.f6 != gamepad_previous.buttons.f6) // MENU
		{
			KeyInput key(DEVICE_ID_PAD_0, NKCODE_BACK, gamepad_curent.buttons.f6 ? KEY_DOWN : KEY_UP);
			NativeKey(key);
		}

		{
			AxisInput axis = {0};
			axis.deviceId = DEVICE_ID_PAD_0;			
			axis.axisId = 0; //SDL_CONTROLLER_AXIS_LEFTX
			axis.value = gamepad_curent.thumb.x;		
			NativeAxis(axis);
		}

		{
			AxisInput axis = {0};
			axis.deviceId = DEVICE_ID_PAD_0;
			axis.axisId = 1; // SDL_CONTROLLER_AXIS_LEFTY
			axis.value = gamepad_curent.thumb.y;		
			NativeAxis(axis);
		}

		gamepad_previous = gamepad_curent;
		

		if (g_QuitRequested)
			break;
		//const uint8_t *keys = SDL_GetKeyboardState(NULL);
		if (emuThreadState == (int)EmuThreadState::DISABLED) {
			UpdateRunLoop();
		}
		if (g_QuitRequested)
			break;
// #if !defined(MOBILE_DEVICE)
// 		if (lastUIState != GetUIState()) {
// 			lastUIState = GetUIState();
// 			if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !g_Config.bShowTouchControls)
// 				SDL_ShowCursor(SDL_DISABLE);
// 			if (lastUIState != UISTATE_INGAME || !g_Config.bFullScreen)
// 				SDL_ShowCursor(SDL_ENABLE);
// 		}
// #endif

		if (framecount % 60 == 0) {
			// glsl_refresh(); // auto-reloads modified GLSL shaders once per second.
		}

		bool renderThreadPaused = windowHidden && g_Config.bPauseWhenMinimized && emuThreadState != (int)EmuThreadState::DISABLED;
		if (emuThreadState != (int)EmuThreadState::DISABLED && !renderThreadPaused) {
			if (!graphicsContext->ThreadFrame())
				break;
		}


		//graphicsContext->SwapBuffers();
		
		go2_context_swap_buffers(context);

		go2_surface_t* surface = go2_context_surface_lock(context);


		go2_presenter_post(presenter,
					surface,
					0, 0, 480, 320,
					0, 0, 320, 480,
					GO2_ROTATION_DEGREES_270);
		go2_context_surface_unlock(context, surface);

 
		//ToggleFullScreenIfFlagSet(window);

		// Simple throttling to not burn the GPU in the menu.
		time_update();
		if (GetUIState() != UISTATE_INGAME || !PSP_IsInited() || renderThreadPaused) {
			double diffTime = time_now_d() - startTime;
			int sleepTime = (int)(1000.0 / 60.0) - (int)(diffTime * 1000.0);
			if (sleepTime > 0)
				sleep_ms(sleepTime);
		}

		time_update();
		framecount++;
	}

	if (useEmuThread) {
		EmuThreadStop();
		while (graphicsContext->ThreadFrame()) {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			continue;
		}
		EmuThreadJoin();
	}

	delete joystick;

	if (!useEmuThread) {
		NativeShutdownGraphics();
	}
	graphicsContext->ThreadEnd();

	NativeShutdown();

	// Destroys Draw, which is used in NativeShutdown to shutdown.
	graphicsContext->ShutdownFromRenderThread();
	delete graphicsContext;

	SDL_PauseAudio(1);
	SDL_CloseAudio();
	SDL_Quit();
// #if PPSSPP_PLATFORM(RPI)
// 	bcm_host_deinit();
// #endif

	glslang::FinalizeProcess();
	ILOG("Leaving main");
	return 0;
}
