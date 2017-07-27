/*
 * Copyright 2017 Rockchip Electronics Co., Ltd
 *     Author: James Lin<james.lin@rock-chips.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */


#ifndef AVCODEC_PUT_BITS64_H
#define AVCODEC_PUT_BITS64_H

#include <stdint.h>
#include <stddef.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"

typedef struct PutBitContext64 {
    uint32_t          buflen;         //!< max buf length, 64bit uint
    uint32_t          index;           //!< current uint position
    uint64_t          *pbuf;          //!< outpacket data
    uint64_t          bvalue;         //!< buffer value, 64 bit
    uint8_t           bitpos;         //!< bit pos in 64bit
    uint32_t          size;           //!< data size,except header
} PutBitContext64;

static inline int init_put_bits_a64(PutBitContext64 *bp, uint64_t *data, uint32_t len)
{
    memset(bp, 0, sizeof(PutBitContext64));
    bp->index  = 0;
    bp->bitpos = 0;
    bp->bvalue = 0;
    bp->size   = len;
    bp->buflen = len;  // align 64bit
    bp->pbuf   = data;
    return 0;
}

static inline void put_bits_a64(PutBitContext64 *bp, int lbits, uint64_t invalue)
{
    uint8_t hbits = 0;

    if (!lbits) return;

    if (bp->index >= bp->buflen) return;

    hbits = 64 - lbits;
    invalue = (invalue << hbits) >> hbits;
    bp->bvalue |= invalue << bp->bitpos;  // high bits value
    if ((bp->bitpos + lbits) >= 64) {
        bp->pbuf[bp->index] = bp->bvalue;
        bp->bvalue = invalue >> (64 - bp->bitpos);  // low bits value
        bp->index++;
    }
    bp->pbuf[bp->index] = bp->bvalue;
    bp->bitpos = (bp->bitpos + lbits) & 63;
}

static inline void put_align_a64(PutBitContext64 *bp, int align_bits, int flag)
{
    uint32_t word_offset = 0,  len = 0;

    word_offset = (align_bits >= 64) ? ((bp->index & (((align_bits & 0xfe0) >> 6) - 1)) << 6) : 0;
    len = (align_bits - (word_offset + (bp->bitpos % align_bits))) % align_bits;
    while (len > 0) {
        if (len >= 8) {
            if (flag == 0)
                put_bits_a64(bp, 8, ((uint64_t)0 << (64 - 8)) >> (64 - 8));
            else
                put_bits_a64(bp, 8, (0xffffffffffffffff << (64 - 8)) >> (64 - 8));
            len -= 8;
        } else {
            if (flag == 0)
                put_bits_a64(bp, len, ((uint64_t)0 << (64 - len)) >> (64 - len));
            else
                put_bits_a64(bp, len, (0xffffffffffffffff << (64 - len)) >> (64 - len));
            len -= len;
        }
    }
}


#endif /* AVCODEC_PUT_BITS64_H */

