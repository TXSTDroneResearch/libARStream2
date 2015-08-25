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


#define BEAVER_WRITER_H264_BYTE_STREAM_NALU_START_CODE 0x00000001

#define BEAVER_WRITER_H264_NALU_TYPE_SLICE 1
#define BEAVER_WRITER_H264_NALU_TYPE_SLICE_IDR 5
#define BEAVER_WRITER_H264_NALU_TYPE_SEI 6
#define BEAVER_WRITER_H264_NALU_TYPE_SPS 7
#define BEAVER_WRITER_H264_NALU_TYPE_PPS 8
#define BEAVER_WRITER_H264_NALU_TYPE_AUD 9
#define BEAVER_WRITER_H264_NALU_TYPE_FILLER_DATA 12

#define BEAVER_WRITER_H264_SEI_PAYLOAD_TYPE_BUFFERING_PERIOD 0
#define BEAVER_WRITER_H264_SEI_PAYLOAD_TYPE_PIC_TIMING 1
#define BEAVER_WRITER_H264_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED 5

#define BEAVER_WRITER_H264_SLICE_TYPE_P 0
#define BEAVER_WRITER_H264_SLICE_TYPE_B 1
#define BEAVER_WRITER_H264_SLICE_TYPE_I 2
#define BEAVER_WRITER_H264_SLICE_TYPE_SP 3
#define BEAVER_WRITER_H264_SLICE_TYPE_SI 4
#define BEAVER_WRITER_H264_SLICE_TYPE_P_ALL 5
#define BEAVER_WRITER_H264_SLICE_TYPE_B_ALL 6
#define BEAVER_WRITER_H264_SLICE_TYPE_I_ALL 7
#define BEAVER_WRITER_H264_SLICE_TYPE_SP_ALL 8
#define BEAVER_WRITER_H264_SLICE_TYPE_SI_ALL 9


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
        _ret = writeBits(_writer, 0, _halfLength, _emulationPrevention);
        if (_ret != _halfLength) return -1;
        _bitsWritten += _ret;

        // Suffix
        _ret = writeBits(_writer, _value, _halfLength + 1, _emulationPrevention);
        if (_ret != _halfLength + 1) return -1;
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
        ret = writeBits(writer, 32, BEAVER_WRITER_H264_BYTE_STREAM_NALU_START_CODE, 0);
        if (ret < 0)
        {
            return -1;
        }
        bitsWritten += ret;
    }

    // forbidden_zero_bit = 0
    // nal_ref_idc = 0
    // nal_unit_type = 6
    ret = writeBits(writer, 8, BEAVER_WRITER_H264_NALU_TYPE_SEI, 0);
    if (ret < 0)
    {
        return -1;
    }
    bitsWritten += ret;

    if ((pbUserDataUnregistered) && (userDataUnregisteredSize >= 16))
    {
        payloadType = BEAVER_WRITER_H264_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED;
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

