/**
 * @file nfc_seos.c
 * @brief SEOS emulation for ChameleonUltra
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include "nfc_seos.h"
#include "nfc_14a_4.h"
#include "nfc_14a.h"
#include "tag_emulation.h"
#include "tag_persistence.h"
#include "fds_util.h"
#include "nrf_log.h"

#include <nrf_crypto_aes.h>
#include <nrf_crypto_hash.h>

// Only AES128/SHA256 are currently available. 3DES and SHA1 would have to be implemented in software.

#define SEOS_ENCRYPTION_2K3DES      0x02
#define SEOS_ENCRYPTION_3K3DES      0x03
#define SEOS_ENCRYPTION_AES         0x09

#define SEOS_HASHING_SHA1           0x06
#define SEOS_HASHING_SHA256         0x07

const uint8_t SEOS_AID[] = { 0xA0, 0x00, 0x00, 0x04, 0x40, 0x00, 0x01, 0x01, 0x00, 0x01 };

static uint8_t block_size(uint8_t algorithm) {
    if (algorithm == SEOS_ENCRYPTION_AES) {
        return 16;
    } else if (algorithm == SEOS_ENCRYPTION_2K3DES) {
        return 8;
    } else if (algorithm == SEOS_ENCRYPTION_3K3DES) {
        return 8;
    } else {
        NRF_LOG_ERROR("Seos: Unknown Encryption Algorithm");
        return 0;
    }
}

static uint8_t round_to_next(uint8_t value, uint8_t step) {
    if (value % step == 0) {
        return value;
    } else {
        return value + step - (value % step);
    }
}

static nrf_crypto_aes_context_t nrf_aes_ctx;
static nrf_crypto_hash_context_t nrf_hash_ctx;

static uint8_t cryptogram_iv[16] = {0x00};
static bool generate_cryptogram(uint8_t *key, bool use_iv, uint8_t *input, size_t length, uint8_t *output, uint8_t algorithm) {
    if (!use_iv) {
        memset(cryptogram_iv, 0x00, 16);
    }

    if (algorithm == SEOS_ENCRYPTION_AES) {
        ret_code_t result = nrf_crypto_aes_crypt(
            &nrf_aes_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_ENCRYPT,
            key, cryptogram_iv,
            input, length,
            output, &length
        );

        if (result != NRF_SUCCESS) {
            NRF_LOG_ERROR("Seos: Error decrypting cryptogram: %s", nrf_crypto_error_string_get(result));
            return false;
        }
    } else {
        NRF_LOG_ERROR("Seos: Unknown Encryption Algorithm");
        return false;
    }

    return true;
}

static bool decrypt_cryptogram(uint8_t *key, uint8_t *input, size_t length, uint8_t *output, uint8_t algorithm) {
    memset(cryptogram_iv, 0x00, 16);

    if (algorithm == SEOS_ENCRYPTION_AES) {
        ret_code_t result = nrf_crypto_aes_crypt(
            &nrf_aes_ctx, &g_nrf_crypto_aes_cbc_128_info, NRF_CRYPTO_DECRYPT,
            key, cryptogram_iv,
            input, length,
            output, &length
        );

        if (result != NRF_SUCCESS) {
            NRF_LOG_ERROR("Seos: Error decrypting cryptogram: %s", nrf_crypto_error_string_get(result));
            return false;
        }
    } else {
        NRF_LOG_ERROR("Seos: Unknown Encryption Algorithm");
        return false;
    }

    return true;
}

static bool generate_cmac(uint8_t *key, uint8_t *input, size_t length, uint8_t *output, uint8_t encryption_algorithm) {
    memset(cryptogram_iv, 0x00, 16);

    if (encryption_algorithm == SEOS_ENCRYPTION_AES) {
        size_t hash_size = NRF_CRYPTO_AES_BLOCK_SIZE;
        ret_code_t result = nrf_crypto_aes_crypt(
            &nrf_aes_ctx, &g_nrf_crypto_aes_cmac_128_info, NRF_CRYPTO_MAC_CALCULATE,
            key, cryptogram_iv,
            input, length,
            output, &hash_size
        );

        if (result != NRF_SUCCESS) {
            NRF_LOG_ERROR("Seos: Error generating CMAC: %s", nrf_crypto_error_string_get(result));
            return false;
        } else if (hash_size != 16) {
            NRF_LOG_ERROR("Seos: Incorrect CMAC output size: %d", hash_size);
            return false;
        }
    } else {
        NRF_LOG_ERROR("Seos: Unknown Encryption Algorithm");
        return false;
    }

    return true;
}

static void seos_kdf(bool forEncryption, uint8_t *masterKey, uint8_t keyslot, uint8_t *work_buffer,
                     uint8_t *adfOid, size_t adfoid_len, uint8_t *diversifier, uint8_t diversifier_len, uint8_t *out, int encryption_algorithm, int hash_algorithm) {

    // Encryption key      = 04
    // KEK Encryption key  = 05
    // MAC key             = 06
    // KEK MAC key         = 07

    uint8_t typeOfKey = 0x06;
    if (forEncryption == true) {
        typeOfKey = 0x04;
    }

    memset(work_buffer, 0x00, 16);
    work_buffer[11] = typeOfKey;
    work_buffer[14] = 0x80;
    work_buffer[15] = 0x01;
    work_buffer[16] = encryption_algorithm;
    work_buffer[17] = hash_algorithm;
    work_buffer[18] = keyslot;
    memcpy(work_buffer + 19, adfOid, adfoid_len);
    memcpy(work_buffer + 19 + adfoid_len, diversifier, diversifier_len);

    // This CMAC always uses AES, regardless of the main encryption algorithm in use.
    generate_cmac(masterKey, work_buffer, 19 + adfoid_len + diversifier_len, out, SEOS_ENCRYPTION_AES);
}

#define WORK_BUFFER_SIZE 0x80
static uint8_t work_buffer_a[WORK_BUFFER_SIZE];
static uint8_t work_buffer_b[WORK_BUFFER_SIZE];

/* ------------------------------------------------------------------ */
/*  Module state                                                        */
/* ------------------------------------------------------------------ */
static nfc_tag_seos_information_t *m_tag_information = NULL;

/* Shadow coll-res references into m_tag_information */
static nfc_tag_14a_coll_res_reference_t m_shadow_coll_res;

/* T=CL session state */
static nfc_tag_14a_4_tcl_state_t m_tcl_session_state;

// These values are determined at runtime
static uint8_t RND_ICC[8] = { 0x00 };
static uint8_t RND_IFD[8] = { 0x00 };
static uint8_t KEY_ICC[16] = { 0x00 };
static uint8_t KEY_IFD[16] = { 0x00 };
static uint8_t diver_encr_key[16] = { 0x00 };
static uint8_t diver_cmac_key[16] = { 0x00 };

/* ------------------------------------------------------------------ */
/*  Anti-collision resource                                             */
/* ------------------------------------------------------------------ */

nfc_tag_14a_coll_res_reference_t *nfc_tag_seos_get_coll_res(void) {
    if (m_tag_information == NULL) return NULL;
    m_shadow_coll_res.sak  = m_tag_information->res_coll.sak;
    m_shadow_coll_res.atqa = m_tag_information->res_coll.atqa;
    m_shadow_coll_res.uid  = m_tag_information->res_coll.uid;
    m_shadow_coll_res.size = &m_tag_information->res_coll.size;
    m_shadow_coll_res.ats  = &m_tag_information->res_coll.ats;
    return &m_shadow_coll_res;
}

/* ------------------------------------------------------------------ */
/*  State handler (called from NFCT ISR on each received frame)        */
/* ------------------------------------------------------------------ */

static void nfc_tag_seos_state_handler(uint8_t *data, uint16_t szBytes) {
    // Only continue if we have a full APDU ready
    if (!nfc_tag_14a_4_base_handler(&m_tcl_session_state, data, szBytes)) return;

    uint8_t *receivedCmd = m_tcl_session_state.m_apdu_buf;
    m_tcl_session_state.m_resp_len = 0;

    uint8_t apdu_status[2] = {0x6A, 0x82}; // Default: Not Found

    // Calculated block size
    const uint8_t bs = block_size(m_tag_information->encr_alg);
    const uint8_t half_bs = bs >> 1;

    NRF_LOG_DEBUG("Seos: INS %02X CLA %02X P1 %02X P2 %02X", receivedCmd[0], receivedCmd[1], receivedCmd[2], receivedCmd[3]);

    switch (receivedCmd[1]) { // APDU Class Byte
        case 0xA4: {  // SELECT FILE
            // Select File AID uses the following format for GlobalPlatform
            //
            // | 00 | A4 | 04 | 00 | xx | AID | 00 |
            // xx in this case is len of the AID value in hex

            // aid len is found as a hex value in receivedCmd[6] (Index Starts at 0)
            uint8_t aid_len = receivedCmd[4];
            uint8_t *aid = &receivedCmd[5];

            if ((aid_len == sizeof(SEOS_AID)) && (memcmp(SEOS_AID, aid, sizeof(SEOS_AID)) == 0)) { // Evaluate the AID sent by the Reader to the AID supplied
                // Format as TLV and acknowledge
                /*
                6F 0C
                    84 0A
                    A0000004400001010001
                90 00
                */

                m_tcl_session_state.m_resp_buf[0] = 0x6F; // Tag
                m_tcl_session_state.m_resp_buf[1] = aid_len + 2; // Length
                m_tcl_session_state.m_resp_buf[2] = 0x84; // Inner Tag
                m_tcl_session_state.m_resp_buf[3] = aid_len; // Inner Length
                memcpy(m_tcl_session_state.m_resp_buf + 4, aid, aid_len);
                m_tcl_session_state.m_resp_len = 4 + aid_len;

                // Set status code to Success
                apdu_status[0] = 0x90;
                apdu_status[1] = 0x00;
            } // Any other SELECT FILE command will return with a Not Found
        }
        break;

        case 0xA5: {  // SELECT OID
            // This is specific to Seos
            // Should be a TLV structure with the OID stored in tag 0x06
            uint8_t received_tlv_len = receivedCmd[4];
            uint8_t *received_tlv = &receivedCmd[5];

            bool selected_oid = false;

            // Check all requested OIDs and see if we support any
            uint8_t tlv_offset = 0;
            while (tlv_offset + 2 <= received_tlv_len) {

                uint8_t tag = received_tlv[tlv_offset++];

                uint8_t length = received_tlv[tlv_offset++];

                NRF_LOG_DEBUG("Seos: Checking OID @ off=%d, T=%02X L=%d", tlv_offset, tag, length);

                if (length > received_tlv_len - tlv_offset) {
                    break;
                }

                uint8_t *value = &received_tlv[tlv_offset];
                if (tag == 0x06) {
                    if (length == m_tag_information->oid_len && memcmp(value, m_tag_information->oid, length) == 0) {
                        selected_oid = true;
                        break;
                    }
                }
                tlv_offset += length;
            }
            NRF_LOG_DEBUG("Seos: Selected OID? %d", selected_oid);

            if (selected_oid) {
                // Synthesized IV: half a block of random data followed by half of the CMAC of that data
                memset(cryptogram_iv, 0, half_bs); // TODO: Maybe actually use random data?
                if (!generate_cmac(m_tag_information->privmac, cryptogram_iv, half_bs, work_buffer_a, m_tag_information->encr_alg)) {
                   NRF_LOG_ERROR("Seos: Select ADF failed: Failed to create IV CMAC.");
                   break;
                }
                memcpy(cryptogram_iv + half_bs, work_buffer_a, half_bs);

                // Always exactly 0x30 bytes in length
                const uint8_t reply_len = 0x30;
                uint8_t reply_idx = 0;
                uint8_t *reply = work_buffer_a;
                memset(reply, 0, reply_len);

                reply[reply_idx++] = 0x06; // Tag: selected OID
                reply[reply_idx++] = m_tag_information->oid_len;
                memcpy(reply + reply_idx, m_tag_information->oid, m_tag_information->oid_len);
                reply_idx += m_tag_information->oid_len;

                reply[reply_idx++] = 0xCF; // Tag: diversifier
                reply[reply_idx++] = m_tag_information->diversifier_len;
                memcpy(reply + reply_idx, m_tag_information->diversifier, m_tag_information->diversifier_len);
                reply_idx += m_tag_information->diversifier_len;

                uint16_t tlv_idx = 0;

                // Pre-flight: 6 fixed bytes + bs (IV) + reply_len (cryptogram) + 10 (CMAC tag+len+data)
                if (6 + bs + reply_len + 10 > NFC_14A_4_MAX_APDU) {
                    NRF_LOG_ERROR("Seos: Select ADF failed: Response too large for buffer.");
                    break;
                }

                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0xCD; // Tag: cryptography type
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x02; // Length
                m_tcl_session_state.m_resp_buf[tlv_idx++] = m_tag_information->encr_alg;
                m_tcl_session_state.m_resp_buf[tlv_idx++] = m_tag_information->hash_alg;

                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x85; // Tag: cryptogram
                m_tcl_session_state.m_resp_buf[tlv_idx++] = reply_len + bs; // Length
                memcpy(m_tcl_session_state.m_resp_buf + tlv_idx, cryptogram_iv, bs);
                tlv_idx += bs;
                
                // Generate cryptogram directly into response buffer
                if (!generate_cryptogram(m_tag_information->privenc, true, reply, reply_len, m_tcl_session_state.m_resp_buf + tlv_idx, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Select ADF failed: Failed to create reply cryptogram.");
                    break;
                }
                tlv_idx += reply_len;

                // Always an 8-byte CMAC
                const uint8_t cmac_size = 8;
                uint8_t *cmac = work_buffer_a;
                if (!generate_cmac(m_tag_information->privmac, m_tcl_session_state.m_resp_buf, tlv_idx, cmac, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Seos: Select ADF failed: Failed to create reply CMAC.");
                    break;
                }

                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x8E; // Tag: CMAC
                m_tcl_session_state.m_resp_buf[tlv_idx++] = cmac_size; // Length
                memcpy(m_tcl_session_state.m_resp_buf + tlv_idx, cmac, cmac_size);
                tlv_idx += cmac_size;

                m_tcl_session_state.m_resp_len = tlv_idx;

                // Set status code to Success
                apdu_status[0] = 0x90;
                apdu_status[1] = 0x00;
            } // No error message here because readers may request multiple OIDs before reaching ours
        }
        break;

        case 0x87: {  // MUTUAL AUTH
            // This is specific to Seos
            // Should be a TLV structure with the OID stored in tag 0x16
            uint8_t *received_tlv = &receivedCmd[5];

            if (received_tlv[0] != 0x7C) {
                NRF_LOG_ERROR("Seos: Mutual auth failed: Invalid tag, expected 7C, got %02X", received_tlv[0]);
                break;
            }

            received_tlv += 2;

            if (received_tlv[0] == 0x81) {
                // Request for RND.ICC
                uint8_t tlv_idx = 0;

                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x7C; // Tag: mutual auth
                m_tcl_session_state.m_resp_buf[tlv_idx++] = sizeof(RND_ICC) + 2; // Length
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x81; // Tag: request for RND.ICC
                m_tcl_session_state.m_resp_buf[tlv_idx++] = sizeof(RND_ICC); // Length
                memcpy(m_tcl_session_state.m_resp_buf + tlv_idx, RND_ICC, sizeof(RND_ICC));
                tlv_idx += sizeof(RND_ICC);

                m_tcl_session_state.m_resp_len = tlv_idx;

                // Set status code to Success
                apdu_status[0] = 0x90;
                apdu_status[1] = 0x00;
            } else if (received_tlv[0] == 0x82) {
                // Request for challenge
                uint8_t received_tlv_len = received_tlv[1];
                received_tlv += 2;

                if (received_tlv_len > WORK_BUFFER_SIZE) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Recieved cryptogram too long.");
                    break;
                }

                if (received_tlv_len < 32) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Recieved cryptogram too short.");
                    break;
                }

                uint8_t keyslot = receivedCmd[3]; // APDU P2 byte

                seos_kdf(true, m_tag_information->authkey, keyslot, work_buffer_a, m_tag_information->oid, m_tag_information->oid_len, m_tag_information->diversifier, m_tag_information->diversifier_len, diver_encr_key, m_tag_information->encr_alg, m_tag_information->hash_alg);
                seos_kdf(false, m_tag_information->authkey, keyslot, work_buffer_a, m_tag_information->oid, m_tag_information->oid_len, m_tag_information->diversifier, m_tag_information->diversifier_len, diver_cmac_key, m_tag_information->encr_alg, m_tag_information->hash_alg);

                // Verify CMAC (last 8 bytes)
                uint8_t request_len = received_tlv_len - 8;
                uint8_t *cmac = work_buffer_a;
                if (!generate_cmac(diver_cmac_key, received_tlv, request_len, cmac, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Failed to create CMAC.");
                    break;
                }
                if (memcmp(cmac, received_tlv + request_len, 8) != 0) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Invalid CMAC.");
                    break;
                }

                memset(work_buffer_a, 0xDD, WORK_BUFFER_SIZE);

                uint8_t *request = work_buffer_a;
                if (!decrypt_cryptogram(diver_encr_key, received_tlv, request_len, request, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Failed to decrypt cryptogram.");
                    break;
                }

                // request = RND.IFD | RND.ICC | Key.IFD
                if (memcmp(RND_ICC, request + 8, 8) != 0) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Incorrect RND.ICC.");
                    break;
                }
                memcpy(RND_IFD, request, 8);
                memcpy(KEY_IFD, request + 16, 16);

                // reply = RND_ICC | RND_IFD | KEY_ICC
                const uint8_t reply_plain_len = 32;
                uint8_t *reply_plain = work_buffer_a;
                memcpy(reply_plain + 0, RND_ICC, 8);
                memcpy(reply_plain + 8, RND_IFD, 8);
                memcpy(reply_plain + 16, KEY_ICC, 16);

                // Generate cryptogram + 8-byte CMAC
                const uint8_t reply_len = reply_plain_len + 8;
                uint8_t *reply = work_buffer_b;
                generate_cryptogram(diver_encr_key, false, reply_plain, reply_plain_len, reply, m_tag_information->encr_alg);
                if (!generate_cmac(diver_cmac_key, reply, reply_plain_len, reply + reply_plain_len, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Seos: Mutual auth failed: Failed to create reply CMAC.");
                    break;
                }

                uint8_t tlv_idx = 0;

                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x7C; // Tag: mutual auth
                m_tcl_session_state.m_resp_buf[tlv_idx++] = reply_len + 2; // Length
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x82; // Tag: request for challenge
                m_tcl_session_state.m_resp_buf[tlv_idx++] = reply_len; // Length
                memcpy(m_tcl_session_state.m_resp_buf + tlv_idx, reply, reply_len);
                tlv_idx += reply_len;

                m_tcl_session_state.m_resp_len = tlv_idx;

                // Set status code to Success
                apdu_status[0] = 0x90;
                apdu_status[1] = 0x00;

                // IMPORTANT: before sending reply, calculate final diversified keys

                uint8_t *hash_input = work_buffer_a;
                uint8_t hash_idx = 0;
                // Counter
                hash_input[hash_idx++] = 0x00;
                hash_input[hash_idx++] = 0x00;
                hash_input[hash_idx++] = 0x00;
                hash_input[hash_idx++] = 0x01;
                // Only copy first 8 bytes of each KEY
                memcpy(hash_input + hash_idx, KEY_IFD, 8);
                hash_idx += 8;
                memcpy(hash_input + hash_idx, KEY_ICC, 8);
                hash_idx += 8;
                // Yes, this is supposed to be the same thing twice
                hash_input[hash_idx++] = m_tag_information->encr_alg;
                hash_input[hash_idx++] = m_tag_information->encr_alg;
                // Copy full RND values
                memcpy(hash_input + hash_idx, RND_ICC, 8);
                hash_idx += 8;
                memcpy(hash_input + hash_idx, RND_IFD, 8);
                hash_idx += 8;

                uint8_t *hash_output = work_buffer_b;
                if (m_tag_information->hash_alg == SEOS_HASHING_SHA256) {
                    size_t hash_size = 32;
                    ret_code_t result = nrf_crypto_hash_calculate(&nrf_hash_ctx,
                        &g_nrf_crypto_hash_sha256_info,
                        hash_input, hash_idx, hash_output, &hash_size
                    );

                    if (result != NRF_SUCCESS) {
                        NRF_LOG_ERROR("Seos: Error generating SHA256 hash: %s", nrf_crypto_error_string_get(result));
                        break;
                    } else if (hash_size != 32) {
                        NRF_LOG_ERROR("Seos: Incorrect SHA256 output size: %d", hash_size);
                        break;
                    }
                } else {
                    NRF_LOG_ERROR("Seos: Unknown Hashing Algorithm");
                    break;
                }

                memcpy(diver_encr_key, hash_output, 16);
                memcpy(diver_cmac_key, hash_output + 16, 16);
            } else {
                NRF_LOG_ERROR("Seos: Mutual auth failed: Incorrect tag %02X found.", received_tlv[0]);
            }
        }
        break;

        case 0xDA:   // PUT DATA
        case 0xCB: { // GET DATA
            bool is_put = receivedCmd[1] == 0xDA;

            uint8_t received_tlv_len = receivedCmd[4];
            uint8_t *received_tlv = &receivedCmd[5];

            uint8_t *cryptogram = NULL;
            uint8_t *recvd_cmac = NULL;
            uint8_t cryptogram_length = 0;
            uint8_t recvd_cmac_length = 0;
            uint8_t recvd_cmac_offset = 0;

            // Extract cryptogram and CMAC
            uint8_t tlv_offset = 0;
            while (tlv_offset + 2 <= received_tlv_len) {

                uint8_t tag_offset = tlv_offset;
                uint8_t tag = received_tlv[tlv_offset++];

                uint8_t length = received_tlv[tlv_offset++];
                if ((length & 0x80) == 0x80) {
                    // Multi-byte length
                    length &= 0x7F;
                    if (length != 1) {
                        NRF_LOG_ERROR("Seos: TLV element too long");
                        break;
                    }

                    // Grab that one extra byte
                    length = received_tlv[tlv_offset++];
                }

                if (length > received_tlv_len - tlv_offset) {
                    break;
                }

                uint8_t *value = &received_tlv[tlv_offset];

                if (tag == 0x85) {
                    cryptogram = value;
                    cryptogram_length = length;
                } else if (tag == 0x8e) {
                    recvd_cmac = value;
                    recvd_cmac_length = length;
                    recvd_cmac_offset = tag_offset;
                }

                tlv_offset += length;
            }

            if (cryptogram != NULL && recvd_cmac != NULL) {
                if (cryptogram_length > WORK_BUFFER_SIZE) {
                    NRF_LOG_ERROR("Seos: Get Data failed: Recieved cryptogram too long.");
                    break;
                }

                // Combine the first half_bs each of RND_ICC and RND_IFD,
                //  then increment as a single counter
                uint8_t rndCounter[bs];
                memcpy(rndCounter, RND_ICC, half_bs);
                memcpy(rndCounter + half_bs, RND_IFD, half_bs);

                // skip zero bytes
                for (int8_t i = bs - 1; i >= 0; i--) {
                    rndCounter[i]++;

                    if (rndCounter[i]) {
                        break;
                    }
                }

                uint8_t *mac_input = work_buffer_a;
                uint16_t mac_input_idx = 0;

                // Add RND_* counter to mac_input
                memcpy(mac_input + mac_input_idx, rndCounter, bs);
                mac_input_idx += bs;

                // Add padded APDU header to mac_input
                uint8_t *padded_apdu_header = mac_input + mac_input_idx;
                memset(padded_apdu_header, 0, bs);
                memcpy(padded_apdu_header, receivedCmd, 4);
                padded_apdu_header[4] = 0x80;
                mac_input_idx += bs;

                // Add received TLV data to mac_input
                if (mac_input_idx + recvd_cmac_offset + bs > WORK_BUFFER_SIZE) {
                    NRF_LOG_ERROR("Seos: Get Data failed: CMAC input too large.");
                    break;
                }
                memcpy(mac_input + mac_input_idx, received_tlv, recvd_cmac_offset);
                mac_input_idx += recvd_cmac_offset;

                // Add padding (if needed) to mac_input
                if (mac_input_idx % bs) {
                    memset(mac_input + mac_input_idx, 0, bs - (mac_input_idx % bs));
                    mac_input[mac_input_idx] = 0x80;
                    mac_input_idx += bs - (mac_input_idx % bs);
                }

                uint8_t *cmac = work_buffer_b;
                if (!generate_cmac(diver_cmac_key, mac_input, mac_input_idx, cmac, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Seos: Get Data failed: Failed to create CMAC.");
                    break;
                }
                if (memcmp(cmac, recvd_cmac, recvd_cmac_length) != 0) {
                    NRF_LOG_ERROR("Seos: Get Data failed: Invalid CMAC.");
                    break;
                }

                uint8_t *request = work_buffer_a;
                decrypt_cryptogram(diver_encr_key, cryptogram, cryptogram_length, request, m_tag_information->encr_alg);

                uint16_t tlv_idx = 0;

                if (is_put) {
                    NRF_LOG_ERROR("Seos: Put Data failed: Not implemented");
                    break;
                } else {
                    //5c 02 ff 00
                    if (request[0] != 0x5C) {
                        NRF_LOG_ERROR("Seos: Get Data failed: Invalid request TLV. Expected tag 5C, but got %02X.", request[0]);
                        break;
                    }

                    if (request[1] != m_tag_information->data_tag_len || memcmp(request + 2, m_tag_information->data_tag, m_tag_information->data_tag_len) != 0) {
                        NRF_LOG_ERROR("Seos: Get Data failed: Requested invalid data tag.");
                        break;
                    }

                    uint8_t reply_len = m_tag_information->data_tag_len + 1 + m_tag_information->data_len;
                    reply_len = round_to_next(reply_len, bs);
                    if (reply_len > WORK_BUFFER_SIZE) {
                        NRF_LOG_ERROR("Seos: Get Data failed: Unable to generate reply: too long.");
                        break;
                    }

                    uint8_t *reply = work_buffer_a;

                    uint8_t reply_idx = 0;
                    memcpy(reply + reply_idx, m_tag_information->data_tag, m_tag_information->data_tag_len); // Tag
                    reply_idx += m_tag_information->data_tag_len;
                    reply[reply_idx++] = m_tag_information->data_len; // Length
                    memcpy(reply + reply_idx, m_tag_information->data, m_tag_information->data_len); // Value
                    reply_idx += m_tag_information->data_len;

                    if (reply_idx != reply_len) {
                        memset(reply + reply_idx, 0, reply_len - reply_idx);
                        // Add 0x80 at first byte after data for start of padding
                        reply[reply_idx] = 0x80;
                    }

                    uint8_t *reply_cryptogram = work_buffer_b;
                    if (!generate_cryptogram(diver_encr_key, false, reply, reply_len, reply_cryptogram, m_tag_information->encr_alg)) {
                        NRF_LOG_ERROR("Seos: Get Data failed: Failed to create reply cryptogram.");
                        break;
                    }

                    // Pre-flight: 2 (cryptogram tag+len) + reply_len + 4 (status) + 2 (CMAC tag+len) + recvd_cmac_length
                    if (2 + reply_len + 4 + 2 + recvd_cmac_length > NFC_14A_4_MAX_APDU) {
                        NRF_LOG_ERROR("Seos: Get Data failed: Response too large for buffer.");
                        break;
                    }

                    // Only include a cryptogram for GET DATA
                    m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x85; // Tag: cryptogram
                    m_tcl_session_state.m_resp_buf[tlv_idx++] = reply_len; // Length
                    memcpy(m_tcl_session_state.m_resp_buf + tlv_idx, reply_cryptogram, reply_len);
                    tlv_idx += reply_len;
                }

                // Whether we GET DATA or PUT DATA, add the response status code and CMAC
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x99; // Tag: status code
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x02; // Length
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x90;
                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x00;

                // Unlike every other CMAC, this time we need to prepend
                //  the same counter from above, but increment it again
                for (int8_t i = bs - 1; i >= 0; i--) {
                    rndCounter[i]++;
                    if (rndCounter[i] != 0x00) break;
                }

                mac_input_idx = 0;

                memcpy(mac_input + mac_input_idx, rndCounter, bs);
                mac_input_idx += bs;
                if (mac_input_idx + tlv_idx + bs > WORK_BUFFER_SIZE) {
                    NRF_LOG_ERROR("Seos: Get Data failed: Reply CMAC input too large.");
                    break;
                }
                memcpy(mac_input + mac_input_idx, m_tcl_session_state.m_resp_buf, tlv_idx);
                mac_input_idx += tlv_idx;

                // Add padding (if needed) to mac_input
                if (mac_input_idx % bs) {
                    memset(mac_input + mac_input_idx, 0, bs - (mac_input_idx % bs));
                    mac_input[mac_input_idx] = 0x80;
                    mac_input_idx += bs - (mac_input_idx % bs);
                }

                uint8_t cmac_size = recvd_cmac_length;
                if (cmac_size > 16) {
                    NRF_LOG_ERROR("Seos: Get Data failed: CMAC size invalid.");
                    break;
                }

                if (!generate_cmac(diver_cmac_key, mac_input, mac_input_idx, cmac, m_tag_information->encr_alg)) {
                    NRF_LOG_ERROR("Seos: Get Data failed: Failed to create reply CMAC.");
                    break;
                }

                m_tcl_session_state.m_resp_buf[tlv_idx++] = 0x8E; // Tag: CMAC
                m_tcl_session_state.m_resp_buf[tlv_idx++] = cmac_size; // Length
                memcpy(m_tcl_session_state.m_resp_buf + tlv_idx, cmac, cmac_size);
                tlv_idx += cmac_size;

                m_tcl_session_state.m_resp_len = tlv_idx;

                // Set status code to Success
                apdu_status[0] = 0x90;
                apdu_status[1] = 0x00;
            } else {
                NRF_LOG_ERROR("Seos: Get Data failed: No cryptogram or CMAC found in request.");
            }
        }
        break;
    }

    NRF_LOG_DEBUG("Seos: Responding with %d bytes, SW=%02X%02X.", m_tcl_session_state.m_resp_len, apdu_status[0], apdu_status[1]);
    NRF_LOG_HEXDUMP_DEBUG(m_tcl_session_state.m_resp_buf, m_tcl_session_state.m_resp_len);

    // Add APDU status code to end of response
    m_tcl_session_state.m_resp_buf[m_tcl_session_state.m_resp_len++] = apdu_status[0];
    m_tcl_session_state.m_resp_buf[m_tcl_session_state.m_resp_len++] = apdu_status[1];
    m_tcl_session_state.m_response_ready = true;

    // Increase maximum frame delay
    // Without this, if the command took too long,
    //  the nRF firmware will just... never respond
    NRF_NFCT->FRAMEDELAYMAX = 135600; // 10ms

    nfc_tag_14a_4_base_respond(&m_tcl_session_state);
}

/* ------------------------------------------------------------------ */
/*  Reset handler                                                       */
/* ------------------------------------------------------------------ */

void nfc_tag_seos_reset_handler(void) {
    nfc_tag_14a_4_reset_state(&m_tcl_session_state);
}

/* ------------------------------------------------------------------ */
/*  Data load / save / factory callbacks                               */
/* ------------------------------------------------------------------ */

int nfc_tag_seos_data_loadcb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    int info_size = sizeof(nfc_tag_seos_information_t);
    if (buffer->length < info_size) {
        NRF_LOG_ERROR("SEOS loadcb: buffer too small (%d < %d)",
                      buffer->length, info_size);
        return info_size;
    }
    m_tag_information = (nfc_tag_seos_information_t *)buffer->buffer;

    nfc_tag_14a_handler_t handler = {
        .get_coll_res = nfc_tag_seos_get_coll_res,
        .cb_state     = nfc_tag_seos_state_handler,
        .cb_reset     = nfc_tag_seos_reset_handler,
    };
    nfc_tag_14a_set_handler(&handler);
    NRF_LOG_INFO("SEOS loadcb OK: SAK=%02x uid_sz=%d",
                 m_tag_information->res_coll.sak[0],
                 m_tag_information->res_coll.size);
    return info_size;
}

int nfc_tag_seos_data_savecb(tag_specific_type_t type, tag_data_buffer_t *buffer) {
    return sizeof(nfc_tag_seos_information_t);
}

bool nfc_tag_seos_data_factory(uint8_t slot, tag_specific_type_t tag_type) {
    if (tag_type != TAG_TYPE_SEOS) return false;

    /* Build factory defaults on stack and write directly to FDS
     * (same pattern as nfc_tag_mf1_data_factory). */
    nfc_tag_seos_information_t info;
    memset(&info, 0, sizeof(info));

    /* Placeholder 4-byte NXP-style UID */
    info.res_coll.size    = NFC_TAG_14A_UID_SINGLE_SIZE;
    info.res_coll.atqa[0] = 0x01;
    info.res_coll.atqa[1] = 0x00;
    info.res_coll.sak[0]  = 0x20;   /* ISO14443-4 */
    info.res_coll.uid[0]  = 0x08;
    info.res_coll.uid[1]  = 0x01;
    info.res_coll.uid[2]  = 0x02;
    info.res_coll.uid[3]  = 0x03;

    static const uint8_t default_ats[] = {
        0x05, 0x78, 0x77, 0x80, 0x02
    };
    info.res_coll.ats.length = sizeof(default_ats);
    memcpy(info.res_coll.ats.data, default_ats, sizeof(default_ats));

    // Load default values
    const uint8_t DEFAULT_DATA[] = {0x05, 0x00};
    const uint8_t DEFAULT_OID[] = {0x03, 0x01, 0x07, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
    const uint8_t DEFAULT_TAG[] = {0xFF, 0x00};
    const uint8_t DEFAULT_DIVERSIFIER[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    info.data_len = sizeof(DEFAULT_DATA);
    memcpy(info.data, DEFAULT_DATA, info.data_len);

    info.oid_len = sizeof(DEFAULT_OID);
    memcpy(info.oid, DEFAULT_OID, info.oid_len);

    info.data_tag_len = sizeof(DEFAULT_TAG);
    memcpy(info.data_tag, DEFAULT_TAG, info.data_tag_len);

    info.diversifier_len = sizeof(DEFAULT_DIVERSIFIER);
    memcpy(info.diversifier, DEFAULT_DIVERSIFIER, info.diversifier_len);

    memset(info.authkey, 0, 16);
    memset(info.privenc, 0, 16);
    memset(info.privmac, 0, 16);

    info.encr_alg = SEOS_ENCRYPTION_AES;
    info.hash_alg = SEOS_HASHING_SHA256;

    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, TAG_SENSE_HF, &map_info);
    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(info), &info);
    NRF_LOG_INFO("SEOS factory slot %d: %s", slot, ret ? "OK" : "FAIL");
    return ret;
}
