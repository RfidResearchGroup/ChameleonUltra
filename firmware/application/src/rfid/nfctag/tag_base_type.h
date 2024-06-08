#ifndef TAG_BASE_TYPE_H
#define TAG_BASE_TYPE_H


// Field sensor type
typedef enum  {
    //No sense of induction
    TAG_SENSE_NO,
    // Low -frequency 125kHz field induction
    TAG_SENSE_LF,
    // High -frequency 13.56MHz field induction
    TAG_SENSE_HF,
} tag_sense_type_t;

/**
 *
 *The definition of all types of labels that support analog
 * Note that all the defined label type below is the specific type statistics of the application layer refine
 * No longer distinguish between high and low frequencies
 */

typedef enum {
    TAG_TYPE_UNDEFINED = 0,

    // old HL/LF common types, slots using these ones need to be migrated first
    OLD_TAG_TYPE_EM410X,
    OLD_TAG_TYPE_MIFARE_Mini,
    OLD_TAG_TYPE_MIFARE_1024,
    OLD_TAG_TYPE_MIFARE_2048,
    OLD_TAG_TYPE_MIFARE_4096,
    OLD_TAG_TYPE_NTAG_213,
    OLD_TAG_TYPE_NTAG_215,
    OLD_TAG_TYPE_NTAG_216,

    //////////// LF ////////////

    //////// ASK Tag-Talk-First   100
    // EM410x
    TAG_TYPE_EM410X = 100,
    // FDX-B
    // securakey
    // gallagher
    // PAC/Stanley
    // Presco
    // Visa2000
    // Viking
    // Noralsy
    // Jablotron

    //////// FSK Tag-Talk-First   200
    // HID Prox
    // ioProx
    // AWID
    // Paradox

    //////// PSK Tag-Talk-First   300
    // Indala
    // Keri
    // NexWatch

    //////// Reader-Talk-First    400
    // T5577
    // EM4x05/4x69
    // EM4x50/4x70
    // Hitag series

    //////////// HF ////////////

    // MIFARE Classic series     1000
    TAG_TYPE_MIFARE_Mini = 1000,
    TAG_TYPE_MIFARE_1024,
    TAG_TYPE_MIFARE_2048,
    TAG_TYPE_MIFARE_4096,
    // MFUL / NTAG series        1100
    TAG_TYPE_NTAG_213 = 1100,
    TAG_TYPE_NTAG_215,
    TAG_TYPE_NTAG_216,
    TAG_TYPE_MF0ICU1,
    TAG_TYPE_MF0ICU2,
    TAG_TYPE_MF0UL11,
    TAG_TYPE_MF0UL21,
    // MIFARE Plus series        1200
    // DESFire series            1300

    // ST25TA series             2000

    // HF14A-4 series            3000

} tag_specific_type_t;

#define TAG_SPECIFIC_TYPE_OLD2NEW_LF_VALUES \
    {OLD_TAG_TYPE_EM410X, TAG_TYPE_EM410X}

#define TAG_SPECIFIC_TYPE_OLD2NEW_HF_VALUES \
    {OLD_TAG_TYPE_MIFARE_Mini, TAG_TYPE_MIFARE_Mini},\
    {OLD_TAG_TYPE_MIFARE_1024, TAG_TYPE_MIFARE_1024},\
    {OLD_TAG_TYPE_MIFARE_2048, TAG_TYPE_MIFARE_2048},\
    {OLD_TAG_TYPE_MIFARE_4096, TAG_TYPE_MIFARE_4096},\
    {OLD_TAG_TYPE_NTAG_213, TAG_TYPE_NTAG_213},\
    {OLD_TAG_TYPE_NTAG_215, TAG_TYPE_NTAG_215},\
    {OLD_TAG_TYPE_NTAG_216, TAG_TYPE_NTAG_216}

#define TAG_SPECIFIC_TYPE_LF_VALUES \
    TAG_TYPE_EM410X

#define TAG_SPECIFIC_TYPE_HF_VALUES \
    TAG_TYPE_MIFARE_Mini,\
    TAG_TYPE_MIFARE_1024,\
    TAG_TYPE_MIFARE_2048,\
    TAG_TYPE_MIFARE_4096,\
    TAG_TYPE_NTAG_213,\
    TAG_TYPE_NTAG_215,\
    TAG_TYPE_NTAG_216,\
    TAG_TYPE_MF0ICU1,\
    TAG_TYPE_MF0ICU2,\
    TAG_TYPE_MF0UL11,\
    TAG_TYPE_MF0UL21

typedef struct {
    tag_specific_type_t tag_hf;
    tag_specific_type_t tag_lf;
} tag_slot_specific_type_t;


#endif
