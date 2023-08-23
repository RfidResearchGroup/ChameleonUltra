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
    //Specific and necessary signs do not exist
    TAG_TYPE_UNKNOWN,
    //125kHz (ID card) series
    TAG_TYPE_EM410X,
    // MiFare series
    TAG_TYPE_MIFARE_Mini,
    TAG_TYPE_MIFARE_1024,
    TAG_TYPE_MIFARE_2048,
    TAG_TYPE_MIFARE_4096,
    // NTAG series
    TAG_TYPE_NTAG_213,
    TAG_TYPE_NTAG_215,
    TAG_TYPE_NTAG_216,
} tag_specific_type_t;


#endif
