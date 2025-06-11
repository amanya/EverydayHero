#!/bin/sh

cc sdl_everyday.cpp -g $(pkg-config --libs --cflags sdl3) -o everyday
