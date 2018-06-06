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

#include "libavutil/time.h"
#include "hevc.h"
#include "hevcdec.h"
#include "put_bits.h"
#include "put_bits64.h"
#include "golomb.h"
#include "allocator_drm.h"
#include "rkvdec_hevc.h"
#include "hwaccel.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <pthread.h>

#define MAX_SLICES 600
#define SCALING_LIST_SIZE  81 * 1360
#define PPS_SIZE  80 * 64
#define RPS_SIZE  600 * 32
#define DATA_SIZE 2048 * 1024
#define SCALING_LIST_SIZE_NUM 4

//#define dump
//#define debug_regs
#define LOG_LEVEL AV_LOG_DEBUG

FILE* fp = NULL;

typedef struct _RKVDECHevcContext RKVDECHevcContext;
typedef struct _RKVDECHevcFrameData RKVDECHevcFrameData;
typedef struct _RKVDEC_PicEntry_HEVC RKVDEC_PicEntry_HEVC, *LPRKVDEC_PicEntry_HEVC;
typedef struct ScalingList RKVDEC_ScalingList_HEVC, *LPRKVDEC_ScalingList_HEVC;
typedef struct _RKVDEC_ScalingFactor_Model_HEVC RKVDEC_ScalingFactor_Model_HEVC, *LPRKVDEC_ScalingFactor_Model_HEVC;
typedef struct _RKVDEC_PicParams_HEVC RKVDEC_PicParams_HEVC, *LPRKVDEC_PicParams_HEVC;
typedef struct H265d_REGS_t RKVDEC_HEVC_Regs, *LPRKVDEC_HEVC_Regs;
typedef struct _RKVDEC_Slice_Ref_Map RKVDEC_Slice_Ref_Map ;
typedef struct _RKVDEC_Slice_RPS_Info RKVDEC_Slice_RPS_Info, *LPRKVDEC_Slice_RPS_Info;
typedef struct _RKVDECHevcHwReq RKVDECHevcHwReq;

struct _RKVDECHevcContext{
     signed int vpu_socket;
     int slice_count;
     LPRKVDEC_HEVC_Regs hw_regs;
     LPRKVDEC_PicParams_HEVC pic_param;
     LPRKVDEC_ScalingList_HEVC scaling_list;
     LPRKVDEC_ScalingFactor_Model_HEVC scaling_rk;
     LPRKVDEC_Slice_RPS_Info rps_info;
     AVFrame* cabac_table_data;
     AVFrame* scaling_list_data;
     AVFrame* pps_data;
     AVFrame* rps_data;
     AVFrame* stream_data;
     AVBufferPool *motion_val_pool;
     os_allocator *allocator;
     void *allocator_ctx;
     pthread_mutex_t hwaccel_mutex;
};

struct _RKVDECHevcHwReq {
    unsigned int *req;
    unsigned int  size;
} ;

struct _RKVDECHevcFrameData{
    AVFrame colmv;
};

/* HEVC Picture Entry structure */
struct _RKVDEC_PicEntry_HEVC {
    union {
        struct {
            unsigned char Index7Bits : 7;
            unsigned char AssociatedFlag : 1;
        };
        unsigned char bPicEntry;
    };
};

/* HEVC Picture Parameter structure */
struct _RKVDEC_PicParams_HEVC {
    unsigned short      PicWidthInMinCbsY;
    unsigned short      PicHeightInMinCbsY;
    union {
        struct {
            unsigned short  chroma_format_idc                       : 2;
            unsigned short  separate_colour_plane_flag              : 1;
            unsigned short  bit_depth_luma_minus8                   : 3;
            unsigned short  bit_depth_chroma_minus8                 : 3;
            unsigned short  log2_max_pic_order_cnt_lsb_minus4       : 4;
            unsigned short  NoPicReorderingFlag                     : 1;
            unsigned short  NoBiPredFlag                            : 1;
            unsigned short  ReservedBits1                           : 1;
        };
        unsigned short wFormatAndSequenceInfoFlags;
    };
    RKVDEC_PicEntry_HEVC  CurrPic;
    RKVDEC_PicEntry_HEVC  CurrMv;
    unsigned char   sps_max_dec_pic_buffering_minus1;
    unsigned char   log2_min_luma_coding_block_size_minus3;
    unsigned char   log2_diff_max_min_luma_coding_block_size;
    unsigned char   log2_min_transform_block_size_minus2;
    unsigned char   log2_diff_max_min_transform_block_size;
    unsigned char   max_transform_hierarchy_depth_inter;
    unsigned char   max_transform_hierarchy_depth_intra;
    unsigned char   num_short_term_ref_pic_sets;
    unsigned char   num_long_term_ref_pics_sps;
    unsigned char   num_ref_idx_l0_default_active_minus1;
    unsigned char   num_ref_idx_l1_default_active_minus1;
    signed char     init_qp_minus26;
    unsigned char   ucNumDeltaPocsOfRefRpsIdx;
    unsigned short  wNumBitsForShortTermRPSInSlice;
    unsigned short  ReservedBits2;

    union {
        struct {
            unsigned int  scaling_list_enabled_flag                    : 1;
            unsigned int  amp_enabled_flag                             : 1;
            unsigned int  sample_adaptive_offset_enabled_flag          : 1;
            unsigned int  pcm_enabled_flag                             : 1;
            unsigned int  pcm_sample_bit_depth_luma_minus1             : 4;
            unsigned int  pcm_sample_bit_depth_chroma_minus1           : 4;
            unsigned int  log2_min_pcm_luma_coding_block_size_minus3   : 2;
            unsigned int  log2_diff_max_min_pcm_luma_coding_block_size : 2;
            unsigned int  pcm_loop_filter_disabled_flag                : 1;
            unsigned int  long_term_ref_pics_present_flag              : 1;
            unsigned int  sps_temporal_mvp_enabled_flag                : 1;
            unsigned int  strong_intra_smoothing_enabled_flag          : 1;
            unsigned int  dependent_slice_segments_enabled_flag        : 1;
            unsigned int  output_flag_present_flag                     : 1;
            unsigned int  num_extra_slice_header_bits                  : 3;
            unsigned int  sign_data_hiding_enabled_flag                : 1;
            unsigned int  cabac_init_present_flag                      : 1;
            unsigned int  ReservedBits3                                : 5;
        };
        unsigned int dwCodingParamToolFlags;
    };

    union {
        struct {
            unsigned int  constrained_intra_pred_flag                 : 1;
            unsigned int  transform_skip_enabled_flag                 : 1;
            unsigned int  cu_qp_delta_enabled_flag                    : 1;
            unsigned int  pps_slice_chroma_qp_offsets_present_flag    : 1;
            unsigned int  weighted_pred_flag                          : 1;
            unsigned int  weighted_bipred_flag                        : 1;
            unsigned int  transquant_bypass_enabled_flag              : 1;
            unsigned int  tiles_enabled_flag                          : 1;
            unsigned int  entropy_coding_sync_enabled_flag            : 1;
            unsigned int  uniform_spacing_flag                        : 1;
            unsigned int  loop_filter_across_tiles_enabled_flag       : 1;
            unsigned int  pps_loop_filter_across_slices_enabled_flag  : 1;
            unsigned int  deblocking_filter_override_enabled_flag     : 1;
            unsigned int  pps_deblocking_filter_disabled_flag         : 1;
            unsigned int  lists_modification_present_flag             : 1;
            unsigned int  slice_segment_header_extension_present_flag : 1;
            unsigned int  IrapPicFlag                                 : 1;
            unsigned int  IdrPicFlag                                  : 1;
            unsigned int  IntraPicFlag                                : 1;
            unsigned int  ReservedBits4                               : 13;
        };
        unsigned int dwCodingSettingPicturePropertyFlags;
    };
    signed char     pps_cb_qp_offset;
    signed char     pps_cr_qp_offset;
    unsigned char   num_tile_columns_minus1;
    unsigned char   num_tile_rows_minus1;
    unsigned short  column_width_minus1[19];
    unsigned short  row_height_minus1[21];
    unsigned char   diff_cu_qp_delta_depth;
    signed char     pps_beta_offset_div2;
    signed char     pps_tc_offset_div2;
    unsigned char   log2_parallel_merge_level_minus2;
    signed int      CurrPicOrderCntVal;
    RKVDEC_PicEntry_HEVC  RefPicList[15];
    RKVDEC_PicEntry_HEVC  RefColmvList[16];
    unsigned char   ReservedBits5;
    signed int      PicOrderCntValList[15];
    unsigned char   RefPicSetStCurrBefore[8];
    unsigned char   RefPicSetStCurrAfter[8];
    unsigned char   RefPicSetLtCurr[8];
    unsigned short  ReservedBits6;
    unsigned short  ReservedBits7;
    signed int      StatusReportFeedbackNumber;
    unsigned int    vps_id;
    unsigned int    pps_id;
    unsigned int    sps_id;
    unsigned char   scaling_list_data_present_flag;
};

/* HEVC Quantizatiuon Matrix structure */
struct _RKVDEC_ScalingFactor_Model_HEVC {
    unsigned char scalingfactor0[1248];
    unsigned char scalingfactor1[96];
    unsigned char scalingdc[12];
    unsigned char reserverd[4];
};

struct _RKVDEC_Slice_Ref_Map {
    unsigned char dpb_index;
    unsigned char is_long_term;
};

struct _RKVDEC_Slice_RPS_Info {
    unsigned char rps_bit_offset[600];
    unsigned char rps_bit_offset_st[600];
    unsigned char slice_nb_rps_poc[600];
    unsigned char lowdelay_flag[600];
    RKVDEC_Slice_Ref_Map rps_pic_info[600][2][15];
};

#define ALIGN(value, x) ((value + (x - 1)) & (~(x - 1)))

/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECHevcContext *ff_rkvdec_get_context(AVCodecContext *avctx)
{
    return avctx->internal->hwaccel_priv_data;
}

static inline int ff_rkvdec_get_fd(AVFrame* frame)
{
    return frame->linesize[2];
}

static int get_refpic_index(const LPRKVDEC_PicParams_HEVC pp, int surface_index)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefPicList); i++) {
        if ((pp->RefPicList[i].bPicEntry & 0x7f) == surface_index) {
            return i;
        }
    }
    return 0xff;
}

static void fill_picture_entry(LPRKVDEC_PicEntry_HEVC pic,
                               unsigned index, unsigned flag)
{
    av_assert0((index & 0x7f) == index && (flag & 0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static void fill_picture_parameters(const HEVCContext *h, LPRKVDEC_PicParams_HEVC pp)
{
    const HEVCFrame *current_picture = h->ref;
    const HEVCPPS *pps = (HEVCPPS *)h->ps.pps;
    const HEVCSPS *sps = (HEVCSPS *)h->ps.sps;

    unsigned int i, j;
    unsigned int rps_used[16];
    unsigned int nb_rps_used;

    memset(pp, 0, sizeof(*pp));

    pp->PicWidthInMinCbsY  = sps->min_cb_width;
    pp->PicHeightInMinCbsY = sps->min_cb_height;
    pp->pps_id = h->sh.pps_id;
    pp->sps_id = pps->sps_id;
    pp->vps_id = sps->vps_id;

    pp->wFormatAndSequenceInfoFlags = (sps->chroma_format_idc             <<  0) |
                                      (sps->separate_colour_plane_flag    <<  2) |
                                      ((sps->bit_depth - 8)               <<  3) |
                                      ((sps->bit_depth - 8)               <<  6) |
                                      ((sps->log2_max_poc_lsb - 4)        <<  9) |
                                      (0                                  << 13) |
                                      (0                                  << 14) |
                                      (0                                  << 15);

    fill_picture_entry(&pp->CurrPic, ff_rkvdec_get_fd(current_picture->frame), 0);
    fill_picture_entry(&pp->CurrMv, ff_rkvdec_get_fd(current_picture->hwaccel_picture_private), 0);
    
    pp->sps_max_dec_pic_buffering_minus1         = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering - 1;
    pp->log2_min_luma_coding_block_size_minus3   = sps->log2_min_cb_size - 3;
    pp->log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_coding_block_size;
    pp->log2_min_transform_block_size_minus2     = sps->log2_min_tb_size - 2;
    pp->log2_diff_max_min_transform_block_size   = sps->log2_max_trafo_size  - sps->log2_min_tb_size;
    pp->max_transform_hierarchy_depth_inter      = sps->max_transform_hierarchy_depth_inter;
    pp->max_transform_hierarchy_depth_intra      = sps->max_transform_hierarchy_depth_intra;
    pp->num_short_term_ref_pic_sets              = sps->nb_st_rps;
    pp->num_long_term_ref_pics_sps               = sps->num_long_term_ref_pics_sps;

    pp->num_ref_idx_l0_default_active_minus1     = pps->num_ref_idx_l0_default_active - 1;
    pp->num_ref_idx_l1_default_active_minus1     = pps->num_ref_idx_l1_default_active - 1;
    pp->init_qp_minus26                          = pps->pic_init_qp_minus26;

    if (h->sh.short_term_ref_pic_set_sps_flag == 0 && h->sh.short_term_rps) {
        pp->ucNumDeltaPocsOfRefRpsIdx            = h->sh.short_term_rps->rps_idx_num_delta_pocs;
        pp->wNumBitsForShortTermRPSInSlice       = h->sh.short_term_ref_pic_set_size;
    }

    pp->dwCodingParamToolFlags = (sps->scaling_list_enable_flag                  <<  0) |
                             (sps->amp_enabled_flag                          <<  1) |
                             (sps->sao_enabled                               <<  2) |
                             (sps->pcm_enabled_flag                          <<  3) |
                             ((sps->pcm_enabled_flag ? (sps->pcm.bit_depth - 1) : 0)            <<  4) |
                             ((sps->pcm_enabled_flag ? (sps->pcm.bit_depth_chroma - 1) : 0)     <<  8) |
                             ((sps->pcm_enabled_flag ? (sps->pcm.log2_min_pcm_cb_size - 3) : 0) << 12) |
                             ((sps->pcm_enabled_flag ? (sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size) : 0) << 14) |
                             (sps->pcm.loop_filter_disable_flag              << 16) |
                             (sps->long_term_ref_pics_present_flag           << 17) |
                             (sps->sps_temporal_mvp_enabled_flag             << 18) |
                             (sps->sps_strong_intra_smoothing_enable_flag    << 19) |
                             (pps->dependent_slice_segments_enabled_flag     << 20) |
                             (pps->output_flag_present_flag                  << 21) |
                             (pps->num_extra_slice_header_bits               << 22) |
                             (pps->sign_data_hiding_flag                     << 25) |
                             (pps->cabac_init_present_flag                   << 26) |
                             (0                                              << 27);

    pp->dwCodingSettingPicturePropertyFlags = (pps->constrained_intra_pred_flag                   <<  0) |
                                              (pps->transform_skip_enabled_flag                   <<  1) |
                                              (pps->cu_qp_delta_enabled_flag                      <<  2) |
                                              (pps->pic_slice_level_chroma_qp_offsets_present_flag <<  3) |
                                              (pps->weighted_pred_flag                            <<  4) |
                                              (pps->weighted_bipred_flag                          <<  5) |
                                              (pps->transquant_bypass_enable_flag                 <<  6) |
                                              (pps->tiles_enabled_flag                            <<  7) |
                                              (pps->entropy_coding_sync_enabled_flag              <<  8) |
                                              (pps->uniform_spacing_flag                          <<  9) |
                                              ((pps->tiles_enabled_flag ? pps->loop_filter_across_tiles_enabled_flag : 0) << 10) |
                                              (pps->seq_loop_filter_across_slices_enabled_flag    << 11) |
                                              (pps->deblocking_filter_override_enabled_flag       << 12) |
                                              (pps->disable_dbf                                   << 13) |
                                              (pps->lists_modification_present_flag               << 14) |
                                              (pps->slice_header_extension_present_flag           << 15) |
                                              (IS_IRAP(h)                                         << 16) |
                                              (IS_IDR(h)                                          << 17) |
                                              /* IntraPicFlag */
                                              (IS_IRAP(h)                                         << 18) |
                                              (0                                                  << 19);
    pp->pps_cb_qp_offset            = pps->cb_qp_offset;
    pp->pps_cr_qp_offset            = pps->cr_qp_offset;
    if (pps->tiles_enabled_flag) {
        pp->num_tile_columns_minus1 = pps->num_tile_columns - 1;
        pp->num_tile_rows_minus1    = pps->num_tile_rows - 1;

        if (!pps->uniform_spacing_flag) {
            for (i = 0; i < (unsigned int)pps->num_tile_columns; i++)
                pp->column_width_minus1[i] = pps->column_width[i] - 1;

            for (i = 0; i < (unsigned int)pps->num_tile_rows; i++)
                pp->row_height_minus1[i] = pps->row_height[i] - 1;
        }
    }

    pp->diff_cu_qp_delta_depth           = pps->diff_cu_qp_delta_depth;
    pp->pps_beta_offset_div2             = pps->beta_offset / 2;
    pp->pps_tc_offset_div2               = pps->tc_offset / 2;
    pp->log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level - 2;
    pp->CurrPicOrderCntVal               = h->poc;

    nb_rps_used = 0;
    for (i = 0; i < NB_RPS_TYPE; i++) {
        for (j = 0; j < (unsigned int)h->rps[i].nb_refs; j++) {
            if ((i == ST_FOLL) || (i == LT_FOLL)) {
                ;
            } else {
                rps_used[nb_rps_used++] = h->rps[i].list[j];
            }
        }
    }
    

    /* Fill RefPicList from the DPB */
    for (i = 0, j = 0; i < FF_ARRAY_ELEMS(pp->RefPicList); i++) {
        const HEVCFrame *frame = NULL;
        while (!frame && j < FF_ARRAY_ELEMS(h->DPB)) {
            if (&h->DPB[j] != current_picture &&
                (h->DPB[j].flags & (HEVC_FRAME_FLAG_LONG_REF | HEVC_FRAME_FLAG_SHORT_REF))) {
                unsigned int k = 0;
                for (k = 0; k < nb_rps_used; k++) {  /*skip fill RefPicList no used in rps*/
                    if (rps_used[k] == (unsigned int)h->DPB[j].poc) {
                        frame = &h->DPB[j];
                    }
                }
            }
            j++;
        }

        if (frame && ff_rkvdec_get_fd(frame->frame)) {
            fill_picture_entry(&pp->RefPicList[i], ff_rkvdec_get_fd(frame->frame), !!(frame->flags & HEVC_FRAME_FLAG_LONG_REF));
            fill_picture_entry(&pp->RefColmvList[i], ff_rkvdec_get_fd(frame->hwaccel_picture_private), 0);
            pp->PicOrderCntValList[i] = frame->poc;
            if (frame->flags & HEVC_FRAME_FLAG_SHORT_REF && !h->frame->key_frame) {
                h->output_frame->decode_error_flags |= frame->frame->decode_error_flags;
                current_picture->frame->decode_error_flags |= frame->frame->decode_error_flags;
            }
        } else {
            pp->RefPicList[i].bPicEntry = 0xff;
            pp->RefColmvList[i].bPicEntry = 0xff;
            pp->PicOrderCntValList[i]   = 0;
        }
    }

    #define DO_REF_LIST(ref_idx, ref_list) { \
        const RefPicList *rpl = &h->rps[ref_idx]; \
        for (i = 0, j = 0; i < FF_ARRAY_ELEMS(pp->ref_list); i++) { \
            const HEVCFrame *frame = NULL; \
            while (!frame && j < (unsigned int)rpl->nb_refs) \
                frame = rpl->ref[j++]; \
            if (frame) \
                pp->ref_list[i] = get_refpic_index(pp, ff_rkvdec_get_fd(frame->frame)); \
            else \
                pp->ref_list[i] = 0xff; \
        } \
    }

    // Fill short term and long term lists
    DO_REF_LIST(ST_CURR_BEF, RefPicSetStCurrBefore);
    DO_REF_LIST(ST_CURR_AFT, RefPicSetStCurrAfter);
    DO_REF_LIST(LT_CURR, RefPicSetLtCurr);

}

static void fill_scaling_lists(const HEVCContext *h, LPRKVDEC_ScalingList_HEVC pScalingList, 
    LPRKVDEC_ScalingFactor_Model_HEVC pScalingFactor_out)
{
    const HEVCPPS *pps = (HEVCPPS *)h->ps.pps;
    const HEVCSPS *sps = (HEVCSPS *)h->ps.sps;
    const ScalingList *sl = pps->scaling_list_data_present_flag ?
                            &pps->scaling_list : &sps->scaling_list;
    unsigned int g_scalingListNum_model[SCALING_LIST_SIZE_NUM] = {6, 6, 6, 2};
    unsigned int nIndex = 0, sizeId, matrixId, listId;
    unsigned char *p = pScalingFactor_out->scalingfactor0;
    unsigned char tmpBuf[8 * 8];
    int i;

    if (memcmp(pScalingList, sl, sizeof(ScalingList))) {
        memcpy(pScalingList, sl, sizeof(ScalingList)); 

        //output non-default scalingFactor Table (1248 BYTES)
        for (sizeId = 0; sizeId < SCALING_LIST_SIZE_NUM; sizeId++) {
            for (listId = 0; listId < g_scalingListNum_model[sizeId]; listId++) {
                if (sizeId < 3) {
                    for (i = 0; i < (sizeId == 0 ? 16 : 64); i++) {
                        pScalingFactor_out->scalingfactor0[nIndex++] = (unsigned char)pScalingList->sl[sizeId][listId][i];
                    }
                } else {
                    for (i = 0; i < 64; i ++) {
                        pScalingFactor_out->scalingfactor0[nIndex++] = (unsigned char)pScalingList->sl[sizeId][listId][i];
                    }
                    for (i = 0; i < 128; i ++) {
                        pScalingFactor_out->scalingfactor0[nIndex++] = 0;
                    }
                }
            }
        }
        //output non-default scalingFactor Table Rotation(96 Bytes)
        nIndex = 0;
        for (listId = 0; listId < g_scalingListNum_model[0]; listId++) {
            unsigned char temp16[16] = {0};
            for (i = 0; i < 16; i ++) {
                temp16[i] = (unsigned char)pScalingList->sl[0][listId][i];
            }
            for (i = 0; i < 4; i ++) {
                pScalingFactor_out->scalingfactor1[nIndex++] = temp16[i];
                pScalingFactor_out->scalingfactor1[nIndex++] = temp16[i + 4];
                pScalingFactor_out->scalingfactor1[nIndex++] = temp16[i + 8];
                pScalingFactor_out->scalingfactor1[nIndex++] = temp16[i + 12];
            }
        }
        //output non-default ScalingList_DC_Coeff (12 BYTES)
        nIndex = 0;
        for (listId = 0; listId < g_scalingListNum_model[2]; listId++) { //sizeId = 2
            pScalingFactor_out->scalingdc[nIndex++] = (unsigned char)pScalingList->sl_dc[0][listId];
        }
        for (listId = 0; listId < g_scalingListNum_model[3]; listId++) { //sizeId = 3
            pScalingFactor_out->scalingdc[nIndex++] = (unsigned char)pScalingList->sl_dc[1][listId];
            pScalingFactor_out->scalingdc[nIndex++] = 0;
            pScalingFactor_out->scalingdc[nIndex++] = 0;
        }

        //align 16X address
        nIndex = 0;
        for (i = 0; i < 4; i ++) {
            pScalingFactor_out->reserverd[nIndex++] = 0;
        }

        //----------------------All above code show the normal store way in HM--------------------------
        //--------from now on, the scalingfactor0 is rotated 90', the scalingfactor1 is also rotated 90'
        //sizeId == 0
        for (matrixId = 0; matrixId < 6; matrixId++) {
            p = pScalingFactor_out->scalingfactor0 + matrixId * 16;

            for (i = 0; i < 4; i++) {
                tmpBuf[4 * 0 + i] = p[i * 4 + 0];
                tmpBuf[4 * 1 + i] = p[i * 4 + 1];
                tmpBuf[4 * 2 + i] = p[i * 4 + 2];
                tmpBuf[4 * 3 + i] = p[i * 4 + 3];
            }
            memcpy(p, tmpBuf, 4 * 4 * sizeof(unsigned char));
        }
        //sizeId == 1
        for (matrixId = 0; matrixId < 6; matrixId++) {
            p = pScalingFactor_out->scalingfactor0 + 6 * 16 + matrixId * 64;

            for (i = 0; i < 8; i++) {
                tmpBuf[8 * 0 + i] = p[i * 8 + 0];
                tmpBuf[8 * 1 + i] = p[i * 8 + 1];
                tmpBuf[8 * 2 + i] = p[i * 8 + 2];
                tmpBuf[8 * 3 + i] = p[i * 8 + 3];
                tmpBuf[8 * 4 + i] = p[i * 8 + 4];
                tmpBuf[8 * 5 + i] = p[i * 8 + 5];
                tmpBuf[8 * 6 + i] = p[i * 8 + 6];
                tmpBuf[8 * 7 + i] = p[i * 8 + 7];
            }
            memcpy(p, tmpBuf, 8 * 8 * sizeof(unsigned char));
        }
        //sizeId == 2
        for (matrixId = 0; matrixId < 6; matrixId++) {
            p = pScalingFactor_out->scalingfactor0 + 6 * 16 + 6 * 64 + matrixId * 64;

            for (i = 0; i < 8; i++) {
                tmpBuf[8 * 0 + i] = p[i * 8 + 0];
                tmpBuf[8 * 1 + i] = p[i * 8 + 1];
                tmpBuf[8 * 2 + i] = p[i * 8 + 2];
                tmpBuf[8 * 3 + i] = p[i * 8 + 3];
                tmpBuf[8 * 4 + i] = p[i * 8 + 4];
                tmpBuf[8 * 5 + i] = p[i * 8 + 5];
                tmpBuf[8 * 6 + i] = p[i * 8 + 6];
                tmpBuf[8 * 7 + i] = p[i * 8 + 7];
            }
            memcpy(p, tmpBuf, 8 * 8 * sizeof(unsigned char));
        }
        //sizeId == 3
        for (matrixId = 0; matrixId < 6; matrixId++) {
            p = pScalingFactor_out->scalingfactor0 + 6 * 16 + 6 * 64 + 6 * 64 + matrixId * 64;

            for (i = 0; i < 8; i++) {
                tmpBuf[8 * 0 + i] = p[i * 8 + 0];
                tmpBuf[8 * 1 + i] = p[i * 8 + 1];
                tmpBuf[8 * 2 + i] = p[i * 8 + 2];
                tmpBuf[8 * 3 + i] = p[i * 8 + 3];
                tmpBuf[8 * 4 + i] = p[i * 8 + 4];
                tmpBuf[8 * 5 + i] = p[i * 8 + 5];
                tmpBuf[8 * 6 + i] = p[i * 8 + 6];
                tmpBuf[8 * 7 + i] = p[i * 8 + 7];
            }
            memcpy(p, tmpBuf, 8 * 8 * sizeof(unsigned char));
        }

        //sizeId == 0
        for (matrixId = 0; matrixId < 6; matrixId++) {
            p = pScalingFactor_out->scalingfactor1 + matrixId * 16;

            for (i = 0; i < 4; i++) {
                tmpBuf[4 * 0 + i] = p[i * 4 + 0];
                tmpBuf[4 * 1 + i] = p[i * 4 + 1];
                tmpBuf[4 * 2 + i] = p[i * 4 + 2];
                tmpBuf[4 * 3 + i] = p[i * 4 + 3];
            }
            memcpy(p, tmpBuf, 4 * 4 * sizeof(unsigned char));
        }
    }
}

static int get_rps_dpb_index(HEVCContext *h, int rps_poc)
{
    int i,j;
    int nb_rps_used = 0;
    int rps_used[10];

    for(i = 0; i < NB_RPS_TYPE; i++) {
        if (i == ST_FOLL || i == LT_FOLL)
            continue;
        for (j = 0; j < h->rps[i].nb_refs; j++) {
            rps_used[nb_rps_used++] = h->rps[i].list[j];
        }
    }

    for(i = 1; i < nb_rps_used; i++) {
        for(j = 0; j < i; j++) {
            if(rps_used[j] > rps_used[i]){
                int temp = rps_used[j];
                rps_used[j] = rps_used[i];
                rps_used[i] = temp;
            }
        }
    }

    for (i = 0; i < nb_rps_used; i++) {
        if (rps_used[i] == rps_poc)
            return i;
    }
    return 0;
}

static int get_reorder_slice_rpl(AVCodecContext *avctx, RefPicListTab* ref)
{
    HEVCContext * const h = avctx->priv_data;    
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_HEVC pp = ctx->pic_param;
    int i, j, list_idx;
    int bef_nb_refs, aft_nb_refs, lt_cur_nb_refs;

    int nb_list = (h->sh.slice_type == HEVC_SLICE_B ? 2 : 1);
    int cand_lists[3];
    
    bef_nb_refs = aft_nb_refs = lt_cur_nb_refs = 0;
    memset(ref, 0, sizeof(RefPicListTab));

    for (i = 0; i < 8; i++ ) {
        if (pp->RefPicSetStCurrBefore[i] != 0xff) {
            bef_nb_refs ++;
        }

        if (pp->RefPicSetStCurrAfter[i] != 0xff) {
            aft_nb_refs ++;
        }

        if (pp->RefPicSetLtCurr[i] != 0xff) {
            lt_cur_nb_refs ++;
        }
    }

    for (list_idx = 0; list_idx < nb_list; list_idx++) {
        RefPicList  rpl_tmp;
        RefPicList *rpl     = &ref->refPicList[list_idx];
        memset(&rpl_tmp, 0, sizeof(RefPicList));

        /* The order of the elements is
         * ST_CURR_BEF - ST_CURR_AFT - LT_CURR for the L0 and
         * ST_CURR_AFT - ST_CURR_BEF - LT_CURR for the L1 */

        cand_lists[0] = list_idx ? ST_CURR_AFT : ST_CURR_BEF;
        cand_lists[1] = list_idx ? ST_CURR_BEF : ST_CURR_AFT;
        cand_lists[2] = LT_CURR;
        
        /* concatenate the candidate lists for the current frame */
        while (rpl_tmp.nb_refs < h->sh.nb_refs[list_idx]) {
            for (i = 0; i < FF_ARRAY_ELEMS(cand_lists); i++) {
                unsigned char *rps = NULL;
                int nb_refs = 0;
                if (cand_lists[i] == ST_CURR_BEF) {
                    rps = &pp->RefPicSetStCurrBefore[0];
                    nb_refs = bef_nb_refs;
                } else if (cand_lists[i] == ST_CURR_AFT) {
                    rps = &pp->RefPicSetStCurrAfter[0];
                    nb_refs = aft_nb_refs;
                } else {
                    rps = &pp->RefPicSetLtCurr[0];
                    nb_refs = lt_cur_nb_refs;
                }
                for (j = 0; j < nb_refs && rpl_tmp.nb_refs < HEVC_MAX_REFS; j++) {
                    rpl_tmp.list[rpl_tmp.nb_refs]       = rps[j];
                    rpl_tmp.nb_refs++;
                }
            }
        }

        /* reorder the references if necessary */
        if (h->sh.rpl_modification_flag[list_idx]) {
            for (i = 0; i < h->sh.nb_refs[list_idx]; i++) {
                int idx = h->sh.list_entry_lx[list_idx][i];
                rpl->list[i]        = rpl_tmp.list[idx];
                rpl->nb_refs++;
            }
        } else {
            memcpy(rpl, &rpl_tmp, sizeof(*rpl));
            rpl->nb_refs = FFMIN(rpl->nb_refs, h->sh.nb_refs[list_idx]);
        }
    }
    return 0;
}

static void fill_rps_info(AVCodecContext *avctx, const uint8_t  *buffer, uint32_t size)
{
    HEVCContext * const h = avctx->priv_data;    
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_Slice_RPS_Info info = ctx->rps_info;
    
    int extract_length, consumed;
    int slice_idx;
    int bit_begin;
    int i,j;
    GetBitContext *gb;
    H2645NAL nal;

    if (h->sh.dependent_slice_segment_flag)
        goto skip;

    memset(&nal, 0, sizeof(nal));

    slice_idx = h->slice_idx;
    
    extract_length = FFMIN(size, 50);
    consumed = ff_h2645_extract_rbsp(buffer, extract_length, &nal, 1);
    (void)consumed;

    gb = &nal.gb;
    init_get_bits8(gb, nal.data, nal.size);

    if (get_bits1(gb) != 0)
        goto skip;

    nal.type = get_bits(gb, 6);

    if (nal.type > 23)
        goto skip;
        
    skip_bits(gb, 9);

    if (nal.type >= 16 && nal.type <= 23)
        skip_bits1(gb);


    get_ue_golomb_long(gb);

    if (!h->sh.first_slice_in_pic_flag) {
        int slice_address_length;

        if (h->ps.pps->dependent_slice_segments_enabled_flag)
            skip_bits1(gb);

        slice_address_length = av_ceil_log2(h->ps.sps->ctb_width *
                        h->ps.sps->ctb_height);
        skip_bits(gb, slice_address_length);
    }

    if (!h->sh.dependent_slice_segment_flag) {
        skip_bits(gb, h->ps.pps->num_extra_slice_header_bits);
        
        get_ue_golomb_long(gb);
        
        if (h->ps.pps->output_flag_present_flag)
            skip_bits1(gb);
    
        if (h->ps.sps->separate_colour_plane_flag)
            skip_bits(gb, 2);
    
        if (!IS_IDR(h)) {
            skip_bits(gb, h->ps.sps->log2_max_poc_lsb);
            skip_bits1(gb);
            
            bit_begin = get_bits_count(gb); 

            if (!h->sh.short_term_ref_pic_set_sps_flag) {
                skip_bits(gb, h->sh.short_term_ref_pic_set_size);
            } else {
                int numbits = av_ceil_log2(h->ps.sps->nb_st_rps);
                skip_bits(gb, numbits);
            }
            info->rps_bit_offset[slice_idx] = info->rps_bit_offset_st[slice_idx] = (get_bits_count(gb) - bit_begin);
                        
            if (h->ps.sps->long_term_ref_pics_present_flag) {
                int nb_sps = 0, nb_sh, nb_refs;
                bit_begin = get_bits_count(gb);
                if (h->ps.sps->num_long_term_ref_pics_sps > 0)
                    nb_sps = get_ue_golomb_long(gb);
                
                nb_sh = get_ue_golomb_long(gb);
                
                nb_refs = nb_sh + nb_sps;

                for(i = 0; i < nb_refs; i++) {
                    if (i < nb_sps) {
                        if (h->ps.sps->num_long_term_ref_pics_sps > 1)
                            skip_bits(gb, av_ceil_log2(h->ps.sps->num_long_term_ref_pics_sps ));
                    } else {
                        skip_bits(gb, h->ps.sps->log2_max_poc_lsb);
                        skip_bits1(gb);
                    }

                    if (get_bits1(gb)) {
                        get_ue_golomb_long(gb);
                    }
                }
                
                info->rps_bit_offset[slice_idx] += (get_bits_count(gb) - bit_begin);
            }
            
            if (h->ps.sps->sps_temporal_mvp_enabled_flag)
                skip_bits1(gb);
        }

        if (h->ps.sps->sao_enabled) {
            skip_bits1(gb);
            if (h->ps.sps->chroma_format_idc)
                skip_bits1(gb);
        }

        if (h->sh.slice_type == HEVC_SLICE_P || h->sh.slice_type == HEVC_SLICE_B) {
            if (get_bits1(gb)) {
                int nb_refs;
                get_ue_golomb_long(gb);
                if (h->sh.slice_type == HEVC_SLICE_B)
                    get_ue_golomb_long(gb);

                nb_refs = ff_hevc_frame_nb_refs(h);

                if (h->ps.pps->lists_modification_present_flag && nb_refs > 1) {
                    skip_bits1(gb);
                    if (h->sh.rpl_modification_flag[0]) {
                        for (i = 0; i < h->sh.nb_refs[L0]; i++)
                        skip_bits(gb, av_ceil_log2(nb_refs));
                    }

                    if (h->sh.slice_type == HEVC_SLICE_B) {
                        skip_bits1(gb);
                    if (h->sh.rpl_modification_flag[1] == 1)
                        for(i = 0; i < h->sh.nb_refs[L1]; i++)
                        skip_bits(gb, av_ceil_log2(nb_refs));
                    }
                }
            }
        }

        if (!h->sh.dependent_slice_segment_flag && h->sh.slice_type != HEVC_SLICE_I) {
            RefPicListTab ref;
            
            get_reorder_slice_rpl(avctx, &ref);
            info->lowdelay_flag[slice_idx] = 1;
            if (ref.refPicList) {
                unsigned int nb_list = HEVC_SLICE_I - h->sh.slice_type;
                for (j = 0; j < nb_list; j++) {
                    for (i = 0; i < ref.refPicList[j].nb_refs; i++) {
                        int index = 0;
                        index = ref.refPicList[j].list[i];
                        info->rps_pic_info[slice_idx][j][i].dpb_index = index;
                        info->rps_pic_info[slice_idx][j][i].is_long_term = h->ref->refPicList[j].isLongTerm[i];
                        if (h->ref->refPicList[j].list[i] > h->poc)
                            info->lowdelay_flag[slice_idx] = 0;
                    }
                }
            }
                
            if (h->sh.slice_type == HEVC_SLICE_I)
                info->slice_nb_rps_poc[slice_idx] = 0;
            else
                info->slice_nb_rps_poc[slice_idx] = h->rps[ST_CURR_BEF].nb_refs +
                                                    h->rps[ST_CURR_AFT].nb_refs +
                                                    h->rps[LT_CURR].nb_refs;;
        }
    }    
    
skip:
    if (nal.rbsp_buffer)
        av_freep(&nal.rbsp_buffer);

    return;

}

static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);
    static const unsigned char start_code[] = {0, 0, 1 };
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset = ctx->stream_data->pkt_size;
    unsigned int left_size = ctx->stream_data->linesize[0] - offset;

    if (data_ptr && left_size < (size + sizeof(start_code))) {
        AVFrame* new_pkt = av_frame_alloc();
        new_pkt->linesize[0] = offset + size + DATA_SIZE;
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
    ctx->slice_count++;

    av_log(avctx, LOG_LEVEL, "fill_stream_data pkg_size %d size %d.\n", ctx->stream_data->pkt_size, size);
}

static int rkvdec_hevc_regs_gen_rps(AVCodecContext* avctx)
{
    HEVCContext * const h = avctx->priv_data;    
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);    
    LPRKVDEC_Slice_RPS_Info info = ctx->rps_info;
    int i, j, k;
    int nb_slice = h->slice_idx + 1;
    int fifo_len = nb_slice * 4 + 1;
    unsigned long long int *rps_packet = av_mallocz(sizeof(unsigned long long int) * fifo_len);
    unsigned char *rps_ptr = ctx->rps_data->data[0];
    
    PutBitContext64 bp;
    init_put_bits_a64(&bp, rps_packet, fifo_len);
    
    for (k = 0; k < nb_slice; k++) {
        for (j = 0; j < 2; j++) {
            for (i = 0; i < 15; i++) {
                put_bits_a64(&bp, 1, info->rps_pic_info[k][j][i].is_long_term);
                if (j == 1 && i == 4) {
                    put_align_a64(&bp, 64, 0xf);
                }
                put_bits_a64(&bp, 4, info->rps_pic_info[k][j][i].dpb_index);
            }
        }
        put_bits_a64(&bp, 1, info->lowdelay_flag[k]);
        put_bits_a64(&bp, 10, info->rps_bit_offset[k]);
        put_bits_a64(&bp, 9, info->rps_bit_offset_st[k]);
        put_bits_a64(&bp, 4, info->slice_nb_rps_poc[k]);
        put_align_a64(&bp, 64, 0xf);        
    }

    if (rps_ptr) {
        memcpy(rps_ptr, rps_packet, nb_slice * 32);
    }

    av_free(rps_packet);

#ifdef dump_rps
    if (fp == NULL)
        fp = fopen("hal.bin", "wb");
    fwrite(rps_ptr, 1, nb_slice * 32, fp);
    fflush(fp);
#endif


    return 0;
}

static int rkvdec_hevc_regs_gen_pps(AVCodecContext* avctx)
{
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_HEVC pp = ctx->pic_param;
    const int len = 10;
    unsigned char column_width[20];
    unsigned char row_height[22];
    int width, height;
    unsigned int log2_min_cb_size, scaling_addr;
    PutBitContext64 bp;
    int i;
    
    void *pps_ptr = ctx->pps_data->data[0];
    unsigned char *scaling_ptr = ctx->scaling_list_data->data[0];
    int   pps_data_size = sizeof(unsigned long long int) * (len + 1);
    void *pps_data = av_mallocz(pps_data_size);
    memset(pps_ptr, 0, PPS_SIZE);
    memset(column_width, 0, sizeof(column_width));
    memset(row_height, 0, sizeof(row_height));

    /* sps */
    init_put_bits_a64(&bp, pps_data, len);
    
    put_bits_a64(&bp, 4, pp->vps_id);
    put_bits_a64(&bp, 4, pp->sps_id);
    put_bits_a64(&bp, 2, pp->chroma_format_idc);

    log2_min_cb_size = pp->log2_min_luma_coding_block_size_minus3 + 3;
    width = (pp->PicWidthInMinCbsY << log2_min_cb_size);
    height = (pp->PicHeightInMinCbsY << log2_min_cb_size);

    put_bits_a64(&bp, 13, width);
    put_bits_a64(&bp, 13, height);
    put_bits_a64(&bp, 4, pp->bit_depth_luma_minus8 + 8);
    put_bits_a64(&bp, 4, pp->bit_depth_chroma_minus8 + 8);
    put_bits_a64(&bp, 5, pp->log2_max_pic_order_cnt_lsb_minus4 + 4);
    put_bits_a64(&bp, 2, pp->log2_diff_max_min_luma_coding_block_size);
    put_bits_a64(&bp, 3, pp->log2_min_luma_coding_block_size_minus3 + 3);
    put_bits_a64(&bp, 3, pp->log2_min_transform_block_size_minus2 + 2);

    put_bits_a64(&bp, 2, pp->log2_diff_max_min_transform_block_size);
    put_bits_a64(&bp, 3, pp->max_transform_hierarchy_depth_inter);
    put_bits_a64(&bp, 3, pp->max_transform_hierarchy_depth_intra);
    put_bits_a64(&bp, 1, pp->scaling_list_enabled_flag);
    put_bits_a64(&bp, 1, pp->amp_enabled_flag);
    put_bits_a64(&bp, 1, pp->sample_adaptive_offset_enabled_flag);

    put_bits_a64(&bp, 1, pp->pcm_enabled_flag);
    put_bits_a64(&bp, 4, pp->pcm_enabled_flag ? (pp->pcm_sample_bit_depth_luma_minus1 + 1) : 0);
    put_bits_a64(&bp, 4, pp->pcm_enabled_flag ? (pp->pcm_sample_bit_depth_chroma_minus1 + 1) : 0);
    put_bits_a64(&bp, 1, pp->pcm_loop_filter_disabled_flag);
    put_bits_a64(&bp, 3, pp->log2_diff_max_min_pcm_luma_coding_block_size);
    put_bits_a64(&bp, 3, pp->pcm_enabled_flag ? (pp->log2_min_pcm_luma_coding_block_size_minus3 + 3) : 0);

    put_bits_a64(&bp, 7, pp->num_short_term_ref_pic_sets);
    put_bits_a64(&bp, 1, pp->long_term_ref_pics_present_flag);
    put_bits_a64(&bp, 6, pp->num_long_term_ref_pics_sps);
    put_bits_a64(&bp, 1, pp->sps_temporal_mvp_enabled_flag);
    put_bits_a64(&bp, 1, pp->strong_intra_smoothing_enabled_flag);

    put_bits_a64(&bp, 7, 0 );
    put_align_a64(&bp, 32, 0xf);

    /* pps */
    put_bits_a64(&bp, 6, pp->pps_id);
    put_bits_a64(&bp, 4, pp->sps_id);
    put_bits_a64(&bp, 1, pp->dependent_slice_segments_enabled_flag);
    put_bits_a64(&bp, 1, pp->output_flag_present_flag);
    put_bits_a64(&bp, 13, pp->num_extra_slice_header_bits);
    put_bits_a64(&bp, 1, pp->sign_data_hiding_enabled_flag);
    put_bits_a64(&bp, 1, pp->cabac_init_present_flag);
    put_bits_a64(&bp, 4, pp->num_ref_idx_l0_default_active_minus1 + 1);
    put_bits_a64(&bp, 4, pp->num_ref_idx_l1_default_active_minus1 + 1);
    put_bits_a64(&bp, 7, pp->init_qp_minus26);
    put_bits_a64(&bp, 1, pp->constrained_intra_pred_flag);
    put_bits_a64(&bp, 1, pp->transform_skip_enabled_flag);
    put_bits_a64(&bp, 1, pp->cu_qp_delta_enabled_flag);
    put_bits_a64(&bp, 3, log2_min_cb_size + pp->log2_diff_max_min_luma_coding_block_size - pp->diff_cu_qp_delta_depth);

    put_bits_a64(&bp, 5, pp->pps_cb_qp_offset);
    put_bits_a64(&bp, 5, pp->pps_cr_qp_offset);
    put_bits_a64(&bp, 1, pp->pps_slice_chroma_qp_offsets_present_flag);
    put_bits_a64(&bp, 1, pp->weighted_pred_flag);
    put_bits_a64(&bp, 1, pp->weighted_bipred_flag);
    put_bits_a64(&bp, 1, pp->transquant_bypass_enabled_flag);
    put_bits_a64(&bp, 1, pp->tiles_enabled_flag);
    put_bits_a64(&bp, 1, pp->entropy_coding_sync_enabled_flag);
    put_bits_a64(&bp, 1, pp->pps_loop_filter_across_slices_enabled_flag);
    put_bits_a64(&bp, 1, pp->loop_filter_across_tiles_enabled_flag);

    put_bits_a64(&bp, 1, pp->deblocking_filter_override_enabled_flag);
    put_bits_a64(&bp, 1, pp->pps_deblocking_filter_disabled_flag);
    put_bits_a64(&bp, 4, pp->pps_beta_offset_div2);
    put_bits_a64(&bp, 4, pp->pps_tc_offset_div2);
    put_bits_a64(&bp, 1, pp->lists_modification_present_flag);
    put_bits_a64(&bp, 3, pp->log2_parallel_merge_level_minus2 + 2);
    put_bits_a64(&bp, 1, pp->slice_segment_header_extension_present_flag);
    put_bits_a64(&bp, 3, 0);
    put_bits_a64(&bp, 5, pp->num_tile_columns_minus1 + 1);
    put_bits_a64(&bp, 5, pp->num_tile_rows_minus1 + 1);
    put_bits_a64(&bp, 2, 3);

    put_align_a64(&bp, 64, 0xf);

    if (pp->tiles_enabled_flag) {
        if (pp->uniform_spacing_flag == 0) {
            int maxcuwidth = pp->log2_diff_max_min_luma_coding_block_size + log2_min_cb_size;
            int ctu_width_in_pic = (width + (1 << maxcuwidth) - 1) / (1 << maxcuwidth);
            int ctu_height_in_pic = (height + (1 << maxcuwidth) - 1) / (1 << maxcuwidth);
            int sum = 0;
            for (i = 0; i < pp->num_tile_columns_minus1; i++) {
                column_width[i] = pp->column_width_minus1[i] + 1;
                sum += column_width[i]  ;
            }
            column_width[i] = ctu_width_in_pic - sum;

            sum = 0;
            for (i = 0; i < pp->num_tile_rows_minus1; i++) {
                row_height[i] = pp->row_height_minus1[i] + 1;
                sum += row_height[i];
            }
            row_height[i] = ctu_height_in_pic - sum;
        } else {
            int pic_in_cts_width = (width + (1 << (log2_min_cb_size +
                                                 pp->log2_diff_max_min_luma_coding_block_size)) - 1)
                                         / (1 << (log2_min_cb_size +
                                                 pp->log2_diff_max_min_luma_coding_block_size));
            int pic_in_cts_height = (height + (1 << (log2_min_cb_size +
                                                 pp->log2_diff_max_min_luma_coding_block_size)) - 1)
                                       / (1 << (log2_min_cb_size +
                                                 pp->log2_diff_max_min_luma_coding_block_size));

            for (i = 0; i < pp->num_tile_columns_minus1 + 1; i++)
                column_width[i] = ((i + 1) * pic_in_cts_width) / (pp->num_tile_columns_minus1 + 1) -
                                  (i * pic_in_cts_width) / (pp->num_tile_columns_minus1 + 1);

            for (i = 0; i < pp->num_tile_rows_minus1 + 1; i++)
                row_height[i] = ((i + 1) * pic_in_cts_height) / (pp->num_tile_rows_minus1 + 1) -
                                (i * pic_in_cts_height) / (pp->num_tile_rows_minus1 + 1);
        }
    } else {
        int max_cu_width = (1 << (pp->log2_diff_max_min_luma_coding_block_size + log2_min_cb_size));
        column_width[0] = (width  + max_cu_width - 1) / max_cu_width;
        row_height[0]   = (height + max_cu_width - 1) / max_cu_width;
    }

    for (i = 0; i < 20; i++) {
        if (column_width[i] > 0)
            column_width[i]--;
        put_bits_a64(&bp, 8, column_width[i]);
    }

    for (i = 0; i < 22; i++) {
        if (row_height[i] > 0)
            row_height[i]--;
        put_bits_a64(&bp, 8, row_height[i]);
    }

    if (pp->scaling_list_data_present_flag)
        scaling_addr = (pp->pps_id + 16) * 1360;
    else if (pp->scaling_list_enabled_flag)
        scaling_addr = pp->sps_id * 1360;
    else
        scaling_addr = 80 * 1360;

    memcpy(scaling_ptr + scaling_addr, ctx->scaling_rk, sizeof(RKVDEC_ScalingFactor_Model_HEVC));
    scaling_addr = (ff_rkvdec_get_fd(ctx->scaling_list_data) | (scaling_addr << 10));
    put_bits_a64(&bp, 32, scaling_addr);

    put_align_a64(&bp, 64, 0xf);

    for (i = 0; i < 64; i++) {
        memcpy((unsigned char*)pps_ptr + i * 80, pps_data, 80);
    }

#ifdef dump_pps
    if (fp == NULL)
        fp = fopen("hal.bin", "wb");
    fwrite(pps_ptr, 1, 80 * 64, fp);
    fflush(fp);
#endif

    av_free(pps_data);
    
    return 0;
}

static int rkvdec_hevc_regs_gen_reg(AVCodecContext *avctx)
{
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);    
    LPRKVDEC_PicParams_HEVC pp = ctx->pic_param;
    LPRKVDEC_HEVC_Regs hw_regs = ctx->hw_regs;
    unsigned int uiMaxCUWidth, uiMaxCUHeight;
    unsigned int log2_min_cb_size;
    int align_offset, valid_ref;
    int width, height, numCuInWidth;
    int stride_y, stride_uv, virstrid_y, virstrid_yuv;
    int i;
    
    memset(hw_regs, 0, sizeof(RKVDEC_HEVC_Regs));

    uiMaxCUWidth = 1 << (pp->log2_diff_max_min_luma_coding_block_size +
                         pp->log2_min_luma_coding_block_size_minus3 + 3);
    uiMaxCUHeight = uiMaxCUWidth;
    log2_min_cb_size = pp->log2_min_luma_coding_block_size_minus3 + 3;
    width = (pp->PicWidthInMinCbsY << log2_min_cb_size);
    height = (pp->PicHeightInMinCbsY << log2_min_cb_size);
    numCuInWidth   = width / uiMaxCUWidth  + (width % uiMaxCUWidth != 0);

    stride_y      = (ALIGN((numCuInWidth * uiMaxCUWidth * (pp->bit_depth_luma_minus8 + 8)), 2048) >> 3);
    stride_uv     = (ALIGN((numCuInWidth * uiMaxCUHeight * (pp->bit_depth_luma_minus8 + 8)), 2048) >> 3);

    virstrid_y    = stride_y * height;
    virstrid_yuv  = virstrid_y + stride_uv * height / 2;

    hw_regs->sw_picparameter.sw_slice_num = ctx->slice_count;
    hw_regs->sw_picparameter.sw_y_hor_virstride = stride_y >> 4;
    hw_regs->sw_picparameter.sw_uv_hor_virstride = stride_uv >> 4;
    hw_regs->sw_y_virstride = virstrid_y >> 4;
    hw_regs->sw_yuv_virstride = virstrid_yuv >> 4;

    hw_regs->sw_decout_base = pp->CurrPic.Index7Bits;
    hw_regs->swreg78_colmv_cur_base.sw_colmv_base = pp->CurrMv.Index7Bits;
    hw_regs->sw_cur_poc = pp->CurrPicOrderCntVal;

    hw_regs->sw_cabactbl_base = ff_rkvdec_get_fd(ctx->cabac_table_data);
    hw_regs->sw_pps_base      = ff_rkvdec_get_fd(ctx->pps_data);
    hw_regs->sw_rps_base      = ff_rkvdec_get_fd(ctx->rps_data);
    hw_regs->sw_strm_rlc_base = ff_rkvdec_get_fd(ctx->stream_data);

    hw_regs->sw_stream_len = ((ctx->stream_data->pkt_size + 15) & (~15)) + 64;
    align_offset = hw_regs->sw_stream_len - ctx->stream_data->pkt_size;
    if (align_offset > 0) {
        memset(ctx->stream_data->data[0] + ctx->stream_data->pkt_size, 0, align_offset);
    }

    hw_regs->sw_interrupt.sw_dec_e         = 1;
    hw_regs->sw_interrupt.sw_dec_timeout_e = 1;
    hw_regs->sw_interrupt.sw_wr_ddr_align_en = pp->tiles_enabled_flag ? 0 : 1;

    hw_regs->sw_ref_valid = 0;
    hw_regs->cabac_error_en = 0xfdfffffd;
    hw_regs->extern_error_en = 0x30000000;

    valid_ref = hw_regs->sw_decout_base;
    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefPicList); i++) {
        if (pp->RefPicList[i].bPicEntry != 0xff &&
            pp->RefPicList[i].bPicEntry != 0x7f) {
            hw_regs->sw_refer_poc[i] = pp->PicOrderCntValList[i];
            hw_regs->sw_refer_base[i] = pp->RefPicList[i].bPicEntry;

            if (pp->RefPicList[i].bPicEntry > 0) {
                hw_regs->sw_refer_base[i] = pp->RefPicList[i].bPicEntry;
                valid_ref = hw_regs->sw_refer_base[i];
            } else {
                hw_regs->sw_refer_base[i] = valid_ref;
            }
            hw_regs->sw_ref_valid |= (1 << i);
        } else {
            hw_regs->sw_refer_base[i] = hw_regs->sw_decout_base;
        }
    }

    valid_ref = hw_regs->swreg78_colmv_cur_base.sw_colmv_base;
    for (i = 0; i < 16; i++) {
        if (pp->RefColmvList[i].bPicEntry != 0xff && pp->RefColmvList[i].bPicEntry > 0) {
            hw_regs->swreg79_94_colmv0_15_base[i].sw_colmv_base = valid_ref = pp->RefColmvList[i].Index7Bits;
        } else {
            hw_regs->swreg79_94_colmv0_15_base[i].sw_colmv_base = valid_ref;
        }
    }

    hw_regs->sw_refer_base[0] |= ((hw_regs->sw_ref_valid & 0xf) << 10);
    hw_regs->sw_refer_base[1] |= (((hw_regs->sw_ref_valid >> 4) & 0xf) << 10);
    hw_regs->sw_refer_base[2] |= (((hw_regs->sw_ref_valid >> 8) & 0xf) << 10);
    hw_regs->sw_refer_base[3] |= (((hw_regs->sw_ref_valid >> 12) & 0x7) << 10);

    if (ctx->motion_val_pool && hw_regs->swreg78_colmv_cur_base.sw_colmv_base)
        hw_regs->sw_sysctrl.sw_colmv_mode = 1;

#ifdef debug_regs
    unsigned char *p = hw_regs;
    for (i = 0; i < 68 ; i++) {
        av_log(avctx, AV_LOG_DEBUG, "RK_HEVC_DEC: regs[%02d]=%08X\n", i, *((unsigned int*)p));
        p += 4;
    }
#endif

    return 0;
}

static void rkvdec_h265_free_dmabuffer(void* opaque, uint8_t *data)
{
    RKVDECHevcContext * const ctx = opaque;
    AVFrame* colmv = (AVFrame*)data;

    av_log(NULL, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_h265_free_dmabuffer size: %d.\n", colmv->linesize[0]);
    if (!ctx || !colmv)
        return;
    ctx->allocator->free(ctx->allocator_ctx, colmv);
    av_freep(&colmv);
}

static AVBufferRef* rkvdec_h265_alloc_dmabuffer(void* opaque, int size)
{
    RKVDECHevcContext * const ctx = opaque;
    AVFrame* colmv = av_frame_alloc();

    av_log(NULL, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_h265_alloc_dmabuffer size: %d.\n", size);
    colmv->linesize[0] = size;
    ctx->allocator->alloc(ctx->allocator_ctx, colmv);
    return av_buffer_create((uint8_t*)colmv, size, rkvdec_h265_free_dmabuffer, ctx, 0);
}

static int fill_picture_colmv(const HEVCContext* h)
{
    RKVDECHevcContext* const ctx = ff_rkvdec_get_context(h->avctx);
    HEVCFrame* current_picture = h->ref;

    int enable_colmv = 1;

    if (!enable_colmv && !ctx->motion_val_pool)
     return 0;

    const int colmv_size = (current_picture->frame->width * current_picture->frame->height * 3 / 2) / 16;

    if (!ctx->motion_val_pool) {
        ctx->motion_val_pool = av_buffer_pool_init2(colmv_size, ctx, rkvdec_h265_alloc_dmabuffer, NULL);
    }

    AVBufferRef* prev_ref = current_picture->hwaccel_priv_buf;
    current_picture->hwaccel_priv_buf = av_buffer_pool_get(ctx->motion_val_pool);
    current_picture->hwaccel_picture_private = current_picture->hwaccel_priv_buf->data;
    if (prev_ref)
      av_buffer_unref(&prev_ref);

    return 0;
}

 /** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_hevc_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    HEVCContext * const h = avctx->priv_data;
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_hevc_start_frame\n");
    pthread_mutex_lock(&ctx->hwaccel_mutex);
    fill_picture_colmv(h);
    fill_picture_parameters(h, ctx->pic_param);
    fill_scaling_lists(h, ctx->scaling_list, ctx->scaling_rk);

    memset(ctx->rps_info, 0, sizeof(RKVDEC_Slice_RPS_Info));
    ctx->stream_data->pkt_size = 0;
    ctx->slice_count = 0;

    return 0;   
}

/** End a hardware decoding based frame. */
static int rkvdec_hevc_end_frame(AVCodecContext *avctx)
{
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);
    HEVCContext * const h = avctx->priv_data;
    RKVDECHevcHwReq req;
    int ret;
    int64_t t;

    av_log(avctx, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_hevc_end_frame.\n");
    rkvdec_hevc_regs_gen_pps(avctx);
    rkvdec_hevc_regs_gen_rps(avctx);
    rkvdec_hevc_regs_gen_reg(avctx);

    req.req = (unsigned int*)ctx->hw_regs;
    req.size = ctx->motion_val_pool ? 95 * sizeof(unsigned int) : 78 * sizeof(unsigned int);

    if (avctx->active_thread_type & FF_THREAD_FRAME) {
        ff_thread_finish_setup(avctx);
    }

    t = av_gettime_relative();

    av_log(avctx, LOG_LEVEL, "ioctl VPU_IOC_SET_REG start.\n");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d.\n", ret);

    av_log(avctx, LOG_LEVEL, "ioctl VPU_IOC_GET_REG start.\n");
    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);
    av_log(avctx, LOG_LEVEL, "ioctl VPU_IOC_GET_REG success. cost %lld.\n", (av_gettime_relative() - t));

    pthread_mutex_unlock(&ctx->hwaccel_mutex);

    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d.\n", ret);

#ifdef dump_frame
if (once-- < 0) {
    if (fp == NULL)
        fp = fopen("hal.bin", "wb");
    fwrite(h->ref->frame->data[0], 1, 3840*2160*1.5, fp);
    fflush(fp);
}
#endif

    return 0;
}

/** Decode the given hevc slice with RKVDEC. */
static int rkvdec_hevc_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{    
    av_log(avctx, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_hevc_decode_slice size:%d\n", size);
    
    fill_rps_info(avctx, buffer, size);
    fill_stream_data(avctx, buffer, size);

    return 0;
}

static int rkvdec_hevc_context_init(AVCodecContext *avctx)
{

    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);
    int ret;

    av_log(avctx, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_hevc_context_init\n");
    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }

    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_HEVC));  
    ctx->scaling_list = av_mallocz(sizeof(RKVDEC_ScalingList_HEVC));
    ctx->scaling_rk = av_mallocz(sizeof(RKVDEC_ScalingFactor_Model_HEVC));
    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_HEVC_Regs));
    ctx->rps_info = av_mallocz(sizeof(RKVDEC_Slice_RPS_Info));

    ctx->cabac_table_data = av_frame_alloc();
    ctx->cabac_table_data->linesize[0] = sizeof(cabac_table);
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->cabac_table_data);
    memcpy(ctx->cabac_table_data->data[0], cabac_table, sizeof(cabac_table));

    ctx->scaling_list_data = av_frame_alloc();
    ctx->scaling_list_data->linesize[0] = SCALING_LIST_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->scaling_list_data);

    ctx->pps_data = av_frame_alloc();
    ctx->pps_data->linesize[0] = PPS_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->pps_data);

    ctx->rps_data = av_frame_alloc();
    ctx->rps_data->linesize[0] = RPS_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->rps_data);

    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = DATA_SIZE;
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->stream_data);

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

static int rkvdec_hevc_context_uninit(AVCodecContext *avctx)
{
    RKVDECHevcContext * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, LOG_LEVEL, "RK_HEVC_DEC: rkvdec_hevc_context_uninit\n");
    ctx->allocator->free(ctx->allocator_ctx, ctx->cabac_table_data);    
    ctx->allocator->free(ctx->allocator_ctx, ctx->scaling_list_data);    
    ctx->allocator->free(ctx->allocator_ctx, ctx->pps_data);
    ctx->allocator->free(ctx->allocator_ctx, ctx->rps_data);
    ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);

    av_free(ctx->cabac_table_data);
    av_free(ctx->scaling_list_data);
    av_free(ctx->pps_data);
    av_free(ctx->rps_data);
    av_free(ctx->stream_data);

    av_free(ctx->pic_param);
    av_free(ctx->scaling_list);
    av_free(ctx->scaling_rk);
    av_free(ctx->hw_regs);
    av_free(ctx->rps_info);

    if (ctx->motion_val_pool)
        av_buffer_pool_uninit(&ctx->motion_val_pool);

    ctx->allocator->close(ctx->allocator_ctx);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }

    pthread_mutex_destroy(&ctx->hwaccel_mutex);
    return 0;
}

static int rkvdec_hevc_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
    av_assert0(frame);
    av_assert0(frame->width);
    av_assert0(frame->height);
    frame->width = ALIGN(frame->width, 256);
    return avctx->get_buffer2(avctx, frame, 0);
}

AVHWAccel ff_hevc_rkvdec_hwaccel = {
    .name                 = "hevc_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_HEVC,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .start_frame          = rkvdec_hevc_start_frame,
    .alloc_frame          = rkvdec_hevc_alloc_frame,
    .end_frame            = rkvdec_hevc_end_frame,
    .decode_slice         = rkvdec_hevc_decode_slice,
    .init                 = rkvdec_hevc_context_init,
    .uninit               = rkvdec_hevc_context_uninit,
    .priv_data_size       = sizeof(RKVDECHevcContext),
    .frame_priv_data_size = sizeof(RKVDECHevcFrameData),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};

AVHWAccel ff_hevc_rkvdec10_hwaccel = {
    .name                 = "hevc_rkvdec10",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_HEVC,
    .pix_fmt              = AV_PIX_FMT_P010LE,
    .start_frame          = rkvdec_hevc_start_frame,
    .end_frame            = rkvdec_hevc_end_frame,
    .decode_slice         = rkvdec_hevc_decode_slice,
    .init                 = rkvdec_hevc_context_init,
    .uninit               = rkvdec_hevc_context_uninit,
    .priv_data_size       = sizeof(RKVDECHevcContext),
    .frame_priv_data_size = sizeof(RKVDECHevcFrameData),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE | HWACCEL_CAP_THREAD_SAFE,
};

