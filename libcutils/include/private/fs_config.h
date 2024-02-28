/*
 * Copyright (C) 2007 The Android Open Source Project
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

/* This file is used to define the properties of the filesystem
** images generated by build tools (mkbootfs and mkyaffs2image) and
** by the device side of adb.
*/

#pragma once

#include <stdint.h>
#include <sys/cdefs.h>

#include <linux/capability.h>

/* Rules for directories and files has moved to system/code/libcutils/fs_config.c */

__BEGIN_DECLS

void fs_config(const char* path, int dir, const char* target_out_path, unsigned* uid, unsigned* gid,
               unsigned* mode, uint64_t* capabilities);
void fs_config_nodefault(const char* path, int dir, const char* target_out_path, unsigned* uid,
                         unsigned* gid, unsigned* mode, uint64_t* capabilities);

__END_DECLS
