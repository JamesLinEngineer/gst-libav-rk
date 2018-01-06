/*
 * Copyright 2017 Rockchip Electronics Co., Ltd
 *     Author: dawnming.huang<dawnming.huang@rock-chips.com>
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

#include "mpeg4video.h"
#include "rkvdec_mpeg4.h"
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"
#include "hwaccel.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>

FILE* fpMPeg4=NULL;
int pcount =0;
#define RKVDECMPEG4_DATA_SIZE        (1408 * 1152) 
#define MPEG4_MAX_MV_BUF_SIZE        ((1920/16)*(1088/16)*4*sizeof(RK_U32))
#define ALIGN(value, x) ((value + (x - 1)) & (~(x - 1)))
#define MPEG4_BUF_SIZE_QPTAB          256
typedef struct _RKVDECMpeg4Context RKVDECMpeg4Context;
typedef struct Mpeg4dRkvRegs_t RKVDEC_Mpeg4_Regs, *LPRKVDEC_Mpeg4_Regs;
typedef struct _RKVDEC_PicParams_Mpeg4 RKVDEC_PicParams_Mpeg4, *LPRKVDEC_PicParams_Mpeg4;
typedef struct _RKVDECMpeg4HwReq RKVDECMpeg4HwReq;
int haloutsize;
struct _RKVDECMpeg4Context{
     signed int vpu_socket;
     LPRKVDEC_Mpeg4_Regs hw_regs;
     LPRKVDEC_PicParams_Mpeg4 pic_param;
     AVFrame* stream_data;
     AVFrame* qp_table;
     AVFrame* mv_buf;
     os_allocator *allocator;
     uint32_t  buf_size;
     void *allocator_ctx;
     pthread_mutex_t hwaccel_mutex;
};



struct _RKVDECMpeg4HwReq {
   unsigned int *req;
   unsigned int  size;
} ;

/* MPEG4PT2 Picture Parameter structure */
struct _RKVDEC_PicParams_Mpeg4 {
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
    RK_U8   *qp_tab;
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
    RK_U32  custorm_version;
    RK_U32  prev_coding_type;
    RK_U32  time_bp;
    RK_U32  time_pp;
    RK_U32  header_bits;
};



/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECMpeg4Context *ff_rkvdec_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdec_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}


static void fill_picture_parameters(const Mpeg4DecContext *ctxf, LPRKVDEC_PicParams_Mpeg4 pp)
{
    MpegEncContext *h = &ctxf->m;
    pp->short_video_header = 0;
    pp->vop_coding_type = h->pict_type;
    pp->vop_quant = h->qscale;
    pp->vop_time_increment_resolution = h->avctx->framerate.num;
    pp->TRB[0] = 0; // FIXME:
    pp->TRD[0] = 0; // FIXME:
    pp->unPicPostProc = 0; // FIXME:
    pp->interlaced = h->interlaced_dct;
    pp->quant_type = h->mpeg_quant;
    pp->quarter_sample = h->quarter_sample;
    pp->resync_marker_disable = 0;// FIXME:
    pp->data_partitioned = 0; // FIXME:
    pp->reversible_vlc = 0; // FIXME:
    pp->reduced_resolution_vop_enable = 0;
    pp->vop_coded = (h->pict_type != AV_PICTURE_TYPE_S);
    pp->vop_rounding_type = h->no_rounding;
    pp->intra_dc_vlc_thr = 0; // h->intra_dc_threshold;
    pp->top_field_first = h->top_field_first;
    pp->alternate_vertical_scan_flag = h->alternate_scan;
    pp->vop_width = h->width;
    pp->vop_height = h->height;
    pp->sprite_enable = 0;
    pp->no_of_sprite_warping_points = 0; // FIXME: 
    pp->sprite_warping_accuracy = 0; // FIXME: 
    memset(pp->warping_mv, 0, sizeof(pp->warping_mv));
    pp->vop_fcode_forward = h->f_code;
    pp->vop_fcode_backward = h->b_code;
    if(pp->vop_coding_type == AV_PICTURE_TYPE_I){
       pp->vop_fcode_forward = 0;

    } 
    if(pp->vop_coding_type != AV_PICTURE_TYPE_B){
       pp->vop_fcode_backward = 0;
    }

    pp->StatusReportFeedbackNumber = 0; // FIXME: 
    pp->Reserved16BitsA = 0; // FIXME:
    pp->Reserved16BitsB = 0; // FIXME:
    haloutsize = h->width *  h->height * 1.5;
    // Rockchip special data
    pp->prev_coding_type = AV_PICTURE_TYPE_NONE;
    pp->time_bp = h->pb_time;
    pp->time_pp = h->pp_time;
    if(ctxf->divx_version == 400){
        pp->custorm_version = 4;
    }
    if (h->next_picture_ptr && pp->vop_coding_type == AV_PICTURE_TYPE_B){	
        
        pp->prev_coding_type = h->next_picture_ptr->f->pict_type-1;
    }
    if (h->last_picture_ptr && pp->vop_coding_type == AV_PICTURE_TYPE_P){	
        
        pp->prev_coding_type = h->last_picture_ptr->f->pict_type-1;
    }
    if(h->last_picture_ptr){
        if((h->last_picture_ptr->f->decode_error_flags && pp->vop_coding_type != AV_PICTURE_TYPE_I )){
          av_log(NULL, AV_LOG_INFO, "fill_picture_parameters missing reference");   
          h->current_picture_ptr->f->decode_error_flags = FF_DECODE_ERROR_MISSING_REFERENCE;
        }
    }
    RK_U8 *dst_tab=pp->qp_tab;
    // intra
    for (int i = 0; i < 64 ; i++) {
        *dst_tab=((RK_U8) h->intra_matrix[i])? ((RK_U8) h->intra_matrix[i]) :(default_intra_matrix[i]) ;
         dst_tab++;
    }
    // inter
    for (int i = 0; i < 64 ; i++) {
        *dst_tab= ((RK_U8) h->inter_matrix[i])? ((RK_U8) h->inter_matrix[i]) : (default_inter_matrix[i]);
         dst_tab++;
    }
    
}


static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECMpeg4Context * const ctx = ff_rkvdec_get_context(avctx);
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset = ctx->stream_data->pkt_size;
    unsigned int left_size = ctx->stream_data->linesize[0] - offset;
    if (data_ptr && left_size > size) {
        memcpy(data_ptr+offset , buffer, size);
        ctx->stream_data->pkt_size += size ;
    } else {
        av_log(avctx, AV_LOG_ERROR, "fill_stream_data err!");
    }
    ctx->buf_size = ctx->stream_data->pkt_size;
}



static int rkvdec_mpeg4_regs_gen_reg(AVCodecContext *avctx)
{
    RKVDECMpeg4Context * const ctx = ff_rkvdec_get_context(avctx);	 
    MpegEncContext * const h = avctx->priv_data;
    LPRKVDEC_PicParams_Mpeg4 pp = ctx->pic_param;
    LPRKVDEC_Mpeg4_Regs regs = ctx->hw_regs;
    RK_U32 stream_length = 0;
    RK_U32 stream_used = 0;
    int mv_buf_fd = ff_rkvdec_get_fd(ctx->mv_buf);
    stream_length = ctx->buf_size;
    stream_used = h->gb.index;
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
    if (pp->custorm_version == 4) {
        regs->reg120.sw_mb_width_off = pp->vop_width & 0xf;
        regs->reg120.sw_mb_height_off = pp->vop_height & 0xf;
    } else {
        regs->reg120.sw_mb_width_off = 0;
        regs->reg120.sw_mb_height_off = 0;
    }

    regs->reg53_dec_mode = 1;
    regs->reg120.sw_alt_scan_e = pp->alternate_vertical_scan_flag;
    regs->reg52_error_concealment.sw_startmb_x = 0;
    regs->reg52_error_concealment.sw_startmb_y = 0;
    regs->reg50_dec_ctrl.sw_filtering_dis = 1;
    regs->reg136.sw_rounding = pp->vop_rounding_type;
    regs->reg122.sw_intradc_vlc_thr = pp->intra_dc_vlc_thr;
    regs->reg51_stream_info.sw_init_qp = pp->vop_quant;
    regs->reg122.sw_sync_markers_en = 1;
    {

        RK_U32 val = regs->reg64_input_stream_base;
        RK_U32 consumed_bytes = stream_used >> 3;
        RK_U32 consumed_bytes_align = consumed_bytes & (~0x7);
        RK_U32 start_bit_offset = stream_used & 0x3F;
        RK_U32 left_bytes = stream_length - consumed_bytes_align;

        val += (consumed_bytes_align << 10);
        regs->reg64_input_stream_base = val;
        regs->reg122.sw_stream_start_word = start_bit_offset;
        regs->reg51_stream_info.sw_stream_len = left_bytes;
    }
    regs->reg122.sw_vop_time_incr = pp->vop_time_increment_resolution;
    switch (pp->vop_coding_type) {
    case AV_PICTURE_TYPE_B : {
        RK_U32 time_bp = pp->time_bp;
        RK_U32 time_pp = pp->time_pp;

        RK_U32 trb_per_trd_d0  = ((((RK_S64)(1 * time_bp + 0)) << 27) + 1 * (time_pp - 1)) / time_pp;
        RK_U32 trb_per_trd_d1  = ((((RK_S64)(2 * time_bp + 1)) << 27) + 2 * (time_pp - 0)) / (2 * time_pp + 1);
        RK_U32 trb_per_trd_dm1 = ((((RK_S64)(2 * time_bp - 1)) << 27) + 2 * (time_pp - 1)) / (2 * time_pp - 1);
        regs->reg57_enable_ctrl.sw_pic_b_e = 1;
        regs->reg57_enable_ctrl.sw_pic_inter_e = 1;
        regs->reg136.sw_rounding = 0;
        regs->reg131_ref0_base = 1;

        if ( ff_rkvdec_get_fd(h->last_picture_ptr->f) >= 0 ) {
            regs->reg131_ref0_base =  ff_rkvdec_get_fd(h->last_picture_ptr->f);
            regs->reg148_ref1_base = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        } else {
            regs->reg131_ref0_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
            regs->reg148_ref1_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        }
        if (ff_rkvdec_get_fd(h->next_picture_ptr->f) >= 0) {
            regs->reg134_ref2_base = ff_rkvdec_get_fd(h->next_picture_ptr->f);
            regs->reg135_ref3_base = ff_rkvdec_get_fd(h->next_picture_ptr->f);
        } else {
            regs->reg134_ref2_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
            regs->reg135_ref3_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        }

        regs->reg136.sw_hrz_bit_of_fwd_mv = pp->vop_fcode_forward;
        regs->reg136.sw_vrz_bit_of_fwd_mv = pp->vop_fcode_forward;
        regs->reg136.sw_hrz_bit_of_bwd_mv = pp->vop_fcode_backward;
        regs->reg136.sw_vrz_bit_of_bwd_mv = pp->vop_fcode_backward;
        regs->reg57_enable_ctrl.sw_write_mvs_e = 0;
        regs->reg62_directmv_base = mv_buf_fd;
        regs->reg137.sw_trb_per_trd_d0 = trb_per_trd_d0;
        regs->reg139.sw_trb_per_trd_d1 = trb_per_trd_d1;
        regs->reg138.sw_trb_per_trd_dm1 = trb_per_trd_dm1;
    } break;
    case AV_PICTURE_TYPE_P : {
        regs->reg57_enable_ctrl.sw_pic_b_e = 0;
        regs->reg57_enable_ctrl.sw_pic_inter_e = 1;
        if (ff_rkvdec_get_fd(h->last_picture_ptr->f) >= 0) {
            regs->reg131_ref0_base = ff_rkvdec_get_fd(h->last_picture_ptr->f);
            regs->reg148_ref1_base = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        } else {
            regs->reg131_ref0_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
            regs->reg148_ref1_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        }
        regs->reg134_ref2_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        regs->reg135_ref3_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);

        regs->reg136.sw_hrz_bit_of_fwd_mv = pp->vop_fcode_forward;
        regs->reg136.sw_vrz_bit_of_fwd_mv = pp->vop_fcode_forward;
        regs->reg57_enable_ctrl.sw_write_mvs_e = 1;
        regs->reg62_directmv_base = mv_buf_fd;
    } break;
    case AV_PICTURE_TYPE_I : {
        regs->reg57_enable_ctrl.sw_pic_b_e = 0;
        regs->reg57_enable_ctrl.sw_pic_inter_e = 0;

        regs->reg131_ref0_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        regs->reg148_ref1_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        regs->reg134_ref2_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        regs->reg135_ref3_base = ff_rkvdec_get_fd(h->current_picture_ptr->f);

        regs->reg57_enable_ctrl.sw_write_mvs_e = 0;
        regs->reg62_directmv_base = mv_buf_fd;

        regs->reg136.sw_hrz_bit_of_fwd_mv = 1;
        regs->reg136.sw_vrz_bit_of_fwd_mv = 1;
    } break;
    default : {
        // no nothing 
    } break;
    }
    if (pp->interlaced) {
        regs->reg57_enable_ctrl.sw_pic_interlace_e = 1;
        regs->reg57_enable_ctrl.sw_pic_fieldmode_e = 0;
        regs->reg120.sw_topfieldfirst_e = pp->top_field_first;
    }

    regs->reg136.sw_prev_pic_type = pp->prev_coding_type;
    regs->reg122.sw_quant_type_1_en = pp->quant_type;
    memcpy(ctx->qp_table->data[0], pp->qp_tab, MPEG4_BUF_SIZE_QPTAB);
    regs->reg61_qtable_base = ff_rkvdec_get_fd(ctx->qp_table);
    regs->reg136.sw_fwd_mv_y_resolution = pp->quarter_sample;

#if 0
    unsigned char *p = regs;
    for (int i = 0; i < 159 ; i++) {
        av_log(avctx, AV_LOG_INFO, "RK_Mpeg4H_DEC: regs[%02d]=%08X\n", i, *((unsigned int*)p));
	p += 4;
    }
#endif
    return 0;
}

/** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_mpeg4_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    RKVDECMpeg4Context * const ctx = ff_rkvdec_get_context(avctx);    
    pthread_mutex_lock(&ctx->hwaccel_mutex);
    Mpeg4DecContext * const ctxf = avctx->priv_data;
    fill_picture_parameters(ctxf, ctx->pic_param);
    ctx->stream_data->pkt_size = 0;

    return 0;
}

/** End a hardware decoding based frame. */
static int rkvdec_mpeg4_end_frame(AVCodecContext *avctx)
{
    RKVDECMpeg4Context * const ctx = ff_rkvdec_get_context(avctx);
    MpegEncContext * const h = avctx->priv_data;
    RKVDECMpeg4HwReq req;
    int ret;
    rkvdec_mpeg4_regs_gen_reg(avctx);
    req.req = (unsigned int*)ctx->hw_regs;
    req.size = sizeof(*ctx->hw_regs);
    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d\n", ret);
    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);
    pthread_mutex_unlock(&ctx->hwaccel_mutex);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d\n", ret);
#if 0
    RK_U32 i = 0;
    for (i = 0; i < 159; i++) {
        av_log(NULL,AV_LOG_INFO, "RK_Mpeg4_DEC_GET reg[%03d]: %08x\n", i, req.req[i]);
    }
#endif

#if 0
    if (fpMPeg4 == NULL)
       fpMPeg4 = fopen("hal.bin", "wb");
    av_log(avctx, AV_LOG_ERROR, "write picture %d\n", pcount);
    pcount++;
    fwrite(h->current_picture_ptr->f->data[0],1, haloutsize, fpMPeg4);
    fflush(fpMPeg4);
#endif
 
    h->current_picture_ptr->f->pict_type=h->pict_type;
 
   return 0;
}


/** Decode the given Mpeg4 slice with RKVDEC. */
static int rkvdec_mpeg4_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{    
    fill_stream_data(avctx, buffer, size);
    return 0;
}

static int rkvdec_mpeg4_context_init(AVCodecContext *avctx)
{
    RKVDECMpeg4Context * const ctx = ff_rkvdec_get_context(avctx);
    int ret;
    
    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }


    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_Mpeg4_Regs));
    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_Mpeg4));  
	
    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDECMPEG4_DATA_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);
	

    ctx->qp_table = av_frame_alloc();
    ctx->qp_table->linesize[0] = MPEG4_BUF_SIZE_QPTAB;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->qp_table);
    ctx->pic_param->qp_tab=av_mallocz(256);

    ctx->mv_buf = av_frame_alloc();
    ctx->mv_buf->linesize[0] = MPEG4_MAX_MV_BUF_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->mv_buf);
    
    pthread_mutex_init(&ctx->hwaccel_mutex, NULL);
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

static int rkvdec_mpeg4_context_uninit(AVCodecContext *avctx)
{
    RKVDECMpeg4Context * const ctx = ff_rkvdec_get_context(avctx);
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);    
    av_free(ctx->hw_regs);    
    av_free(ctx->pic_param);    
    av_free(ctx->qp_table);
    av_free(ctx->stream_data);
    av_free(ctx->mv_buf);
    ctx->allocator->close(ctx->allocator_ctx);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }
    pthread_mutex_destroy(&ctx->hwaccel_mutex);
    return 0;
}

AVHWAccel ff_mpeg4_rkvdec_hwaccel = {
    .name                 = "mpeg4_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG4,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .start_frame          = rkvdec_mpeg4_start_frame,
    .end_frame            = rkvdec_mpeg4_end_frame,
    .decode_slice         = rkvdec_mpeg4_decode_slice,
    .init                 = rkvdec_mpeg4_context_init,
    .uninit               = rkvdec_mpeg4_context_uninit,
    .priv_data_size       = sizeof(RKVDECMpeg4Context),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};








