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

bool CowParserV3::Parse(borrowed_fd fd, const CowHeaderV3& header, std::optional<uint64_t> label) {
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

bool CowParserV3::ParseOps(borrowed_fd fd, std::optional<uint64_t> label) {
    uint64_t pos;
    auto data_loc = std::make_shared<std::unordered_map<uint64_t, uint64_t>>();

    // Skip the scratch space
    if (header_.prefix.major_version >= 2 && (header_.buffer_size > 0)) {
        LOG(DEBUG) << " Scratch space found of size: " << header_.buffer_size;
        size_t init_offset = header_.prefix.header_size + header_.buffer_size;
        pos = lseek(fd.get(), init_offset, SEEK_SET);
        if (pos != init_offset) {
            PLOG(ERROR) << "lseek ops failed";
            return false;
        }
    } else {
        pos = lseek(fd.get(), header_.prefix.header_size, SEEK_SET);
        if (pos != header_.prefix.header_size) {
            PLOG(ERROR) << "lseek ops failed";
            return false;
        }
        // Reading a v1 version of COW which doesn't have buffer_size.
        header_.buffer_size = 0;
    }
    uint64_t data_pos = 0;

    data_pos = pos + sizeof(CowOperationV3);

    auto ops_buffer = std::make_shared<std::vector<CowOperationV3>>();
    uint64_t current_op_num = 0;
    bool done = false;

    // Alternating op clusters and data
    while (!done) {
        uint64_t to_add = (fd_size_ - pos) / sizeof(CowOperationV3);
        if (to_add == 0) break;
        ops_buffer->resize(current_op_num + to_add);
        if (!android::base::ReadFully(fd, &ops_buffer->data()[current_op_num],
                                      to_add * sizeof(CowOperationV3))) {
            PLOG(ERROR) << "read op failed";
            return false;
        }
        // Parse current cluster to find start of next cluster
        while (current_op_num < ops_buffer->size()) {
            auto& current_op = ops_buffer->data()[current_op_num];
            current_op_num++;
            if (current_op.type == kCowXorOp) {
                data_loc->insert({current_op.new_block, data_pos});
            }
            pos += sizeof(CowOperationV3);
            data_pos += current_op.data_length;

            if (current_op.type == kCowClusterOp) {
                break;
            } else if (current_op.type == kCowLabelOp) {
                last_label_ = {GetCowOpSourceInfoData(current_op)};

                // If we reach the requested label, stop reading.
                if (label && label.value() == GetCowOpSourceInfoData(current_op)) {
                    done = true;
                    break;
                }
            }
        }

        // Position for next cluster read
        off_t offs = lseek(fd.get(), pos, SEEK_SET);
        if (offs < 0 || pos != static_cast<uint64_t>(offs)) {
            PLOG(ERROR) << "lseek next op failed " << offs;
            return false;
        }
        ops_buffer->resize(current_op_num);
    }

    LOG(DEBUG) << "COW file read complete. Total ops: " << ops_buffer->size();
    // To successfully parse a COW file, we need either:
    //  (1) a label to read up to, and for that label to be found, or
    //  (2) a valid footer.
    if (label) {
        if (!last_label_) {
            LOG(ERROR) << "Did not find label " << label.value()
                       << " while reading COW (no labels found)";
            return false;
        }
        if (last_label_.value() != label.value()) {
            LOG(ERROR) << "Did not find label " << label.value()
                       << ", last label=" << last_label_.value();
            return false;
        }
    }

    uint8_t csum[32];
    memset(csum, 0, sizeof(uint8_t) * 32);

    ops_ = ops_buffer;
    ops_->shrink_to_fit();
    data_loc_ = data_loc;
    return true;
}

}  // namespace snapshot
}  // namespace android