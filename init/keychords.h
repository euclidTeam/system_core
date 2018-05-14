/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef _INIT_KEYCHORDS_H_
#define _INIT_KEYCHORDS_H_

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <android-base/chrono_utils.h>

#include "epoll.h"

namespace android {
namespace init {

#define KEYCODES_MAXIMUM_TIMEOUT 30000

class Keychords {
  public:
    Keychords();
    Keychords(const Keychords&) = delete;
    Keychords(Keychords&&) = delete;
    Keychords& operator=(Keychords&) = delete;
    ~Keychords() noexcept;

    void Register(const std::vector<int>& keycodes);
    void Start(Epoll* epoll, std::function<void(const std::vector<int>&)> handler);
    std::optional<std::chrono::milliseconds> CheckAndCalculateNextIfLess(
        std::optional<std::chrono::milliseconds> wait);

  private:
    // Bit management
    class Mask {
      public:
        explicit Mask(size_t bit = 0);

        void SetBit(size_t bit, bool value = true);
        bool GetBit(size_t bit) const;

        size_t bytesize() const;
        void* data();
        size_t size() const;
        void resize(size_t bit);

        operator bool() const;
        Mask operator&(const Mask& rval) const;
        void operator|=(const Mask& rval);

      private:
        typedef unsigned int mask_t;
        static constexpr size_t kBitsPerByte = 8;

        std::vector<mask_t> bits_;
    };

    struct Entry {
        static constexpr std::chrono::milliseconds kDurationOff = {};
        static constexpr android::base::boot_clock::time_point kMatchedOff = {};

        Entry(std::chrono::milliseconds duration);

        bool notified_;
        const std::chrono::milliseconds duration_;
        android::base::boot_clock::time_point matched_;
    };

    static constexpr char kDevicePath[] = "/dev/input";

    void LambdaCheck();
    void LambdaHandler(int fd);
    void InotifyHandler();

    bool GeteventEnable(int fd);
    void GeteventOpenDevice(const std::string& device);
    void GeteventOpenDevice();
    void GeteventCloseDevice(const std::string& device);

    Epoll* epoll_;
    std::function<void(const std::vector<int>&)> handler_;

    std::map<std::string, int> registration_;

    std::map<const std::vector<int>, Entry> entries_;

    Mask current_;

    int inotify_fd_;
};

}  // namespace init
}  // namespace android

#endif
