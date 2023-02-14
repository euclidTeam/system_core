//
// Copyright (C) 2023 The Android Open Source Project
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
//
#include "task.h"
#include <iostream>
#include "fastboot.h"
#include "filesystem.h"
#include "super_flash_helper.h"

FlashTask::FlashTask(const std::string& _slot, const std::string& _pname)
    : pname_(_pname), fname_(find_item(_pname)), slot_(_slot) {
    if (fname_.empty()) die("cannot determine image filename for '%s'", pname_.c_str());
}
FlashTask::FlashTask(const std::string& _slot, const std::string& _pname, const std::string& _fname)
    : pname_(_pname), fname_(_fname), slot_(_slot) {}

void FlashTask::Run() {
    auto flash = [&](const std::string& partition) {
        if (should_flash_in_userspace(partition) && !is_userspace_fastboot()) {
            die("The partition you are trying to flash is dynamic, and "
                "should be flashed via fastbootd. Please run:\n"
                "\n"
                "    fastboot reboot fastboot\n"
                "\n"
                "And try again. If you are intentionally trying to "
                "overwrite a fixed partition, use --force.");
        }
        do_flash(partition.c_str(), fname_.c_str());
    };
    do_for_partitions(pname_, slot_, flash, true);
}

RebootTask::RebootTask(FlashingPlan* _fp) : fp_(_fp){};
RebootTask::RebootTask(FlashingPlan* _fp, std::string _reboot_target)
    : reboot_target_(std::move(_reboot_target)), fp_(_fp){};

void RebootTask::Run() {
    if ((reboot_target_ == "userspace" || reboot_target_ == "fastboot")) {
        if (!is_userspace_fastboot()) {
            reboot_to_userspace_fastboot();
            fp_->fb->WaitForDisconnect();
        }
    } else if (reboot_target_ == "recovery") {
        fp_->fb->RebootTo("recovery");
        fp_->fb->WaitForDisconnect();
    } else if (reboot_target_ == "bootloader") {
        fp_->fb->RebootTo("bootloader");
        fp_->fb->WaitForDisconnect();
    } else if (reboot_target_ == "") {
        fp_->fb->Reboot();
        fp_->fb->WaitForDisconnect();
    } else {
        syntax_error("unknown reboot target %s", reboot_target_.c_str());
    }
}

FlashSuperLayoutTask::FlashSuperLayoutTask(FlashingPlan* _fp) : fp_(_fp) {}

void FlashSuperLayoutTask::Run() {
    auto s = fp_->helper->GetSparseLayout();

    std::vector<SparsePtr> files;
    if (int limit = get_sparse_limit(sparse_file_len(s.get(), false, false))) {
        files = resparse_file(s.get(), limit);
    } else {
        files.emplace_back(std::move(s));
    }

    // Send the data to the device.
    flash_partition_files(fp_->super_name, files);
}

std::unique_ptr<FlashSuperLayoutTask> FlashSuperLayoutTask::Initialize(
        FlashingPlan* _fp, std::vector<ImageEntry>& os_images) {
    if (!supports_AB()) {
        LOG(VERBOSE) << "Cannot optimize flashing super on non-AB device";
        return nullptr;
    }
    if (_fp->slot == "all") {
        LOG(VERBOSE) << "Cannot optimize flashing super for all slots";
        return nullptr;
    }

    // Does this device use dynamic partitions at all?
    unique_fd fd = _fp->source->OpenFile("super_empty.img");

    if (fd < 0) {
        LOG(VERBOSE) << "could not open super_empty.img";
        return nullptr;
    }

    // Try to find whether there is a super partition.
    if (_fp->fb->GetVar("super-partition-name", &_fp->super_name) != fastboot::SUCCESS) {
        _fp->super_name = "super";
    }
    std::string partition_size_str;

    if (_fp->fb->GetVar("partition-size:" + _fp->super_name, &partition_size_str) !=
        fastboot::SUCCESS) {
        LOG(VERBOSE) << "Cannot optimize super flashing: could not determine super partition";
        return nullptr;
    }
    _fp->helper = new SuperFlashHelper(*_fp->source);
    if (!_fp->helper->Open(fd)) {
        return nullptr;
    }

    for (const auto& entry : os_images) {
        auto partition = GetPartitionName(entry, _fp->current_slot);
        auto image = entry.first;

        if (!_fp->helper->AddPartition(partition, image->img_name, image->optional_if_no_image)) {
            return nullptr;
        }
    }
    // Remove images that we already flashed, just in case we have non-dynamic OS images.
    auto remove_if_callback = [&](const ImageEntry& entry) -> bool {
        return _fp->helper->WillFlash(GetPartitionName(entry, _fp->current_slot));
    };
    os_images.erase(std::remove_if(os_images.begin(), os_images.end(), remove_if_callback),
                    os_images.end());
    return std::make_unique<FlashSuperLayoutTask>(_fp);
}