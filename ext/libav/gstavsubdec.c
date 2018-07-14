/* GStreamer
 * Copyright 2018 Rockchip Electronics Co., Ltd
 *   Author: James <james.lin@rock-chips.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
     
#include <assert.h>
#include <string.h>
     
#include <libavcodec/avcodec.h>

#include <gst/gst.h>

#include "gstav.h"
#include "gstavsubdec.h"

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("avdec-params")

#define gst_ffmpeg_sub_dec_parent_class parent_class
G_DEFINE_TYPE (GstFFMpegSubDec, gst_ffmpeg_sub_dec, GST_TYPE_ELEMENT);

#define MAX_SUBTITLE_LENGTH 512

static void gst_ffmpeg_sub_dec_base_init(GstFFMpegSubDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  AVCodec *in_plugin;
  gchar *longname, *description;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFDEC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("libav %s decoder", in_plugin->long_name);
  description = g_strdup_printf ("libav %s decoder", in_plugin->name);
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Decoder/Subtitle", description,
      "James Lin <smallhujiu@gmail.com>");
  g_free (longname);
  g_free (description);

  /* set the caps */
  switch (in_plugin->id) {
  case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
    sinkcaps = gst_caps_new_empty_simple ("subpicture/x-pgs");
    break;
  case AV_CODEC_ID_DVD_SUBTITLE:
    sinkcaps = gst_caps_new_empty_simple ("subpicture/x-dvd");
    break;
  case AV_CODEC_ID_DVB_SUBTITLE:
    sinkcaps = gst_caps_new_empty_simple ("subpicture/x-dvb");
    break;
  case AV_CODEC_ID_XSUB:
    sinkcaps = gst_caps_new_empty_simple ("subpicture/x-xsub");
    break;
  case AV_CODEC_ID_TEXT:
    sinkcaps = gst_caps_new_empty_simple ("text/x-raw");
    break;
  default:
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }

  srccaps = gst_caps_new_simple("video/x-raw",
    "format", G_TYPE_STRING, "RGBA",
    "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
    "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
    NULL);

  if (!srccaps) {
    GST_DEBUG ("Couldn't get source caps for decoder '%s'", in_plugin->name);
    srccaps = gst_caps_from_string ("video/x-raw");
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->in_plugin = in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;

};

static void
gst_ffmpeg_sub_dec_finalize (GObject * gobject)
{
  GstFFMpegSubDec *ffmpegdec = (GstFFMpegSubDec *) gobject;

  avsubtitle_free (ffmpegdec->frame);

  if (ffmpegdec->context != NULL) {
    gst_ffmpeg_avcodec_close (ffmpegdec->context);
    av_free (ffmpegdec->context);
    ffmpegdec->context = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void gst_ffmpeg_sub_dec_class_init(GstFFMpegSubDecClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_ffmpeg_sub_dec_finalize;
};

static gboolean
gst_ffmpegsubdec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:{
      res = gst_pad_event_default (pad, parent, event);
      break;
    }
  }

  return res;
}

static gboolean
gst_ffmpegdec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static void
gst_avpacket_init (AVPacket * packet, guint8 * data, guint size)
{
  memset (packet, 0, sizeof (AVPacket));
  packet->data = data;
  packet->size = size;
}

static void gst_ffmpegsubdec_picture(GstFFMpegSubDec *ffmpegdec, AVSubtitle* sub, GstClockTime ts, GstClockTime dur)
{
  gint i, j, w, h, size;
  guint8 **data;
  guint8 *ptr;
  GstBuffer *buffer;

  if (sub && sub->num_rects) {
    for (i = 0; i < sub->num_rects; i++) {
      w = sub->rects[i]->w;
      h = sub->rects[i]->h;

      size = w*h*4;
      data = sub->rects[i]->pict.data;
      ptr = g_malloc(size);
      for (j = 0; j < w * h; j++) {
        gint index = data[0][j];
        guint8 r = data[1][index*4];
        guint8 g = data[1][index*4 + 1];
        guint8 b = data[1][index*4 + 2];
        guint8 a = data[1][index*4 + 3];

        ptr[4*j + 0] = a*r/255;
        ptr[4*j + 1] = a*g/255;
        ptr[4*j + 2] = a*b/255;
        ptr[4*j + 3] = a*a/255;
      }
      buffer = gst_buffer_new_allocate (NULL, size, NULL);
      gst_buffer_fill(buffer, 0, ptr, size);
      gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_RGBA, w, h);
      GST_BUFFER_PTS(buffer) = ts;
      GST_BUFFER_DURATION(buffer) = dur;

      GST_DEBUG_OBJECT(ffmpegdec, "Have picture w:%d, h:%d, ts %"
        GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, w, h, GST_TIME_ARGS (ts), GST_BUFFER_DURATION (buffer));

      gst_pad_push(ffmpegdec->srcpad, buffer);
      g_free(ptr);
    }
  }else {
     buffer = gst_buffer_new_allocate (NULL, 0, NULL);
     gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_RGBA, 0, 0);
     GST_BUFFER_PTS(buffer) = ts;
     GST_BUFFER_DURATION(buffer) = dur;
     gst_pad_push(ffmpegdec->srcpad, buffer);

     GST_DEBUG_OBJECT(ffmpegdec, "Have empty picture ts %"
        GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, GST_TIME_ARGS (ts), GST_BUFFER_DURATION (buffer));
  }
}


static void gst_ffmpegsubdec_text(GstFFMpegSubDec *ffmpegdec, AVSubtitle* sub, GstClockTime ts, GstClockTime dur)
{
    gint i, j, w, h, size;
    guint8 *data;
    guint8 *ptr;
    GstBuffer *buffer, *buffernull;
    if (sub && sub->num_rects) {
        for (i = 0; i < sub->num_rects; i++) {
            w = sub->rects[i]->w;
            h = sub->rects[i]->h;
            size = MAX_SUBTITLE_LENGTH;
            if( sub->rects[i]->ass!= NULL){
             data = sub->rects[i]->ass;
             char * rawContent = sub->rects[i]->ass;
             ptr = g_malloc(size);
             int hour1, hour2, min1, min2, sec1, sec2, msec1, msec2, index;
             char* txtContent[MAX_SUBTITLE_LENGTH]= {0};
             if(strlen(rawContent)>= (MAX_SUBTITLE_LENGTH-1)){
                 GST_ERROR("invalid rawContent; Too long");
             }
             if(sscanf(rawContent, "Dialogue: %2d,%02d:%02d:%02d.%02d,%02d:%02d:%02d.%02d,Default,%[^\r\n]",
                       &index, &hour1, &min1, &sec1, &msec1, &hour2, &min2, &sec2, &msec2, txtContent) != 10) {
                    GST_ERROR(" ERROR_MALFORMED; txtContent:%s", txtContent);
              }
             int  txtLen = strlen(txtContent);
             if(txtLen <= 0) {
                GST_ERROR("invalid rawContent;NULL");
             }
             filterSpecialChar(txtContent);
             strcpy(ptr, txtContent);
            GST_ERROR(">>>>>>>>>>>>>>> txtContent:%s", txtContent);
            buffer = gst_buffer_new_allocate (NULL, size, NULL);
            gst_buffer_fill(buffer, 0, ptr, size);
            gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_NV21, w, h);

            GST_BUFFER_PTS(buffer) = ts;
            GST_BUFFER_DURATION(buffer) = dur;

            GST_ERROR_OBJECT(ffmpegdec, " Have text w:%d, h:%d, ts %"
            GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, w, h, GST_TIME_ARGS (ts), GST_BUFFER_DURATION (buffer));

            gst_pad_push(ffmpegdec->srcpad, buffer);
            g_free(ptr);

            push_void_buffer(ffmpegdec, ts+dur, 0);
               
            }
          }
        }else {
	      buffer = gst_buffer_new_allocate (NULL, 0, NULL);
	      gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
	         GST_VIDEO_FORMAT_NV21, 0, 0);
	      GST_BUFFER_PTS(buffer) = ts;
	      GST_BUFFER_DURATION(buffer) = dur;
	      gst_pad_push(ffmpegdec->srcpad, buffer);

	      GST_DEBUG_OBJECT(ffmpegdec, "Have empty picture ts %"
	         GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, GST_TIME_ARGS (ts), GST_BUFFER_DURATION (buffer));
      }
}

void push_void_buffer(GstFFMpegSubDec *ffmpegdec,GstClockTime ts, GstClockTime dur)
{

     GstBuffer *buffer;
     buffer = gst_buffer_new_allocate (NULL, 0, NULL);
     gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
         GST_VIDEO_FORMAT_NV21 , 0, 0);
     GST_BUFFER_PTS(buffer) = ts;
     GST_BUFFER_DURATION(buffer) = dur;
     gst_pad_push(ffmpegdec->srcpad, buffer);

     GST_ERROR_OBJECT(ffmpegdec, "Text Have empty picture ts %"
         GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, GST_TIME_ARGS (ts), GST_BUFFER_DURATION (buffer));    

}

void filterSpecialChar(char* rawContent)
{
    int i;
    if(NULL != rawContent && strlen(rawContent) > 0) {
        for (i =0; i <strlen(rawContent) - 1; i++) {
            if ((rawContent + i) && (*(rawContent + i) == 0x5C)) {
                if (((*(rawContent + i + 1) == 0x6E) ||
                        (*(rawContent + i + 1) == 0x4E))) {
                    //replace "\n" or "\N" with "\r\n"
                    *(rawContent + i + 0) = 0x0D;
                    *(rawContent + i + 1) = 0x0A;
                } else if (*(rawContent + i + 1) == 0x68) {
                    // replace "\h" with space
                    *(rawContent + i + 0) = 0x20;
                    *(rawContent + i + 1) = 0x20;
                }
            }
        }
    }

    int error = 0;
    do {
        error = filterSpecialTag(rawContent);
    } while(0 == error);
}

int  filterSpecialTag(char* rawContent)
{
    int pattern[3] = {0};
    int len,idx,i;
    len = idx = i =0;
    pattern[0] = pattern[1] = pattern[2] = -1;
    if((NULL!=rawContent)||(strlen(rawContent)>0)) {
        len = strlen(rawContent);
        for (i =0; i < len; i++) {
            if(rawContent[i]=='{') {
                pattern[0] = i;
            }
            if(rawContent[i]=='\\') {
                if((-1 != pattern[0])&&(i==(pattern[0]+1))) {
                    pattern[1] = i;
                    //ALOGE("filterSpecialTag pattern[1] = %d", pattern[1]);
                } else {
                    pattern[0] = -1;
                }
            }
            if((rawContent[i]=='}')&&(-1 != pattern[0])&&(-1 != pattern[1])) {
                pattern[2] = i;
                //ALOGE("filterSpecialTag pattern[2] = %d", pattern[2]);
                break;
            }
        }
    }
    if((pattern[0] >= 0)&&(pattern[2] > 0)&&(len>0)) {
        //ALOGE("filterSpecialTag pattern[0] =%d; pattern[2]=%d; rawContent = %s", pattern[0], pattern[2], rawContent);
        char newContent[MAX_SUBTITLE_LENGTH]= {0};
        memset(newContent, 0, MAX_SUBTITLE_LENGTH);
        for(i = 0; i < pattern[0]; i++) {
            newContent[idx] = rawContent[i];
            idx++;
        }

        for(i = pattern[2]+1; i < len; i++) {
            newContent[idx] = rawContent[i];
            idx++;
        }
        snprintf(rawContent, MAX_SUBTITLE_LENGTH, "%s", newContent);
        return 0;
    }
    return -1;
}


static GstFlowReturn
gst_ffmpegsubdec_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstFFMpegSubDec *ffmpegdec;
  GstMapInfo map;
  guint8 *bdata;
  glong bsize = 0;
  guint8 retry = 10;

  ffmpegdec = GST_FFMPEG_SUB_DEC (parent);

  GST_DEBUG_OBJECT (ffmpegdec, "Have buffer of size %" G_GSIZE_FORMAT ", ts %"
      GST_TIME_FORMAT ", dur %" G_GINT64_FORMAT, gst_buffer_get_size (inbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)), GST_BUFFER_DURATION (inbuf));

  inbuf = gst_buffer_ref (inbuf);
  gst_buffer_map (inbuf, &map, GST_MAP_READ);

  bdata = map.data;
  bsize = map.size;

  do {
    AVPacket packet = {0};
    gint have_data = 0;    
    gint len = -1;

    if (ffmpegdec->context->codec_id == AV_CODEC_ID_DVB_SUBTITLE) {
      if (bsize >= 3 && bdata[0] == 0x20 && bdata[1] == 0x00) {
        bdata += 2;
        bsize -= 2;
      }
    }

    gst_avpacket_init (&packet, bdata, bsize);

    len = avcodec_decode_subtitle2(ffmpegdec->context, ffmpegdec->frame,
                               &have_data,
                               &packet);

    if (len >= 0) {
      bdata += len;
      bsize -= len;
    }

    if (have_data) {
      if (ffmpegdec->frame->format == 0 /* graphics */) {
        switch(ffmpegdec->context->codec_id) {
        case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
        case AV_CODEC_ID_DVB_SUBTITLE:
        case AV_CODEC_ID_DVD_SUBTITLE:
        case AV_CODEC_ID_XSUB:
          gst_ffmpegsubdec_picture(ffmpegdec, ffmpegdec->frame, 
            GST_BUFFER_TIMESTAMP (inbuf), GST_BUFFER_DURATION (inbuf));
          break;
        default:
          break;
        }
      } else if ( ffmpegdec->frame->format == 1 ){
        switch(ffmpegdec->context->codec_id) {
        case AV_CODEC_ID_TEXT:
           gst_ffmpegsubdec_text(ffmpegdec, ffmpegdec->frame,
            GST_BUFFER_TIMESTAMP (inbuf), GST_BUFFER_DURATION (inbuf));
          break;
        default:
          break;
        }
      }
      avsubtitle_free(ffmpegdec->frame);
    }
  } 
  while (bsize > 0 && retry--);

  gst_buffer_unmap(inbuf, &map);
  gst_buffer_unref(inbuf);

  return ret;
}

static void gst_ffmpeg_sub_dec_init(GstFFMpegSubDec * ffmpegdec)
{
  GstFFMpegSubDecClass *klass =
      (GstFFMpegSubDecClass *) G_OBJECT_GET_CLASS (ffmpegdec);

  ffmpegdec->opened = FALSE;

  if (!klass->in_plugin)
    return;

  ffmpegdec->sinkpad = gst_pad_new_from_template (klass->sinktempl, "sink");
  gst_pad_set_chain_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegsubdec_chain));
  gst_pad_set_event_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegsubdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);

  ffmpegdec->srcpad = gst_pad_new_from_template (klass->srctempl, "src");
  gst_pad_set_event_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_src_event));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  /* some ffmpeg data */
  ffmpegdec->codec = avcodec_find_decoder(klass->in_plugin->id);
  if (ffmpegdec->codec) {
    GST_DEBUG_OBJECT(ffmpegdec, "try open codec %d name %s\n", ffmpegdec->codec->id, ffmpegdec->codec->name);
    ffmpegdec->context = avcodec_alloc_context3 (ffmpegdec->codec);
    ffmpegdec->context->opaque = ffmpegdec;

    ffmpegdec->frame = av_mallocz(sizeof(AVSubtitle));

    if (gst_ffmpeg_avcodec_open (ffmpegdec->context, ffmpegdec->codec) < 0)
      goto could_not_open;

    ffmpegdec->opened = TRUE;

    GST_DEBUG_OBJECT (ffmpegdec, "Opened libav codec %s, id %d",
        ffmpegdec->codec->name, ffmpegdec->codec->id);
  }

  return;

could_not_open:

  gst_ffmpeg_avcodec_close (ffmpegdec->context);
  GST_DEBUG_OBJECT (ffmpegdec, "avdec_%s: Failed to open libav codec",
      klass->in_plugin->name);
};

gboolean
gst_ffmpegsubdec_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegSubDecClass),
    (GBaseInitFunc) gst_ffmpeg_sub_dec_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpeg_sub_dec_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegSubDec),
    0,
    (GInstanceInitFunc) gst_ffmpeg_sub_dec_init,
  };
  GType type;
  AVCodec *in_plugin;
  gint rank;

  in_plugin = av_codec_next(NULL);

  GST_LOG ("Registering decoders");

  while (in_plugin) {
    gchar *type_name;

    /* only decoders */
    if (!av_codec_is_decoder (in_plugin)
        || in_plugin->type != AVMEDIA_TYPE_SUBTITLE) {
      goto next;
    }

    switch (in_plugin->id) {
    case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
    case AV_CODEC_ID_DVD_SUBTITLE:
    case AV_CODEC_ID_DVB_SUBTITLE:
    case AV_CODEC_ID_XSUB:
    case AV_CODEC_ID_TEXT:
      break;
    default:
      goto next;
    }

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* construct the type */
    type_name = g_strdup_printf ("avdec_%s", in_plugin->name);
    g_strdelimit (type_name, ".,|-<> ", '_');

    type = g_type_from_name (type_name);

    if (!type) {
      /* create the gtype now */
      type =
          g_type_register_static (GST_TYPE_FFMPEG_SUB_DEC, type_name, &typeinfo,
          0);
      g_type_set_qdata (type, GST_FFDEC_PARAMS_QDATA, (gpointer) in_plugin);
    }

    switch(in_plugin->id){
    case AV_CODEC_ID_HDMV_PGS_SUBTITLE:
    case AV_CODEC_ID_DVD_SUBTITLE:
      rank = GST_RANK_PRIMARY;
      break;
    default:
      rank = GST_RANK_SECONDARY;
    };

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_warning ("Failed to register %s", type_name);
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_codec_next (in_plugin);
  }

  GST_LOG ("Finished Registering decoders");

  return TRUE;
}
