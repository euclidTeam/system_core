/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <linux/dm-ioctl.h>
#include <sys/ioctl.h>

#include <android-base/logging.h>
#include <android-base/macros.h>

#include <string>

#include "dm_device.h"

namespace android {
namespace dm {

const std::string& DmDevice::getName(void) const {
    return name_;
}

const DmTable& DmDevice::getTable(void) const {
    return table_;
}

DmDeviceState DmDevice::getState(void) const {
    return state_;
}

bool DmDevice::LoadTable(const DmTable& table) {
    UNUSED(table);
    return true;
}

}  // namespace dm
}  // namespace android
