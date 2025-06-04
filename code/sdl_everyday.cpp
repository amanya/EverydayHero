#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdint>
#include <stdlib.h>
#include <sys/mman.h>
#include <math.h>

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

struct sdl_audio_ring_buffer
{
    int Size;
    int WriteCursor;
    int PlayCursor;
    void *Data;
};

sdl_audio_ring_buffer AudioRingBuffer;

struct sdl_sound_output
{
    int SamplesPerSecond;
    int ToneHz;
    int16_t ToneVolume;
    uint32_t RunningSampleIndex;
    int WavePeriod;
    int BytesPerSample;
    int SecondaryBufferSize;
    float tSine;
    int LatencySampleCount;
};

static bool GlobalRunning;
static sdl_offscreen_buffer GlobalBackBuffer;
static SDL_Joystick *GlobalJoystick;
static SDL_AudioStream *GlobalStream;

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

static void SDLFillSoundBuffer(sdl_sound_output *SoundOutput, int ByteToLock, int BytesToWrite)
{
    void *Region1 = (uint8_t*)AudioRingBuffer.Data + ByteToLock;
    int Region1Size = BytesToWrite;
    if (Region1Size + ByteToLock > SoundOutput->SecondaryBufferSize)
    {
        Region1Size = SoundOutput->SecondaryBufferSize - ByteToLock;
    }
    void *Region2 = AudioRingBuffer.Data;
    int Region2Size = BytesToWrite - Region1Size;
    int Region1SampleCount = Region1Size/SoundOutput->BytesPerSample;
    int16_t *SampleOut = (int16_t *)Region1;
    for(int SampleIndex = 0;
        SampleIndex < Region1SampleCount;
        ++SampleIndex)
    {
        // TODO(casey): Draw this out for people
        float SineValue = sinf(SoundOutput->tSine);
        int16_t SampleValue = (int16_t)(SineValue * SoundOutput->ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;

        SoundOutput->tSine += 2.0f*SDL_PI_F*1.0f/(float)SoundOutput->WavePeriod;
        ++SoundOutput->RunningSampleIndex;
    }

    int Region2SampleCount = Region2Size/SoundOutput->BytesPerSample;
    SampleOut = (int16_t *)Region2;
    for(int SampleIndex = 0;
        SampleIndex < Region2SampleCount;
        ++SampleIndex)
    {
        // TODO(casey): Draw this out for people
        float SineValue = sinf(SoundOutput->tSine);
        int16_t SampleValue = (int16_t)(SineValue * SoundOutput->ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;

        SoundOutput->tSine += 2.0f*SDL_PI_F*1.0f/(float)SoundOutput->WavePeriod;
        ++SoundOutput->RunningSampleIndex;
    }
}

static void SDLAudioCallback(void *UserData, SDL_AudioStream *Stream, int AdditionalAmount, int TotalAmount)
{
    if (AdditionalAmount > 0) {
        uint8_t *Data = SDL_stack_alloc(uint8_t, AdditionalAmount);
        if (Data) {

            sdl_audio_ring_buffer *RingBuffer = (sdl_audio_ring_buffer *)UserData;

            int Region1Size = AdditionalAmount;
            int Region2Size = 0;
            if (RingBuffer->PlayCursor + AdditionalAmount > RingBuffer->Size)
            {
                Region1Size = RingBuffer->Size - RingBuffer->PlayCursor;
                Region2Size = AdditionalAmount - Region1Size;
            }
            memcpy(Data, (uint8_t*)(RingBuffer->Data) + RingBuffer->PlayCursor, Region1Size);
            memcpy(&Data[Region1Size], RingBuffer->Data, Region2Size);
            RingBuffer->PlayCursor = (RingBuffer->PlayCursor + AdditionalAmount) % RingBuffer->Size;
            RingBuffer->WriteCursor = (RingBuffer->PlayCursor + AdditionalAmount) % RingBuffer->Size;

            SDL_PutAudioStreamData(Stream, Data, AdditionalAmount);
            SDL_stack_free(Data);
        }
    }
}

static bool SDLInitAudio(int32_t SamplesPerSecond, int32_t BufferSize) {
    SDL_AudioSpec AudioSettings;

    AudioSettings.freq = SamplesPerSecond;
    AudioSettings.format = SDL_AUDIO_S16LE;
    AudioSettings.channels = 2;

    AudioRingBuffer.Size = BufferSize;
    AudioRingBuffer.Data = malloc(BufferSize);
    AudioRingBuffer.PlayCursor = AudioRingBuffer.WriteCursor = 0;

    GlobalStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &AudioSettings, &SDLAudioCallback, &AudioRingBuffer);
    if (!GlobalStream) {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
        return false;
    }

    return true;
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *Window;
    SDL_Renderer *Renderer;

    if(SDL_CreateWindowAndRenderer("Everyday Hero", 640, 480, SDL_WINDOW_RESIZABLE, &Window, &Renderer)) {
        sdl_window_dimension WindowDimension = SDLGetWindowDimension(Window);

        ResizeTexture(Renderer, WindowDimension.Width, WindowDimension.Height);
        int XOffset = 0;
        int YOffset = 0;

        // NOTE: Sound test
        sdl_sound_output SoundOutput = {};
        SoundOutput.SamplesPerSecond = 48000;
        SoundOutput.ToneHz = 256;
        SoundOutput.ToneVolume = 3000;
        SoundOutput.RunningSampleIndex = 0;
        SoundOutput.WavePeriod = SoundOutput.SamplesPerSecond / SoundOutput.ToneHz;
        SoundOutput.BytesPerSample = sizeof(int16_t) * 2;
        SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
        SoundOutput.tSine = 0.0f;
        SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;
        // Open our audio device:
        if(!SDLInitAudio(SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize)) {
            SDL_Log("Audio initialization failed");
            return 1;
        }
        SDLFillSoundBuffer(&SoundOutput, 0, SoundOutput.LatencySampleCount * SoundOutput.BytesPerSample);

        bool SoundEnabled = false;

        bool Running = true;

        while (Running) {
            uint64_t LastCounter = SDL_GetPerformanceCounter();

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

                SoundOutput.ToneHz = 512 + (int)(256.0f*(float)StickY);
                SDL_Log("ToneHz: %d", SoundOutput.ToneHz);
                SDL_Log("StickY: %f", StickY);
                SoundOutput.WavePeriod = SoundOutput.SamplesPerSecond / SoundOutput.ToneHz;
            }

            RenderGradient(GlobalBackBuffer, XOffset, YOffset);

            // Sound output test
            int ByteToLock = (SoundOutput.RunningSampleIndex*SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize;
            int TargetCursor = ((AudioRingBuffer.PlayCursor +
                                 (SoundOutput.LatencySampleCount*SoundOutput.BytesPerSample)) %
                                SoundOutput.SecondaryBufferSize);
            int BytesToWrite;
            if(ByteToLock > TargetCursor)
            {
                BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
                BytesToWrite += TargetCursor;
            }
            else
            {
                BytesToWrite = TargetCursor - ByteToLock;
            }

            SDLFillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite); 

            if (!SoundEnabled) {
                SDL_ResumeAudioStreamDevice(GlobalStream);
                SoundEnabled = true;
            }

            DisplayBufferInWindow(Renderer);

            uint64_t PerfCountFrequency = SDL_GetPerformanceFrequency();
            uint64_t EndCounter = SDL_GetPerformanceCounter();
            uint64_t CounterElapsed = EndCounter - LastCounter;

            float MSPerFrame = (((1000.0f * (float)CounterElapsed) / (float)PerfCountFrequency));
            float FPS = (float)PerfCountFrequency / (float)CounterElapsed;

            SDL_Log("%.02f ms/f, %.02ff/s\n", MSPerFrame, FPS);
            LastCounter = EndCounter;
        }

        SDL_DestroyRenderer(Renderer);
        SDL_DestroyWindow(Window);

        SDL_Quit();
    } else {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
    }

}
