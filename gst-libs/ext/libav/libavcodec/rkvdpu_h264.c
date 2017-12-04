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
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"
#include "libavcodec/golomb.h"
#include "hwaccel.h"
#include "rkvdpu_h264.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define LOG_LEVEL AV_LOG_DEBUG

typedef struct _RKVDPUH264Context RKVDPUH264Context;
typedef struct H264dVdpuRegs_t RKVDPU_H264_Regs, *LPRKVDPU_H264_Regs;
typedef struct _RKVDPU_PicEntry_H264 RKVDPU_PicEntry_H264, *LPRKVDPU_PicEntry_H264;
typedef struct _RKVDPU_PicParams_H264 RKVDPU_PicParams_H264, *LPRKVDPU_PicParams_H264;
typedef struct _RKVDPUH264HwReq RKVDPUH264HwReq;
typedef struct _RKVDPU_FrameData_H264 RKVDPU_FrameData_H264;

#define RKVDPU_H264_CABAC_TAB_SIZE        (3680)        /* bytes */
#define RKVDPU_H264_SCALING_LIST_SIZE     (6*16 + 2*64) /* bytes */
#define RKVDPU_H264_POC_BUF_SIZE          (34*4)        /* bytes */
#define RKVDPU_H264_DATA_SIZE             (2048 * 1024) /* bytes */

#define ALIGN(value, x) ((value + (x - 1)) & (~(x - 1)))

struct _RKVDPU_PicEntry_H264 {
    union {
        struct {
            unsigned char Index7Bits : 7;
            unsigned char AssociatedFlag : 1;
        };
        unsigned char bPicEntry;
    };
};

struct _RKVDPUH264Context {
    signed int vpu_socket;
    LPRKVDPU_H264_Regs hw_regs;
    LPRKVDPU_PicParams_H264 pic_param;
    RKVDPU_PicEntry_H264 HwRefFrameList[16];
    AVFrame* stream_data;
    AVFrame* syntax_data;
    AVFrame* cabac_table_data;
    AVFrame* scaling_list_data;
    AVFrame* poc_buf_data;
    os_allocator *allocator;
    void *allocator_ctx;
    pthread_mutex_t hwaccel_mutex;
};

struct _RKVDPU_FrameData_H264 {
    AVFrame* data;
};

struct _RKVDPUH264HwReq {
    unsigned int *req;
    unsigned int  size;
} ;


/* H.264 MVC picture parameters structure */
struct _RKVDPU_PicParams_H264 {
    unsigned short  wFrameWidthInMbsMinus1;
    unsigned short  wFrameHeightInMbsMinus1;
    RKVDPU_PicEntry_H264  CurrPic; /* flag is bot field flag */
    RKVDPU_PicEntry_H264  CurrMv;
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

    RKVDPU_PicEntry_H264  RefFrameList[16]; /* flag LT */
    RKVDPU_PicEntry_H264  RefColmvList[16];

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

    unsigned int    drpm_used_bits;
    unsigned int    poc_used_bits;
    unsigned int    idr_pic_id;
};


/** Extract rkvdpu_context from an AVCodecContext */
static inline RKVDPUH264Context *ff_rkvdpu_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdpu_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}

static int get_refpic_index(const LPRKVDPU_PicParams_H264 pp, int surface_index)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        if ((pp->RefFrameList[i].bPicEntry & 0x7f) == surface_index && surface_index > 0) {
            return i;
        }
    }

    return 0xff;
}

static void fill_picture_entry(LPRKVDPU_PicEntry_H264 pic,  unsigned int index, unsigned int flag)
{
    av_assert0((index & 0x7f) == index && (flag & 0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static void fill_picture_parameters(const H264Context *h, LPRKVDPU_PicParams_H264 pp)
{
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(h->avctx);
    const H264Picture *current_picture = h->cur_pic_ptr;
    const PPS *pps = h->ps.pps;
    const SPS *sps = h->ps.sps;
    int i, j;

    memset(pp, 0, sizeof(RKVDPU_PicParams_H264));
    fill_picture_entry(&pp->CurrPic, ff_rkvdpu_get_fd(current_picture->f), h->picture_structure == PICT_BOTTOM_FIELD);

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
                               ff_rkvdpu_get_fd(r->f),
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

            if (r->field_picture)
                pp->RefPicFiledFlags |= (1 << i);
        } else {
            pp->RefFrameList[i].bPicEntry = 0xff;
            pp->FieldOrderCntList[i][0]   = 0;
            pp->FieldOrderCntList[i][1]   = 0;
            pp->FrameNumList[i]           = 0;
            if (j - 1 < h->short_ref_count) {
                av_log(h, AV_LOG_WARNING, "fill_picture_parameters miss short ref\n");
                current_picture->f->decode_error_flags = FF_DECODE_ERROR_MISSING_REFERENCE;
            }
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
        pp->CurrFieldOrderCnt[0] = current_picture->field_poc[0];
    pp->CurrFieldOrderCnt[1] = 0;
    if ((h->picture_structure & PICT_BOTTOM_FIELD) && current_picture->field_poc[1] != INT_MAX)
        pp->CurrFieldOrderCnt[1] = current_picture->field_poc[1];
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

    if (sps->scaling_matrix_present ||
        memcmp(sps->scaling_matrix4, pps->scaling_matrix4, sizeof(sps->scaling_matrix4)) ||
        memcmp(sps->scaling_matrix8, pps->scaling_matrix8, sizeof(sps->scaling_matrix8)))
        pp->scaleing_list_enable_flag = 1;
    else
        pp->scaleing_list_enable_flag = 0;
    
}

static int adjust_picture_references(const H264Context *h, LPRKVDPU_PicParams_H264 pp)
{
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(h->avctx);
    int i, j, find;
    RKVDPU_PicEntry_H264* old_pic_entry, *new_pic_entry;

    for (i = 0; i < FF_ARRAY_ELEMS(ctx->HwRefFrameList); i++) {
        old_pic_entry = &ctx->HwRefFrameList[i];
        if (old_pic_entry->bPicEntry > 0 && old_pic_entry->bPicEntry != 0xff) {
            find = 0;
            for (j = 0; j < FF_ARRAY_ELEMS(pp->RefFrameList); j++) {
                new_pic_entry = &pp->RefFrameList[j];
                if (new_pic_entry->bPicEntry > 0 && new_pic_entry->bPicEntry != 0xff) {
                    if (old_pic_entry->bPicEntry == new_pic_entry->bPicEntry) {
                        new_pic_entry->bPicEntry = 0xff;
                        find = 1;
                        break;
                    }
                }
            }
            if (!find)
                old_pic_entry->bPicEntry = 0xff;
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        new_pic_entry = &pp->RefFrameList[i];
        if (new_pic_entry->bPicEntry > 0 && new_pic_entry->bPicEntry != 0xff) {
            for (j = 0; j < FF_ARRAY_ELEMS(ctx->HwRefFrameList); j++) {
                old_pic_entry = &ctx->HwRefFrameList[j];
                if (old_pic_entry->bPicEntry <= 0 || old_pic_entry->bPicEntry == 0xff) {
                    old_pic_entry->bPicEntry = new_pic_entry->bPicEntry;
                    break;
                }
            }
        }
    }

    pp->UsedForReferenceFlags = 0;
    pp->RefPicFiledFlags = 0;

    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        pp->RefFrameList[i].bPicEntry = ctx->HwRefFrameList[i].bPicEntry;
        pp->FrameNumList[i] = 0;
        const H264Picture *r;
        for (j = 0; j < h->short_ref_count + 16; j++) {
            r = NULL;
            if (j < h->short_ref_count)
                r = h->short_ref[j];
            else {
                while (!r && j < h->short_ref_count + 16)
                    r = h->long_ref[j++ - h->short_ref_count];
            }
            if (r && r->f && ff_rkvdpu_get_fd(r->f) == pp->RefFrameList[i].Index7Bits) {
                if ((r->reference & PICT_TOP_FIELD) && r->field_poc[0] != INT_MAX)
                    pp->FieldOrderCntList[i][0] = r->field_poc[0];
                if ((r->reference & PICT_BOTTOM_FIELD) && r->field_poc[1] != INT_MAX)
                    pp->FieldOrderCntList[i][1] = r->field_poc[1];
                pp->FrameNumList[i] = r->long_ref ? r->pic_id : r->frame_num;
                if (r->reference & PICT_TOP_FIELD)
                    pp->UsedForReferenceFlags |= 1 << (2*i + 0);
                if (r->reference & PICT_BOTTOM_FIELD)
                    pp->UsedForReferenceFlags |= 1 << (2*i + 1);
                if (r->field_picture)
                    pp->RefPicFiledFlags |= (1 << i);
            }
        }
    }

    return 0;
}

static int fill_slice_parameters(const H264Context *h, LPRKVDPU_PicParams_H264 pp)
{
    H264SliceContext *sl = &h->slice_ctx[0];
    GetBitContext gb = sl->gb;
    const SPS *sps;
    const PPS *pps;
    int nal_type, ref_idc, poc_used_bits, drpm_used_bits;

    gb.index = 0;
    poc_used_bits = drpm_used_bits = 0;

    get_bits1(&gb);
    ref_idc = get_bits(&gb, 2); // ref_idc
    nal_type = get_bits(&gb, 5); // nal_type

    pps = (const PPS*)h->ps.pps_list[sl->pps_id]->data;
    sps = (const SPS*)h->ps.sps_list[pps->sps_id]->data;

    get_ue_golomb_long(&gb); // first mb
    get_ue_golomb_31(&gb); // slice type
    get_ue_golomb(&gb); // pps id

    get_bits(&gb, sps->log2_max_frame_num); //frame num

    if (!sps->frame_mbs_only_flag) {
        // field_pic_flag
        if (get_bits1(&gb)) { 
            get_bits1(&gb); // bottom_field_flag 
        }
    }

    if (nal_type == H264_NAL_IDR_SLICE)
        pp->idr_pic_id = get_ue_golomb_long(&gb); // idr_pic_id

    poc_used_bits = get_bits_count(&gb);

    if (sps->poc_type == 0) {
        get_bits(&gb, sps->log2_max_poc_lsb); //sl->poc_lsb
        if (pps->pic_order_present == 1 && sl->picture_structure == PICT_FRAME)
            get_se_golomb(&gb); // sl->delta_poc_bottom
    }

    if (sps->poc_type == 1 && !sps->delta_pic_order_always_zero_flag) {
        get_se_golomb(&gb); // sl->delta_poc[0]
        if (pps->pic_order_present == 1 && sl->picture_structure == PICT_FRAME)
            get_se_golomb(&gb); // sl->delta_poc[1]
    }

    poc_used_bits = get_bits_count(&gb) - poc_used_bits;

    if (pps->redundant_pic_cnt_present)
        get_ue_golomb(&gb); // sl->redundant_pic_count

    if (sl->slice_type_nos == AV_PICTURE_TYPE_B)
        get_bits1(&gb); // sl->direct_spatial_mv_pred

    // ff_h264_parse_ref_count
    if (sl->slice_type_nos != AV_PICTURE_TYPE_I) {
        // num_ref_idx_active_override_flag
        if (get_bits1(&gb)) {
            get_ue_golomb(&gb) + 1; // ref_count[0]
            if (sl->slice_type_nos == AV_PICTURE_TYPE_B)
                get_ue_golomb(&gb) + 1; // ref_count[1]
        }
    }

    // ff_h264_decode_ref_pic_list_reordering
    if (sl->slice_type_nos != AV_PICTURE_TYPE_I) {
        for (int list = 0; list < sl->list_count; list++) {
            if (!get_bits1(&gb)) // ref_pic_list_modification_flag_l[01]
                continue;
            for (int index = 0; ; index++) {
                unsigned int op = get_ue_golomb_31(&gb);
                if (op == 3)
                    break;
                get_ue_golomb_long(&gb); // val
            }
        }
    }

    // ff_h264_pred_weight_table
    if ((pps->weighted_pred && sl->slice_type_nos == AV_PICTURE_TYPE_P) ||
        (pps->weighted_bipred_idc == 1 && sl->slice_type_nos == AV_PICTURE_TYPE_B)) {
        get_ue_golomb(&gb); // pwt->luma_log2_weight_denom
        if (sps->chroma_format_idc)
            get_ue_golomb(&gb); // pwt->chroma_log2_weight_denom

        for (int list = 0; list < 2; list++) {
            for (int i = 0; i < sl->ref_count[list]; i++) {
                // luma_weight_flag
                if (get_bits1(&gb)) {
                    get_se_golomb(&gb); // pwt->luma_weight[i][list][0]
                    get_se_golomb(&gb); // pwt->luma_weight[i][list][1]
                }

                if (sps->chroma_format_idc) {
                    // chroma_weight_flag
                    if (get_bits1(&gb)) {
                        for (int j = 0; j < 2; j++) {
                            get_se_golomb(&gb); // chroma_weight[i][list][j][0]
                            get_se_golomb(&gb); // chroma_weight[i][list][j][1]
                        }
                    }
                }
            }
            if (sl->slice_type_nos != AV_PICTURE_TYPE_B)
                break;
        }
    }

    // ff_h264_decode_ref_pic_marking
    drpm_used_bits = get_bits_count(&gb);

    if (ref_idc) {
        if (nal_type == H264_NAL_IDR_SLICE) { // FIXME fields
            skip_bits1(&gb); // broken_link
            get_bits1(&gb);
        } else {
            // explicit_ref_marking
            if (get_bits1(&gb)) {
                for (int i = 0; i < MAX_MMCO_COUNT; i++) {
                    MMCOOpcode opcode = get_ue_golomb_31(&gb);
                    if (opcode == MMCO_SHORT2UNUSED || opcode == MMCO_SHORT2LONG)
                        get_ue_golomb_long(&gb);
                    else if (opcode == MMCO_SHORT2LONG || opcode == MMCO_LONG2UNUSED ||
                        opcode == MMCO_LONG || opcode == MMCO_SET_MAX_LONG)
                        get_ue_golomb_31(&gb);
                    else if (opcode == MMCO_END)
                        break;
                }
            }
        }
    }
    drpm_used_bits = get_bits_count(&gb) - drpm_used_bits;

    pp->drpm_used_bits = drpm_used_bits;
    pp->poc_used_bits = poc_used_bits;

    return 0;
}

static int rkvdpu_h264_start_frame(AVCodecContext          *avctx,
                                   av_unused const uint8_t *buffer,
                                   av_unused uint32_t       size)
{
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(avctx);
    H264Context * const h = avctx->priv_data;

    av_log(avctx, LOG_LEVEL, "RK_H264_DEC: rkvdpu_h264_start_frame.\n");
    pthread_mutex_lock(&ctx->hwaccel_mutex);
    fill_picture_parameters(h, ctx->pic_param);
    fill_slice_parameters(h, ctx->pic_param);
    
    ctx->stream_data->pkt_size = 0;

    return 0;
}

static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(avctx);
    static const unsigned char start_code[] = {0, 0, 1 };    
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset = ctx->stream_data->pkt_size;
    unsigned int left_size = ctx->stream_data->linesize[0] - offset;

    if (data_ptr && left_size < (size + sizeof(start_code))) {
        AVFrame* new_pkt = av_frame_alloc();
        new_pkt->linesize[0] = offset + size + RKVDPU_H264_DATA_SIZE;
        ctx->allocator->alloc(ctx->allocator_ctx, new_pkt);
        new_pkt->pkt_size = offset;
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

    av_log(avctx, LOG_LEVEL, "fill_stream_data pkg_size %d size %d.\n", ctx->stream_data->pkt_size, size);
}

static int rkvdpu_h264_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    av_log(avctx, LOG_LEVEL, "RK_H264_DEC: rkvdpu_h264_decode_slice size:%d.\n", size);
    fill_stream_data(avctx, buffer, size);

    return 0;
}

static int set_refer_pic_idx(RKVDPU_H264_Regs *p_regs, uint32_t i, uint32_t val)
{
    switch (i) {
    case 0:
        p_regs->sw76.num_ref_idx0 = val;
        break;
    case 1:
        p_regs->sw76.num_ref_idx1 = val;
        break;
    case 2:
        p_regs->sw77.num_ref_idx2 = val;
        break;
    case 3:
        p_regs->sw77.num_ref_idx3 = val;
        break;
    case 4:
        p_regs->sw78.num_ref_idx4 = val;
        break;
    case 5:
        p_regs->sw78.num_ref_idx5 = val;
        break;
    case 6:
        p_regs->sw79.num_ref_idx6 = val;
        break;
    case 7:
        p_regs->sw79.num_ref_idx7 = val;
        break;
    case 8:
        p_regs->sw80.num_ref_idx8 = val;
        break;
    case 9:
        p_regs->sw80.num_ref_idx9 = val;
        break;
    case 10:
        p_regs->sw81.num_ref_idx10 = val;
        break;
    case 11:
        p_regs->sw81.num_ref_idx11 = val;
        break;
    case 12:
        p_regs->sw82.num_ref_idx12 = val;
        break;
    case 13:
        p_regs->sw82.num_ref_idx13 = val;
        break;
    case 14:
        p_regs->sw83.num_ref_idx14 = val;
        break;
    case 15:
        p_regs->sw83.num_ref_idx15 = val;
        break;
    default:
        break;
    }

    return 0;
}

static int set_refer_pic_list_p(RKVDPU_H264_Regs *p_regs, uint32_t i,
                                    uint16_t val)
{
    switch (i) {
    case 0:
        p_regs->sw106.init_reflist_pf0 = val;
        break;
    case 1:
        p_regs->sw106.init_reflist_pf1 = val;
        break;
    case 2:
        p_regs->sw106.init_reflist_pf2 = val;
        break;
    case 3:
        p_regs->sw106.init_reflist_pf3 = val;
        break;
    case 4:
        p_regs->sw74.init_reflist_pf4 = val;
        break;
    case 5:
        p_regs->sw74.init_reflist_pf5 = val;
        break;
    case 6:
        p_regs->sw74.init_reflist_pf6 = val;
        break;
    case 7:
        p_regs->sw74.init_reflist_pf7 = val;
        break;
    case 8:
        p_regs->sw74.init_reflist_pf8 = val;
        break;
    case 9:
        p_regs->sw74.init_reflist_pf9 = val;
        break;
    case 10:
        p_regs->sw75.init_reflist_pf10 = val;
        break;
    case 11:
        p_regs->sw75.init_reflist_pf11 = val;
        break;
    case 12:
        p_regs->sw75.init_reflist_pf12 = val;
        break;
    case 13:
        p_regs->sw75.init_reflist_pf13 = val;
        break;
    case 14:
        p_regs->sw75.init_reflist_pf14 = val;
        break;
    case 15:
        p_regs->sw75.init_reflist_pf15 = val;
        break;
    default:
        break;
    }

    return 0;
}

static int set_refer_pic_list_b0(RKVDPU_H264_Regs *p_regs, uint32_t i,
                                     uint16_t val)
{
    switch (i) {
    case 0:
        p_regs->sw100.init_reflist_df0 = val;
        break;
    case 1:
        p_regs->sw100.init_reflist_df1 = val;
        break;
    case 2:
        p_regs->sw100.init_reflist_df2 = val;
        break;
    case 3:
        p_regs->sw100.init_reflist_df3 = val;
        break;
    case 4:
        p_regs->sw100.init_reflist_df4 = val;
        break;
    case 5:
        p_regs->sw100.init_reflist_df5 = val;
        break;
    case 6:
        p_regs->sw101.init_reflist_df6 = val;
        break;
    case 7:
        p_regs->sw101.init_reflist_df7 = val;
        break;
    case 8:
        p_regs->sw101.init_reflist_df8 = val;
        break;
    case 9:
        p_regs->sw101.init_reflist_df9 = val;
        break;
    case 10:
        p_regs->sw101.init_reflist_df10 = val;
        break;
    case 11:
        p_regs->sw101.init_reflist_df11 = val;
        break;
    case 12:
        p_regs->sw102.init_reflist_df12 = val;
        break;
    case 13:
        p_regs->sw102.init_reflist_df13 = val;
        break;
    case 14:
        p_regs->sw102.init_reflist_df14 = val;
        break;
    case 15:
        p_regs->sw102.init_reflist_df15 = val;
        break;
    default:
        break;
    }

    return 0;
}

static int set_refer_pic_list_b1(RKVDPU_H264_Regs *p_regs, uint32_t i,
                                     uint16_t val)
{
    switch (i) {
    case 0:
        p_regs->sw103.init_reflist_db0 = val;
        break;
    case 1:
        p_regs->sw103.init_reflist_db1 = val;
        break;
    case 2:
        p_regs->sw103.init_reflist_db2 = val;
        break;
    case 3:
        p_regs->sw103.init_reflist_db3 = val;
        break;
    case 4:
        p_regs->sw103.init_reflist_db4 = val;
        break;
    case 5:
        p_regs->sw103.init_reflist_db5 = val;
        break;
    case 6:
        p_regs->sw104.init_reflist_db6 = val;
        break;
    case 7:
        p_regs->sw104.init_reflist_db7 = val;
        break;
    case 8:
        p_regs->sw104.init_reflist_db8 = val;
        break;
    case 9:
        p_regs->sw104.init_reflist_db9 = val;
        break;
    case 10:
        p_regs->sw104.init_reflist_db10 = val;
        break;
    case 11:
        p_regs->sw104.init_reflist_db11 = val;
        break;
    case 12:
        p_regs->sw105.init_reflist_db12 = val;
        break;
    case 13:
        p_regs->sw105.init_reflist_db13 = val;
        break;
    case 14:
        p_regs->sw105.init_reflist_db14 = val;
        break;
    case 15:
        p_regs->sw105.init_reflist_db15 = val;
        break;
    default:
        break;
    }

    return 0;
}

static int set_refer_pic_base_addr(RKVDPU_H264_Regs *p_regs, uint32_t i,
                                       uint32_t val)
{
    switch (i) {
    case 0:
        p_regs->sw84.ref0_st_addr = val;
        break;
    case 1:
        p_regs->sw85.ref1_st_addr = val;
        break;
    case 2:
        p_regs->sw86.ref2_st_addr = val;
        break;
    case 3:
        p_regs->sw87.ref3_st_addr = val;
        break;
    case 4:
        p_regs->sw88.ref4_st_addr = val;
        break;
    case 5:
        p_regs->sw89.ref5_st_addr = val;
        break;
    case 6:
        p_regs->sw90.ref6_st_addr = val;
        break;
    case 7:
        p_regs->sw91.ref7_st_addr = val;
        break;
    case 8:
        p_regs->sw92.ref8_st_addr = val;
        break;
    case 9:
        p_regs->sw93.ref9_st_addr = val;
        break;
    case 10:
        p_regs->sw94.ref10_st_addr = val;
        break;
    case 11:
        p_regs->sw95.ref11_st_addr = val;
        break;
    case 12:
        p_regs->sw96.ref12_st_addr = val;
        break;
    case 13:
        p_regs->sw97.ref13_st_addr = val;
        break;
    case 14:
        p_regs->sw98.ref14_st_addr = val;
        break;
    case 15:
        p_regs->sw99.ref15_st_addr = val;
        break;
    default:
        break;
    }
    return 0;
}

static int rkvdpu_h264_regs_gen_reg(AVCodecContext *avctx)
{
    H264Context * const h = avctx->priv_data;
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(avctx);   
    LPRKVDPU_H264_Regs hw_regs = ctx->hw_regs;
    LPRKVDPU_PicParams_H264 pp = ctx->pic_param;    
    H264SliceContext *sl = &h->slice_ctx[0];
    int i, j, dpb_valid, dpb_idx;
    uint32_t dir_mv_offset, pic_size_in_mbs, *poc_data;

    memset(&hw_regs->sw55, 0, sizeof(uint32_t));

    if (!hw_regs->sw54.dec_out_endian) {
        hw_regs->sw53.dec_fmt_sel = 0;   //!< set H264 mode
        hw_regs->sw54.dec_out_endian = 1;  //!< little endian
        hw_regs->sw54.dec_in_endian = 0;  //!< big endian
        hw_regs->sw54.dec_strendian_e = 1; //!< little endian
        hw_regs->sw50.dec_tiled_msb  = 0; //!< 0: raster scan  1: tiled
        hw_regs->sw56.dec_max_burlen = 16;  //!< (0, 4, 8, 16) choice one
        hw_regs->sw50.dec_ascmd0_dis = 0;   //!< disable
        hw_regs->sw50.adv_pref_dis = 0; //!< disable
        hw_regs->sw52.adv_pref_thrd = 8;
        hw_regs->sw50.adtion_latency = 0; //!< compensation for bus latency; values up to 63
        hw_regs->sw56.dec_data_discd_en = 0;
        hw_regs->sw54.dec_out_wordsp = 1;//!< little endian
        hw_regs->sw54.dec_in_wordsp = 1;//!< little endian
        hw_regs->sw54.dec_strm_wordsp = 1;//!< little endian
        hw_regs->sw57.timeout_sts_en = 1;
        hw_regs->sw57.dec_clkgate_en = 1;
        hw_regs->sw55.dec_irq_dis = 0;
        //!< set AXI RW IDs
        hw_regs->sw56.dec_axi_id_rd = (0xFF & 0xFFU);  //!< 0-255
        hw_regs->sw56.dec_axi_id_wr = (0x0 & 0xFFU);  //!< 0-255

        RK_U32 val = 0;
        hw_regs->sw59.pflt_set0_tap0 = 1;
        val = (RK_U32)(-5);
        hw_regs->sw59.pflt_set0_tap1 = val;
        hw_regs->sw59.pflt_set0_tap2 = 20;
        hw_regs->sw50.adtion_latency = 0;
        //!< clock_gating  0:clock always on, 1: clock gating module control the key(turn off when decoder free)
        hw_regs->sw57.dec_clkgate_en = 1;
        hw_regs->sw50.dec_tiled_msb = 0; //!< 0: raster scan  1: tiled
        //!< bus_burst_length = 16, bus burst
        hw_regs->sw56.dec_max_burlen = 16;
        hw_regs->sw56.dec_data_discd_en = 0;
    }

    // pic regs
    hw_regs->sw110.pic_mb_w = pp->wFrameWidthInMbsMinus1 + 1;
    hw_regs->sw110.pic_mb_h = (2 - pp->frame_mbs_only_flag)
                             * (pp->wFrameHeightInMbsMinus1 + 1);

    // vlc regs
    hw_regs->sw57.dec_wr_extmen_dis = 0;
    hw_regs->sw57.rlc_mode_en = 0;
    hw_regs->sw51.qp_init_val = pp->pic_init_qp_minus26 + 26;
    hw_regs->sw114.max_refidx0 = pp->num_ref_idx_l0_active_minus1 + 1;
    hw_regs->sw111.max_refnum = pp->num_ref_frames;
    hw_regs->sw112.cur_frm_len = pp->log2_max_frame_num_minus4 + 4;
    hw_regs->sw112.curfrm_num = pp->frame_num;
    hw_regs->sw115.const_intra_en = pp->constrained_intra_pred_flag;
    hw_regs->sw112.dblk_ctrl_flag = pp->deblocking_filter_control_present_flag;
    hw_regs->sw112.rpcp_flag = pp->redundant_pic_cnt_present_flag;

    hw_regs->sw113.refpic_mk_len = pp->drpm_used_bits;
    hw_regs->sw115.idr_pic_flag = h->picture_idr;
    hw_regs->sw113.idr_pic_id = pp->idr_pic_id;
    hw_regs->sw114.pps_id = sl->pps_id;
    hw_regs->sw114.poc_field_len = pp->poc_used_bits;

    if (pp->field_pic_flag) {
        uint32_t valid_flags = 0;
        uint32_t long_term_tmp = 0, long_term_flags = 0;
        for (i = 0; i < 32; i++) {
            if (pp->RefFrameList[i / 2].bPicEntry <= 0 || pp->RefFrameList[i / 2].bPicEntry == 0xff) {
                long_term_flags <<= 1;
                valid_flags <<= 1;
            } else {
                long_term_tmp = pp->RefFrameList[i / 2].AssociatedFlag;
                long_term_flags = (long_term_flags << 1) | long_term_tmp;
                valid_flags = (valid_flags << 1) | ((pp->UsedForReferenceFlags >> i) & 0x01);
            }
        }
        hw_regs->sw107.refpic_term_flag = long_term_flags;
        hw_regs->sw108.refpic_valid_flag = valid_flags;
    } else {
        uint32_t valid_flags = 0;
        uint32_t long_term_tmp = 0, long_term_flags = 0;
        for (i = 0; i < 16; i++) {
            if (pp->RefFrameList[i].bPicEntry <= 0 || pp->RefFrameList[i].bPicEntry == 0xff) {
                long_term_flags <<= 1;
                valid_flags <<= 1;
            } else {
                long_term_tmp = pp->RefFrameList[i].AssociatedFlag;
                long_term_flags = (long_term_flags << 1) | long_term_tmp;
                valid_flags = (valid_flags << 1) | ((pp->UsedForReferenceFlags >> (2 * i)) & 0x03);
            }
        }
        hw_regs->sw107.refpic_term_flag = (long_term_flags << 16);
        hw_regs->sw108.refpic_valid_flag = (valid_flags << 16);
    }

    for (i = 0; i < 16; i++) {
        if (pp->RefFrameList[i].bPicEntry > 0 && pp->RefFrameList[i].bPicEntry != 0xff) {
            set_refer_pic_idx(hw_regs, i, pp->FrameNumList[i]);
        }
    }

    hw_regs->sw57.rd_cnt_tab_en = 1;

    // set poc to buffer
    poc_data = ctx->poc_buf_data->data[0];
    for (i = 0; i < 32; i++) {
        if (pp->RefFrameList[i / 2].bPicEntry > 0 && pp->RefFrameList[i / 2].bPicEntry != 0xff) {
            *poc_data++ = pp->FieldOrderCntList[i / 2][i & 0x1];
        } else {
            *poc_data++ = 0;
        }
    }

    if (pp->field_pic_flag || !pp->MbaffFrameFlag) {
        if (pp->field_pic_flag)
            *poc_data++ = pp->CurrPic.AssociatedFlag ? pp->CurrFieldOrderCnt[1] : pp->CurrFieldOrderCnt[0];
        else
            *poc_data++ = FFMIN(pp->CurrFieldOrderCnt[0], pp->CurrFieldOrderCnt[1]);
    } else {
        *poc_data++ = pp->CurrFieldOrderCnt[0];
        *poc_data++ = pp->CurrFieldOrderCnt[1];
    }

    hw_regs->sw115.cabac_en = pp->entropy_coding_mode_flag;
    hw_regs->sw57.st_code_exit = 1;
    hw_regs->sw109.strm_start_bit = 0;
    hw_regs->sw64.rlc_vlc_st_adr = ff_rkvdpu_get_fd(ctx->stream_data);
    hw_regs->sw51.stream_len = ALIGN(ctx->stream_data->pkt_size, 16);

    /* b ref */
    for (j = 0; j < 2; j++) {
        for (i = 0; i < 16; i++) {
            const H264Picture *r = NULL;
            if (pp->field_pic_flag)
                r = pp->CurrPic.AssociatedFlag ? sl->ref_list[j][i * 2 + 1].parent : sl->ref_list[j][i * 2].parent;
            else 
                r = sl->ref_list[j][i].parent;
            dpb_valid = (r == NULL || get_refpic_index(pp, ff_rkvdpu_get_fd(r->f)) == 0xff  || sl->slice_type_nos != AV_PICTURE_TYPE_B) ? 0 : 1;
            if (dpb_valid) {
                dpb_idx = get_refpic_index(pp, ff_rkvdpu_get_fd(r->f));
                if (j == 0)
                    set_refer_pic_list_b0(hw_regs, i, dpb_idx);
                else
                    set_refer_pic_list_b1(hw_regs, i, dpb_idx);
            } else {
                if (j == 0)
                    set_refer_pic_list_b0(hw_regs, i, i);
                else
                    set_refer_pic_list_b1(hw_regs, i, i);
            }
        }
    }

    /* p ref */
    for (i = 0; i < 16; i++) {
        const H264Picture *r = NULL;
        if (pp->field_pic_flag)
            r = pp->CurrPic.AssociatedFlag ? sl->ref_list[0][i * 2 + 1].parent : sl->ref_list[0][i * 2].parent;
        else 
            r = sl->ref_list[0][i].parent;
        dpb_valid = (r == NULL || get_refpic_index(pp, ff_rkvdpu_get_fd(r->f)) == 0xff || sl->slice_type_nos != AV_PICTURE_TYPE_P) ? 0 : 1;
        if (dpb_valid) {
            dpb_idx = get_refpic_index(pp, ff_rkvdpu_get_fd(r->f));
            set_refer_pic_list_p(hw_regs, i, dpb_idx);
         } else {
            set_refer_pic_list_p(hw_regs, i, i);
        }
    }

    /* asic regs */
    for (i = 0, j = 0xff; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        uint32_t val = 0;
        uint32_t top_closer = 0;
        uint32_t field_flag = 0;
        uint32_t fd = ff_rkvdpu_get_fd(h->cur_pic_ptr->f);

        if (pp->RefFrameList[i].bPicEntry > 0 && pp->RefFrameList[i].bPicEntry != 0xff) {
            fd = pp->RefFrameList[i].Index7Bits;
            j = i;
        }

        field_flag = ((pp->RefPicFiledFlags >> i) & 0x1) ? 0x2 : 0;
        if (field_flag) {
            uint32_t cur_poc = 0;
            int top_diff, bottom_diff;
            cur_poc = pp->CurrPic.AssociatedFlag ? pp->CurrFieldOrderCnt[1] : pp->CurrFieldOrderCnt[0];
            top_diff = pp->FieldOrderCntList[i][0] - cur_poc;
            bottom_diff = pp->FieldOrderCntList[i][1] - cur_poc;
            if (FFABS(top_diff) < FFABS(bottom_diff))
                top_closer = 0x1;
        }
        val = top_closer | field_flag;
        val = fd | (val << 10);
        set_refer_pic_base_addr(hw_regs , i, val);
    }

    hw_regs->sw50.dec_fixed_quant = pp->curr_layer_id;
    hw_regs->sw50.dblk_flt_dis = 0;

    hw_regs->sw63.dec_out_st_adr = ff_rkvdpu_get_fd(h->cur_pic_ptr->f);
    if (pp->field_pic_flag && pp->CurrPic.AssociatedFlag)
        hw_regs->sw63.dec_out_st_adr |= ((pp->wFrameWidthInMbsMinus1 + 1) * 16) << 10;

    hw_regs->sw110.flt_offset_cb_qp = pp->chroma_qp_index_offset;
    hw_regs->sw110.flt_offset_cr_qp = pp->second_chroma_qp_index_offset;

    dir_mv_offset = pic_size_in_mbs = 0;

    pic_size_in_mbs = pp->wFrameWidthInMbsMinus1 + 1;
    pic_size_in_mbs = pic_size_in_mbs * (2 - pp->frame_mbs_only_flag) * (pp->wFrameHeightInMbsMinus1 + 1);
    dir_mv_offset = pic_size_in_mbs * (pp->chroma_format_idc == 0 ? 256 : 384);
    dir_mv_offset += (pp->field_pic_flag && pp->CurrPic.AssociatedFlag) ? pic_size_in_mbs * 32 : 0;
    hw_regs->sw62.dmmv_st_adr = pp->CurrPic.Index7Bits | (dir_mv_offset << 6);

    hw_regs->sw57.dmmv_wr_en = ((pp->wBitFields >> 6) & 0x1);
    hw_regs->sw115.dlmv_method_en = pp->direct_8x8_inference_flag;
    hw_regs->sw115.weight_pred_en = pp->weighted_pred_flag;
    hw_regs->sw111.wp_bslice_sel = pp->weighted_bipred_idc;
    hw_regs->sw114.max_refidx1 = pp->num_ref_idx_l1_active_minus1 + 1;
    hw_regs->sw115.fieldpic_flag_exist = (!pp->frame_mbs_only_flag) ? 1 : 0;
    hw_regs->sw57.curpic_code_sel = (!pp->frame_mbs_only_flag
                                    && (pp->MbaffFrameFlag || pp->field_pic_flag)) ? 1 : 0;
    hw_regs->sw57.curpic_stru_sel = pp->field_pic_flag;
    hw_regs->sw57.pic_decfield_sel = (!pp->CurrPic.AssociatedFlag) ? 1 : 0;
    hw_regs->sw57.sequ_mbaff_en = pp->MbaffFrameFlag;
    hw_regs->sw115.tranf_8x8_flag_en = pp->transform_8x8_mode_flag;
    hw_regs->sw115.monochr_en = (h->ps.sps->profile_idc >= 100 && pp->chroma_format_idc == 0) ? 1 : 0; 
    hw_regs->sw115.scl_matrix_en = pp->scaleing_list_enable_flag;

    if (pp->scaleing_list_enable_flag) {
        uint32_t* sl_ptr = ctx->scaling_list_data->data[0];
        uint32_t temp = 0;
        for (i = 0; i < 6; i++) {
            for (j = 0; j < 4; j++) {
                temp = (h->ps.pps->scaling_matrix4[i][4 * j + 0] << 24) |
                       (h->ps.pps->scaling_matrix4[i][4 * j + 1] << 16) |
                       (h->ps.pps->scaling_matrix4[i][4 * j + 2] << 8)  |
                       (h->ps.pps->scaling_matrix4[i][4 * j + 3] << 0);
                *sl_ptr++ = temp;
            }
        }
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 16; j++) {
                temp = (h->ps.pps->scaling_matrix4[i][4 * j + 0] << 24) |
                       (h->ps.pps->scaling_matrix4[i][4 * j + 1] << 16) |
                       (h->ps.pps->scaling_matrix4[i][4 * j + 2] << 8)  |
                       (h->ps.pps->scaling_matrix4[i][4 * j + 3] << 0);
                *sl_ptr++ = temp; 
            }
        }
    }
    hw_regs->sw61.qtable_st_adr = ff_rkvdpu_get_fd(ctx->syntax_data);
    hw_regs->sw57.dec_st_work = 1;

    /*hw_regs->sw57.dec_wr_extmen_dis = 0;
    hw_regs->sw57.addit_ch_fmt_wen = 0;
    hw_regs->sw57.dec_st_work = 1;

    hw_regs->sw57.cache_en = 1;
    hw_regs->sw57.pref_sigchan = 1;
    hw_regs->sw56.bus_pos_sel = 1;
    hw_regs->sw57.intra_dbl3t = 1;
    hw_regs->sw57.inter_dblspeed = 1;
    hw_regs->sw57.intra_dblspeed = 1;
*/
    return 0;
}

#define AV_GET_BUFFER_FLAG_CONTAIN_MV (1 << 1)

static int rkvdpu_h264_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
    av_assert0(frame);
    av_assert0(frame->width);
    av_assert0(frame->height);
    frame->width = ALIGN(frame->width, 16);
    frame->height = ALIGN(frame->height, 16);

    return avctx->get_buffer2(avctx, frame, AV_GET_BUFFER_FLAG_CONTAIN_MV);
}


FILE* fp11 = NULL;
//#define dump
//#define debug_regs 1
static int rkvdpu_h264_end_frame(AVCodecContext *avctx)
{
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(avctx);
    H264Context * const h = avctx->priv_data;
    RKVDPUH264HwReq req;
    int ret;

    av_log(avctx, LOG_LEVEL, "RK_H264_DEC: rkvdpu_h264_end_frame.\n");

    if (ctx->stream_data->pkt_size <= 0) {
        av_log(avctx, AV_LOG_WARNING, "RK_H264_DEC: rkvdec_h264_end_frame not valid stream.\n");
        if (h->cur_pic_ptr && h->cur_pic_ptr->f)
            h->cur_pic_ptr->f->decode_error_flags = FF_DECODE_ERROR_INVALID_BITSTREAM;
        return 0;
    }

    adjust_picture_references(h, ctx->pic_param);
    rkvdpu_h264_regs_gen_reg(avctx);

    req.req = ctx->hw_regs;
    req.size = 159 * sizeof(unsigned int);

#ifdef debug_regs
    unsigned char *p = ctx->hw_regs;
    for (int i = 0; i < 159 ; i++) {
        av_log(avctx, AV_LOG_ERROR, "set reg[%03d]: %08x\n", i, *((unsigned int*)p));
        p += 4;
    }
#endif

    av_log(avctx, LOG_LEVEL, "ioctl VPU_IOC_SET_REG start.\n");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d.\n", ret);

    av_log(avctx, LOG_LEVEL, "ioctl VPU_IOC_GET_REG start.\n");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);

    av_log(avctx, LOG_LEVEL, "ioctl VPU_IOC_GET_REG success. ret: %d.\n", ret);

    pthread_mutex_unlock(&ctx->hwaccel_mutex);
    
#ifdef dump
        if (fp11 == NULL)
           fp11 = fopen("hal.bin", "wb");
        av_log(avctx, AV_LOG_INFO, "write cur_pic fd %d", ff_rkvdpu_get_fd(h->cur_pic_ptr->f));
        fwrite(h->cur_pic_ptr->f->data[0],1, ALIGN(h->cur_pic_ptr->f->width, 16) * ALIGN(h->cur_pic_ptr->f->height, 16) * 1.5, fp11);
        fflush(fp11);
#endif

    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d\n", ret);

    return 0;
}

static int rkvdpu_h264_context_init(AVCodecContext *avctx)
{    
    RKVDPUH264Context * const ctx = ff_rkvdpu_get_context(avctx);
    int ret;

    av_log(avctx, LOG_LEVEL, "RK_H264_DEC: rkvdpu_h264_context_init.\n");

    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }

    ctx->hw_regs = av_mallocz(sizeof(RKVDPU_H264_Regs));

    ctx->syntax_data = av_frame_alloc();
    ctx->syntax_data->linesize[0] = RKVDPU_H264_CABAC_TAB_SIZE +
                                    RKVDPU_H264_SCALING_LIST_SIZE +
                                    RKVDPU_H264_POC_BUF_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->syntax_data);

    ctx->cabac_table_data = av_frame_alloc();
    ctx->cabac_table_data->linesize[0] = RKVDPU_H264_CABAC_TAB_SIZE;
    ctx->cabac_table_data->data[0] = ctx->syntax_data->data[0];
    memcpy(ctx->cabac_table_data->data[0], vdpu_cabac_table, sizeof(vdpu_cabac_table));

    ctx->poc_buf_data = av_frame_alloc();
    ctx->poc_buf_data->linesize[0] = RKVDPU_H264_POC_BUF_SIZE;
    ctx->poc_buf_data->data[0] = ctx->syntax_data->data[0] + RKVDPU_H264_CABAC_TAB_SIZE;

    ctx->scaling_list_data = av_frame_alloc();
    ctx->scaling_list_data->linesize[0] = RKVDPU_H264_SCALING_LIST_SIZE;
    ctx->scaling_list_data->data[0] = ctx->poc_buf_data->data[0] + RKVDPU_H264_POC_BUF_SIZE;

    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDPU_H264_DATA_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);

    ctx->pic_param = av_mallocz(sizeof(RKVDPU_PicParams_H264));
    
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

static int rkvdpu_h264_context_uninit(AVCodecContext *avctx)
{
    RKVDPUH264Context* const ctx = ff_rkvdpu_get_context(avctx);

    av_log(avctx, LOG_LEVEL, "RK_H264_DEC: rkvdec_h264_context_uninit.\n");
    ctx->allocator->free(ctx->allocator_ctx, ctx->syntax_data);
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
    ctx->allocator->close(ctx->allocator_ctx);

    av_free(ctx->syntax_data);
    av_free(ctx->stream_data);
    av_free(ctx->cabac_table_data);
    av_free(ctx->poc_buf_data);
    av_free(ctx->scaling_list_data);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }

    return 0;
}

AVHWAccel ff_h264_rkvdpu_hwaccel = {
    .name                 = "h264_rkvdpu",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_H264,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .alloc_frame          = rkvdpu_h264_alloc_frame,
    .start_frame          = rkvdpu_h264_start_frame,
    .end_frame            = rkvdpu_h264_end_frame,
    .decode_slice         = rkvdpu_h264_decode_slice,
    .init                 = rkvdpu_h264_context_init,
    .uninit               = rkvdpu_h264_context_uninit,
    .priv_data_size       = sizeof(RKVDPUH264Context),    
    .frame_priv_data_size = sizeof(RKVDPU_FrameData_H264),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};

