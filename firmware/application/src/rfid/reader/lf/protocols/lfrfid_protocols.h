#pragma once
#include "protocol.h"
#include "utils/t5577.h"

typedef enum {
    LFRFIDFeatureASK = 1 << 0, /** ASK Demodulation */
    LFRFIDFeaturePSK = 1 << 1, /** PSK Demodulation */
} LFRFIDFeature;

typedef enum {
    LFRFIDProtocolEM4100,
    LFRFIDProtocolEM410032,
    LFRFIDProtocolEM410016,
    LFRFIDProtocolH10301,
    LFRFIDProtocolIdteck,
    LFRFIDProtocolIndala26,
    LFRFIDProtocolIOProxXSF,
    LFRFIDProtocolAwid,
    LFRFIDProtocolFDXA,
    LFRFIDProtocolFDXB,
    LFRFIDProtocolHidGeneric,
    LFRFIDProtocolHidExGeneric,
    LFRFIDProtocolPyramid,
    LFRFIDProtocolViking,
    LFRFIDProtocolJablotron,
    LFRFIDProtocolParadox,
    LFRFIDProtocolPACStanley,
    LFRFIDProtocolKeri,
    LFRFIDProtocolGallagher,
    LFRFIDProtocolNexwatch,
    LFRFIDProtocolMax,
} LFRFIDProtocol;

extern const ProtocolBase* lfrfid_protocols[];
extern size_t lfrfid_protocols_size;

typedef enum {
    LFRFIDWriteTypeT5577,
} LFRFIDWriteType;

typedef struct {
    LFRFIDWriteType write_type;
    union {
        LFRFIDT5577 t5577;
    };
} LFRFIDWriteRequest;
