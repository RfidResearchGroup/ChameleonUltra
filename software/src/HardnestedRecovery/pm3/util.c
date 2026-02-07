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

#include "util.h"

#ifdef _WIN32 // Only compile this block on Windows

#include <windows.h> // Include the necessary Windows header
#include <stdint.h>  // <-- Add this line to include standard integer types


// Function to return milliseconds since some arbitrary point in time
uint64_t msclock(void) {
    // Static variable to store the frequency; initialized only once.
    static LARGE_INTEGER frequency = {0};
    LARGE_INTEGER counter;

    // Get the high-resolution timer frequency (ticks per second)
    // This only needs to be done once.
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }

    // Get the current high-resolution counter value (ticks)
    QueryPerformanceCounter(&counter);

    // Calculate milliseconds: (ticks * 1000) / ticks_per_second
    // We multiply by 1000 first to maintain precision before dividing.
    // Ensure 64-bit arithmetic is used via QuadPart.
    return (uint64_t)((counter.QuadPart * 1000) / frequency.QuadPart);
}

#endif // _WIN32

// global client debug variable
uint8_t g_debugMode = 0;
// global client disable logging variable
uint8_t g_printAndLog = PRINTANDLOG_PRINT | PRINTANDLOG_LOG;
// global client tell if a pending prompt is present

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// determine number of logical CPU cores (use for multithreaded functions)
int num_CPUs(void) {
#if defined(_WIN32)
#include <sysinfoapi.h>
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    int count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count <= 0)
        count = 1;
    return count;
#endif
}
