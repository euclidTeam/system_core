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
#include <optional>
#include <set>

#include "epoll.h"

namespace android {
namespace init {

void HandleKeychord(const std::set<int>& keycodes);

void KeychordInit(Epoll* init_epoll);
bool RegisterKeychord(const std::set<int>& keycodes);
std::optional<std::chrono::milliseconds> KeychordWait(std::optional<std::chrono::milliseconds> wait);

}  // namespace init
}  // namespace android

#endif
