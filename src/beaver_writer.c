/**
 * @file beaver_writer.c
 * @brief H.264 Elementary Stream Tools Library - Writer
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <arpa/inet.h>

#include <beaver/beaver_writer.h>
#include "beaver_h264.h"


typedef struct BEAVER_Writer_s
{
    BEAVER_Writer_Config_t config;

    // NALU buffer
    uint8_t* pNaluBuf;
    unsigned int naluBufSize;
    unsigned int naluSize;      // in bytes

    // Bitstream cache
    uint32_t cache;
    int cacheLength;   // in bits
    int oldZeroCount;

    // Context
    BEAVER_H264_SpsContext_t spsContext;
    BEAVER_H264_PpsContext_t ppsContext;
    int isSpsPpsContextValid;
    BEAVER_H264_SliceContext_t sliceContext;

} BEAVER_Writer_t;


#ifndef BEAVER_VERBOSE
    #define printf(...) {} //TODO
    #define fprintf(...) {} //TODO
    #define log2(x) (log(x) / log(2)) //TODO
#endif


static inline int writeBits(BEAVER_Writer_t* _writer, unsigned int _numBits, uint32_t _value, int _emulationPrevention)
{
    int _cacheBits, _remBits, _i;
    uint8_t _write8;
    uint32_t _bitMask;

    _remBits = (int)_numBits;
    _cacheBits = 32 - _writer->cacheLength;

    if (_remBits >= _cacheBits)
    {
        // Write _cacheBits bits to the cache
        _writer->cache |= (_value >> (_remBits - _cacheBits));

        // The cache is full; write 4 bytes in the buffer
        for (_i = 0; _i < 4; _i++)
        {
            _write8 = (_writer->cache >> 24) & 0xFF;

            // Emulation prevention
            if (_emulationPrevention)
            {
                if ((_writer->oldZeroCount == 2) && (_write8 <= 3))
                {
                    // 0x000000 or 0x000001 or 0x000002 or 0x000003 => insert 0x03
                    if (_writer->naluSize < _writer->naluBufSize)
                    {
                        *(_writer->pNaluBuf++) = 0x03;
                        _writer->naluSize++;
                    }
                    else
                    {
                        return -1;
                    }
                    _writer->oldZeroCount = 0;
                }
                if (_write8 == 0)
                {
                    _writer->oldZeroCount++;
                }
                else
                {
                    _writer->oldZeroCount = 0;
                }
            }

            if (_writer->naluSize < _writer->naluBufSize)
            {
                *(_writer->pNaluBuf++) = _write8;
                _writer->naluSize++;
            }
            else
            {
                return -1;
            }
            _writer->cache <<= 8;
        }
        _remBits -= _cacheBits;
        _bitMask = (uint32_t)-1 >> (32 - _remBits);
        _value &= _bitMask;

        // Reset the chache
        _writer->cache = 0;
        _writer->cacheLength = 0;
        _cacheBits = 32;
    }

    if (_remBits > 0)
    {
        _writer->cache |= (_value << (_cacheBits - _remBits));
        _writer->cacheLength += _remBits;
    }

    return _numBits;
}


static inline int bitstreamByteAlign(BEAVER_Writer_t* _writer, int _emulationPrevention)
{
    int _bitsWritten = 0, _i;
    uint8_t _write8;

    if (_writer->cacheLength & 7)
    {
        _bitsWritten = writeBits(_writer, (8 - (_writer->cacheLength & 7)), 0, _emulationPrevention);
    }

    if (_writer->cacheLength)
    {
        // Write the cache bytes in the buffer
        for (_i = 0; _i < _writer->cacheLength / 8; _i++)
        {
            _write8 = (_writer->cache >> 24) & 0xFF;

            // Emulation prevention
            if (_emulationPrevention)
            {
                if ((_writer->oldZeroCount == 2) && (_write8 <= 3))
                {
                    // 0x000000 or 0x000001 or 0x000002 or 0x000003 => insert 0x03
                    if (_writer->naluSize < _writer->naluBufSize)
                    {
                        *(_writer->pNaluBuf++) = 0x03;
                        _writer->naluSize++;
                    }
                    else
                    {
                        return -1;
                    }
                    _writer->oldZeroCount = 0;
                }
                else if (_write8 == 0)
                {
                    _writer->oldZeroCount++;
                }
                else
                {
                    _writer->oldZeroCount = 0;
                }
            }

            if (_writer->naluSize < _writer->naluBufSize)
            {
                *(_writer->pNaluBuf++) = _write8;
                _writer->naluSize++;
            }
            else
            {
                return -1;
            }
            _writer->cache <<= 8;
        }

        // Reset the chache
        _writer->cache = 0;
        _writer->cacheLength = 0;
    }

    return _bitsWritten;
}


static inline int writeBits_expGolomb_code(BEAVER_Writer_t* _writer, uint32_t _value, int _emulationPrevention)
{
    int _ret, _leadingZeroBits, _halfLength, _bitsWritten = 0;
    uint32_t _val;

    if (_value == 0)
    {
        return -1;
    }
    else
    {
        // Count leading zeros in _value
        for (_val = _value, _leadingZeroBits = 0; _val; _val <<= 1)
        {
            if (_val & 0x80000000)
            {
                break;
            }
            _leadingZeroBits++;
        }

        _halfLength = 31 - _leadingZeroBits;

        // Prefix
        _ret = writeBits(_writer, _halfLength, 0, _emulationPrevention);
        if (_ret != _halfLength) return -41;
        _bitsWritten += _ret;

        // Suffix
        _ret = writeBits(_writer, _halfLength + 1, _value, _emulationPrevention);
        if (_ret != _halfLength + 1) return -42;
        _bitsWritten += _ret;
    }

    return _leadingZeroBits * 2 + 1;
}


static inline int writeBits_expGolomb_ue(BEAVER_Writer_t* _writer, uint32_t _value, int _emulationPrevention)
{
    if (_value == 0)
    {
        return writeBits(_writer, 1, 1, _emulationPrevention);
    }
    else
    {
        return writeBits_expGolomb_code(_writer, _value + 1, _emulationPrevention);
    }
}


static inline int writeBits_expGolomb_se(BEAVER_Writer_t* _writer, int32_t _value, int _emulationPrevention)
{
    if (_value == 0)
    {
        return writeBits(_writer, 1, 1, _emulationPrevention);
    }
    else if (_value < 0)
    {
        return writeBits_expGolomb_code(_writer, (uint32_t)(-_value * 2 + 1), _emulationPrevention);
    }
    else
    {
        return writeBits_expGolomb_code(_writer, (uint32_t)(_value * 2), _emulationPrevention);
    }
}


static int BEAVER_Writer_WriteSeiPayload_userDataUnregistered(BEAVER_Writer_t* writer, const uint8_t *pbPayload, unsigned int payloadSize)
{
    int ret = 0;
    unsigned int i;
    int _bitsWritten = 0;

    for (i = 0; i < payloadSize; i++)
    {
        ret = writeBits(writer, 8, (uint32_t)(*pbPayload++), 1);
        if (ret < 0)
        {
            fprintf(stderr, "error: failed to write bits\n");
            return ret;
        }
        _bitsWritten += ret;
    }

    return _bitsWritten;
}


int BEAVER_Writer_WriteSeiNalu(BEAVER_Writer_Handle writerHandle, const uint8_t *pbUserDataUnregistered, unsigned int userDataUnregisteredSize, uint8_t *pbOutputBuf, unsigned int outputBufSize, unsigned int *outputSize)
{
    BEAVER_Writer_t *writer = (BEAVER_Writer_t*)writerHandle;
    int ret = 0, bitsWritten = 0;
    unsigned int payloadType, payloadSize;

    if ((!writerHandle) || (!pbOutputBuf) || (outputBufSize == 0) || (!outputSize))
    {
        return -1;
    }

    writer->pNaluBuf = pbOutputBuf;
    writer->naluBufSize = outputBufSize;
    writer->naluSize = 0;

    // Reset the bitstream cache
    writer->cache = 0;
    writer->cacheLength = 0;
    writer->oldZeroCount = 0;

    // NALU start code
    if (writer->config.naluPrefix)
    {
        ret = writeBits(writer, 32, BEAVER_H264_BYTE_STREAM_NALU_START_CODE, 0);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    // forbidden_zero_bit = 0
    // nal_ref_idc = 0
    // nal_unit_type = 6
    ret = writeBits(writer, 8, BEAVER_H264_NALU_TYPE_SEI, 0);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    if ((pbUserDataUnregistered) && (userDataUnregisteredSize >= 16))
    {
        payloadType = BEAVER_H264_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED;
        payloadSize = userDataUnregisteredSize;

        while (payloadType > 255)
        {
            // ff_byte
            ret = writeBits(writer, 8, 0xFF, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
            payloadType -= 255;
        }
        // last_payload_type_byte
        ret = writeBits(writer, 8, payloadType, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        while (payloadSize > 255)
        {
            // ff_byte
            ret = writeBits(writer, 8, 0xFF, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
            payloadSize -= 255;
        }
        // last_payload_type_byte
        ret = writeBits(writer, 8, payloadSize, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        // user_data_payload_byte
        ret = BEAVER_Writer_WriteSeiPayload_userDataUnregistered(writer, pbUserDataUnregistered, userDataUnregisteredSize);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    // rbsp_trailing_bits
    ret = writeBits(writer, 1, 1, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    ret = bitstreamByteAlign(writer, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    *outputSize = writer->naluSize;

    return 0;
}


static int BEAVER_Writer_WriteRefPicListModification(BEAVER_Writer_t* writer, BEAVER_H264_SliceContext_t *slice, BEAVER_H264_SpsContext_t *sps, BEAVER_H264_PpsContext_t *pps)
{
    int ret = 0;
    int bitsWritten = 0;

    if ((slice->sliceTypeMod5 != BEAVER_H264_SLICE_TYPE_I) && (slice->sliceTypeMod5 != BEAVER_H264_SLICE_TYPE_SI))
    {
        // ref_pic_list_modification_flag_l0
        ret = writeBits(writer, 1, slice->ref_pic_list_modification_flag_l0, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if (slice->ref_pic_list_modification_flag_l0)
        {
            // UNSUPPORTED
            return -1;
        }
    }

    if (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_B)
    {
        // ref_pic_list_modification_flag_l1
        ret = writeBits(writer, 1, slice->ref_pic_list_modification_flag_l1, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if (slice->ref_pic_list_modification_flag_l1)
        {
            // UNSUPPORTED
            return -1;
        }
    }

    return bitsWritten;
}


static int BEAVER_Writer_WriteDecRefPicMarking(BEAVER_Writer_t* writer, BEAVER_H264_SliceContext_t *slice, BEAVER_H264_SpsContext_t *sps, BEAVER_H264_PpsContext_t *pps)
{
    int ret = 0;
    int bitsWritten = 0;

    if (slice->idrPicFlag)
    {
        // no_output_of_prior_pics_flag
        ret = writeBits(writer, 1, slice->no_output_of_prior_pics_flag, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        // long_term_reference_flag
        ret = writeBits(writer, 1, slice->long_term_reference_flag, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }
    else
    {
        // adaptive_ref_pic_marking_mode_flag
        ret = writeBits(writer, 1, slice->adaptive_ref_pic_marking_mode_flag, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if (slice->adaptive_ref_pic_marking_mode_flag)
        {
            // UNSUPPORTED
            return -1;
        }
    }

    return bitsWritten;
}


static int BEAVER_Writer_WriteSliceHeader(BEAVER_Writer_t* writer, BEAVER_H264_SliceContext_t *slice, BEAVER_H264_SpsContext_t *sps, BEAVER_H264_PpsContext_t *pps)
{
    int ret = 0;
    int bitsWritten = 0;

    // first_mb_in_slice
    ret = writeBits_expGolomb_ue(writer, slice->first_mb_in_slice, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    // slice_type
    ret = writeBits_expGolomb_ue(writer, slice->slice_type, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    // pic_parameter_set_id
    ret = writeBits_expGolomb_ue(writer, slice->pic_parameter_set_id, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    if (sps->separate_colour_plane_flag == 1)
    {
        // colour_plane_id
        ret = writeBits(writer, 2, slice->colour_plane_id, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    // frame_num
    ret = writeBits(writer, sps->log2_max_frame_num_minus4 + 4, slice->frame_num, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    if (!sps->frame_mbs_only_flag)
    {
        // field_pic_flag
        ret = writeBits(writer, 1, slice->field_pic_flag, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if (slice->field_pic_flag)
        {
            // bottom_field_flag
            ret = writeBits(writer, 1, slice->bottom_field_flag, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
        }
    }

    if (slice->idrPicFlag)
    {
        // idr_pic_id
        ret = writeBits_expGolomb_ue(writer, slice->idr_pic_id, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    if (sps->pic_order_cnt_type == 0)
    {
        // pic_order_cnt_lsb
        ret = writeBits(writer, sps->log2_max_pic_order_cnt_lsb_minus4 + 4, slice->pic_order_cnt_lsb, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if ((pps->bottom_field_pic_order_in_frame_present_flag) && (!slice->field_pic_flag))
        {
            // delta_pic_order_cnt_bottom
            ret = writeBits_expGolomb_se(writer, slice->delta_pic_order_cnt_bottom, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
        }
    }

    if ((sps->pic_order_cnt_type == 1) && (!sps->delta_pic_order_always_zero_flag))
    {
        // delta_pic_order_cnt[0]
        ret = writeBits_expGolomb_se(writer, slice->delta_pic_order_cnt_0, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if ((pps->bottom_field_pic_order_in_frame_present_flag) && (!slice->field_pic_flag))
        {
            // delta_pic_order_cnt[1]
            ret = writeBits_expGolomb_se(writer, slice->delta_pic_order_cnt_1, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
        }
    }

    if (pps->redundant_pic_cnt_present_flag)
    {
        // redundant_pic_cnt
        ret = writeBits_expGolomb_ue(writer, slice->redundant_pic_cnt, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    if (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_B)
    {
        // direct_spatial_mv_pred_flag
        ret = writeBits(writer, 1, slice->direct_spatial_mv_pred_flag, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    if ((slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_P) || (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_SP) || (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_B))
    {
        // num_ref_idx_active_override_flag
        ret = writeBits(writer, 1, slice->num_ref_idx_active_override_flag, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if (slice->num_ref_idx_active_override_flag)
        {
            // num_ref_idx_l0_active_minus1
            ret = writeBits_expGolomb_ue(writer, slice->num_ref_idx_l0_active_minus1, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;

            if (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_B)
            {
                // num_ref_idx_l1_active_minus1
                ret = writeBits_expGolomb_ue(writer, slice->num_ref_idx_l1_active_minus1, 1);
                if (ret < 0)
                {
                    return -1;
                }
                bitsWritten += ret;
            }
        }
    }

    if ((slice->nal_unit_type == 20) || (slice->nal_unit_type == 21))
    {
        // ref_pic_list_mvc_modification()
        // UNSUPPORTED
        return -1;
    }
    else
    {
        // ref_pic_list_modification()
        ret = BEAVER_Writer_WriteRefPicListModification(writer, slice, sps, pps);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    if ((pps->weighted_pred_flag && ((slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_P) || (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_SP))) 
            || ((pps->weighted_bipred_idc == 1) && (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_B)))
    {
        // pred_weight_table()
        // UNSUPPORTED
        return -1;
    }

    if (slice->nal_ref_idc != 0)
    {
        // dec_ref_pic_marking()
        ret = BEAVER_Writer_WriteDecRefPicMarking(writer, slice, sps, pps);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    if ((pps->entropy_coding_mode_flag) && (slice->sliceTypeMod5 != BEAVER_H264_SLICE_TYPE_I) && (slice->sliceTypeMod5 != BEAVER_H264_SLICE_TYPE_SI))
    {
        // cabac_init_idc
        ret = writeBits_expGolomb_ue(writer, slice->cabac_init_idc, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    // slice_qp_delta
    ret = writeBits_expGolomb_se(writer, slice->slice_qp_delta, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    if ((slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_SP) || (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_SI))
    {
        if (slice->sliceTypeMod5 == BEAVER_H264_SLICE_TYPE_SP)
        {
            // sp_for_switch_flag
            ret = writeBits(writer, 1, slice->sp_for_switch_flag, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
        }

        // slice_qs_delta
        ret = writeBits_expGolomb_se(writer, slice->slice_qs_delta, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    if (pps->deblocking_filter_control_present_flag)
    {
        // disable_deblocking_filter_idc
        ret = writeBits_expGolomb_ue(writer, slice->disable_deblocking_filter_idc, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;

        if (slice->disable_deblocking_filter_idc != 1)
        {
            // slice_alpha_c0_offset_div2
            ret = writeBits_expGolomb_se(writer, slice->slice_alpha_c0_offset_div2, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;

            // slice_beta_offset_div2
            ret = writeBits_expGolomb_se(writer, slice->slice_beta_offset_div2, 1);
            if (ret < 0)
            {
                return -1;
            }
            bitsWritten += ret;
        }
    }

    if ((pps->num_slice_groups_minus1 > 0) && (pps->slice_group_map_type >= 3) && (pps->slice_group_map_type <= 5))
    {
        int picSizeInMapUnits, n;

        picSizeInMapUnits = (sps->pic_width_in_mbs_minus1 + 1) * (sps->pic_height_in_map_units_minus1 + 1);
        n = ceil(log2((picSizeInMapUnits / (pps->slice_group_change_rate_minus1 + 1)) + 1));

        // slice_group_change_cycle
        ret = writeBits(writer, n, slice->slice_group_change_cycle, 1);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    return bitsWritten;
}


static int BEAVER_Writer_WriteSkippedPSliceData(BEAVER_Writer_t* writer, BEAVER_H264_SliceContext_t *slice, BEAVER_H264_SpsContext_t *sps, BEAVER_H264_PpsContext_t *pps)
{
    int ret = 0;
    int bitsWritten = 0;

    if (pps->entropy_coding_mode_flag)
    {
        // cabac_alignment_one_bit
        // UNSUPPORTED
        return -1;
    }

    // mb_skip_run
    ret = writeBits_expGolomb_ue(writer, slice->sliceMbCount, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    return bitsWritten;
}


int BEAVER_Writer_WriteSkippedPSliceNalu(BEAVER_Writer_Handle writerHandle, unsigned int firstMbInSlice, unsigned int sliceMbCount, void *sliceContext, uint8_t *pbOutputBuf, unsigned int outputBufSize, unsigned int *outputSize)
{
    BEAVER_Writer_t *writer = (BEAVER_Writer_t*)writerHandle;
    int ret = 0, bitsWritten = 0;

    if ((!writerHandle) || (!pbOutputBuf) || (outputBufSize == 0) || (!outputSize))
    {
        return -1;
    }

    if (!writer->isSpsPpsContextValid)
    {
        return -1;
    }

    // Slice context
    if (sliceContext)
    {
        memcpy(&writer->sliceContext, sliceContext, sizeof(BEAVER_H264_SliceContext_t));
        writer->sliceContext.first_mb_in_slice = firstMbInSlice;
        writer->sliceContext.sliceMbCount = sliceMbCount;
        writer->sliceContext.slice_type = (writer->sliceContext.slice_type >= 5) ? BEAVER_H264_SLICE_TYPE_P_ALL : BEAVER_H264_SLICE_TYPE_P;
        writer->sliceContext.sliceTypeMod5 = writer->sliceContext.slice_type % 5;
        writer->sliceContext.redundant_pic_cnt = 0;
        writer->sliceContext.direct_spatial_mv_pred_flag = 0;
        writer->sliceContext.slice_qp_delta = 0;
        writer->sliceContext.disable_deblocking_filter_idc = 2; // disable deblocking across slice boundaries
        writer->sliceContext.slice_alpha_c0_offset_div2 = 0;
        writer->sliceContext.slice_beta_offset_div2 = 0;
    }
    else
    {
        //TODO
        return -1;
    }

    writer->pNaluBuf = pbOutputBuf;
    writer->naluBufSize = outputBufSize;
    writer->naluSize = 0;

    // Reset the bitstream cache
    writer->cache = 0;
    writer->cacheLength = 0;
    writer->oldZeroCount = 0;

    // NALU start code
    if (writer->config.naluPrefix)
    {
        ret = writeBits(writer, 32, BEAVER_H264_BYTE_STREAM_NALU_START_CODE, 0);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    // forbidden_zero_bit
    // nal_ref_idc
    // nal_unit_type
    ret = writeBits(writer, 8, ((writer->sliceContext.nal_ref_idc & 3) << 5) | writer->sliceContext.nal_unit_type, 0);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    // slice_header
    ret = BEAVER_Writer_WriteSliceHeader(writer, &writer->sliceContext, &writer->spsContext, &writer->ppsContext);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    // slice_data
    ret = BEAVER_Writer_WriteSkippedPSliceData(writer, &writer->sliceContext, &writer->spsContext, &writer->ppsContext);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    // rbsp_slice_trailing_bits

    // rbsp_trailing_bits
    ret = writeBits(writer, 1, 1, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    ret = bitstreamByteAlign(writer, 1);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    //TODO: cabac_zero_word

    *outputSize = writer->naluSize;

    return 0;
}


int BEAVER_Writer_SetSpsPpsContext(BEAVER_Writer_Handle writerHandle, const void *spsContext, const void *ppsContext)
{
    BEAVER_Writer_t *writer = (BEAVER_Writer_t*)writerHandle;

    if ((!writerHandle) || (!spsContext) || (!ppsContext))
    {
        return -1;
    }

    memcpy(&writer->spsContext, spsContext, sizeof(BEAVER_H264_SpsContext_t));
    memcpy(&writer->ppsContext, ppsContext, sizeof(BEAVER_H264_PpsContext_t));
    writer->isSpsPpsContextValid = 1;

    return 0;
}


int BEAVER_Writer_Init(BEAVER_Writer_Handle* writerHandle, BEAVER_Writer_Config_t* config)
{
    BEAVER_Writer_t* writer;

    if (!writerHandle)
    {
        fprintf(stderr, "Error: invalid pointer for handle\n");
        return -1;
    }

    writer = (BEAVER_Writer_t*)malloc(sizeof(*writer));
    if (!writer)
    {
        fprintf(stderr, "Error: allocation failed (size %ld)\n", sizeof(*writer));
        return -1;
    }
    memset(writer, 0, sizeof(*writer));

    if (config)
    {
        memcpy(&writer->config, config, sizeof(writer->config));
    }

    *writerHandle = (BEAVER_Writer_Handle*)writer;

    return 0;
}


int BEAVER_Writer_Free(BEAVER_Writer_Handle writerHandle)
{
    BEAVER_Writer_t* writer = (BEAVER_Writer_t*)writerHandle;

    if (!writerHandle)
    {
        return 0;
    }

    free(writer);

    return 0;
}
