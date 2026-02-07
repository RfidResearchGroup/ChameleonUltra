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
#include "commonutil.h"

uint64_t bytes_to_num(uint8_t *src, size_t len) {
    uint64_t num = 0;
    while (len--) {
        num = (num << 8) | (*src);
        src++;
    }
    return num;
}
