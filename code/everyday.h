#ifndef EVERYDAY_H

struct game_offscreen_buffer {
    void *Memory;
    int Width;
    int Height;
    int Pitch;
};

static void GameUpdateAndRender(game_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset);

#define EVERYDAY_H
#endif // !EVERYDAY_H
