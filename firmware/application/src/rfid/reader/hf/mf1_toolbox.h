#ifndef MF1_TOOLBOX
#define MF1_TOOLBOX

#include <stdint.h>
#include <stdio.h>
#include <rc522.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include "netdata.h"

#define SETS_NR         2       // Using several sets of random number probes, at least two can ensure that there are two sets of random number combinations for intersection inquiries. The larger the value, the easier it is to succeed.
#define DIST_NR         3       // The more distance the distance can accurately judge the communication stability of the current card

// mifare authentication
#define CRYPT_NONE      0
#define CRYPT_ALL       1
#define CRYPT_REQUEST   2
#define AUTH_FIRST      0
#define AUTH_NESTED     2

typedef enum {
    PRNG_STATIC        = 0u, // the random number of the card response is fixed
    PRNG_WEAK          = 1u, // the random number of the card response is weak
    PRNG_HARD          = 2u, // the random number of the card response is unpredictable
} mf1_prng_type_t;

typedef struct {            //Answer the random number parameters required for Nested attack
    uint8_t nt1[4];         //Unblocked explicitly random number
    uint8_t nt2[4];         //Random number of nested verification encryption
    uint8_t par;            //The puppet test of the communication process of nested verification encryption, only the "low 3 digits', that is, the right 3
} mf1_nested_core_t;

typedef struct {
    uint8_t uid[4];
    struct {
        uint8_t nt1[4];
        uint8_t nt2[4];
    } core[2];
} mf1_static_nested_core_t;

typedef enum {
    DARKSIDE_OK               = 0u, // normal process
    DARKSIDE_CANT_FIX_NT      = 1u, // the random number cannot be fixed, this situation may appear on some UID card
    DARKSIDE_LUCKY_AUTH_OK    = 2u, // the direct authentification is successful, maybe the key is just the default one
    DARKSIDE_NO_NAK_SENT      = 3u, // the card does not respond to NACK, it may be a card that fixes Nack logic vulnerabilities
    DARKSIDE_TAG_CHANGED      = 4u, // card change while running DARKSIDE
} mf1_darkside_status_t;

// this struct is also used in the fw/cli protocol, therefore PACKED
typedef struct {
    uint8_t uid[4];
    uint8_t nt[4];
    uint8_t par_list[8];
    uint8_t ks_list[8];
    uint8_t nr[4];
    uint8_t ar[4];
} PACKED DarksideCore_t;

typedef struct {
    uint8_t key[6];
} PACKED mf1_key_t;

typedef struct {
    uint8_t b[10]; // 80 bits: 40 sectors * 2 keys
} PACKED mf1_toolbox_check_keys_of_sectors_mask_t;

typedef struct {
    mf1_toolbox_check_keys_of_sectors_mask_t mask;
    uint8_t keys_len;
    mf1_key_t *keys;
} mf1_toolbox_check_keys_of_sectors_in_t;

typedef struct {
    mf1_toolbox_check_keys_of_sectors_mask_t found;
    mf1_key_t keys[40][2]; // 6 bytes * 2 keys * 40 sectors = 480 bytes
} PACKED mf1_toolbox_check_keys_of_sectors_out_t;

#ifdef __cplusplus
extern "C" {
#endif


uint8_t darkside_recover_key(
    uint8_t targetBlk,
    uint8_t targetTyp,
    uint8_t firstRecover,
    uint8_t ntSyncMax,
    DarksideCore_t *dc,
    mf1_darkside_status_t *darkside_status
);

uint8_t nested_distance_detect(
    uint8_t block,
    uint8_t type,
    uint8_t *key,
    uint8_t *uid,
    uint32_t *distance
);

#define NESTED_CORE_PARAM_DEF \
    uint64_t keyKnown,        \
    uint8_t blkKnown,         \
    uint8_t typKnown,         \
    uint8_t targetBlock,      \
    uint8_t targetType        \

uint8_t nested_recover_key(NESTED_CORE_PARAM_DEF, mf1_nested_core_t ncs[SETS_NR]);
uint8_t static_nested_recover_key(NESTED_CORE_PARAM_DEF, mf1_static_nested_core_t *sncs);

uint8_t check_prng_type(mf1_prng_type_t *type);
uint8_t check_std_mifare_nt_support();
void antenna_switch_delay(uint32_t delay_ms);
uint16_t auth_key_use_522_hw(uint8_t block, uint8_t type, uint8_t *key);

uint16_t mf1_toolbox_check_keys_of_sectors (
    mf1_toolbox_check_keys_of_sectors_in_t *in,
    mf1_toolbox_check_keys_of_sectors_out_t *out
);

#ifdef __cplusplus
}
#endif

#endif
