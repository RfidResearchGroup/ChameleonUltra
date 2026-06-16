#include "mf1_crypto1.h"
#include "parity.h"


#define __inline__ inline
#define ODD_PARITY oddparity8


// uncomment if platform is not avr
#define NO_INLINE_ASM 1

#define PRNG_MASK        0x002D0000UL
/* x^16 + x^14 + x^13 + x^11 + 1 */

#define PRNG_SIZE        4 /* Bytes */
#define NONCE_SIZE       4 /* Bytes */

#define LFSR_MASK_EVEN   0x2010E1UL
#define LFSR_MASK_ODD    0x3A7394UL
/* x^48 + x^43 + x^39 + x^38 + x^36 + x^34 + x^33 + x^31 + x^29 +
 * x^24 + x^23 + x^21 + x^19 + x^13 + x^9 + x^7 + x^6 + x^5 + 1 */

#define LFSR_SIZE        6 /* Bytes */

// Functions fa, fb and fc in filter output network. Definitions taken from Timo Kasper's thesis
#define FA(x3, x2, x1, x0) ( \
    ( (x0 | x1) ^ (x0 & x3) ) ^ ( x2 & ( (x0 ^ x1) | x3 ) ) \
)

#define FB(x3, x2, x1, x0) ( \
    ( (x0 & x1) | x2 ) ^ ( (x0 ^ x1) & (x2 | x3) ) \
)

#define FC(x4, x3, x2, x1, x0) ( \
    ( x0 | ( (x1 | x4) & (x3 ^ x4) ) ) ^ ( ( x0 ^ (x1 & x3) ) & ( (x2 ^ x3) | (x1 & x4) ) ) \
)


/* For AVR only */
#ifndef NO_INLINE_ASM


/* Special macros for optimized usage of the xmega */
/* see http://rn-wissen.de/wiki/index.php?title=Inline-Assembler_in_avr-gcc */

/* Split byte into odd and even nibbles- */
/* Used for LFSR setup. */
#define SPLIT_BYTE(__even, __odd, __byte) \
    __asm__ __volatile__ ( \
        "lsr %2"             "\n\t"   \
        "ror %0"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %1"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %0"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %1"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %0"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %1"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %0"             "\n\t"   \
        "lsr %2"             "\n\t"   \
        "ror %1"                      \
        : "+r" (__even),              \
                  "+r" (__odd),       \
          "+r" (__byte)               \
                :                     \
        : "r0" )

/* Shift half LFSR state stored in three registers */
/* Input is bit 0 of __in */
#define SHIFT24(__b0, __b1, __b2, __in) \
    __asm__ __volatile__ (              \
        "lsr %3"    "\n\t"              \
        "ror %2"    "\n\t"              \
        "ror %1"    "\n\t"              \
        "ror %0"                        \
        : "+r" (__b0),                  \
          "+r" (__b1),                  \
          "+r" (__b2),                  \
          "+r" (__in)                   \
        :                               \
        :   )

/* Shift half LFSR state stored in three registers    */
/* Input is bit 0 of __in                             */
/* decrypt with __stream if bit 0 of __decrypt is set */
#define SHIFT24_COND_DECRYPT(__b0, __b1, __b2, __in, __stream, __decrypt) \
    __asm__ __volatile__ ( \
        "sbrc %5, 0"  "\n\t"    \
        "eor  %3, %4" "\n\t"    \
        "lsr  %3"     "\n\t"    \
        "ror  %2"     "\n\t"    \
        "ror  %1"     "\n\t"    \
        "ror  %0"               \
        : "+r" (__b0),          \
          "+r" (__b1),          \
          "+r" (__b2),          \
          "+r" (__in)           \
        : "r"  (__stream),      \
          "r"  (__decrypt)      \
        : "r0" )

/* Shift a byte with input from an other byte  */
/* Input is bit 0 of __in */
#define SHIFT8(__byte, __in) \
        __asm__ __volatile__ (  \
        "lsr %1"    "\n\t"      \
        "ror %0"                \
        : "+r" (__byte),        \
          "+r"  (__in)          \
                :               \
        : "r0" )
/* End AVR specific */
#else

/* Platform independent code */

#define SPLIT_BYTE(__even, __odd, __byte) \
    __even = (__even >> 1) | (__byte<<7); __byte>>=1; \
    __odd  = (__odd  >> 1) | (__byte<<7); __byte>>=1; \
    __even = (__even >> 1) | (__byte<<7); __byte>>=1; \
    __odd  = (__odd  >> 1) | (__byte<<7); __byte>>=1; \
    __even = (__even >> 1) | (__byte<<7); __byte>>=1; \
    __odd  = (__odd  >> 1) | (__byte<<7); __byte>>=1; \
    __even = (__even >> 1) | (__byte<<7); __byte>>=1; \
    __odd  = (__odd  >> 1) | (__byte<<7)

#define SHIFT24(__b0, __b1, __b2, __in) \
               __b0 = (__b0>>1) | (__b1<<7); \
               __b1 = (__b1>>1) | (__b2<<7); \
               __b2 = (__b2>>1) | ((__in)<<7)

#define SHIFT24_COND_DECRYPT(__b0, __b1, __b2, __in, __stream, __decrypt) \
               __b0 = (__b0>>1) | (__b1<<7); \
               __b1 = (__b1>>1) | (__b2<<7); \
               __b2 = (__b2>>1) | (((__in)^((__stream)&(__decrypt)))<<7)

#define SHIFT8(__byte, __in)  __byte = (__byte>>1) | ((__in)<<7)


#endif

/* Space/speed trade-off. */
/* We want speed, so we have to pay with size. */
/* If we combine the A and B filter tables and precalculate the values */
/* for each state byte, we get the following tables which gives a */
/* faster calculation of the filter output */
/* Table of the filter A/B output per byte */
static const uint8_t abFilterTable[3][256] = {
    /* for Odd[0] */
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
    },
    /* for Odd[1] */
    {
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x02,
        0x04, 0x04, 0x04, 0x06, 0x06, 0x04, 0x04, 0x06,
        0x04, 0x06, 0x06, 0x06, 0x06, 0x04, 0x04, 0x06
    },
    /* for Odd[2] */
    {
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x00, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x08, 0x00, 0x08, 0x08, 0x00, 0x08,
        0x10, 0x18, 0x18, 0x18, 0x10, 0x10, 0x10, 0x18,
        0x10, 0x10, 0x18, 0x10, 0x18, 0x18, 0x10, 0x18
    }
};

/* Standard FC  table, feedback at bit 0 */
static const uint8_t TableC0[32] = {
    /* fc with Input {4,3,2,1,0} = (0,0,0,0,0) to (1,1,1,1,1) */
    FC(0, 0, 0, 0, 0), FC(0, 0, 0, 0, 1), FC(0, 0, 0, 1, 0), FC(0, 0, 0, 1, 1),
    FC(0, 0, 1, 0, 0), FC(0, 0, 1, 0, 1), FC(0, 0, 1, 1, 0), FC(0, 0, 1, 1, 1),
    FC(0, 1, 0, 0, 0), FC(0, 1, 0, 0, 1), FC(0, 1, 0, 1, 0), FC(0, 1, 0, 1, 1),
    FC(0, 1, 1, 0, 0), FC(0, 1, 1, 0, 1), FC(0, 1, 1, 1, 0), FC(0, 1, 1, 1, 1),
    FC(1, 0, 0, 0, 0), FC(1, 0, 0, 0, 1), FC(1, 0, 0, 1, 0), FC(1, 0, 0, 1, 1),
    FC(1, 0, 1, 0, 0), FC(1, 0, 1, 0, 1), FC(1, 0, 1, 1, 0), FC(1, 0, 1, 1, 1),
    FC(1, 1, 0, 0, 0), FC(1, 1, 0, 0, 1), FC(1, 1, 0, 1, 0), FC(1, 1, 0, 1, 1),
    FC(1, 1, 1, 0, 0), FC(1, 1, 1, 0, 1), FC(1, 1, 1, 1, 0), FC(1, 1, 1, 1, 1)
};

/* Special table for byte processing, feedback at bit 7 */
static const uint8_t TableC7[32] = {
    /* fc with Input {4,3,2,1,0} = (0,0,0,0,0) to (1,1,1,1,1) */
    FC(0, 0, 0, 0, 0) << 7, FC(0, 0, 0, 0, 1) << 7, FC(0, 0, 0, 1, 0) << 7, FC(0, 0, 0, 1, 1) << 7,
        FC(0, 0, 1, 0, 0) << 7, FC(0, 0, 1, 0, 1) << 7, FC(0, 0, 1, 1, 0) << 7, FC(0, 0, 1, 1, 1) << 7,
            FC(0, 1, 0, 0, 0) << 7, FC(0, 1, 0, 0, 1) << 7, FC(0, 1, 0, 1, 0) << 7, FC(0, 1, 0, 1, 1) << 7,
                FC(0, 1, 1, 0, 0) << 7, FC(0, 1, 1, 0, 1) << 7, FC(0, 1, 1, 1, 0) << 7, FC(0, 1, 1, 1, 1) << 7,
                    FC(1, 0, 0, 0, 0) << 7, FC(1, 0, 0, 0, 1) << 7, FC(1, 0, 0, 1, 0) << 7, FC(1, 0, 0, 1, 1) << 7,
                        FC(1, 0, 1, 0, 0) << 7, FC(1, 0, 1, 0, 1) << 7, FC(1, 0, 1, 1, 0) << 7, FC(1, 0, 1, 1, 1) << 7,
                            FC(1, 1, 0, 0, 0) << 7, FC(1, 1, 0, 0, 1) << 7, FC(1, 1, 0, 1, 0) << 7, FC(1, 1, 0, 1, 1) << 7,
                                FC(1, 1, 1, 0, 0) << 7, FC(1, 1, 1, 0, 1) << 7, FC(1, 1, 1, 1, 0) << 7, FC(1, 1, 1, 1, 1) << 7
};

/* Special table for nibble processing (e.g. ack), feedback at bit 3 */
static const uint8_t TableC3[32] = {
    /* fc with Input {4,3,2,1,0} = (0,0,0,0,0) to (1,1,1,1,1) */
    FC(0, 0, 0, 0, 0) << 3, FC(0, 0, 0, 0, 1) << 3, FC(0, 0, 0, 1, 0) << 3, FC(0, 0, 0, 1, 1) << 3,
        FC(0, 0, 1, 0, 0) << 3, FC(0, 0, 1, 0, 1) << 3, FC(0, 0, 1, 1, 0) << 3, FC(0, 0, 1, 1, 1) << 3,
            FC(0, 1, 0, 0, 0) << 3, FC(0, 1, 0, 0, 1) << 3, FC(0, 1, 0, 1, 0) << 3, FC(0, 1, 0, 1, 1) << 3,
                FC(0, 1, 1, 0, 0) << 3, FC(0, 1, 1, 0, 1) << 3, FC(0, 1, 1, 1, 0) << 3, FC(0, 1, 1, 1, 1) << 3,
                    FC(1, 0, 0, 0, 0) << 3, FC(1, 0, 0, 0, 1) << 3, FC(1, 0, 0, 1, 0) << 3, FC(1, 0, 0, 1, 1) << 3,
                        FC(1, 0, 1, 0, 0) << 3, FC(1, 0, 1, 0, 1) << 3, FC(1, 0, 1, 1, 0) << 3, FC(1, 0, 1, 1, 1) << 3,
                            FC(1, 1, 0, 0, 0) << 3, FC(1, 1, 0, 0, 1) << 3, FC(1, 1, 0, 1, 0) << 3, FC(1, 1, 0, 1, 1) << 3,
                                FC(1, 1, 1, 0, 0) << 3, FC(1, 1, 1, 0, 1) << 3, FC(1, 1, 1, 1, 0) << 3, FC(1, 1, 1, 1, 1) << 3
};

/* Filter Output Macros */
/* Output at bit 7 for optimized byte processing */
#define CRYPTO1_FILTER_OUTPUT_B7_24(__O0, __O1, __O2) TableC7[ abFilterTable[0][__O0] | \
                    abFilterTable[1][__O1] | \
                    abFilterTable[2][__O2]]

/* Output at bit 3 for optimized nibble processing */
#define CRYPTO1_FILTER_OUTPUT_B3_24(__O0, __O1, __O2) TableC3[ abFilterTable[0][__O0] | \
                    abFilterTable[1][__O1] | \
                    abFilterTable[2][__O2]]

/* Output at bit 0 for general purpose */
#define CRYPTO1_FILTER_OUTPUT_B0_24(__O0, __O1, __O2) TableC0[ abFilterTable[0][__O0] | \
                    abFilterTable[1][__O1] | \
                    abFilterTable[2][__O2]]

/* Split Crypto1 state into even and odd bits            */
/* to speed up the output filter network                 */
/* Put both into one struct to enable relative addressing */
typedef struct {
    uint8_t Even[LFSR_SIZE / 2];
    uint8_t Odd[LFSR_SIZE / 2];
} Crypto1LfsrState_t;
static Crypto1LfsrState_t State = { { 0 }, { 0 } };


/* Debug output of state */
void Crypto1GetState(uint8_t *pEven, uint8_t *pOdd) {
    if (pEven) {
        pEven[0] = State.Even[0];
        pEven[1] = State.Even[1];
        pEven[2] = State.Even[2];
    }
    if (pOdd) {
        pOdd[0] = State.Odd[0];
        pOdd[1] = State.Odd[1];
        pOdd[2] = State.Odd[2];
    }

}

/* Proceed LFSR by one clock cycle */
/* Prototype to force inlining */
static __inline__ uint8_t Crypto1LFSRbyteFeedback(uint8_t E0,
        uint8_t E1,
        uint8_t E2,
        uint8_t O0,
        uint8_t O1,
        uint8_t O2) __attribute__((always_inline));
static uint8_t Crypto1LFSRbyteFeedback(uint8_t E0,
                                       uint8_t E1,
                                       uint8_t E2,
                                       uint8_t O0,
                                       uint8_t O1,
                                       uint8_t O2) {
    uint8_t Feedback;

    /* Calculate feedback according to LFSR taps. XOR all state bytes
     * into a single bit. */
    Feedback  = E0 & (uint8_t)(LFSR_MASK_EVEN);
    Feedback ^= E1 & (uint8_t)(LFSR_MASK_EVEN >> 8);
    Feedback ^= E2 & (uint8_t)(LFSR_MASK_EVEN >> 16);

    Feedback ^= O0 & (uint8_t)(LFSR_MASK_ODD);
    Feedback ^= O1 & (uint8_t)(LFSR_MASK_ODD >> 8);
    Feedback ^= O2 & (uint8_t)(LFSR_MASK_ODD >> 16);

    /* fold 8 into 1 bit */
    Feedback ^= ((Feedback >> 4) | (Feedback << 4)); /* Compiler uses a swap for this (fast!) */
    Feedback ^= Feedback >> 2;
    Feedback ^= Feedback >> 1;

    return (Feedback);
}

/* Proceed LFSR by one clock cycle */
/* Prototype to force inlining */
static __inline__ void Crypto1LFSR(uint8_t In) __attribute__((always_inline));
static void Crypto1LFSR(uint8_t In) {
    uint8_t Feedback;
    register uint8_t Temp0, Temp1, Temp2;

    /* Load even state. */
    Temp0 = State.Even[0];
    Temp1 = State.Even[1];
    Temp2 = State.Even[2];


    /* Calculate feedback according to LFSR taps. XOR all 6 state bytes
     * into a single bit. */
    Feedback  = Temp0 & (uint8_t)(LFSR_MASK_EVEN >> 0);
    Feedback ^= Temp1 & (uint8_t)(LFSR_MASK_EVEN >> 8);
    Feedback ^= Temp2 & (uint8_t)(LFSR_MASK_EVEN >> 16);

    Feedback ^= State.Odd[0] & (uint8_t)(LFSR_MASK_ODD >> 0);
    Feedback ^= State.Odd[1] & (uint8_t)(LFSR_MASK_ODD >> 8);
    Feedback ^= State.Odd[2] & (uint8_t)(LFSR_MASK_ODD >> 16);

    Feedback ^= ((Feedback >> 4) | (Feedback << 4)); /* Compiler uses a swap for this (fast!) */
    Feedback ^= Feedback >> 2;
    Feedback ^= Feedback >> 1;

    /* Now the shifting of the Crypto1 state gets more complicated when
     * split up into even/odd parts. After some hard thinking, one can
     * see that after one LFSR clock cycle
     * - the new even state becomes the old odd state
     * - the new odd state becomes the old even state right-shifted by 1. */
    SHIFT24(Temp0, Temp1, Temp2, Feedback);

    /* Convert even state back into byte array and swap odd/even state
    * as explained above. */
    State.Even[0] = State.Odd[0];
    State.Even[1] = State.Odd[1];
    State.Even[2] = State.Odd[2];

    State.Odd[0] = Temp0;
    State.Odd[1] = Temp1;
    State.Odd[2] = Temp2;
}

uint8_t Crypto1FilterOutput(void) {
    return (CRYPTO1_FILTER_OUTPUT_B0_24(State.Odd[0], State.Odd[1], State.Odd[2]));
}

/* Setup LFSR split into odd and even states, feed in uid ^nonce */
/* Version for first (not nested) authentication.                 */
void Crypto1Setup(uint8_t Key[6], uint8_t Uid[4], uint8_t CardNonce[4]) {
    // state registers
    register uint8_t Even0 = 0x00, Even1 = 0x00, Even2 = 0x00;
    register uint8_t Odd0 = 0x00,  Odd1 = 0x00,  Odd2 = 0x00;
    uint8_t KeyStream, Feedback, Out, In, ByteCount;

    KeyStream = *Key++;
    SPLIT_BYTE(Even0, Odd0, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even0, Odd0, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even1, Odd1, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even1, Odd1, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even2, Odd2, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even2, Odd2, KeyStream);

    for (ByteCount = 0; ByteCount < NONCE_SIZE; ByteCount++) {
        In = *CardNonce ^ *Uid++;

        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 1 */
        In >>= 1;
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 2 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 3 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 4 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 5 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 6 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 7 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        *CardNonce++ ^= KeyStream;  // Encrypt Nonce byte
    }
    // save state
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;
}

/* Setup LFSR split into odd and even states, feed in uid ^nonce    */
/* Version for nested authentication.                               */
/* Also generates encrypted parity bits at CardNonce[4]..[7]        */
/* Use: Decrypt = false for the tag, Decrypt = true for the reader  */
void Crypto1SetupNested(uint8_t Key[6], uint8_t Uid[4], uint8_t CardNonce[4], uint8_t NonceParity[4], bool Decrypt) {
    // state registers
    register uint8_t Even0 = 0x00, Even1 = 0x00, Even2 = 0x00;
    register uint8_t Odd0 = 0x00,  Odd1 = 0x00,  Odd2 = 0x00;
    uint8_t KeyStream, Feedback, Out, In, ByteCount;

    KeyStream = *Key++;
    SPLIT_BYTE(Even0, Odd0, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even0, Odd0, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even1, Odd1, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even1, Odd1, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even2, Odd2, KeyStream);
    KeyStream = *Key++;
    SPLIT_BYTE(Even2, Odd2, KeyStream);

    /* Get first filter output */
    Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);

    for (ByteCount = 0; ByteCount < NONCE_SIZE; ByteCount++) {
        In = *CardNonce ^ *Uid++;

        /* we can reuse the filter output used to decrypt the parity bit! */
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Even0, Even1, Even2, Feedback, Out, Decrypt);

        /* Bit 1 */
        In >>= 1;
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Odd0, Odd1, Odd2, Feedback, Out, Decrypt);

        /* Bit 2 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Even0, Even1, Even2, Feedback, Out, Decrypt);

        /* Bit 3 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Odd0, Odd1, Odd2, Feedback, Out, Decrypt);

        /* Bit 4 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Even0, Even1, Even2, Feedback, Out, Decrypt);

        /* Bit 5 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Odd0, Odd1, Odd2, Feedback, Out, Decrypt);

        /* Bit 6 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Even0, Even1, Even2, Feedback, Out, Decrypt);

        /* Bit 7 */
        In >>= 1;
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        SHIFT8(KeyStream, Out);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        Feedback ^= In;
        SHIFT24_COND_DECRYPT(Odd0, Odd1, Odd2, Feedback, Out, Decrypt);

        /* Generate parity bit */
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        In = *CardNonce;
        Feedback = ODD_PARITY(In);
        // Store parity bit to out buffer
        *NonceParity++ = Out ^ Feedback;  /* Encrypted parity at Offset 4*/

        /* Encrypt byte   */
        *CardNonce++ = In ^ KeyStream;
    }
    /* save state */
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;
}

/* Crypto1Auth is similar to Crypto1Byte but */
/* EncryptedReaderNonce is decrypted and fed back */
void Crypto1Auth(uint8_t EncryptedReaderNonce[NONCE_SIZE]) {
    /* registers to hold temporary LFSR state */
    register uint8_t Even0, Even1, Even2;
    register uint8_t Odd0, Odd1, Odd2;
    uint8_t In, Feedback, i;

    /* read state */
    Even0 = State.Even[0];
    Even1 = State.Even[1];
    Even2 = State.Even[2];
    Odd0 = State.Odd[0];
    Odd1 = State.Odd[1];
    Odd2 = State.Odd[2];

    /* 4 Bytes */
    for (i = 0; i < NONCE_SIZE; i++) {
        In = EncryptedReaderNonce[i];

        /* Bit 0 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        Feedback = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2)
                   ^ Feedback
                   ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 1 */
        /* remember Odd/Even swap has been omitted! */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2)
                   ^ Feedback
                   ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 2 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2)
                    ^ Feedback
                    ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 3 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2)
                   ^ Feedback
                   ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 4 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        Feedback = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2)
                   ^ Feedback
                   ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 5 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2)
                   ^ Feedback
                   ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 6 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        Feedback = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2)
                   ^ Feedback
                   ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 7 */
        Feedback = CRYPTO1_FILTER_OUTPUT_B0_24(Even0, Even1, Even2);
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2)
                   ^ Feedback
                   ^ In;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);
    }
    // save state
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;
}

/* Crypto1Nibble generates keystream for a nibble (4 bit) */
/* no input to the LFSR  */
uint8_t Crypto1Nibble(void) {
    /* state registers */
    register uint8_t Even0, Even1, Even2;
    register uint8_t Odd0,  Odd1,  Odd2;
    uint8_t KeyStream, Feedback, Out;

    /* read state */
    Even0 = State.Even[0];
    Even1 = State.Even[1];
    Even2 = State.Even[2];
    Odd0 = State.Odd[0];
    Odd1 = State.Odd[1];
    Odd2 = State.Odd[2];

    /* Bit 0, initialise keystream */
    KeyStream = CRYPTO1_FILTER_OUTPUT_B3_24(Odd0, Odd1, Odd2);
    Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
    SHIFT24(Even0, Even1, Even2, Feedback);

    /* Bit 1 */
    Out = CRYPTO1_FILTER_OUTPUT_B3_24(Even0, Even1, Even2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
    SHIFT24(Odd0, Odd1, Odd2, Feedback);

    /* Bit 2 */
    Out = CRYPTO1_FILTER_OUTPUT_B3_24(Odd0, Odd1, Odd2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
    SHIFT24(Even0, Even1, Even2, Feedback);

    /* Bit 3 */
    Out = CRYPTO1_FILTER_OUTPUT_B3_24(Even0, Even1, Even2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
    SHIFT24(Odd0, Odd1, Odd2, Feedback);

    /* save state */
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;

    return (KeyStream);
}

/* Crypto1Byte generates keystream for a byte (8 bit) */
/* no input to the LFSR  */
uint8_t Crypto1Byte(void) {
    /* state registers */
    register uint8_t Even0, Even1, Even2;
    register uint8_t Odd0,  Odd1,  Odd2;
    uint8_t KeyStream, Feedback, Out;

    /* read state */
    Even0 = State.Even[0];
    Even1 = State.Even[1];
    Even2 = State.Even[2];
    Odd0 = State.Odd[0];
    Odd1 = State.Odd[1];
    Odd2 = State.Odd[2];

    /* Bit 0, initialise keystream */
    KeyStream = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
    Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
    SHIFT24(Even0, Even1, Even2, Feedback);

    /* Bit 1 */
    /* remember Odd/Even swap has been omitted! */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
    SHIFT24(Odd0, Odd1, Odd2, Feedback);

    /* Bit 2 */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
    SHIFT24(Even0, Even1, Even2, Feedback);

    /* Bit 3 */
    /* remember Odd/Even swap has been omitted! */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
    SHIFT24(Odd0, Odd1, Odd2, Feedback);

    /* Bit 4 */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
    SHIFT24(Even0, Even1, Even2, Feedback);

    /* Bit 5 */
    /* remember Odd/Even swap has been omitted! */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
    SHIFT24(Odd0, Odd1, Odd2, Feedback);

    /* Bit 6 */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
    SHIFT24(Even0, Even1, Even2, Feedback);

    /* Bit 7 */
    /* remember Odd/Even swap has been omitted! */
    Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
    KeyStream = (KeyStream >> 1) | Out;
    Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
    SHIFT24(Odd0, Odd1, Odd2, Feedback);

    /* save state */
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;

    return (KeyStream);
}

/* Crypto1ByteArray transcripts array of bytes         */
/* No input to the LFSR                                */
/* Avoids load/store of the LFSR-state for each byte!  */
/* Enhancement for the original function Crypto1Byte() */
void Crypto1ByteArray(uint8_t *Buffer, uint8_t Count) {
    /* state registers */
    register uint8_t Even0, Even1, Even2;
    register uint8_t Odd0,  Odd1,  Odd2;
    uint8_t KeyStream, Feedback, Out;

    /* read state */
    Even0 = State.Even[0];
    Even1 = State.Even[1];
    Even2 = State.Even[2];
    Odd0 = State.Odd[0];
    Odd1 = State.Odd[1];
    Odd2 = State.Odd[2];

    while (Count--) {
        /* Bit 0, initialise keystream */
        KeyStream = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 1 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 2 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 3 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 4 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 5 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 6 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 7 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Transcript and increment buffer address */
        *Buffer++ ^= KeyStream;
    }

    /* save state */
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;
}

/* Crypto1ByteArrayWithParity encrypts an array of bytes   */
/* and generates the parity bits                           */
/* No input to the LFSR                                    */
/* Avoids load/store of the LFSR-state for each byte!      */
/* The filter output used to encrypt the parity is         */
/* reused to encrypt bit 0 in the next byte.               */
void Crypto1ByteArrayWithParity(uint8_t *Buffer, uint8_t *Parity, uint8_t Count) {
    /* state registers */
    register uint8_t Even0, Even1, Even2;
    register uint8_t Odd0,  Odd1,  Odd2;
    // KeyStream is direct to use, must to init.
    uint8_t KeyStream = 0x00, Feedback, Out;

    /* read state */
    Even0 = State.Even[0];
    Even1 = State.Even[1];
    Even2 = State.Even[2];
    Odd0 = State.Odd[0];
    Odd1 = State.Odd[1];
    Odd2 = State.Odd[2];

    /* First pass needs output, next pass uses parity bit! */
    Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);

    while (Count--) {
        /* Bit 0, initialise keystream from parity */
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 1 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 2 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 3 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 4 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 5 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 6 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2);
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 7 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2);
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Next bit encodes parity */
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        *Parity++ = ODD_PARITY(*Buffer) ^ Out;

        /* encode Byte */
        *Buffer++ ^= KeyStream;
    }
    /* save state */
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;
}

/* Crypto1ByteArrayWithParity encrypts an array of bytes   */
/* and generates the parity bits                           */
/* No input to the LFSR                                    */
/* Avoids load/store of the LFSR-state for each byte!      */
/* The filter output used to encrypt the parity is         */
/* reused to encrypt bit 0 in the next byte.               */
void Crypto1ByteArrayWithParityHasIn(uint8_t *Buffer, uint8_t *Parity, uint8_t Count) {
    /* state registers */
    register uint8_t Even0, Even1, Even2;
    register uint8_t Odd0,  Odd1,  Odd2;
    // KeyStream is direct to use, must to init.
    uint8_t KeyStream = 0x00, Feedback, Out;

    /* read state */
    Even0 = State.Even[0];
    Even1 = State.Even[1];
    Even2 = State.Even[2];
    Odd0 = State.Odd[0];
    Odd1 = State.Odd[1];
    Odd2 = State.Odd[2];

    /* First pass needs output, next pass uses parity bit! */
    Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);

    while (Count--) {
        uint8_t In = *Buffer;

        /* Bit 0, initialise keystream from parity */
        SHIFT8(KeyStream, Out);
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2) ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 1 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2) ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 2 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2) ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 3 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2) ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 4 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2) ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 5 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2) ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Bit 6 */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Odd0, Odd1, Odd2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback  = Crypto1LFSRbyteFeedback(Even0, Even1, Even2, Odd0, Odd1, Odd2) ^ In;
        In >>= 1;
        SHIFT24(Even0, Even1, Even2, Feedback);

        /* Bit 7 */
        /* remember Odd/Even swap has been omitted! */
        Out = CRYPTO1_FILTER_OUTPUT_B7_24(Even0, Even1, Even2);
        KeyStream = (KeyStream >> 1) | Out;
        Feedback = Crypto1LFSRbyteFeedback(Odd0, Odd1, Odd2, Even0, Even1, Even2) ^ In;
        In >>= 1;
        SHIFT24(Odd0, Odd1, Odd2, Feedback);

        /* Next bit encodes parity */
        Out = CRYPTO1_FILTER_OUTPUT_B0_24(Odd0, Odd1, Odd2);
        *Parity++ = ODD_PARITY(*Buffer) ^ Out;

        /* encode Byte */
        *Buffer++ ^= KeyStream;
    }
    /* save state */
    State.Even[0] = Even0;
    State.Even[1] = Even1;
    State.Even[2] = Even2;
    State.Odd[0]  = Odd0;
    State.Odd[1]  = Odd1;
    State.Odd[2]  = Odd2;
}

/* Function Crypto1PRNG                                           */
/* New version of the PRNG which can calculate multiple           */
/* feedback bits at once!                                         */
/* Feedback mask = 0x2d  = 101101 binary                          */
/* Because pattern 101 is repeated, only 2 shifts are necessary!  */
/*      Feedback ^= Feedback >> 3;     folds 101 101 to 101       */
/*      Feedback ^= Feedback >> 2;     folds 101 => 1             */
/* With these two lines not only bit 0 is calculated,             */
/* but all the bits which do no overlap with the feedback!        */
/* I.e. the 10 leading zeros in the feedback mask bits            */
/* gives us a total of 11 valid feedback bits!                    */
/* The ClockCount for the PRNG is always multiple of 32!          */
/* Up tp 11 Bits can be calculated at once                        */
/* Split into chunks of 11+11+10 = 32 bits                        */
/* This avoids a calculated number of shifts                      */
void Crypto1PRNG(uint8_t State[4], uint8_t ClockCount) {
    /* For ease of processing convert the state into a 32 bit integer first */
    uint32_t Temp;
    uint16_t Feedback;

    Temp  = (uint32_t) State[0] << 0;
    Temp |= (uint32_t) State[1] << 8;
    Temp |= (uint32_t) State[2] << 16;
    Temp |= (uint32_t) State[3] << 24;

    /* PRNG is always a multiple of 32!        */
    /* Up tp 11 Bits can be calculated at once */
    /* Split into chunks of 11+11+10 = 32 bits */
    while (ClockCount >= 32) {
        Feedback = (uint16_t)(Temp >> 16);
        Feedback ^= Feedback >> 3;   /* 2d = 101101,  fold 101 101 => 101 */
        Feedback ^= Feedback >> 2;   /* fold 101 => 1 */
        /* Cycle LFSR and feed back. */
        Temp = (Temp >> 11) | (((uint32_t)Feedback) << (32 - 11));

        /* Same for the next 11 Bits */
        Feedback = (uint16_t)(Temp >> 16);
        Feedback ^= Feedback >> 3;   /* 2d = 101101,  fold 101 101 => 101 */
        Feedback ^= Feedback >> 2;   /* fold 101 => 1 */
        Temp = (Temp >> 11) | (((uint32_t)Feedback) << (32 - 11));

        /* Remaining 10 bits */
        Feedback = (uint16_t)(Temp >> 16);
        Feedback ^= Feedback >> 3;   /* 2d = 101101,  fold 101 101 => 101 */
        Feedback ^= Feedback >> 2;   /* fold 101 => 1 */
        Temp = (Temp >> 10) | (((uint32_t)Feedback) << (32 - 10));

        /* Now 32 bits are fed back */
        ClockCount -= 32;
    }

    /* Store back state */
    State[0] = (uint8_t)(Temp >> 0);
    State[1] = (uint8_t)(Temp >> 8);
    State[2] = (uint8_t)(Temp >> 16);
    State[3] = (uint8_t)(Temp >> 24);
}

/* prng_successor
 * helper used to obscure the keystream during authentication
 */
uint32_t Crypto1FreePRNG(uint32_t x, uint32_t n) {
    x = __builtin_bswap32(x);

    while (n--)
        x = x >> 1 | (x >> 16 ^ x >> 18 ^ x >> 19 ^ x >> 21) << 31;

    return __builtin_bswap32(x);
}


void Crypto1EncryptWithParity(uint8_t *Buffer, uint8_t BitCount) {
    uint8_t i = 0;
    while (i < BitCount) {
        Buffer[i / 8] ^=
            CRYPTO1_FILTER_OUTPUT_B0_24(State.Odd[0], State.Odd[1], State.Odd[2])
            << (i % 8);
        if (++i % 9 != 0) // only shift, if this was no parity bit
            Crypto1LFSR(0);
    }
}

void Crypto1ReaderAuthWithParity(uint8_t PlainReaderAnswerWithParityBits[9]) {
    uint8_t i = 0, feedback;
    while (i < 72) {
        feedback = PlainReaderAnswerWithParityBits[i / 8] >> (i % 8);
        PlainReaderAnswerWithParityBits[i / 8] ^=
            CRYPTO1_FILTER_OUTPUT_B0_24(State.Odd[0], State.Odd[1], State.Odd[2])
            << (i % 8);
        if (++i % 9 != 0) { // only shift, if this was no parity bit
            if (i <= 36)
                Crypto1LFSR(feedback & 1);
            else
                Crypto1LFSR(0);
        }
    }
}
