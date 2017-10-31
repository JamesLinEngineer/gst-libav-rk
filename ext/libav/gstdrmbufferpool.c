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
 
#include <gst/video/gstvideometa.h>
 
#include "gstdrmbufferpool.h"
#include "gstdrmallocator.h"
 
GST_DEBUG_CATEGORY_STATIC (gst_drm_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_drm_buffer_pool_debug

#define parent_class gst_drm_buffer_pool_parent_class
G_DEFINE_TYPE_WITH_CODE (GstDRMBufferPool, gst_drm_buffer_pool,
    GST_TYPE_BUFFER_POOL, G_ADD_PRIVATE (GstDRMBufferPool);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "drmbufferpool", 0,
     "Rockchip DRM buffer pool"));

static const gchar **
gst_drm_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_DRM_BUFFER, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };
  return options;
}

static gboolean
gst_drm_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstDRMBufferPool *vpool;
  GstDRMBufferPoolPrivate *priv;
  GstCaps *caps;
  GstVideoInfo vinfo;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstMemory *mem;
  guint size, min_buffers, max_buffers;
  gboolean ret;

  vpool = GST_DRM_BUFFER_POOL_CAST (pool);
  priv = vpool->priv;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers, &max_buffers))
    goto wrong_config;

  if (!caps)
    goto no_caps;

  if (priv->allocator)
    gst_object_unref(priv->allocator);

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&vinfo, caps))
    goto wrong_caps;

  /* try alloc buffer and change info size */
  mem = gst_drm_allocator_alloc(priv->vallocator, &vinfo);
  if (!mem)
    goto no_allocator;

  if (vinfo.size != ((GstDRMMemory *) mem)->bo->size) {
    GST_WARNING_OBJECT(pool, "drm buffer pool change size %d to %d.", vinfo.size, ((GstDRMMemory *) mem)->bo->size);
    vinfo.size = ((GstDRMMemory *) mem)->bo->size;
    gst_buffer_pool_config_set_params(config, caps, vinfo.size, min_buffers, max_buffers);
  }

  /* create dma allocator */
  priv->allocator = gst_dmabuf_allocator_new();
  priv->vinfo = vinfo;

  /* enable metadata based on config of the pool */
  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  ret = GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  gst_memory_unref(mem);

  return ret;

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
no_allocator:
  {
    GST_WARNING_OBJECT (pool, "no valid allocator in pool");
    return FALSE;
  }
}


static GstFlowReturn
gst_drm_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstDRMBufferPool *vpool;
  GstDRMBufferPoolPrivate *priv;
  GstVideoInfo *info;
  GstMemory *mem;
  GstMemory *dma_mem;

  vpool = GST_DRM_BUFFER_POOL_CAST (pool);
  priv = vpool->priv;
  info = &priv->vinfo;

  GST_DEBUG_OBJECT(pool, "Drm buffer pool alloc buffer: w:%d h:%d stride:%d", 
    GST_VIDEO_INFO_WIDTH(info), GST_VIDEO_INFO_HEIGHT(info), GST_VIDEO_INFO_PLANE_STRIDE(info, 0));

  *buffer = gst_buffer_new ();
  if (*buffer == NULL)
    goto no_memory;

  mem = gst_drm_allocator_alloc (priv->vallocator, info);

  if (!mem) {
    gst_buffer_unref (*buffer);
    goto no_memory;
  }

  dma_mem = gst_dmabuf_allocator_alloc(priv->allocator, gst_drm_memory_get_fd(mem), ((GstDRMMemory *) mem)->bo->size);

  gst_mini_object_set_qdata(GST_MINI_OBJECT(dma_mem),
      GST_DRM_MEMORY_QUARK, mem, (GDestroyNotify) gst_memory_unref);

  gst_buffer_append_memory (*buffer, dma_mem);

  if (priv->add_videometa) {
    GstVideoMeta *meta = 
    gst_buffer_add_video_meta_full (*buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
        GST_VIDEO_INFO_N_PLANES (info), info->offset, info->stride);
  }

  return GST_FLOW_OK;

  /* ERROR */
no_memory:
  {
    GST_WARNING_OBJECT (pool, "can't create memory");
    return GST_FLOW_ERROR;
  }
}

static void
gst_drm_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstDRMBufferPool *pool;

  pool = GST_DRM_BUFFER_POOL(bpool);

  GST_DEBUG_OBJECT(pool, "Drm buffer pool release buffer");

  return GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool, buffer);
}

static void
gst_drm_buffer_pool_finalize (GObject * object)
{
  GstDRMBufferPool *pool;
  GstDRMBufferPoolPrivate *priv;

  pool = GST_DRM_BUFFER_POOL (object);
  priv = pool->priv;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  if (priv->vallocator)
    gst_object_unref(priv->vallocator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_drm_buffer_pool_init (GstDRMBufferPool * pool)
{
  pool->priv = gst_drm_buffer_pool_get_instance_private (pool);
  pool->priv->fd = -1;
}

static void
gst_drm_buffer_pool_class_init (GstDRMBufferPoolClass * klass)
{
  GObjectClass *gobject_class;
  GstBufferPoolClass *gstbufferpool_class;

  gobject_class = (GObjectClass *) klass;
  gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_drm_buffer_pool_finalize;

  gstbufferpool_class->get_options = gst_drm_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_drm_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_drm_buffer_pool_alloc_buffer;
  gstbufferpool_class->release_buffer = gst_drm_buffer_pool_release_buffer;
}

GstBufferPool *
gst_drm_buffer_pool_new (guint flag)
{
  GstDRMBufferPool* pool;

  pool = g_object_new (GST_TYPE_DRM_BUFFER_POOL, NULL);
  pool->priv->vallocator = gst_drm_allocator_new(0);
  if (flag)
    g_object_set(G_OBJECT(pool->priv->vallocator),
                    "alloc-scale", 1.4,
                    NULL);
  return pool;
}


