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

#pragma once

#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <android-base/unique_fd.h>
#include <android/hardware/boot/1.0/IBootControl.h>
#include <ext4_utils/ext4_utils.h>

#include "commands.h"
#include "transport.h"
#include "variables.h"

class FastbootDevice;

using android::hardware::boot::V1_0::IBootControl;

using android::sp;

// Logical partitions are only mapped to a block device as needed, and
// immediately unmapped when no longer needed. In order to enforce this we
// require accessing partitions through a Handle abstraction, which may perform
// additional operations after closing its file description.
class PartitionHandle {
  public:
    PartitionHandle() {}
    PartitionHandle(android::base::unique_fd&& fd, std::function<void()>&& closer)
        : fd_(std::move(fd)), closer_(std::move(closer)) {}
    PartitionHandle(const PartitionHandle&) = delete;
    PartitionHandle(PartitionHandle&&) = default;
    PartitionHandle& operator=(const PartitionHandle&) = delete;
    PartitionHandle& operator=(PartitionHandle&&) = default;
    ~PartitionHandle() {
        if (closer_) {
            // Make sure the device is closed first.
            fd_ = {};
            closer_();
        }
    }
    int fd() const { return fd_.get(); }

  private:
    android::base::unique_fd fd_;
    std::function<void()> closer_;
};

class FastbootDevice {
  public:
    FastbootDevice();
    ~FastbootDevice();

    void CloseDevice();
    void ExecuteCommands();
    bool WriteStatus(FastbootResult result, const std::string& message);
    bool HandleData(bool read, std::vector<char>* data);
    std::optional<std::string> GetVariable(const std::string& var,
                                           const std::vector<std::string>& args);

    std::vector<char>& get_download_data() { return download_data_; }
    void set_upload_data(const std::vector<char>& data) { upload_data_ = data; }
    void set_upload_data(std::vector<char>&& data) { upload_data_ = std::move(data); }
    Transport* get_transport() { return transport_.get(); }
    sp<IBootControl> boot_control_module() { return boot_control_module_; }

    std::string GetCurrentSlot();
    bool OpenPartition(const std::string& name, PartitionHandle* handle);
    int Flash(const std::string& name);


  private:
    const std::unordered_map<std::string, CommandHandler> kCommandMap;

    std::unique_ptr<Transport> transport_;

    sp<IBootControl> boot_control_module_;
    std::vector<char> download_data_;
    std::vector<char> upload_data_;

    const std::unordered_map<std::string, VariableHandler> variables_map;
    std::future<int> flash_thread_;
};
