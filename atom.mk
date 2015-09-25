LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CATEGORY_PATH := dragon/libs
LOCAL_MODULE := libBeaver
LOCAL_DESCRIPTION := H.264 Elementary Stream Tools Library

LOCAL_LIBRARIES := libARSAL libARStream

ifdef ARSDK_BUILD_FOR_APP

# Copy in build dir so bootstrap files are generated in build dir
LOCAL_AUTOTOOLS_COPY_TO_BUILD_DIR := 1

# Configure script is not at the root
LOCAL_AUTOTOOLS_CONFIGURE_SCRIPT := Build/configure

# Autotools variable
LOCAL_AUTOTOOLS_CONFIGURE_ARGS := \
	--with-libARSALInstallDir="" \
	--with-libARStreamInstallDir=""

ifeq ("$(TARGET_OS_FLAVOUR)","android")

LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-static \
	--enable-shared \
	--disable-so-version \
	LIBS=" -lm -lz"

else ifneq ($(filter iphoneos iphonesimulator, $(TARGET_OS_FLAVOUR)),)

LOCAL_AUTOTOOLS_CONFIGURE_ARGS += \
	--disable-shared \
	--enable-static \
	LIBS=" -lm -lz" \
	OBJCFLAGS=" -x objective-c -fobjc-arc -std=gnu99 $(TARGET_GLOBAL_CFLAGS)" \
	OBJC="$(TARGET_CC)" \
	CFLAGS=" -std=gnu99 -x c $(TARGET_GLOBAL_CFLAGS)"

endif

define LOCAL_AUTOTOOLS_CMD_POST_UNPACK
	$(Q) cd $(PRIVATE_SRC_DIR)/Build && ./bootstrap
endef

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/Includes
LOCAL_EXPORT_LDLIBS := -lbeaver

include $(BUILD_AUTOTOOLS)

else

LOCAL_INSTALL_HEADERS := \
    Includes/libBeaver/beaver_filter.h:usr/include/libBeaver/ \
    Includes/libBeaver/beaver_readerfilter.h:usr/include/libBeaver/ \
    Includes/libBeaver/beaver_writer.h:usr/include/libBeaver/ \
    Includes/libBeaver/beaver_parser.h:usr/include/libBeaver/ \
    Includes/libBeaver/beaver_parrot.h:usr/include/libBeaver/

LOCAL_CFLAGS := -Wextra
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/Includes
LOCAL_SRC_FILES := src/beaver_filter.c src/beaver_readerfilter.c src/beaver_parser.c src/beaver_writer.c src/beaver_parrot.c
include $(BUILD_LIBRARY)


ifeq ("$(TARGET_OS_FLAVOUR)","native")

include $(CLEAR_VARS)

LOCAL_MODULE := beavertool
LOCAL_DESCRIPTION := H.264 Elementary Stream Tools
LOCAL_CATEGORY_PATH := dragon
LOCAL_LIBRARIES := libBeaver
LOCAL_SRC_FILES := tools/beavertool.c

include $(BUILD_EXECUTABLE)

endif

endif
