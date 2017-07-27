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

#ifndef __OS_ALLOCATOR_H__
#define __OS_ALLOCATOR_H__

#include "libavutil/frame.h"

typedef int (*OsAllocatorFunc)(void *ctx, AVFrame *info);

typedef struct os_allocator_t {
    int (*open)(void **ctx, size_t alignment);
    int (*close)(void *ctx);

    OsAllocatorFunc alloc;
    OsAllocatorFunc free;
    OsAllocatorFunc import;
    OsAllocatorFunc release;
    OsAllocatorFunc mmap;
} os_allocator;

#endif /*__OS_ALLOCATOR_H__*/


