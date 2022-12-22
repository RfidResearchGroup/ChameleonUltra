/**
* MIT License
*
* Copyright (c) 2018 Infineon Technologies AG
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
* \file ecdsa_utils.h
*
* \brief   This file contains utilities for ECDSA signature encoding/decoding
*
*
* \addtogroup grOptigaUtil
* @{
*/
#ifndef _H_ECDSA_UTILS_H_
#define _H_ECDSA_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Encode two integers in DER format
// TAG + LENGTH needs 2 bytes for this implementation.
// If the highest bit of the integer is set we need an extra byte.
/** @brief Maximum overhead of the ASN.1 encoding for the R and S components as concatenated DER INTEGERs */
#define ECDSA_RS_MAX_ASN1_OVERHEAD ((2 + 1) * 2)

// Encode two integers as SEQUENCE of INTEGERs in DER format
// TAG + LENGTH fields need 2 bytes and if the highest bit of the integer is set,
// we need an extra byte, so we have 3 bytes overhead per INTEGER.
// SEQUENCE encoding needs an addtional 2 bytes overhead, for TAG + LENGTH fields.
// By limiting the LENGTH field to one byte, we are limited to 127 bytes for the two DER INTEGERS,
// this allows a maximum of 60 bytes per R and S component which is sufficient for the
// NIST P-256 and NIST P-384 curves we deal with.
/** @brief Maximum overhead of the ASN.1 encoding for the R and S components as SEQUENCE of INTEGERs */
#define ECDSA_SIGNATURE_MAX_ASN1_OVERHEAD (ECDSA_RS_MAX_ASN1_OVERHEAD + 2)

/**
 * @brief Encodes the ECDSA signature components (r, s) as two concatenated ASN.1 INTEGERs. This is the format
 *         the OPTIGA hostcode expects.
 *
 * @param[in]   r            Component R of the ECDSA signature
 * @param[in]   s            Component S of the ECDSA signature
 * @param[in]   rs_len       Length of each buffer for the R and S components of the ECDSA signature, must be smaller than 127
 * @param[out]  asn_sig      Buffer where the ASN.1 encoded result will be stored
 * @param[out]  asn_sig_len  Length of the ASN.1 encoded data that was copied into the output buffer
 * @returns     true on success, false on error
 * @note        The output buffer must be at least 2*rs_len + ECDSA_SIGNATURE_MAX_ASN1_OVERHEAD to fit the result in all cases.
 * @note        If the function returns false, all output values are invalid.
 */
bool ecdsa_rs_to_asn1_integers(const uint8_t* r, const uint8_t* s, size_t rs_len,
                               uint8_t* asn_sig, size_t* asn_sig_len);
/**
 * @brief Encodes the ECDSA signature components (r, s) as ASN.1 SEQUENCE of two INTEGERs. This is the format
 *        mbedTLS and OpenSSL use.
 *
 * @param[in]   r            Component R of the ECDSA signature
 * @param[in]   s            Component S of the ECDSA signature
 * @param[in]   rs_len       Length of each buffer for the R and S components of the ECDSA signature, must be smaller than 127
 * @param[out]  asn_sig      Buffer where the ASN.1-encoded ECDSA signature will be stored
 * @param[out]  asn_sig_len  Length of the actual data that was copied into the output buffer
 * @returns     true on success, false on error
 * @note        The output buffer must be at least 2*rs_len + ECDSA_RS_MAX_ASN1_OVERHEAD to fit the result in all cases
 * @note        If the function returns false, all output values are invalid.
 */
bool ecdsa_rs_to_asn1_signature(const uint8_t* r, const uint8_t* s, size_t rs_len,
                                uint8_t* asn_sig, size_t* asn_sig_len);
/**
 * @brief decodes two concatenated ASN.1 integers to the R and S components of an ECDSA signature
 * @param[in]   asn1        Buffer containing the ASN.1 encoded R and S values as two concatenated DER INTEGERs
 * @param[in]   asn1_len    Length of the asn1 buffer
 * @param[out]  rs          Output buffer for the concatenated R and S values
 * @param[in]   rs_len      Length of the rs buffer
 * @returns     true on success, false else
 * @note        The R and S components will be padded with zeros in the output buffer and each component will take rs_len/2 bytes.
 *              e.g.: [ (0x00) R | S ], where '|' denotes the border for half the rs buffer,
 *              'R' and 'S' the bytes of the R and S components and '(0x00)' one or multiple padding bytes
 *              needed to completely fill the buffer.
 *              If you need to know the exact length of R and S use asn1_to_ecdsa_rs_sep(...)
 * @note        If the function returns false, all output values are invalid.
 */
bool asn1_to_ecdsa_rs(const uint8_t* asn1, size_t asn1_len,
                      uint8_t* rs, size_t rs_len);

/**
 * @brief decodes two concatenated ASN.1 integers to the R and S components of an ECDSA signature with separate buffers for R and S
 * @param[in]     asn1      Buffer containing the ASN.1 encoded R and S values as two concatenated DER INTEGERs
 * @param[in]     asn1_len  Length of the asn1 buffer
 * @param[out]    r         Output buffer for the R value
 * @param[in,out] r_len     Length of the r buffer, contains the number of non padding bytes afterwards
 * @param[out]    s         Output buffer for the S value
 * @param[in,out] s_len     Length of the s buffer, contains the number of non padding bytes afterwards
 * @returns       true on success, false else
 * @note          The r and s buffers will be padded at the least significant byte with zeros to the length of the buffer.
 * @note          If the function returns false, all output values are invalid.
 */
bool asn1_to_ecdsa_rs_sep(const uint8_t* asn1, size_t asn1_len,
                          uint8_t* r, size_t* r_len,
                          uint8_t* s, size_t* s_len);

#ifdef __cplusplus
}
#endif

#endif // _H_ECDSA_UTILS_H_

/**
* @}
*/
