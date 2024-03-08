/*
 * Copyright 2022, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "libdebuggerd/utility_host.h"

#include <sys/prctl.h>

#include <string>

#include <android-base/stringprintf.h>

using android::base::StringPrintf;

#ifndef PR_MTE_TAG_SHIFT
#define PR_MTE_TAG_SHIFT 3
#endif

#ifndef PR_MTE_TAG_MASK
#define PR_MTE_TAG_MASK (0xffffUL << PR_MTE_TAG_SHIFT)
#endif

#ifndef PR_MTE_TCF_ASYNC
#define PR_MTE_TCF_ASYNC (1UL << 2)
#endif

#ifndef PR_MTE_TCF_SYNC
#define PR_MTE_TCF_SYNC (1UL << 1)
#endif

#ifndef PR_PAC_APIAKEY
#define PR_PAC_APIAKEY (1UL << 0)
#endif

#ifndef PR_PAC_APIBKEY
#define PR_PAC_APIBKEY (1UL << 1)
#endif

#ifndef PR_PAC_APDAKEY
#define PR_PAC_APDAKEY (1UL << 2)
#endif

#ifndef PR_PAC_APDBKEY
#define PR_PAC_APDBKEY (1UL << 3)
#endif

#ifndef PR_PAC_APGAKEY
#define PR_PAC_APGAKEY (1UL << 4)
#endif

#ifndef PR_TAGGED_ADDR_ENABLE
#define PR_TAGGED_ADDR_ENABLE (1UL << 0)
#endif

#define DESCRIBE_FLAG(flag) \
  if (value & flag) {       \
    desc += ", ";           \
    desc += #flag;          \
    value &= ~flag;         \
  }

static std::string describe_end(long value, std::string& desc) {
  if (value) {
    desc += StringPrintf(", unknown 0x%lx", value);
  }
  return desc.empty() ? "" : " (" + desc.substr(2) + ")";
}

std::string describe_tagged_addr_ctrl(long value) {
  std::string desc;
  DESCRIBE_FLAG(PR_TAGGED_ADDR_ENABLE);
  DESCRIBE_FLAG(PR_MTE_TCF_SYNC);
  DESCRIBE_FLAG(PR_MTE_TCF_ASYNC);
  if (value & PR_MTE_TAG_MASK) {
    desc += StringPrintf(", mask 0x%04lx", (value & PR_MTE_TAG_MASK) >> PR_MTE_TAG_SHIFT);
    value &= ~PR_MTE_TAG_MASK;
  }
  return describe_end(value, desc);
}

std::string describe_pac_enabled_keys(long value) {
  std::string desc;
  DESCRIBE_FLAG(PR_PAC_APIAKEY);
  DESCRIBE_FLAG(PR_PAC_APIBKEY);
  DESCRIBE_FLAG(PR_PAC_APDAKEY);
  DESCRIBE_FLAG(PR_PAC_APDBKEY);
  DESCRIBE_FLAG(PR_PAC_APGAKEY);
  return describe_end(value, desc);
}
