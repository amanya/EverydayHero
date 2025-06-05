#ifndef EVERYDAY_H
#include <stdlib.h>

#define Pi32 3.14159265359f

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

static void GameUpdateAndRender(game_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset,
                                game_sound_output_buffer *SoundBuffer, int ToneHz);

#define EVERYDAY_H
#endif // !EVERYDAY_H
