/**
 * @file beaver_jni_readerfilter.c
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <jni.h>
#include <beaver/beaver_filter.h>
#include <libARStream/ARSTREAM_Reader2.h>
#include <libARSAL/ARSAL_Print.h>


#define BEAVER_JNI_READERFILTER_TAG "BEAVER_JNI_ReaderFilter"


static jmethodID g_spsPpsCbWrapper_id = 0;
static jmethodID g_getAuBufferCbWrapper_id = 0;
static jmethodID g_cancelAuBufferCbWrapper_id = 0;
static jmethodID g_auReadyCbWrapper_id = 0;
static JavaVM *g_vm = NULL;


static int BEAVER_JNI_ReaderFilter_SpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *thizz);
static int BEAVER_JNI_ReaderFilter_GetAuBufferCallback(uint8_t **auBuffer, int *auBufferSize, void *thizz);
static int BEAVER_JNI_ReaderFilter_CancelAuBufferCallback(uint8_t *auBuffer, int auBufferSize, void *thizz);
static int BEAVER_JNI_ReaderFilter_AuReadyCallback(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, BEAVER_Filter_AuSyncType_t auSyncType, void *thizz);


typedef struct
{
    jobject thizz;
    BEAVER_Filter_Handle beaverFilter;
    ARSTREAM_Reader2_t *reader;

} BEAVER_JNI_ReaderFilter_t;


JNIEXPORT void JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeInitClass(JNIEnv *env, jclass clazz)
{
    jint res = (*env)->GetJavaVM(env, &g_vm);
    if (res < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to get JavaVM pointer");
    }
    g_spsPpsCbWrapper_id = (*env)->GetMethodID(env, clazz, "spsPpsCallbackWrapper", "(IJIZZJII)[J"); //TODO
    g_getAuBufferCbWrapper_id = (*env)->GetMethodID(env, clazz, "getAuBufferCallbackWrapper", "(IJIZZJII)[J"); //TODO
    g_cancelAuBufferCbWrapper_id = (*env)->GetMethodID(env, clazz, "cancelAuBufferCallbackWrapper", "(IJIZZJII)[J"); //TODO
    g_auReadyCbWrapper_id = (*env)->GetMethodID(env, clazz, "auReadyCallbackWrapper", "(IJIZZJII)[J"); //TODO
}


JNIEXPORT jlong JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeConstructor(JNIEnv *env, jobject thizz, jstring serverAddress, jint serverStreamPort, jint serverControlPort,
                                                      jint clientStreamPort, jint clientControlPort, jint maxPacketSize, jint maxBitrate, jint maxLatency, jint maxNetworkLatency,
                                                      jint auFifoSize, jint waitForSync, jint outputIncompleteAu, jint filterOutSpsPps, jint filterOutSei)
{
    int ret = 0;
    eARSTREAM_ERROR err = ARSTREAM_OK;
    BEAVER_JNI_ReaderFilter_t *readerFilter = NULL;

    readerFilter = malloc(sizeof(BEAVER_JNI_ReaderFilter_t));
    if (!readerFilter)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Allocation failed");
        return (jlong)(intptr_t)NULL;
    }
    memset(readerFilter, 0, sizeof(BEAVER_JNI_ReaderFilter_t));
    readerFilter->thizz = (*env)->NewGlobalRef(env, thizz);

    BEAVER_Filter_Config_t beaverFilterConfig;
    memset(&beaverFilterConfig, 0, sizeof(beaverFilterConfig));
    beaverFilterConfig.spsPpsCallback = &BEAVER_JNI_Filter_SpsPpsCallback;
    beaverFilterConfig.spsPpsCallbackUserPtr = (void*)readerFilter->thizz;
    beaverFilterConfig.getAuBufferCallback = &BEAVER_JNI_Filter_GetAuBufferCallback;
    beaverFilterConfig.getAuBufferCallbackUserPtr = (void*)readerFilter->thizz;
    beaverFilterConfig.cancelAuBufferCallback = &BEAVER_JNI_Filter_CancelAuBufferCallback;
    beaverFilterConfig.cancelAuBufferCallbackUserPtr = (void*)readerFilter->thizz;
    beaverFilterConfig.auReadyCallback = &BEAVER_JNI_Filter_AuReadyCallback;
    beaverFilterConfig.auReadyCallbackUserPtr = (void*)readerFilter->thizz;
    beaverFilterConfig.auFifoSize = auFifoSize;
    beaverFilterConfig.waitForSync = waitForSync;
    beaverFilterConfig.outputIncompleteAu = outputIncompleteAu;
    beaverFilterConfig.filterOutSpsPps = filterOutSpsPps;
    beaverFilterConfig.filterOutSei = filterOutSei;

    ret = BEAVER_Filter_Init(&readerFilter->beaverFilter, &beaverFilterConfig);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Error while creating beaver_filter: %d", ret);
    }

    const char *c_serverAddress = (*env)->GetStringUTFChars(env, serverAddress, NULL);
    ARSTREAM_Reader2_Config_t readerConfig;
    memset(&readerConfig, 0, sizeof(readerConfig));
    readerConfig.serverAddr = c_serverAddress;
    readerConfig.mcastAddr = NULL;
    readerConfig.mcastIfaceAddr = NULL;
    readerConfig.serverStreamPort = serverStreamPort;
    readerConfig.serverControlPort = serverControlPort;
    readerConfig.clientStreamPort = clientStreamPort;
    readerConfig.clientControlPort = clientControlPort;
    readerConfig.naluCallback = BEAVER_Filter_ArstreamReader2NaluCallback;
    readerConfig.naluCallbackUserPtr = (void*)beaverFilter;
    readerConfig.maxPacketSize = maxPacketSize;
    readerConfig.maxBitrate = maxBitrate;
    readerConfig.maxLatencyMs = maxLatency;
    readerConfig.maxNetworkLatencyMs = maxNetworkLatency;
    readerConfig.insertStartCodes = 1;

    readerFilter->reader = ARSTREAM_Reader2_New(&readerConfig, &err);
    if (err != ARSTREAM_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Error while creating reader : %s", ARSTREAM_Error_ToString(err));
    }

    (*env)->ReleaseStringUTFChars(env, serverAddress, c_serverAddress);

    return (jlong)(intptr_t)readerFilter;
}


static int BEAVER_JNI_ReaderFilter_SpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *thizz)
{
    int ret = -1;
    JNIEnv *env = NULL;
    int wasAlreadyAttached = 1;
    int envStatus = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED)
    {
        wasAlreadyAttached = 0;
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
    }

    jlongArray newNativeDataInfos = (*env)->CallObjectMethod(env, (jobject)thizz, g_spsPpsCbWrapper_id, (jlong)(intptr_t)spsBuffer,
                                                             (jint)spsSize, (jlong)(intptr_t)ppsBuffer, (jint)ppsSize);

    if (newNativeDataInfos != NULL)
    {
        jlong *array = (*env)->GetLongArrayElements(env, newNativeDataInfos, NULL);
        ret = (int)array[0];
        (*env)->ReleaseLongArrayElements(env, newNativeDataInfos, array, 0);
        (*env)->DeleteLocalRef(env, newNativeDataInfos);
    }

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}


static int BEAVER_JNI_ReaderFilter_GetAuBufferCallback(uint8_t **auBuffer, int *auBufferSize, void *thizz)
{
    int ret = -1;
    JNIEnv *env = NULL;
    int wasAlreadyAttached = 1;
    int envStatus = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED)
    {
        wasAlreadyAttached = 0;
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
    }

    jlongArray newNativeDataInfos = (*env)->CallObjectMethod(env, (jobject)thizz, g_getAuBufferCbWrapper_id);

    if (newNativeDataInfos != NULL)
    {
        jlong *array = (*env)->GetLongArrayElements(env, newNativeDataInfos, NULL);
        ret = (int)array[0];
        *auBuffer = (uint8_t*)(intptr_t)array[1];
        *auBufferSize = (int)array[2];
        (*env)->ReleaseLongArrayElements(env, newNativeDataInfos, array, 0);
        (*env)->DeleteLocalRef(env, newNativeDataInfos);
    }

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}


static int BEAVER_JNI_ReaderFilter_CancelAuBufferCallback(uint8_t *auBuffer, int auBufferSize, void *thizz)
{
    int ret = -1;
    JNIEnv *env = NULL;
    int wasAlreadyAttached = 1;
    int envStatus = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED)
    {
        wasAlreadyAttached = 0;
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
    }

    jlongArray newNativeDataInfos = (*env)->CallObjectMethod(env, (jobject)thizz, g_cancelAuBufferCbWrapper_id, (jlong)(intptr_t)auBuffer, (jint)auBufferSize);

    if (newNativeDataInfos != NULL)
    {
        jlong *array = (*env)->GetLongArrayElements(env, newNativeDataInfos, NULL);
        ret = (int)array[0];
        (*env)->ReleaseLongArrayElements(env, newNativeDataInfos, array, 0);
        (*env)->DeleteLocalRef(env, newNativeDataInfos);
    }

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}


static int BEAVER_JNI_ReaderFilter_AuReadyCallback(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, BEAVER_Filter_AuSyncType_t auSyncType, void *thizz)
{
    int ret = -1;
    JNIEnv *env = NULL;
    int wasAlreadyAttached = 1;
    int envStatus = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    if (envStatus == JNI_EDETACHED)
    {
        wasAlreadyAttached = 0;
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
    }

    jlongArray newNativeDataInfos = (*env)->CallObjectMethod(env, (jobject)thizz, g_auReadyCbWrapper_id, (jlong)(intptr_t)auBuffer, (jint)auSize,
                                                             (jlong)auTimestamp, (jlong)auTimestampShifted, (jint)auSyncType);

    if (newNativeDataInfos != NULL)
    {
        jlong *array = (*env)->GetLongArrayElements(env, newNativeDataInfos, NULL);
        ret = (int)array[0];
        (*env)->ReleaseLongArrayElements(env, newNativeDataInfos, array, 0);
        (*env)->DeleteLocalRef(env, newNativeDataInfos);
    }

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}


JNIEXPORT void JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeRunFilterThread(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    BEAVER_JNI_ReaderFilter_t *readerFilter = (BEAVER_JNI_ReaderFilter_t*)(intptr_t)cBeaverFilter;
    BEAVER_Filter_RunFilterThread((void*)readerFilter->beaverFilter);
}


JNIEXPORT void JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeRunStreamThread(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    BEAVER_JNI_ReaderFilter_t *readerFilter = (BEAVER_JNI_ReaderFilter_t*)(intptr_t)cBeaverFilter;
    ARSTREAM_Reader2_RunStreamThread((void*)readerFilter->reader);
}


JNIEXPORT void JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeRunControlThread(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    BEAVER_JNI_ReaderFilter_t *readerFilter = (BEAVER_JNI_ReaderFilter_t*)(intptr_t)cBeaverFilter;
    ARSTREAM_Reader2_RunControlThread((void*)readerFilter->reader);
}


JNIEXPORT void JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeStop(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    BEAVER_JNI_ReaderFilter_t *readerFilter = (BEAVER_JNI_ReaderFilter_t*)(intptr_t)cBeaverFilter;
    ARSTREAM_Reader2_StopReader(readerFilter->reader);
    BEAVER_Filter_Stop(readerFilter->beaverFilter);
}


JNIEXPORT jboolean JNICALL
Java_com_parrot_beaver_BeaverReaderFilter_nativeDispose(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    jboolean retVal = JNI_TRUE;
    BEAVER_JNI_ReaderFilter_t *readerFilter = (BEAVER_JNI_ReaderFilter_t*)(intptr_t)cBeaverFilter;

    eARSTREAM_ERROR err = ARSTREAM_Reader2_Delete(&readerFilter->reader);
    if (err != ARSTREAM_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to delete reader: %s", ARSTREAM_Error_ToString(err));
        retVal = JNI_FALSE;
    }

    int ret = BEAVER_Filter_Free(&readerFilter->beaverFilter);
    if (ret != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_READERFILTER_TAG, "Unable to delete beaver_filter: %d", ret);
        retVal = JNI_FALSE;
    }

    if ((retVal == JNI_TRUE) && (readerFilter->thizz != NULL))
    {
        (*env)->DeleteGlobalRef(env, readerFilter->thizz);
        free(readerFilter);
    }

    return retVal;
}

