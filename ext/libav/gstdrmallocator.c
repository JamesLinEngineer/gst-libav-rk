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
 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* it needs to be below because is internal to libdrm */
#include <drm.h>
#include <drm_fourcc.h>

#include "gstdrmallocator.h"

#define GST_CAT_DEFAULT drmallocator_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define parent_class gst_drm_allocator_parent_class

G_DEFINE_TYPE_WITH_CODE (GstDRMAllocator, gst_drm_allocator, GST_TYPE_ALLOCATOR,
    G_ADD_PRIVATE (GstDRMAllocator);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "drmallocator", 0,
        "Rockchip DRM allocator"));

enum
{
  PROP_DRM_FD = 1,
  PROP_N,
};

static GParamSpec *g_props[PROP_N] = { NULL, };

gboolean
gst_is_drm_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_DRM_MEMORY_TYPE);
}

guint32
gst_drm_memory_get_fb_id (GstMemory * mem)
{
  if (!gst_is_drm_memory (mem))
    return 0;
  return ((GstDRMMemory *) mem)->fb_id;
}

guint32
gst_drm_memory_get_fd (GstMemory * mem)
{
  if (!gst_is_drm_memory (mem))
    return 0;
  return ((GstDRMMemory *) mem)->dma_fd;
}

/* *INDENT-OFF* */
static const struct
{
  guint32 fourcc;
  GstVideoFormat format;
} format_map[] = {
#define DEF_FMT(fourcc, fmt) \
  { DRM_FORMAT_##fourcc,GST_VIDEO_FORMAT_##fmt }

  /* DEF_FMT (XRGB1555, ???), */
  /* DEF_FMT (XBGR1555, ???), */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  DEF_FMT (ARGB8888, BGRA),
  DEF_FMT (XRGB8888, BGRx),
  DEF_FMT (ABGR8888, RGBA),
  DEF_FMT (XBGR8888, RGBx),
#else
  DEF_FMT (ARGB8888, ARGB),
  DEF_FMT (XRGB8888, xRGB),
  DEF_FMT (ABGR8888, ABGR),
  DEF_FMT (XBGR8888, xBGR),
#endif
  DEF_FMT (UYVY, UYVY),
  DEF_FMT (YUYV, YUY2),
  DEF_FMT (YVYU, YVYU),
  DEF_FMT (YUV420, I420),
  DEF_FMT (YVU420, YV12),
  DEF_FMT (YUV422, Y42B),
  DEF_FMT (NV12, NV12),
  DEF_FMT (NV12_10, P010_10LE),
  DEF_FMT (NV21, NV21),
  DEF_FMT (NV16, NV16),

#undef DEF_FMT
};
/* *INDENT-ON* */

GstVideoFormat
gst_video_format_from_drm (guint32 drmfmt)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].fourcc == drmfmt)
      return format_map[i].format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

guint32
gst_drm_format_from_video (GstVideoFormat fmt)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == fmt)
      return format_map[i].fourcc;
  }

  return 0;
}

guint32
gst_drm_bpp_from_drm (guint32 drmfmt)
{
  guint32 bpp;

  switch (drmfmt) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
    case DRM_FORMAT_YUV422:
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
    case DRM_FORMAT_NV16:
      bpp = 8;
      break;
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_YUYV:
    case DRM_FORMAT_YVYU:
      bpp = 16;
      break;
    default:
      bpp = 32;
      break;
  }

  return bpp;
}

guint32
gst_drm_height_from_drm (guint32 drmfmt, guint32 height)
{
  guint32 ret;

  switch (drmfmt) {
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
    case DRM_FORMAT_YUV422:
    case DRM_FORMAT_NV21:
      ret = height * 3 / 2;
      break;
    case DRM_FORMAT_NV16:
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV12_10:
      ret = height * 2;
      break;
    default:
      ret = height;
      break;
  }

  return ret;
}

static gboolean
check_fd (GstDRMAllocator * alloc)
{
  return alloc->priv->device_fd > -1;
}

static void
gst_drm_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstDRMAllocator *alloc;
  GstDRMMemory *drmmem;
  int err;
  struct drm_mode_destroy_dumb arg = { 0, };

  alloc = GST_DRM_ALLOCATOR (allocator);
  drmmem = (GstDRMMemory *) mem;

  if (!check_fd (allocator))
    return;

  /* remove fb */
  if (drmmem->fb_id) {
    GST_DEBUG_OBJECT (allocator, "removing fb id %d", drmmem->fb_id);
    drmModeRmFB (alloc->priv->device_fd, drmmem->fb_id);
    drmmem->fb_id = 0;
  }

  if (!drmmem->bo)
    return;

  if (drmmem->bo->ptr != NULL) {
      GST_WARNING_OBJECT (allocator, "destroying mapped bo (refcount=%d)",
        drmmem->bo->refs);
      munmap (drmmem->bo->ptr, drmmem->bo->size);
      drmmem->bo->ptr = NULL;
  }

  /* close fd*/
  if (drmmem->dma_fd > 0)
    close(drmmem->dma_fd);

  /* destory drm buffer */
  arg.handle = drmmem->bo->handle;

  err = drmIoctl (alloc->priv->device_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);
  if (err)
    GST_WARNING_OBJECT (allocator,
      "Failed to destroy dumb buffer object: %s %d", strerror (errno), errno);
  
  g_free(drmmem->bo);
  drmmem->bo = NULL;
  g_slice_free (GstDRMMemory, drmmem);
}

GstMemory *
gst_drm_allocator_alloc (GstAllocator * allocator, GstVideoInfo * vinfo)
{
  GstDRMAllocator *alloc;
  GstDRMMemory *drmmem;
  GstMemory *mem;  
  gint num_planes = GST_VIDEO_INFO_N_PLANES (vinfo);
  struct drm_mode_create_dumb arg = { 0, };  
  struct drm_prime_handle dph = {0,};
  guint32 w, h, fmt, pitch = 0, bo_handles[4] = { 0, };
  guint32 offsets[4] = { 0, };
  guint32 pitches[4] = { 0, };
  gint i, ret;
  gpointer p = NULL;

  /* construct memory*/
  drmmem = g_slice_new0 (GstDRMMemory);
  if (!drmmem)
    return NULL;
  mem = GST_MEMORY_CAST(drmmem);

  gst_memory_init (mem, GST_MEMORY_FLAG_NO_SHARE, allocator, NULL,
    GST_VIDEO_INFO_SIZE (vinfo), 0, 0, GST_VIDEO_INFO_SIZE (vinfo));

  alloc = GST_DRM_ALLOCATOR (allocator);

  /* ioctrl request drm buffer */
  if (!check_fd (alloc))
    goto fail;

  drmmem->bo = g_malloc0 (sizeof (*drmmem->bo));
  if (!drmmem->bo)
    goto fail;

  w = GST_VIDEO_INFO_WIDTH (vinfo);
  h = GST_VIDEO_INFO_HEIGHT (vinfo);
  fmt = gst_drm_format_from_video(GST_VIDEO_INFO_FORMAT (vinfo));
  arg.bpp = gst_drm_bpp_from_drm (fmt);
  arg.width = w;
  arg.height = gst_drm_height_from_drm (fmt, GST_VIDEO_INFO_HEIGHT (vinfo)); 

  ret = drmIoctl (alloc->priv->device_fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
  if (ret)
    goto create_failed;

  drmmem->bo->handle = arg.handle;
  drmmem->bo->size = arg.size;
  drmmem->bo->pitch = arg.pitch;

  dph.handle = drmmem->bo->handle;
  dph.fd = -1;
  dph.flags = 0;
  ret = drmIoctl(alloc->priv->device_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &dph);
  if (ret)
    goto create_failed;

  drmmem->dma_fd = dph.fd;
  GST_DEBUG_OBJECT(alloc, "Alloc drm mem object: dmafd:%d vaddr:%p", drmmem->dma_fd, drmmem->bo->ptr);

  /* bind frame buffer */
  for (i = 0; i < num_planes; i++)
    bo_handles[i] = drmmem->bo->handle;
  if (num_planes == 1)
    pitch = drmmem->bo->pitch;

  for (i = 0; i < num_planes; i++) {
    offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET(vinfo, i);
    if (pitch)
      GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i) = pitch;    
    pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i);
  }

  ret = drmModeAddFB2 (alloc->priv->device_fd, w, h, fmt, bo_handles, pitches,
      offsets, &drmmem->fb_id, 0);

  p = gst_drm_memory_map(mem, drmmem->bo->size, 0);
  
  GST_DEBUG_OBJECT(alloc, "Bind drm mem object: dmafd:%d fb:%d pointer:%p", drmmem->dma_fd, drmmem->fb_id, p);

  return mem;

  /* ERRORS */
create_failed:
  GST_ERROR_OBJECT(alloc, "Fail to alloc drm mem: %s (%d)", strerror (-ret), ret);
  g_free(drmmem->bo);
  drmmem->bo = NULL;
fail:
  gst_memory_unref (mem);
  return NULL;
}

static void
gst_drm_allocator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDRMAllocator *alloc;

  alloc = GST_DRM_ALLOCATOR (object);

  switch (prop_id) {
    case PROP_DRM_FD:
      g_value_set_int (value, alloc->priv->device_fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_drm_allocator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDRMAllocator *alloc;

  alloc = GST_DRM_ALLOCATOR (object);

  switch (prop_id) {
    case PROP_DRM_FD:{
      int fd = g_value_get_int (value);
      if (fd > -1)
        alloc->priv->device_fd = fd;

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_drm_allocator_finalize (GObject * obj)
{
  GstDRMAllocator *alloc;

  alloc = GST_DRM_ALLOCATOR (obj);

  GST_DEBUG_OBJECT(alloc, "finalize drm allocator fd(%d)", alloc->priv->device_fd);
  if (check_fd (alloc))
    close (alloc->priv->device_fd);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_drm_allocator_class_init (GstDRMAllocatorClass * klass)
{
  GObjectClass *gobject_class;
  GstAllocatorClass *allocator_class;

  allocator_class = GST_ALLOCATOR_CLASS (klass);
  gobject_class = G_OBJECT_CLASS (klass);

  allocator_class->free = gst_drm_allocator_free;
  /* FIXME: use videoinfo to alloc*/
  gobject_class->set_property = gst_drm_allocator_set_property;
  gobject_class->get_property = gst_drm_allocator_get_property;
  gobject_class->finalize = gst_drm_allocator_finalize;

  g_props[PROP_DRM_FD] = g_param_spec_int ("drm-fd", "DRM fd",
      "DRM file descriptor", -1, G_MAXINT, -1,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (gobject_class, PROP_N, g_props);
}

static gpointer
gst_drm_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstDRMMemory *drmmem;
  GstDRMAllocator *alloc;
  int err;
  gpointer out;
  struct drm_mode_map_dumb arg = { 0, };

  alloc = (GstDRMAllocator *) mem->allocator;

  if (!check_fd (alloc))
    return NULL;

  drmmem = (GstDRMMemory *) mem;
  if (!drmmem->bo)
    return NULL;

  /* Reuse existing buffer object mapping if possible */
  if (drmmem->bo->ptr != NULL) {
    goto out;
  }

  arg.handle = drmmem->bo->handle;
  arg.offset = 0;

  err = drmIoctl (alloc->priv->device_fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
  if (err) {
    GST_ERROR_OBJECT (alloc, "Failed to get offset of buffer object: %s %d",
        strerror (-err), err);
    return NULL;
  }

  out = mmap64 (0, drmmem->bo->size,
      PROT_READ | PROT_WRITE, MAP_SHARED, alloc->priv->device_fd, arg.offset);
  if (out == MAP_FAILED) {
    GST_ERROR_OBJECT (alloc, "Failed to map dumb buffer object: %s %d",
        strerror (errno), errno);
    return NULL;
  }
  drmmem->bo->ptr = out;

  GST_DEBUG_OBJECT(alloc, "Map drm mem object: dmafd:%d vaddr:%p", drmmem->dma_fd, drmmem->bo->ptr);

out:
  g_atomic_int_inc (&drmmem->bo->refs);
  return drmmem->bo->ptr;
}

static void
gst_drm_memory_unmap (GstMemory * mem)
{
  GstDRMMemory *drmmem;  
  GstDRMAllocator *alloc;

  alloc = (GstDRMAllocator *) mem->allocator;

  if (!check_fd (alloc))
    return;

  drmmem = (GstDRMMemory *) mem;
  if (!drmmem->bo)
    return;

  if (g_atomic_int_dec_and_test (&drmmem->bo->refs)) {
    munmap (drmmem->bo->ptr, drmmem->bo->size);
    drmmem->bo->ptr = NULL;
  }

  GST_DEBUG_OBJECT(alloc, "Unmap drm mem object: dmafd:%d vaddr:%p", drmmem->dma_fd, drmmem->bo->ptr);
}

static void
gst_drm_allocator_init (GstDRMAllocator * allocator)
{
  GstAllocator *alloc;

  alloc = GST_ALLOCATOR_CAST (allocator);
  
  allocator->priv = gst_drm_allocator_get_instance_private (allocator);
  allocator->priv->device_fd = -1;
  
  alloc->mem_type = GST_DRM_MEMORY_TYPE;
  alloc->mem_map = gst_drm_memory_map;
  alloc->mem_unmap = gst_drm_memory_unmap;
  /* Use the default, fallback copy function */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_drm_allocator_new (int fd)
{
  if (fd == 0)
    fd = drmOpen ("rockchip", NULL);

  return g_object_new (GST_TYPE_DRM_ALLOCATOR, "name",
      "DRMMemory::allocator", "drm-fd", fd, NULL);
}



