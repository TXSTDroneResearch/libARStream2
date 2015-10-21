/**
 * @file beaver_parser.h
 * @brief H.264 Elementary Stream Tools Library - Parser
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#ifndef _BEAVER_PARSER_H_
#define _BEAVER_PARSER_H_

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

#include <inttypes.h>
#include <stdio.h>


/**
 * @brief Beaver Parser instance handle.
 */
typedef void* BEAVER_Parser_Handle;


/**
 * @brief Beaver Parser configuration for initialization.
 */
typedef struct
{
    int extractUserDataSei;                 /**< enable user data SEI extraction, see BEAVER_Parser_GetUserDataSei() */
    int printLogs;                          /**< output parsing logs to stdout */

} BEAVER_Parser_Config_t;


/**
 * @brief Output slice information.
 */
typedef struct
{
    unsigned int idrPicFlag;                         /**< idrPicFlag syntax element */
    unsigned int nal_ref_idc;                        /**< nal_ref_idc syntax element */
    unsigned int nal_unit_type;                      /**< nal_unit_type syntax element */
    unsigned int first_mb_in_slice;                  /**< first_mb_in_slice syntax element */
    unsigned int slice_type;                         /**< slice_type syntax element */
    unsigned int sliceTypeMod5;                      /**< slice_type syntax element modulo 5 */
    unsigned int frame_num;                          /**< frame_num syntax element */
    unsigned int idr_pic_id;                         /**< idr_pic_id syntax element */
    int slice_qp_delta;                              /**< slice_qp_delta syntax element */
    unsigned int disable_deblocking_filter_idc;      /**< disable_deblocking_filter_idc syntax element */

} BEAVER_Parser_SliceInfo_t;


/**
 * @brief Initialize a Beaver Parser instance.
 *
 * The library allocates the required resources. The user must call BEAVER_Parser_Free() to free the resources.
 *
 * @param parserHandle Pointer to the handle used in future calls to the library.
 * @param config The instance configuration.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_Init(BEAVER_Parser_Handle* parserHandle, BEAVER_Parser_Config_t* config);


/**
 * @brief Free a Beaver Parser instance.
 *
 * The library frees the allocated resources.
 *
 * @param parserHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_Free(BEAVER_Parser_Handle parserHandle);


/**
 * @brief Read the next NAL unit from a file.
 *
 * The function finds the next NALU start and end in the file. The NALU shall then be parsed using the BEAVER_Parser_ParseNalu() function.
 *
 * @param parserHandle Instance handle.
 * @param fp Opened file to parse.
 * @param fileSize Total file size.
 * @param naluSize Optional pointer to the NAL unit size.
 *
 * @return 0 if no error occurred.
 * @return -2 if no start code has been found.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_ReadNextNalu_file(BEAVER_Parser_Handle parserHandle, FILE* fp, unsigned long long fileSize, unsigned int *naluSize);


/**
 * @brief Read the next NAL unit from a buffer.
 *
 * The function finds the next NALU start and end in the buffer. The NALU shall then be parsed using the BEAVER_Parser_ParseNalu() function.
 *
 * @param parserHandle Instance handle.
 * @param pBuf Buffer to parse.
 * @param bufSize Buffer size.
 * @param nextStartCodePos Optional pointer to the following NALU start code filled if one has been found (i.e. if more NALUs are present 
 * in the buffer).
 *
 * @return 0 if no error occurred.
 * @return -2 if no start code has been found.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_ReadNextNalu_buffer(BEAVER_Parser_Handle parserHandle, void* pBuf, unsigned int bufSize, unsigned int* nextStartCodePos);


/**
 * @brief Setup a NAL unit from a buffer before parsing.
 *
 * The function configures the parser for a NALU. The NALU shall then be parsed using the BEAVER_Parser_ParseNalu() function.
 * The buffer must contain only one NAL unit without start code.
 *
 * @param parserHandle Instance handle.
 * @param pNaluBuf NAL unit buffer to parse.
 * @param naluSize NAL unit size.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_SetupNalu_buffer(BEAVER_Parser_Handle parserHandle, void* pNaluBuf, unsigned int naluSize);


/**
 * @brief Parse the NAL unit.
 *
 * The function parses the current NAL unit. A call either to BEAVER_Parser_ReadNextNalu_file() or BEAVER_Parser_ReadNextNalu_buffer() must have been made 
 * prior to calling this function.
 *
 * @param parserHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_ParseNalu(BEAVER_Parser_Handle parserHandle);


/**
 * @brief Get the NAL unit type.
 *
 * The function returns the NALU type of the last parsed NALU. A call to BEAVER_Parser_ParseNalu() must have been made prior to calling this function.
 *
 * @param parserHandle Instance handle.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_GetLastNaluType(BEAVER_Parser_Handle parserHandle);


/**
 * @brief Get the slice info.
 *
 * The function returns the slice info of the last parsed slice. A call to BEAVER_Parser_ParseNalu() must have been made prior to calling this function. 
 * This function must only be called if the last NALU type is either 1 or 5 (coded slice).
 *
 * @param parserHandle Instance handle.
 * @param sliceInfo Pointer to the slice info structure to fill.
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_GetSliceInfo(BEAVER_Parser_Handle parserHandle, BEAVER_Parser_SliceInfo_t* sliceInfo);


/**
 * @brief Get the user data SEI count.
 *
 * The function returns the number of user data SEI of the last frame parsed. A call to BEAVER_Parser_ParseNalu() must have been made prior to calling this function.
 * Multiple user data SEI payloads can be present for the same frame.
 * This function should only be called if the last NALU type is 6 and the library instance has been initialized with extractUserDataSei = 1 in the config.
 * If no user data SEI has been found or if extractUserDataSei == 0 the function returns 0.
 * 
 * @param parserHandle Instance handle.
 *
 * @return the user data SEI count if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_GetUserDataSeiCount(BEAVER_Parser_Handle parserHandle);


/**
 * @brief Get a user data SEI.
 *
 * The function gets a pointer to the user data SEI of the last frame parsed. A call to BEAVER_Parser_ParseNalu() must have been made prior to calling this function.
 * User data SEI are identified by an index in case multiple user data SEI payloads are present for the same frame.
 * This function should only be called if the last NALU type is 6 and the library instance has been initialized with extractUserDataSei = 1 in the config.
 * If no user data SEI has been found or if extractUserDataSei == 0 the function fills pBuf with a NULL pointer.
 * 
 * @warning The returned pointer is managed by the library and must not be freed.
 *
 * @param parserHandle Instance handle.
 * @param index Index of the user data SEI.
 * @param pBuf Pointer to the user data SEI buffer pointer (filled with NULL if no user data SEI is available).
 * @param bufSize Pointer to the user data SEI buffer size (filled with 0 if no user data SEI is available).
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_GetUserDataSei(BEAVER_Parser_Handle parserHandle, unsigned int index, void** pBuf, unsigned int* bufSize);


/**
 * @brief Gets the Parser SPS and PPS context.
 *
 * The function exports SPS and PPS context from a Beaver Parser.
 *
 * @param[in] parserHandle Instance handle.
 * @param[out] spsContext Pointer to the SPS context
 * @param[out] ppsContext Pointer to the PPS context
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_GetSpsPpsContext(BEAVER_Parser_Handle parserHandle, void **spsContext, void **ppsContext);


/**
 * @brief Gets the Parser slice context.
 *
 * The function exports the last processed slice context from a Beaver Parser.
 * This function must only be called if the last NALU type is either 1 or 5 (coded slice).
 *
 * @param[in] parserHandle Instance handle.
 * @param[out] sliceContext Pointer to the slice context
 *
 * @return 0 if no error occurred.
 * @return -1 if an error occurred.
 */
int BEAVER_Parser_GetSliceContext(BEAVER_Parser_Handle parserHandle, void **sliceContext);


#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif /* #ifndef _BEAVER_PARSER_H_ */

