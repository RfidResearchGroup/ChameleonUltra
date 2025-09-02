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
// Utility functions used in many places, not specific to any piece of code.
//-----------------------------------------------------------------------------

#ifndef __COMMONUTIL_H
#define __COMMONUTIL_H

#include "common.h"

// endian change for 16bit
#ifdef __GNUC__
#ifndef BSWAP_16
#define BSWAP_16(x) __builtin_bswap16(x)
#endif
#else
#ifdef _MSC_VER
#ifndef BSWAP_16
#define BSWAP_16(x) _byteswap_ushort(x)
#endif
#else
#ifndef BSWAP_16
#define BSWAP_16(x) (((((x)&0xFF00) >> 8)) | ((((x)&0x00FF) << 8)))
#endif
#endif
#endif

#ifndef BITMASK
#define BITMASK(X) (1 << (X))
#endif
#ifndef ARRAYLEN
#define ARRAYLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef NTIME
#define NTIME(n) for (int _index = 0; _index < n; _index++)
#endif

uint64_t bytes_to_num(uint8_t *src, size_t len);

#endif
