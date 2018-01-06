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

static const char *name_rkvdec = "/dev/rkvdec";

#define VPU_IOC_MAGIC                       'l'

#define VPU_IOC_SET_CLIENT_TYPE             _IOW(VPU_IOC_MAGIC, 1, unsigned long)
#define VPU_IOC_SET_CLIENT_TYPE_U32         _IOW(VPU_IOC_MAGIC, 1, unsigned int)
#define VPU_IOC_GET_HW_FUSE_STATUS          _IOW(VPU_IOC_MAGIC, 2, unsigned long)
#define VPU_IOC_SET_REG                     _IOW(VPU_IOC_MAGIC, 3, unsigned long)
#define VPU_IOC_GET_REG                     _IOW(VPU_IOC_MAGIC, 4, unsigned long)
#define VPU_IOC_PROBE_IOMMU_STATUS          _IOR(VPU_IOC_MAGIC, 5, unsigned long)
#define VPU_IOC_WRITE(nr, size)             _IOC(_IOC_WRITE, VPU_IOC_MAGIC, (nr), (size))


 typedef unsigned int			 RK_U32;
 typedef signed int 			 RK_S32;
 typedef signed short			 RK_S16;
 typedef unsigned char			 RK_U8;
 typedef char			 		 RK_S8;
 typedef unsigned short 		 RK_U16;
 typedef signed long long int	 RK_S64;
 typedef unsigned short      	 USHORT;
 typedef unsigned char       	 UCHAR;
 typedef signed   char      	 CHAR;
 typedef signed   int        	 INT;
 typedef signed   short     	 SHORT;
 typedef unsigned int        	 UINT;
 typedef unsigned long long int  RK_U64;

 

 typedef struct vp9_dec_last_info {
	 RK_S32 abs_delta_last;
	 RK_S8 last_ref_deltas[4];
	 RK_S8 last_mode_deltas[2];
	 RK_U8 segmentation_enable_flag_last;
	 RK_U8 last_show_frame;
	 RK_U8 last_intra_only;
	 RK_U32 last_width;
	 RK_U32 last_height;
	 SHORT feature_data[8][4];
	 UCHAR feature_mask[8];
 } vp9_dec_last_info_t;


 struct _VP9dRkvRegs_t{
    struct {
        RK_U32   minor_ver   : 8;
        RK_U32    level       : 1;
        RK_U32    dec_support : 3;
        RK_U32    profile     : 1;
        RK_U32    reserve0    : 1;
        RK_U32    codec_flag  : 1;
        RK_U32    reserve1    : 1;
        RK_U32    prod_num    : 16;
    } swreg0_id;

    struct {
        RK_U32    sw_dec_e                        : 1;//0
        RK_U32    sw_dec_clkgate_e                : 1; // 1
        RK_U32    reserve0                        : 1;// 2
        RK_U32    sw_timeout_mode                 : 1; // 3
        RK_U32    sw_dec_irq_dis                  : 1;//4    // 4
        RK_U32    sw_dec_timeout_e                : 1; //5
        RK_U32    sw_buf_empty_en                 : 1; // 6
        RK_U32    sw_stmerror_waitdecfifo_empty   : 1; // 7
        RK_U32    sw_dec_irq                      : 1; // 8
        RK_U32    sw_dec_irq_raw                  : 1; // 9
        RK_U32    reserve2                        : 2;
        RK_U32    sw_dec_rdy_sta                  : 1; //12
        RK_U32    sw_dec_bus_sta                  : 1; //13
        RK_U32    sw_dec_error_sta                : 1; // 14
        RK_U32    sw_dec_timeout_sta              : 1; //15
        RK_U32    sw_dec_empty_sta                : 1; // 16
        RK_U32    sw_colmv_ref_error_sta          : 1; // 17
        RK_U32    sw_cabu_end_sta                 : 1; // 18
        RK_U32    sw_h264orvp9_error_mode         : 1; //19
        RK_U32    sw_softrst_en_p                 : 1; //20
        RK_U32    sw_force_softreset_valid        : 1; //21
        RK_U32    sw_softreset_rdy                : 1; // 22
    } swreg1_int;

    struct {
        RK_U32    sw_in_endian                    : 1;
        RK_U32    sw_in_swap32_e                  : 1;
        RK_U32    sw_in_swap64_e                  : 1;
        RK_U32    sw_str_endian                   : 1;
        RK_U32    sw_str_swap32_e                 : 1;
        RK_U32    sw_str_swap64_e                 : 1;
        RK_U32    sw_out_endian                   : 1;
        RK_U32    sw_out_swap32_e                 : 1;
        RK_U32    sw_out_cbcr_swap                : 1;
        RK_U32    reserve0                        : 1;
        RK_U32    sw_rlc_mode_direct_write        : 1;
        RK_U32    sw_rlc_mode                     : 1;
        RK_U32    sw_strm_start_bit               : 7;
        RK_U32    reserve1                        : 1;
        RK_U32    sw_dec_mode                     : 2;
        RK_U32    reserve2                        : 2;
        RK_U32    sw_h264_rps_mode                : 1;
        RK_U32    sw_h264_stream_mode             : 1;
        RK_U32    sw_h264_stream_lastpacket       : 1;
        RK_U32    sw_h264_firstslice_flag         : 1;
        RK_U32    sw_h264_frame_orslice           : 1;
        RK_U32    sw_buspr_slot_disable           : 1;
        RK_U32  reserve3                        : 2;
    } swreg2_sysctrl;

    struct {
        RK_U32    sw_y_hor_virstride              : 9;
        RK_U32    reserve                         : 2;
        RK_U32    sw_slice_num_highbit            : 1;
        RK_U32    sw_uv_hor_virstride             : 9;
        RK_U32    sw_slice_num_lowbits            : 11;
    } swreg3_picpar;

    RK_U32 swreg4_strm_rlc_base;
    RK_U32 swreg5_stream_len;
    RK_U32 swreg6_cabactbl_prob_base;
    RK_U32 swreg7_decout_base;

    struct {
        RK_U32    sw_y_virstride                  : 20;
        RK_U32    reverse0                        : 12;
    } swreg8_y_virstride;

    struct {
        RK_U32    sw_yuv_virstride                : 21;
        RK_U32    reverse                         : 11;
    } swreg9_yuv_virstride;


    //only for vp9
    struct {
        RK_U32    sw_vp9_cprheader_offset         : 16;
        RK_U32    reverse                         : 16;
    } swreg10_vp9_cprheader_offset;

    RK_U32 swreg11_vp9_referlast_base;
    RK_U32 swreg12_vp9_refergolden_base;
    RK_U32 swreg13_vp9_referalfter_base;
    RK_U32 swreg14_vp9_count_base;
    RK_U32 swreg15_vp9_segidlast_base;
    RK_U32 swreg16_vp9_segidcur_base;

    struct {
        RK_U32    sw_framewidth_last              : 16;
        RK_U32    sw_frameheight_last             : 16;
    } swreg17_vp9_frame_size_last;

    struct {
        RK_U32    sw_framewidth_golden            : 16;
        RK_U32    sw_frameheight_golden           : 16;
    } swreg18_vp9_frame_size_golden;


    struct {
        RK_U32    sw_framewidth_alfter            : 16;
        RK_U32    sw_frameheight_alfter           : 16;
    } swreg19_vp9_frame_size_altref;


    struct {
        RK_U32    sw_vp9segid_abs_delta                      : 1; //NOTE: only in reg#20, this bit is valid.
        RK_U32    sw_vp9segid_frame_qp_delta_en              : 1;
        RK_U32    sw_vp9segid_frame_qp_delta                 : 9;
        RK_U32    sw_vp9segid_frame_loopfitler_value_en      : 1;
        RK_U32    sw_vp9segid_frame_loopfilter_value         : 7;
        RK_U32    sw_vp9segid_referinfo_en                   : 1;
        RK_U32    sw_vp9segid_referinfo                      : 2;
        RK_U32    sw_vp9segid_frame_skip_en                  : 1;
        RK_U32    reverse                                    : 9;
    } swreg20_27_vp9_segid_grp[8];


    struct {
        RK_U32    sw_vp9_tx_mode                              : 3;
        RK_U32    sw_vp9_frame_reference_mode                 : 2;
        RK_U32    reserved                                    : 27;
    } swreg28_vp9_cprheader_config;


    struct {
        RK_U32    sw_vp9_lref_hor_scale                       : 16;
        RK_U32    sw_vp9_lref_ver_scale                       : 16;
    } swreg29_vp9_lref_scale;

    struct {
        RK_U32    sw_vp9_gref_hor_scale                       : 16;
        RK_U32    sw_vp9_gref_ver_scale                       : 16;
    } swreg30_vp9_gref_scale;

    struct {
        RK_U32    sw_vp9_aref_hor_scale                       : 16;
        RK_U32    sw_vp9_aref_ver_scale                       : 16;
    } swreg31_vp9_aref_scale;

    struct {
        RK_U32    sw_vp9_ref_deltas_lastframe                 : 28;
        RK_U32    reserve                                     : 4;
    } swreg32_vp9_ref_deltas_lastframe;

    struct {
        RK_U32    sw_vp9_mode_deltas_lastframe                : 14;
        RK_U32    reserve0                                    : 2;
        RK_U32    sw_segmentation_enable_lstframe             : 1;
        RK_U32    sw_vp9_last_show_frame                      : 1;
        RK_U32    sw_vp9_last_intra_only                      : 1;
        RK_U32    sw_vp9_last_widthheight_eqcur               : 1;
        RK_U32    sw_vp9_color_space_lastkeyframe             : 3;
        RK_U32    reserve1                                    : 9;
    } swreg33_vp9_info_lastframe;

    RK_U32 swreg34_vp9_intercmd_base;

    struct {
        RK_U32    sw_vp9_intercmd_num                         : 24;
        RK_U32    reserve                                     : 8;
    } swreg35_vp9_intercmd_num;

    struct {
        RK_U32    sw_vp9_lasttile_size                        : 24;
        RK_U32    reserve                                     : 8;
    } swreg36_vp9_lasttile_size;

    struct {
        RK_U32    sw_vp9_lastfy_hor_virstride                 : 9;
        RK_U32    reserve0                                    : 7;
        RK_U32    sw_vp9_lastfuv_hor_virstride                : 9;
        RK_U32    reserve1                                    : 7;
    } swreg37_vp9_lastf_hor_virstride;

    struct {
        RK_U32    sw_vp9_goldenfy_hor_virstride               : 9;
        RK_U32    reserve0                                    : 7;
        RK_U32    sw_vp9_goldenuv_hor_virstride               : 9;
        RK_U32    reserve1                                    : 7;
    } swreg38_vp9_goldenf_hor_virstride;

    struct {
        RK_U32    sw_vp9_altreffy_hor_virstride               : 9;
        RK_U32    reserve0                                    : 7;
        RK_U32    sw_vp9_altreffuv_hor_virstride              : 9;
        RK_U32    reserve1                                    : 7;
    } swreg39_vp9_altreff_hor_virstride;

    struct {
        RK_U32 sw_cur_poc                                     : 32;
    } swreg40_cur_poc;

    struct {
        RK_U32 reserve                                        : 3;
        RK_U32 sw_rlcwrite_base                               : 29;
    } swreg41_rlcwrite_base;

    struct {
        RK_U32 reserve                                        : 4;
        RK_U32 sw_pps_base                                    : 28;
    } swreg42_pps_base;

    struct {
        RK_U32 reserve                                        : 4;
        RK_U32 sw_rps_base                                    : 28;
    } swreg43_rps_base;

    struct {
        RK_U32 sw_strmd_error_e                               : 28;
        RK_U32 reserve                                        : 4;
    } swreg44_strmd_error_en;

    struct {
        RK_U32 vp9_error_info0                                : 32;
    } swreg45_vp9_error_info0;

    struct {
        RK_U32 sw_strmd_error_ctu_xoffset                     : 8;
        RK_U32 sw_strmd_error_ctu_yoffset                     : 8;
        RK_U32 sw_streamfifo_space2full                       : 7;
        RK_U32 reserve                                        : 1;
        RK_U32 sw_vp9_error_ctu0_en                           : 1;
    } swreg46_strmd_error_ctu;

    struct {
        RK_U32 sw_saowr_xoffet                                : 9;
        RK_U32 reserve                                        : 7;
        RK_U32 sw_saowr_yoffset                               : 10;
    } swreg47_sao_ctu_position;

    struct {
        RK_U32 sw_vp9_lastfy_virstride                        : 20;
        RK_U32 reserve                                        : 12;
    } swreg48_vp9_last_ystride;

    struct {
        RK_U32 sw_vp9_goldeny_virstride                       : 20;
        RK_U32 reserve                                        : 12;
    } swreg49_vp9_golden_ystride;

    struct {
        RK_U32 sw_vp9_altrefy_virstride                       : 20;
        RK_U32 reserve                                        : 12;
    } swreg50_vp9_altrefy_ystride;

    struct {
        RK_U32 sw_vp9_lastref_yuv_virstride                   : 21;
        RK_U32 reserve                                        : 11;
    } swreg51_vp9_lastref_yuvstride;


    RK_U32 swreg52_vp9_refcolmv_base;

    RK_U32 reg_not_use0[64 - 52 - 1];

    struct {
        RK_U32 sw_performance_cycle                           : 32;
    } swreg64_performance_cycle;

    struct {
        RK_U32 sw_axi_ddr_rdata                               : 32;
    } swreg65_axi_ddr_rdata;

    struct {
        RK_U32 sw_axi_ddr_wdata                               : 32;
    } swreg66_axi_ddr_wdata;

    struct {
        RK_U32 sw_busifd_resetn                               : 1;
        RK_U32 sw_cabac_resetn                                : 1;
        RK_U32 sw_dec_ctrl_resetn                             : 1;
        RK_U32 sw_transd_resetn                               : 1;
        RK_U32 sw_intra_resetn                                : 1;
        RK_U32 sw_inter_resetn                                : 1;
        RK_U32 sw_recon_resetn                                : 1;
        RK_U32 sw_filer_resetn                                : 1;
    } swreg67_fpgadebug_reset;

    struct {
        RK_U32 perf_cnt0_sel  : 6;
        RK_U32 reserve0       : 2;
        RK_U32 perf_cnt1_sel  : 6;
        RK_U32 reserve1       : 2;
        RK_U32 perf_cnt2_sel  : 6;
    } swreg68_performance_sel;

    struct {
        RK_U32 perf_cnt0 : 32;
    } swreg69_performance_cnt0;

    struct {
        RK_U32 perf_cnt1 : 32;
    } swreg70_performance_cnt1;

    struct {
        RK_U32 perf_cnt2 : 32;
    } swreg71_performance_cnt2;

    RK_U32 reg_not_use1[74 - 71 - 1];

    struct {
        RK_U32 sw_h264_cur_poc1 : 32;
    } swreg74_h264_cur_poc1;

    struct {
        RK_U32 vp9_error_info1 : 32;
    } swreg75_vp9_error_info1;

    struct {
        RK_U32 vp9_error_ctu1_x       : 6;
        RK_U32 reserve0               : 2;
        RK_U32 vp9_error_ctu1_y       : 6;
        RK_U32 reserve1               : 1;
        RK_U32 vp9_error_ctu1_en      : 1;
        RK_U32 reserve2               : 16;
    } swreg76_vp9_error_ctu1;
} ;


#define INTRA_MODES                 10
#define PARTITION_CONTEXTS          16
#define PARTITION_TYPES             4



const RK_U8 vp9_kf_y_mode_prob[INTRA_MODES][INTRA_MODES][INTRA_MODES - 1] = {
    {
        // above = dc
        { 137,  30,  42, 148, 151, 207,  70,  52,  91 },  // left = dc
        {  92,  45, 102, 136, 116, 180,  74,  90, 100 },  // left = v
        {  73,  32,  19, 187, 222, 215,  46,  34, 100 },  // left = h
        {  91,  30,  32, 116, 121, 186,  93,  86,  94 },  // left = d45
        {  72,  35,  36, 149,  68, 206,  68,  63, 105 },  // left = d135
        {  73,  31,  28, 138,  57, 124,  55, 122, 151 },  // left = d117
        {  67,  23,  21, 140, 126, 197,  40,  37, 171 },  // left = d153
        {  86,  27,  28, 128, 154, 212,  45,  43,  53 },  // left = d207
        {  74,  32,  27, 107,  86, 160,  63, 134, 102 },  // left = d63
        {  59,  67,  44, 140, 161, 202,  78,  67, 119 }   // left = tm
    }, {  // above = v
        {  63,  36, 126, 146, 123, 158,  60,  90,  96 },  // left = dc
        {  43,  46, 168, 134, 107, 128,  69, 142,  92 },  // left = v
        {  44,  29,  68, 159, 201, 177,  50,  57,  77 },  // left = h
        {  58,  38,  76, 114,  97, 172,  78, 133,  92 },  // left = d45
        {  46,  41,  76, 140,  63, 184,  69, 112,  57 },  // left = d135
        {  38,  32,  85, 140,  46, 112,  54, 151, 133 },  // left = d117
        {  39,  27,  61, 131, 110, 175,  44,  75, 136 },  // left = d153
        {  52,  30,  74, 113, 130, 175,  51,  64,  58 },  // left = d207
        {  47,  35,  80, 100,  74, 143,  64, 163,  74 },  // left = d63
        {  36,  61, 116, 114, 128, 162,  80, 125,  82 }   // left = tm
    }, {  // above = h
        {  82,  26,  26, 171, 208, 204,  44,  32, 105 },  // left = dc
        {  55,  44,  68, 166, 179, 192,  57,  57, 108 },  // left = v
        {  42,  26,  11, 199, 241, 228,  23,  15,  85 },  // left = h
        {  68,  42,  19, 131, 160, 199,  55,  52,  83 },  // left = d45
        {  58,  50,  25, 139, 115, 232,  39,  52, 118 },  // left = d135
        {  50,  35,  33, 153, 104, 162,  64,  59, 131 },  // left = d117
        {  44,  24,  16, 150, 177, 202,  33,  19, 156 },  // left = d153
        {  55,  27,  12, 153, 203, 218,  26,  27,  49 },  // left = d207
        {  53,  49,  21, 110, 116, 168,  59,  80,  76 },  // left = d63
        {  38,  72,  19, 168, 203, 212,  50,  50, 107 }   // left = tm
    }, {  // above = d45
        { 103,  26,  36, 129, 132, 201,  83,  80,  93 },  // left = dc
        {  59,  38,  83, 112, 103, 162,  98, 136,  90 },  // left = v
        {  62,  30,  23, 158, 200, 207,  59,  57,  50 },  // left = h
        {  67,  30,  29,  84,  86, 191, 102,  91,  59 },  // left = d45
        {  60,  32,  33, 112,  71, 220,  64,  89, 104 },  // left = d135
        {  53,  26,  34, 130,  56, 149,  84, 120, 103 },  // left = d117
        {  53,  21,  23, 133, 109, 210,  56,  77, 172 },  // left = d153
        {  77,  19,  29, 112, 142, 228,  55,  66,  36 },  // left = d207
        {  61,  29,  29,  93,  97, 165,  83, 175, 162 },  // left = d63
        {  47,  47,  43, 114, 137, 181, 100,  99,  95 }   // left = tm
    }, {  // above = d135
        {  69,  23,  29, 128,  83, 199,  46,  44, 101 },  // left = dc
        {  53,  40,  55, 139,  69, 183,  61,  80, 110 },  // left = v
        {  40,  29,  19, 161, 180, 207,  43,  24,  91 },  // left = h
        {  60,  34,  19, 105,  61, 198,  53,  64,  89 },  // left = d45
        {  52,  31,  22, 158,  40, 209,  58,  62,  89 },  // left = d135
        {  44,  31,  29, 147,  46, 158,  56, 102, 198 },  // left = d117
        {  35,  19,  12, 135,  87, 209,  41,  45, 167 },  // left = d153
        {  55,  25,  21, 118,  95, 215,  38,  39,  66 },  // left = d207
        {  51,  38,  25, 113,  58, 164,  70,  93,  97 },  // left = d63
        {  47,  54,  34, 146, 108, 203,  72, 103, 151 }   // left = tm
    }, {  // above = d117
        {  64,  19,  37, 156,  66, 138,  49,  95, 133 },  // left = dc
        {  46,  27,  80, 150,  55, 124,  55, 121, 135 },  // left = v
        {  36,  23,  27, 165, 149, 166,  54,  64, 118 },  // left = h
        {  53,  21,  36, 131,  63, 163,  60, 109,  81 },  // left = d45
        {  40,  26,  35, 154,  40, 185,  51,  97, 123 },  // left = d135
        {  35,  19,  34, 179,  19,  97,  48, 129, 124 },  // left = d117
        {  36,  20,  26, 136,  62, 164,  33,  77, 154 },  // left = d153
        {  45,  18,  32, 130,  90, 157,  40,  79,  91 },  // left = d207
        {  45,  26,  28, 129,  45, 129,  49, 147, 123 },  // left = d63
        {  38,  44,  51, 136,  74, 162,  57,  97, 121 }   // left = tm
    }, {  // above = d153
        {  75,  17,  22, 136, 138, 185,  32,  34, 166 },  // left = dc
        {  56,  39,  58, 133, 117, 173,  48,  53, 187 },  // left = v
        {  35,  21,  12, 161, 212, 207,  20,  23, 145 },  // left = h
        {  56,  29,  19, 117, 109, 181,  55,  68, 112 },  // left = d45
        {  47,  29,  17, 153,  64, 220,  59,  51, 114 },  // left = d135
        {  46,  16,  24, 136,  76, 147,  41,  64, 172 },  // left = d117
        {  34,  17,  11, 108, 152, 187,  13,  15, 209 },  // left = d153
        {  51,  24,  14, 115, 133, 209,  32,  26, 104 },  // left = d207
        {  55,  30,  18, 122,  79, 179,  44,  88, 116 },  // left = d63
        {  37,  49,  25, 129, 168, 164,  41,  54, 148 }   // left = tm
    }, {  // above = d207
        {  82,  22,  32, 127, 143, 213,  39,  41,  70 },  // left = dc
        {  62,  44,  61, 123, 105, 189,  48,  57,  64 },  // left = v
        {  47,  25,  17, 175, 222, 220,  24,  30,  86 },  // left = h
        {  68,  36,  17, 106, 102, 206,  59,  74,  74 },  // left = d45
        {  57,  39,  23, 151,  68, 216,  55,  63,  58 },  // left = d135
        {  49,  30,  35, 141,  70, 168,  82,  40, 115 },  // left = d117
        {  51,  25,  15, 136, 129, 202,  38,  35, 139 },  // left = d153
        {  68,  26,  16, 111, 141, 215,  29,  28,  28 },  // left = d207
        {  59,  39,  19, 114,  75, 180,  77, 104,  42 },  // left = d63
        {  40,  61,  26, 126, 152, 206,  61,  59,  93 }   // left = tm
    }, {  // above = d63
        {  78,  23,  39, 111, 117, 170,  74, 124,  94 },  // left = dc
        {  48,  34,  86, 101,  92, 146,  78, 179, 134 },  // left = v
        {  47,  22,  24, 138, 187, 178,  68,  69,  59 },  // left = h
        {  56,  25,  33, 105, 112, 187,  95, 177, 129 },  // left = d45
        {  48,  31,  27, 114,  63, 183,  82, 116,  56 },  // left = d135
        {  43,  28,  37, 121,  63, 123,  61, 192, 169 },  // left = d117
        {  42,  17,  24, 109,  97, 177,  56,  76, 122 },  // left = d153
        {  58,  18,  28, 105, 139, 182,  70,  92,  63 },  // left = d207
        {  46,  23,  32,  74,  86, 150,  67, 183,  88 },  // left = d63
        {  36,  38,  48,  92, 122, 165,  88, 137,  91 }   // left = tm
    }, {  // above = tm
        {  65,  70,  60, 155, 159, 199,  61,  60,  81 },  // left = dc
        {  44,  78, 115, 132, 119, 173,  71, 112,  93 },  // left = v
        {  39,  38,  21, 184, 227, 206,  42,  32,  64 },  // left = h
        {  58,  47,  36, 124, 137, 193,  80,  82,  78 },  // left = d45
        {  49,  50,  35, 144,  95, 205,  63,  78,  59 },  // left = d135
        {  41,  53,  52, 148,  71, 142,  65, 128,  51 },  // left = d117
        {  40,  36,  28, 143, 143, 202,  40,  55, 137 },  // left = d153
        {  52,  34,  29, 129, 183, 227,  42,  35,  43 },  // left = d207
        {  42,  44,  44, 104, 105, 164,  64, 130,  80 },  // left = d63
        {  43,  81,  53, 140, 169, 204,  68,  84,  72 }   // left = tm
    }
};

const RK_U8 vp9_kf_uv_mode_prob[INTRA_MODES][INTRA_MODES - 1] = {
    { 144,  11,  54, 157, 195, 130,  46,  58, 108 },  // y = dc
    { 118,  15, 123, 148, 131, 101,  44,  93, 131 },  // y = v
    { 113,  12,  23, 188, 226, 142,  26,  32, 125 },  // y = h
    { 120,  11,  50, 123, 163, 135,  64,  77, 103 },  // y = d45
    { 113,   9,  36, 155, 111, 157,  32,  44, 161 },  // y = d135
    { 116,   9,  55, 176,  76,  96,  37,  61, 149 },  // y = d117
    { 115,   9,  28, 141, 161, 167,  21,  25, 193 },  // y = d153
    { 120,  12,  32, 145, 195, 142,  32,  38,  86 },  // y = d207
    { 116,  12,  64, 120, 140, 125,  49, 115, 121 },  // y = d63
    { 102,  19,  66, 162, 182, 122,  35,  59, 128 }   // y = tm
};

const RK_U8 vp9_kf_partition_probs[PARTITION_CONTEXTS]
[PARTITION_TYPES - 1] = {
    // 8x8 -> 4x4
    { 158,  97,  94 },  // a/l both not split
    {  93,  24,  99 },  // a split, l not split
    {  85, 119,  44 },  // l split, a not split
    {  62,  59,  67 },  // a/l both split
    // 16x16 -> 8x8
    { 149,  53,  53 },  // a/l both not split
    {  94,  20,  48 },  // a split, l not split
    {  83,  53,  24 },  // l split, a not split
    {  52,  18,  18 },  // a/l both split
    // 32x32 -> 16x16
    { 150,  40,  39 },  // a/l both not split
    {  78,  12,  26 },  // a split, l not split
    {  67,  33,  11 },  // l split, a not split
    {  24,   7,   5 },  // a/l both split
    // 64x64 -> 32x32
    { 174,  35,  49 },  // a/l both not split
    {  68,  11,  27 },  // a split, l not split
    {  57,  15,   9 },  // l split, a not split
    {  12,   3,   3 },  // a/l both split
};


