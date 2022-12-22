/**
 * NOTICE
 * 
 * Copyright 2020 Tile Inc.  All Rights Reserved.
 * All code or other information included in the accompanying files ("Tile Source Material")
 * is PROPRIETARY information of Tile Inc. ("Tile") and access and use of the Tile Source Material
 * is subject to these terms. The Tile Source Material may only be used for demonstration purposes,
 * and may not be otherwise distributed or made available to others, including for commercial purposes.
 * Without limiting the foregoing , you understand and agree that no production use
 * of the Tile Source Material is allowed without a Tile ID properly obtained under a separate
 * agreement with Tile.
 * You also understand and agree that Tile may terminate the limited rights granted under these terms
 * at any time in its discretion.
 * All Tile Source Material is provided AS-IS without warranty of any kind.
 * Tile does not warrant that the Tile Source Material will be error-free or fit for your purposes.
 * Tile will not be liable for any damages resulting from your use of or inability to use
 * the Tile Source Material.
 *
 * Support: firmware_support@tile.com
 */

/** @file tile_assert.h
 ** @brief Define a standard Tile assert interface
 */

#ifndef TILE_ASSERT_H_
#define TILE_ASSERT_H_

#include <stdint.h>
#include <stdbool.h>

void tile_assert(bool cond, uint32_t line, const char file[], const char func[], bool ignore);

#define TILE_ASSERT(cond)         tile_assert(cond, __LINE__, __FILE__, __func__, false)
#define TILE_ASSERT_IGNORE(cond)  tile_assert(cond, __LINE__, __FILE__, __func__, true)

#endif
