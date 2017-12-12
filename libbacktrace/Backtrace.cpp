/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <ucontext.h>

#include <string>

#include <android-base/stringprintf.h>

#include <backtrace/Backtrace.h>
#include <backtrace/BacktraceMap.h>

#include <demangle.h>

#include "BacktraceLog.h"
#include "UnwindStack.h"
#include "thread_utils.h"

using android::base::StringPrintf;

//-------------------------------------------------------------------------
// Backtrace functions.
//-------------------------------------------------------------------------
Backtrace::Backtrace(pid_t pid, pid_t tid, BacktraceMap* map)
    : pid_(pid), tid_(tid), map_(map), map_shared_(true) {
  if (map_ == nullptr) {
    map_ = BacktraceMap::Create(pid);
    map_shared_ = false;
  }
}

Backtrace::~Backtrace() {
  if (map_ && !map_shared_) {
    delete map_;
    map_ = nullptr;
  }
}

std::string Backtrace::GetFunctionName(uint64_t pc, uint64_t* offset, const backtrace_map_t* map) {
  backtrace_map_t map_value;
  if (map == nullptr) {
    FillInMap(pc, &map_value);
    map = &map_value;
  }
  // If no map is found, or this map is backed by a device, then return nothing.
  if (map->start == 0 || (map->flags & PROT_DEVICE_MAP)) {
    return "";
  }
  return demangle(GetFunctionNameRaw(pc, offset).c_str());
}

bool Backtrace::VerifyReadWordArgs(uint64_t ptr, word_t* out_value) {
  if (ptr & (sizeof(word_t)-1)) {
    BACK_LOGW("invalid pointer %p", reinterpret_cast<void*>(ptr));
    *out_value = static_cast<word_t>(-1);
    return false;
  }
  return true;
}

std::string Backtrace::FormatFrameData(size_t frame_num) {
  if (frame_num >= frames_.size()) {
    return "";
  }
  return FormatFrameData(&frames_[frame_num]);
}

std::string Backtrace::FormatFrameData(const backtrace_frame_data_t* frame) {
  std::string map_name;
  if (BacktraceMap::IsValid(frame->map)) {
    if (!frame->map.name.empty()) {
      map_name = frame->map.name.c_str();
      if (map_name[0] == '[' && map_name[map_name.size() - 1] == ']') {
        map_name.resize(map_name.size() - 1);
        map_name += StringPrintf(":%" PRIPTR "]", frame->map.start);
      }
    } else {
      map_name = StringPrintf("<anonymous:%" PRIPTR ">", frame->map.start);
    }
  } else {
    map_name = "<unknown>";
  }

  std::string line(StringPrintf("#%02zu pc %" PRIPTR "  ", frame->num, frame->rel_pc));
  line += map_name;
  // Special handling for non-zero offset maps, we need to print that
  // information.
  if (frame->map.offset != 0) {
    line += " (offset " + StringPrintf("0x%" PRIx64, frame->map.offset) + ")";
  }
  if (!frame->func_name.empty()) {
    line += " (" + frame->func_name;
    if (frame->func_offset) {
      line += StringPrintf("+%" PRIu64, frame->func_offset);
    }
    line += ')';
  }

  return line;
}

void Backtrace::FillInMap(uint64_t pc, backtrace_map_t* map) {
  if (map_ != nullptr) {
    map_->FillIn(pc, map);
  }
}

void Backtrace::SetArch(ArchEnum) {
  abort();
}

Backtrace* Backtrace::Create(pid_t pid, pid_t tid, BacktraceMap* map) {
  if (pid == BACKTRACE_CURRENT_PROCESS) {
    pid = getpid();
    if (tid == BACKTRACE_CURRENT_THREAD) {
      tid = gettid();
    }
  } else if (tid == BACKTRACE_CURRENT_THREAD) {
    tid = pid;
  }

  if (pid == getpid()) {
    return new UnwindStackCurrent(pid, tid, map);
  } else {
    return new UnwindStackPtrace(pid, tid, map);
  }
}

std::string Backtrace::GetErrorString(BacktraceUnwindError error) {
  switch (error) {
  case BACKTRACE_UNWIND_NO_ERROR:
    return "No error";
  case BACKTRACE_UNWIND_ERROR_SETUP_FAILED:
    return "Setup failed";
  case BACKTRACE_UNWIND_ERROR_MAP_MISSING:
    return "No map found";
  case BACKTRACE_UNWIND_ERROR_INTERNAL:
    return "Internal libbacktrace error, please submit a bugreport";
  case BACKTRACE_UNWIND_ERROR_THREAD_DOESNT_EXIST:
    return "Thread doesn't exist";
  case BACKTRACE_UNWIND_ERROR_THREAD_TIMEOUT:
    return "Thread has not responded to signal in time";
  case BACKTRACE_UNWIND_ERROR_UNSUPPORTED_OPERATION:
    return "Attempt to use an unsupported feature";
  case BACKTRACE_UNWIND_ERROR_NO_CONTEXT:
    return "Attempt to do an offline unwind without a context";
  }
}
