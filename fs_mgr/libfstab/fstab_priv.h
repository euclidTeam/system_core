/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <string>
#include <utility>
#include <vector>

#include <fstab/fstab.h>

// Do not include logging_macros.h here as this header is used by fs_mgr, too.

std::vector<std::pair<std::string, std::string>> fs_mgr_parse_cmdline(const std::string& cmdline);
bool fs_mgr_get_boot_config_from_kernel(const std::string& cmdline, const std::string& key,
                                        std::string* out_val);
bool fs_mgr_get_boot_config_from_kernel_cmdline(const std::string& key, std::string* out_val);
bool fs_mgr_get_boot_config(const std::string& key, std::string* out_val);
std::vector<std::pair<std::string, std::string>> fs_mgr_parse_proc_bootconfig(
        const std::string& bootconfig);
bool fs_mgr_get_boot_config_from_bootconfig(const std::string& bootconfig, const std::string& key,
                                            std::string* out_val);
bool fs_mgr_get_boot_config_from_bootconfig_source(const std::string& key, std::string* out_val);

bool fs_mgr_update_for_slotselect(android::fs_mgr::Fstab* fstab);
bool is_dt_compatible();

namespace android {
namespace fs_mgr {

bool InRecovery();
bool ParseFstabFromString(const std::string& fstab_str, bool proc_mounts, Fstab* fstab_out);
bool SkipMountWithConfig(const std::string& skip_config, Fstab* fstab, bool verbose);
std::string GetFstabPath();

}  // namespace fs_mgr
}  // namespace android
