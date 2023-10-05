//
// Copyright (C) 2020 The Android Open Source_info Project
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

#include "writer_v3.h"

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <brotli/encode.h>
#include <libsnapshot/cow_format.h>
#include <libsnapshot/cow_reader.h>
#include <libsnapshot/cow_writer.h>
#include <lz4.h>
#include <zlib.h>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "android-base/parseint.h"
#include "android-base/strings.h"

// The info messages here are spammy, but as useful for update_engine. Disable
// them when running on the host.
#ifdef __ANDROID__
#define LOG_INFO LOG(INFO)
#else
#define LOG_INFO LOG(VERBOSE)
#endif

namespace android {
namespace snapshot {

static_assert(sizeof(off_t) == sizeof(uint64_t));

using android::base::unique_fd;

CowWriterV3::CowWriterV3(const CowOptions& options, unique_fd&& fd)
    : CowWriterBase(options, std::move(fd)) {
    SetupHeaders();
}

void CowWriterV3::SetupHeaders() {
    header_ = {};
    header_.prefix.magic = kCowMagicNumber;
    header_.prefix.major_version = 3;
    header_.prefix.minor_version = 0;
    header_.prefix.header_size = sizeof(CowHeader);
    header_.footer_size = 0;
    header_.op_size = sizeof(CowOperationV3);
    header_.block_size = options_.block_size;
    header_.num_merge_ops = options_.num_merge_ops;
    header_.cluster_ops = 0;
    header_.buffer_size = 0;
    return;
}

bool CowWriterV3::ParseOptions() {
    num_compress_threads_ = std::max(options_.num_compress_threads, 1);
    auto parts = android::base::Split(options_.compression, ",");
    if (parts.size() > 2) {
        LOG(ERROR) << "failed to parse compression parameters: invalid argument count: "
                   << parts.size() << " " << options_.compression;
        return false;
    }
    auto algorithm = CompressionAlgorithmFromString(parts[0]);
    if (!algorithm) {
        LOG(ERROR) << "unrecognized compression: " << options_.compression;
        return false;
    }
    if (parts.size() > 1) {
        if (!android::base::ParseUint(parts[1], &compression_.compression_level)) {
            LOG(ERROR) << "failed to parse compression level invalid type: " << parts[1];
            return false;
        }
    } else {
        compression_.compression_level =
                CompressWorker::GetDefaultCompressionLevel(algorithm.value());
    }

    compression_.algorithm = *algorithm;
    return true;
}

CowWriterV3::~CowWriterV3() {}

bool CowWriterV3::Initialize(std::optional<uint64_t> label) {
    if (!InitFd() || !ParseOptions()) {
        return false;
    }
    if (!label) {
        if (!OpenForWrite()) {
            return false;
        }
    }
    if (!compress_threads_.size()) {
        InitWorkers();
    }
    return true;
}

bool CowWriterV3::OpenForWrite() {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    return false;
}

bool CowWriterV3::OpenForAppend(uint64_t label) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (label) return false;
    return false;
}

bool CowWriterV3::EmitCopy(uint64_t new_block, uint64_t old_block, uint64_t num_blocks) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (new_block || old_block || num_blocks) return false;
    return false;
}

bool CowWriterV3::EmitRawBlocks(uint64_t new_block_start, const void* data, size_t size) {
    return EmitBlocks(new_block_start, data, size, 0, 0, kCowReplaceOp);
}

bool CowWriterV3::EmitXorBlocks(uint32_t new_block_start, const void* data, size_t size,
                                uint32_t old_block, uint16_t offset) {
    return EmitBlocks(new_block_start, data, size, old_block, offset, kCowXorOp);
}

bool CowWriterV3::CompressBlocks(size_t num_blocks, const void* data) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (num_blocks && data) return false;
    return false;
}

bool CowWriterV3::EmitBlocks(uint64_t new_block_start, const void* data, size_t size,
                             uint64_t old_block, uint16_t offset, uint8_t type) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (new_block_start && data && size && old_block && offset && type) return false;
    return false;
}

bool CowWriterV3::EmitZeroBlocks(uint64_t new_block_start, uint64_t num_blocks) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (new_block_start && num_blocks) return false;
    return false;
}

bool CowWriterV3::EmitLabel(uint64_t label) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (label) return false;
    return false;
}

bool CowWriterV3::EmitSequenceData(size_t num_ops, const uint32_t* data) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (num_ops && data) return false;
    return false;
}

bool CowWriterV3::Finalize() {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    return false;
}

uint64_t CowWriterV3::GetCowSize() {
    LOG(ERROR) << __LINE__ << " " << __FILE__
               << " <- Get Cow Size function here should never be called";
    return 0;
}

bool CowWriterV3::GetDataPos(uint64_t* pos) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (pos) return false;
    return false;
}

bool CowWriterV3::EnsureSpaceAvailable(const uint64_t bytes_needed) const {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (bytes_needed) return false;
    return false;
}

bool CowWriterV3::WriteOperation(const CowOperation& op, const void* data, size_t size) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (op.data_length && data && size) return false;
    return false;
}

void CowWriterV3::AddOperation(const CowOperation& op) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (op.data_length) return;
    return;
}

bool CowWriterV3::WriteRawData(const void* data, const size_t size) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    if (data && size) return false;
    return false;
}

bool CowWriterV3::Sync() {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    return false;
}

bool CowWriterV3::Truncate(off_t length) {
    LOG(ERROR) << __LINE__ << " " << __FILE__ << " <- function here should never be called";
    return length ? false : false;
}

}  // namespace snapshot
}  // namespace android
