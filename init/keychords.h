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

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "epoll.h"

namespace android {
namespace init {

class Keychords {
  private:
    int count;
    Epoll* epoll;
    std::function<void(int)> handler;

    struct Entry {
        const std::vector<int> keycodes;
        bool notified;
        int id;

        Entry(const std::vector<int>& keycodes, int id);
    };
    std::vector<Entry> entries;

    int inotify_fd;

    static constexpr char kDevicePath[] = "/dev/input";

    std::map<std::string, int> registration;

    // Bit management
    class Mask {
      private:
        typedef unsigned int mask_t;
        std::vector<mask_t> bits;
        static constexpr size_t bits_per_byte = 8;

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
    };
    Mask current;

    void LambdaCheck();
    void LambdaHandler(int fd);

    bool GeteventEnable(int fd);
    void GeteventOpenDevice(const std::string& device);
    void GeteventCloseDevice(const std::string& device);

    void InotifyHandler();

    void GeteventOpenDevice();

  public:
    Keychords();
    ~Keychords();

    int GetId(const std::vector<int>& keycodes);
    void Start(Epoll* init_epoll, std::function<void(int)> init_handler);
};

}  // namespace init
}  // namespace android

#endif
