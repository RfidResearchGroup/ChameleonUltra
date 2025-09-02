#ifndef UTILS_H_
#define UTILS_H_

// u32 size align.
#define ALIGN_U32 __attribute__((aligned(4)))

#define PACKED __attribute__((packed))

#ifndef ARRAYLEN
#define ARRAYLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

// nrf52840 platform is little endian
#define U16HTONS(x) ((uint16_t)((((x) & (uint16_t)0x00ffU) << 8) | (((x) & (uint16_t)0xff00U) >> 8)))
#define U16NTOHS(x) U16HTONS(x)
#define U32HTONL(x)                                                                 \
    ((((x) & (uint32_t)0x000000ffUL) << 24) | (((x) & (uint32_t)0x0000ff00UL) << 8) \
     | (((x) & (uint32_t)0x00ff0000UL) >> 8) | (((x) & (uint32_t)0xff000000UL) >> 24))
#define U32NTOHL(x) U32HTONL(x)

#endif
