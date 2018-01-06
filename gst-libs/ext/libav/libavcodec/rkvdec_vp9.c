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

#include "vp9.h"
#include "vp9dec.h"
#include "rkvdec_vp9.h"
#include "allocator_drm.h"
#include "put_bits64.h"
#include "libavutil/time.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/internal.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>


//#define dump_probe
#ifdef dump_probe
static RK_S32 num_probe = 0;
FILE* fp_vp9_probe=NULL;
#endif

//#define dump_count
#ifdef dump_count
static RK_S32 num_count = 0;
FILE *fp_vp9_count=NULL;
#endif


typedef struct _RKVDECVP9Context 		RKVDECVP9Context;
typedef struct _RKVDECVP9FrameData		RKVDEC_FrameData_VP9, *LPRKVDEC_FrameData_VP9;
typedef struct _VP9dRkvRegs_t			RKVDEC_VP9_Regs, *LPRKVDEC_VP9_Regs;
typedef struct _RKVDEC_PicParams_VP9	RKVDEC_PicParams_VP9, *LPRKVDEC_PicParams_VP9;
typedef struct _RKVDEC_PicEntry_VP9		RKVDEC_PicEntry_VP9, *LPRKVDEC_PicEntry_VP9;
typedef struct _RKVDECVP9HwReq 			RKVDECVP9HwReq;


struct _RKVDECVP9Context{
    signed int vpu_socket;
    LPRKVDEC_VP9_Regs hw_regs;
    LPRKVDEC_PicParams_VP9 pic_param;
    AVFrame* probe_base;
    AVFrame* count_base;
    AVFrame* segid_cur_base;
    AVFrame* segid_last_base;
    AVFrame* stream_data;
	
    RK_S32 mv_base_addr;
    RK_S32 pre_mv_base_addr;
	RK_U32 last_segid_flag;
	vp9_dec_last_info_t ls_info;
    os_allocator *allocator;
    void *allocator_ctx;
};

struct _RKVDECVP9FrameData{
    AVFrame colmv;
};

struct _RKVDEC_PicEntry_VP9 {
    union {
        struct {
            UCHAR Index7Bits     : 7;
            UCHAR AssociatedFlag : 1;
        };
        UCHAR bPicEntry;
    };
};

struct _RKVDEC_segmentation_VP9 {
    union {
        struct {
            UCHAR enabled : 1;
            UCHAR update_map : 1;
            UCHAR temporal_update : 1;
            UCHAR abs_delta : 1;
            UCHAR ReservedSegmentFlags4Bits : 4;
        };
        UCHAR wSegmentInfoFlags;
    };
    UCHAR tree_probs[7];
    UCHAR pred_probs[3];
    SHORT feature_data[8][4];
    UCHAR feature_mask[8];
};


struct _RKVDEC_PicParams_VP9 {
    struct _RKVDEC_PicEntry_VP9 CurrPic;
    UCHAR profile;
    union {
        struct {
            USHORT frame_type : 1;
            USHORT show_frame : 1;
            USHORT error_resilient_mode : 1;
            USHORT subsampling_x : 1;
            USHORT subsampling_y : 1;
            USHORT extra_plane : 1;
            USHORT refresh_frame_context : 1;
            USHORT intra_only : 1;
            USHORT frame_context_idx : 2;
            USHORT reset_frame_context : 2;
            USHORT allow_high_precision_mv : 1;
            USHORT parallelmode            : 1;
            USHORT ReservedFormatInfo2Bits : 1;
        };
        USHORT wFormatAndPictureInfoFlags;
    };
    UINT width;
    UINT height;
    UCHAR BitDepthMinus8Luma;
    UCHAR BitDepthMinus8Chroma;
    UCHAR interp_filter;
    UCHAR Reserved8Bits;
    struct _RKVDEC_PicEntry_VP9 ref_frame_map[8];
    UINT ref_frame_coded_width[8];
    UINT ref_frame_coded_height[8];
    struct _RKVDEC_PicEntry_VP9 frame_refs[3];
    CHAR ref_frame_sign_bias[4];
    CHAR filter_level;
    CHAR sharpness_level;
    union {
        struct {
            UCHAR mode_ref_delta_enabled : 1;
            UCHAR mode_ref_delta_update : 1;
            UCHAR use_prev_in_find_mv_refs : 1;
            UCHAR ReservedControlInfo5Bits : 5;
        };
        UCHAR wControlInfoFlags;
    };
    CHAR ref_deltas[4];
    CHAR mode_deltas[2];
    SHORT base_qindex;
    CHAR y_dc_delta_q;
    CHAR uv_dc_delta_q;
    CHAR uv_ac_delta_q;
    struct _RKVDEC_segmentation_VP9 stVP9Segments;
    UCHAR log2_tile_cols;
    UCHAR log2_tile_rows;
    USHORT uncompressed_header_size_byte_aligned;
    USHORT first_partition_size;
    USHORT Reserved16Bits;
    USHORT Reserved32Bits;
    UINT StatusReportFeedbackNumber;
    struct {
        UCHAR y_mode[4][9];
        UCHAR uv_mode[10][9];
        UCHAR filter[4][2];
        UCHAR mv_mode[7][3];
        UCHAR intra[4];
        UCHAR comp[5];
        UCHAR single_ref[5][2];
        UCHAR comp_ref[5];
        UCHAR tx32p[2][3];
        UCHAR tx16p[2][2];
        UCHAR tx8p[2];
        UCHAR skip[3];
        UCHAR mv_joint[3];
        struct {
            UCHAR sign;
            UCHAR classes[10];
            UCHAR class0;
            UCHAR bits[10];
            UCHAR class0_fp[2][3];
            UCHAR fp[3];
            UCHAR class0_hp;
            UCHAR hp;
        } mv_comp[2];
        UCHAR partition[4][4][3];
        UCHAR coef[4][2][2][6][6][11];
    } prob;
    struct {
        UINT partition[4][4][4];
        UINT skip[3][2];
        UINT intra[4][2];
        UINT tx32p[2][4];
        UINT tx16p[2][4];
        UINT tx8p[2][2];
        UINT y_mode[4][10];
        UINT uv_mode[10][10];
        UINT comp[5][2];
        UINT comp_ref[5][2];
        UINT single_ref[5][2][2];
        UINT mv_mode[7][4];
        UINT filter[4][3];
        UINT mv_joint[4];
        UINT sign[2][2];
        UINT classes[2][12]; // orign classes[12]
        UINT class0[2][2];
        UINT bits[2][10][2];
        UINT class0_fp[2][2][4];
        UINT fp[2][4];
        UINT class0_hp[2][2];
        UINT hp[2][2];
        UINT coef[4][2][2][6][6][3];
        UINT eob[4][2][2][6][6][2];
    } counts;
    USHORT mvscale[3][2];
    CHAR txmode;
    CHAR refmode;
} ;


struct _RKVDECVP9HwReq {
    unsigned int *req;
    unsigned int  size;
} ;


#define RKVDECVP9_PROBE_SIZE   	4864
#define RKVDECVP9_COUNT_SIZE   	13208
#define RKVDECVP9_MAX_SEGMAP_SIZE  73728
#define RKVDECVP9_DATA_SIZE        (2048 * 1024) 

#define PARTITION_CONTEXTS          16
#define PARTITION_TYPES             4
#define MAX_SEGMENTS                8
#define SEG_TREE_PROBS              (MAX_SEGMENTS-1)
#define PREDICTION_PROBS            3
#define SKIP_CONTEXTS               3
#define TX_SIZE_CONTEXTS            2
#define TX_SIZES                    4
#define INTRA_INTER_CONTEXTS        4
#define PLANE_TYPES                 2
#define COEF_BANDS                  6
#define COEFF_CONTEXTS              6
#define UNCONSTRAINED_NODES         3
#define INTER_PROB_SIZE_ALIGN_TO_128    151
#define INTRA_PROB_SIZE_ALIGN_TO_128    149
#define BLOCK_SIZE_GROUPS 4
#define COMP_INTER_CONTEXTS 5
#define REF_CONTEXTS 5
#define INTER_MODE_CONTEXTS 7
#define SWITCHABLE_FILTERS 3   // number of switchable filters
#define SWITCHABLE_FILTER_CONTEXTS (SWITCHABLE_FILTERS + 1)
#define INTER_MODES 4
#define MV_JOINTS     4
#define MV_CLASSES     11
#define CLASS0_BITS    1  /* bits at integer precision for class 0 */
#define CLASS0_SIZE    (1 << CLASS0_BITS)
#define MV_OFFSET_BITS (MV_CLASSES + CLASS0_BITS - 2)
#define MV_FP_SIZE 4
#define CUR_FRAME 0

#define ALIGN(value, x) (((value) + (x) - 1)&~((x) - 1))


/** Extract rkvdec_context from an AVCodecContext */
static inline RKVDECVP9Context *ff_rkvdec_get_context(AVCodecContext *avctx)
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


static void fill_picture_entry(LPRKVDEC_PicEntry_VP9 pic,  unsigned int index, unsigned int flag)
{
    av_assert0((index & 0x7f) == index && (flag & 0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static void fill_picture_parameters(AVCodecContext *avctx, LPRKVDEC_PicParams_VP9 pp)
{
    int i;
	RK_U8 partition_probs[16][3];
    RK_U8 uv_mode_prob[10][9];
    const AVPixFmtDescriptor * pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
	VP9Context * const h = avctx->priv_data;
    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: fill_picture_parameters\n");
	
    if (!pixdesc)
        return;
    memset(pp, 0, sizeof(*pp));

    fill_picture_entry(&pp->CurrPic, ff_rkvdec_get_fd(h->s.frames[CUR_FRAME].tf.f), 0);

    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: fill_picture_parameters keyframe = %d \n", h->s.h.keyframe);
	av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: fill_picture_parameters intraonly = %d \n", h->s.h.intraonly);
    pp->profile = h->s.h.profile;
	pp->frame_type = !h->s.h.keyframe;
	pp->show_frame = !h->s.h.invisible;
    pp->error_resilient_mode =  h->s.h.errorres;
	pp->refresh_frame_context = h->s.h.refreshctx;
	pp->intra_only = h->s.h.intraonly;
	pp->frame_context_idx = h->s.h.framectxid;
    pp->reset_frame_context = h->s.h.resetctx;
    pp->allow_high_precision_mv = h->s.h.highprecisionmvs;
    pp->parallelmode = h->s.h.parallelmode;
    pp->width  = avctx->width;
    pp->height = avctx->height; 
    pp->BitDepthMinus8Luma   = pixdesc->comp[0].depth - 8;
    pp->BitDepthMinus8Chroma = pixdesc->comp[1].depth - 8;
	
    /* swap 0/1 to match the reference */
    //pp->interp_filter = h->s.h.filtermode ^ (h->s.h.filtermode <= 1);
    pp->interp_filter = h->s.h.filtermode;
    pp->Reserved8Bits = 0;

    for (i = 0; i < 8; i++) {
        if (h->s.refs[i].f->buf[0]) {
            fill_picture_entry(&pp->ref_frame_map[i], ff_rkvdec_get_fd(h->s.refs[i].f), 0);
            pp->ref_frame_coded_width[i]  = h->s.refs[i].f->width;
            pp->ref_frame_coded_height[i] = h->s.refs[i].f->height;
			av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: pp->ref_frame_map[%d].bPicEntry = %d\n", i, pp->ref_frame_map[i].bPicEntry);
        } else{
            pp->ref_frame_map[i].bPicEntry = 0xFF;
        }
    }
    for (i = 0; i < 3; i++) {
        pp->frame_refs[i].Index7Bits = h->s.h.refidx[i];
        pp->ref_frame_sign_bias[i + 1] = h->s.h.signbias[i];
    }

    pp->filter_level    = h->s.h.filter.level;
    pp->sharpness_level = h->s.h.filter.sharpness;
    pp->wControlInfoFlags = (h->s.h.lf_delta.enabled   << 0) |
                            (h->s.h.lf_delta.updated   << 1) |
                            (h->s.h.use_last_frame_mvs << 2) |
                            (0                       << 3);  /* ReservedControlInfo5Bits */

    for (i = 0; i < 4; i++)
        pp->ref_deltas[i]  = h->s.h.lf_delta.ref[i];

    for (i = 0; i < 2; i++)
        pp->mode_deltas[i]  = h->s.h.lf_delta.mode[i];

    pp->base_qindex   = h->s.h.yac_qi;
    pp->y_dc_delta_q  = h->s.h.ydc_qdelta;
    pp->uv_dc_delta_q = h->s.h.uvdc_qdelta;
    pp->uv_ac_delta_q = h->s.h.uvac_qdelta;
    pp->txmode = h->s.h.txfmmode;
    pp->refmode = h->s.h.comppredmode;
    /* segmentation data */
    pp->stVP9Segments.wSegmentInfoFlags = (h->s.h.segmentation.enabled       << 0) |
                                          (h->s.h.segmentation.update_map    << 1) |
                                          (h->s.h.segmentation.temporal      << 2) |
                                          (h->s.h.segmentation.absolute_vals << 3) |
                                          (0                               << 4);  /* ReservedSegmentFlags4Bits */

    for (i = 0; i < 7; i++)
        pp->stVP9Segments.tree_probs[i] = h->s.h.segmentation.prob[i];

    if (h->s.h.segmentation.temporal)
        for (i = 0; i < 3; i++)
            pp->stVP9Segments.pred_probs[i] = h->s.h.segmentation.pred_prob[i];
    else
        memset(pp->stVP9Segments.pred_probs, 0, sizeof(pp->stVP9Segments.pred_probs));
		//memset(pp->stVP9Segments.pred_probs, 255, sizeof(pp->stVP9Segments.pred_probs));

	


    for (i = 0; i < 8; i++) {
        pp->stVP9Segments.feature_mask[i] = (h->s.h.segmentation.feat[i].q_enabled    << 0) |
                                            (h->s.h.segmentation.feat[i].lf_enabled   << 1) |
                                            (h->s.h.segmentation.feat[i].ref_enabled  << 2) |
                                            (h->s.h.segmentation.feat[i].skip_enabled << 3);

        pp->stVP9Segments.feature_data[i][0] = h->s.h.segmentation.feat[i].q_val;
        pp->stVP9Segments.feature_data[i][1] = h->s.h.segmentation.feat[i].lf_val;
        pp->stVP9Segments.feature_data[i][2] = h->s.h.segmentation.feat[i].ref_val;
        pp->stVP9Segments.feature_data[i][3] = 0; /* no data for skip */
    }

    pp->log2_tile_cols = h->s.h.tiling.log2_tile_cols;
    pp->log2_tile_rows = h->s.h.tiling.log2_tile_rows;
    pp->uncompressed_header_size_byte_aligned = h->s.h.uncompressed_header_size;
    pp->first_partition_size = 0;
	
	memcpy(pp->mvscale, h->mvscale, sizeof(h->mvscale));
	memcpy(&pp->prob, &h->prob, sizeof(pp->prob));
    {
        RK_U8 *uv_ptr = NULL;
        RK_U32 m = 0;
        /*change partition to hardware need style*/
        /*
              hardware            syntax
          *+++++8x8+++++*     *++++64x64++++*
          *+++++16x16+++*     *++++32x32++++*
          *+++++32x32+++*     *++++16x16++++*
          *+++++64x64+++*     *++++8x8++++++*
        */
        m = 0;
        for (i = 3; i >= 0; i--) {
            memcpy(&partition_probs[m][0], &pp->prob.partition[i][0][0], 12);
            m += 4;
        }
        /*change uv_mode to hardware need style*/
        /*
            hardware              syntax
         *+++++ dc  ++++*     *++++ v   ++++*
         *+++++ v   ++++*     *++++ h   ++++*
         *+++++ h   ++++*     *++++ dc  ++++*
         *+++++ d45 ++++*     *++++ d45 ++++*
         *+++++ d135++++*     *++++ d135++++*
         *+++++ d117++++*     *++++ d117++++*
         *+++++ d153++++*     *++++ d153++++*
         *+++++ d207++++*     *++++ d63 ++++*
         *+++++ d63 ++++*     *++++ d207++++*
         *+++++ tm  ++++*     *++++ tm  ++++*
        */

        for (i = 0; i < 10; i++) {
            if (i == 0) {
                uv_ptr = pp->prob.uv_mode[2];//dc
            } else if ( i == 1) {
                uv_ptr =  pp->prob.uv_mode[0]; //h
            }  else if ( i == 2) {
                uv_ptr = pp->prob.uv_mode[1]; //h
            }  else if ( i == 7) {
                uv_ptr = pp->prob.uv_mode[8]; //d207
            } else if (i == 8) {
                uv_ptr = pp->prob.uv_mode[7]; //d63
            } else {
                uv_ptr = pp->prob.uv_mode[i];
            }
            memcpy(&uv_mode_prob[i], uv_ptr, 9);
        }
        memcpy(pp->prob.partition, partition_probs, sizeof(partition_probs));
        memcpy(pp->prob.uv_mode, uv_mode_prob, sizeof(uv_mode_prob));
    }
}



static void fill_stream_data(AVCodecContext* avctx, const uint8_t  *buffer, uint32_t size)
{
    RKVDECVP9Context * const ctx = ff_rkvdec_get_context(avctx);
    unsigned char *data_ptr = ctx->stream_data->data[0];
    unsigned int offset = ctx->stream_data->pkt_size;
    unsigned int left_size = ctx->stream_data->linesize[0] - offset;
    if (data_ptr && left_size < size) {
        AVFrame* new_pkt = av_frame_alloc();
        new_pkt->linesize[0] = offset + size + RKVDECVP9_DATA_SIZE;
        ctx->allocator->alloc(ctx->allocator_ctx, new_pkt);
        memcpy(new_pkt->data[0], ctx->stream_data->data[0], ctx->stream_data->pkt_size);
        ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
        memcpy(ctx->stream_data, new_pkt, sizeof(AVFrame));
        data_ptr = ctx->stream_data->data[0];
        av_free(new_pkt);
    }

    memcpy(data_ptr + offset, buffer, size);
    ctx->stream_data->pkt_size += size;
    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: fill_stream_data pkg_size %d size %d", ctx->stream_data->pkt_size, size);
}



static void rkvdec_vp9_inv_count_data(AVCodecContext* avctx)
{
	VP9Context * const s = avctx->priv_data;
	
	RK_U32 partition_probs[4][4][4];
	RK_U32 count_uv[10][10];
	RK_U32 count_y_mode[4][10];
	RK_U32 *dst_uv = NULL;
	RK_S32 i, j;

	/*
				 syntax 			 hardware
			 *+++++64x64+++++*	 *++++8x8++++*
			 *+++++32x32+++*	 *++++16x16++++*
			 *+++++16x16+++*	 *++++32x32++++*
			 *+++++8x8+++*		 *++++64x64++++++*
	 */

	memcpy(&partition_probs, s->counts.partition, sizeof(s->counts.partition));
	j = 0;
	for (i = 3; i >= 0; i--) {
		memcpy(&s->counts.partition[j], &partition_probs[i], 64);
		j++;
	}
	if (!(s->s.h.keyframe || s->s.h.intraonly)) {
		memcpy(count_y_mode, s->counts.y_mode, sizeof(s->counts.y_mode));
		for (i = 0; i < 4; i++) {
			RK_U32 value = 0;
			for (j = 0; j < 10; j++) {
				value = count_y_mode[i][j];
				if (j == 0)
					s->counts.y_mode[i][2] = value;
				else if (j == 1)
					s->counts.y_mode[i][0] = value;
				else if (j == 2)
					s->counts.y_mode[i][1] = value;
				else if (j == 7)
					s->counts.y_mode[i][8] = value;
				else if (j == 8)
					s->counts.y_mode[i][7] = value;
				else
					s->counts.y_mode[i][j] = value;

			}
		}


		memcpy(count_uv, s->counts.uv_mode, sizeof(s->counts.uv_mode));

		/*change uv_mode to hardware need style*/
		/*
			  syntax			  hardware
		 *+++++ v	++++*	  *++++ dc	 ++++*
		 *+++++ h	++++*	  *++++ v	++++*
		 *+++++ dc	++++*	  *++++ h  ++++*
		 *+++++ d45 ++++*	  *++++ d45 ++++*
		 *+++++ d135++++*	  *++++ d135++++*
		 *+++++ d117++++*	  *++++ d117++++*
		 *+++++ d153++++*	  *++++ d153++++*
		 *+++++ d63 ++++*	  *++++ d207++++*
		 *+++++ d207 ++++*	  *++++ d63 ++++*
		 *+++++ tm	++++*	  *++++ tm	++++*
		*/
		for (i = 0; i < 10; i++) {
			RK_U32 *src_uv = (RK_U32 *)(count_uv[i]);
			RK_U32 value = 0;
			if (i == 0) {
				dst_uv = s->counts.uv_mode[2]; //dc
			} else if ( i == 1) {
				dst_uv = s->counts.uv_mode[0]; //h
			}  else if ( i == 2) {
				dst_uv = s->counts.uv_mode[1]; //h
			}  else if ( i == 7) {
				dst_uv = s->counts.uv_mode[8]; //d207
			} else if (i == 8) {
				dst_uv = s->counts.uv_mode[7]; //d63
			} else {
				dst_uv = s->counts.uv_mode[i];
			}
			for (j = 0; j < 10; j++) {
				value = src_uv[j];
				if (j == 0)
					dst_uv[2] = value;
				else if (j == 1)
					dst_uv[0] = value;
				else if (j == 2)
					dst_uv[1] = value;
				else if (j == 7)
					dst_uv[8] = value;
				else if (j == 8)
					dst_uv[7] = value;
				else
					dst_uv[j] = value;
			}

		}
	}
}




static void rkvdec_vp9_parser_update(AVCodecContext* avctx)
{
    RKVDECVP9Context * const reg_cxt = ff_rkvdec_get_context(avctx);
	LPRKVDEC_PicParams_VP9 s = reg_cxt->pic_param;
	VP9Context * const h = avctx->priv_data;

	 av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_parser_update \n");
    //update count from hardware
    {
        memcpy((void *)&h->counts, (void *)&s->counts, sizeof(h->counts));
		for(int i = 0; i < 2; i++){
			memcpy((void *)h->counts.mv_comp[i].sign, (void *)s->counts.sign[i], sizeof(h->counts.mv_comp[i].sign));
			memcpy((void *)h->counts.mv_comp[i].classes, (void *)s->counts.classes[i], sizeof(h->counts.mv_comp[i].classes));
			memcpy((void *)h->counts.mv_comp[i].class0, (void *)s->counts.class0[i], sizeof(h->counts.mv_comp[i].class0));
			memcpy((void *)h->counts.mv_comp[i].bits, (void *)s->counts.bits[i], sizeof(h->counts.mv_comp[i].bits));
			memcpy((void *)h->counts.mv_comp[i].class0_fp, (void *)s->counts.class0_fp[i], sizeof(h->counts.mv_comp[i].class0_fp));
			memcpy((void *)h->counts.mv_comp[i].fp, (void *)s->counts.fp[i], sizeof(h->counts.mv_comp[i].fp));
			memcpy((void *)h->counts.mv_comp[i].class0_hp, (void *)s->counts.class0_hp[i], sizeof(h->counts.mv_comp[i].class0_hp));
			memcpy((void *)h->counts.mv_comp[i].hp, (void *)s->counts.hp[i], sizeof(h->counts.mv_comp[i].hp));
			
		}
        if (h->s.h.refreshctx && !h->s.h.parallelmode) {
            rkvdec_vp9_inv_count_data(avctx);
            ff_vp9_adapt_probs(h);

        }
    }
}


static void rkvdec_vp9_update_counts(AVCodecContext* avctx)
{
    RKVDECVP9Context * const reg_cxt = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP9 s = reg_cxt->pic_param;
    RK_S32 i, j, m, n, k;
    RK_U32 *eob_coef;
    RK_S32 ref_type;
	
#ifdef dump_count
    RK_U32 count_length;
	char filename[30] = "/home/linaro/lyh/count";
#endif

    RK_U32 com_len = 0;
    RK_U8 *counts_ptr = reg_cxt->count_base->data[0];
    if (NULL == counts_ptr) {
        av_log(avctx, AV_LOG_ERROR, "counts_ptr get ptr error");
        return;
    }

#ifdef dump_count
	if (fp_vp9_count != NULL) {
        fclose(fp_vp9_count);
    }
	sprintf(&filename[22], "%d.bin", fp_vp9_count);
	fp_vp9_count = fopen(filename, "wb");
    if (!(s->frame_type == 0 || s->intra_only)) //inter
        count_length = (213 * 64 + 576 * 5 * 32) / 8;
    else //intra
        count_length = (49 * 64 + 288 * 5 * 32) / 8;
    fwrite(counts_ptr, 1, count_length, fp_vp9_count);
    fflush(fp_vp9_count);
	fp_vp9_count++;
#endif

    if ((s->frame_type == 0 || s->intra_only)) {
        com_len = sizeof(s->counts.partition) + sizeof(s->counts.skip) + sizeof(s->counts.intra)
                  + sizeof(s->counts.tx32p) + sizeof(s->counts.tx16p) + sizeof(s->counts.tx8p);
    } else {
        com_len = sizeof(s->counts) - sizeof(s->counts.coef) - sizeof(s->counts.eob);
    }
    eob_coef = (RK_U32 *)(counts_ptr + com_len);
    memcpy(&s->counts, counts_ptr, com_len);
    ref_type = (!(s->frame_type == 0 || s->intra_only)) ? 2 : 1;
    if (ref_type == 1) {
        memset(s->counts.eob, 0, sizeof(s->counts.eob));
        memset(s->counts.coef, 0, sizeof(s->counts.coef));
    }
    for (i = 0; i < ref_type; i++) {
        for (j = 0; j < 4; j++) {
            for (m = 0; m < 2; m++) {
                for (n = 0; n < 6; n++) {
                    for (k = 0; k < 6; k++) {
                        s->counts.eob[j][m][i][n][k][0] = eob_coef[1];
                        s->counts.eob[j][m][i][n][k][1] = eob_coef[0] - eob_coef[1]; //ffmpeg need do  branch_ct[UNCONSTRAINED_NODES][2] =  { neob, eob_counts[i][j][k][l] - neob },
                        s->counts.coef[j][m][i][n][k][0] = eob_coef[2];
                        s->counts.coef[j][m][i][n][k][1] = eob_coef[3];
                        s->counts.coef[j][m][i][n][k][2] = eob_coef[4];
                        eob_coef += 5;
                    }
                }
            }
        }
    }
}



static int rkvdec_vp9_regs_gen_ls_info(AVCodecContext* avctx)
{
	RKVDECVP9Context * const reg_cxt = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP9 pic_param = reg_cxt->pic_param;
	int i;
	
	reg_cxt->ls_info.abs_delta_last = pic_param->stVP9Segments.abs_delta;
    for (i = 0 ; i < 4; i ++) {
        reg_cxt->ls_info.last_ref_deltas[i] = pic_param->ref_deltas[i];
    }

    for (i = 0 ; i < 2; i ++) {
        reg_cxt->ls_info.last_mode_deltas[i] = pic_param->mode_deltas[i];
    }

    for (i = 0; i < 8; i++) {
        reg_cxt->ls_info.feature_data[i][0] = pic_param->stVP9Segments.feature_data[i][0];
        reg_cxt->ls_info.feature_data[i][1] = pic_param->stVP9Segments.feature_data[i][1];
        reg_cxt->ls_info.feature_data[i][2] = pic_param->stVP9Segments.feature_data[i][2];
        reg_cxt->ls_info.feature_data[i][3] = pic_param->stVP9Segments.feature_data[i][3];
        reg_cxt->ls_info.feature_mask[i]  = pic_param->stVP9Segments.feature_mask[i];
    }
    reg_cxt->ls_info.segmentation_enable_flag_last = pic_param->stVP9Segments.enabled;
    reg_cxt->ls_info.last_show_frame = pic_param->show_frame;
    reg_cxt->ls_info.last_width = pic_param->width;
    reg_cxt->ls_info.last_height = pic_param->height;
    reg_cxt->ls_info.last_intra_only = (!pic_param->frame_type || pic_param->intra_only);

	return 0;
}


static int rkvdec_vp9_regs_gen_probe(AVCodecContext* avctx)
{
    RKVDECVP9Context * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP9 pic_param = ctx->pic_param;
    RK_S32 i, j, k, m, n;
    RK_S32 fifo_len = 304;
	PutBitContext64 bp;

	RK_S32 intraFlag;
	RK_U8 partition_probs[PARTITION_CONTEXTS][PARTITION_TYPES - 1];
    RK_U8 uv_mode_prob[INTRA_MODES][INTRA_MODES - 1];
	void *probe_ptr = ctx->probe_base->data[0];
    void *probe_packet = av_mallocz((fifo_len+1)*8);
	memset(probe_ptr, 0, fifo_len * 8);
	init_put_bits_a64(&bp, probe_packet, fifo_len);
    intraFlag = (!pic_param->frame_type || pic_param->intra_only);
	av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_regs_gen_probe intraFlag = %d\n", intraFlag);


    if (intraFlag) {
        memcpy(partition_probs, vp9_kf_partition_probs, sizeof(partition_probs));
        memcpy(uv_mode_prob, vp9_kf_uv_mode_prob, sizeof(uv_mode_prob));
    } else {
        memcpy(partition_probs, pic_param->prob.partition, sizeof(partition_probs));
        memcpy(uv_mode_prob, pic_param->prob.uv_mode, sizeof(uv_mode_prob));
    }

    //sb info  5 x 128 bit
    for (i = 0; i < PARTITION_CONTEXTS; i++) //kf_partition_prob
        for (j = 0; j < PARTITION_TYPES - 1; j++)
            put_bits_a64(&bp, 8, partition_probs[i][j]); //48

    for (i = 0; i < PREDICTION_PROBS; i++){ //Segment_id_pred_prob //3
        put_bits_a64(&bp, 8, pic_param->stVP9Segments.pred_probs[i]);
    }

    for (i = 0; i < SEG_TREE_PROBS; i++){ //Segment_id_probs
        put_bits_a64(&bp, 8, pic_param->stVP9Segments.tree_probs[i]); //7
    }

    for (i = 0; i < SKIP_CONTEXTS; i++){ //Skip_flag_probs //3
        put_bits_a64(&bp, 8, pic_param->prob.skip[i]);
    }
	
    for (i = 0; i < TX_SIZE_CONTEXTS; i++)//Tx_size_probs //6
        for (j = 0; j < TX_SIZES - 1; j++){
            put_bits_a64(&bp, 8, pic_param->prob.tx32p[i][j]);
        }

    for (i = 0; i < TX_SIZE_CONTEXTS; i++) //Tx_size_probs //4
        for (j = 0; j < TX_SIZES - 2; j++){
            put_bits_a64(&bp, 8,  pic_param->prob.tx16p[i][j]);
        }

    for (i = 0; i < TX_SIZE_CONTEXTS; i++){ //Tx_size_probs //2
        put_bits_a64(&bp, 8, pic_param->prob.tx8p[i]);
    }

    for (i = 0; i < INTRA_INTER_CONTEXTS; i++){ //Tx_size_probs //4
        put_bits_a64(&bp, 8, pic_param->prob.intra[i]);
    }

    put_align_a64(&bp, 128, 0);                     // total 80
    if (intraFlag) { //intra probs
        //intra only //149 x 128 bit ,aligned to 152 x 128 bit
        //coeff releated prob   64 x 128 bit
        for (i = 0; i < TX_SIZES; i++)
            for (j = 0; j < PLANE_TYPES; j++) {
                RK_S32 byte_count = 0;
                for (k = 0; k < COEF_BANDS; k++) {
                    for (m = 0; m < COEFF_CONTEXTS; m++)
                        for (n = 0; n < UNCONSTRAINED_NODES; n++) {
                            put_bits_a64(&bp, 8, pic_param->prob.coef[i][j][0][k][m][n]);

                            byte_count++;
                            if (byte_count == 27) {
                                put_align_a64(&bp, 128, 0);
                                byte_count = 0;
                            }
                        }
                }
                put_align_a64(&bp, 128, 0);
            }
        //intra mode prob  80 x 128 bit
        for (i = 0; i < INTRA_MODES; i++) { //vp9_kf_y_mode_prob
            RK_S32 byte_count = 0;
            for (j = 0; j < INTRA_MODES; j++)
                for (k = 0; k < INTRA_MODES - 1; k++) {
                    put_bits_a64(&bp, 8, vp9_kf_y_mode_prob[i][j][k]);
                    byte_count++;
                    if (byte_count == 27) {
                        byte_count = 0;
                        put_align_a64(&bp, 128, 0);
                    }

                }
            if (i < 4) {
                for (m = 0; m < (i < 3 ? 23 : 21); m++)
                    put_bits_a64(&bp, 8, ((RK_U8 *)(&vp9_kf_uv_mode_prob[0][0]))[i * 23 + m]);
                for (; m < 23; m++)
                    put_bits_a64(&bp, 8, 0);
            } else {
                for (m = 0; m < 23; m++)
                    put_bits_a64(&bp, 8, 0);
            }
            put_align_a64(&bp, 128, 0);
        }
        //align to 152 x 128 bit
        for (i = 0; i < INTER_PROB_SIZE_ALIGN_TO_128 - INTRA_PROB_SIZE_ALIGN_TO_128; i++) { //aligned to 153 x 256 bit
            put_bits_a64(&bp, 8, 0);
            put_align_a64(&bp, 128, 0);
        }
    } else {
        //inter probs
        //151 x 128 bit ,aligned to 152 x 128 bit
        //inter only

        //intra_y_mode & inter_block info   6 x 128 bit
        for (i = 0; i < BLOCK_SIZE_GROUPS; i++) //intra_y_mode         36
            for (j = 0; j < INTRA_MODES - 1; j++)
                put_bits_a64(&bp, 8, pic_param->prob.y_mode[i][j]);

        for (i = 0; i < COMP_INTER_CONTEXTS; i++) //reference_mode prob 5
            put_bits_a64(&bp, 8, pic_param->prob.comp[i]);

        for (i = 0; i < REF_CONTEXTS; i++) //comp ref bit 5
            put_bits_a64(&bp, 8, pic_param->prob.comp_ref[i]);

        for (i = 0; i < REF_CONTEXTS; i++) //single ref bit 10
            for (j = 0; j < 2; j++)
                put_bits_a64(&bp, 8, pic_param->prob.single_ref[i][j]);

        for (i = 0; i < INTER_MODE_CONTEXTS; i++) //mv mode bit  21
            for (j = 0; j < INTER_MODES - 1; j++)
                put_bits_a64(&bp, 8, pic_param->prob.mv_mode[i][j]);


        for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; i++) //comp ref bit 8
            for (j = 0; j < SWITCHABLE_FILTERS - 1; j++)
                put_bits_a64(&bp, 8, pic_param->prob.filter[i][j]);

        put_align_a64(&bp, 128, 0);

        //128 x 128bit
        //coeff releated
        for (i = 0; i < TX_SIZES; i++)
            for (j = 0; j < PLANE_TYPES; j++) {
                RK_S32 byte_count = 0;
                for (k = 0; k < COEF_BANDS; k++) {
                    for (m = 0; m < COEFF_CONTEXTS; m++)
                        for (n = 0; n < UNCONSTRAINED_NODES; n++) {
                            put_bits_a64(&bp, 8, pic_param->prob.coef[i][j][0][k][m][n]);
                            byte_count++;
                            if (byte_count == 27) {
                                put_align_a64(&bp, 128, 0);
                                byte_count = 0;
                            }
                        }
                }
                put_align_a64(&bp, 128, 0);
            }

			
        for (i = 0; i < TX_SIZES; i++)
            for (j = 0; j < PLANE_TYPES; j++) {
                RK_S32 byte_count = 0;
                for (k = 0; k < COEF_BANDS; k++) {
                    for (m = 0; m < COEFF_CONTEXTS; m++) {
                        for (n = 0; n < UNCONSTRAINED_NODES; n++) {
                            put_bits_a64(&bp, 8, pic_param->prob.coef[i][j][1][k][m][n]);
                            byte_count++;
                            if (byte_count == 27) {
                                put_align_a64(&bp, 128, 0);
                                byte_count = 0;
                            }
                        }

                    }
                }
                put_align_a64(&bp, 128, 0);
            }

        //intra uv mode 6 x 128
        for (i = 0; i < 3; i++) //intra_uv_mode
            for (j = 0; j < INTRA_MODES - 1; j++)
                put_bits_a64(&bp, 8, uv_mode_prob[i][j]);
        put_align_a64(&bp, 128, 0);

        for (; i < 6; i++) //intra_uv_mode
            for (j = 0; j < INTRA_MODES - 1; j++)
                put_bits_a64(&bp, 8, uv_mode_prob[i][j]);
        put_align_a64(&bp, 128, 0);

        for (; i < 9; i++) //intra_uv_mode
            for (j = 0; j < INTRA_MODES - 1; j++)
                put_bits_a64(&bp, 8, uv_mode_prob[i][j]);
        put_align_a64(&bp, 128, 0);
        for (; i < INTRA_MODES; i++) //intra_uv_mode
            for (j = 0; j < INTRA_MODES - 1; j++)
                put_bits_a64(&bp, 8, uv_mode_prob[i][j]);

        put_align_a64(&bp, 128, 0);
        put_bits_a64(&bp, 8, 0);
        put_align_a64(&bp, 128, 0);

        //mv releated 6 x 128
        for (i = 0; i < MV_JOINTS - 1; i++) //mv_joint_type
            put_bits_a64(&bp, 8, pic_param->prob.mv_joint[i]);

        for (i = 0; i < 2; i++) { //sign bit
            put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].sign);
        }
        for (i = 0; i < 2; i++) { //classes bit
            for (j = 0; j < MV_CLASSES - 1; j++)
                put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].classes[j]);
        }
        for (i = 0; i < 2; i++) { //classe0 bit
            put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].class0);
        }
        for (i = 0; i < 2; i++) { // bits
            for (j = 0; j < MV_OFFSET_BITS; j++)
                put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].bits[j]);
        }
        for (i = 0; i < 2; i++) { //class0_fp bit
            for (j = 0; j < CLASS0_SIZE; j++)
                for (k = 0; k < MV_FP_SIZE - 1; k++)
                    put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].class0_fp[j][k]);
        }
        for (i = 0; i < 2; i++) { //comp ref bit
            for (j = 0; j < MV_FP_SIZE - 1; j++)
                put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].fp[j]);
        }
        for (i = 0; i < 2; i++) { //class0_hp bit

            put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].class0_hp);
        }
        for (i = 0; i < 2; i++) { //hp bit
            put_bits_a64(&bp, 8, pic_param->prob.mv_comp[i].hp);
        }
        put_align_a64(&bp, 128, 0);
    }

    memcpy(probe_ptr, probe_packet, 304 * 8);
		
#ifdef dump_probe
		char filename[30] = "/home/linaro/lyh/probe";
		if (fp_vp9_probe != NULL) {
        	fclose(fp_vp9_probe);
    	}
		sprintf(&filename[22], "%d.bin", num_probe);
		fp_vp9_probe = fopen(filename, "wb");
		if(fp_vp9_probe){
		    if (intraFlag) {
        		fwrite(probe_packet, 1, 302 * 8, fp_vp9_probe);
    		} else {
        		fwrite(probe_packet, 1, 304 * 8, fp_vp9_probe);
    		}
			fflush(fp_vp9_probe);
		}
		num_probe++;
#endif
	if (probe_packet != NULL)
		av_free(probe_packet);
    return 0;
}

static int rkvdec_vp9_regs_gen_count(AVCodecContext* avctx)
{
    return 0;
}

static int rkvdec_vp9_regs_gen_segid_cur(AVCodecContext* avctx)
{
    return 0;
}

static int rkvdec_vp9_regs_gen_segid_last(AVCodecContext* avctx)
{
    return 0;
}


static RK_U32 vp9_ver_align(RK_U32 val)
{
    return ALIGN(val, 64);
}

static RK_U32 vp9_hor_align(RK_U32 val)
{
    return ALIGN(val, 64);   //128
}


static int rkvdec_vp9_regs_gen_reg(AVCodecContext *avctx)
{
    RKVDECVP9Context * const reg_cxt = ff_rkvdec_get_context(avctx);    
    LPRKVDEC_PicParams_VP9 pic_param = reg_cxt->pic_param;
    LPRKVDEC_VP9_Regs vp9_hw_regs = reg_cxt->hw_regs;

	RK_S32	 i;
	RK_U8	 bit_depth = 0;
	RK_U32	 pic_h[3] = { 0 };
	RK_U32	 ref_frame_width_y;
	RK_U32	 ref_frame_height_y;
	RK_S32	 stream_len = 0, aglin_offset = 0;
	RK_U32	 y_hor_virstride, uv_hor_virstride, y_virstride, uv_virstride, yuv_virstride;
	RK_U8	*bitstream = NULL;
	RK_U32 sw_y_hor_virstride;
	RK_U32 sw_uv_hor_virstride;
	RK_U32 sw_y_virstride;
	RK_U32 sw_uv_virstride;
	RK_U32 sw_yuv_virstride ;
	RK_U8  ref_idx = 0;
	RK_U32 *reg_ref_base = 0;
	RK_S32 intraFlag = 0;

	av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_regs_gen_reg\n");
    memset(reg_cxt->hw_regs, 0, sizeof(struct _VP9dRkvRegs_t));

    intraFlag = (!pic_param->frame_type || pic_param->intra_only);
    stream_len = reg_cxt->stream_data->pkt_size;

    vp9_hw_regs->swreg2_sysctrl.sw_dec_mode = 2; //set as vp9 dec
    //vp9_hw_regs->swreg5_stream_len = ((stream_len + 15) & (~15)) + 0x80;
	vp9_hw_regs->swreg5_stream_len = stream_len;

    bitstream = reg_cxt->stream_data->data[0];
    aglin_offset = vp9_hw_regs->swreg5_stream_len - stream_len;
    if (aglin_offset > 0) {
        memset((void *)(bitstream + stream_len), 0, aglin_offset);
    }

    //--- caculate the yuv_frame_size and mv_size
    bit_depth = pic_param->BitDepthMinus8Luma + 8;
    pic_h[0] = vp9_ver_align(pic_param->height); //p_cm->height;
    pic_h[1] = vp9_ver_align(pic_param->height) / 2; //(p_cm->height + 1) / 2;
    pic_h[2] = pic_h[1];

    sw_y_hor_virstride = (vp9_hor_align((pic_param->width * bit_depth) >> 3) >> 4);
    sw_uv_hor_virstride = (vp9_hor_align((pic_param->width * bit_depth) >> 3) >> 4);
    sw_y_virstride = pic_h[0] * sw_y_hor_virstride;
    sw_uv_virstride = pic_h[1] * sw_uv_hor_virstride;
    sw_yuv_virstride = sw_y_virstride + sw_uv_virstride;

    vp9_hw_regs->swreg3_picpar.sw_y_hor_virstride = sw_y_hor_virstride;
    vp9_hw_regs->swreg3_picpar.sw_uv_hor_virstride = sw_uv_hor_virstride;
    vp9_hw_regs->swreg8_y_virstride.sw_y_virstride = sw_y_virstride;
    vp9_hw_regs->swreg9_yuv_virstride.sw_yuv_virstride = sw_yuv_virstride;

    if (!pic_param->intra_only && pic_param->frame_type && !pic_param->error_resilient_mode) {
        reg_cxt->pre_mv_base_addr = reg_cxt->mv_base_addr;
    }

    if (!(pic_param->stVP9Segments.enabled && !pic_param->stVP9Segments.update_map)) {
        if (!pic_param->intra_only && pic_param->frame_type && !pic_param->error_resilient_mode) {
            if (reg_cxt->last_segid_flag) {
                reg_cxt->last_segid_flag = 0;
            } else {
                reg_cxt->last_segid_flag = 1;
            }
        }
    }
    vp9_hw_regs->swreg7_decout_base =  pic_param->CurrPic.Index7Bits;
    vp9_hw_regs->swreg4_strm_rlc_base = ff_rkvdec_get_fd(reg_cxt->stream_data);

    vp9_hw_regs->swreg6_cabactbl_prob_base = ff_rkvdec_get_fd(reg_cxt->probe_base);
    vp9_hw_regs->swreg14_vp9_count_base  = ff_rkvdec_get_fd(reg_cxt->count_base);

    if (reg_cxt->last_segid_flag) {
        vp9_hw_regs->swreg15_vp9_segidlast_base = ff_rkvdec_get_fd(reg_cxt->segid_last_base);
        vp9_hw_regs->swreg16_vp9_segidcur_base = ff_rkvdec_get_fd(reg_cxt->segid_cur_base);
    } else {
        vp9_hw_regs->swreg15_vp9_segidlast_base = ff_rkvdec_get_fd(reg_cxt->segid_cur_base);
        vp9_hw_regs->swreg16_vp9_segidcur_base = ff_rkvdec_get_fd(reg_cxt->segid_last_base);
    }

    reg_cxt->mv_base_addr = vp9_hw_regs->swreg7_decout_base | ((sw_yuv_virstride << 4) << 6);
    if (reg_cxt->pre_mv_base_addr < 0) {
        reg_cxt->pre_mv_base_addr = reg_cxt->mv_base_addr;
    }
    vp9_hw_regs->swreg52_vp9_refcolmv_base = reg_cxt->pre_mv_base_addr;
    vp9_hw_regs->swreg10_vp9_cprheader_offset.sw_vp9_cprheader_offset = 0; //no use now.
    reg_ref_base = &vp9_hw_regs->swreg11_vp9_referlast_base;
    for (i = 0; i < 3; i++) {
        ref_idx = pic_param->frame_refs[i].Index7Bits;
        ref_frame_width_y = pic_param->ref_frame_coded_width[ref_idx];
        ref_frame_height_y = pic_param->ref_frame_coded_height[ref_idx];
        pic_h[0] = vp9_ver_align(ref_frame_height_y);
        pic_h[1] = vp9_ver_align(ref_frame_height_y) / 2;
        y_hor_virstride = (vp9_hor_align((ref_frame_width_y * bit_depth) >> 3) >> 4);
        uv_hor_virstride = (vp9_hor_align((ref_frame_width_y * bit_depth) >> 3) >> 4);
        y_virstride = y_hor_virstride * pic_h[0];
        uv_virstride = uv_hor_virstride * pic_h[1];
        yuv_virstride = y_virstride + uv_virstride;
        
        if (pic_param->ref_frame_map[ref_idx].Index7Bits < 0x7f) {
            switch (i) {
            case 0: {

                vp9_hw_regs->swreg17_vp9_frame_size_last.sw_framewidth_last = ref_frame_width_y;
                vp9_hw_regs->swreg17_vp9_frame_size_last.sw_frameheight_last = ref_frame_height_y;
                vp9_hw_regs->swreg37_vp9_lastf_hor_virstride.sw_vp9_lastfy_hor_virstride = y_hor_virstride;
                vp9_hw_regs->swreg37_vp9_lastf_hor_virstride.sw_vp9_lastfuv_hor_virstride = uv_hor_virstride;
                vp9_hw_regs->swreg48_vp9_last_ystride.sw_vp9_lastfy_virstride = y_virstride;
                vp9_hw_regs->swreg51_vp9_lastref_yuvstride.sw_vp9_lastref_yuv_virstride = yuv_virstride;
                break;
            }
            case 1: {
                vp9_hw_regs->swreg18_vp9_frame_size_golden.sw_framewidth_golden = ref_frame_width_y;
                vp9_hw_regs->swreg18_vp9_frame_size_golden.sw_frameheight_golden = ref_frame_height_y;
                vp9_hw_regs->swreg38_vp9_goldenf_hor_virstride.sw_vp9_goldenfy_hor_virstride = y_hor_virstride;
                vp9_hw_regs->swreg38_vp9_goldenf_hor_virstride.sw_vp9_goldenuv_hor_virstride = uv_hor_virstride;
                vp9_hw_regs->swreg49_vp9_golden_ystride.sw_vp9_goldeny_virstride = y_virstride;
                break;
            }
            case 2: {
                vp9_hw_regs->swreg19_vp9_frame_size_altref.sw_framewidth_alfter = ref_frame_width_y;
                vp9_hw_regs->swreg19_vp9_frame_size_altref.sw_frameheight_alfter = ref_frame_height_y;
                vp9_hw_regs->swreg39_vp9_altreff_hor_virstride.sw_vp9_altreffy_hor_virstride = y_hor_virstride;
                vp9_hw_regs->swreg39_vp9_altreff_hor_virstride.sw_vp9_altreffuv_hor_virstride = uv_hor_virstride;
                vp9_hw_regs->swreg50_vp9_altrefy_ystride.sw_vp9_altrefy_virstride = y_virstride;
                break;
            }
            default:
                break;

            }

            if (pic_param->ref_frame_map[ref_idx].Index7Bits != 0) {
                reg_ref_base[i] = pic_param->ref_frame_map[ref_idx].Index7Bits;
            } else {
                reg_ref_base[i] = vp9_hw_regs->swreg7_decout_base; //set
            }

        } else {
            reg_ref_base[i] = vp9_hw_regs->swreg7_decout_base; //set
        }
    }

    for (i = 0; i < 8; i++) {
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_frame_qp_delta_en              = (reg_cxt->ls_info.feature_mask[i]) & 0x1;
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_frame_qp_delta                 = reg_cxt->ls_info.feature_data[i][0];
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_frame_loopfitler_value_en      = (reg_cxt->ls_info.feature_mask[i] >> 1) & 0x1;
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_frame_loopfilter_value         = reg_cxt->ls_info.feature_data[i][1];
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_referinfo_en                   = (reg_cxt->ls_info.feature_mask[i] >> 2) & 0x1;
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_referinfo                      = reg_cxt->ls_info.feature_data[i][2];
        vp9_hw_regs->swreg20_27_vp9_segid_grp[i].sw_vp9segid_frame_skip_en                  = (reg_cxt->ls_info.feature_mask[i] >> 3) & 0x1;
    }


    vp9_hw_regs->swreg20_27_vp9_segid_grp[0].sw_vp9segid_abs_delta                              = reg_cxt->ls_info.abs_delta_last;

    vp9_hw_regs->swreg28_vp9_cprheader_config.sw_vp9_tx_mode                                    = pic_param->txmode;

    vp9_hw_regs->swreg28_vp9_cprheader_config.sw_vp9_frame_reference_mode                   = pic_param->refmode;

    vp9_hw_regs->swreg32_vp9_ref_deltas_lastframe.sw_vp9_ref_deltas_lastframe               = 0;

    if (!intraFlag) {
        for (i = 0; i < 4; i++)
            vp9_hw_regs->swreg32_vp9_ref_deltas_lastframe.sw_vp9_ref_deltas_lastframe           |= (reg_cxt->ls_info.last_ref_deltas[i] & 0x7f) << (7 * i);

        for (i = 0; i < 2; i++)
            vp9_hw_regs->swreg33_vp9_info_lastframe.sw_vp9_mode_deltas_lastframe                |= (reg_cxt->ls_info.last_mode_deltas[i] & 0x7f) << (7 * i);


    } else {
        reg_cxt->ls_info.segmentation_enable_flag_last = 0;
        reg_cxt->ls_info.last_intra_only = 1;
    }

    vp9_hw_regs->swreg33_vp9_info_lastframe.sw_vp9_mode_deltas_lastframe                        = 0;
    vp9_hw_regs->swreg33_vp9_info_lastframe.sw_segmentation_enable_lstframe                  = reg_cxt->ls_info.segmentation_enable_flag_last;
    vp9_hw_regs->swreg33_vp9_info_lastframe.sw_vp9_last_show_frame                          = reg_cxt->ls_info.last_show_frame;
    vp9_hw_regs->swreg33_vp9_info_lastframe.sw_vp9_last_intra_only                          = reg_cxt->ls_info.last_intra_only;
    vp9_hw_regs->swreg33_vp9_info_lastframe.sw_vp9_last_widthheight_eqcur                   = (pic_param->width == reg_cxt->ls_info.last_width) && (pic_param->height == reg_cxt->ls_info.last_height);
    vp9_hw_regs->swreg36_vp9_lasttile_size.sw_vp9_lasttile_size                             =  stream_len - pic_param->first_partition_size;


    if (!intraFlag) {
        vp9_hw_regs->swreg29_vp9_lref_scale.sw_vp9_lref_hor_scale = pic_param->mvscale[0][0];
        vp9_hw_regs->swreg29_vp9_lref_scale.sw_vp9_lref_ver_scale = pic_param->mvscale[0][1];
        vp9_hw_regs->swreg30_vp9_gref_scale.sw_vp9_gref_hor_scale = pic_param->mvscale[1][0];
        vp9_hw_regs->swreg30_vp9_gref_scale.sw_vp9_gref_ver_scale = pic_param->mvscale[1][1];
        vp9_hw_regs->swreg31_vp9_aref_scale.sw_vp9_aref_hor_scale = pic_param->mvscale[2][0];
        vp9_hw_regs->swreg31_vp9_aref_scale.sw_vp9_aref_ver_scale = pic_param->mvscale[2][1];
        // vp9_hw_regs.swreg33_vp9_info_lastframe.sw_vp9_color_space_lastkeyframe = p_cm->color_space_last;
    }


    //reuse reg64, and it will be written by hardware to show performance.
    vp9_hw_regs->swreg64_performance_cycle.sw_performance_cycle = 0;
    vp9_hw_regs->swreg64_performance_cycle.sw_performance_cycle |= pic_param->width;
    vp9_hw_regs->swreg64_performance_cycle.sw_performance_cycle |= pic_param->height << 16;

    vp9_hw_regs->swreg1_int.sw_dec_e         = 1;
    vp9_hw_regs->swreg1_int.sw_dec_timeout_e = 1;

    return 0;

}


 /** Initialize and start decoding a frame with RKVDEC. */
static int rkvdec_vp9_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    RKVDECVP9Context * const ctx = ff_rkvdec_get_context(avctx);
    fill_picture_parameters(avctx, ctx->pic_param);
    ctx->stream_data->pkt_size = 0;
    return 0;
}

/** End a hardware decoding based frame. */
static int rkvdec_vp9_end_frame(AVCodecContext *avctx)
{
    RKVDECVP9Context * const ctx = ff_rkvdec_get_context(avctx);
    LPRKVDEC_PicParams_VP9 	 pic_param = ctx->pic_param;
    RKVDECVP9HwReq req;
	unsigned char *preg;
    int ret;
    int i;

    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_end_frame\n");
    if (ctx->stream_data->pkt_size <= 0) {
        av_log(avctx, AV_LOG_WARNING, "RK_VP9_DEC: rkvdec_vp9_end_frame not valid stream\n");
        return 0;
    }
    rkvdec_vp9_regs_gen_probe(avctx);
    rkvdec_vp9_regs_gen_count(avctx);
    rkvdec_vp9_regs_gen_segid_cur(avctx);
    rkvdec_vp9_regs_gen_segid_last(avctx);
    rkvdec_vp9_regs_gen_reg(avctx);


    preg = (unsigned char*)ctx->hw_regs;
    for (i = 0; i < 78 ; i++) {
        av_log(avctx, AV_LOG_DEBUG, "RK_H264H_DEC: set  regs[%02d]=%08X\n", i, *((unsigned int*)preg));
        preg += 4;
    }

    req.req = (unsigned int*)ctx->hw_regs;
    req.size = 78 * sizeof(unsigned int);

    ret = ioctl(ctx->vpu_socket, VPU_IOC_SET_REG, &req);
    if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_SET_REG failed ret %d\n", ret);

    ret = ioctl(ctx->vpu_socket, VPU_IOC_GET_REG, &req);
	if (ret)
        av_log(avctx, AV_LOG_ERROR, "ioctl VPU_IOC_GET_REG failed ret %d\n", ret);
	
    preg = (unsigned char*)ctx->hw_regs;
    for (i = 0; i < 78 ; i++) {
        av_log(avctx, AV_LOG_DEBUG, "RK_H264H_DEC: get  regs[%02d]=%08X\n", i, *((unsigned int*)preg));
        preg += 4;
    }

	rkvdec_vp9_regs_gen_ls_info(avctx);
	av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: pic_param->refresh_frame_context = %d \n",pic_param->refresh_frame_context);
	av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: pic_param->parallelmode = %d \n",pic_param->parallelmode);
	if (pic_param->refresh_frame_context && !pic_param->parallelmode) {
            rkvdec_vp9_update_counts(avctx);
            rkvdec_vp9_parser_update(avctx);
    }
    return 0;
}


/** Decode the given vp9 slice with RKVDEC. */
static int rkvdec_vp9_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{    
    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_decode_slice size:%d\n", size);
    fill_stream_data(avctx, buffer, size);
    return 0;
}

#define AV_GET_BUFFER_FLAG_CONTAIN_MV (1 << 1)

static int rkvdec_vp9_alloc_frame(AVCodecContext *avctx, AVFrame *frame)
{
	int ret;
	avctx->coded_width = vp9_hor_align(frame->width);
	avctx->coded_height = vp9_hor_align(frame->height);
	frame->width = vp9_hor_align(frame->width);
	frame->height = vp9_ver_align(frame->height);

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


static int rkvdec_vp9_context_init(AVCodecContext *avctx)
{
    RKVDECVP9Context * const ctx = ff_rkvdec_get_context(avctx);
    int ret;
    
    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_context_init\n");

	ctx->mv_base_addr = -1;
    ctx->pre_mv_base_addr = -1;
	ctx->last_segid_flag = 1;
	
    ctx->allocator = &allocator_drm;
    ret = ctx->allocator->open(&ctx->allocator_ctx, 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "failed to open allocator.");
        return -1;
    }

    ctx->hw_regs = av_mallocz(sizeof(RKVDEC_VP9_Regs));
    ctx->pic_param = av_mallocz(sizeof(RKVDEC_PicParams_VP9));

    ctx->probe_base = av_frame_alloc();
    ctx->probe_base->linesize[0] = RKVDECVP9_PROBE_SIZE;   
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->probe_base);
    
    ctx->count_base = av_frame_alloc();
    ctx->count_base->linesize[0] = RKVDECVP9_COUNT_SIZE;   
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->count_base); 

    ctx->segid_cur_base = av_frame_alloc();
    ctx->segid_cur_base->linesize[0] = RKVDECVP9_MAX_SEGMAP_SIZE;   
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->segid_cur_base); 

    ctx->segid_last_base = av_frame_alloc();
    ctx->segid_last_base->linesize[0] = RKVDECVP9_MAX_SEGMAP_SIZE;   
    ctx->allocator->alloc(ctx->allocator_ctx, ctx->segid_last_base);

    ctx->stream_data = av_frame_alloc();
    ctx->stream_data->linesize[0] = RKVDECVP9_DATA_SIZE;
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

static int rkvdec_vp9_context_uninit(AVCodecContext *avctx)
{
    RKVDECVP9Context * const ctx = ff_rkvdec_get_context(avctx);

    av_log(avctx, AV_LOG_INFO, "RK_VP9_DEC: rkvdec_vp9_context_uninit\n");
    ctx->allocator->free(ctx->allocator_ctx, ctx->probe_base);
	ctx->allocator->free(ctx->allocator_ctx, ctx->count_base);
	ctx->allocator->free(ctx->allocator_ctx, ctx->segid_cur_base);
    ctx->allocator->free(ctx->allocator_ctx, ctx->segid_last_base);
	ctx->allocator->free(ctx->allocator_ctx, ctx->stream_data);
    ctx->allocator->close(ctx->allocator_ctx);

    av_free(ctx->probe_base);
    av_free(ctx->count_base);
    av_free(ctx->segid_cur_base);
    av_free(ctx->segid_last_base);
    av_free(ctx->stream_data);

    av_free(ctx->hw_regs);

    if (ctx->vpu_socket > 0) {
        close(ctx->vpu_socket);
        ctx->vpu_socket = -1;
    }
#ifdef  dump_probe
	if (fp_vp9_probe != NULL) {
    	fclose(fp_vp9_probe);
    }
#endif
#ifdef  dump_probe
		if (fp_vp9_count != NULL) {
			fclose(fp_vp9_count);
		}
#endif
    return 0;
}

AVHWAccel ff_vp9_rkvdec_hwaccel = {
    .name                 = "vp9_rkvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_VP9,
    .pix_fmt              = AV_PIX_FMT_NV12,
    .start_frame          = rkvdec_vp9_start_frame,
    .end_frame            = rkvdec_vp9_end_frame,
    .decode_slice         = rkvdec_vp9_decode_slice,
    .alloc_frame		  = rkvdec_vp9_alloc_frame,
    .init                 = rkvdec_vp9_context_init,
    .uninit               = rkvdec_vp9_context_uninit,
    .priv_data_size       = sizeof(RKVDECVP9Context),
    .frame_priv_data_size = sizeof(RKVDEC_FrameData_VP9),
};

