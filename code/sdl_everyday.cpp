#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_render.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdint>
#include <stdlib.h>
#include <sys/mman.h>

static bool Running;

static SDL_Texture *Texture;
static void *Buffer;
static int BufferWidth;
static int BufferHeight;
static int BytesPerPixel = 4;

static void RenderGradient(int BlueOffset, int GreenOffset) {
    int Width = BufferWidth;
    int Height = BufferHeight;

    int Pitch = Width * BytesPerPixel;
    uint8_t *Row = (uint8_t *)Buffer;
    for (int Y = 0; Y < BufferHeight; ++Y) {
        uint32_t *Pixel = (uint32_t *)Row;
        for(int X = 0; X < BufferWidth; ++X) {
            uint8_t Blue = (X + BlueOffset);
            uint8_t Green = (Y + GreenOffset);

            *Pixel++ = ((Green << 8) | Blue);
        }
        Row += Pitch;
    }
}

static void UpdateWindow(SDL_Renderer *Renderer) {
    SDL_RenderClear(Renderer);
    SDL_UpdateTexture(Texture, NULL, Buffer, BufferWidth * BytesPerPixel);
    SDL_RenderTexture(Renderer, Texture, NULL, NULL);
    SDL_RenderPresent(Renderer);
}

static void ResizeTexture(SDL_Renderer *Renderer, int Width, int Height) {
    if (Buffer) {
        munmap(Buffer, BufferWidth * BufferHeight * BytesPerPixel);
    }
    if (Texture) {
        SDL_DestroyTexture(Texture);
    }
    Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, Width, Height);
    BufferWidth = Width;
    BufferHeight = Height;
    Buffer = mmap(0, Width * Height * BytesPerPixel, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
                UpdateWindow(Renderer);
            } break;
    }

    return should_quit;
}

int main() {
    BufferWidth = 640;
    BufferHeight = 480;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* Window = SDL_CreateWindow("Everyday Hero", 
                                          BufferWidth, BufferHeight, SDL_WINDOW_RESIZABLE);
    if (!Window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Renderer* Renderer = SDL_CreateRenderer(Window, NULL);
    if (!Renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }

    Running = true;
    int Width, Height;
    SDL_GetWindowSize(Window, &Width, &Height);
    ResizeTexture(Renderer, Width, Height);
    int XOffset = 0;
    int YOffset = 0;

    while (Running) {
        SDL_Event event;

        while(SDL_PollEvent(&event)) {
            if (HandleEvent(&event)) {
                Running = false;
                break;
            }
        }
        RenderGradient(XOffset, YOffset);
        UpdateWindow(Renderer);

        ++XOffset;
        YOffset += 2;
    }

    SDL_DestroyRenderer(Renderer);
    SDL_DestroyWindow(Window);

    SDL_Quit();
}
