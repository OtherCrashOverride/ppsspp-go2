#include "go2input.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <pthread.h>

#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/limits.h>


static const char* INPUT_DIRECTORY = "/dev/input/";
static const char* EVDEV_NAME = "go2-gamepad";

static struct libevdev *dev = NULL;
static go2_gamepad_t current_state;
static go2_gamepad_t pending_state;
static pthread_mutex_t gamepadMutex;
static pthread_t thread_id;


static void* input_task(void* arg)
{
    int ret;

    if (!dev) return NULL;


	while (true)
	{
		/* EAGAIN is returned when the queue is empty */
		struct input_event ev;
		int rc = libevdev_next_event(dev, /*LIBEVDEV_READ_FLAG_NORMAL*/ LIBEVDEV_READ_FLAG_BLOCKING, &ev);
		if (rc == 0)
		{
#if 0
			printf("Gamepad Event: %s-%s(%d)=%d\n",
			       libevdev_event_type_get_name(ev.type),
			       libevdev_event_code_get_name(ev.type, ev.code), ev.code,
			       ev.value);
#endif

            if (ev.type == EV_KEY)
			{
                go2_button_state_t state = ev.value ? ButtonState_Pressed : ButtonState_Released;

                switch (ev.code)
                {
                    case BTN_A:
                        pending_state.buttons.a = state;
                        break;
                    case BTN_B:
                        pending_state.buttons.b = state;
                        break;
                    case BTN_X:
                        pending_state.buttons.x = state;
                        break;
                    case BTN_Y:
                        pending_state.buttons.y = state;
                        break;

                    case BTN_TL:
                        pending_state.buttons.top_left = state;
                        break;                    
                    case BTN_TR:          
                        pending_state.buttons.top_right = state;
                        break;

                    case BTN_TRIGGER_HAPPY1:
                        pending_state.buttons.f1 = state;
                        break;
                    case BTN_TRIGGER_HAPPY2:
                        pending_state.buttons.f2 = state;
                        break;
                    case BTN_TRIGGER_HAPPY3:
                        pending_state.buttons.f3 = state;
                        break;
                    case BTN_TRIGGER_HAPPY4:
                        pending_state.buttons.f4 = state;
                        break;
                    case BTN_TRIGGER_HAPPY5:
                        pending_state.buttons.f5 = state;
                        break;
                    case BTN_TRIGGER_HAPPY6:
                        pending_state.buttons.f6 = state;
                        break;
                }
            }
            else if (ev.type == EV_ABS)
            {
                switch (ev.code)
                {
                    case ABS_X:
                        pending_state.thumb.x = ev.value / 32767.0f;
                        break;
                    case ABS_Y:
                        pending_state.thumb.y = ev.value / 32767.0f;
                        break;

                    case ABS_HAT0X:
                        if (ev.value < 0)
                        {
                            pending_state.dpad.left = ButtonState_Pressed;
                            pending_state.dpad.right = ButtonState_Released;
                        }
                        else if (ev.value > 0)
                        {
                            pending_state.dpad.left = ButtonState_Released;
                            pending_state.dpad.right = ButtonState_Pressed;
                        }
                        else
                        {
                            pending_state.dpad.left = ButtonState_Released;
                            pending_state.dpad.right = ButtonState_Released;
                        }                        
                        break;
                    case ABS_HAT0Y:
                        if (ev.value < 0)
                        {
                            pending_state.dpad.up = ButtonState_Pressed;
                            pending_state.dpad.down = ButtonState_Released;
                        }
                        else if (ev.value > 0)
                        {
                            pending_state.dpad.up = ButtonState_Released;
                            pending_state.dpad.down = ButtonState_Pressed;
                        }
                        else
                        {
                            pending_state.dpad.up = ButtonState_Released;
                            pending_state.dpad.down = ButtonState_Released;
                        }                        
                        break;
                }
            }
            else if (ev.type == EV_SYN)
            {
                pthread_mutex_lock(&gamepadMutex);
    
                current_state = pending_state;

                pthread_mutex_unlock(&gamepadMutex); 
            }
        }
    }
}


void go2_gamepad_read(go2_gamepad_t* outGamepadState)
{
    pthread_mutex_lock(&gamepadMutex);
    
    *outGamepadState = current_state;        

    pthread_mutex_unlock(&gamepadMutex);  
}

void go2_gamepad_init()
{
	int fd;
	int rc = 1;

	// Detect the first joystick event file
	DIR* dir = opendir(INPUT_DIRECTORY);
	if (!dir)
	{
		printf("opendir failed.\n");
		exit(1);
	}

	struct dirent *dp;

	char buffer[PATH_MAX];

	while ((dp = readdir(dir)) != NULL)
	{
		strcpy(buffer, INPUT_DIRECTORY);
		strcat(buffer, dp->d_name);

        printf("Joystick: Found '%s'\n", buffer);

        fd = open(buffer, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
        {
            //fprintf(stderr, "Failed to open input device '%s' (%s)\n", buffer, strerror(-rc));
            continue; //exit(1);
        }

        rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            //fprintf(stderr, "Failed to init libevdev (%s)\n", strerror(-rc));
            continue; //exit(1);
        }

        const char* cmp = strstr(libevdev_get_name(dev), EVDEV_NAME);
        if (cmp)
        {
            break;
        }

        libevdev_free(dev);
        close(fd);
        fd = -1;
	}

	closedir(dir);


    if (fd < 0)
    {
        printf("Joystick: No gamepad found.\n");
    }
    else
    {
        memset(&current_state, 0, sizeof(current_state));
        memset(&pending_state, 0, sizeof(pending_state));
    
    
        printf("Input device name: \"%s\"\n", libevdev_get_name(dev));
        printf("Input device ID: bus %#x vendor %#x product %#x\n",
            libevdev_get_id_bustype(dev),
            libevdev_get_id_vendor(dev),
            libevdev_get_id_product(dev));

        if(pthread_create(&thread_id, NULL, input_task, (void*)NULL) < 0)
        {
            printf("could not create input_task thread\n");
            abort();
        }
    }
}

void go2_gamepad_close()
{
    // TODO: Kill thread and join

    libevdev_free(dev);
}
