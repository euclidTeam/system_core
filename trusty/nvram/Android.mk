#
# Copyright (C) 2016 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

# nvram.trusty is the Trusty NVRAM HAL module.
include $(CLEAR_VARS)
LOCAL_MODULE := nvram.trusty
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := \
	module.c \
	nvram_ipc.cpp \
	trusty_nvram.cpp
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wall -Werror -Wextra -fvisibility=hidden
LOCAL_SHARED_LIBRARIES := libbase liblog libtrusty libnvram
include $(BUILD_SHARED_LIBRARY)

# nvram_client implements a command-line utility to access the
# access-controlled NVRAM Trusty app.
include $(CLEAR_VARS)
LOCAL_MODULE := nvram_client
LOCAL_SRC_FILES := \
	nvram_client.cpp \
	nvram_ipc.cpp
LOCAL_CFLAGS := -Wall -Werror -Wextra
LOCAL_SHARED_LIBRARIES := libbase liblog libtrusty libnvram
include $(BUILD_EXECUTABLE)
