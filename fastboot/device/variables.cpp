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

#include "variables.h"

#include <dirent.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <ext4_utils/ext4_utils.h>
#include <liblp/liblp.h>

#include "fastboot_device.h"
#include "flashing.h"
#include "utility.h"

using ::android::hardware::boot::V1_0::BoolResult;
using ::android::hardware::boot::V1_0::Slot;
using namespace android::fs_mgr;

constexpr int kMaxDownloadSizeDefault = 0x20000000;
constexpr char kFastbootProtocolVersion[] = "0.4";

bool GetVersion(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                std::string* message) {
    *message = kFastbootProtocolVersion;
    return true;
}

bool GetBootloaderVersion(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                          std::string* message) {
    *message = android::base::GetProperty("ro.bootloader", "");
    return true;
}

bool GetBasebandVersion(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                        std::string* message) {
    *message = android::base::GetProperty("ro.build.expect.baseband", "");
    return true;
}

bool GetProduct(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                std::string* message) {
    *message = android::base::GetProperty("ro.product.device", "");
    return true;
}

bool GetSerial(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
               std::string* message) {
    *message = android::base::GetProperty("ro.serialno", "");
    return true;
}

bool GetSecure(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
               std::string* message) {
    *message = android::base::GetBoolProperty("ro.secure", "") ? "yes" : "no";
    return true;
}

bool GetCurrentSlot(FastbootDevice* device, const std::vector<std::string>& /* args */,
                    std::string* message) {
    std::string suffix = device->GetCurrentSlot();
    *message = suffix.size() == 2 ? suffix.substr(1) : suffix;
    return true;
}

bool GetSlotCount(FastbootDevice* device, const std::vector<std::string>& /* args */,
                  std::string* message) {
    auto boot_control_hal = device->boot_control_hal();
    if (!boot_control_hal) {
        *message = "0";
    } else {
        *message = std::to_string(boot_control_hal->getNumberSlots());
    }
    return true;
}

bool GetSlotSuccessful(FastbootDevice* device, const std::vector<std::string>& args,
                       std::string* message) {
    if (args.empty()) {
        *message = "Missing argument";
        return false;
    }
    Slot slot;
    if (!GetSlotNumber(args[0], &slot)) {
        *message = "Invalid slot";
        return false;
    }
    auto boot_control_hal = device->boot_control_hal();
    if (!boot_control_hal) {
        *message = "Device has no slots";
        return false;
    }
    if (boot_control_hal->isSlotMarkedSuccessful(slot) != BoolResult::TRUE) {
        *message = "no";
    } else {
        *message = "yes";
    }
    return true;
}

bool GetSlotUnbootable(FastbootDevice* device, const std::vector<std::string>& args,
                       std::string* message) {
    if (args.empty()) {
        *message = "Missing argument";
        return false;
    }
    Slot slot;
    if (!GetSlotNumber(args[0], &slot)) {
        *message = "Invalid slot";
        return false;
    }
    auto boot_control_hal = device->boot_control_hal();
    if (!boot_control_hal) {
        *message = "Device has no slots";
        return false;
    }
    if (boot_control_hal->isSlotBootable(slot) != BoolResult::TRUE) {
        *message = "yes";
    } else {
        *message = "no";
    }
    return true;
}

bool GetMaxDownloadSize(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                        std::string* message) {
    *message = std::to_string(kMaxDownloadSizeDefault);
    return true;
}

bool GetUnlocked(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                 std::string* message) {
    *message = "yes";
    return true;
}

bool GetHasSlot(FastbootDevice* device, const std::vector<std::string>& args,
                std::string* message) {
    if (args.empty()) {
        *message = "Missing argument";
        return false;
    }
    std::string slot_suffix = device->GetCurrentSlot();
    if (slot_suffix.empty()) {
        *message = "no";
        return true;
    }
    std::string partition_name = args[0] + slot_suffix;
    if (FindPhysicalPartition(partition_name) ||
        LogicalPartitionExists(partition_name, slot_suffix)) {
        *message = "yes";
    } else {
        *message = "no";
    }
    return true;
}

bool GetPartitionSize(FastbootDevice* device, const std::vector<std::string>& args,
                      std::string* message) {
    if (args.size() < 1) {
        *message = "Missing argument";
        return false;
    }
    // Zero-length partitions cannot be created through device-mapper, so we
    // special case them here.
    bool is_zero_length;
    if (LogicalPartitionExists(args[0], device->GetCurrentSlot(), &is_zero_length) &&
        is_zero_length) {
        *message = "0";
        return true;
    }
    // Otherwise, open the partition as normal.
    PartitionHandle handle;
    if (!OpenPartition(device, args[0], &handle)) {
        *message = "Could not open partition";
        return false;
    }
    uint64_t size = get_block_device_size(handle.fd());
    *message = android::base::StringPrintf("%" PRIX64, size);
    return true;
}

bool GetPartitionIsLogical(FastbootDevice* device, const std::vector<std::string>& args,
                           std::string* message) {
    if (args.size() < 1) {
        *message = "Missing argument";
        return false;
    }
    // Note: if a partition name is in both the GPT and the super partition, we
    // return "true", to be consistent with prefering to flash logical partitions
    // over physical ones.
    std::string partition_name = args[0];
    if (LogicalPartitionExists(partition_name, device->GetCurrentSlot())) {
        *message = "yes";
        return true;
    }
    if (FindPhysicalPartition(partition_name)) {
        *message = "no";
        return true;
    }
    *message = "Partition not found";
    return false;
}

bool GetIsUserspace(FastbootDevice* /* device */, const std::vector<std::string>& /* args */,
                    std::string* message) {
    *message = "yes";
    return true;
}

std::vector<std::vector<std::string>> GetAllPartitions(FastbootDevice* device) {
    std::vector<std::vector<std::string>> partitions;

    // First get physical partitions.
    struct dirent* de;
    std::unique_ptr<DIR, decltype(&closedir)> by_name(opendir("/dev/block/by-name"), closedir);
    while ((de = readdir(by_name.get())) != nullptr) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        struct stat s;
        std::string path = "/dev/block/by-name/" + std::string(de->d_name);
        if (!stat(path.c_str(), &s) && S_ISBLK(s.st_mode)) {
            partitions.emplace_back(std::initializer_list<std::string>{de->d_name});
        }
    }

    // Next get logical partitions.
    if (auto path = FindPhysicalPartition(LP_METADATA_PARTITION_NAME)) {
        uint32_t slot_number = SlotNumberForSlotSuffix(device->GetCurrentSlot());
        if (auto metadata = ReadMetadata(path->c_str(), slot_number)) {
            for (const auto& partition : metadata->partitions) {
                std::string partition_name = GetPartitionName(partition);
                partitions.emplace_back(std::initializer_list<std::string>{partition_name});
            }
        }
    }

    return partitions;
}
