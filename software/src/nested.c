#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "nested_util.h"

int main(int argc, char *const argv[])
{
    NtpKs1 *pNK = NULL;
    uint32_t i, j, m;
    uint32_t nt1, nt2, nttest, ks1, dist;
    uint8_t par_int;
    uint8_t par_arr[3] = {0x00};

    uint32_t authuid = atoui(argv[1]);  // uid
    dist = atoui(argv[2]);              // dist

    // process all args.
    for (i = 3, j = 0; i < argc; i += 3) {
        // nt + par
        nt1 = atoui(argv[i]);
        nt2 = atoui(argv[i + 1]);
        par_int = atoui(argv[i + 2]);
        if (par_int != 0) {
            for (m = 0; m < 3; m++) {
                par_arr[m] = (par_int >> m) & 0x01;
            }
        }
        else {
            memset(par_arr, 0, 3);
        }
        // Try to recover the keystream1
        nttest = prng_successor(nt1, dist - 14);
        for (m = dist - 14; m <= dist + 14; m += 1) {
            ks1 = nt2 ^ nttest;
            if (valid_nonce(nttest, nt2, ks1, par_arr)) {
                ++j;
                // append to list
                void *tmp = realloc(pNK, sizeof(NtpKs1) * j);
                if (tmp == NULL) {
                    goto error;
                }
                pNK = tmp;
                pNK[j - 1].ntp = nttest;
                pNK[j - 1].ks1 = ks1;
            }
            nttest = prng_successor(nttest, 1);
        }
    }

    uint32_t keyCount = 0;
    uint64_t *keys = nested(pNK, j, authuid, &keyCount);

    if (keyCount > 0) {
        for (i = 0; i < keyCount; i++) {
            printf("Key %d... %" PRIx64 " \r\n", i + 1, keys[i]);
            fflush(stdout);
        }
    }
    fflush(stdout);
    free(keys);
    exit(EXIT_SUCCESS);
error:
    exit(EXIT_FAILURE);
}
