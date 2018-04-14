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

static const char *name_rkvdecs[] = {"/dev/vpu-service", "/dev/vpu_service"};

#define VPU_IOC_MAGIC                       'l'

#define VPU_IOC_SET_CLIENT_TYPE             _IOW(VPU_IOC_MAGIC, 1, unsigned long)
#define VPU_IOC_SET_CLIENT_TYPE_U32         _IOW(VPU_IOC_MAGIC, 1, unsigned int)
#define VPU_IOC_GET_HW_FUSE_STATUS          _IOW(VPU_IOC_MAGIC, 2, unsigned long)
#define VPU_IOC_SET_REG                     _IOW(VPU_IOC_MAGIC, 3, unsigned long)
#define VPU_IOC_GET_REG                     _IOW(VPU_IOC_MAGIC, 4, unsigned long)
#define VPU_IOC_PROBE_IOMMU_STATUS          _IOR(VPU_IOC_MAGIC, 5, unsigned long)
#define VPU_IOC_WRITE(nr, size)             _IOC(_IOC_WRITE, VPU_IOC_MAGIC, (nr), (size))

typedef unsigned int            RK_U32;
typedef signed int              RK_S32;
typedef signed short            RK_S16;
typedef unsigned char           RK_U8;
typedef unsigned short          RK_U16;
typedef signed long long int    RK_S64;

typedef struct {
    RK_U32  sw_dec_out_tiled_e  : 1;
    RK_U32  sw_dec_latency      : 6;
    RK_U32  sw_pic_fixed_quant  : 1;
    RK_U32  sw_filtering_dis    : 1;
    RK_U32  sw_skip_mode        : 1;
    RK_U32  sw_dec_scmd_dis     : 1;
    RK_U32  sw_dec_adv_pre_dis  : 1;
    RK_U32  sw_priority_mode    : 1;            //chang
    RK_U32  sw_refbu2_thr       : 12;
    RK_U32  sw_refbu2_picid     : 5;
    RK_U32  reserve1            : 2;
} Device_config_reg1;

typedef struct {
    RK_U32  sw_stream_len       : 24;
    RK_U32  reserve1            : 1;
    RK_U32  sw_init_qp          : 6;
    RK_U32  reserve2           : 1;
} Dec_control_reg3;

typedef struct {
    RK_U32  sw_startmb_y        : 8;
    RK_U32  sw_startmb_x        : 9;
    RK_U32  sw_apf_threshold    : 14;
    RK_U32  sw_reserve          : 1;
} Dec_Error_concealment_reg;

typedef struct {
    RK_U32  sw_dec_in_endian    : 1;
    RK_U32  sw_dec_out_endian   : 1;
    RK_U32  sw_dec_inswap32_e   : 1;
    RK_U32  sw_dec_outswap32_e  : 1;
    RK_U32  sw_dec_strswap32_e  : 1;
    RK_U32  sw_dec_strendian_e  : 1;
    RK_U32  reserve3            : 26;
} Device_config_reg2;

typedef struct {
    RK_U32  sw_dec_irq      : 1;
    RK_U32  sw_dec_irq_dis  : 1;
    RK_U32  reserve0        : 2;
    RK_U32  sw_dec_rdy_int  : 1;
    RK_U32  sw_dec_bus_int  : 1;
    RK_U32  sw_dec_buffer_int   : 1;
    RK_U32  reserve1        : 1;
    RK_U32  sw_dec_aso_int  : 1;
    RK_U32  sw_dec_slice_int    : 1;
    RK_U32  sw_dec_pic_inf  : 1;
    RK_U32  reserve2        : 1;
    RK_U32  sw_dec_error_int: 1;
    RK_U32  sw_dec_timeout  : 1;
    RK_U32  reserve3        : 18;
} Dec_Interrupt_reg;

typedef struct {
    RK_U32  sw_dec_axi_rn_id    : 8;
    RK_U32  sw_dec_axi_wr_id    : 8;
    RK_U32  sw_dec_max_burst    : 5;
    RK_U32  resever             : 1;
    RK_U32  sw_dec_data_disc_e  : 1;
    RK_U32  resever1            : 9;
} Device_config_reg3;

typedef struct {
    RK_U32  sw_dec_e            : 1;
    RK_U32  sw_refbu2_buf_e     : 1;
    RK_U32  sw_dec_out_dis      : 1;
    RK_U32  resever             : 1;
    RK_U32  sw_dec_clk_gate_e   : 1;
    RK_U32  sw_dec_timeout_e    : 1;
    RK_U32  sw_picord_count_e   : 1;
    RK_U32  sw_seq_mbaff_e      : 1;
    RK_U32  sw_reftopfirst_e    : 1;
    RK_U32  sw_ref_topfield_e   : 1;
    RK_U32  sw_write_mvs_e      : 1;
    RK_U32  sw_sorenson_e       : 1;
    RK_U32  sw_fwd_interlace_e  : 1;
    RK_U32  sw_pic_topfield_e   : 1 ;
    RK_U32  sw_pic_inter_e      : 1;
    RK_U32  sw_pic_b_e          : 1;
    RK_U32  sw_pic_fieldmode_e  : 1;
    RK_U32  sw_pic_interlace_e  : 1;
    RK_U32  sw_pjpeg_e          : 1;
    RK_U32  sw_divx3_e          : 1;
    RK_U32  sw_rlc_mode_e       : 1;
    RK_U32  sw_ch_8pix_ileav_e  : 1;
    RK_U32  sw_start_code_e     : 1;
    RK_U32  resever1            : 8;
    RK_U32 sw_dec_ahb_hlock_e  : 1;

} Dec_control_reg0;

typedef struct {
    RK_U32  sw_reserve          : 2;
    RK_U32  sw_dec_ch8pix_base  : 30;
} Dec_BaseAdd_ch8pix_reg;

typedef struct {
    RK_U32  sw_refbu_y_offset   : 9;
    RK_U32  sw_reserve          : 3;
    RK_U32  sw_refbu_fparmod_e  : 1;
    RK_U32  sw_refbu_eval_e     : 1;
    RK_U32  sw_refbu_picid      : 5;
    RK_U32  sw_refbu_thr        : 12;
    RK_U32  sw_refbu_e          : 1;
} Dec_Refpicbuff_control_reg;

typedef struct {
    RK_U32  sw_reserve          : 25;
    RK_U32  sw_dec_rtl_rom      : 1;
    RK_U32  sw_dec_rv_prof      : 2;
    RK_U32  sw_ref_buff2_exist  : 1;
    RK_U32  sw_dec_divx_prof    : 1;
    RK_U32  sw_dec_refbu_ilace  : 1;
    RK_U32  sw_dec_jpeg_exten   : 1;
} Dec_Syn_configinfo_reg;

typedef struct {
    RK_U32  sw_refbu_top_sum    : 16;
    RK_U32  sw_refbu_bot_sum    : 16;
} Dec_Refpicbuff_info3_reg;

typedef struct {
    RK_U32  sw_refbu_intra_sum  : 16;
    RK_U32  sw_refbu_hit_sum    : 16;
} Dec_Refpicbuff_info1_reg;

typedef struct {
    RK_U32  sw_refbu_mv_sum : 22;
    RK_U32  sw_reserve          : 10;
} Dec_Refpicbuff_info2_reg;

typedef struct {
    RK_U32  sw_dec_max_owidth   : 11;
    RK_U32  sw_dec_soren_prof   : 1;
    RK_U32  sw_dec_bus_width    : 2;
    RK_U32  sw_dec_synth_lan    : 2;
    RK_U32  sw_dec_bus_strd : 4;
    RK_U32  sw_ref_buff_exist   : 1;
    RK_U32  sw_dec_obuff_leve   : 1;
    RK_U32  sw_dec_pjpeg_exist  : 1;
    RK_U32  sw_vp6_prof         : 1;
    RK_U32  sw_dec_h264_prof    : 2;
    RK_U32  sw_dec_mpeg4_prof   : 2;
    RK_U32  sw_dec_jpeg_prof    : 1;
    RK_U32  sw_dec_vc1_prof : 2;
    RK_U32  sw_dec_mpeg2_prof   : 1;
} Dec_Synthesis_config_reg;

typedef struct {
    RK_U32  debug_dec_mb_count  : 13;
    RK_U32  debug_referreq1     : 1;
    RK_U32  debug_referreq0     : 1;
    RK_U32  debug_filter_req        : 1;
    RK_U32  debug_framerdy      : 1;
    RK_U32  debug_strm_da_e : 1;
    RK_U32  debug_res_c_req     : 1;
    RK_U32  debug_res_y_req : 1;
    RK_U32  debug_rlc_req       : 1;
    RK_U32  debug_mv_req        : 10;
} Dec_Debug_reg;

typedef struct {
    RK_U32  sw_ref_frames       : 5;
    RK_U32  sw_topfieldfirst_e  : 1;
    RK_U32  sw_alt_scan_e       : 1;
    RK_U32  sw_mb_height_off    : 4;
    RK_U32  sw_pic_mb_height_p  : 8;
    RK_U32  sw_mb_width_off : 4;
    RK_U32  sw_pic_mb_width : 9;
} Dec_control_reg1;

typedef struct {
    RK_U32  sw_frame_pred_dct   : 1;
    RK_U32  sw_intra_vlc_tab        : 1;
    RK_U32  sw_intra_dc_prec    : 2;
    RK_U32  sw_con_mv_e     : 1;
    RK_U32  reserve             : 19;
    RK_U32  sw_qscale_type      : 1;
    RK_U32  reserve1            : 1;
    RK_U32  sw_stream_start_bit : 6;
} Dec_control_reg2;

typedef struct {
    RK_U32  reserve             : 1;
    RK_U32  sw_mv_accuracy_bwd  : 1;
    RK_U32  sw_mv_accuracy_fwd  : 1;
    RK_U32  sw_fcode_bwd_ver        : 4;
    RK_U32  sw_fcode_bwd_hor        : 4;
    RK_U32  sw_fcode_fwd_ver        : 4;
    RK_U32  sw_fcode_fwd_hor        : 4;
    RK_U32  sw_alt_scan_flag_e      : 1;
    RK_U32  reserve1                : 12;
} Dec_BaseAdd_Ref4_reg;


struct Mpeg2videodRkvRegs_t {
    RK_U32 ppReg[50];
    Device_config_reg1            config1;
    Dec_control_reg3              stream_buffinfo;//51
    Dec_Error_concealment_reg     error_position;
    RK_U32                        sw_dec_mode;
    Device_config_reg2            config2;
    Dec_Interrupt_reg             interrupt;
    Device_config_reg3            config3;
    Dec_control_reg0              control;
    RK_U32                        reserve0[2];
    Dec_BaseAdd_ch8pix_reg        ch8pix;
    RK_U32                        slice_table;
    RK_U32                        directmv_reg;
    RK_U32                        cur_pic_base;
    RK_U32                        VLC_base;
    Dec_Refpicbuff_control_reg    refbuff_ctl;
    RK_U32                        reserve1;
    Dec_Syn_configinfo_reg        syn_cfg;
    Dec_Refpicbuff_info3_reg      refbuff_info3;
    Dec_Refpicbuff_info1_reg      refbuff_info1;
    Dec_Refpicbuff_info2_reg      refbuff_info2;
    Dec_Synthesis_config_reg      syn_config;
    RK_U32                        reserve2;
    Dec_Debug_reg                 debug_info;
    RK_U32                        reserve3[46];
    Dec_control_reg1              pic_params;
    RK_U32                        reserve4;
    Dec_control_reg2              stream_bitinfo;
    RK_U32                        reserve5[8];
    RK_U32                        ref0;
    RK_U32                        reserve6[2];
    RK_U32                        ref2;
    RK_U32                        ref3;
    Dec_BaseAdd_Ref4_reg          dec_info;
    RK_U32                        reserve7[11];
    RK_U32                        ref1;
    RK_U32                        reserve8[10];
};

typedef struct _DXVA_PicEntry_M2V {
    union {
        struct {
            RK_U8 Index7Bits     : 7;
            RK_U8 AssociatedFlag : 1;
        };
        RK_U8 bPicEntry;
    };
} DXVA_PicEntry_M2V;

typedef struct M2VDDxvaSeq_t {
    RK_U32          decode_width; //horizontal_size_value
    RK_U32          decode_height; //vertical_size_value
    RK_S32          aspect_ratio_information;
    RK_S32          frame_rate_code;
    RK_S32          bit_rate_value;
    RK_S32          vbv_buffer_size;
    RK_S32          constrained_parameters_flag;
    RK_U32          load_intra_quantizer_matrix; //[TEMP]
    RK_U32          load_non_intra_quantizer_matrix; //[TEMP]
} M2VDDxvaSeq;

typedef struct M2VDDxvaSeqExt_t {
    RK_S32             profile_and_level_indication;
    RK_S32             progressive_sequence;
    RK_S32             chroma_format;
    RK_S32             low_delay;
    RK_S32             frame_rate_extension_n;
    RK_S32             frame_rate_extension_d;
} M2VDDxvaSeqExt;

/* ISO/IEC 13818-2 section 6.2.2.6: group_of_pictures_Dxvaer()  */
typedef struct M2VDDxvaGop_t {
    RK_S32             drop_flag;
    RK_S32             hour;
    RK_S32             minute;
    RK_S32             sec;
    RK_S32             frame;
    RK_S32             closed_gop;
    RK_S32             broken_link;
} M2VDDxvaGop;


/* ISO/IEC 13818-2 section 6.2.3: picture_Dxvaer() */
typedef struct M2VDDxvaPic_t {
    RK_S32             temporal_reference;
    RK_S32             picture_coding_type;
    RK_S32             pre_picture_coding_type;
    RK_S32             vbv_delay;
    RK_S32             full_pel_forward_vector;
    RK_S32             forward_f_code;
    RK_S32             full_pel_backward_vector;
    RK_S32             backward_f_code;
    RK_S32             pre_temporal_reference;
} M2VDDxvaPic;

typedef struct M2VDDxvaSeqDispExt_t {
    RK_S32             video_format;
    RK_S32             color_description;
    RK_S32             color_primaries;
    RK_S32             transfer_characteristics;
    RK_S32             matrix_coefficients;
} M2VDDxvaSeqDispExt;

/* ISO/IEC 13818-2 section 6.2.3.1: picture_coding_extension() Dxvaer */
typedef struct M2VDDxvaPicCodeExt_t {
    RK_S32             f_code[2][2];
    RK_S32             intra_dc_precision;
    RK_S32             picture_structure;
    RK_S32             top_field_first;
    RK_S32             frame_pred_frame_dct;
    RK_S32             concealment_motion_vectors;
    RK_S32             q_scale_type;
    RK_S32             intra_vlc_format;
    RK_S32             alternate_scan;
    RK_S32             repeat_first_field;
    RK_S32             chroma_420_type;
    RK_S32             progressive_frame;
    RK_S32             composite_display_flag;
    RK_S32             v_axis;
    RK_S32             field_sequence;
    RK_S32             sub_carrier;
    RK_S32             burst_amplitude;
    RK_S32             sub_carrier_phase;
} M2VDDxvaPicCodeExt;


/* ISO/IEC 13818-2 section 6.2.3.3: picture_display_extension() Dxvaer */
typedef struct M2VDDxvaPicDispExt_t {
    RK_S32             frame_center_horizontal_offset[3];
    RK_S32             frame_center_vertical_offset[3];
} M2VDDxvaPicDispExt;

typedef enum M2VDPicStruct_t {
    M2VD_PIC_STRUCT_TOP_FIELD    = 1,
    M2VD_PIC_STRUCT_BOTTOM_FIELD = 2,
    M2VD_PIC_STRUCT_FRAME        = 3
} M2VDPicStruct;

typedef enum M2VDPicCodingType_t {
    M2VD_CODING_TYPE_I = 1,
    M2VD_CODING_TYPE_P = 2,
    M2VD_CODING_TYPE_B = 3,
    M2VD_CODING_TYPE_D = 4
} M2VDPicCodingType;

