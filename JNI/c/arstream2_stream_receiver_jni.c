/**
 * @file arstream2_stream_receiver_jni.c
 * @brief Parrot Streaming Library - Stream Receiver JNI
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <jni.h>
#include <libARStream2/arstream2_stream_receiver.h>
#include <libARSAL/ARSAL_Print.h>


#define ARSTREAM2_STREAM_RECEIVER_JNI_TAG "ARSTREAM2_StreamReceiver_JNI"

static jmethodID g_onSpsPpsReady = 0;
static jmethodID g_getFreeBufferIdx = 0;
static jmethodID g_getBuffer = 0;
static jmethodID g_onBufferReady = 0;
static JavaVM *g_vm = NULL;


// ---------------------------------------
// ARStream2Manager
// ---------------------------------------

JNIEXPORT jlong JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Manager_nativeInit(JNIEnv *env, jobject thizz, jstring serverAddress, jint serverStreamPort, jint serverControlPort,
    jint clientStreamPort, jint clientControlPort, jint maxPacketSize, jint maxBitrate, jint maxLatency, jint maxNetworkLatency)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Manager_nativeInit");
    ARSTREAM2_StreamReceiver_Config_t config;
    memset(&config, 0, sizeof(ARSTREAM2_StreamReceiver_Config_t));

    const char *c_serverAddress = (*env)->GetStringUTFChars(env, serverAddress, NULL);

    config.serverAddr = c_serverAddress;
    config.mcastAddr = NULL;
    config.mcastIfaceAddr = NULL;
    config.serverStreamPort = serverStreamPort;
    config.serverControlPort = serverControlPort;
    config.clientStreamPort = clientStreamPort;
    config.clientControlPort = clientControlPort;
    config.maxPacketSize = maxPacketSize;
    config.maxBitrate = maxBitrate;
    config.maxLatencyMs = maxLatency;
    config.maxNetworkLatencyMs = maxNetworkLatency;
    config.waitForSync = 1;
    config.outputIncompleteAu = 1;
    config.filterOutSpsPps = 1;
    config.filterOutSei = 1;
    config.replaceStartCodesWithNaluSize = 0;
    config.generateSkippedPSlices = 1;
    config.generateFirstGrayIFrame = 1;

    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = 0;
    eARSTREAM2_ERROR result = ARSTREAM2_StreamReceiver_Init(&streamReceiverHandle, &config);

    (*env)->ReleaseStringUTFChars(env, serverAddress, c_serverAddress);

    if (result != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error in ARSTREAM2_StreamReceiver_Init(): %s", ARSTREAM2_Error_ToString(result));
        return (jlong)(intptr_t)NULL;
    }

    return (jlong)(intptr_t)streamReceiverHandle;
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Manager_nativeStop(JNIEnv *env, jobject thizz, jlong cStreamReceiver)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Manager_nativeStop");
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;
    ARSTREAM2_StreamReceiver_Stop(streamReceiverHandle);
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Manager_nativeFree(JNIEnv *env, jobject thizz, jlong cStreamReceiver)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Manager_nativeFree");
    jboolean retVal = JNI_TRUE;
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;

    eARSTREAM2_ERROR err = ARSTREAM2_StreamReceiver_Free(&streamReceiverHandle);
    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to delete StreamReceiver: %s", ARSTREAM2_Error_ToString(err));
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Manager_nativeRunFilterThread(JNIEnv *env, jobject thizz, jlong cStreamReceiver)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Manager_nativeRunFilterThread");
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;
    ARSTREAM2_StreamReceiver_RunFilterThread((void*)streamReceiverHandle);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Manager_nativeRunStreamThread(JNIEnv *env, jobject thizz, jlong cStreamReceiver)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Manager_nativeRunStreamThread");
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;
    ARSTREAM2_StreamReceiver_RunStreamThread((void*)streamReceiverHandle);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Manager_nativeRunControlThread(JNIEnv *env, jobject thizz, jlong cStreamReceiver)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Manager_nativeRunControlThread");
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;
    ARSTREAM2_StreamReceiver_RunControlThread((void*)streamReceiverHandle);
}


// ---------------------------------------
// ARStream2Receiver
// ---------------------------------------

static eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_JNI_SpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *thizz);
static eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_JNI_GetAuBufferCallback(uint8_t **auBuffer, int *auBufferSize, void **auBufferUserPtr, void *thizz);
static eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_JNI_AuReadyCallback(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, eARSTREAM2_H264_FILTER_AU_SYNC_TYPE auSyncType, void *auUserData, int auUserDataSize, void *auBufferUserPtr, void *userPtr);

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Receiver_nativeInitClass(JNIEnv *env, jclass clazz)
{
    jint res = (*env)->GetJavaVM(env, &g_vm);
    if (res < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to get JavaVM pointer");
    }

    g_onSpsPpsReady = (*env)->GetMethodID(env, clazz, "onSpsPpsReady", "(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)I");
    if (!g_onSpsPpsReady)
    {
         ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to find method onSpsPpsReady");
    }
    g_getFreeBufferIdx = (*env)->GetMethodID(env, clazz, "getFreeBufferIdx", "()I");
    if (!g_getFreeBufferIdx)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to find method getFreeBufferIdx");
    }
    g_getBuffer = (*env)->GetMethodID(env, clazz, "getBuffer", "(I)Ljava/nio/ByteBuffer;");
    if (!g_getBuffer)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to find method getBuffer");
    }
    g_onBufferReady = (*env)->GetMethodID(env, clazz, "onBufferReady", "(IIJJI)I");
    if (!g_onBufferReady)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to find method onBufferReady");
    }
}

JNIEXPORT jlong JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Receiver_nativeInit(JNIEnv *env, jobject thizz)
{
    return (jlong)(intptr_t)(*env)->NewGlobalRef(env, thizz);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Receiver_nativeFree(JNIEnv *env, jobject thizz, jlong ref)
{
    jobject gthizz = (jobject)ref;
    (*env)->DeleteGlobalRef(env, gthizz);
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Receiver_nativeStart(JNIEnv *env, jobject thizz, jlong cStreamReceiver, jlong thisRef)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Receiver_nativeStart");
    jobject gthizz = (jobject)thisRef;
    jboolean retVal = JNI_TRUE;
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;

    eARSTREAM2_ERROR err = ARSTREAM2_StreamReceiver_StartFilter(streamReceiverHandle,
            &ARSTREAM2_StreamReceiver_JNI_SpsPpsCallback, (void*)gthizz,
            &ARSTREAM2_StreamReceiver_JNI_GetAuBufferCallback, (void*)gthizz,
            &ARSTREAM2_StreamReceiver_JNI_AuReadyCallback, (void*)gthizz);

    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to delete start filter: %s", ARSTREAM2_Error_ToString(err));
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Receiver_nativeStop(JNIEnv *env, jobject thizz, jlong cStreamReceiver)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Receiver_nativeStop");
    jboolean retVal = JNI_TRUE;
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;

    eARSTREAM2_ERROR err = ARSTREAM2_StreamReceiver_PauseFilter(streamReceiverHandle);

    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to delete pause filter: %s", ARSTREAM2_Error_ToString(err));
        retVal = JNI_FALSE;
    }

    return retVal;
}


static eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_JNI_SpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *thizz)
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to attach thread to VM");
            return ARSTREAM2_ERROR_INVALID_STATE;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error %d while getting JNI Environment", envStatus);
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    jobject spsByteBuffer = (*env)->NewDirectByteBuffer(env, spsBuffer, spsSize);
    if (spsByteBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error allocationg sps byte buffer");
        return ARSTREAM2_ERROR_ALLOC;
    }
    jobject ppsByteBuffer = (*env)->NewDirectByteBuffer(env, ppsBuffer, ppsSize);
    if (ppsByteBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error allocationg pps byte buffer");
        (*env)->DeleteLocalRef(env, spsByteBuffer);
        return ARSTREAM2_ERROR_ALLOC;
    }

    ret = (*env)->CallIntMethod(env, (jobject)thizz, g_onSpsPpsReady, spsByteBuffer, ppsByteBuffer);

    (*env)->DeleteLocalRef(env, spsByteBuffer);
    (*env)->DeleteLocalRef(env, ppsByteBuffer);

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return (ret == 0) ? ARSTREAM2_OK : ARSTREAM2_ERROR_INVALID_STATE;
}


static eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_JNI_GetAuBufferCallback(uint8_t **auBuffer, int *auBufferSize, void **auBufferUserPtr, void *userPtr)
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to attach thread to VM");
            return ARSTREAM2_ERROR_INVALID_STATE;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error %d while getting JNI Environment", envStatus);
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    jint bufferIdx = (*env)->CallIntMethod(env, (jobject)userPtr, g_getFreeBufferIdx);
    if (bufferIdx >=0)
    {
        jobject byteBuffer = (*env)->CallObjectMethod(env, (jobject)userPtr, g_getBuffer, bufferIdx);
        if (byteBuffer != NULL)
        {
            *auBuffer = (*env)->GetDirectBufferAddress(env, byteBuffer);
            *auBufferSize = (*env)->GetDirectBufferCapacity(env, byteBuffer);
            *auBufferUserPtr = (void *)bufferIdx;
            ret = 0;
            (*env)->DeleteLocalRef(env, byteBuffer);
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error while getting buffer idx  %d", bufferIdx);
        }
    }

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return (ret == 0) ? ARSTREAM2_OK : ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE;
}

static eARSTREAM2_ERROR ARSTREAM2_StreamReceiver_JNI_AuReadyCallback(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, eARSTREAM2_H264_FILTER_AU_SYNC_TYPE auSyncType, void *auUserData, int auUserDataSize, void *auBufferUserPtr, void *userPtr)
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to attach thread to VM");
            return ARSTREAM2_ERROR_INVALID_STATE;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error %d while getting JNI Environment", envStatus);
        return ARSTREAM2_ERROR_INVALID_STATE;
    }

    ret = (*env)->CallIntMethod(env, (jobject)userPtr, g_onBufferReady, (jint)auBufferUserPtr, (jint)auSize,
            (jlong)auTimestamp, (jlong)auTimestampShifted, (jint)auSyncType);

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return (ret == 0) ? ARSTREAM2_OK : ARSTREAM2_ERROR_RESYNC_REQUIRED;
}


// ---------------------------------------
// ARStream2Resender
// ---------------------------------------

JNIEXPORT jlong JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeInit(JNIEnv *env, jobject thizz, jlong cStreamReceiver, jstring clientAddress,
        jint serverStreamPort, jint serverControlPort, jint clientStreamPort, jint clientControlPort,
        jint maxPacketSize, jint targetPacketSize, jint maxLatency, jint maxNetworkLatency)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "ARStream2Resender_nativeInit");
    jboolean retVal = JNI_TRUE;
    ARSTREAM2_StreamReceiver_Handle streamReceiverHandle = (ARSTREAM2_StreamReceiver_Handle)(intptr_t)cStreamReceiver;

    ARSTREAM2_StreamReceiver_ResenderConfig_t config;
    memset(&config, 0, sizeof(ARSTREAM2_StreamReceiver_ResenderConfig_t));

    const char *c_clientAddress = (*env)->GetStringUTFChars(env, clientAddress, NULL);

    config.clientAddr = c_clientAddress;
    config.mcastAddr = NULL;
    config.mcastIfaceAddr = NULL;
    config.serverStreamPort = serverStreamPort;
    config.serverControlPort = serverControlPort;
    config.clientStreamPort = clientStreamPort;
    config.clientControlPort = clientControlPort;
    config.maxPacketSize = maxPacketSize;
    config.targetPacketSize = targetPacketSize;
    config.maxLatencyMs = maxLatency;
    config.maxNetworkLatencyMs = maxNetworkLatency;

    ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle = 0;
    eARSTREAM2_ERROR result = ARSTREAM2_StreamReceiver_InitResender(streamReceiverHandle, &resenderHandle, &config);

    (*env)->ReleaseStringUTFChars(env, clientAddress, c_clientAddress);

    if (result != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Error in ARSTREAM2_StreamReceiver_InitResender(): %s", ARSTREAM2_Error_ToString(result));
        return (jlong)(intptr_t)NULL;
    }

    return (jlong)(intptr_t)resenderHandle;

}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeStop(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeStop: %d", cResender);
    jboolean retVal = JNI_TRUE;
    ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle = (ARSTREAM2_StreamReceiver_ResenderHandle)(intptr_t)cResender;

    eARSTREAM2_ERROR err = ARSTREAM2_StreamReceiver_StopResender(resenderHandle);

    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to stop resender: %s", ARSTREAM2_Error_ToString(err));
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeFree(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeFree: %d", cResender);
    jboolean retVal = JNI_TRUE;
    ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle = (ARSTREAM2_StreamReceiver_ResenderHandle)(intptr_t)cResender;

    eARSTREAM2_ERROR err = ARSTREAM2_StreamReceiver_FreeResender(&resenderHandle);

    if (err != ARSTREAM2_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Unable to delete resender: %s", ARSTREAM2_Error_ToString(err));
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeRunStreamThread(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeRunStreamThread %d", cResender);
    ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle = (ARSTREAM2_StreamReceiver_ResenderHandle)(intptr_t)cResender;
    ARSTREAM2_StreamReceiver_RunResenderStreamThread((void*)resenderHandle);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeRunControlThread(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, ARSTREAM2_STREAM_RECEIVER_JNI_TAG, "Java_com_parrot_arsdk_arstream2_ARStream2Resender_nativeRunControlThread %d", cResender);
    ARSTREAM2_StreamReceiver_ResenderHandle resenderHandle = (ARSTREAM2_StreamReceiver_ResenderHandle)(intptr_t)cResender;
    ARSTREAM2_StreamReceiver_RunResenderControlThread((void*)resenderHandle);
}
