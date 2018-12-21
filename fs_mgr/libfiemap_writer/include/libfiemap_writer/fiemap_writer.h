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

#include <linux/fiemap.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <android-base/unique_fd.h>

namespace android {
namespace fiemap_writer {

class FiemapWriter;
using FiemapUniquePtr = std::unique_ptr<FiemapWriter>;

class FiemapWriter final {
  public:
    static constexpr const char* kSysDevBlock = "/sys/dev/block";
    static constexpr const char* kSysBlock = "/sys/block";
    static constexpr const char* kBlockDevDir = "/dev/block";
    // We are expecting no more than 512 extents in a fiemap of the file we create.
    // If we find more, then it is treated as error for now.
    // TODO: may be accept the max extent count as the input to the class.
    static constexpr uint32_t kMaxExtents = 512;
    // TODO: Fallback to using fibmap if FIEMAP_EXTENT_MERGED is set.
    // TODO: Double check on FIEMAP_EXTENT_ENCODED or FIEMAP_EXTENT_DATA_ENCRYPTED before finalizing
    // the class for writes
    static constexpr uint32_t kUnsupportedExtentFlags =
            FIEMAP_EXTENT_UNKNOWN | FIEMAP_EXTENT_UNWRITTEN | FIEMAP_EXTENT_DELALLOC |
            FIEMAP_EXTENT_NOT_ALIGNED | FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_DATA_TAIL |
            FIEMAP_EXTENT_UNWRITTEN | FIEMAP_EXTENT_SHARED | FIEMAP_EXTENT_MERGED;

    // Factory method for FiemapWriter.
    // The method returns FiemapUniquePtr that contains all the data necessary to be able to write
    // to the given file directly using raw block i/o.
    static FiemapUniquePtr Open(const std::string& file_path, uint64_t size, bool create = true);

    // Syncs block device writes.
    bool Flush() const;

    // Writes the file by using its FIEMAP and performing i/o on the raw block device.
    // The return value is success / failure. This will happen in particular if the
    // kernel write returns errors, extents are not writeable or more importantly, if the 'size' is
    // not aligned to the block device's block size.
    bool Write(off64_t off, uint8_t* buffer, uint64_t size);

    // The counter part of Write(). It is an error for the offset to be unaligned with
    // the block device's block size.
    // In case of error, the contents of buffer MUST be discarded.
    bool Read(off64_t off, uint8_t* buffer, uint64_t size);

    ~FiemapWriter() = default;

    const std::string& file_path() const { return file_path_; };
    uint64_t size() const { return file_size_; };
    const std::string& bdev_path() const { return bdev_path_; };
    uint64_t block_size() const { return block_size_; };
    const std::vector<struct fiemap_extent>& extents() { return extents_; };

    // Non-copyable & Non-movable
    FiemapWriter(const FiemapWriter&) = delete;
    FiemapWriter& operator=(const FiemapWriter&) = delete;
    FiemapWriter& operator=(FiemapWriter&&) = delete;
    FiemapWriter(FiemapWriter&&) = delete;

  private:
    // Name of the file managed by this class.
    std::string file_path_;
    // Block device on which we have created the file.
    std::string bdev_path_;

    // File descriptors for the file and block device
    ::android::base::unique_fd file_fd_;
    ::android::base::unique_fd bdev_fd_;

    // Size in bytes of the file this class is writing
    uint64_t file_size_;

    // total size in bytes of the block device
    uint64_t bdev_size_;

    // Filesystem type where the file is being created.
    // See: <uapi/linux/magic.h> for filesystem magic numbers
    uint32_t fs_type_;

    // block size as reported by the kernel of the underlying block device;
    uint64_t block_size_;

    // This file's fiemap
    std::vector<struct fiemap_extent> extents_;

    FiemapWriter() = default;

    void LogExtent(int extent_num, const struct fiemap_extent* ext);
    uint64_t WriteExtent(const struct fiemap_extent& ext, uint8_t* buffer, off64_t logical_off,
                         uint64_t length);
};

}  // namespace fiemap_writer
}  // namespace android
