#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parity.h"
#include "crapto1.h"
#include "mfkey.h"
#include "common.h"

typedef struct {
    uint32_t nt;
    uint32_t nr;
    uint32_t ar;

    uint64_t par_list;
    uint64_t ks_list;
} DarksideParam;

int main(int argc, char *argv[]) {

    if (((argc - 2) % 5) != 0) {
        printf("Unexpected param count\n");
        return EXIT_FAILURE;
    }
    // Initialize UID
    uint32_t uid = (uint32_t)atoui(argv[1]);
    uint32_t count = 0, i, j;
    uint32_t keycount = 0;
    uint64_t *keylist = NULL, *last_keylist = NULL;
    DarksideParam *dps = NULL;
    bool no_key_recover = true;

    for (i = 1; i + 5 < argc;) {
        void *pTmp = realloc(dps, sizeof(DarksideParam) * ++count);
        if (pTmp == NULL) {
            printf("Can't malloc at param construct.");
            return EXIT_FAILURE;
        }
        dps = pTmp;
        dps[count - 1].nt = (uint32_t)atoui(argv[++i]);
        dps[count - 1].ks_list = atoui(argv[++i]);
        dps[count - 1].par_list = atoui(argv[++i]);
        dps[count - 1].nr = (uint32_t)atoui(argv[++i]);
        dps[count - 1].ar = (uint32_t)atoui(argv[++i]);
    }

    for (i = 0; i < count; i++) {
        // Initialize NT, NR, AR
        uint32_t nt = dps[i].nt;
        uint32_t nr = dps[i].nr;
        uint32_t ar = dps[i].ar;

        uint64_t par_list = dps[i].par_list;
        uint64_t ks_list = dps[i].ks_list;

        /*
        printf("UID = %"PRIu32"\r\n", uid);
        printf("NT = %"PRIu32"\r\n", nt);
        printf("KS = %"PRIu64"\r\n", ks_list);
        printf("PAR = %"PRIu64"\r\n", par_list);
        printf("NR = %"PRIu32"\r\n", nr);
        printf("AR = %"PRIu32"\r\n", ar);
        */

        // start decrypting
        keycount = nonce2key(uid, nt, nr, ar, par_list, ks_list, &keylist);

        if (keycount == 0) {
            continue;
        }

        // only parity zero attack
        if (par_list == 0) {
            qsort(keylist, keycount, sizeof(*keylist), compare_uint64);
            keycount = intersection(last_keylist, keylist);
            if (keycount == 0) {
                free(last_keylist);
                last_keylist = keylist;
                continue;
            }
        }
        uint8_t key_tmp[6] = { 0 };
        if (keycount > 0) {
            no_key_recover = false;
            for (j = 0; j < keycount; j++) {
                if (par_list == 0) {
                    num_to_bytes(last_keylist[j], 6, key_tmp);
                } else {
                    num_to_bytes(keylist[j], 6, key_tmp);
                }
                printf("Key%d: %02X%02X%02X%02X%02X%02X\r\n", j + 1, key_tmp[0], key_tmp[1], key_tmp[2], key_tmp[3], key_tmp[4], key_tmp[5]);
            }
        }
    }

    if (no_key_recover) {
        printf("key not found\r\n");
    }

    if (last_keylist == keylist && last_keylist != NULL) {
        free(keylist);
    } else {
        if (last_keylist) {
            free(last_keylist);
        }
        if (keylist) {
            free(keylist);
        }
    }
    free(dps);
    return EXIT_SUCCESS;
}
