#include <cstdint>
#include <stdlib.h>
#include <math.h>

#include "everyday.h"

static void RenderGradient(game_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset) {
    uint8_t *Row = (uint8_t *)Buffer->Memory;

    for (int Y = 0; Y < Buffer->Height; ++Y) {
        uint32_t *Pixel = (uint32_t *)Row;
        for(int X = 0; X < Buffer->Width; ++X) {
            uint8_t Blue = (X + BlueOffset);
            uint8_t Green = (Y + GreenOffset);

            *Pixel++ = ((Green << 8) | Blue);
        }
        Row += Buffer->Pitch;
    }
}

static void GameOutputSound(game_sound_output_buffer *SoundBuffer, int ToneHz)
{
    static float tSine;
    int16_t ToneVolume = 3000;
    int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;

    int16_t *SampleOut = SoundBuffer->Samples;
    for(int SampleIndex = 0;
        SampleIndex < SoundBuffer->SampleCount;
        ++SampleIndex)
    {
        // TODO(casey): Draw this out for people
        float SineValue = sinf(tSine);
        int16_t SampleValue = (int16_t)(SineValue * ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;

        tSine += 2.0f*Pi32*1.0f/(float)WavePeriod;
    }
}

static void GameUpdateAndRender(game_memory *Memory, game_input *Input, game_offscreen_buffer *Buffer,
                                game_sound_output_buffer *SoundBuffer) {

    Assert(sizeof(game_state) <= Memory->PersistentStorageSize);
    
    game_state *GameState = (game_state *)Memory->PersistentStorage;

    game_controller_input *Input0 = &Input->Controllers[0];    
    if(Input0->IsAnalog)
    {
        // NOTE(casey): Use analog movement tuning
        GameState->BlueOffset += (int)4.0f*(Input0->EndX);
        GameState->ToneHz = 256 + (int)(128.0f*(Input0->EndY));
    }
    else
    {
        // NOTE(casey): Use digital movement tuning
    }

    // Input.AButtonEndedDown;
    // Input.AButtonHalfTransitionCount;
    if(Input0->Down.EndedDown)
    {
        GameState->GreenOffset += 1;
    }
    RenderGradient(Buffer, GameState->BlueOffset, GameState->GreenOffset);
    GameOutputSound(SoundBuffer, GameState->ToneHz);
}
