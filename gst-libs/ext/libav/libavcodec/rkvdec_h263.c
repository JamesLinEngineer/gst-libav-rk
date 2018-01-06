/*
 * Copyright 2017 Rockchip Electronics Co., Ltd
 *     Author: Shelly Xie<shelly.xie@rock-chips.com>
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

#include "h263.h"
#include "rkvdec_h263.h"
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

FILE* fp2=NULL;
    
typedef struct _RKVDECH263Context RKVDECH263Context;
typedef struct H263dRkvRegs_t RKVDEC_H263_Regs, *LPRKVDEC_H263_Regs;
typedef struct _RKVDEC_PicParams_H263 RKVDEC_PicParams_H263, *LPRKVDEC_PicParams_H263;
typedef struct _RKVDECH263HwReq RKVDECH263HwReq;


struct _RKVDECH263Context{
     signed int vpu_socket;
     LPRKVDEC_H263_Regs hw_regs;
     LPRKVDEC_PicParams_H263 pic_param;
     AVFrame* stream_data;
     os_allocator *allocator;
     void *allocator_ctx;
};

struct _RKVDECH263HwReq {
   unsigned int *req;
   unsigned int  size;
} ;


/* H.263 MVC picture parameters structure */
struct _RKVDEC_PicParams_H263 {
    RK_U8   short_video_header;
    RK_U8   vop_coding_type;
    RK_U8   vop_quant;
    RK_U16  wDecodedPictureIndex;
    RK_U16  wDeblockedPictureIndex;
    RK_U16  wForwardRefPictureIndex;
    RK_U16  wBackwardRefPictureIndex;
    RK_U16  vop_time_increment_resolution;
    RK_U32  TRB[2];
    RK_U32  TRD[2];

    union {
        struct {
            RK_U16  unPicPostProc                 : 2;
            RK_U16  interlaced                    : 1;
            RK_U16  quant_type                    : 1;
            RK_U16  quarter_sample                : 1;
            RK_U16  resync_marker_disable         : 1;
            RK_U16  data_partitioned              : 1;
            RK_U16  reversible_vlc                : 1;
            RK_U16  reduced_resolution_vop_enable : 1;
            RK_U16  vop_coded                     : 1;
            RK_U16  vop_rounding_type             : 1;
            RK_U16  intra_dc_vlc_thr              : 3;
            RK_U16  top_field_first               : 1;
            RK_U16  alternate_vertical_scan_flag  : 1;
        };
        RK_U16 wPicFlagBitFields;
    };
    RK_U8   profile_and_level_indication;
    RK_U8   video_object_layer_verid;
    RK_U16  vop_width;
    RK_U16  vop_height;
    union {
        struct {
            RK_U16  sprite_enable               : 2;
            RK_U16  no_of_sprite_warping_points : 6;
            RK_U16  sprite_warping_accuracy     : 2;
        };
        RK_U16 wSpriteBitFields;
    };
    RK_S16  warping_mv[4][2];
    union {
        struct {
            RK_U8  vop_fcode_forward   : 3;
            RK_U8  vop_fcode_backward  : 3;
        };
        RK_U8 wFcodeBitFields;
    };
    RK_U16  StatusReportFeedbackNumber;
    RK_U16  Reserved16BitsA;
    RK_U16  Reserved16BitsB;

    // FIXME: added for rockchip hardware information
    RK_U32  prev_coding_type;
    RK_U32  header_bits;
};

#define RKVDECH263_DATA_SIZE             (1408 * 1152) 


#define ALIGN(value, x) ((value + (x - 1)) & (~(x - 1)))

/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECH263Context *ff_rkvdec_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdec_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}

int h263_skip_picture_header(MpegEncContext *s)
{
    int n=50;

    return n;
}


static void fill_picture_parameters(const MpegEncContext *h, LPRKVDEC_PicParams_H263 pp)
{
    pp->short_video_header = 1;
    pp->vop_coding_type = h->pict_type;
    pp->vop_quant = h->qscale;
    pp->vop_time_increment_resolution = 30000;
    pp->vop_width = h->width;
    pp->vop_height = h->height;
    pp->prev_coding_type = AV_PICTURE_TYPE_NONE;
    if (h->last_picture_ptr){	
        pp->prev_coding_type = h->last_picture_ptr->f->pict_type;
    }
}


static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECH263Context * const ctx = ff_rkvdec_get_context(avctx);
    unsigned char *data_ptr = ctx->stream_data->data[0];

    if (data_ptr && ctx->stream_data->linesize[0] > size) {
        memcpy(data_ptr , buffer, size);
        ctx->stream_data->pkt_size = size ;
    } else {
        av_log(avctx, AV_LOG_ERROR, "fill_stream_data err!");
    }
}



static int rkvdec_h263_regs_gen_reg(AVCodecContext *avctx)
{
    RKVDECH263Context * const ctx = ff_rkvdec_get_context(avctx);	 
    MpegEncContext * const h = avctx->priv_data;
    LPRKVDEC_PicParams_H263 pp = ctx->pic_param;
    LPRKVDEC_H263_Regs regs = ctx->hw_regs;
    RK_U32 stream_length = 0;
    RK_U32 stream_used = 0;
    av_log(avctx, AV_LOG_INFO, "rkvdec_h263_regs_gen_reg");
    
    memset(regs,0,sizeof(*regs));
    regs->reg54_endian.sw_dec_out_endian = 1;
    regs->reg54_endian.sw_dec_in_endian = 1;
    regs->reg54_endian.sw_dec_inswap32_e = 1;
    regs->reg54_endian.sw_dec_outswap32_e = 1;
    regs->reg54_endian.sw_dec_strswap32_e = 1;
    regs->reg54_endian.sw_dec_strendian_e = 1;
    regs->reg56_axi_ctrl.sw_dec_max_burst = 16;
    regs->reg52_error_concealment.sw_apf_threshold = 1;
    regs->reg57_enable_ctrl.sw_dec_timeout_e = 1;
    regs->reg57_enable_ctrl.sw_dec_clk_gate_e = 1;
    regs->reg57_enable_ctrl.sw_dec_e = 1;
    regs->reg59.sw_pred_bc_tap_0_0 = -1;
    regs->reg59.sw_pred_bc_tap_0_1 = 3;
    regs->reg59.sw_pred_bc_tap_0_2 = -6;
    regs->reg153.sw_pred_bc_tap_0_3 = 20;

    regs->reg63_cur_pic_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
    regs->reg64_input_stream_base = ff_rkvdec_get_fd(ctx->stream_data);
		
    regs->reg120.sw_pic_mb_width = (pp->vop_width  + 15) >> 4;
    regs->reg120.sw_pic_mb_hight_p = (pp->vop_height + 15) >> 4;
    regs->reg120.sw_mb_width_off = pp->vop_width & 0xf;
    regs->reg120.sw_mb_height_off = pp->vop_height & 0xf;

    regs->reg53_dec_mode = 2;
    regs->reg50_dec_ctrl.sw_filtering_dis = 1;
    regs->reg136.sw_rounding = 0;
    regs->reg51_stream_info.sw_init_qp = pp->vop_quant;
    regs->reg122.sw_sync_markers_en = 1;

    stream_length=ctx->stream_data->pkt_size;
    stream_used=  h263_skip_picture_header(h);

    //update stream base address
    RK_U32 val = regs->reg64_input_stream_base;
    RK_U32 consumed_bytes = stream_used >> 3;
    RK_U32 consumed_bytes_align = consumed_bytes & (~0x7);
    RK_U32 start_bit_offset = stream_used & 0x3F;
    RK_U32 left_bytes = stream_length - consumed_bytes_align;

    val += (consumed_bytes_align << 10);
    regs->reg64_input_stream_base = val;
    regs->reg122.sw_stream_start_word = start_bit_offset;
    regs->reg51_stream_info.sw_stream_len = left_bytes;

    regs->reg122.sw_vop_time_incr = pp->vop_time_increment_resolution;
	
    switch (pp->vop_coding_type) {
    case AV_PICTURE_TYPE_P : {
        regs->reg57_enable_ctrl.sw_pic_inter_e = 1;

        if (ff_rkvdec_get_fd(h->last_picture_ptr->f)>= 0) {
            regs->reg131_ref0_base = ff_rkvdec_get_fd(h->last_picture_ptr->f);
            regs->reg148_ref1_base = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        } else {
            regs->reg131_ref0_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
            regs->reg148_ref1_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        }
    } break;
    case AV_PICTURE_TYPE_I : {
        regs->reg57_enable_ctrl.sw_pic_inter_e = 0;

        regs->reg131_ref0_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        regs->reg148_ref1_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
    } break;
    default : {
        /* no nothing */
    } break;
    }	

    regs->reg136.sw_hrz_bit_of_fwd_mv = 1;
    regs->reg136.sw_vrz_bit_of_fwd_mv = 1;
    regs->reg136.sw_prev_pic_type = (pp->prev_coding_type == AV_PICTURE_TYPE_P);
	
#ifdef debug_regs
    unsigned char *p = regs;
    for (int i = 0; i < 159 ; i++) {
        av_log(avctx, AV_LOG_INFO, "RK_H263H_DEC: regs[%02d]=%08X\n", i, *((unsigned int*)p));
	p += 4;
    }
#endif

    return 0;
}

/** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_h263_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    RKVDECH263Context * const ctx = ff_rkvdec_get_context(avctx);
    MpegEncContext * const h = avctx->priv_data;
    
    av_log(avctx, AV_LOG_INFO, "RK_H263_DEC: rkvdec_h263_start_frame\n");
    fill_picture_parameters(h, ctx->pic_param);
    ctx->stream_data->pkt_size = 0;

    return 0;
}

/** End a hardware decoding based frame. */
static int rkvdec_h263_end_frame(AVCodecContext *avctx)
{
    RKVDECH263Context * const ctx = ff_rkvdec_get_context(avctx);
    MpegEncContext * const h = avctx->priv_data;
    RKVDECH263HwReq req;
    int ret;

    av_log(avctx, AV_LOG_INFO, "RK_H263_DEC: rkvdec_h263_end_frame\n");
    rkvdec_h263_regs_gen_reg(avctx);

    req.req = (unsigned int*)ctx->hw_regs;
    req.size = sizeof(*ctx->hw_regs);

    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_SET_REG start.");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d\n", ret);

    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_GET_REG start.");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);
    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_GET_REG success.");
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d\n", ret);

#ifdef dump
    if (fp2 == NULL)
       fp2 = fopen("hal.bin", "wb");
    fwrite(h->current_picture_ptr->f->data[0],1, 352*288*1.5, fp2);
    fflush(fp2);
#endif
 
    h->current_picture_ptr->f->pict_type=h->pict_type;
 

   return 0;
}


/** Decode the given h263 slice with RKVDEC. */
static int rkvdec_h263_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{    
    av_log(avctx, AV_LOG_INFO, "RK_H263_DEC: rkvdec_h263_decode_slice size:%d\n", size);
    fill_stream_data(avctx, buffer, size);
    return 0;
}

static int rkvdec_h263_context_init(AVCodecContext *avctx)
{
    RKVDECH263Context * const ctx = ff_rkvdec_get_context(avctx);
    int ret;
    
    av_log(avctx, AV_LOG_INFO, "RK_H263_DEC: rkvdec_h263_context_init\n");
    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }


    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_H263_Regs));
    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_H263));  
	
    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDECH263_DATA_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);
	
    if (ctx->vpu_socket <= 0) 
        ctx->vpu_socket = open(name_rkvdec, O_RDWR);
	
    if (ctx->vpu_socket < 0) {
        av_log(avctx, AV_LOG_ERROR, "failed to open rkvdec.");
	return -1;
    }

    if(ioctl(ctx->vpu_socket, VPU_IOC_SET_CLIENT_TYPE, 0x1)) {
        if (ioctl(ctx->vpu_socket, VPU_IOC_SET_CLIENT_TYPE_U32, 0x1)) {
            av_log(avctx, AV_LOG_ERROR, "failed to ioctl rkvdec.");
            return -1;
        }
    }

    return 0;
}

static int rkvdec_h263_context_uninit(AVCodecContext *avctx)
{
    RKVDECH263Context * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, AV_LOG_INFO, "RK_H263_DEC: rkvdec_h263_context_uninit\n");
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
    av_free(ctx->stream_data);
    av_free(ctx->pic_param);
    av_free(ctx->hw_regs);
    ctx->allocator->close(ctx->allocator_ctx);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }
    
    return 0;
}

AVHWAccel ff_h263_rkvdec_hwaccel = {
    .name                 = "h263_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_H263,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .start_frame          = rkvdec_h263_start_frame,
    .end_frame            = rkvdec_h263_end_frame,
    .decode_slice         = rkvdec_h263_decode_slice,
    .init                 = rkvdec_h263_context_init,
    .uninit               = rkvdec_h263_context_uninit,
    .priv_data_size       = sizeof(RKVDECH263Context),
};








