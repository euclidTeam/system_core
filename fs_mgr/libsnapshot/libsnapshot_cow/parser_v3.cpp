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
#include "parser_v3.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include "libsnapshot/cow_format.h"

namespace android {
namespace snapshot {

using android::base::borrowed_fd;

bool CowParserV3::Parse(borrowed_fd fd, const CowHeader& header, std::optional<uint64_t> label) {
    auto pos = lseek(fd.get(), 0, SEEK_END);
    if (pos < 0) {
        PLOG(ERROR) << "lseek end failed";
        return false;
    }
    fd_size_ = pos;
    header_ = header;

    if (header_.footer_size != 0) {
        LOG(ERROR) << "Footer size isn't 0, read " << header_.footer_size;
        return false;
    }
    if (header_.op_size != sizeof(CowOperationV3)) {
        LOG(ERROR) << "Operation size unknown, read " << header_.op_size << ", expected "
                   << sizeof(CowOperationV3);
        return false;
    }
    if (header_.cluster_ops != 0) {
        LOG(ERROR) << "Cluster ops not supported in v3";
        return false;
    }

    if ((header_.prefix.major_version > kCowVersionMajor) ||
        (header_.prefix.minor_version != kCowVersionMinor)) {
        LOG(ERROR) << "Header version mismatch, "
                   << "major version: " << header_.prefix.major_version
                   << ", expected: " << kCowVersionMajor
                   << ", minor version: " << header_.prefix.minor_version
                   << ", expected: " << kCowVersionMinor;
        return false;
    }

    return ParseOps(fd, label);
}

}  // namespace snapshot
}  // namespace android