/*
 * Single translation unit that compiles the miniaudio implementation for the
 * test binary. The app gets this from main.cpp; the headless tests need their
 * own copy since they do not link main.cpp.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define MA_ENCODING_ENABLED
#define MINIAUDIO_IMPLEMENTATION
#include "../vendor/miniaudio/miniaudio.h"
