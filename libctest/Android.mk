LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libctest
LOCAL_SRC_FILES := ctest.c
LOCAL_CFLAGS := -Werror

include $(BUILD_SHARED_LIBRARY)
