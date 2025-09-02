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

/* Ensure strtok_r is available even with -std=c99; must be included before
 */

#include "ui.h"

#include <stdarg.h>
#include <stdio.h>  // for Mingw readline
#include <stdlib.h>

#include "commonutil.h"  // ARRAYLEN

#if defined(HAVE_READLINE)
// Load readline after stdio.h
#include <readline/readline.h>
#endif

#include "util.h"

#ifdef _WIN32
#include <direct.h>  // _mkdir
#endif

#include <string.h>

#include "emojis.h"
#include "emojis_alt.h"

session_arg_t g_session;

static bool flushAfterWrite = false;
static void fPrintAndLog(FILE *stream, const char *fmt, ...);

#ifdef _WIN32
#define MKDIR_CHK _mkdir(path)
// STRTOK macro removed - replaced with conditional calls below
#else
#define MKDIR_CHK mkdir(path, 0700)
// STRTOK macro removed - replaced with conditional calls below
#endif

static uint8_t PrintAndLogEx_spinidx = 0;

void PrintAndLogEx(logLevel_t level, const char *fmt, ...)
{
    // skip debug messages if client debugging is turned off i.e. 'DATA SETDEBUG -0'
    if (g_debugMode == 0 && level == DEBUG) return;

    // skip HINT messages if client has hints turned off i.e. 'HINT 0'
    if (g_session.show_hints == false && level == HINT) return;

    char prefix[40] = {0};
    char buffer[MAX_PRINT_BUFFER] = {0};
    char buffer2[MAX_PRINT_BUFFER + sizeof(prefix)] = {0};
    char *token = NULL;
    char *tmp_ptr = NULL;  // Save pointer for strtok_r/strtok_s
    FILE *stream = stdout;
    const char *spinner[] = {_YELLOW_("[\\]"), _YELLOW_("[|]"), _YELLOW_("[/]"), _YELLOW_("[-]")};
    const char *spinner_emoji[]
        = {" :clock1: ", " :clock2: ", " :clock3: ", " :clock4: ",  " :clock5: ",  " :clock6: ",
           " :clock7: ", " :clock8: ", " :clock9: ", " :clock10: ", " :clock11: ", " :clock12: "};
    switch (level) {
        case ERR:
            if (g_session.emoji_mode == EMO_EMOJI)
                strncpy(prefix, "[" _RED_("!!") "] :rotating_light: ", sizeof(prefix) - 1);
            else
                strncpy(prefix, "[" _RED_("!!") "] ", sizeof(prefix) - 1);
            stream = stderr;
            break;
        case FAILED:
            if (g_session.emoji_mode == EMO_EMOJI)
                strncpy(prefix, "[" _RED_("-") "] :no_entry: ", sizeof(prefix) - 1);
            else
                strncpy(prefix, "[" _RED_("-") "] ", sizeof(prefix) - 1);
            break;
        case DEBUG:
            strncpy(prefix, "[" _BLUE_("#") "] ", sizeof(prefix) - 1);
            break;
        case HINT:
            strncpy(prefix, "[" _YELLOW_("?") "] ", sizeof(prefix) - 1);
            break;
        case SUCCESS:
            strncpy(prefix, "[" _GREEN_("+") "] ", sizeof(prefix) - 1);
            break;
        case WARNING:
            if (g_session.emoji_mode == EMO_EMOJI)
                strncpy(prefix, "[" _CYAN_("!") "] :warning:  ", sizeof(prefix) - 1);
            else
                strncpy(prefix, "[" _CYAN_("!") "] ", sizeof(prefix) - 1);
            break;
        case INFO:
            strncpy(prefix, "[" _YELLOW_("=") "] ", sizeof(prefix) - 1);
            break;
        case INPLACE:
            if (g_session.emoji_mode == EMO_EMOJI) {
                strncpy(prefix, spinner_emoji[PrintAndLogEx_spinidx], sizeof(prefix) - 1);
                PrintAndLogEx_spinidx++;
                if (PrintAndLogEx_spinidx >= ARRAYLEN(spinner_emoji)) PrintAndLogEx_spinidx = 0;
            }
            else {
                strncpy(prefix, spinner[PrintAndLogEx_spinidx], sizeof(prefix) - 1);
                PrintAndLogEx_spinidx++;
                if (PrintAndLogEx_spinidx >= ARRAYLEN(spinner)) PrintAndLogEx_spinidx = 0;
            }
            break;
        case NORMAL:
            // no prefixes for normal
            break;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    // no prefixes for normal & inplace
    if (level == NORMAL) {
        fPrintAndLog(stream, "%s", buffer);
        return;
    }

    if (strchr(buffer, '\n')) {
        const char delim[2] = "\n";

        // line starts with newline
        if (buffer[0] == '\n') fPrintAndLog(stream, "");

            // Use platform-specific strtok
#if defined(_WIN32) && defined(_MSC_VER)
        token = strtok_s(buffer, delim, &tmp_ptr);  // Use strtok_s for MSVC
#else
        token = strtok_r(buffer, delim, &tmp_ptr);    // Use strtok_r for POSIX
#endif

        while (token != NULL) {
            size_t size = strlen(buffer2);

            if (strlen(token))
                snprintf(buffer2 + size, sizeof(buffer2) - size, "%s%s\n", prefix, token);
            else
                snprintf(buffer2 + size, sizeof(buffer2) - size, "\n");

                // Use platform-specific strtok for subsequent calls
#if defined(_WIN32) && defined(_MSC_VER)
            token = strtok_s(NULL, delim, &tmp_ptr);  // Use strtok_s for MSVC
#else
            token = strtok_r(NULL, delim, &tmp_ptr);  // Use strtok_r for POSIX
#endif
        }
        fPrintAndLog(stream, "%s", buffer2);
    }
    else {
        snprintf(buffer2, sizeof(buffer2), "%s%s", prefix, buffer);
        if (level == INPLACE) {
            char buffer3[sizeof(buffer2)] = {0};
            char buffer4[sizeof(buffer2)] = {0};
            memcpy_filter_ansi(buffer3, buffer2, sizeof(buffer2), !g_session.supports_colors);
            memcpy_filter_emoji(buffer4, buffer3, sizeof(buffer3), g_session.emoji_mode);
            fprintf(stream, "\r%s", buffer4);
            fflush(stream);
        }
        else {
            fPrintAndLog(stream, "%s", buffer2);
        }
    }
}

static void fPrintAndLog(FILE *stream, const char *fmt, ...)
{
    va_list argptr;
    static FILE *logfile = NULL;
    static int logging = 1;  // TODO: This is immediately set to 0 below, review logging logic
    char buffer[MAX_PRINT_BUFFER] = {0};
    char buffer2[MAX_PRINT_BUFFER] = {0};
    char buffer3[MAX_PRINT_BUFFER] = {0};
    // lock this section to avoid interlacing prints from different threads
    bool linefeed = true;

    logging = 0;  // TODO: This disables file logging. Is this intended?

// If there is an incoming message from the hardware (eg: lf hid read) in
// the background (while the prompt is displayed and accepting user input),
// stash the prompt and bring it back later.
#ifdef RL_STATE_READCMD
    // We are using GNU readline. libedit (OSX) doesn't support this flag.
    int need_hack = (rl_readline_state & RL_STATE_READCMD) > 0;
    char *saved_line = NULL;  // Initialize to NULL
    int saved_point = 0;      // Initialize to 0

    if (need_hack) {
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();
    }
#endif

    va_start(argptr, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);
    if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == NOLF[0]) {
        linefeed = false;
        buffer[strlen(buffer) - 1] = 0;
    }
    bool filter_ansi = !g_session.supports_colors;
    memcpy_filter_ansi(buffer2, buffer, sizeof(buffer), filter_ansi);
    if (g_printAndLog & PRINTANDLOG_PRINT) {
        memcpy_filter_emoji(buffer3, buffer2, sizeof(buffer2), g_session.emoji_mode);
        fprintf(stream, "%s", buffer3);
        if (linefeed) fprintf(stream, "\n");
    }

#ifdef RL_STATE_READCMD
    // We are using GNU readline. libedit (OSX) doesn't support this flag.
    if (need_hack) {
        rl_restore_prompt();
        rl_replace_line(saved_line, 0);
        rl_point = saved_point;
        rl_redisplay();
        free(saved_line);  // Free the copied text
    }
#endif

    // TODO: Review logging logic. 'logging' is always 0 here.
    if ((g_printAndLog & PRINTANDLOG_LOG) && logging && logfile) {
        memcpy_filter_emoji(buffer3, buffer2, sizeof(buffer2), EMO_ALTTEXT);
        if (filter_ansi) {  // already done
            fprintf(logfile, "%s", buffer3);
        }
        else {
            memcpy_filter_ansi(buffer, buffer3, sizeof(buffer3), true);
            fprintf(logfile, "%s", buffer);
        }
        if (linefeed) fprintf(logfile, "\n");
        fflush(logfile);
    }

    if (flushAfterWrite) fflush(stdout);
}

void memcpy_filter_ansi(void *dest, const void *src, size_t n, bool filter)
{
    if (!dest || !src) return;  // Basic null check

    if (filter) {
        // Filter out ANSI sequences on these OS
        uint8_t *rdest = (uint8_t *)dest;
        const uint8_t *rsrc = (const uint8_t *)src;  // Use const for source
        size_t si = 0;                               // Use size_t for index
        size_t i = 0;

        // Ensure we don't write past the destination buffer boundary 'n'
        while (i < n && si < n) {
            // Check for ESC character '\x1b'
            if (rsrc[i] == '\x1b') {
                // Check for Control Sequence Introducer (CSI) '[', or other single-char sequences
                if (i + 1 < n) {
                    if (rsrc[i + 1] == '[') {  // CSI sequence
                        i += 2;                // Skip ESC and '['
                        // Skip parameter bytes (0x30-0x3F)
                        while (i < n && rsrc[i] >= 0x30 && rsrc[i] <= 0x3F) {
                            i++;
                        }
                        // Skip intermediate bytes (0x20-0x2F)
                        while (i < n && rsrc[i] >= 0x20 && rsrc[i] <= 0x2F) {
                            i++;
                        }
                        // Check for final byte (0x40-0x7E)
                        if (i < n && rsrc[i] >= 0x40 && rsrc[i] <= 0x7E) {
                            i++;       // Skip the final byte
                            continue;  // Continue to next character after sequence
                        }
                        // If sequence is incomplete or invalid, backtrack 'i' to avoid skipping valid chars
                        // This part is tricky; for simplicity, we might just copy the partial sequence
                        // if it doesn't end correctly. A more robust parser might be needed.
                        // For now, let's assume sequences are well-formed or we just skip ESC+[
                        // and let the rest be copied if it doesn't match the pattern.
                        // If we reached here, the sequence was malformed after '['.
                        // We already skipped ESC and '[', so just continue processing from current 'i'.
                    }
                    else if (rsrc[i + 1] >= 0x40 && rsrc[i + 1] <= 0x5F) {  // Single char sequence after ESC
                        i += 2;                                             // Skip ESC and the command character
                        continue;                                           // Continue to next character
                    }
                    else {
                        // Not a recognized sequence start after ESC, copy ESC and continue
                        if (si < n) rdest[si++] = rsrc[i];
                        i++;
                    }
                }
                else {
                    // ESC at the very end of the buffer, copy it
                    if (si < n) rdest[si++] = rsrc[i];
                    i++;
                }
            }
            else {
                // Not an ESC character, copy normally
                if (si < n) rdest[si++] = rsrc[i];
                i++;
            }
        }
        // Null-terminate if there's space, assuming dest is a string buffer
        if (si < n) {
            rdest[si] = '\0';
        }
        else if (n > 0) {
            rdest[n - 1] = '\0';  // Ensure null termination even if truncated
        }
    }
    else {
        memcpy(dest, src, n);
    }
}

static bool emojify_token(const char *token, uint8_t token_length, const char **emojified_token,
                          uint8_t *emojified_token_length, emojiMode_t mode)
{
    // Basic null checks
    if (!token || !emojified_token || !emojified_token_length) return false;

    // Iterate through the EmojiTable
    for (int i = 0; EmojiTable[i].alias != NULL; ++i) {  // Check alias for NULL
        // Check if alias length matches and content is the same
        if ((strlen(EmojiTable[i].alias) == token_length) && (memcmp(EmojiTable[i].alias, token, token_length) == 0)) {
            switch (mode) {
                case EMO_EMOJI: {
                    if (EmojiTable[i].emoji) {  // Check emoji for NULL
                        *emojified_token = EmojiTable[i].emoji;
                        *emojified_token_length = strlen(EmojiTable[i].emoji);
                        return true;
                    }
                    break;  // Should not happen if table is well-formed, but good practice
                }
                case EMO_ALTTEXT: {
                    *emojified_token_length = 0;  // Default to 0 if not found in AltTable
                    // Search in EmojiAltTable
                    for (int j = 0; EmojiAltTable[j].alias != NULL; ++j) {  // Check alias for NULL
                        if ((strlen(EmojiAltTable[j].alias) == token_length)
                            && (memcmp(EmojiAltTable[j].alias, token, token_length) == 0)) {
                            if (EmojiAltTable[j].alttext) {  // Check alttext for NULL
                                *emojified_token = EmojiAltTable[j].alttext;
                                *emojified_token_length = strlen(EmojiAltTable[j].alttext);
                            }
                            return true;  // Found alias, return even if alttext is NULL (length will be 0)
                        }
                    }
                    // Alias found in EmojiTable but not EmojiAltTable, return true with length 0
                    return true;
                }
                case EMO_NONE: {
                    *emojified_token_length = 0;
                    return true;  // Alias found, but mode is NONE
                }
                case EMO_ALIAS: {  // This mode means "do nothing", should not be handled here
                    return false;  // Indicate no replacement happened
                }
            }
            // If we fall through the switch (e.g., EMO_EMOJI but emoji is NULL), return false
            return false;
        }
    }
    // Token not found in EmojiTable
    return false;
}

static bool token_charset(uint8_t c)
{
    if ((c >= '0') && (c <= '9')) return true;
    if ((c >= 'a') && (c <= 'z')) return true;
    if ((c >= 'A') && (c <= 'Z')) return true;
    if ((c == '_') || (c == '+') || (c == '-')) return true;
    return false;
}

void memcpy_filter_emoji(void *dest, const void *src, size_t n, emojiMode_t mode)
{
    if (!dest || !src) return;  // Basic null check

    if (mode == EMO_ALIAS) {
        // If mode is ALIAS, just copy the source to destination
        memcpy(dest, src, n);
        // Ensure null termination if possible and if dest is treated as string
        if (n > 0) {
            ((char *)dest)[n - 1] = '\0';
        }
        return;
    }

    // tokenize emoji
    const char *emojified_token = NULL;
    uint8_t emojified_token_length = 0;
    const char *current_token_start = NULL;  // Use const char* for source pointers
    uint8_t current_token_length = 0;
    char *rdest = (char *)dest;
    const char *rsrc = (const char *)src;  // Use const char* for source pointers
    size_t si = 0;                         // Use size_t for destination index

    for (size_t i = 0; i < n && si < n; /* i incremented inside loop */) {
        char current_char = rsrc[i];

        if (current_token_length == 0) {
            // Not currently inside a potential emoji token
            if (current_char == ':') {
                // Start of a potential token
                current_token_start = rsrc + i;
                current_token_length = 1;
                i++;
            }
            else {
                // Regular character, copy to destination
                if (si < n) rdest[si++] = current_char;
                i++;
            }
        }
        else {
            // Currently inside a potential emoji token (started with ':')
            if (current_char == ':') {
                // End of a potential token
                current_token_length++;  // Include the closing ':'
                if (emojify_token(current_token_start, current_token_length, &emojified_token, &emojified_token_length,
                                  mode)) {
                    // Valid emoji token found, copy replacement (or nothing if length is 0)
                    if (si + emojified_token_length <= n) {  // Check bounds before copying
                        memcpy(rdest + si, emojified_token, emojified_token_length);
                        si += emojified_token_length;
                    }
                    else {
                        // Not enough space for the replacement, copy what fits
                        size_t fits = n - si;
                        memcpy(rdest + si, emojified_token, fits);
                        si += fits;
                        // Buffer full, break loop
                        break;
                    }
                }
                else {
                    // Not a valid emoji token (e.g., "::" or ":invalid:"), copy original token
                    if (si + current_token_length <= n) {  // Check bounds
                        memcpy(rdest + si, current_token_start, current_token_length);
                        si += current_token_length;
                    }
                    else {
                        size_t fits = n - si;
                        memcpy(rdest + si, current_token_start, fits);
                        si += fits;
                        break;  // Buffer full
                    }
                }
                // Reset token state
                current_token_length = 0;
                current_token_start = NULL;
                i++;  // Move past the closing ':'
            }
            else if (token_charset(current_char)) {
                // Character is valid within an emoji token, extend current token
                current_token_length++;
                i++;
            }
            else {
                // Invalid character within a potential token (e.g., ":abc def:")
                // Copy the token so far (including the starting ':') and the invalid char
                if (si + current_token_length + 1 <= n) {  // Check bounds
                    memcpy(rdest + si, current_token_start, current_token_length);
                    si += current_token_length;
                    rdest[si++] = current_char;  // Copy the invalid character
                }
                else {
                    // Not enough space, copy what fits from token and potentially the char
                    size_t fits_token = (n - si > current_token_length) ? current_token_length : n - si;
                    memcpy(rdest + si, current_token_start, fits_token);
                    si += fits_token;
                    if (si < n) {  // If there's still space for the invalid char
                        rdest[si++] = current_char;
                    }
                    break;  // Buffer full
                }
                // Reset token state
                current_token_length = 0;
                current_token_start = NULL;
                i++;  // Move past the invalid character
            }
        }
    }

    // If loop finished while inside a potential token (e.g., source ends with ":abc")
    if (current_token_length > 0 && si < n) {
        size_t fits = (n - si > current_token_length) ? current_token_length : n - si;
        memcpy(rdest + si, current_token_start, fits);
        si += fits;
    }

    // Null-terminate the destination buffer if there's space
    if (si < n) {
        rdest[si] = '\0';
    }
    else if (n > 0) {
        rdest[n - 1] = '\0';  // Ensure null termination even if truncated
    }
}
