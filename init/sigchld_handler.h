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

#ifndef _INIT_SIGCHLD_HANDLER_H_
#define _INIT_SIGCHLD_HANDLER_H_

#include <chrono>
#include <set>
#include <vector>

namespace android {
namespace init {

std::set<pid_t> ReapAnyOutstandingChildren();

void WaitToBeReaped(int sigchld_fd, const std::vector<pid_t>& pids,
                    std::chrono::milliseconds timeout);

}  // namespace init
}  // namespace android

#endif
