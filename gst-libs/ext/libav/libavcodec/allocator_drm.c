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

#include "drm.h"
#include "allocator_drm.h"
#include "libavutil/log.h"

typedef struct {
    unsigned int alignment;
    signed int   drm_device;
} allocator_ctx_drm;

static const char *dev_drm = "/dev/dri/card0";

static int drm_ioctl(int fd, int req, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, req, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

    return ret;
}

extern void *mmap64(void* addr,size_t length, int prot, int flags, int fd, int64_t offset);

static int os_allocator_drm_alloc(void *ctx, AVFrame *info)
{
    int ret;
    size_t len;
    allocator_ctx_drm *p = NULL;
    void* ptr;
    struct drm_mode_create_dumb dmcb;
    struct drm_prime_handle dph;
    struct drm_mode_map_dumb dmmd;

    if (NULL == ctx) {
        return -EINVAL;
    }

    p = (allocator_ctx_drm *)ctx;
    memset(&dmcb, 0, sizeof(struct drm_mode_create_dumb));
    memset(&dph, 0, sizeof(struct drm_prime_handle));    
    memset(&dmmd, 0, sizeof(dmmd));

    len = info->linesize[0];
    
    dmcb.bpp = 8;
    dmcb.width = (len + p->alignment- 1) & (~(p->alignment- 1));
    dmcb.height = 1;
    dmcb.size = dmcb.width * dmcb.bpp;

    ret = drm_ioctl(p->drm_device, DRM_IOCTL_MODE_CREATE_DUMB, &dmcb);
    if (ret < 0)
        return ret;

    dph.handle = dmcb.handle;
    dph.fd = -1;
    dph.flags = 0;

    ret = drm_ioctl(p->drm_device, DRM_IOCTL_PRIME_HANDLE_TO_FD, &dph);
    if (ret < 0)
        return ret;

    dmmd.handle = dmcb.handle;
    ret = drm_ioctl(p->drm_device, DRM_IOCTL_MODE_MAP_DUMB, &dmmd);
    if (ret < 0) {
        close(dph.fd);
        return ret;
    }
    
    ptr = mmap64(0, dmcb.size, PROT_READ | PROT_WRITE, MAP_SHARED, p->drm_device, dmmd.offset);

    info->linesize[0] = dmcb.size;
    info->linesize[1] = dmcb.handle;
    info->linesize[2] = dph.fd;
    info->data[0] = ptr;
        
    return 0;
}

static int drm_free(int fd, unsigned int handle)
{
    int ret = 0;
    struct drm_mode_destroy_dumb data = {
        .handle = handle,
    };
    struct drm_gem_close arg;

    ret = drm_ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &data);
    if (ret)
        return -errno;

    memset(&arg, 0, sizeof(arg));
    arg.handle = handle;
    ret = drm_ioctl(fd, DRM_IOCTL_GEM_CLOSE, &arg);
    if (ret)
        return -errno;

    return 0;
}

static int os_allocator_drm_free(void *ctx, AVFrame *info)
{
    allocator_ctx_drm *p = NULL;

    if (NULL == ctx) {
        return -EINVAL;
    }

    p = (allocator_ctx_drm *)ctx;
    munmap(info->data[0], info->linesize[0]);
    close(info->linesize[2]);
    drm_free(p->drm_device, (unsigned)(info->linesize[1]));
    return 0;
}

static int os_allocator_drm_open(void **ctx, size_t alignment)
{
    int fd;
    allocator_ctx_drm *p;

    if (NULL == ctx) {
        return -EINVAL;
    }
    
    *ctx = NULL;

    fd = open(dev_drm, O_RDWR);
    if (fd < 0) {
        return -EINVAL;
    }
    p = av_mallocz(sizeof(allocator_ctx_drm));
    
    if (NULL == p) {
        close(fd);
        return -EINVAL;
    } else {
        /*
         * default drm use cma, do nothing here
         */
        p->alignment    = alignment;
        p->drm_device   = fd;
        *ctx = p;
    }

    return 0;
}

static int os_allocator_drm_close(void *ctx)
{
    int ret;
    allocator_ctx_drm *p;

    if (NULL == ctx) {
        return -EINVAL;
    }

    p = (allocator_ctx_drm *)ctx;
    ret = close(p->drm_device);
    av_free(p);
    if (ret < 0)
        return -errno;
    return 0;
}

os_allocator allocator_drm = {
    .open = os_allocator_drm_open,
    .close = os_allocator_drm_close,
    .alloc = os_allocator_drm_alloc,
    .free = os_allocator_drm_free,
    .import = NULL,
    .release = NULL,
    .mmap = NULL,
};
