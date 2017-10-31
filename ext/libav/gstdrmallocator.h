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


#ifndef __GST_DRM_ALLOCATOR_H__
#define __GST_DRM_ALLOCATOR_H__
 
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstDRMAllocator GstDRMAllocator;
typedef struct _GstDRMAllocatorClass GstDRMAllocatorClass;
typedef struct _GstDRMAllocatorPrivate GstDRMAllocatorPrivate;
typedef struct _GstDRMMemory GstDRMMemory;

#define GST_DRM_MEMORY_QUARK gst_drm_memory_quark()

#define GST_TYPE_DRM_ALLOCATOR	\
   (gst_drm_allocator_get_type())
#define GST_IS_DRM_ALLOCATOR(obj)				\
       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DRM_ALLOCATOR))
#define GST_DRM_ALLOCATOR(obj)				\
   (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DRM_ALLOCATOR, GstDRMAllocator))
#define GST_DRM_ALLOCATOR_CLASS(klass)			\
   (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DRM_ALLOCATOR, GstDRMAllocatorClass))


#define GST_DRM_MEMORY_TYPE "DRMMemory"

struct drm_bo
{
  void *ptr;
  size_t size;
  size_t offset;
  size_t pitch;
  unsigned handle;
  unsigned int refs;
};

struct _GstDRMAllocatorPrivate
{
  int device_fd;
  gfloat alloc_scale;
};

struct _GstDRMMemory
{
  GstMemory parent;

  guint32 fb_id;
  guint32 gem_handle[GST_VIDEO_MAX_PLANES];
  guint32 dma_fd;
  struct drm_bo *bo;
};

struct _GstDRMAllocator
{
  GstAllocator parent;
  GstDRMAllocatorPrivate *priv;
};

struct _GstDRMAllocatorClass {
  GstAllocatorClass parent_class;
};

GType gst_drm_allocator_get_type(void) G_GNUC_CONST;

gboolean gst_is_drm_memory (GstMemory *mem);

GQuark gst_drm_memory_quark();

guint32 gst_drm_memory_get_fb_id (GstMemory *mem);

guint32 gst_drm_memory_get_fd(GstMemory *mem);

GstAllocator* gst_drm_allocator_new (gint fd);

GstMemory* gst_drm_allocator_alloc (GstAllocator *allocator,
					  GstVideoInfo *vinfo);

static gpointer gst_drm_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags);

G_END_DECLS


#endif 
