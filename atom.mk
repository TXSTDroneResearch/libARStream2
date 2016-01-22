LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libARStream2
LOCAL_DESCRIPTION := Parrot Streaming Library
LOCAL_CATEGORY_PATH := dragon/libs

LOCAL_MODULE_FILENAME := libarstream2.so

LOCAL_LIBRARIES := libARSAL

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/Includes \
	$(LOCAL_PATH)/src

LOCAL_SRC_FILES := \
	src/arstream2_error.c \
	src/arstream2_h264_filter.c \
	src/arstream2_h264_parser.c \
	src/arstream2_h264_sei.c \
	src/arstream2_h264_writer.c \
	src/arstream2_rtp_receiver.c \
	src/arstream2_rtp_sender.c \
	src/arstream2_stream_receiver.c

LOCAL_INSTALL_HEADERS := \
	Includes/libARStream2/arstream2_error.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_h264_filter.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_h264_parser.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_h264_sei.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_h264_writer.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_rtp_receiver.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_rtp_sender.h:usr/include/libARStream2/ \
	Includes/libARStream2/arstream2_stream_receiver.h:usr/include/libARStream2/

include $(BUILD_LIBRARY)
