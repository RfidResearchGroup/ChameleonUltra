#include <errno.h>  // For error handling
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmdhfmfhard.h"
#include "crapto1.h"
#include "parity.h"

typedef enum {
    KEY_A = 0,  // Matches the binary file format (0 for A)
    KEY_B = 1   // Matches the binary file format (1 for B)
} key_type_t;

bool read_uint32_le(FILE *f, uint32_t *value)
{
    uint8_t bytes[4];
    size_t read_count = fread(bytes, 1, 4, f);
    if (read_count != 4) {
        if (feof(f)) {
            return false;  // Clean EOF
        }
        else {
            perror("fread uint32_le failed");
            fprintf(stderr, "Read only %zu bytes\n", read_count);
            return false;  // Error
        }
    }
    // Construct Little-Endian value
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return true;
}

bool read_uint32_be(FILE *f, uint32_t *value)
{
    uint8_t bytes[4];
    size_t read_count = fread(bytes, 1, 4, f);
    if (read_count != 4) {
        if (feof(f)) {
            return false;  // Clean EOF
        }
        else {
            perror("fread uint32_be failed");
            fprintf(stderr, "Read only %zu bytes\n", read_count);
            return false;  // Error
        }
    }
    // Construct Big-Endian value
    *value = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
    return true;
}

bool read_uint8(FILE *f, uint8_t *value)
{
    size_t read_count = fread(value, sizeof(uint8_t), 1, f);
    if (read_count != 1) {
        if (feof(f)) {
            return false;  // Clean EOF
        }
        else {
            perror("fread uint8 failed");
            fprintf(stderr, "Read only %zu bytes\n", read_count);
            return false;  // Error
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        // Updated usage message for the new binary input
        fprintf(stderr, "Usage: %s <binary_nonce_file_path.bin>\n", argv[0]);
        return 1;
    }

    char *binary_file_path = argv[1];

    // --- Open binary input file ---
    FILE *bin_fp = fopen(binary_file_path, "rb");  // Open in binary read mode
    if (bin_fp == NULL) {
        perror("Error opening binary nonce file");
        return 1;
    }

    // --- Read Header ---
    uint32_t uid;
    uint8_t sector;
    uint8_t key_type_byte;

    // Use Big-Endian reader for UID
    if (!read_uint32_be(bin_fp, &uid)) {
        fprintf(stderr, "Error reading UID (BE) from binary file header.\n");
        fclose(bin_fp);
        return 1;
    }
    // Sector and KeyType are single bytes, no endianness issue
    if (!read_uint8(bin_fp, &sector)) {
        fprintf(stderr, "Error reading Sector from binary file header.\n");
        fclose(bin_fp);
        return 1;
    }
    if (!read_uint8(bin_fp, &key_type_byte)) {
        fprintf(stderr, "Error reading KeyType from binary file header.\n");
        fclose(bin_fp);
        return 1;
    }

    // Validate key_type_byte
    if (key_type_byte != KEY_A && key_type_byte != KEY_B) {
        fprintf(stderr, "Error: Invalid key type byte %u in header. Should be 0 or 1.\n", key_type_byte);
        fclose(bin_fp);
        return 1;
    }
    key_type_t key_type = (key_type_t)key_type_byte;

    // --- Create and open temporary text file ---
    char temp_file[] = "temp_nonces.txt";
    FILE *temp_fp = fopen(temp_file, "w");
    if (temp_fp == NULL) {
        perror("Error creating temporary file");
        fclose(bin_fp);
        return 1;
    }

    printf("Read Header -> UID: %08x, Sector: %u, Key type: %c\n", uid, sector, (key_type == KEY_A) ? 'A' : 'B');
    printf("Reading nonce data from binary file: %s\n", binary_file_path);

    // --- Read binary file (nonce data) and write to temp text file ---
    uint32_t nt_enc1, nt_enc2;
    uint8_t par_packed;
    uint8_t par_enc1, par_enc2;
    size_t nonces_processed = 0;  // Counts pairs of nonces (nt1, nt2)

    while (true) {
        // *** Use Big-Endian reader for nonces ***
        if (!read_uint32_be(bin_fp, &nt_enc1)) {
            if (feof(bin_fp)) break;  // Expected EOF after last full chunk
            fprintf(stderr, "Error reading nt_enc1 (BE) from binary file body.\n");
            fclose(bin_fp);
            fclose(temp_fp);
            remove(temp_file);
            return 1;
        }

        // *** Use Big-Endian reader for nonces ***
        if (!read_uint32_be(bin_fp, &nt_enc2)) {
            // Should not happen if nt_enc1 read succeeded, unless file is truncated
            fprintf(stderr, "Error reading nt_enc2 (BE) from binary file body (truncated file?).\n");
            fclose(bin_fp);
            fclose(temp_fp);
            remove(temp_file);
            return 1;
        }

        // Read packed parity byte (single byte, no endianness issue)
        if (!read_uint8(bin_fp, &par_packed)) {
            // Should not happen if nt_enc2 read succeeded, unless file is truncated
            fprintf(stderr, "Error reading packed parity from binary file body (truncated file?).\n");
            fclose(bin_fp);
            fclose(temp_fp);
            remove(temp_file);
            return 1;
        }

        // Extract individual parities
        par_enc1 = par_packed >> 4;
        par_enc2 = par_packed & 0x0F;

        // Write both nonce/parity pairs to temp file in expected text format
        if (fprintf(temp_fp, "%u|%u\n", nt_enc1, par_enc1) < 0) {
            perror("Error writing nt_enc1 pair to temporary file");
            fclose(bin_fp);
            fclose(temp_fp);
            remove(temp_file);
            return 1;
        }
        if (fprintf(temp_fp, "%u|%u\n", nt_enc2, par_enc2) < 0) {
            perror("Error writing nt_enc2 pair to temporary file");
            fclose(bin_fp);
            fclose(temp_fp);
            remove(temp_file);
            return 1;
        }

        nonces_processed++;
    }

    fclose(bin_fp);
    fclose(temp_fp);  // Close temp file so mfnestedhard can read it

    printf("Processed %zu nonce pairs (total %zu nonces) from binary file.\n", nonces_processed, nonces_processed * 2);

    if (nonces_processed == 0) {
        fprintf(stderr, "Error: No nonce data chunks found in the binary file after the header.\n");
        remove(temp_file);
        return 1;
    }

    // --- Call the core attack function ---
    uint64_t foundkey = 0;
    // mfnestedhard expects keyType as 0 for A, 1 for B, which matches our enum/byte value
    int result = mfnestedhard(sector, key_type, NULL, 0, 0, NULL, false, false, false, &foundkey, NULL, uid, temp_file);

    // --- Report result ---
    if (result == 1) {
        printf("Key found: %012" PRIx64 "\n", foundkey);
        // Original code prints UID/Sector/KeyType here too, which is good for clarity
        printf("Details -> UID: %08x, Sector: %u, Key type: %c\n", uid, sector, (key_type == KEY_A) ? 'A' : 'B');
    }
    else {
        printf("Key not found.\n");
        printf("Details -> UID: %08x, Sector: %u, Key type: %c\n", uid, sector, (key_type == KEY_A) ? 'A' : 'B');
    }

    // --- Cleanup ---
    remove(temp_file);

    return (result == 1) ? 0 : 1;  // Return 0 on success (key found), 1 otherwise
}
