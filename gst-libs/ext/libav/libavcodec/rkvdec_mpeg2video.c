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

#include "mpeg12.h"
#include "libavutil/stereo3d.h"

#include "rkvdec_mpeg2video.h"
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

FILE* fp3=NULL;
typedef struct _RKVDECMpeg2videoContext RKVDECMpeg2videoContext;
typedef struct Mpeg2videodRkvRegs_t RKVDEC_Mpeg2video_Regs, *LPRKVDEC_Mpeg2video_Regs;
typedef struct _RKVDEC_PicParams_Mpeg2video RKVDEC_PicParams_Mpeg2video, *LPRKVDEC_PicParams_Mpeg2video;
typedef struct _RKVDECMpeg2videoHwReq RKVDECMpeg2videoHwReq;


struct _RKVDECMpeg2videoContext{
     signed int vpu_socket;
     int mpeg2_flag;
     LPRKVDEC_Mpeg2video_Regs hw_regs;
     LPRKVDEC_PicParams_Mpeg2video pic_param;
     AVFrame* qp_table;
     AVFrame* stream_data;
     os_allocator *allocator;
     void *allocator_ctx;
};

typedef struct Mpeg1Context {
    MpegEncContext mpeg_enc_ctx;
    int mpeg_enc_ctx_allocated; /* true if decoding context allocated */
    int repeat_field;           /* true if we must repeat the field */
    AVPanScan pan_scan;         /* some temporary storage for the panscan */
    AVStereo3D stereo3d;
    int has_stereo3d;
    uint8_t *a53_caption;
    int a53_caption_size;
    uint8_t afd;
    int has_afd;
    int slice_count;
    AVRational save_aspect;
    int save_width, save_height, save_progressive_seq;
    AVRational frame_rate_ext;  /* MPEG-2 specific framerate modificator */
    int sync;                   /* Did we reach a sync point like a GOP/SEQ/KEYFrame? */
    int tmpgexs;
    int first_slice;
    int extradata_decoded;
} Mpeg1Context;

struct _RKVDECMpeg2videoHwReq {
   unsigned int *req;
   unsigned int  size;
} ;


/* Mpeg2video MVC picture parameters structure */
struct _RKVDEC_PicParams_Mpeg2video {
    RK_U32              bitstream_length;
    RK_U32              bitstream_start_bit;
    RK_U32              bitstream_offset;
    RK_U8               *qp_tab;

    DXVA_PicEntry_M2V   CurrPic;
    DXVA_PicEntry_M2V   frame_refs[4];

    RK_U32              seq_ext_head_dec_flag;

    M2VDDxvaSeq         seq;
    M2VDDxvaSeqExt      seq_ext;
    M2VDDxvaGop         gop;
    M2VDDxvaPic         pic;
    M2VDDxvaSeqDispExt  seq_disp_ext;
    M2VDDxvaPicCodeExt  pic_code_ext;
    M2VDDxvaPicDispExt  pic_disp_ext;
};

#define RKVDECMPEG2VIDEO_DATA_SIZE             (512 * 1024)
#define M2VD_BUF_SIZE_QPTAB                    (256)

#define ALIGN(value, x) ((value + (x - 1)) & (~(x - 1)))


/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECMpeg2videoContext *ff_rkvdec_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdec_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}


static void fill_picture_parameters(const MpegEncContext *p, LPRKVDEC_PicParams_Mpeg2video pp)
{
    LPRKVDEC_PicParams_Mpeg2video dst = pp;

    dst->seq.decode_width                       = p->width;
    dst->seq.decode_height                      = p->height;
    dst->seq_ext.progressive_sequence           = p->progressive_sequence;
    dst->pic.picture_coding_type                = p->pict_type;
    dst->pic.full_pel_forward_vector            = p->mpeg_f_code[0][0];
    dst->pic.forward_f_code                     = p->mpeg_f_code[0][1];
    dst->pic.full_pel_backward_vector           = p->mpeg_f_code[1][0];
    dst->pic.backward_f_code                    = p->mpeg_f_code[1][1];
    dst->pic_code_ext.intra_dc_precision            = p->intra_dc_precision;
    dst->pic_code_ext.picture_structure             = p->picture_structure;
    dst->pic_code_ext.top_field_first               = p->top_field_first;
    dst->pic_code_ext.frame_pred_frame_dct          = p->frame_pred_frame_dct;
    dst->pic_code_ext.concealment_motion_vectors    = p->concealment_motion_vectors;
    dst->pic_code_ext.q_scale_type                  = p->q_scale_type;
    dst->pic_code_ext.intra_vlc_format              = p->intra_vlc_format;
    dst->pic_code_ext.alternate_scan                = p->alternate_scan;

    RK_U8 *p_tab = dst->qp_tab;
    for (int i = 0; i < 64 ; i++) {
        *p_tab=(RK_U8) p->intra_matrix[i];
        p_tab++;
    }
    for (int i = 0; i < 64 ; i++) {
        *p_tab=(RK_U8) p->inter_matrix[i];
        p_tab++;
    }
}

static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECMpeg2videoContext * const ctx = ff_rkvdec_get_context(avctx);
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset = ctx->stream_data->pkt_size;
    unsigned int left_size = ctx->stream_data->linesize[0] - offset;

    if (data_ptr && left_size < size ) {
        AVFrame* new_pkt = av_frame_alloc();
        new_pkt->linesize[0] = offset + size + RKVDECMPEG2VIDEO_DATA_SIZE;
        ctx->allocator->alloc(ctx->allocator_ctx, new_pkt);
        new_pkt->pkt_size = offset;
        memcpy(new_pkt->data[0], ctx->stream_data->data[0], ctx->stream_data->pkt_size);
        ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
        memcpy(ctx->stream_data, new_pkt,sizeof(AVFrame));
        data_ptr = ctx->stream_data->data[0];
        av_free(new_pkt);
    }

    memcpy(data_ptr + offset, buffer, size);
    ctx->stream_data->pkt_size += size;

    av_log(avctx, AV_LOG_INFO, "fill_stream_data pkg_size %d size %d.\n", ctx->stream_data->pkt_size, size);
}



static int rkvdec_mpeg2video_regs_gen_reg(AVCodecContext *avctx)
{
    RKVDECMpeg2videoContext * const ctx = ff_rkvdec_get_context(avctx);
    Mpeg1Context *s  = avctx->priv_data;
    MpegEncContext *h = &s->mpeg_enc_ctx;
    LPRKVDEC_PicParams_Mpeg2video dx = ctx->pic_param;
    LPRKVDEC_Mpeg2video_Regs reg = ctx->hw_regs;

    av_log(avctx, AV_LOG_INFO, "rkvdec_Mpeg2video_regs_gen_reg");

    memset(ctx->hw_regs, 0, sizeof(RKVDEC_Mpeg2video_Regs));
    reg->config3.sw_dec_axi_rn_id = 0;
    reg->control.sw_dec_timeout_e = 1;
    reg->config2.sw_dec_strswap32_e = 1;
    reg->config2.sw_dec_strendian_e = 1;
    reg->config2.sw_dec_inswap32_e = 1;
    reg->config2.sw_dec_outswap32_e = 1;
    reg->control.sw_dec_clk_gate_e = 1;
    reg->config2.sw_dec_in_endian = 1;
    reg->config2.sw_dec_out_endian = 1;
    reg->config1.sw_dec_out_tiled_e = 0;
    reg->config3.sw_dec_max_burst = 16;
    reg->config1.sw_dec_scmd_dis = 0;
    reg->config1.sw_dec_adv_pre_dis = 0;
    reg->error_position.sw_apf_threshold = 8;
    reg->config1.sw_dec_latency = 0;
    reg->config3.sw_dec_data_disc_e  = 0;
    reg->interrupt.sw_dec_irq = 0;
    reg->config3.sw_dec_axi_rn_id = 0;
    reg->config3.sw_dec_axi_wr_id = 0;
    reg->sw_dec_mode = 8;

    reg->dec_info.sw_mv_accuracy_fwd = 1;
    reg->dec_info.sw_mv_accuracy_bwd = 1;

    ctx->mpeg2_flag=1;
    if (ctx->mpeg2_flag) {
       reg->sw_dec_mode = 5;
       reg->dec_info.sw_fcode_fwd_hor = dx->pic.full_pel_forward_vector;
       reg->dec_info.sw_fcode_fwd_ver = dx->pic.forward_f_code;
       reg->dec_info.sw_fcode_bwd_hor = dx->pic.full_pel_backward_vector;
       reg->dec_info.sw_fcode_bwd_ver = dx->pic.backward_f_code;
    } else {
       reg->sw_dec_mode = 6;
       reg->dec_info.sw_fcode_fwd_hor = dx->pic.forward_f_code;
       reg->dec_info.sw_fcode_fwd_ver = dx->pic.forward_f_code;
       reg->dec_info.sw_fcode_bwd_hor = dx->pic.backward_f_code;
       reg->dec_info.sw_fcode_bwd_ver = dx->pic.backward_f_code;
       if (dx->pic.full_pel_forward_vector)
          reg->dec_info.sw_mv_accuracy_fwd = 0;
       if (dx->pic.full_pel_backward_vector)
          reg->dec_info.sw_mv_accuracy_bwd = 0;
    }

    reg->pic_params.sw_pic_mb_width = (dx->seq.decode_width + 15) >> 4;
    reg->pic_params.sw_pic_mb_height_p = (dx->seq.decode_height + 15) >> 4;
    reg->control.sw_pic_interlace_e = 1 - dx->seq_ext.progressive_sequence;
    if (dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_FRAME)
        reg->control.sw_pic_fieldmode_e = 0;
    else {
        reg->control.sw_pic_fieldmode_e = 1;
        reg->control.sw_pic_topfield_e = dx->pic_code_ext.picture_structure == 1;
    }
    if (dx->pic.picture_coding_type == M2VD_CODING_TYPE_B)
        reg->control.sw_pic_b_e = 1;
    else
        reg->control.sw_pic_b_e = 0;
    if (dx->pic.picture_coding_type == M2VD_CODING_TYPE_I)
        reg->control.sw_pic_inter_e = 0;
    else
        reg->control.sw_pic_inter_e = 1;

    reg->pic_params.sw_topfieldfirst_e = dx->pic_code_ext.top_field_first;
    reg->control.sw_fwd_interlace_e = 0;
    reg->control.sw_write_mvs_e = 0;//concealment_motion_vectors;
    reg->pic_params.sw_alt_scan_e = dx->pic_code_ext.alternate_scan;
    reg->dec_info.sw_alt_scan_flag_e = dx->pic_code_ext.alternate_scan;

    reg->stream_bitinfo.sw_qscale_type = dx->pic_code_ext.q_scale_type;
    reg->stream_bitinfo.sw_intra_dc_prec = dx->pic_code_ext.intra_dc_precision;
    reg->stream_bitinfo.sw_con_mv_e = dx->pic_code_ext.concealment_motion_vectors;
    reg->stream_bitinfo.sw_intra_vlc_tab = dx->pic_code_ext.intra_vlc_format;
    reg->stream_bitinfo.sw_frame_pred_dct = dx->pic_code_ext.frame_pred_frame_dct;
    reg->stream_buffinfo.sw_init_qp = 1;

    reg->error_position.sw_startmb_x = 0;
    reg->error_position.sw_startmb_y = 0;
    reg->control.sw_dec_out_dis = 0;
    reg->config1.sw_filtering_dis = 1;

    reg->stream_buffinfo.sw_stream_len = ctx->stream_data->pkt_size ;
    reg->stream_bitinfo.sw_stream_start_bit =0;
    reg->control.sw_dec_e = 1;

    reg->VLC_base = ff_rkvdec_get_fd(ctx->stream_data);

    if ((dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_TOP_FIELD) ||
        (dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_FRAME)) {
        reg->cur_pic_base = ff_rkvdec_get_fd(h->current_picture_ptr->f); //just index need map
    } else {
        reg->cur_pic_base = ff_rkvdec_get_fd(h->current_picture_ptr->f) | (((dx->seq.decode_width + 15) & (~15)) << 10);
    }

    if (h->last_picture_ptr) {
        if (h->last_picture_ptr->f->decode_error_flags && h->pict_type != AV_PICTURE_TYPE_I) {
            av_log(avctx, AV_LOG_INFO, "fill_picture_parameters missing reference");
            h->current_picture_ptr->f->decode_error_flags = FF_DECODE_ERROR_MISSING_REFERENCE;
        }
    }

    if (!h->last_picture_ptr){
        h->last_picture_ptr=h->current_picture_ptr;
    }
    if (h->pict_type == AV_PICTURE_TYPE_B) {
        reg->ref0 = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        reg->ref1 = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        reg->ref2 = ff_rkvdec_get_fd(h->next_picture_ptr->f);
        reg->ref3 = ff_rkvdec_get_fd(h->next_picture_ptr->f);
    } else {
        if ((dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_FRAME) ||
            ((dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_TOP_FIELD) && dx->pic_code_ext.top_field_first) ||
            ((dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_BOTTOM_FIELD) && (!dx->pic_code_ext.top_field_first))) {
            reg->ref0 = ff_rkvdec_get_fd(h->last_picture_ptr->f);
            reg->ref1 = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        } else if (dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_TOP_FIELD) {
            reg->ref0 = ff_rkvdec_get_fd(h->last_picture_ptr->f);
            reg->ref1 = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        } else if (dx->pic_code_ext.picture_structure == M2VD_PIC_STRUCT_BOTTOM_FIELD) {
            reg->ref0 = ff_rkvdec_get_fd(h->current_picture_ptr->f);
            reg->ref1 = ff_rkvdec_get_fd(h->last_picture_ptr->f);
        }
        reg->ref2 = ff_rkvdec_get_fd(h->current_picture_ptr->f);
        reg->ref3 = ff_rkvdec_get_fd(h->current_picture_ptr->f);
    }

    memcpy(ctx->qp_table->data[0], dx->qp_tab, M2VD_BUF_SIZE_QPTAB);
    reg->slice_table = ff_rkvdec_get_fd(ctx->qp_table);
#ifdef debug_regs
    unsigned char *p = reg;
    p+=50*4;
    for (int i = 50; i < 159 ; i++) {
        av_log(avctx, AV_LOG_ERROR, "RK_Mpeg2video_DEC: regs[%02d]=%08X\n", i, *((unsigned int*)p));
        p += 4;
    }
#endif

    return 0;
}

#define AV_GET_BUFFER_FLAG_CONTAIN_MV (1 << 1)

static int rkvdec_mpeg2video_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
    av_assert0(frame);
    av_assert0(frame->width);
    av_assert0(frame->height);
    frame->width = ALIGN(frame->width, 16);
    frame->height = ALIGN(frame->height, 16);
    return avctx->get_buffer2(avctx, frame, AV_GET_BUFFER_FLAG_CONTAIN_MV);
}

/** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_mpeg2video_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    RKVDECMpeg2videoContext * const ctx = ff_rkvdec_get_context(avctx);
    Mpeg1Context *s1  = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;

    av_log(avctx, AV_LOG_INFO, "RK_Mpeg2video_DEC: rkvdec_Mpeg2video_start_frame\n");
    fill_picture_parameters(s, ctx->pic_param);
    ctx->stream_data->pkt_size = 0;

    return 0;
}

/** End a hardware decoding based frame. */
static int rkvdec_mpeg2video_end_frame(AVCodecContext *avctx)
{
    RKVDECMpeg2videoContext * const ctx = ff_rkvdec_get_context(avctx);
    RKVDECMpeg2videoHwReq req;
    Mpeg1Context *s  = avctx->priv_data;
    MpegEncContext *h = &s->mpeg_enc_ctx
    int ret;

    av_log(avctx, AV_LOG_INFO, "RK_Mpeg2video_DEC: rkvdec_Mpeg2video_end_frame\n");
    rkvdec_mpeg2video_regs_gen_reg(avctx);

    req.req = (unsigned int*)ctx->hw_regs;
    req.size = sizeof(*ctx->hw_regs);

    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_SET_REG start.");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d\n", ret);

    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_GET_REG start.");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);
    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_GET_REG success.");

    if (ctx->hw_regs->interrupt.sw_dec_error_int) {
        h->current_picture_ptr->f->decode_error_flags = FF_DECODE_ERROR_INVALID_BITSTREAM;
    }
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d\n", ret);

#ifdef dump
    if (fp3 == NULL)
       fp3 = fopen("hal.bin", "wb");
    fwrite(h->current_picture_ptr->f->data[0],1, 352*288*1.5, fp3);
    fflush(fp3);
#endif

    return 0;
}


/** Decode the given h263 slice with RKVDEC. */
static int rkvdec_mpeg2video_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    av_log(avctx, AV_LOG_INFO, "RK_Mpeg2video_DEC: rkvdec_Mpeg2video_decode_slice size:%d\n", size);
    fill_stream_data(avctx, buffer, size);
    return 0;
}

static int rkvdec_mpeg2video_context_init(AVCodecContext *avctx)
{
    RKVDECMpeg2videoContext * const ctx = ff_rkvdec_get_context(avctx);
    int ret;

    av_log(avctx, AV_LOG_INFO, "RK_Mpeg2video_DEC: rkvdec_Mpeg2video_context_init\n");
    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }

    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_Mpeg2video_Regs));
    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_Mpeg2video));

    ctx->qp_table = av_frame_alloc();
    ctx->qp_table->linesize[0] = M2VD_BUF_SIZE_QPTAB;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->qp_table);

    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDECMPEG2VIDEO_DATA_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);

    ctx->pic_param->qp_tab = av_mallocz(256);

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

static int rkvdec_mpeg2video_context_uninit(AVCodecContext *avctx)
{
    RKVDECMpeg2videoContext * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, AV_LOG_INFO, "RK_Mpeg2video_DEC: rkvdec_Mpeg2video_context_uninit\n");
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
    ctx->allocator->free(ctx->allocator_ctx, ctx->qp_table);
    av_free(ctx->pic_param->qp_tab);
    av_free(ctx->stream_data);
    av_free(ctx->qp_table);
    av_free(ctx->pic_param);
    av_free(ctx->hw_regs);
    ctx->allocator->close(ctx->allocator_ctx);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }

    return 0;
}

AVHWAccel ff_mpeg2video_rkvdec_hwaccel = {
    .name                 = "mpeg2video_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG2VIDEO,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .alloc_frame          = rkvdec_mpeg2video_alloc_frame,
    .start_frame          = rkvdec_mpeg2video_start_frame,
    .end_frame            = rkvdec_mpeg2video_end_frame,
    .decode_slice         = rkvdec_mpeg2video_decode_slice,
    .init                 = rkvdec_mpeg2video_context_init,
    .uninit               = rkvdec_mpeg2video_context_uninit,
    .priv_data_size       = sizeof(RKVDECMpeg2videoContext),
};

