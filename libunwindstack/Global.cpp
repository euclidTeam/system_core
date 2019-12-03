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

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include <string>
#include <vector>

#include <unwindstack/Global.h>
#include <unwindstack/MapInfo.h>
#include <unwindstack/Maps.h>
#include <unwindstack/Memory.h>

namespace unwindstack {

Global::Global(std::shared_ptr<Memory>& memory) : memory_(memory) {}
Global::Global(std::shared_ptr<Memory>& memory, std::vector<std::string>& search_libs)
    : memory_(memory), search_libs_(search_libs) {}

void Global::SetArch(ArchEnum arch) {
  if (arch_ == ARCH_UNKNOWN) {
    arch_ = arch;
    ProcessArch();
  }
}

bool Global::Searchable(const std::string& name) {
  if (search_libs_.empty()) {
    return true;
  }

  if (name.empty()) {
    return false;
  }

  const char* base_name = basename(name.c_str());
  for (const std::string& lib : search_libs_) {
    if (base_name == lib) {
      return true;
    }
  }
  return false;
}

void Global::FindAndReadVariable(Maps* maps, const char* var_str) {
  std::string variable(var_str);
  // When looking for global variables, do not arbitrarily search every
  // readable map. Instead look for a specific pattern that must exist.
  // The pattern should be a readable map, followed by a read-write
  // map with a non-zero offset.
  // For example:
  //   f0000-f1000 0 r-- /system/lib/libc.so
  //   f1000-f2000 1000 r-x /system/lib/libc.so
  //   f2000-f3000 2000 rw- /system/lib/libc.so
  // This also works:
  //   f0000-f2000 0 r-- /system/lib/libc.so
  //   f2000-f3000 2000 rw- /system/lib/libc.so
  MapInfo* map_start = nullptr;
  for (const auto& info : *maps) {
    if (map_start != nullptr && map_start->name == info->name) {
      if (info->offset != 0 && (info->flags & (PROT_READ | PROT_WRITE)) == (PROT_READ | PROT_WRITE)) {
        Elf* elf = map_start->GetElf(memory_, arch());
        uint64_t ptr;
        if (elf->GetGlobalVariableOffset(variable, &ptr) && ptr != 0) {
          uint64_t offset_end = info->offset + info->end - info->start;
          if (ptr >= info->offset && ptr < offset_end) {
            ptr = info->start + ptr - info->offset;
            if (ReadVariableData(ptr)) {
              break;
            }
          }
        }
        map_start = nullptr;
      }
    } else {
      map_start = nullptr;
    }
    if (map_start == nullptr && (info->flags & PROT_READ) && info->offset == 0 &&
        Searchable(info->name)) {
      map_start = info.get();
    }
  }
}

}  // namespace unwindstack
