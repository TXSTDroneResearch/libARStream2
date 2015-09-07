/**
 * @file beaver_jni_readerfilter.c
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

#include <jni.h>
#include <libBeaver/beaver_readerfilter.h>
#include <libARStream/ARSTREAM_Reader2.h>
#include <libARSAL/ARSAL_Print.h>


#define BEAVER_JNI_TAG "BeaverJNI"

static jmethodID g_onSpsPpsReady = 0;
static jmethodID g_getFreeBufferIdx = 0;
static jmethodID g_getBuffer = 0;
static jmethodID g_onBufferReady = 0;
static JavaVM *g_vm = NULL;


// ---------------------------------------
// BeaverManager
// ---------------------------------------

JNIEXPORT jlong JNICALL
Java_com_parrot_arsdk_beaver_BeaverManager_nativeInit(JNIEnv *env, jobject thizz, jstring serverAddress, jint serverStreamPort, jint serverControlPort,
    jint clientStreamPort, jint clientControlPort, jint maxPacketSize, jint maxBitrate, jint maxLatency, jint maxNetworkLatency,
    jint auFifoSize)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverManager_nativeInit");
    BEAVER_ReaderFilter_Config_t config;
    memset(&config, 0, sizeof(BEAVER_ReaderFilter_Config_t));

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
    config.auFifoSize = auFifoSize;
    config.waitForSync = 1;
    config.outputIncompleteAu = 1;
    config.filterOutSpsPps = 1;
    config.filterOutSei = 1;
    config.replaceStartCodesWithNaluSize = 0;
    config.generateSkippedPSlices = 1;
    config.generateFirstGrayIFrame = 1;

    BEAVER_ReaderFilter_Handle readerFilterHandle = 0;
    int result = BEAVER_ReaderFilter_Init(&readerFilterHandle, &config);

    (*env)->ReleaseStringUTFChars(env, serverAddress, c_serverAddress);

    if (result != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error in BEAVER_ReaderFilter_Init %d", result);
        return (jlong)(intptr_t)NULL;
    }

    return (jlong)(intptr_t)readerFilterHandle;
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverManager_nativeStop(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverManager_nativeStop");
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;
    BEAVER_ReaderFilter_Stop(readerFilterHandle);
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_beaver_BeaverManager_nativeFree(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverManager_nativeFree");
    jboolean retVal = JNI_TRUE;
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;

    int err = BEAVER_ReaderFilter_Free(&readerFilterHandle);
    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to delete reader filter: %d", err);
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverManager_nativeRunFilterThread(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverManager_nativeRunFilterThread");
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;
    BEAVER_ReaderFilter_RunFilterThread((void*)readerFilterHandle);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverManager_nativeRunStreamThread(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverManager_nativeRunStreamThread");
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;
    BEAVER_ReaderFilter_RunStreamThread((void*)readerFilterHandle);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverManager_nativeRunControlThread(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverManager_nativeRunControlThread");
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;
    BEAVER_ReaderFilter_RunControlThread((void*)readerFilterHandle);
}


// ---------------------------------------
// BeaverReceiver
// ---------------------------------------

static int BEAVER_JNI_ReaderFilter_SpsPpsCallback(uint8_t *spsBuffer, int spsSize, uint8_t *ppsBuffer, int ppsSize, void *thizz);
static int BEAVER_JNI_ReaderFilter_GetAuBufferCallback(uint8_t **auBuffer, int *auBufferSize, void **auBufferUserPtr, void *thizz);
static int BEAVER_JNI_ReaderFilter_AuReadyCallback(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, BEAVER_Filter_AuSyncType_t auSyncType, void *auBufferUserPtr, void *userPtr);

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverReceiver_nativeInitClass(JNIEnv *env, jclass clazz)
{
    jint res = (*env)->GetJavaVM(env, &g_vm);
    if (res < 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to get JavaVM pointer");
    }

    g_onSpsPpsReady = (*env)->GetMethodID(env, clazz, "onSpsPpsReady", "(Ljava/nio/ByteBuffer;Ljava/nio/ByteBuffer;)I");
    if (!g_onSpsPpsReady)
    {
         ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to find method onSpsPpsReady");
    }
    g_getFreeBufferIdx = (*env)->GetMethodID(env, clazz, "getFreeBufferIdx", "()I");
    if (!g_getFreeBufferIdx)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to find method getFreeBufferIdx");
    }
    g_getBuffer = (*env)->GetMethodID(env, clazz, "getBuffer", "(I)Ljava/nio/ByteBuffer;");
    if (!g_getBuffer)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to find method getBuffer");
    }
    g_onBufferReady = (*env)->GetMethodID(env, clazz, "onBufferReady", "(IIJJI)I");
    if (!g_onBufferReady)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to find method onBufferReady");
    }
}

JNIEXPORT jlong JNICALL
Java_com_parrot_arsdk_beaver_BeaverReceiver_nativeInit(JNIEnv *env, jobject thizz)
{
    return (jlong)(intptr_t)(*env)->NewGlobalRef(env, thizz);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverReceiver_nativeFree(JNIEnv *env, jobject thizz, jlong ref)
{
    jobject gthizz = (jobject)ref;
    (*env)->DeleteGlobalRef(env, gthizz);
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_beaver_BeaverReceiver_nativeStart(JNIEnv *env, jobject thizz, jlong cReaderFilter, jlong thisRef)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverReceiver_nativeStart");
    jobject gthizz = (jobject)thisRef;
    jboolean retVal = JNI_TRUE;
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;

    int err = BEAVER_ReaderFilter_StartFilter(readerFilterHandle,
            &BEAVER_JNI_ReaderFilter_SpsPpsCallback, (void*)gthizz,
            &BEAVER_JNI_ReaderFilter_GetAuBufferCallback, (void*)gthizz,
            &BEAVER_JNI_ReaderFilter_AuReadyCallback, (void*)gthizz);

    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to delete start filter: %d", err);
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_beaver_BeaverReceiver_nativeStop(JNIEnv *env, jobject thizz, jlong cReaderFilter)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverReceiver_nativeStop");
    jboolean retVal = JNI_TRUE;
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;

    int err = BEAVER_ReaderFilter_PauseFilter(readerFilterHandle);

    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to delete start filter: %d", err);
        retVal = JNI_FALSE;
    }

    return retVal;
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
    }

    jobject spsByteBuffer = (*env)->NewDirectByteBuffer(env, spsBuffer, spsSize);
    if (spsByteBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error allocationg sps byte buffer");
        return -1;
    }
    jobject ppsByteBuffer = (*env)->NewDirectByteBuffer(env, ppsBuffer, ppsSize);
    if (ppsByteBuffer == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error allocationg pps byte buffer");
        (*env)->DeleteLocalRef(env, spsByteBuffer);
        return -1;
    }

    ret = (*env)->CallIntMethod(env, (jobject)thizz, g_onSpsPpsReady, spsByteBuffer, ppsByteBuffer);

    (*env)->DeleteLocalRef(env, spsByteBuffer);
    (*env)->DeleteLocalRef(env, ppsByteBuffer);

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}


static int BEAVER_JNI_ReaderFilter_GetAuBufferCallback(uint8_t **auBuffer, int *auBufferSize, void **auBufferUserPtr, void *userPtr)
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error while getting buffer idx  %d", bufferIdx);
        }
    }

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}

static int BEAVER_JNI_ReaderFilter_AuReadyCallback(uint8_t *auBuffer, int auSize, uint64_t auTimestamp, uint64_t auTimestampShifted, BEAVER_Filter_AuSyncType_t auSyncType, void *auBufferUserPtr, void *userPtr)
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
            ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to attach thread to VM");
            return -1;
        }
    }
    else if (envStatus != JNI_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error %d while getting JNI Environment", envStatus);
        return -1;
    }

    ret = (*env)->CallIntMethod(env, (jobject)userPtr, g_onBufferReady, (jint)auBufferUserPtr, (jint)auSize,
            (jlong)auTimestamp, (jlong)auTimestampShifted, (jint)auSyncType);

    if (wasAlreadyAttached == 0)
    {
        (*g_vm)->DetachCurrentThread(g_vm);
    }

    return ret;
}


// ---------------------------------------
// BeaverResender
// ---------------------------------------

JNIEXPORT jlong JNICALL
Java_com_parrot_arsdk_beaver_BeaverResender_nativeInit(JNIEnv *env, jobject thizz, jlong cReaderFilter, jstring clientAddress,
        jint serverStreamPort, jint serverControlPort, jint clientStreamPort, jint clientControlPort,
        jint maxPacketSize, jint targetPacketSize, jint maxLatency, jint maxNetworkLatency)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "BeaverResender_nativeInit");
    jboolean retVal = JNI_TRUE;
    BEAVER_ReaderFilter_Handle readerFilterHandle = (BEAVER_ReaderFilter_Handle)(intptr_t)cReaderFilter;

    BEAVER_ReaderFilter_ResenderConfig_t config;
    memset(&config, 0, sizeof(BEAVER_ReaderFilter_ResenderConfig_t));

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

    BEAVER_ReaderFilter_ResenderHandle resenderFilterHandle = 0;
    int result = BEAVER_ReaderFilter_InitResender(readerFilterHandle, &resenderFilterHandle, &config);
    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "resenderFilterHandle %d", resenderFilterHandle);

    (*env)->ReleaseStringUTFChars(env, clientAddress, c_clientAddress);

    if (result != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Error in BEAVER_ReaderFilter_InitResender %d", result);
        return (jlong)(intptr_t)NULL;
    }

    return (jlong)(intptr_t)resenderFilterHandle;

}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_beaver_BeaverResender_nativeStop(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Java_com_parrot_arsdk_beaver_BeaverResender_nativeStop: %d", cResender);
    jboolean retVal = JNI_TRUE;
    BEAVER_ReaderFilter_ResenderHandle resenderHandle = (BEAVER_ReaderFilter_ResenderHandle)(intptr_t)cResender;

    int err = BEAVER_ReaderFilter_StopResender(resenderHandle);

    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to stop resender: %d", err);
        retVal = JNI_FALSE;
    }

    return retVal;
}

JNIEXPORT jboolean JNICALL
Java_com_parrot_arsdk_beaver_BeaverResender_nativeFree(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Java_com_parrot_arsdk_beaver_BeaverResender_nativeFree: %d", cResender);
    jboolean retVal = JNI_TRUE;
    BEAVER_ReaderFilter_ResenderHandle resenderHandle = (BEAVER_ReaderFilter_ResenderHandle)(intptr_t)cResender;

    int err = BEAVER_ReaderFilter_FreeResender(&resenderHandle);
    if (err != 0)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, BEAVER_JNI_TAG, "Unable to delete resender: %d", err);
        retVal = JNI_FALSE;
    }
    return retVal;
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverResender_nativeRunStreamThread(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "Java_com_parrot_arsdk_beaver_BeaverResender_nativeRunStreamThread %d", cResender);
    BEAVER_ReaderFilter_ResenderHandle resenderHandle = (BEAVER_ReaderFilter_ResenderHandle)(intptr_t)cResender;
    BEAVER_ReaderFilter_RunResenderStreamThread((void*)resenderHandle);
}

JNIEXPORT void JNICALL
Java_com_parrot_arsdk_beaver_BeaverResender_nativeRunControlThread(JNIEnv *env, jobject thizz, jlong cResender)
{
    ARSAL_PRINT(ARSAL_PRINT_VERBOSE, BEAVER_JNI_TAG, "Java_com_parrot_arsdk_beaver_BeaverResender_nativeRunControlThread %d", cResender);
    BEAVER_ReaderFilter_ResenderHandle resenderHandle = (BEAVER_ReaderFilter_ResenderHandle)(intptr_t)cResender;
    BEAVER_ReaderFilter_RunResenderControlThread((void*)resenderHandle);
}


