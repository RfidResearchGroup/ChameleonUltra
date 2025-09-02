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
// utilities
//-----------------------------------------------------------------------------
#ifndef __UTIL_H_
#define __UTIL_H_

#include "common.h"

#ifdef ANDROID
#include <endian.h>
#endif

// used for save/load files
#ifndef FILE_PATH_SIZE
#define FILE_PATH_SIZE 1000
#endif

extern uint8_t g_debugMode;
extern uint8_t g_printAndLog;

#define PRINTANDLOG_PRINT 1
#define PRINTANDLOG_LOG 2

int num_CPUs(void);  // number of logical CPUs

#endif
