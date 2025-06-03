#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_oldnames.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdint>
#include <stdlib.h>
#include <sys/mman.h>

struct sdl_offscreen_buffer {
    // Pixels are always 32-bits wide, Memory order BB GG RR XX
    void *Memory;
    int Width;
    int Height;
    int Pitch;
};

struct sdl_window_dimension {
    int Width;
    int Height;
};

static bool GlobalRunning;
static sdl_offscreen_buffer GlobalBackBuffer;
static SDL_Joystick *GlobalJoystick;

sdl_window_dimension SDLGetWindowDimension(SDL_Window *Window) {
    sdl_window_dimension Result;

    SDL_GetWindowSize(Window, &Result.Width, &Result.Height);

    return Result;
}

static void RenderGradient(sdl_offscreen_buffer Buffer, int BlueOffset, int GreenOffset) {
    uint8_t *Row = (uint8_t *)Buffer.Memory;

    for (int Y = 0; Y < Buffer.Height; ++Y) {
        uint32_t *Pixel = (uint32_t *)Row;
        for(int X = 0; X < Buffer.Width; ++X) {
            uint8_t Blue = (X + BlueOffset);
            uint8_t Green = (Y + GreenOffset);

            *Pixel++ = ((Green << 8) | Blue);
        }
        Row += Buffer.Pitch;
    }
}

static void DisplayBufferInWindow(SDL_Renderer *Renderer) {
    SDL_RenderClear(Renderer);
    SDL_Texture *Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, GlobalBackBuffer.Width, GlobalBackBuffer.Height);
    SDL_UpdateTexture(Texture, NULL, GlobalBackBuffer.Memory, GlobalBackBuffer.Pitch);
    SDL_RenderTexture(Renderer, Texture, NULL, NULL);
    SDL_RenderPresent(Renderer);
}

static void ResizeTexture(SDL_Renderer *Renderer, int Width, int Height) {
    int BytesPerPixel = 4;

    GlobalBackBuffer.Pitch = Width * BytesPerPixel;

    if (GlobalBackBuffer.Memory) {
        munmap(GlobalBackBuffer.Memory, GlobalBackBuffer.Width * GlobalBackBuffer.Height * BytesPerPixel);
    }
    GlobalBackBuffer.Width = Width;
    GlobalBackBuffer.Height = Height;
    GlobalBackBuffer.Memory = mmap(0, Width * Height * BytesPerPixel, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

bool HandleEvent(SDL_Event *event) {
    bool should_quit = false;

    switch (event->type) {
        case SDL_EVENT_QUIT:
            {
                SDL_Log("SDL_QUIT");
                should_quit = true;
            } break;
        case SDL_EVENT_WINDOW_RESIZED:
            {
                SDL_Log("SDL_EVENT_WINDOW_RESIZED");
                SDL_Window *Window = SDL_GetWindowFromID(event->window.windowID);
                SDL_Renderer *Renderer = SDL_GetRenderer(Window);
                ResizeTexture(Renderer, event->window.data1, event->window.data2);
            } break;
        case SDL_EVENT_WINDOW_EXPOSED:
            {
                SDL_Log("SDL_EVENT_WINDOW_EXPOSED");
                SDL_Window *Window = SDL_GetWindowFromID(event->window.windowID);
                SDL_Renderer *Renderer = SDL_GetRenderer(Window);
                DisplayBufferInWindow(Renderer);
            } break;
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_KEY_DOWN:
            {
                SDL_Log("SDL_EVENT_KEY_DOWN");
                if (event->key.key == SDLK_ESCAPE) {
                    should_quit = true;
                }
            } break;
        case SDL_EVENT_JOYSTICK_ADDED:
            {
                SDL_Log("SDL_EVENT_JOYSTICK_ADDED");
                if (GlobalJoystick == NULL) {
                    GlobalJoystick = SDL_OpenJoystick(event->jdevice.which);
                    if (!GlobalJoystick) {
                        SDL_Log("Failed to open joystick ID %u: %s", (unsigned int) event->jdevice.which, SDL_GetError());
                    }
                }
            } break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            {
                SDL_Log("SDL_EVENT_JOYSTICK_REMOVED");
                if (GlobalJoystick && (SDL_GetJoystickID(GlobalJoystick) == event->jdevice.which)) {
                    SDL_CloseJoystick(GlobalJoystick);
                    GlobalJoystick = NULL;
                }
            } break;
    }

    return should_quit;
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* Window = SDL_CreateWindow("Everyday Hero", 640, 480, SDL_WINDOW_RESIZABLE);
    if (!Window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Renderer* Renderer = SDL_CreateRenderer(Window, NULL);
    if (!Renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }

    sdl_window_dimension WindowDimension = SDLGetWindowDimension(Window);

    ResizeTexture(Renderer, WindowDimension.Width, WindowDimension.Height);
    int XOffset = 0;
    int YOffset = 0;

    bool Running = true;

    while (Running) {
        SDL_Event event;

        while(SDL_PollEvent(&event)) {
            if (HandleEvent(&event)) {
                Running = false;
                break;
            }
        }

        if (GlobalJoystick) {
            const float StickX = (((float) SDL_GetJoystickAxis(GlobalJoystick, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f);
            const float StickY = (((float) SDL_GetJoystickAxis(GlobalJoystick, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f);
            XOffset += StickX;
            YOffset += StickY;
        }

        RenderGradient(GlobalBackBuffer, XOffset, YOffset);
        DisplayBufferInWindow(Renderer);
    }

    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);

    SDL_Quit();
}
