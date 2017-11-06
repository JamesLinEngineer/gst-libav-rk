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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dlfcn.h>

#include "allocator_ion.h"
#include "ion.h"

typedef struct {
    unsigned int alignment;
    int ion_device;
} allocator_ctx_ion;

static int ion_ioctl(int fd, int req, void *arg)
{
    int ret = ioctl(fd, req, arg);
    if (ret < 0) {
        return -errno;
    }
    return ret;
}

const char *dev_ion = "/dev/ion";

static int os_allocator_ion_alloc(void *ctx, AVFrame *info)
{
    int ret;
    size_t len;
    allocator_ctx_ion *p = NULL;
    struct ion_allocation_data alloc_data;
    struct ion_fd_data fd_data;
    void* ptr;

    if (NULL == ctx) {
        return -EINVAL;
    }

    memset(&alloc_data, 0, sizeof(struct ion_allocation_data));
    memset(&fd_data, 0, sizeof(struct ion_fd_data));

    p = (allocator_ctx_ion *)ctx;
    len = info->linesize[0];

    alloc_data.align = p->alignment;
    alloc_data.flags = 0;
    alloc_data.heap_id_mask = ION_HEAP_SYSTEM_MASK;
    alloc_data.len = len;

    ret = ion_ioctl(p->ion_device, ION_IOC_ALLOC, &alloc_data);

    if (ret)
        return ret;

    fd_data.handle = alloc_data.handle;

    ret = ion_ioctl(p->ion_device, ION_IOC_MAP, &fd_data);
    if (ret)
        return ret;

    if (fd_data.fd < 0)
        return -EINVAL;

    ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_data.fd, 0);

    if (ptr == MAP_FAILED)
        return -EINVAL;

    info->data[0] = ptr;
    info->linesize[1] = fd_data.handle;
    info->linesize[2] = fd_data.fd;

    return ret;
}

static int os_allocator_ion_free(void* ctx, AVFrame* data)
{
    allocator_ctx_ion *p = NULL;
    void* ptr;
    size_t len;
    int fd, handle, ret;
    struct ion_handle_data handle_data;

    if (NULL == ctx) 
        return -EINVAL;

    memset(&handle_data, 0, sizeof(struct ion_handle_data));
    
    p = (allocator_ctx_ion*)ctx;
    ptr = data->data[0];
    len = data->linesize[0];
    handle = data->linesize[1];
    fd = data->linesize[2];
 
    munmap(ptr, len);
    close(fd);

    handle_data.handle = handle;

    ret = ion_ioctl(p->ion_device, ION_IOC_FREE, &handle_data);

    return ret;
}

static int os_allocator_ion_open(void **ctx, size_t alignment)
{
    int fd;
    allocator_ctx_ion *p;

    if (NULL == ctx) {
        return -EINVAL;
    }

    *ctx = NULL;

    fd = open(dev_ion, O_RDWR);
    if (fd < 0) {
        return -EINVAL;
    }

    p = av_mallocz(sizeof(allocator_ctx_ion));

    if (NULL == p) {
        close(fd);
        return -EINVAL;
    } else {
        p->alignment    = alignment;
        p->ion_device   = fd;
        *ctx = p;
    }

    return 0;
}

static int os_allocator_ion_close(void *ctx)
{
    int ret;
    allocator_ctx_ion *p;

    if (NULL == ctx) {
        return -EINVAL;
    }

    p = (allocator_ctx_ion *)ctx;
    ret = close(p->ion_device);
    av_free(p);
    if (ret < 0)
        ret =  -errno;

    return ret;
}

os_allocator allocator_ion = {
    .open = os_allocator_ion_open,
    .close = os_allocator_ion_close,
    .alloc = os_allocator_ion_alloc,
    .free = os_allocator_ion_free,
    .import = NULL,
    .release = NULL,
    .mmap = NULL,
};
 
