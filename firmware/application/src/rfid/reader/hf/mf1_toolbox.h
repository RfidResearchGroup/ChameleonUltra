#ifndef MF1_TOOLBOX
#define MF1_TOOLBOX

#include <stdint.h>
#include <stdio.h>
#include <rc522.h>
#include <stdbool.h>
#include <stdlib.h>


#define SETS_NR         2       // Using several sets of random number probes, at least two can ensure that there are two sets of random number combinations for intersection inquiries. The larger the value, the easier it is to succeed.
#define DIST_NR         3       // The more distance the distance can accurately judge the communication stability of the current card

// mifare authentication
#define CRYPT_NONE      0
#define CRYPT_ALL       1
#define CRYPT_REQUEST   2
#define AUTH_FIRST      0
#define AUTH_NESTED     2

typedef struct {            //Answer the distance parameters required for Nested attack
    uint8_t uid[4];         //The U32 part of the UID part of this distance data
    uint8_t distance[4];    //Unblocked explicitly random number
} NestedDist;


typedef struct {            //Answer the random number parameters required for Nested attack
    uint8_t nt1[4];         //Unblocked explicitly random number
    uint8_t nt2[4];         //Random number of nested verification encryption
    uint8_t par;            //The puppet test of the communication process of nested verification encryption, only the "low 3 digits', that is, the right 3
} NestedCore;

typedef struct {
    uint8_t uid[4];
    uint8_t nt[4];
    uint8_t par_list[8];
    uint8_t ks_list[8];
    uint8_t nr[4];
    uint8_t ar[4];
} DarksideCore;


#ifdef __cplusplus
extern "C" {
#endif


uint8_t darkside_recover_key(
    uint8_t targetBlk,
    uint8_t targetTyp,
    uint8_t firstRecover,
    uint8_t ntSyncMax,
    DarksideCore *dc
);
uint8_t nested_distance_detect(
    uint8_t block,
    uint8_t type,
    uint8_t *key,
    NestedDist *nd
);
uint8_t nested_recover_key(
    uint64_t keyKnown,
    uint8_t blkKnown,
    uint8_t typKnown,
    uint8_t targetBlock,
    uint8_t targetType,
    NestedCore ncs[SETS_NR]
);
uint8_t check_darkside_support(void);
uint8_t check_weak_nested_support(void);
uint8_t check_std_mifare_nt_support(void);
void antenna_switch_delay(uint32_t delay_ms);
uint8_t auth_key_use_522_hw(uint8_t block, uint8_t type, uint8_t *key);

#ifdef __cplusplus
}
#endif

#endif
