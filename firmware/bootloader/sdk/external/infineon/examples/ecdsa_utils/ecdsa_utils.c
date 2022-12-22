/**
* MIT License
*
* Copyright (c) 2019 Infineon Technologies AG
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE
*
*
* \file ecdsa_utils.c
*
* \brief   This file provides functions to convert raw r and s components of the ECDSA signature to asn1 encoding
*
*
* \addtogroup grOptigaUtil
* @{
*/

#include "ecdsa_utils.h"

#include <string.h>

// This implementation only supports a single byte LENGTH field. The maximum
// possible value than can be encoded within a single byte is 0x7F (127 dec).
// For higher values, the length must be coded in a multi-byte field.
#define DER_INTEGER_MAX_LEN 0x7F

// This implementation only supports a single byte LENGTH field. The maximum
// possible value than can be encoded within a single byte is 0x7F (127 dec).
// For higher values, the length must be coded in a multi-byte field.
#define DER_SEQUENCE_MAX_LEN 0x7F

// ASN.1 DER TAG field offset
#define ASN1_DER_TAG_OFFSET 0

// ASN.1 DER LENGTH field offset
#define ASN1_DER_LEN_OFFSET 1

// ASN.1 DER VALUE field offset
// Only for this implementation!
#define ASN1_DER_VAL_OFFSET 2

// ASN.1 DER Tag for INTEGER
#define DER_TAG_INTEGER 0x02

// ASN.1 DER Tag for SEQUENCE
#define DER_TAG_SEQUENCE 0x30

#define DER_UINT_MASK 0x80

/**
 * @brief Encodes a byte buffer as unsigned ASN.1 DER INTEGER
 *
 * @param  data[in]            Buffer containing the bytes to be encoded
 * @param  data_len[in]        Length of the data buffer
 * @param  out_buf[out]        Output buffer for the encoded ASN.1 bytes
 * @param  out_buf_len[in]     Size of the out_buf buffer
 * @return The number of bytes of the ASN.1 encoded stream on success, 0 on error
 * @note   The parameters to this function must not be NULL.
 */
static size_t encode_der_integer(const uint8_t* data, size_t data_len,
                                 uint8_t* out_buf, size_t out_buf_len)
{
    // all write access must be smaller or equal to this pointer
    const uint8_t* const out_end = out_buf + out_buf_len - 1;

    // fixed position fields
    uint8_t* const tag_field = &out_buf[ASN1_DER_TAG_OFFSET];
    uint8_t* const length_field = &out_buf[ASN1_DER_LEN_OFFSET];
    uint8_t* const integer_field_start = &out_buf[ASN1_DER_VAL_OFFSET];

    // write pointer
    uint8_t* integer_field_cur = integer_field_start;

    // search for beginning of integer
    const uint8_t* cur_data = data;
    const uint8_t* const data_end = data + data_len;

    // check if something to encode, else next loop condition overflows
    if (data_len == 0) {
        return 0;
    }

    // don't check the last byte, it will always be a data byte
    for(; cur_data < (data_end - 1); cur_data++) {
        if (*cur_data != 0x00) {
            break;
        }
    }

    // check if stuffing byte needed
    if (*cur_data & DER_UINT_MASK) {
        integer_field_cur++;
    }

    // calculate number of bytes left in data
    const size_t write_length = data_end - cur_data;
    // check if it fits in the output buffer
    if ((integer_field_cur + write_length - 1) > out_end) {
        // Prevented out-of-bounds write
        return 0;
    }

    // ensure we can encode the length
    const size_t integer_len = (integer_field_cur + write_length) - integer_field_start;
    if (integer_len > DER_INTEGER_MAX_LEN) {
        // This implementation support single-byte LENGTH fields only
        return 0;
    }

    // commit writes
    memcpy(integer_field_cur, cur_data, write_length);
    *tag_field = DER_TAG_INTEGER;
    *length_field = integer_len;
    // check if we have a stuffing byte, and explicitly zero it
    if (integer_field_cur != integer_field_start) {
        *integer_field_start = 0x00;
    }

    return integer_len + ASN1_DER_VAL_OFFSET;
}

bool ecdsa_rs_to_asn1_integers(const uint8_t* r, const uint8_t* s, size_t rs_len,
                               uint8_t* asn_sig, size_t* asn_sig_len)
{
    if (r == NULL || s == NULL || asn_sig == NULL || asn_sig_len == NULL) {
        // No NULL paramters allowed
        return false;
    }

    // encode R component
    const size_t out_len_r = encode_der_integer(r, rs_len, asn_sig, *asn_sig_len);
    if (out_len_r == 0) {
        // error while encoding R as DER INTEGER
        return false;
    }

    uint8_t* const s_start = asn_sig + out_len_r;
    const size_t s_len = *asn_sig_len - out_len_r;

    // encode S component
    const size_t out_len_s = encode_der_integer(s, rs_len, s_start, s_len);
    if (out_len_s == 0) {
        // error while encoding S as DER INTEGER
        return false;
    }

    *asn_sig_len = out_len_r + out_len_s;

    return true;
}

bool ecdsa_rs_to_asn1_signature(const uint8_t* r, const uint8_t* s, size_t rs_len,
                                uint8_t* asn_sig, size_t* asn_sig_len)
{
    if (r == NULL || s == NULL || asn_sig == NULL || asn_sig_len == NULL) {
        // No NULL paramters allowed
        return false;
    }

    if (*asn_sig_len < ASN1_DER_VAL_OFFSET) {
        // Not enough space, can't encode anything
        return false;
    }

     // fixed position fields
    uint8_t* const tag_field = &asn_sig[ASN1_DER_TAG_OFFSET];
    uint8_t* const length_field = &asn_sig[ASN1_DER_LEN_OFFSET];
    uint8_t* const value_field_start = &asn_sig[ASN1_DER_VAL_OFFSET];

    // compute size left after SEQUENCE header TAG and LENGTH fields
    size_t integers_len = *asn_sig_len - ASN1_DER_VAL_OFFSET;

    if (!ecdsa_rs_to_asn1_integers(r, s, rs_len, value_field_start, &integers_len)) {
        // Failed to encode R and S as INTEGERs
        return false;
    }

    if (integers_len > DER_SEQUENCE_MAX_LEN) {
        // This implementation support single-byte LENGTH fields only
        return false;
    }

    // write SEQUENCE header
    *tag_field = DER_TAG_SEQUENCE;
    *length_field = integers_len;
    *asn_sig_len = integers_len + ASN1_DER_VAL_OFFSET;

    return true;
}

/**
 * @brief Decodes an ASN.1 encoded integer to a byte buffer
 *
 * @param  asn1[in]            Buffer containing the ASN.1 encoded data
 * @param  asn1_len[in]        Length of the asn1 buffer
 * @param  out_int[out]        Output buffer for the decoded integer bytes
 * @param  out_int_len[in,out] Size of the out_int buffer, contains the number of written bytes afterwards
 * @return The number of bytes advanced in the ASN.1 stream on success, 0 on failure
 * @note   The parameters to this function must not be NULL.
 */
static size_t decode_asn1_uint(const uint8_t* asn1, size_t asn1_len,
                               uint8_t* out_int, size_t* out_int_len)
{
    if (asn1_len < (ASN1_DER_VAL_OFFSET + 1)) {
        // Not enough data to decode anything
        return 0;
    }

    // all read access must be before this pointer
    const uint8_t* const asn1_end = asn1 + asn1_len;

    // fixed position fields
    const uint8_t* const tag_field = &asn1[ASN1_DER_TAG_OFFSET];
    const uint8_t* const length_field = &asn1[ASN1_DER_LEN_OFFSET];

    if (*tag_field != DER_TAG_INTEGER) {
        // Not an DER INTEGER
        return 0;
    }

    if (*length_field == 0 || *length_field > DER_INTEGER_MAX_LEN) {
        // Invalid length value
        return  0;
    }

    uint8_t integer_length = *length_field;
    const uint8_t* integer_field_cur = &asn1[ASN1_DER_VAL_OFFSET];

    if ((integer_field_cur + integer_length - 1) > (asn1_end - 1)) {
        // prevented out-of-bounds read
        return 0;
    }

    // one byte can never be a stuffing byte
    if (integer_length > 1) {
        if (*integer_field_cur == 0x00) {
            // remove stuffing byte
            integer_length--;
            integer_field_cur++;
        }

        if (*integer_field_cur == 0x00) {
            // second zero byte is an encoding error
            return 0;
        }
    }

    if (integer_length > *out_int_len) {
        // prevented out-of-bounds write
        return 0;
    }

    // insert padding zeros to ensure position of least significant byte matches
    const size_t padding = *out_int_len - integer_length;
    memset(out_int, 0, padding);

    memcpy(out_int + padding, integer_field_cur, integer_length);
    *out_int_len = integer_length;

    // return number of consumed ASN.1 bytes
    return integer_field_cur + integer_length - tag_field;
}

bool asn1_to_ecdsa_rs_sep(const uint8_t* asn1, size_t asn1_len,
                      uint8_t* r, size_t* r_len,
                      uint8_t* s, size_t* s_len)
{
    if (asn1 == NULL || r == NULL || r_len == NULL || s == NULL || s_len == NULL) {
        // No NULL paramters allowed
        return false;
    }

    // decode R component
    const size_t consumed_r = decode_asn1_uint(asn1, asn1_len, r, r_len);
    if (consumed_r == 0) {
        // error while decoding R component
        return false;
    }

    const uint8_t* const asn1_s = asn1 + consumed_r;
    const size_t asn1_s_len = asn1_len - consumed_r;

    // decode S component
    const size_t consumed_s = decode_asn1_uint(asn1_s, asn1_s_len, s, s_len);
    if (consumed_s == 0) {
        // error while decoding R component
        return false;
    }

    return true;
}

bool asn1_to_ecdsa_rs(const uint8_t* asn1, size_t asn1_len,
                      uint8_t* rs, size_t rs_len)
{
    if (asn1 == NULL || rs == NULL || rs_len == 0) {
        // No NULL paramters allowed
        return false;
    }

    if ((rs_len % 2) != 0) {
        // length of the output buffer must be 2 times the component size and even
        return false;
    }

    const size_t component_length = rs_len / 2;
    size_t r_len = component_length;
    size_t s_len = component_length;

    return asn1_to_ecdsa_rs_sep(asn1, asn1_len, rs, &r_len, rs + component_length, &s_len);
}
