/**
 * @file beaver_writer.h
 * @brief H.264 Elementary Stream Tools Library - Writer
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _BEAVER_WRITER_H_
#define _BEAVER_WRITER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <stdio.h>


/**
 * @brief Beaver Writer instance handle.
 */
typedef void* BEAVER_Writer_Handle;


/**
 * @brief Beaver Writer configuration for initialization.
 */
typedef struct
{
    int naluPrefix;                         /**< write a NAL unit start code before each NALU */

} BEAVER_Writer_Config_t;


/**
 * @brief Initialize a Beaver Writer instance.
 *
 * The library allocates the required resources. The user must call BEAVER_Writer_Free() to free the resources.
 *
 * @param writerHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Writer_Init(BEAVER_Writer_Handle* writerHandle, BEAVER_Writer_Config_t* config);


/**
 * @brief Free a Beaver Writer instance.
 *
 * The library frees the allocated resources.
 *
 * @param writerHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Writer_Free(BEAVER_Writer_Handle writerHandle);


/**
 * @brief Sets the Writer SPS and PPS context.
 *
 * The function imports SPS and PPS context from a Beaver Parser.
 *
 * @param[in] writerHandle Instance handle.
 * @param[in] spsContext SPS context to use (from a Beaver Parser)
 * @param[in] ppsContext PPS context to use (from a Beaver Parser)
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Writer_SetSpsPpsContext(BEAVER_Writer_Handle writerHandle, const void *spsContext, const void *ppsContext);


/**
 * @brief Write a SEI NAL unit.
 *
 * The function writes a Supplemental Enhancement Information NAL unit.
 *
 * @param[in] writerHandle Instance handle.
 * @param[in] pbUserDataUnregistered User data input buffer
 * @param[in] userDataUnregisteredSize User data input buffer size
 * @param[in] pbOutputBuf Bitstream output buffer
 * @param[in] outputBufSize Bitstream output buffer size
 * @param[out] outputSize Bitstream output size
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Writer_WriteSeiNalu(BEAVER_Writer_Handle writerHandle, const uint8_t *pbUserDataUnregistered, unsigned int userDataUnregisteredSize, uint8_t *pbOutputBuf, unsigned int outputBufSize, unsigned int *outputSize);


/**
 * @brief Write a gray I-slice NAL unit.
 *
 * The function writes an entirely gray I-slice NAL unit.
 *
 * @param[in] writerHandle Instance handle.
 * @param[in] firstMbInSlice Slice first macroblock index
 * @param[in] sliceMbCount Slice macroblock count
 * @param[in] sliceContext Optional slice context to use (from a Beaver Parser)
 * @param[in] pbOutputBuf Bitstream output buffer
 * @param[in] outputBufSize Bitstream output buffer size
 * @param[out] outputSize Bitstream output size
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Writer_WriteGrayISliceNalu(BEAVER_Writer_Handle writerHandle, unsigned int firstMbInSlice, unsigned int sliceMbCount, void *sliceContext, uint8_t *pbOutputBuf, unsigned int outputBufSize, unsigned int *outputSize);


/**
 * @brief Write a skipped P-slice NAL unit.
 *
 * The function writes an entirely skipped P-slice NAL unit.
 *
 * @param[in] writerHandle Instance handle.
 * @param[in] firstMbInSlice Slice first macroblock index
 * @param[in] sliceMbCount Slice macroblock count
 * @param[in] sliceContext Optional slice context to use (from a Beaver Parser)
 * @param[in] pbOutputBuf Bitstream output buffer
 * @param[in] outputBufSize Bitstream output buffer size
 * @param[out] outputSize Bitstream output size
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Writer_WriteSkippedPSliceNalu(BEAVER_Writer_Handle writerHandle, unsigned int firstMbInSlice, unsigned int sliceMbCount, void *sliceContext, uint8_t *pbOutputBuf, unsigned int outputBufSize, unsigned int *outputSize);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _BEAVER_WRITER_H_ */

