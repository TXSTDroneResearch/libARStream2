LOCAL_PATH := $(call my-dir)

# JNI Wrapper
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp -mfpu=neon
endif
LOCAL_MODULE := libarstream2_android
LOCAL_SRC_FILES := JNI/c/arstream2_stream_receiver_jni.c
LOCAL_LDLIBS := -llog -lz
LOCAL_SHARED_LIBRARIES := libARStream2-prebuilt libARSAL-prebuilt
include $(BUILD_SHARED_LIBRARY)
