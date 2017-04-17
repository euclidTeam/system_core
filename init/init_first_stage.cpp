/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "init_first_stage.h"

#include <stdlib.h>
#include <unistd.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "devices.h"
#include "fs_mgr.h"
#include "fs_mgr_avb.h"
#include "util.h"

class FirstStageMount {
  public:
    FirstStageMount();
    virtual ~FirstStageMount() = default;

    // The factory method to create either FirstStageMountVBootV1 or FirstStageMountVBootV2
    // based on device tree configurations.
    static std::unique_ptr<FirstStageMount> Create();
    bool DoFirstStageMount();  // Mounts fstab entries read from device tree.

  protected:
    void InitRequiredDevices();
    void InitVerityDevice(const std::string& verity_device);
    bool MountPartitions();

    virtual bool GetRequiredDevices() = 0;
    virtual bool SetUpDmVerity(fstab_rec* fstab_rec) = 0;

    bool need_dm_verity_;
    // Partition names (including A/B suffix) for device_init().
    std::set<std::string> device_init_partitions_;
    // Device tree fstab entries.
    std::unique_ptr<fstab, decltype(&fs_mgr_free_fstab)> device_tree_fstab_;
    // Eligible first stage mount candidates, only allow /vendor, /odm and /system.
    std::vector<fstab_rec*> mount_fstab_recs_;
};

class FirstStageMountVBootV1 : public FirstStageMount {
  public:
    FirstStageMountVBootV1() = default;
    ~FirstStageMountVBootV1() override = default;

  protected:
    bool GetRequiredDevices() override;
    bool SetUpDmVerity(fstab_rec* fstab_rec) override;
};

class FirstStageMountVBootV2 : public FirstStageMount {
  public:
    FirstStageMountVBootV2();
    ~FirstStageMountVBootV2() override = default;

  protected:
    bool GetRequiredDevices() override;
    bool SetUpDmVerity(fstab_rec* fstab_rec) override;
    bool InitAvbHandle();

    std::string device_tree_vbmeta_parts_;
    std::string device_tree_by_name_prefix_;
    FsManagerAvbUniquePtr avb_handle_;
};

FirstStageMount::FirstStageMount()
    : need_dm_verity_(false), device_tree_fstab_(fs_mgr_read_fstab_dt(), fs_mgr_free_fstab) {
    if (!device_tree_fstab_) {
        LOG(ERROR) << "Failed to read fstab from device tree";
        return;
    }
    // Searches fstab records for /vendor, /odm or /system.
    for (auto mount_point : {"/vendor", "/odm", "/system"}) {
        fstab_rec* fstab_rec =
            fs_mgr_get_entry_for_mount_point(device_tree_fstab_.get(), mount_point);
        if (fstab_rec != nullptr) {
            mount_fstab_recs_.push_back(fstab_rec);
        }
    }
}

std::unique_ptr<FirstStageMount> FirstStageMount::Create() {
    if (is_android_dt_value_expected("vbmeta/compatible", "android,vbmeta")) {
        return std::make_unique<FirstStageMountVBootV2>();
    } else {
        return std::make_unique<FirstStageMountVBootV1>();
    }
}

bool FirstStageMount::DoFirstStageMount() {
    // Nothing to mount.
    if (mount_fstab_recs_.empty()) return true;

    // Gets required partition names to device_init() in device_init_partitions_.
    // The partition name MUST have A/B suffix when A/B is used.
    if (!GetRequiredDevices()) return false;

    bool success = false;
    InitRequiredDevices();  // Creates the devices we need.

    // InitRequiredDevices() will remove found partitions from device_init_partitions_.
    // So if it isn't empty here, it means some partitions are not found.
    if (!device_init_partitions_.empty()) {
        LOG(ERROR) << __FUNCTION__ << "(): partition(s) not found: "
                   << android::base::Join(device_init_partitions_, ", ");
        goto done;
    }

    if (need_dm_verity_) {
        // Creates /dev/device-mapper.
        device_init("/sys/devices/virtual/misc/device-mapper",
                    [&](uevent* uevent) -> coldboot_action_t { return COLDBOOT_STOP; });
    }

    if (MountPartitions()) success = true;

done:
    device_close();
    return success;
}

// Creates devices with uevent->partition_name matching one in the device_init_partitions_.
// Notes that the partition_names here have A/B suffix when A/B is used.
// Found partitions will then be removed from the device_init_partitions_, so we can check
// which devices are NOT created.
void FirstStageMount::InitRequiredDevices() {
    if (device_init_partitions_.empty()) {
        return;
    }
    device_init(nullptr, [=](uevent* uevent) -> coldboot_action_t {
        // We need platform devices to create symlinks.
        if (uevent->subsystem == "platform") {
            return COLDBOOT_CREATE;
        }

        // Ignores everything that is not a block device.
        if (uevent->subsystem != "block") {
            return COLDBOOT_CONTINUE;
        }

        if (!uevent->partition_name.empty()) {
            // Matches partition names to create device nodes for partitions.
            // Both device_init_partitions_ and uevent->partition_name have A/B
            // suffix when A/B is used.
            auto iter = device_init_partitions_.find(uevent->partition_name);
            if (iter != device_init_partitions_.end()) {
                LOG(VERBOSE) << __FUNCTION__ << "(): found partition: " << *iter;
                device_init_partitions_.erase(iter);
                if (device_init_partitions_.empty()) {
                    return COLDBOOT_STOP;  // Found all partitions, stop coldboot.
                } else {
                    return COLDBOOT_CREATE;  // Creates this device and continue to find others.
                }
            }
        }
        // Not found a partition or find an unneeded partition, continue to find others.
        return COLDBOOT_CONTINUE;
    });
}

// Creates "/dev/block/dm-XX" for dm-verity by running coldboot on /sys/block/dm-XX.
void FirstStageMount::InitVerityDevice(const std::string& verity_device) {
    const std::string device_name(basename(verity_device.c_str()));
    const std::string syspath = "/sys/block/" + device_name;

    device_init(syspath.c_str(), [&](uevent* uevent) -> coldboot_action_t {
        if (uevent->device_name == device_name) {
            LOG(VERBOSE) << "Creating dm-verity device : " << verity_device;
            return COLDBOOT_STOP;
        }
        return COLDBOOT_CONTINUE;
    });
    device_close();
}

bool FirstStageMount::MountPartitions() {
    for (auto fstab_rec : mount_fstab_recs_) {
        if (!SetUpDmVerity(fstab_rec)) {
            PLOG(ERROR) << "Failed to setup verity for '" << fstab_rec->mount_point << "'";
            return false;
        }
        if (fs_mgr_do_mount_one(fstab_rec)) {
            PLOG(ERROR) << "Failed to mount '" << fstab_rec->mount_point << "'";
            return false;
        }
    }
    return true;
}

bool FirstStageMountVBootV1::GetRequiredDevices() {
    std::string meta_partition;

    for (auto fstab_rec : mount_fstab_recs_) {
        // Don't allow verifyatboot in the first stage.
        if (fs_mgr_is_verifyatboot(fstab_rec)) {
            LOG(ERROR) << "Partitions can't be verified at boot";
            return false;
        }
        // Checks for verified partitions.
        if (fs_mgr_is_verified(fstab_rec)) {
            need_dm_verity_ = true;
        }
        // Checks if verity metadata is on a separate partition and get partition
        // name from the end of the ->verity_loc path. Verity state is not partition
        // specific, so there must be only one additional partition that carries
        // verity state.
        if (fstab_rec->verity_loc) {
            if (!meta_partition.empty()) {
                LOG(ERROR) << "More than one meta partition found: " << meta_partition << ", "
                           << basename(fstab_rec->verity_loc);
                return false;
            } else {
                meta_partition = basename(fstab_rec->verity_loc);
            }
        }
    }

    // Includes those fstab partitions and meta_partition (if any).
    // Notes that fstab_rec->blk_device has A/B suffix updated by fs_mgr when A/B is used.
    for (auto fstab_rec : mount_fstab_recs_) {
        device_init_partitions_.emplace(basename(fstab_rec->blk_device));
    }

    if (!meta_partition.empty()) {
        device_init_partitions_.emplace(std::move(meta_partition));
    }

    return true;
}

bool FirstStageMountVBootV1::SetUpDmVerity(fstab_rec* fstab_rec) {
    if (fs_mgr_is_verified(fstab_rec)) {
        int ret = fs_mgr_setup_verity(fstab_rec, false /* wait_for_verity_dev */);
        if (ret == FS_MGR_SETUP_VERITY_DISABLED) {
            LOG(INFO) << "Verity disabled for '" << fstab_rec->mount_point << "'";
        } else if (ret == FS_MGR_SETUP_VERITY_SUCCESS) {
            // The exact block device name (fstab_rec->blk_device) is changed to "/dev/block/dm-XX".
            // Needs to create it because ueventd isn't started in init first stage.
            InitVerityDevice(fstab_rec->blk_device);
        } else {
            return false;
        }
    }
    return true;  // Returns true to mount the partition.
}

// FirstStageMountVBootV2 constructor.
// Gets the vbmeta configurations from device tree.
// Specifically, the 'parts' and 'by_name_prefix' below.
// /{
//     firmware {
//         android {
//             vbmeta {
//                 compatible = "android,vbmeta";
//                 parts = "vbmeta,boot,system,vendor"
//                 by_name_prefix = "/dev/block/platform/soc.0/f9824900.sdhci/by-name/"
//             };
//         };
//     };
//  }
FirstStageMountVBootV2::FirstStageMountVBootV2() : avb_handle_(nullptr) {
    if (!read_android_dt_file("vbmeta/parts", &device_tree_vbmeta_parts_)) {
        PLOG(ERROR) << "Failed to read vbmeta/parts from device tree";
        return;
    }

    if (!read_android_dt_file("vbmeta/by_name_prefix", &device_tree_by_name_prefix_)) {
        PLOG(ERROR) << "Failed to read vbmeta/by_name_prefix from dt";
        return;
    }
}

bool FirstStageMountVBootV2::GetRequiredDevices() {
    // fstab_rec->blk_device has A/B suffix.
    for (auto fstab_rec : mount_fstab_recs_) {
        if (fs_mgr_is_avb(fstab_rec)) {
            need_dm_verity_ = true;
        }
        device_init_partitions_.emplace(basename(fstab_rec->blk_device));
    }

    // libavb verifies AVB metadata on all verified partitions at once.
    // e.g., The device_tree_vbmeta_parts_ will be "vbmeta,boot,system,vendor"
    // for libavb to verify metadata, even if there is only /vendor in the
    // above mount_fstab_recs_.
    if (need_dm_verity_) {
        if (device_tree_vbmeta_parts_.empty()) {
            LOG(ERROR) << "Missing vbmeta parts in device tree";
            return false;
        }
        std::vector<std::string> partitions = android::base::Split(device_tree_vbmeta_parts_, ",");
        std::string ab_suffix = fs_mgr_get_slot_suffix();
        for (const auto& partition : partitions) {
            // device_init_partitions_ is of type std::set so it's not an issue to emplace
            // a partition twice. e.g., /vendor might be in both places:
            //   - device_tree_vbmeta_parts_ = "vbmeta,boot,system,vendor"
            //   - mount_fstab_recs_: /vendor_a
            device_init_partitions_.emplace(partition + ab_suffix);
        }
    }
    return true;
}

bool FirstStageMountVBootV2::SetUpDmVerity(fstab_rec* fstab_rec) {
    if (fs_mgr_is_avb(fstab_rec)) {
        if (!InitAvbHandle()) return false;
        if (avb_handle_->hashtree_disabled()) {
            LOG(INFO) << "avb hashtree disabled for '" << fstab_rec->mount_point << "'";
        } else if (avb_handle_->SetUpAvb(fstab_rec, false /* wait_for_verity_dev */)) {
            // The exact block device name (fstab_rec->blk_device) is changed to "/dev/block/dm-XX".
            // Needs to create it because ueventd isn't started in init first stage.
            InitVerityDevice(fstab_rec->blk_device);
        } else {
            return false;
        }
    }
    return true;  // Returns true to mount the partition.
}

bool FirstStageMountVBootV2::InitAvbHandle() {
    if (avb_handle_) return true;  // Returns true if the handle is already initialized.

    avb_handle_ = FsManagerAvbHandle::Open(device_tree_by_name_prefix_);
    if (!avb_handle_) {
        PLOG(ERROR) << "Failed to open FsManagerAvbHandle";
        return false;
    }
    // Sets INIT_AVB_VERSION here for init to set ro.boot.avb_version in the second stage.
    setenv("INIT_AVB_VERSION", avb_handle_->avb_version().c_str(), 1);
    return true;
}

// Public functions
// ----------------
// Mounts /vendor, /odm or /system in init first stage.
// The fstab is read from device-tree.
bool InitFirstStageMount() {
    // Skips first stage mount if we're in recovery mode.
    if (access("/sbin/recovery", F_OK) == 0) {
        LOG(INFO) << "First stage mount skipped (recovery mode)";
        return true;
    }

    // Firstly checks if device tree fstab entries are compatible.
    if (!is_android_dt_value_expected("fstab/compatible", "android,fstab")) {
        LOG(INFO) << "First stage mount skipped (missing/incompatible fstab in device tree)";
        return true;
    }

    std::unique_ptr<FirstStageMount> handle = FirstStageMount::Create();
    if (!handle) {
        LOG(ERROR) << "Failed to create FirstStageMount";
        return false;
    }
    return handle->DoFirstStageMount();
}
