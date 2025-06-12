#ifndef EVERYDAY_H

#define Pi32 3.14159265359f

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))

#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)
#define Terabytes(Value) (Gigabytes(Value)*1024LL)

#if EVERYDAY_SLOW
#define Assert(Expression) if(!(Expression)) {*(int *)0 = 0;}
#else
#define Assert(Expression)
#endif

typedef int32_t bool32;

inline uint32_t SafeTruncateUInt64(uint64_t Value)
{
    Assert(Value <= 0xFFFFFFFF);
    uint32_t Result = (uint32_t)Value;
    return(Result);
}

#if EVERYDAY_INTERNAL
struct debug_read_file_result
{
    uint32_t ContentsSize;
    void *Contents;
};
static debug_read_file_result DEBUGPlatformReadEntireFile(char *Filename);
static void DEBUGPlatformFreeFileMemory(void *Memory);
static bool32 DEBUGPlatformWriteEntireFile(char *Filename, uint32_t MemorySize, void *Memory);
#endif

struct game_state {
    int BlueOffset;
    int GreenOffset;
    int ToneHz;
};

struct game_memory {
    bool32 IsInitialized;
    uint64_t PersistentStorageSize;
    void *PersistentStorage;
    uint64_t TransientStorageSize;
    void *TransientStorage;
};

struct game_offscreen_buffer {
    void *Memory;
    int Width;
    int Height;
    int Pitch;
};

struct game_sound_output_buffer
{
    int SamplesPerSecond;
    int SampleCount;
    int16_t *Samples;
};

struct game_button_state
{
    int HalfTransitionCount;
    bool32 EndedDown;
};

struct game_controller_input
{
    bool32 IsAnalog;

    float StartX;
    float StartY;

    float MinX;
    float MinY;

    float MaxX;
    float MaxY;

    float EndX;
    float EndY;

    union
    {
        game_button_state Buttons[6];
        struct
        {
            game_button_state Up;
            game_button_state Down;
            game_button_state Left;
            game_button_state Right;
            game_button_state LeftShoulder;
            game_button_state RightShoulder;
        };
    };
};

struct game_input
{
    game_controller_input Controllers[4];
};


static void GameUpdateAndRender(game_memory *GameMemory, game_input *Input, game_offscreen_buffer *Buffer,
                                game_sound_output_buffer *SoundBuffer);

#define EVERYDAY_H
#endif // !EVERYDAY_H
