LOCAL_PATH := $(call my-dir)

#Prebuilt libraries
include $(CLEAR_VARS)
LOCAL_MODULE := aospPdfium

ARCH_PATH = $(TARGET_ARCH_ABI)

LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/$(ARCH_PATH)/libpdfium.cr.so

include $(PREBUILT_SHARED_LIBRARY)

#c++_shared
include $(CLEAR_VARS)
LOCAL_MODULE := libmodc++_shared

LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/$(ARCH_PATH)/libc++.cr.so

include $(PREBUILT_SHARED_LIBRARY)

#libchrome_zlib
include $(CLEAR_VARS)
LOCAL_MODULE := libchrome_zlib

LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/$(ARCH_PATH)/libchrome_zlib.cr.so

include $(PREBUILT_SHARED_LIBRARY)

#libicui18n
include $(CLEAR_VARS)
LOCAL_MODULE := libicui18n

LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/$(ARCH_PATH)/libicui18n.cr.so

include $(PREBUILT_SHARED_LIBRARY)

#libicuuc
include $(CLEAR_VARS)
LOCAL_MODULE := libicuuc

LOCAL_SRC_FILES := $(LOCAL_PATH)/lib/$(ARCH_PATH)/libicuuc.cr.so

include $(PREBUILT_SHARED_LIBRARY)

#Main JNI library
include $(CLEAR_VARS)
LOCAL_MODULE := jniPdfium

LOCAL_CFLAGS += -DHAVE_PTHREADS
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_SHARED_LIBRARIES += aospPdfium
LOCAL_LDLIBS += -llog -landroid -ljnigraphics

LOCAL_SRC_FILES :=  $(LOCAL_PATH)/src/mainJNILib.cpp

include $(BUILD_SHARED_LIBRARY)
