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

#include "vp8.h"
#include "rkvdec_vp8.h"
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/internal.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>


//#define dump 0
FILE* fp8=NULL;

#ifdef dump_probe
static RK_S32 num_probe = 0;
FILE* fp_vp8_probe=NULL;
#endif

#ifdef dump_count
static RK_S32 num_count = 0;
FILE *fp_vp8_count=NULL;
#endif


typedef struct _RKVDECVP8Context        RKVDECVP8Context;
typedef struct _RKVDECVP8FrameData      RKVDEC_FrameData_VP8, *LPRKVDEC_FrameData_VP8;
typedef struct _VP8dRkvRegs_t           RKVDEC_VP8_Regs, *LPRKVDEC_VP8_Regs;
typedef struct _RKVDEC_PicParams_VP8    RKVDEC_PicParams_VP8, *LPRKVDEC_PicParams_VP8;
typedef struct _RKVDEC_PicEntry_VP8     RKVDEC_PicEntry_VP8, *LPRKVDEC_PicEntry_VP8;
typedef struct _RKVDECVP8HwReq          RKVDECVP8HwReq;


struct _RKVDECVP8Context{
    signed int vpu_socket;
    LPRKVDEC_VP8_Regs hw_regs;
    LPRKVDEC_PicParams_VP8 pic_param;
    AVFrame* stream_data;
    AVFrame* seg_map;
    AVFrame* probe_table;
    os_allocator *allocator;
    void *allocator_ctx;
};

struct _RKVDECVP8FrameData{
    AVFrame colmv;
};

struct _RKVDEC_PicEntry_VP8 {
    union {
        struct {
            UCHAR Index7Bits     : 7;
            UCHAR AssociatedFlag : 1;
        };
        UCHAR bPicEntry;
    };
};

struct _RKVDECVP8HwReq {
    unsigned int *req;
    unsigned int  size;
} ;

#define RKVDECVP8_PROBE_SIZE  (1<<16) /* TODO */
#define RKVDECVP8_MAX_SEGMAP_SIZE  (2048 + 1024)  //1920*1080 /* TODO */
#define RKVDECVP8_DATA_SIZE        (2048 * 1024)

#define ALIGN(value, x) (((value) + (x) - 1)&~((x) - 1))
#define CLIP3(l, h, v) ((v) < (l) ? (l) : ((v) > (h) ? (h) : (v)))


/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECVP8Context *ff_rkvdec_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdec_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}

static inline int ff_rkvdec_get_size(AVFrame* frame)
{
    return frame->linesize[0];
}

static int rkvdec_vp8_SetPartitionOffsets(AVCodecContext *avctx)
{
    RK_U32 i = 0;
    RK_U32 offset = 0, extraBytesPacked = 0;
    RK_U32 baseOffset;

    RKVDECVP8Context * const p = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP8 pic_param = p->pic_param;
    unsigned char *stream = p->stream_data->data[0];//?
    stream += pic_param->frameTagSize;
    if (pic_param->frame_type)
        extraBytesPacked += 7;
    stream += pic_param->offsetToDctParts + extraBytesPacked;
    baseOffset = pic_param->frameTagSize + pic_param->offsetToDctParts + 3 * (pic_param->log2_nbr_of_dct_partitions - 1);
    for ( i = 0 ; i < (RK_U32)(pic_param->log2_nbr_of_dct_partitions) - 1 ; ++i ) {
        RK_U32  tmp;

        pic_param->dctPartitionOffsets[i] = baseOffset + offset;
        tmp = stream[0] | (stream[1] << 8) | (stream[2] << 16);
        offset += tmp;
        stream += 3;
    }
    pic_param->dctPartitionOffsets[i] = baseOffset + offset;

    return 0;
}

static void fill_header_parameters(AVCodecContext *avctx, void* buf, LPRKVDEC_PicParams_VP8 pp)
{
    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: fill_header_parameters\n");
    VP8Context *s = avctx->priv_data;
    VP56RangeCoder *c = &s->c;
    VP56RangeCoder tmp_c;
    int i;

    pp->bit_offset = (c->buffer - (uint8_t*)buf) * 8 + c->bits;
    pp->offsetToDctParts = AV_RL24(buf) >> 5;

    buf += 3;
    if (s->keyframe)
        buf += 7;

    ff_vp56_init_range_decoder(&tmp_c, buf, pp->offsetToDctParts);
    c = &tmp_c;

    if (s->keyframe) {
        vp8_rac_get(c); // colorspace
        vp8_rac_get(c); // fullrange
    }

    // segmentation.enabled
    if (vp8_rac_get(c)) {
        // parse_segment_info
        vp8_rac_get(c); //segmentation.update_map
        if (vp8_rac_get(c)) { // update segment feature data
            vp8_rac_get(c); //segmentation.absolute_vals

            for (i = 0; i < 4; i++)
                vp8_rac_get_sint(c, 7); //segmentation.base_quant[i]

            for (i = 0; i < 4; i++)
                vp8_rac_get_sint(c, 6); //segmentation.filter_level[i]
        }
        if (s->segmentation.update_map)
            for (i = 0; i < 3; i++)
                if (vp8_rac_get(c)) vp8_rac_get_uint(c, 8); // prob->segmentid[i]
    }

    vp8_rac_get(c); //filter.simple 
    vp8_rac_get_uint(c, 6); //filter.level
    vp8_rac_get_uint(c, 3); //filter.sharpness

    //lf_delta.enabled
    if (vp8_rac_get(c)) {
        if (vp8_rac_get(c)) {
            //update_lf_deltas(s);
            for (i = 0; i < 4; i++) {
                if (vp8_rac_get(c)) {
                    vp8_rac_get_uint(c, 6); //lf_delta.ref[i] 
                    vp8_rac_get(c);
                }
            }

            for (i = MODE_I4x4; i <= VP8_MVMODE_SPLIT; i++) {
                if (vp8_rac_get(c)) {
                    vp8_rac_get_uint(c, 6); //lf_delta.mode[i]
                    vp8_rac_get(c);
                }
            }
        }
    }

    vp8_rac_get_uint(c, 2); //setup_partitions

    pp->y1ac_delta_q = vp8_rac_get_uint(c, 7);
    pp->y1dc_delta_q = vp8_rac_get_sint(c, 4);
    pp->y2ac_delta_q = vp8_rac_get_sint(c, 4);
    pp->y2dc_delta_q = vp8_rac_get_sint(c, 4);
    pp->uvac_delta_q = vp8_rac_get_sint(c, 4);
    pp->uvdc_delta_q = vp8_rac_get_sint(c, 4);

}

static void fill_picture_parameters(AVCodecContext *avctx, LPRKVDEC_PicParams_VP8 pp)
{
    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: fill_picture_parameters\n");
    VP8Context *p = avctx->priv_data;
    RKVDECVP8Context * const ctx = ff_rkvdec_get_context(avctx);
    RK_U32 i,j,temp;
    pp->frame_type = p->keyframe;
    pp->stVP8Segments.segmentation_enabled = p->segmentation.enabled;
    pp->stVP8Segments.update_mb_segmentation_map = p->segmentation.update_map;
    pp->mode_ref_lf_delta_enabled = p->lf_delta.enabled;
    pp->mb_no_coeff_skip = p->mbskip_enabled;
    pp->width            = p->avctx->width;
    pp->height           = p->avctx->height;
    pp->decMode          = VP8HWD_VP8;
    pp->filter_type      = p->filter.simple ;
    pp->sharpness        = p->filter.sharpness;
    pp->filter_level     = p->filter.level;
    pp->stVP8Segments.update_mb_segmentation_data = p->segmentation.absolute_vals;
    pp->version          = p->profile;

    temp                 = pp->bit_offset;
    pp->bool_range       = (p->c.high & (0xFFU));
    if (pp->bool_range < 0x80) {
        pp->bool_range <<= 1;
        pp->bool_value   = ((p->c.code_word >> 15) & (0xFFU));
        temp++;
    } else {
        pp->bool_value   = ((p->c.code_word >> 16) & (0xFFU));
    }
    pp->stream_start_offset  = temp / 8;
    pp->stream_start_offset &= (~0x07U);  /* align the base */
    pp->stream_start_bit     = temp - pp->stream_start_offset * 8;

    pp->frameTagSize        = 3;
    pp->streamEndPos        = ctx->stream_data->pkt_size;

    pp->log2_nbr_of_dct_partitions = p->num_coeff_partitions;

    pp->probe_skip_false = p->prob->mbskip;
    pp->prob_intra   = p->prob->intra;
    pp->prob_last    = p->prob->last;
    pp->prob_golden  = p->prob->golden;

    for (i = 0; i < 4; i++) {
        memcpy(pp->vp8_coef_update_probs[i][0],p->prob->token[i][0], sizeof(p->prob->token[i][0]));
        memcpy(pp->vp8_coef_update_probs[i][1],p->prob->token[i][1], sizeof(p->prob->token[i][1]));
        memcpy(pp->vp8_coef_update_probs[i][2],p->prob->token[i][2], sizeof(p->prob->token[i][2]));
        memcpy(pp->vp8_coef_update_probs[i][3],p->prob->token[i][3], sizeof(p->prob->token[i][3]));
        memcpy(pp->vp8_coef_update_probs[i][4],p->prob->token[i][5], sizeof(p->prob->token[i][5]));
        memcpy(pp->vp8_coef_update_probs[i][5],p->prob->token[i][6], sizeof(p->prob->token[i][6]));
        memcpy(pp->vp8_coef_update_probs[i][6],p->prob->token[i][4], sizeof(p->prob->token[i][4]));
        memcpy(pp->vp8_coef_update_probs[i][7],p->prob->token[i][15], sizeof(p->prob->token[i][15]));	
    }
    memcpy(pp->vp8_mv_update_probs, p->prob->mvc, sizeof(pp->vp8_mv_update_probs));

    for ( i = 0; i < 3; i++) {
        pp->intra_chroma_prob[i] = p->prob->pred8x8c[i];
        pp->stVP8Segments.mb_segment_tree_probs[i] = p->prob->segmentid[i];
    }

    pp->ref_frame_sign_bias_golden = p->sign_bias[VP56_FRAME_GOLDEN];
    pp->ref_frame_sign_bias_altref = p->sign_bias[VP56_FRAME_GOLDEN2];

    for (i = 0; i < 4; i++) {
        pp->stVP8Segments.segment_feature_data[0][i] = p->segmentation.base_quant[i];
        pp->ref_lf_deltas[i]    = p->lf_delta.ref[i];
        pp->mode_lf_deltas[i]   = p->lf_delta.mode[i+MODE_I4x4];
        pp->stVP8Segments.segment_feature_data[1][i] = p->segmentation.filter_level[i];
        pp->intra_16x16_prob[i] = p->prob->pred16x16[i];
    }
}

static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECVP8Context * const ctx = ff_rkvdec_get_context(avctx);
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset     = ctx->stream_data->pkt_size;
    unsigned int left_size  = ctx->stream_data->linesize[0] - offset;
    if (data_ptr && left_size < size) {
        AVFrame* new_pkt = av_frame_alloc();
        new_pkt->linesize[0] = offset + size + RKVDECVP8_DATA_SIZE;
        ctx->allocator->alloc(ctx->allocator_ctx, new_pkt);
        memcpy(new_pkt->data[0], ctx->stream_data->data[0], ctx->stream_data->pkt_size);
        ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
        memcpy(ctx->stream_data, new_pkt, sizeof(AVFrame));
        data_ptr = ctx->stream_data->data[0];
        av_free(new_pkt);
    }

    memcpy(data_ptr + offset, buffer, size);
    ctx->stream_data->pkt_size += size;
    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: fill_stream_data pkg_size %d size %d", ctx->stream_data->pkt_size, size);
}

static int rkvdec_vp8_init_hwcfg(RKVDECVP8Context *reg_cxt)
{
    LPRKVDEC_VP8_Regs reg = reg_cxt->hw_regs;
    memset(reg,0,sizeof(*reg));
    reg->reg50_dec_ctrl.sw_dec_out_tiled_e = 0;
    reg->reg50_dec_ctrl.sw_dec_scmd_dis    = 0;
    reg->reg50_dec_ctrl.sw_dec_adv_pre_dis = 0;
    reg->reg50_dec_ctrl.sw_dec_latency     = 0;

    reg->reg53_dec_mode =  10;

    reg->reg54_endian.sw_dec_in_endian     = 1;
    reg->reg54_endian.sw_dec_out_endian    = 1;
    reg->reg54_endian.sw_dec_inswap32_e    = 1;
    reg->reg54_endian.sw_dec_outswap32_e   = 1;
    reg->reg54_endian.sw_dec_strswap32_e   = 1;
    reg->reg54_endian.sw_dec_strendian_e   = 1;

    reg->reg55_Interrupt.sw_dec_irq = 0;

    reg->reg56_axi_ctrl.sw_dec_axi_rn_id     = 0;
    reg->reg56_axi_ctrl.sw_dec_axi_wr_id     = 0;

    reg->reg56_axi_ctrl.sw_dec_data_disc_e   = 0;
    reg->reg56_axi_ctrl.sw_dec_max_burst     = 16;
    reg->reg57_enable_ctrl.sw_dec_timeout_e  = 1;
    reg->reg57_enable_ctrl.sw_dec_clk_gate_e = 1;
    reg->reg57_enable_ctrl.sw_dec_out_dis    = 0;

    reg->reg149_segment_map_base = ff_rkvdec_get_fd(reg_cxt->seg_map);
    reg->reg61_qtable_base = ff_rkvdec_get_fd(reg_cxt->probe_table);

    return 0;
}

static int rkvdec_vp8_pre_filter_tap_set(RKVDECVP8Context *reg_cxt)
{
    LPRKVDEC_VP8_Regs regs = (LPRKVDEC_VP8_Regs )reg_cxt->hw_regs;
    regs->reg59.sw_pred_bc_tap_0_0  = mcFilter[0][1];
    regs->reg59.sw_pred_bc_tap_0_1  = mcFilter[0][2];
    regs->reg59.sw_pred_bc_tap_0_2  = mcFilter[0][3];
    regs->reg153.sw_pred_bc_tap_0_3 = mcFilter[0][4];
    regs->reg153.sw_pred_bc_tap_1_0 = mcFilter[1][1];
    regs->reg153.sw_pred_bc_tap_1_1 = mcFilter[1][2];
    regs->reg154.sw_pred_bc_tap_1_2 = mcFilter[1][3];
    regs->reg154.sw_pred_bc_tap_1_3 = mcFilter[1][4];
    regs->reg154.sw_pred_bc_tap_2_0 = mcFilter[2][1];
    regs->reg155.sw_pred_bc_tap_2_1 = mcFilter[2][2];
    regs->reg155.sw_pred_bc_tap_2_2 = mcFilter[2][3];
    regs->reg155.sw_pred_bc_tap_2_3 = mcFilter[2][4];

    regs->reg156.sw_pred_bc_tap_3_0 = mcFilter[3][1];
    regs->reg156.sw_pred_bc_tap_3_1 = mcFilter[3][2];
    regs->reg156.sw_pred_bc_tap_3_2 = mcFilter[3][3];
    regs->reg157.sw_pred_bc_tap_3_3 = mcFilter[3][4];
    regs->reg157.sw_pred_bc_tap_4_0 = mcFilter[4][1];
    regs->reg157.sw_pred_bc_tap_4_1 = mcFilter[4][2];
    regs->reg158.sw_pred_bc_tap_4_2 = mcFilter[4][3];
    regs->reg158.sw_pred_bc_tap_4_3 = mcFilter[4][4];
    regs->reg158.sw_pred_bc_tap_5_0 = mcFilter[5][1];

    regs->reg125.sw_pred_bc_tap_5_1 = mcFilter[5][2];

    regs->reg125.sw_pred_bc_tap_5_2 = mcFilter[5][3];

    regs->reg125.sw_pred_bc_tap_5_3 = mcFilter[5][4];
    regs->reg126.sw_pred_bc_tap_6_0 = mcFilter[6][1];
    regs->reg126.sw_pred_bc_tap_6_1 = mcFilter[6][2];
    regs->reg126.sw_pred_bc_tap_6_2 = mcFilter[6][3];
    regs->reg127.sw_pred_bc_tap_6_3 = mcFilter[6][4];
    regs->reg127.sw_pred_bc_tap_7_0 = mcFilter[7][1];
    regs->reg127.sw_pred_bc_tap_7_1 = mcFilter[7][2];
    regs->reg128.sw_pred_bc_tap_7_2 = mcFilter[7][3];
    regs->reg128.sw_pred_bc_tap_7_3 = mcFilter[7][4];

    regs->reg128.sw_pred_tap_2_M1 = mcFilter[2][0];
    regs->reg128.sw_pred_tap_2_4  = mcFilter[2][5];
    regs->reg128.sw_pred_tap_4_M1 = mcFilter[4][0];
    regs->reg128.sw_pred_tap_4_4  = mcFilter[4][5];
    regs->reg128.sw_pred_tap_6_M1 = mcFilter[6][0];
    regs->reg128.sw_pred_tap_6_4  = mcFilter[6][5];

    return 0;
}

static int rkvdec_vp8_dct_partition_cfg(RKVDECVP8Context *reg_cxt)
{
    RK_U32 i = 0, len = 0, len1 = 0;
    RK_U32 extraBytesPacked = 0;
    RK_U32 addr = 0, byte_offset = 0;

    LPRKVDEC_VP8_Regs regs = (LPRKVDEC_VP8_Regs )reg_cxt->hw_regs;
    LPRKVDEC_PicParams_VP8 pic_param = reg_cxt->pic_param;

    regs->reg145_bitpl_ctrl_base    = ff_rkvdec_get_fd(reg_cxt->stream_data);
    regs->reg145_bitpl_ctrl_base   |= (pic_param->stream_start_offset << 10);
    regs->reg122.sw_strm1_start_bit = pic_param->stream_start_bit;

    /* calculate dct partition length here instead */
    if (pic_param->decMode == VP8HWD_VP8 && pic_param->frame_type)
        extraBytesPacked += 7;

    pic_param->streamEndPos = reg_cxt->stream_data->pkt_size - 3;
    if (pic_param->frame_type) {//I_frame
        regs->reg51_stream_info.sw_stream_len -= 7;
        pic_param->streamEndPos -= 7;
    }
    len = pic_param->streamEndPos + pic_param->frameTagSize - pic_param->dctPartitionOffsets[0];
    len += (  pic_param->log2_nbr_of_dct_partitions - 1) * 3;
    len1 = extraBytesPacked + pic_param->dctPartitionOffsets[0];
    len += (len1 & 0x7);
    regs->reg51_stream_info.sw_stream_len = len;

    len = pic_param->offsetToDctParts + pic_param->frameTagSize -
          (pic_param->stream_start_offset - extraBytesPacked);
    /* give extra byte of data to negotiate "tight" buffers */
    len++;
    regs->reg124.sw_stream1_len = len;
    regs->reg124.sw_coeffs_part_am =  pic_param->log2_nbr_of_dct_partitions - 1;
    for (i = 0; i < (RK_U32)(pic_param->log2_nbr_of_dct_partitions); i++) {
        addr = extraBytesPacked + pic_param->dctPartitionOffsets[i];
        byte_offset = addr & 0x7;
        addr = addr & 0xFFFFFFF8;

        if ( i == 0) {
            regs->reg64_input_stream_base = ff_rkvdec_get_fd(reg_cxt->stream_data) | (addr << 10);
        } else if ( i <= 5) {
            regs->reg_dct_strm_base[i - 1] = ff_rkvdec_get_fd(reg_cxt->stream_data)| (addr << 10);
        } else {
            regs->reg_dct_strm1_base[i - 6] = ff_rkvdec_get_fd(reg_cxt->stream_data) | (addr << 10);
        }
        switch (i) {
        case 0:
            regs->reg122.sw_strm_start_bit = byte_offset * 8;
            break;
        case 1:
            regs->reg121.sw_dct1_start_bit = byte_offset * 8;
            break;
        case 2:
            regs->reg121.sw_dct2_start_bit = byte_offset * 8;
            break;
        case 3:
            regs->reg150.sw_dct_start_bit_3 = byte_offset * 8;
            break;
        case 4:
            regs->reg150.sw_dct_start_bit_4 = byte_offset * 8;
            break;
        case 5:
            regs->reg150.sw_dct_start_bit_5 = byte_offset * 8;
            break;
        case 6:
            regs->reg150.sw_dct_start_bit_6 = byte_offset * 8;
            break;
        case 7:
            regs->reg150.sw_dct_start_bit_6 = byte_offset * 8;
            break;
        default:
            break;
        }
    }

    return 0;
}

static void rkvdec_vp8_asic_probe_update(LPRKVDEC_PicParams_VP8 p, RK_U8 *probTbl)
{
    RK_U8   *dst;
    RK_U32  i, j, k;

    /* first probs */
    dst = probTbl;

    dst[0] = p->probe_skip_false;
    dst[1] = p->prob_intra;
    dst[2] = p->prob_last;
    dst[3] = p->prob_golden;
    dst[4] = p->stVP8Segments.mb_segment_tree_probs[0];
    dst[5] = p->stVP8Segments.mb_segment_tree_probs[1];
    dst[6] = p->stVP8Segments.mb_segment_tree_probs[2];
    dst[7] = 0; /*unused*/

    dst += 8;
    dst[0] = p->intra_16x16_prob[0];
    dst[1] = p->intra_16x16_prob[1];
    dst[2] = p->intra_16x16_prob[2];
    dst[3] = p->intra_16x16_prob[3];
    dst[4] = p->intra_chroma_prob[0];
    dst[5] = p->intra_chroma_prob[1];
    dst[6] = p->intra_chroma_prob[2];
    dst[7] = 0; /*unused*/

    /* mv probs */
    dst += 8;
    dst[0] = p->vp8_mv_update_probs[0][0]; /* is short */
    dst[1] = p->vp8_mv_update_probs[1][0];
    dst[2] = p->vp8_mv_update_probs[0][1]; /* sign */
    dst[3] = p->vp8_mv_update_probs[1][1];
    dst[4] = p->vp8_mv_update_probs[0][8 + 9];
    dst[5] = p->vp8_mv_update_probs[0][9 + 9];
    dst[6] = p->vp8_mv_update_probs[1][8 + 9];
    dst[7] = p->vp8_mv_update_probs[1][9 + 9];
    dst += 8;
    for ( i = 0 ; i < 2 ; ++i ) {
        for ( j = 0 ; j < 8 ; j += 4 ) {
            dst[0] = p->vp8_mv_update_probs[i][j + 9 + 0];
            dst[1] = p->vp8_mv_update_probs[i][j + 9 + 1];
            dst[2] = p->vp8_mv_update_probs[i][j + 9 + 2];
            dst[3] = p->vp8_mv_update_probs[i][j + 9 + 3];
            dst += 4;
        }
    }
    for ( i = 0 ; i < 2 ; ++i ) {
        dst[0] =  p->vp8_mv_update_probs[i][0 + 2];
        dst[1] =  p->vp8_mv_update_probs[i][1 + 2];
        dst[2] =  p->vp8_mv_update_probs[i][2 + 2];
        dst[3] =  p->vp8_mv_update_probs[i][3 + 2];
        dst[4] =  p->vp8_mv_update_probs[i][4 + 2];
        dst[5] =  p->vp8_mv_update_probs[i][5 + 2];
        dst[6] =  p->vp8_mv_update_probs[i][6 + 2];
        dst[7] = 0; /*unused*/
        dst += 8;
    }

    /* coeff probs (header part) */
    dst = (RK_U8*)probTbl;
    dst += (8 * 7);
    for ( i = 0 ; i < 4 ; ++i ) {
        for ( j = 0 ; j < 8 ; ++j ) {
            for ( k = 0 ; k < 3 ; ++k ) {
                dst[0] = p->vp8_coef_update_probs[i][j][k][0];
                dst[1] = p->vp8_coef_update_probs[i][j][k][1];
                dst[2] = p->vp8_coef_update_probs[i][j][k][2];
                dst[3] = p->vp8_coef_update_probs[i][j][k][3];
                dst += 4;
            }
        }
    }

    /* coeff probs (footer part) */
    dst = (RK_U8*)probTbl;
    dst += (8 * 55);
    for ( i = 0 ; i < 4 ; ++i ) {
        for ( j = 0 ; j < 8 ; ++j ) {
            for ( k = 0 ; k < 3 ; ++k ) {
                dst[0] = p->vp8_coef_update_probs[i][j][k][4];
                dst[1] = p->vp8_coef_update_probs[i][j][k][5];
                dst[2] = p->vp8_coef_update_probs[i][j][k][6];
                dst[3] = p->vp8_coef_update_probs[i][j][k][7];
                dst[4] = p->vp8_coef_update_probs[i][j][k][8];
                dst[5] = p->vp8_coef_update_probs[i][j][k][9];
                dst[6] = p->vp8_coef_update_probs[i][j][k][10];
                dst[7] = 0; /*unused*/
                dst += 8;
            }
        }
    }
    return ;
}

static int rkvdec_vp8_regs_gen_reg(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: rkvdec_vp8_regs_gen_reg\n");
    RK_U32 mb_width = 0, mb_height = 0;
    RK_U8 *segmap_ptr = NULL;
    RK_U8 *probe_ptr = NULL;

    VP8Context *p = avctx->priv_data;
    RKVDECVP8Context * const reg_cxt = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP8 pic_param = reg_cxt->pic_param;
    LPRKVDEC_VP8_Regs vp8_hw_regs = reg_cxt->hw_regs;

    rkvdec_vp8_SetPartitionOffsets(avctx);

    rkvdec_vp8_init_hwcfg(reg_cxt);
    mb_width = (pic_param->width + 15) >> 4;
    mb_height = (pic_param->height + 15) >> 4;

    vp8_hw_regs->reg120.sw_pic_mb_width = mb_width & 0x1FF;
    vp8_hw_regs->reg120.sw_pic_mb_hight_p =  mb_height & 0xFF;
    vp8_hw_regs->reg120.sw_pic_mb_w_ext = mb_width >> 9;
    vp8_hw_regs->reg120.sw_pic_mb_h_ext = mb_height >> 8;
    if (pic_param->frame_type) {
        segmap_ptr = reg_cxt->seg_map->data[0];
        if (NULL != segmap_ptr) {
            memset(segmap_ptr, 0, RKVDECVP8_MAX_SEGMAP_SIZE);
        }
    }

    probe_ptr = reg_cxt->probe_table->data[0];
    if (NULL != probe_ptr) {
        rkvdec_vp8_asic_probe_update(pic_param, probe_ptr);
    }

    vp8_hw_regs->reg63_cur_pic_base = ff_rkvdec_get_fd(p->framep[VP56_FRAME_CURRENT]->tf.f);

    if (pic_param->frame_type) {
        if ((mb_width * mb_height) << 8 > 0x400000) {
             av_log(avctx, AV_LOG_ERROR, "mb_width*mb_height is big then 0x400000,iommu err\n");
        }
        vp8_hw_regs->reg131_ref0_base = vp8_hw_regs->reg63_cur_pic_base | ((mb_width * mb_height) << 18);
    } else if (p->framep[VP56_FRAME_PREVIOUS] != NULL) {
        vp8_hw_regs->reg131_ref0_base = ff_rkvdec_get_fd(p->framep[VP56_FRAME_PREVIOUS]->tf.f);
    } else {
        vp8_hw_regs->reg131_ref0_base = vp8_hw_regs->reg63_cur_pic_base;
    }

    /* golden reference */
    if (p->framep[VP56_FRAME_GOLDEN] != NULL) {
        vp8_hw_regs->reg136_golden_ref_base = ff_rkvdec_get_fd(p->framep[VP56_FRAME_GOLDEN]->tf.f);
    } else {
        vp8_hw_regs->reg136_golden_ref_base = vp8_hw_regs->reg63_cur_pic_base;
    }

    vp8_hw_regs->reg136_golden_ref_base = vp8_hw_regs->reg136_golden_ref_base | (pic_param->ref_frame_sign_bias_golden << 10);

    /* alternate reference */
    if (p->framep[VP56_FRAME_GOLDEN2] != NULL) {
        vp8_hw_regs->reg137.alternate_ref_base = ff_rkvdec_get_fd(p->framep[VP56_FRAME_GOLDEN2]->tf.f);
    } else {
        vp8_hw_regs->reg137.alternate_ref_base = vp8_hw_regs->reg63_cur_pic_base;
    }

    vp8_hw_regs->reg137.alternate_ref_base = vp8_hw_regs->reg137.alternate_ref_base | (pic_param->ref_frame_sign_bias_altref << 10);

    vp8_hw_regs->reg149_segment_map_base = vp8_hw_regs->reg149_segment_map_base |
                                           ((pic_param->stVP8Segments.segmentation_enabled +
                                            (pic_param->stVP8Segments.update_mb_segmentation_map << 1)) << 10);

    vp8_hw_regs->reg57_enable_ctrl.sw_pic_inter_e = !pic_param->frame_type;
    vp8_hw_regs->reg50_dec_ctrl.sw_skip_mode = !pic_param->mb_no_coeff_skip;

    if (!pic_param->stVP8Segments.segmentation_enabled) {
        vp8_hw_regs->reg129.sw_filt_level_0 = pic_param->filter_level;
    } else if (pic_param->stVP8Segments.update_mb_segmentation_data) {
        vp8_hw_regs->reg129.sw_filt_level_0 = pic_param->stVP8Segments.segment_feature_data[1][0];
        vp8_hw_regs->reg129.sw_filt_level_1 = pic_param->stVP8Segments.segment_feature_data[1][1];
        vp8_hw_regs->reg129.sw_filt_level_2 = pic_param->stVP8Segments.segment_feature_data[1][2];
        vp8_hw_regs->reg129.sw_filt_level_3 = pic_param->stVP8Segments.segment_feature_data[1][3];
    } else {
        vp8_hw_regs->reg129.sw_filt_level_0 = CLIP3(0, 63, (RK_S32)pic_param->filter_level + pic_param->stVP8Segments.segment_feature_data[1][0]);
        vp8_hw_regs->reg129.sw_filt_level_1 = CLIP3(0, 63, (RK_S32)pic_param->filter_level + pic_param->stVP8Segments.segment_feature_data[1][1]);
        vp8_hw_regs->reg129.sw_filt_level_2 = CLIP3(0, 63, (RK_S32)pic_param->filter_level + pic_param->stVP8Segments.segment_feature_data[1][2]);
        vp8_hw_regs->reg129.sw_filt_level_3 = CLIP3(0, 63, (RK_S32)pic_param->filter_level + pic_param->stVP8Segments.segment_feature_data[1][3]);
    }

    vp8_hw_regs->reg132.sw_filt_type = pic_param->filter_type;
    vp8_hw_regs->reg132.sw_filt_sharpness = pic_param->sharpness;

    if (pic_param->filter_level == 0) {
        vp8_hw_regs->reg50_dec_ctrl.sw_filtering_dis = 1;
    }

    if (pic_param->version != 3) {
        vp8_hw_regs->reg121.sw_romain_mv = 1;
    }

    if (pic_param->decMode == VP8HWD_VP8 && (pic_param->version & 0x3)) {
        vp8_hw_regs->reg121.sw_eable_bilinear = 1;
    }
    vp8_hw_regs->reg122.sw_boolean_value = pic_param->bool_value;
    vp8_hw_regs->reg122.sw_boolean_range = pic_param->bool_range;

    {
        if (!pic_param->stVP8Segments.segmentation_enabled)
            vp8_hw_regs->reg130.sw_quant_0 = pic_param->y1ac_delta_q;
        else if (pic_param->stVP8Segments.update_mb_segmentation_data) { /* absolute mode */
            vp8_hw_regs->reg130.sw_quant_0 = pic_param->stVP8Segments.segment_feature_data[0][0];
            vp8_hw_regs->reg130.sw_quant_1 = pic_param->stVP8Segments.segment_feature_data[0][1];
            vp8_hw_regs->reg151.sw_quant_2 = pic_param->stVP8Segments.segment_feature_data[0][2];
            vp8_hw_regs->reg151.sw_quant_3 = pic_param->stVP8Segments.segment_feature_data[0][3];
        } else { /* delta mode */
            vp8_hw_regs->reg130.sw_quant_0 = CLIP3(0, 127, pic_param->y1ac_delta_q + pic_param->stVP8Segments.segment_feature_data[0][0]);
            vp8_hw_regs->reg130.sw_quant_1 = CLIP3(0, 127, pic_param->y1ac_delta_q + pic_param->stVP8Segments.segment_feature_data[0][1]);
            vp8_hw_regs->reg151.sw_quant_2 = CLIP3(0, 127, pic_param->y1ac_delta_q + pic_param->stVP8Segments.segment_feature_data[0][2]);
            vp8_hw_regs->reg151.sw_quant_3 = CLIP3(0, 127, pic_param->y1ac_delta_q + pic_param->stVP8Segments.segment_feature_data[0][3]);
        }

        vp8_hw_regs->reg130.sw_quant_delta_0 = pic_param->y1dc_delta_q;
        vp8_hw_regs->reg130.sw_quant_delta_1 = pic_param->y2dc_delta_q;
        vp8_hw_regs->reg151.sw_quant_delta_2 = pic_param->y2ac_delta_q;
        vp8_hw_regs->reg151.sw_quant_delta_3 = pic_param->uvdc_delta_q;
        vp8_hw_regs->reg152.sw_quant_delta_4 = pic_param->uvac_delta_q;

        if (pic_param->mode_ref_lf_delta_enabled) {
            vp8_hw_regs->reg133.sw_filt_ref_adj_0 = pic_param->ref_lf_deltas[0];
            vp8_hw_regs->reg133.sw_filt_ref_adj_1 = pic_param->ref_lf_deltas[1];
            vp8_hw_regs->reg133.sw_filt_ref_adj_2 = pic_param->ref_lf_deltas[2];
            vp8_hw_regs->reg133.sw_filt_ref_adj_3 = pic_param->ref_lf_deltas[3];
            vp8_hw_regs->reg132.sw_filt_mb_adj_0  = pic_param->mode_lf_deltas[0];
            vp8_hw_regs->reg132.sw_filt_mb_adj_1  = pic_param->mode_lf_deltas[1];
            vp8_hw_regs->reg132.sw_filt_mb_adj_2  = pic_param->mode_lf_deltas[2];
            vp8_hw_regs->reg132.sw_filt_mb_adj_3  = pic_param->mode_lf_deltas[3];
        }

    }

    if ((pic_param->version & 0x3) == 0)
        rkvdec_vp8_pre_filter_tap_set(reg_cxt);

    rkvdec_vp8_dct_partition_cfg(reg_cxt);
    vp8_hw_regs->reg57_enable_ctrl.sw_dec_e = 1;

    return 0;
}


 /** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_vp8_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    RKVDECVP8Context * const ctx = ff_rkvdec_get_context(avctx);
    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: rkvdec_vp8_start_frame\n");
    fill_header_parameters(avctx, buffer, ctx->pic_param);
    fill_picture_parameters(avctx, ctx->pic_param);
    ctx->stream_data->pkt_size = 0;
    return 0;
}

/** End a hardware decoding based frame. */
static int rkvdec_vp8_end_frame(AVCodecContext *avctx)
{
    RKVDECVP8Context * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP8 pic_param = ctx->pic_param;
    VP8Context *p = avctx->priv_data;
    RKVDECVP8HwReq req;
    unsigned char *preg;
    int ret, i;

    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: rkvdec_vp8_end_frame\n");
    if (ctx->stream_data->pkt_size <= 0) {
        av_log(avctx, AV_LOG_WARNING, "RK_VP8_DEC: rkvdec_vp8_end_frame not valid stream\n");
        return 0;
    }
    rkvdec_vp8_regs_gen_reg(avctx);

    req.req = (unsigned int*)ctx->hw_regs;
    req.size = sizeof(*ctx->hw_regs);
    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_SET_REG start.");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d\n", ret);

#ifdef dump1
    preg = (unsigned char*)ctx->hw_regs;
    for (i = 0; i < 159 ; i++) {
        av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: get  regs[%02d]=%08X\n", i, *((unsigned int*)preg));
        preg += 4;
    }
#endif

    av_log(avctx, AV_LOG_INFO, "ioctl VPU_IOC_GET_REG start.");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d\n", ret);

#ifdef dump
    if (fp8 == NULL)
        fp8 = fopen("hal.bin", "wb");
    fwrite(p->framep[VP56_FRAME_CURRENT]->tf.f->data[0],1, 1920 * 1088 * 1.5, fp8);
    fflush(fp8);
#endif

    return 0;
}


/** Decode the given vp8 slice with RKVDEC. */
static int rkvdec_vp8_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: rkvdec_vp8_decode_slice size:%d\n", size);
    fill_stream_data(avctx, buffer, size);
    return 0;
}

#define AV_GET_BUFFER_FLAG_CONTAIN_MV (1 << 1)

static RK_U32 vp8_hor_align(RK_U32 val)
{
    return ALIGN(val, 16);
}
static RK_U32 vp8_ver_align(RK_U32 val)
{
    return ALIGN(val, 16);
}

static int rkvdec_vp8_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
    int ret;
    avctx->coded_width = vp8_hor_align(frame->width);
    avctx->coded_height = vp8_hor_align(frame->height);
    frame->width = vp8_hor_align(frame->width);
    frame->height = vp8_ver_align(frame->height);

    ret = avctx->get_buffer2(avctx, frame, AV_GET_BUFFER_FLAG_CONTAIN_MV);
    if (ret >= 0){
        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            int i;
            int num_planes = av_pix_fmt_count_planes(frame->format);
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
            int flags = desc ? desc->flags : 0;
            if (num_planes == 1 && (flags & AV_PIX_FMT_FLAG_PAL))
                num_planes = 2;
            for (i = 0; i < num_planes; i++) {
                av_assert0(frame->data[i]);
            }
            // For now do not enforce anything for palette of pseudopal formats
            if (num_planes == 1 && (flags & AV_PIX_FMT_FLAG_PSEUDOPAL))
                num_planes = 2;
            // For formats without data like hwaccel allow unused pointers to be non-NULL.
            for (i = num_planes; num_planes > 0 && i < FF_ARRAY_ELEMS(frame->data); i++) {
                if (frame->data[i])
                av_log(avctx, AV_LOG_ERROR, "Buffer returned by get_buffer2() did not zero unused plane pointers\n");
                frame->data[i] = NULL;
            }
        }
    }
    return 0;
}


static int rkvdec_vp8_context_init(AVCodecContext *avctx)
{
    RKVDECVP8Context * const ctx = ff_rkvdec_get_context(avctx);
    int ret;

    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: rkvdec_vp8_context_init\n");

    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }

    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_VP8_Regs));
    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_VP8));

    ctx->probe_table = av_frame_alloc();
    ctx->probe_table->linesize[0] = RKVDECVP8_PROBE_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->probe_table);

    ctx->seg_map = av_frame_alloc();
    ctx->seg_map->linesize[0] = RKVDECVP8_MAX_SEGMAP_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->seg_map);

    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDECVP8_DATA_SIZE;

    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);
    for (int i = 0; i <  FF_ARRAY_ELEMS(name_rkvdecs); i++) {
        ctx->vpu_socket = open(name_rkvdecs[i], O_RDWR);
        if (ctx->vpu_socket > 0)
            break;
    }

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

static int rkvdec_vp8_context_uninit(AVCodecContext *avctx)
{
    RKVDECVP8Context * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, AV_LOG_INFO, "RK_VP8_DEC: rkvdec_vp8_context_uninit\n");

    ctx->allocator->free(ctx->allocator_ctx, ctx->seg_map);
    ctx->allocator->free(ctx->allocator_ctx, ctx->probe_table);
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
    av_free(ctx->seg_map);
    av_free(ctx->probe_table);
    av_free(ctx->stream_data);
    av_free(ctx->hw_regs);
    av_free(ctx->pic_param);
    ctx->allocator->close(ctx->allocator_ctx);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }
#ifdef  dump_probe
    if (fp_vp8_probe != NULL) {
        fclose(fp_vp8_probe);
    }
#endif
#ifdef  dump_probe
    if (fp_vp8_count != NULL) {
        fclose(fp_vp8_count);
    }
#endif
    return 0;
}

AVHWAccel ff_vp8_rkvdec_hwaccel = {
    .name                 = "vp8_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_VP8,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .start_frame          = rkvdec_vp8_start_frame,
    .end_frame            = rkvdec_vp8_end_frame,
    .decode_slice         = rkvdec_vp8_decode_slice,
    .alloc_frame          = rkvdec_vp8_alloc_frame,
    .init                 = rkvdec_vp8_context_init,
    .uninit               = rkvdec_vp8_context_uninit,
    .priv_data_size       = sizeof(RKVDECVP8Context),
    .frame_priv_data_size = sizeof(RKVDEC_FrameData_VP8),
};

