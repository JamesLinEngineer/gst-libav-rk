/* GStreamer
 * Copyright (C) <2005> Jan Schmidt <jan@fluendo.com>
 * Copyright (C) <2002> Wim Taymans <wim@fluendo.com>
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
 */

#include <gst/gst.h>
#include <gst/video/video.h>
#include <libavcodec/avcodec.h>

#define GST_TYPE_FFMPEG_SUB_DEC             (gst_ffmpeg_sub_dec_get_type())
#define GST_FFMPEG_SUB_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FFMPEG_SUB_DEC, GstFFMpegSubDec))
#define GST_FFMPEG_SUB_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FFMPEG_SUB_DEC, GstFFMpegSubDecClass))
#define GST_IS_FFMPEG_SUB_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FFMPEG_SUB_DEC))
#define GST_IS_FFMPEG_SUB_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FFMPEG_SUB_DEC))

typedef struct _GstFFMpegSubDec GstFFMpegSubDec;
typedef struct _GstFFMpegSubDecClass GstFFMpegSubDecClass;

struct _GstFFMpegSubDec
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* decoding */
  AVCodecContext *context;
  AVCodec *codec;
  AVCodecParserContext* parser;
  gboolean opened;

  AVSubtitle *frame;
};

struct _GstFFMpegSubDecClass 
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

GType gst_ffmpeg_sub_dec_get_type (void);

