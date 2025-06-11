#!/bin/sh

mkdir -p ../build
pushd ../build
cc -DEVERYDAY_INTERNAL=0 -DEVERYDAY_SLOW=0 ../code/sdl_everyday.cpp -g $(pkg-config --libs --cflags sdl3) -o everyday
popd
