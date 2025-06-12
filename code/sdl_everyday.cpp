#include "SDL3/SDL_audio.h"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_haptic.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_joystick.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "everyday.h"
#include "everyday.cpp"
#include "sdl_everyday.h"

#include <cstring>

#define MAX_CONTROLLERS 4
SDL_Gamepad *ControllerHandles[MAX_CONTROLLERS];
SDL_Haptic *RumbleHandles[MAX_CONTROLLERS];

sdl_audio_ring_buffer AudioRingBuffer;

static bool GlobalRunning;
static game_offscreen_buffer GlobalBackBuffer;
static SDL_Joystick *GlobalJoystick;
static SDL_AudioStream *GlobalStream;

static debug_read_file_result DEBUGPlatformReadEntireFile(char *Filename)
{
    debug_read_file_result Result = {};
    
    int FileHandle = open(Filename, O_RDONLY);
    if(FileHandle == -1)
    {
        return Result;
    }

    struct stat FileStatus;
    if(fstat(FileHandle, &FileStatus) == -1)
    {
        close(FileHandle);
        return Result;
    }
    Result.ContentsSize = SafeTruncateUInt64(FileStatus.st_size);

    Result.Contents = malloc(Result.ContentsSize);
    if(!Result.Contents)
    {
        close(FileHandle);
        Result.ContentsSize = 0;
        return Result;
    }


    uint32_t BytesToRead = Result.ContentsSize;
    uint8_t *NextByteLocation = (uint8_t*)Result.Contents;
    while (BytesToRead)
    {
        uint32_t BytesRead = read(FileHandle, NextByteLocation, BytesToRead);
        if (BytesRead == -1)
        {
            free(Result.Contents);
            Result.Contents = 0;
            Result.ContentsSize = 0;
            close(FileHandle);
            return Result;
        }
        BytesToRead -= BytesRead;
        NextByteLocation += BytesRead;
    }

    close(FileHandle);
    return(Result);
}

static void
DEBUGPlatformFreeFileMemory(void *Memory)
{
    free(Memory);
}

static bool32
DEBUGPlatformWriteEntireFile(char *Filename, uint32_t MemorySize, void *Memory)
{
    int FileHandle = open(Filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (!FileHandle)
        return false;

    uint32_t BytesToWrite = MemorySize;
    uint8_t *NextByteLocation = (uint8_t*)Memory;
    while (BytesToWrite)
    {
        uint32_t BytesWritten = write(FileHandle, NextByteLocation, BytesToWrite);
        if (BytesWritten == -1)
        {
            close(FileHandle);
            return false;
        }
        BytesToWrite -= BytesWritten;
        NextByteLocation += BytesWritten;
    }

    close(FileHandle);

    return true;
}

sdl_window_dimension SDLGetWindowDimension(SDL_Window *Window) {
    sdl_window_dimension Result;

    SDL_GetWindowSize(Window, &Result.Width, &Result.Height);

    return Result;
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

static void SDLFillSoundBuffer(sdl_sound_output *SoundOutput, int ByteToLock, int BytesToWrite, game_sound_output_buffer *SoundBuffer)
{
    int16_t *Samples = SoundBuffer->Samples;
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
        *SampleOut++ = *Samples++;
        *SampleOut++ = *Samples++;
        ++SoundOutput->RunningSampleIndex;
    }

    int Region2SampleCount = Region2Size/SoundOutput->BytesPerSample;
    SampleOut = (int16_t *)Region2;
    for(int SampleIndex = 0;
        SampleIndex < Region2SampleCount;
        ++SampleIndex)
    {
        *SampleOut++ = *Samples++;
        *SampleOut++ = *Samples++;
        ++SoundOutput->RunningSampleIndex;
    }
}

static void SDLProcessGameControllerButton(game_button_state *OldState,
                                           game_button_state *NewState,
                                           SDL_Gamepad *ControllerHandle,
                                           SDL_GamepadButton Button)
{
    NewState->EndedDown = SDL_GetGamepadButton(ControllerHandle, Button);
    NewState->HalfTransitionCount += ((NewState->EndedDown == OldState->EndedDown)?0:1);
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
    AudioRingBuffer.Data = calloc(BufferSize, 1);
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
                // if (GlobalJoystick == NULL) {
                //     GlobalJoystick = SDL_OpenJoystick(event->jdevice.which);
                //     if (!GlobalJoystick) {
                //         SDL_Log("Failed to open joystick ID %u: %s", (unsigned int) event->jdevice.which, SDL_GetError());
                //     }
                // }
            } break;
        case SDL_EVENT_JOYSTICK_REMOVED:
            {
                SDL_Log("SDL_EVENT_JOYSTICK_REMOVED");
                // if (GlobalJoystick && (SDL_GetJoystickID(GlobalJoystick) == event->jdevice.which)) {
                //     SDL_CloseJoystick(GlobalJoystick);
                //     GlobalJoystick = NULL;
                // }
            } break;
    }

    return should_quit;
}

static void SDLOpenGameControllers()
{
    int MaxJoysticks;
    SDL_JoystickID *JoystickIDs = SDL_GetJoysticks(&MaxJoysticks);
    int ControllerIndex = 0;
    for(int JoystickIndex=0; JoystickIndex < MaxJoysticks; ++JoystickIndex)
    {
        SDL_JoystickID JoystickID = JoystickIDs[JoystickIndex];
        if (!SDL_IsGamepad(JoystickID))
        {
            continue;
        }
        if (ControllerIndex >= MAX_CONTROLLERS)
        {
            break;
        }
        ControllerHandles[ControllerIndex] = SDL_OpenGamepad(JoystickID);
        SDL_Joystick *JoystickHandle = SDL_GetGamepadJoystick(ControllerHandles[ControllerIndex]);
        RumbleHandles[ControllerIndex] = SDL_OpenHapticFromJoystick(JoystickHandle);
        if (SDL_InitHapticRumble(RumbleHandles[ControllerIndex]) != 0)
        {
            SDL_CloseHaptic(RumbleHandles[ControllerIndex]);
            RumbleHandles[ControllerIndex] = 0;
        }

        ControllerIndex++;
    }
}

static void
SDLCloseGameControllers()
{
    for(int ControllerIndex = 0; ControllerIndex < MAX_CONTROLLERS; ++ControllerIndex)
    {
        if (ControllerHandles[ControllerIndex])
        {
            if (RumbleHandles[ControllerIndex])
                SDL_CloseHaptic(RumbleHandles[ControllerIndex]);
            SDL_CloseGamepad(ControllerHandles[ControllerIndex]);
        }
    }
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDLOpenGameControllers();

    SDL_Window *Window;
    SDL_Renderer *Renderer;

    if(SDL_CreateWindowAndRenderer("Everyday Hero", 640, 480, SDL_WINDOW_RESIZABLE, &Window, &Renderer)) {
        sdl_window_dimension WindowDimension = SDLGetWindowDimension(Window);

        ResizeTexture(Renderer, WindowDimension.Width, WindowDimension.Height);

        game_input Input[2] = {};
        game_input *NewInput = &Input[0];
        game_input *OldInput = &Input[1];

        // NOTE: Sound test
        sdl_sound_output SoundOutput = {};
        SoundOutput.SamplesPerSecond = 48000;
        SoundOutput.RunningSampleIndex = 0;
        SoundOutput.BytesPerSample = sizeof(int16_t) * 2;
        SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
        SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;
        // Open our audio device:
        if(!SDLInitAudio(SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize)) {
            SDL_Log("Audio initialization failed");
            return 1;
        }
        int16_t *Samples = (int16_t *)calloc(SoundOutput.SamplesPerSecond, SoundOutput.BytesPerSample);


#if EVERYDAY_INTERNAL
        // TODO: This will fail gently on 32-bit at the moment, but we should probably fix it.
        void *BaseAddress = (void *)Terabytes(2);
#else
        void *BaseAddress = (void *)(0);
#endif

        game_memory GameMemory = {};
        GameMemory.PersistentStorageSize = Megabytes(64);
        GameMemory.TransientStorageSize = Gigabytes(4);

        uint64_t TotalStorageSize = GameMemory.PersistentStorageSize + GameMemory.TransientStorageSize;

        GameMemory.PersistentStorage = mmap(BaseAddress, TotalStorageSize,
                                           PROT_READ | PROT_WRITE,
                                           MAP_ANON | MAP_PRIVATE,
                                           -1, 0);

        Assert(GameMemory.PersistentStorage);

        GameMemory.TransientStorage = (uint8_t*)(GameMemory.PersistentStorage) + GameMemory.PersistentStorageSize;



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

            // Poll our controllers for input.
            for (int ControllerIndex = 0; ControllerIndex < MAX_CONTROLLERS; ++ControllerIndex)
            {
                if(ControllerHandles[ControllerIndex] != 0 && SDL_GamepadConnected(ControllerHandles[ControllerIndex]))
                {
                    game_controller_input *OldController = &OldInput->Controllers[ControllerIndex];
                    game_controller_input *NewController = &NewInput->Controllers[ControllerIndex];

                    NewController->IsAnalog = true;
                
                    //TODO: Do something with the DPad, Start and Selected?
                    bool Up = SDL_GetGamepadButton(ControllerHandles[ControllerIndex], SDL_GAMEPAD_BUTTON_DPAD_UP);
                    bool Down = SDL_GetGamepadButton(ControllerHandles[ControllerIndex], SDL_GAMEPAD_BUTTON_DPAD_DOWN);
                    bool Left = SDL_GetGamepadButton(ControllerHandles[ControllerIndex], SDL_GAMEPAD_BUTTON_DPAD_LEFT);
                    bool Right = SDL_GetGamepadButton(ControllerHandles[ControllerIndex], SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
                    bool Start = SDL_GetGamepadButton(ControllerHandles[ControllerIndex], SDL_GAMEPAD_BUTTON_START);
                    bool Back = SDL_GetGamepadButton(ControllerHandles[ControllerIndex], SDL_GAMEPAD_BUTTON_BACK);

                    SDLProcessGameControllerButton(&(OldController->LeftShoulder),
                           &(NewController->LeftShoulder),
                           ControllerHandles[ControllerIndex],
                           SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);

                    SDLProcessGameControllerButton(&(OldController->RightShoulder),
                           &(NewController->RightShoulder),
                           ControllerHandles[ControllerIndex],
                           SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

                    SDLProcessGameControllerButton(&(OldController->Down),
                           &(NewController->Down),
                           ControllerHandles[ControllerIndex],
                           SDL_GAMEPAD_BUTTON_DPAD_DOWN);

                    SDLProcessGameControllerButton(&(OldController->Right),
                           &(NewController->Right),
                           ControllerHandles[ControllerIndex],
                           SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

                    SDLProcessGameControllerButton(&(OldController->Left),
                           &(NewController->Left),
                           ControllerHandles[ControllerIndex],
                           SDL_GAMEPAD_BUTTON_DPAD_LEFT);

                    SDLProcessGameControllerButton(&(OldController->Up),
                           &(NewController->Up),
                           ControllerHandles[ControllerIndex],
                           SDL_GAMEPAD_BUTTON_DPAD_UP);

                    int16_t StickX = SDL_GetGamepadAxis(ControllerHandles[ControllerIndex], SDL_GAMEPAD_AXIS_LEFTX);
                    int16_t StickY = SDL_GetGamepadAxis(ControllerHandles[ControllerIndex], SDL_GAMEPAD_AXIS_LEFTY);

                    if (StickX < 0)
                    {
                        NewController->EndX = StickX / -32768.0f;
                    }
                    else
                    {
                        NewController->EndX = StickX / -32767.0f;
                    }

                    NewController->MinX = NewController->MaxX = NewController->EndX;

                    if (StickY < 0)
                    {
                        NewController->EndY = StickY / -32768.0f;
                    }
                    else
                    {
                        NewController->EndY = StickY / -32767.0f;
                    }

                    NewController->MinY = NewController->MaxY = NewController->EndY;
                    }
                else
                {
                    // TODO: This controller is not plugged in.
                }
            }

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

            game_sound_output_buffer SoundBuffer = {};
            SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
            SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
            SoundBuffer.Samples = Samples;

            game_offscreen_buffer Buffer = {};
            Buffer.Memory = GlobalBackBuffer.Memory;
            Buffer.Width = GlobalBackBuffer.Width; 
            Buffer.Height = GlobalBackBuffer.Height;
            Buffer.Pitch = GlobalBackBuffer.Pitch; 

            GameUpdateAndRender(&GameMemory, NewInput, &Buffer, &SoundBuffer);

            game_input *Temp = NewInput;
            NewInput = OldInput;
            OldInput = Temp;

            SDLFillSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer); 

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

            //SDL_Log("%.02f ms/f, %.02ff/s\n", MSPerFrame, FPS);
            LastCounter = EndCounter;
        }

        SDL_DestroyRenderer(Renderer);
        SDL_DestroyWindow(Window);

        SDLCloseGameControllers();
        SDL_Quit();
    } else {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
    }

}
