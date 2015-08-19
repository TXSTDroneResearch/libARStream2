LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CATEGORY_PATH := dragon/libs
LOCAL_MODULE := libBeaver
LOCAL_DESCRIPTION := H.264 Elementary Stream Tools Library

LOCAL_LIBRARIES := libARSAL libARStream

LOCAL_INSTALL_HEADERS := \
    include/beaver/beaver.h:usr/include/beaver/ \
    include/beaver/beaver_filter.h:usr/include/beaver/ \
    include/beaver/beaver_parser.h:usr/include/beaver/ \
    include/beaver/beaver_parrot.h:usr/include/beaver/

LOCAL_CFLAGS := -Wextra
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_SRC_FILES := src/beaver_filter.c src/beaver_parser.c src/beaver_parrot.c
include $(BUILD_LIBRARY)

