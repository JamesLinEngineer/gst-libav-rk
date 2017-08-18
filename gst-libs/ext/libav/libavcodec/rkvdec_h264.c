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

#include "h264.h"
#include "h264dec.h"
#include "rkvdec_h264.h"
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

//#define debug_regs
//#define dump
FILE* fp1=NULL;

typedef struct _RKVDECH264Context RKVDECH264Context;
typedef struct _RKVDECH264FrameData RKVDECH264FrameData;
typedef struct H264dRkvRegs_t RKVDEC_H264_Regs, *LPRKVDEC_H264_Regs;
typedef struct _RKVDEC_PicParams_H264 RKVDEC_PicParams_H264, *LPRKVDEC_PicParams_H264;
typedef struct _RKVDEC_PicEntry_H264 RKVDEC_PicEntry_H264, *LPRKVDEC_PicEntry_H264;
typedef struct ScalingList RKVDEC_ScalingList_H264, *LPRKVDEC_ScalingList_H264;
typedef struct _RKVDECH264HwReq RKVDECH264HwReq;


struct _RKVDECH264Context{
     signed int vpu_socket;
     LPRKVDEC_H264_Regs hw_regs;
     LPRKVDEC_PicParams_H264 pic_param;
     LPRKVDEC_ScalingList_H264 scaling_list;
     AVFrame* syntax_data;
     AVFrame* cabac_table_data;
     AVFrame* scaling_list_data;
     AVFrame* pps_data;
     AVFrame* rps_data;
     AVFrame* errorinfo_data;
     AVFrame* stream_data;
     AVFrame* frame_data;
     os_allocator *allocator;
     void *allocator_ctx;
};

struct _RKVDECH264FrameData{

};

struct _RKVDECH264HwReq {
    unsigned int *req;
    unsigned int  size;
} ;

struct _RKVDEC_PicEntry_H264 {
    union {
        struct {
            unsigned char Index7Bits : 7;
            unsigned char AssociatedFlag : 1;
        };
        unsigned char bPicEntry;
    };
};

/* H.264 MVC picture parameters structure */
struct _RKVDEC_PicParams_H264 {
    unsigned short  wFrameWidthInMbsMinus1;
    unsigned short  wFrameHeightInMbsMinus1;
    RKVDEC_PicEntry_H264  CurrPic; /* flag is bot field flag */
    unsigned char   num_ref_frames;

    union {
        struct {
            unsigned short  field_pic_flag : 1;
            unsigned short  MbaffFrameFlag : 1;
            unsigned short  residual_colour_transform_flag : 1;
            unsigned short  sp_for_switch_flag : 1;
            unsigned short  chroma_format_idc : 2;
            unsigned short  RefPicFlag : 1;
            unsigned short  constrained_intra_pred_flag : 1;

            unsigned short  weighted_pred_flag : 1;
            unsigned short  weighted_bipred_idc : 2;
            unsigned short  MbsConsecutiveFlag : 1;
            unsigned short  frame_mbs_only_flag : 1;
            unsigned short  transform_8x8_mode_flag : 1;
            unsigned short  MinLumaBipredSize8x8Flag : 1;
            unsigned short  IntraPicFlag : 1;
        };
        unsigned short  wBitFields;
    };
    unsigned char   bit_depth_luma_minus8;
    unsigned char   bit_depth_chroma_minus8;

    unsigned short  Reserved16Bits;
    unsigned int  StatusReportFeedbackNumber;

    RKVDEC_PicEntry_H264  RefFrameList[16]; /* flag LT */
    int  CurrFieldOrderCnt[2];
    int  FieldOrderCntList[16][2];

    signed char     pic_init_qs_minus26;
    signed char     chroma_qp_index_offset;   /* also used for QScb */
    signed char     second_chroma_qp_index_offset; /* also for QScr */
    unsigned char   ContinuationFlag;

    /* remainder for parsing */
    signed char     pic_init_qp_minus26;
    unsigned char   num_ref_idx_l0_active_minus1;
    unsigned char   num_ref_idx_l1_active_minus1;
    unsigned char   Reserved8BitsA;

    unsigned short  FrameNumList[16];
    unsigned short  LongTermPicNumList[16];
    unsigned int    UsedForReferenceFlags;
    unsigned short  NonExistingFrameFlags;
    unsigned short  frame_num;

    unsigned char   log2_max_frame_num_minus4;
    unsigned char   pic_order_cnt_type;
    unsigned char   log2_max_pic_order_cnt_lsb_minus4;
    unsigned char   delta_pic_order_always_zero_flag;

    unsigned char   direct_8x8_inference_flag;
    unsigned char   entropy_coding_mode_flag;
    unsigned char   pic_order_present_flag;
    unsigned char   num_slice_groups_minus1;

    unsigned char   slice_group_map_type;
    unsigned char   deblocking_filter_control_present_flag;
    unsigned char   redundant_pic_cnt_present_flag;
    unsigned char   Reserved8BitsB;
    /* SliceGroupMap is not needed for MVC, as MVC is for high profile only */
    unsigned short  slice_group_change_rate_minus1;
    /* Following are H.264 MVC Specific parameters */
    unsigned char   num_views_minus1;
    unsigned short  view_id[16];
    unsigned char   num_anchor_refs_l0[16];
    unsigned short  anchor_ref_l0[16][16];
    unsigned char   num_anchor_refs_l1[16];
    unsigned short  anchor_ref_l1[16][16];
    unsigned char   num_non_anchor_refs_l0[16];
    unsigned short  non_anchor_ref_l0[16][16];
    unsigned char   num_non_anchor_refs_l1[16];
    unsigned short  non_anchor_ref_l1[16][16];

    unsigned short  curr_view_id;
    unsigned char   anchor_pic_flag;
    unsigned char   inter_view_flag;
    unsigned short  ViewIDList[16];

    unsigned short  curr_layer_id;
    unsigned short  RefPicColmvUsedFlags;
    unsigned short  RefPicFiledFlags;
    unsigned char   RefPicLayerIdList[16];
    unsigned char   scaleing_list_enable_flag;
    unsigned short  UsedForInTerviewflags;
};


#define RKVDECH264_CABAC_TAB_SIZE        (3680 + 128)        /* bytes */
#define RKVDECH264_SPSPPS_SIZE           (256*32 + 128)      /* bytes */
#define RKVDECH264_RPS_SIZE              (128 + 128)         /* bytes */
#define RKVDECH264_SCALING_LIST_SIZE     (6*16+2*64 + 128)   /* bytes */
#define RKVDECH264_ERROR_INFO_SIZE       (256*144*4)         /* bytes */
#define RKVDECH264_DATA_SIZE             (2048 * 1024) 

struct _RKVDEC_PicParams{

};

typedef struct ScalingList {
    uint8_t bScalingLists4x4[6][16];;
    uint8_t bScalingLists8x8[6][64];
} ScalingList;

#define ALIGN(value, x) ((value + (x - 1)) & (~(x - 1)))

/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECH264Context *ff_rkvdec_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdec_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}

static int get_refpic_index(const LPRKVDEC_PicParams_H264 pp, int surface_index)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        if ((pp->RefFrameList[i].bPicEntry & 0x7f) == surface_index) {
            return i;
        }
    }
    return 0xff;
}

static void fill_picture_entry(LPRKVDEC_PicEntry_H264 pic,  unsigned int index, unsigned int flag)
{
    av_assert0((index & 0x7f) == index && (flag & 0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static void fill_picture_parameters(const H264Context *h, LPRKVDEC_PicParams_H264 pp)
{
    const H264Picture *current_picture = h->cur_pic_ptr;
    const PPS *pps = h->ps.pps;
    const SPS *sps = h->ps.sps;
    int i, j;

    memset(pp, 0, sizeof(RKVDEC_PicParams_H264));
    fill_picture_entry(&pp->CurrPic, ff_rkvdec_get_fd(current_picture->f), h->picture_structure == PICT_BOTTOM_FIELD);

    pp->UsedForReferenceFlags = 0;
    pp->NonExistingFrameFlags = 0;
    j = 0;
    for(i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        const H264Picture *r;
        if (j < h->short_ref_count) {
            r = h->short_ref[j++];
        } else {
            r = NULL;
            while (!r && j < h->short_ref_count + 16)
                r = h->long_ref[j++ - h->short_ref_count];
        }
        if (r && r->f) {
            fill_picture_entry(&pp->RefFrameList[i],
                               ff_rkvdec_get_fd(r->f),
                               r->long_ref != 0);

            if ((r->reference & PICT_TOP_FIELD) && r->field_poc[0] != INT_MAX)
                pp->FieldOrderCntList[i][0] = r->field_poc[0];
            if ((r->reference & PICT_BOTTOM_FIELD) && r->field_poc[1] != INT_MAX)
                pp->FieldOrderCntList[i][1] = r->field_poc[1];

            pp->FrameNumList[i] = r->long_ref ? r->pic_id : r->frame_num;
            if (r->reference & PICT_TOP_FIELD)
                pp->UsedForReferenceFlags |= 1 << (2*i + 0);
            if (r->reference & PICT_BOTTOM_FIELD)
                pp->UsedForReferenceFlags |= 1 << (2*i + 1);
        } else {
            pp->RefFrameList[i].bPicEntry = 0xff;
            pp->FieldOrderCntList[i][0]   = 0;
            pp->FieldOrderCntList[i][1]   = 0;
            pp->FrameNumList[i]           = 0;
        }
    }

    pp->wFrameWidthInMbsMinus1        = h->mb_width  - 1;
    pp->wFrameHeightInMbsMinus1       = h->mb_height / (2 - sps->frame_mbs_only_flag) - 1;
    pp->num_ref_frames                = sps->ref_frame_count;

    pp->wBitFields                    = ((h->picture_structure != PICT_FRAME) <<  0) |
                                        ((sps->mb_aff &&
                                        (h->picture_structure == PICT_FRAME)) <<  1) |
                                        (sps->residual_color_transform_flag   <<  2) |
                                        /* sp_for_switch_flag (not implemented by FFmpeg) */
                                        (0                                    <<  3) |
                                        (sps->chroma_format_idc               <<  4) |
                                        ((h->nal_ref_idc != 0)                <<  6) |
                                        (pps->constrained_intra_pred          <<  7) |
                                        (pps->weighted_pred                   <<  8) |
                                        (pps->weighted_bipred_idc             <<  9) |
                                        /* MbsConsecutiveFlag */
                                        (1                                    << 11) |
                                        (sps->frame_mbs_only_flag             << 12) |
                                        (pps->transform_8x8_mode              << 13) |
                                        ((sps->level_idc >= 31)               << 14) |
                                        /* IntraPicFlag (Modified if we detect a non
                                         * intra slice in dxva2_h264_decode_slice) */
                                        (1                                    << 15);

    pp->bit_depth_luma_minus8         = sps->bit_depth_luma - 8;
    pp->bit_depth_chroma_minus8       = sps->bit_depth_chroma - 8; 
    pp->Reserved16Bits                = 3; 
    
    pp->StatusReportFeedbackNumber    = 1;
    pp->CurrFieldOrderCnt[0] = 0;
    if ((h->picture_structure & PICT_TOP_FIELD) && current_picture->field_poc[0] != INT_MAX)
        pp->CurrFieldOrderCnt[0] = current_picture->field_poc[0] & 0xf;
    pp->CurrFieldOrderCnt[1] = 0;
    if ((h->picture_structure & PICT_BOTTOM_FIELD) && current_picture->field_poc[1] != INT_MAX)
        pp->CurrFieldOrderCnt[1] = current_picture->field_poc[1] & 0xf;
    pp->pic_init_qs_minus26           = pps->init_qs - 26;
    pp->chroma_qp_index_offset        = pps->chroma_qp_index_offset[0];
    pp->second_chroma_qp_index_offset = pps->chroma_qp_index_offset[1];
    pp->ContinuationFlag              = 1;
    pp->pic_init_qp_minus26           = pps->init_qp - 26;
    pp->num_ref_idx_l0_active_minus1  = pps->ref_count[0] - 1;
    pp->num_ref_idx_l1_active_minus1  = pps->ref_count[1] - 1;
    pp->Reserved8BitsA                = 0;
    
    pp->frame_num                     = h->poc.frame_num;
    pp->log2_max_frame_num_minus4     = sps->log2_max_frame_num - 4;
    pp->pic_order_cnt_type            = sps->poc_type;
    if (sps->poc_type == 0)
        pp->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_poc_lsb - 4;
    else if (sps->poc_type == 1)
        pp->delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag;
    pp->direct_8x8_inference_flag     = sps->direct_8x8_inference_flag;
    pp->entropy_coding_mode_flag      = pps->cabac;
    pp->pic_order_present_flag        = pps->pic_order_present;
    pp->num_slice_groups_minus1       = pps->slice_group_count - 1;
    pp->slice_group_map_type          = pps->mb_slice_group_map_type;
    pp->deblocking_filter_control_present_flag = pps->deblocking_filter_parameters_present;
    pp->redundant_pic_cnt_present_flag= pps->redundant_pic_cnt_present;
    pp->Reserved8BitsB                = 0;
    pp->slice_group_change_rate_minus1= 0;  /* XXX not implemented by FFmpeg */
    //pp->SliceGroupMap[810];               /* XXX not implemented by FFmpeg */

    pp->curr_view_id = 0;
    pp->curr_layer_id = 0;
    
    for(i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        if (h->DPB[i].field_picture)
            pp->RefPicFiledFlags |= (1 << i);
        pp->RefPicLayerIdList[i] = 0;
    }

    if (sps->scaling_matrix_present ||
        memcmp(sps->scaling_matrix4, pps->scaling_matrix4, sizeof(sps->scaling_matrix4)) ||
        memcmp(sps->scaling_matrix8, pps->scaling_matrix8, sizeof(sps->scaling_matrix8)))
        pp->scaleing_list_enable_flag = 1;
    else
        pp->scaleing_list_enable_flag = 0;
    
}

static void fill_scaling_lists(const H264Context *h, LPRKVDEC_ScalingList_H264 pScalingList)
{
    const PPS *pps = h->ps.pps;
    memcpy(pScalingList->bScalingLists4x4, pps->scaling_matrix4, sizeof(pScalingList->bScalingLists4x4));
    memcpy(pScalingList->bScalingLists8x8, pps->scaling_matrix8, sizeof(pScalingList->bScalingLists8x8));
}

static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    static const unsigned char start_code[] = {0, 0, 1 };    
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset = ctx->stream_data->pkt_size;
    unsigned int left_size = ctx->stream_data->linesize[0] - offset;

    if (data_ptr && left_size < (size + sizeof(start_code))) {
        AVFrame* new_pkt = av_frame_alloc();
        new_pkt->linesize[0] = offset + size + RKVDECH264_DATA_SIZE;
        ctx->allocator->alloc(ctx->allocator_ctx, new_pkt);
        memcpy(new_pkt->data[0], ctx->stream_data->data[0], ctx->stream_data->pkt_size);
        ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
        memcpy(ctx->stream_data, new_pkt, sizeof(AVFrame));
        data_ptr = ctx->stream_data->data[0];
        av_free(new_pkt);
    }

    memcpy(data_ptr + offset, start_code, sizeof(start_code));
    offset += sizeof(start_code);
    memcpy(data_ptr + offset, buffer, size);
    ctx->stream_data->pkt_size += (size + sizeof(start_code));

    av_log(avctx, AV_LOG_INFO, "fill_stream_data pkg_size %d size %d", ctx->stream_data->pkt_size, size);
}


static int rkvdec_h264_regs_gen_pps(AVCodecContext* avctx)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_H264 pp = ctx->pic_param;
    const int len = 32;
    unsigned int scaling_addr;
    int i;
    void *pps_ptr = ctx->pps_data->data[0];
    void *pps_data = av_mallocz(len+10);

    PutBitContext64 bp;
    init_put_bits_a64(&bp, pps_data, 32);
    
    /* sps */
    put_bits_a64(&bp, 4, -1);
    put_bits_a64(&bp, 8, -1);
    put_bits_a64(&bp, 1, -1);
    put_bits_a64(&bp, 2, pp->chroma_format_idc);
    put_bits_a64(&bp, 3, (pp->bit_depth_luma_minus8 + 8));
    put_bits_a64(&bp, 3, (pp->bit_depth_chroma_minus8 + 8));
    put_bits_a64(&bp, 1, 0);
    put_bits_a64(&bp, 4, pp->log2_max_frame_num_minus4);
    put_bits_a64(&bp, 5, pp->num_ref_frames);
    put_bits_a64(&bp, 2, pp->pic_order_cnt_type);
    put_bits_a64(&bp, 4, pp->log2_max_pic_order_cnt_lsb_minus4);
    put_bits_a64(&bp, 1, pp->delta_pic_order_always_zero_flag);
    put_bits_a64(&bp, 9, (pp->wFrameWidthInMbsMinus1 + 1));
    put_bits_a64(&bp, 9, (pp->wFrameHeightInMbsMinus1 + 1));

    put_bits_a64(&bp, 1, pp->frame_mbs_only_flag);
    put_bits_a64(&bp, 1, pp->MbaffFrameFlag);
    put_bits_a64(&bp, 1, pp->direct_8x8_inference_flag);

    put_bits_a64(&bp, 1, 1); // mvc_extension_enable
    put_bits_a64(&bp, 2, (pp->num_views_minus1 + 1));
    put_bits_a64(&bp, 10, pp->view_id[0]);
    put_bits_a64(&bp, 10, pp->view_id[1]);
    put_bits_a64(&bp, 1, pp->num_anchor_refs_l0[0]);
    if (pp->num_anchor_refs_l1[0]) {
        put_bits_a64(&bp, 10, pp->anchor_ref_l1[0][0]);
    } else {
        put_bits_a64(&bp, 10, 0); //!< anchor_ref_l1
    }
    put_bits_a64(&bp, 1, pp->num_non_anchor_refs_l0[0]);
    if (pp->num_non_anchor_refs_l0[0]) {
        put_bits_a64(&bp, 10, pp->non_anchor_ref_l0[0][0]);
    } else {
        put_bits_a64(&bp, 10, 0); //!< non_anchor_ref_l0
    }
    put_bits_a64(&bp, 1, pp->num_non_anchor_refs_l1[0]);
    if (pp->num_non_anchor_refs_l1[0]) {
        put_bits_a64(&bp, 10, pp->non_anchor_ref_l1[0][0]);
    } else {
        put_bits_a64(&bp, 10, 0);//!< non_anchor_ref_l1
    }
    put_align_a64(&bp, 32, 0);

    /* pps */
    put_bits_a64(&bp, 8, -1); //!< pps_pic_parameter_set_id
    put_bits_a64(&bp, 5, -1); //!< pps_seq_parameter_set_id
    put_bits_a64(&bp, 1, pp->entropy_coding_mode_flag);
    put_bits_a64(&bp, 1, pp->pic_order_present_flag);
    put_bits_a64(&bp, 5, pp->num_ref_idx_l0_active_minus1);
    put_bits_a64(&bp, 5, pp->num_ref_idx_l1_active_minus1);
    put_bits_a64(&bp, 1, pp->weighted_pred_flag);
    put_bits_a64(&bp, 2, pp->weighted_bipred_idc);
    put_bits_a64(&bp, 7, pp->pic_init_qp_minus26);
    put_bits_a64(&bp, 6, pp->pic_init_qs_minus26);
    put_bits_a64(&bp, 5, pp->chroma_qp_index_offset);
    put_bits_a64(&bp, 1, pp->deblocking_filter_control_present_flag);
    put_bits_a64(&bp, 1, pp->constrained_intra_pred_flag);
    put_bits_a64(&bp, 1, pp->redundant_pic_cnt_present_flag);
    put_bits_a64(&bp, 1, pp->transform_8x8_mode_flag);
    put_bits_a64(&bp, 5, pp->second_chroma_qp_index_offset);
    put_bits_a64(&bp, 1, pp->scaleing_list_enable_flag);
    scaling_addr = ff_rkvdec_get_fd(ctx->syntax_data);
    scaling_addr |= ((ctx->scaling_list_data->data[0] - ctx->syntax_data->data[0]) << 10);
    put_bits_a64(&bp, 32, scaling_addr);

    for (i = 0; i < 16; i++) {
        int is_long_term = (pp->RefFrameList[i].bPicEntry != 0xff) ? pp->RefFrameList[i].AssociatedFlag : 0;
        put_bits_a64(&bp, 1, is_long_term);
    }
    for (i = 0; i < 16; i++) {
        int voidx = (pp->RefFrameList[i].bPicEntry != 0xff) ? pp->RefPicLayerIdList[i] : 0;
        put_bits_a64(&bp, 1, voidx);
    }
    put_align_a64(&bp, 64, 0);

    for(i = 0; i < 256; i++) {
        memcpy((unsigned char*)pps_ptr + i * len, pps_data, len);
    }
	
    av_free(pps_data);

#ifdef dump_pps
    if (fp1 == NULL)
        fp1 = fopen("hal.bin", "wb");
    fwrite(pps_ptr, 1, 256 * 32, fp1);
    fflush(fp1);
#endif

    return 0;
}

static int rkvdec_h264_regs_gen_rps(AVCodecContext* avctx)
{
    const H264Context *h = avctx->priv_data;
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_H264 pp = ctx->pic_param;
    const int len = RKVDECH264_RPS_SIZE;
    void *rps_ptr = ctx->rps_data->data[0];
    H264SliceContext *sl = &h->slice_ctx[0];
    unsigned int max_frame_num, frame_num_wrap;
    int dpb_valid, bottom_flag, dpb_idx, voidx;
    int i, j;
	
    PutBitContext64 bp;
    init_put_bits_a64(&bp, rps_ptr, len);

    max_frame_num = 1 << (pp->log2_max_frame_num_minus4 + 4);
    for (i = 0; i < 16; i++) {
        if ((pp->NonExistingFrameFlags >> i) & 0x01) {
            frame_num_wrap = 0;
        } else {
            if (pp->RefFrameList[i].AssociatedFlag) {
                frame_num_wrap = pp->FrameNumList[i];
            } else {
                frame_num_wrap = (pp->FrameNumList[i] > pp->frame_num) ?
                                 (pp->FrameNumList[i] - max_frame_num) : pp->FrameNumList[i];
            }
        }

        put_bits_a64(&bp, 16, frame_num_wrap);
    }
	
    for (i = 0; i < 16; i++) {
        put_bits_a64(&bp, 1, 0);//!< NULL
    }
    for (i = 0; i < 16; i++) {
        put_bits_a64(&bp, 1, pp->RefPicLayerIdList[i]); //!< voidx
    }

    /* p ref */
    for (i = 0; i < 32; i++) {
	const H264Picture *r = sl->ref_list[0][i].parent;
	dpb_valid = (r == NULL || get_refpic_index(pp, ff_rkvdec_get_fd(r->f)) == 0xff || sl->slice_type_nos != AV_PICTURE_TYPE_P) ? 0 : 1;
        dpb_idx = dpb_valid ? get_refpic_index(pp, ff_rkvdec_get_fd(r->f)) : 0;
        bottom_flag = dpb_valid ? sl->ref_list[0][i].reference == PICT_BOTTOM_FIELD : 0;
        voidx = dpb_valid ? pp->RefPicLayerIdList[dpb_idx] : 0;
        put_bits_a64(&bp, 5, dpb_idx | (dpb_valid << 4)); //!< dpb_idx
        put_bits_a64(&bp, 1, bottom_flag);
        put_bits_a64(&bp, 1, voidx);
    }

    /* b ref */
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 32; i++) {
	    const H264Picture *r = sl->ref_list[j][i].parent;
            dpb_valid =  (r == NULL || get_refpic_index(pp, ff_rkvdec_get_fd(r->f)) == 0xff || sl->slice_type_nos != AV_PICTURE_TYPE_B) ? 0 : 1;
            dpb_idx = dpb_valid ? get_refpic_index(pp, ff_rkvdec_get_fd(r->f)) : 0;
            bottom_flag = dpb_valid ? sl->ref_list[j][i].reference == PICT_BOTTOM_FIELD : 0;
            voidx = dpb_valid ? pp->RefPicLayerIdList[dpb_idx] : 0;
            put_bits_a64(&bp, 5, dpb_idx | (dpb_valid << 4)); //!< dpb_idx
            put_bits_a64(&bp, 1, bottom_flag);
            put_bits_a64(&bp, 1, voidx);
        }
    }
    put_align_a64(&bp, 128, 0);

#ifdef dump_rps
    if (fp1 == NULL)
        fp1 = fopen("hal.bin", "wb");
    fwrite(rps_ptr, 1, RKVDECH264_RPS_SIZE, fp1);
    fflush(fp1);
#endif

    return 0;
}

static int rkvdec_h264_regs_gen_scanlist(AVCodecContext* avctx)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_H264 pp = ctx->pic_param;
    LPRKVDEC_ScalingList_H264 sl = ctx->scaling_list;
    
    if (pp->scaleing_list_enable_flag) {
        void* scanlist_ptr = ctx->scaling_list_data->data[0];
        int i, j;
        PutBitContext64 bp;

        init_put_bits_a64(&bp, scanlist_ptr, RKVDECH264_SCALING_LIST_SIZE);
        memset(scanlist_ptr, 0, RKVDECH264_SCALING_LIST_SIZE);
        for (i = 0; i < 6; i++) {
            for (j = 0; j < 16; j++)
                put_bits_a64(&bp, 8, sl->bScalingLists4x4[i][j]);
        }
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 64; j++)
                put_bits_a64(&bp, 8, sl->bScalingLists8x8[i][j]);
        }
    }

#ifdef dump_sl
    if (fp1 == NULL)
        fp1 = fopen("hal.bin", "wb");
    fwrite(scanlist_ptr, 1, RKVDECH264_SCALING_LIST_SIZE, fp1);
    fflush(fp1);
#endif

    return 0;
}

static int rkvdec_h264_regs_gen_reg(AVCodecContext *avctx)
{
    H264Context * const h = avctx->priv_data;
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);    
    LPRKVDEC_PicParams_H264 pp = ctx->pic_param;
    LPRKVDEC_H264_Regs hw_regs = ctx->hw_regs;
    unsigned int* p_regs = (unsigned int*)hw_regs;
    H264Picture * const pic = h->cur_pic_ptr;
    unsigned int mb_width, mb_height, mv_size;
    int i, ref_index = -1, near_index = -1;

    memset(hw_regs, 0, sizeof(RKVDEC_H264_Regs));

    hw_regs->swreg2_sysctrl.sw_dec_mode = 1;
    hw_regs->swreg5_stream_rlc_len.sw_stream_len = ALIGN(ctx->stream_data->pkt_size, 16);
    hw_regs->swreg3_picpar.sw_slice_num_lowbits = 0x7ff;
    hw_regs->swreg3_picpar.sw_slice_num_highbit = 1;
    hw_regs->swreg3_picpar.sw_y_hor_virstride = ALIGN(pic->f->width, 16) / 16;
    hw_regs->swreg3_picpar.sw_uv_hor_virstride =  ALIGN(pic->f->width, 16) / 16;
    hw_regs->swreg8_y_virstride.sw_y_virstride = ALIGN(ALIGN(pic->f->width, 16) * ALIGN(pic->f->height, 16), 16) / 16;
    hw_regs->swreg9_yuv_virstride.sw_yuv_virstride = ALIGN(ALIGN(pic->f->width, 16) * ALIGN(pic->f->height, 16) * 3 / 2, 16) / 16;
    mb_width = pp->wFrameWidthInMbsMinus1 + 1;
    mb_height = (2 - pp->frame_mbs_only_flag) * (pp->wFrameHeightInMbsMinus1 + 1);
    mv_size = mb_width * mb_height * 8;
    hw_regs->compare_len = (hw_regs->swreg9_yuv_virstride.sw_yuv_virstride + mv_size) * 2;

    hw_regs->swreg40_cur_poc.sw_cur_poc = pp->CurrFieldOrderCnt[0];
    hw_regs->swreg74_h264_cur_poc1.sw_h264_cur_poc1 = pp->CurrFieldOrderCnt[1];
    hw_regs->swreg7_decout_base.sw_decout_base = ff_rkvdec_get_fd(pic->f);

    for (i = 0; i < 15; i++) {
        hw_regs->swreg25_39_refer0_14_poc[i] = (i & 1) ? pp->FieldOrderCntList[i / 2][1] : pp->FieldOrderCntList[i / 2][0];
        hw_regs->swreg49_63_refer15_29_poc[i] = (i & 1) ? pp->FieldOrderCntList[(i + 15) / 2][0] : pp->FieldOrderCntList[(i + 15) / 2][1];
        hw_regs->swreg10_24_refer0_14_base[i].sw_ref_field = (pp->RefPicFiledFlags >> i) & 0x01;
        hw_regs->swreg10_24_refer0_14_base[i].sw_ref_topfield_used = (pp->UsedForReferenceFlags >> (2 * i + 0)) & 0x01;
        hw_regs->swreg10_24_refer0_14_base[i].sw_ref_botfield_used = (pp->UsedForReferenceFlags >> (2 * i + 1)) & 0x01;
        hw_regs->swreg10_24_refer0_14_base[i].sw_ref_colmv_use_flag = (pp->RefPicColmvUsedFlags >> i) & 0x01;
        hw_regs->swreg25_39_refer0_14_poc[i] = hw_regs->swreg25_39_refer0_14_poc[i];
        hw_regs->swreg10_24_refer0_14_base[i].sw_ref_colmv_use_flag = 0x01;

        if (pp->RefFrameList[i].bPicEntry != 0xff) {
            ref_index  = pp->RefFrameList[i].Index7Bits;
            near_index = pp->RefFrameList[i].Index7Bits;
        } else {
            ref_index = (near_index < 0) ? pp->CurrPic.Index7Bits : near_index;
        }
        hw_regs->swreg10_24_refer0_14_base[i].sw_refer_base = ref_index;
    }
    
    hw_regs->swreg72_refer30_poc = pp->FieldOrderCntList[15][0];
    hw_regs->swreg73_refer31_poc = pp->FieldOrderCntList[15][1];
    hw_regs->swreg48_refer15_base.sw_ref_field = (pp->RefPicFiledFlags >> 15) & 0x01;
    hw_regs->swreg48_refer15_base.sw_ref_topfield_used = (pp->UsedForReferenceFlags >> 30) & 0x01;
    hw_regs->swreg48_refer15_base.sw_ref_botfield_used = (pp->UsedForReferenceFlags >> 31) & 0x01;
    hw_regs->swreg48_refer15_base.sw_ref_colmv_use_flag = (pp->RefPicColmvUsedFlags >> 15) & 0x01;
    hw_regs->swreg48_refer15_base.sw_ref_colmv_use_flag = 0x01;
    
    if (pp->RefFrameList[15].bPicEntry != 0xff) {
        ref_index = pp->RefFrameList[15].Index7Bits;
    } else {
        ref_index = (near_index < 0) ? pp->CurrPic.Index7Bits : near_index;
    }
    hw_regs->swreg48_refer15_base.sw_refer_base = ref_index;

    hw_regs->swreg4_strm_rlc_base.sw_strm_rlc_base = ff_rkvdec_get_fd(ctx->stream_data);
    hw_regs->swreg6_cabactbl_prob_base.sw_cabactbl_base = ff_rkvdec_get_fd(ctx->syntax_data);
    hw_regs->swreg41_rlcwrite_base.sw_rlcwrite_base = hw_regs->swreg4_strm_rlc_base.sw_strm_rlc_base;

    hw_regs->swreg42_pps_base.sw_pps_base = ff_rkvdec_get_fd(ctx->syntax_data) + ((ctx->pps_data->data[0] - ctx->syntax_data->data[0]) << 10);
    hw_regs->swreg43_rps_base.sw_rps_base = ff_rkvdec_get_fd(ctx->syntax_data) + ((ctx->rps_data->data[0] - ctx->syntax_data->data[0]) << 10);    
    hw_regs->swreg75_h264_errorinfo_base.sw_errorinfo_base = ff_rkvdec_get_fd(ctx->syntax_data) + ((ctx->errorinfo_data->data[0] - ctx->syntax_data->data[0]) << 10);

    p_regs[64] = 0;
    p_regs[65] = 0;
    p_regs[66] = 0;
    p_regs[67] = 0x000000ff;
    p_regs[44] = 0xffffffff;
    p_regs[77] = 0xffffffff;
    p_regs[1] |= 0x00000061;

#ifdef debug_regs
    unsigned char *p = hw_regs;
    for (i = 0; i < 78 ; i++) {
        av_log(avctx, AV_LOG_DEBUG, "RK_H264H_DEC: regs[%02d]=%08X\n", i, *((unsigned int*)p));
        p += 4;
    }
#endif
    return 0;
}


 /** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_h264_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    H264Context * const h = avctx->priv_data;
    
    av_log(avctx, AV_LOG_INFO, "RK_H264_DEC: rkvdec_h264_start_frame\n");
    fill_picture_parameters(h, ctx->pic_param);
    fill_scaling_lists(h, ctx->scaling_list);
    ctx->stream_data->pkt_size = 0;

    return 0;
}

/** End a hardware decoding based frame. */
static int rkvdec_h264_end_frame(AVCodecContext *avctx)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    RKVDECH264HwReq req;
    H264Context * const h = avctx->priv_data;
    int ret;

    av_log(avctx, AV_LOG_INFO, "RK_H264_DEC: rkvdec_h264_end_frame\n");
    rkvdec_h264_regs_gen_pps(avctx);
    rkvdec_h264_regs_gen_rps(avctx);
    rkvdec_h264_regs_gen_scanlist(avctx);
    rkvdec_h264_regs_gen_reg(avctx);

    req.req = (unsigned int*)ctx->hw_regs;
    req.size = 78 * sizeof(unsigned int);

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
    if (fp1 == NULL)
       fp1 = fopen("hal.bin", "wb");
    av_log(avctx, AV_LOG_INFO, "write cur_pic fd %d", ff_rkvdec_get_fd(h->cur_pic_ptr->f));
    fwrite(h->cur_pic_ptr->f->data[0],1, ALIGN(h->cur_pic_ptr->width, 16) * ALIGN(h->cur_pic_ptr->height, 16) * 1.5, fp1);
    fflush(fp1);
#endif
    
    return 0;
}


/** Decode the given h264 slice with RKVDEC. */
static int rkvdec_h264_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{    
    av_log(avctx, AV_LOG_INFO, "RK_H264_DEC: rkvdec_h264_decode_slice size:%d\n", size);
    fill_stream_data(avctx, buffer, size);
    return 0;
}

static int rkvdec_h264_context_init(AVCodecContext *avctx)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);
    int ret;
    
    av_log(avctx, AV_LOG_INFO, "RK_H264_DEC: rkvdec_h264_context_init\n");
    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }

    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_H264_Regs));
    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_H264));
    ctx->scaling_list = av_mallocz(sizeof(RKVDEC_ScalingList_H264));

    ctx->syntax_data = av_frame_alloc();
    ctx->syntax_data->linesize[0] = RKVDECH264_CABAC_TAB_SIZE + 
                                    RKVDECH264_SPSPPS_SIZE + 
                                    RKVDECH264_RPS_SIZE + 
                                    RKVDECH264_SCALING_LIST_SIZE + 
                                    RKVDECH264_ERROR_INFO_SIZE;    
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->syntax_data);
    
    ctx->cabac_table_data = av_frame_alloc();
    ctx->cabac_table_data->linesize[0] = RKVDECH264_CABAC_TAB_SIZE;
    ctx->cabac_table_data->data[0] = ctx->syntax_data->data[0];
    memcpy(ctx->cabac_table_data->data[0], cabac_table, sizeof(cabac_table));

    ctx->pps_data = av_frame_alloc();
    ctx->pps_data->linesize[0] = RKVDECH264_SPSPPS_SIZE;
    ctx->pps_data->data[0] = ctx->syntax_data->data[0] + RKVDECH264_CABAC_TAB_SIZE;

    ctx->rps_data = av_frame_alloc();
    ctx->rps_data->linesize[0] = RKVDECH264_RPS_SIZE;
    ctx->rps_data->data[0] = ctx->pps_data->data[0] + RKVDECH264_SPSPPS_SIZE;

    ctx->scaling_list_data = av_frame_alloc();
    ctx->scaling_list_data->linesize[0] = RKVDECH264_SCALING_LIST_SIZE;
    ctx->scaling_list_data->data[0] = ctx->rps_data->data[0] + RKVDECH264_RPS_SIZE;

    ctx->errorinfo_data = av_frame_alloc();
    ctx->errorinfo_data->linesize[0] = RKVDECH264_ERROR_INFO_SIZE;
    ctx->errorinfo_data->data[0] = ctx->scaling_list_data->data[0] + RKVDECH264_SCALING_LIST_SIZE;

    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDECH264_DATA_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);

    if (ctx->vpu_socket <= 0) 
        ctx->vpu_socket = open(name_rkvdec, O_RDWR);
    
    if (ctx->vpu_socket < 0) {
        av_log(avctx, AV_LOG_ERROR, "failed to open rkvdec.");
        return -1;
    }

    if(ioctl(ctx->vpu_socket, VPU_IOC_SET_CLIENT_TYPE, 0x1)) {
        av_log(avctx, AV_LOG_ERROR, "failed to ioctl rkvdec.");
        return -1;
    }

    return 0;
}

static int rkvdec_h264_context_uninit(AVCodecContext *avctx)
{
    RKVDECH264Context * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, AV_LOG_INFO, "RK_H264_DEC: rkvdec_h264_context_uninit\n");
    ctx->allocator->free(ctx->allocator_ctx, ctx->syntax_data);
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
    ctx->allocator->close(ctx->allocator_ctx);

    av_free(ctx->syntax_data);
    av_free(ctx->pps_data);
    av_free(ctx->rps_data);
    av_free(ctx->scaling_list_data);
    av_free(ctx->errorinfo_data);
    av_free(ctx->stream_data);

    av_free(ctx->hw_regs);
    av_free(ctx->pic_param);
    av_free(ctx->scaling_list);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }
    
    return 0;
}

AVHWAccel ff_h264_rkvdec_hwaccel = {
    .name                 = "h264_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_H264,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .start_frame          = rkvdec_h264_start_frame,
    .end_frame            = rkvdec_h264_end_frame,
    .decode_slice         = rkvdec_h264_decode_slice,
    .init                 = rkvdec_h264_context_init,
    .uninit               = rkvdec_h264_context_uninit,
    .priv_data_size       = sizeof(RKVDECH264Context),
    .frame_priv_data_size = sizeof(RKVDECH264FrameData),
};


