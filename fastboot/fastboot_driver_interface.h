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
#include <cstdint>
#include <limits>

#include <cstdlib>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include <android-base/endian.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <bootimg.h>
#include <inttypes.h>
#include <sparse/sparse.h>

#include "constants.h"
#include "transport.h"

class Transport;

namespace fastboot {

enum RetCode : int {
    SUCCESS = 0,
    BAD_ARG,
    IO_ERROR,
    BAD_DEV_RESP,
    DEVICE_FAIL,
    TIMEOUT,
};

class IFBDriver {
  public:
    static constexpr int RESP_TIMEOUT = 30;  // 30 seconds
    static constexpr uint32_t MAX_DOWNLOAD_SIZE = std::numeric_limits<uint32_t>::max();
    static constexpr size_t TRANSPORT_CHUNK_SIZE = 1024;

    RetCode virtual Boot(std::string* response = nullptr,
                         std::vector<std::string>* info = nullptr) = 0;
    RetCode virtual Reboot(std::string* response = nullptr,
                           std::vector<std::string>* info = nullptr) = 0;
    RetCode virtual RebootTo(std::string target, std::string* response = nullptr,
                             std::vector<std::string>* info = nullptr) = 0;

    RetCode virtual RawCommand(const std::string& cmd, const std::string& message,
                               std::string* response = nullptr,
                               std::vector<std::string>* info = nullptr, int* dsize = nullptr) = 0;

    RetCode virtual RawCommand(const std::string& cmd, std::string* response = nullptr,
                               std::vector<std::string>* info = nullptr, int* dsize = nullptr) = 0;
};
}  // namespace fastboot