//-----------------------------------------------------------------------------
// Copyright (C) Proxmark3 contributors. See AUTHORS.md for details.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See LICENSE.txt for the text of the license.
//-----------------------------------------------------------------------------
// UI utilities
//-----------------------------------------------------------------------------

#ifndef UI_H__
#define UI_H__

#include "common.h"
#include "../pm3/ansi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _USE_MATH_DEFINES

typedef enum {STYLE_BAR, STYLE_MIXED, STYLE_VALUE} barMode_t;
typedef enum logLevel {NORMAL, SUCCESS, INFO, FAILED, WARNING, ERR, DEBUG, INPLACE, HINT} logLevel_t;
typedef enum emojiMode {EMO_ALIAS, EMO_EMOJI, EMO_ALTTEXT, EMO_NONE} emojiMode_t;
typedef enum clientdebugLevel {cdbOFF, cdbSIMPLE, cdbFULL} clientdebugLevel_t;
// typedef enum devicedebugLevel {ddbOFF, ddbERROR, ddbINFO, ddbDEBUG, ddbEXTENDED} devicedebugLevel_t;

// last item spItemCount used to auto map to number of files
typedef enum savePaths {spDefault, spDump, spTrace, spItemCount} savePaths_t;
typedef struct {int x; int y; int h; int w;} qtWindow_t;

typedef struct {
    bool preferences_loaded;
    bool stdinOnTTY;
    bool stdoutOnTTY;
    bool supports_colors;
    emojiMode_t emoji_mode;
    bool pm3_present;
    bool help_dump_mode;
    bool show_hints;
    bool dense_output;
    bool window_changed; // track if plot/overlay pos/size changed to save on exit
    qtWindow_t plot;
    qtWindow_t overlay;
    bool overlay_sliders;
    bool incognito;
    char *defaultPaths[spItemCount]; // Array should allow loop searching for files
    clientdebugLevel_t client_debug_level;
    barMode_t bar_mode;
//    uint8_t device_debug_level;
    uint16_t client_exe_delay;
    char *history_path;
} session_arg_t;

extern session_arg_t g_session;
#ifndef M_PI
#define M_PI 3.14159265358979323846264338327
#endif
#define MAX_PRINT_BUFFER 2048

void PrintAndLogEx(logLevel_t level, const char *fmt, ...);
void memcpy_filter_ansi(void *dest, const void *src, size_t n, bool filter);
void memcpy_filter_emoji(void *dest, const void *src, size_t n, emojiMode_t mode);

#ifdef __cplusplus
}
#endif
#endif
