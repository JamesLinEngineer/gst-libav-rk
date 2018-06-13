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

#ifndef __GST_DRM_BUFFER_POOL_H__
#define __GST_DRM_BUFFER_POOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GST_BUFFER_POOL_OPTION_DRM_BUFFER:
 *
 * An option that can be activated on buffer pool to request DRM
 * buffers.
 */
#define GST_BUFFER_POOL_OPTION_DRM_BUFFER "GstBufferPoolOptionDRMBuffer"

#define GST_TYPE_DRM_BUFFER_POOL \
      (gst_drm_buffer_pool_get_type())
#define GST_DRM_BUFFER_POOL(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DRM_BUFFER_POOL, GstDRMBufferPool))
#define GST_DRM_BUFFER_POOL_CAST(obj) \
      ((GstDRMBufferPool*)(obj))

/* video bufferpool */
typedef struct _GstDRMBufferPoolClass GstDRMBufferPoolClass;
typedef struct _GstDRMBufferPoolPrivate GstDRMBufferPoolPrivate;
typedef struct _GstDRMBufferPool GstDRMBufferPool;

    
struct _GstDRMBufferPool
{
  GstBufferPool parent;
  GstDRMBufferPoolPrivate *priv;
};

struct _GstDRMBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

struct _GstDRMBufferPoolPrivate
{
  gint fd;
  GstVideoInfo vinfo;
  GstAllocator *allocator;
  GstAllocator *vallocator;
  gboolean add_videometa;
  gint outstanding;
};


GType gst_drm_buffer_pool_get_type (void) G_GNUC_CONST;

GstBufferPool *gst_drm_buffer_pool_new (guint flag);

G_END_DECLS
    
#endif /* __GST_DRM_BUFFER_POOL_H__ */


