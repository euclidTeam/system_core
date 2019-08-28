// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>

#include <android-base/macros.h>
#include <libdm/dm.h>
#include <libfiemap/image_manager.h>

namespace android {
namespace snapshot {

bool UnmapImageIfExists(android::fiemap::IImageManager* manager, const std::string& name);

struct AutoDevice {
    AutoDevice(AutoDevice&& other);
    virtual ~AutoDevice() {};
    virtual void Release();
   protected:
    AutoDevice(const std::string& name) : name_(name) {}
    std::string name_;
   private:
    DISALLOW_COPY_AND_ASSIGN(AutoDevice);
};

// A list of devices we created along the way.
// - Whenever a device is created that is subject to GC'ed at the end of
//   this function, add it to this list.
// - If any error has occured, the list is destroyed, and all these devices
//   are cleaned up.
// - Upon success, Release() should be called so that the created devices
//   are kept.
struct AutoDevices {
    ~AutoDevices();
    template<typename T, typename... Args>
    void EmplaceBack(Args&&... args) {
        devices_.emplace_back(std::make_unique<T>(args...));
    }
    void Release();
   private:
    std::vector<std::unique_ptr<AutoDevice>> devices_;
};

// Automatically unmap a device upon deletion.
struct AutoUnmapDevice : AutoDevice {
    // On destruct, delete |name| from device mapper.
    AutoUnmapDevice(android::dm::DeviceMapper* dm, const std::string& name) : AutoDevice(name), dm_(dm) {}
    AutoUnmapDevice(AutoUnmapDevice&& other) = default;
    ~AutoUnmapDevice();
   private:
    DISALLOW_COPY_AND_ASSIGN(AutoUnmapDevice);
    android::dm::DeviceMapper* dm_ = nullptr;
};

// Automatically unmap an image upon deletion.
struct AutoUnmapImage : AutoDevice {
    // On destruct, delete |name| from image manager.
    AutoUnmapImage(android::fiemap::IImageManager* images, const std::string& name) : AutoDevice(name), images_(images) {}
    AutoUnmapImage(AutoUnmapImage&& other) = default;
    ~AutoUnmapImage();
   private:
    DISALLOW_COPY_AND_ASSIGN(AutoUnmapImage);
    android::fiemap::IImageManager* images_ = nullptr;
};

}  // namespace snapshot
}  // namespace android
