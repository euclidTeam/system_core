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

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cutils/fs.h>
#include <private/android_filesystem_config.h>

struct fs_path_config_from_file {
    unsigned len;
    unsigned mode;
    unsigned uid;
    unsigned gid;
    uint64_t capabilities;
    char prefix[];
} __attribute__((__aligned__(sizeof(uint64_t))));

/* Rules for directories.
** These rules are applied based on "first match", so they
** should start with the most specific path and work their
** way up to the root.
*/

static const struct fs_path_config android_dirs[] = {
    { 00770, AID_SYSTEM, AID_CACHE,  0, "cache" },
    { 00771, AID_SYSTEM, AID_SYSTEM, 0, "data/app" },
    { 00771, AID_SYSTEM, AID_SYSTEM, 0, "data/app-private" },
    { 00771, AID_ROOT,   AID_ROOT,   0, "data/dalvik-cache" },
    { 00771, AID_SYSTEM, AID_SYSTEM, 0, "data/data" },
    { 00771, AID_SHELL,  AID_SHELL,  0, "data/local/tmp" },
    { 00771, AID_SHELL,  AID_SHELL,  0, "data/local" },
    { 01771, AID_SYSTEM, AID_MISC,   0, "data/misc" },
    { 00770, AID_DHCP,   AID_DHCP,   0, "data/misc/dhcp" },
    { 00771, AID_SHARED_RELRO, AID_SHARED_RELRO, 0, "data/misc/shared_relro" },
    { 00775, AID_MEDIA_RW, AID_MEDIA_RW, 0, "data/media" },
    { 00775, AID_MEDIA_RW, AID_MEDIA_RW, 0, "data/media/Music" },
    { 00771, AID_SYSTEM, AID_SYSTEM, 0, "data" },
    { 00750, AID_ROOT,   AID_SHELL,  0, "sbin" },
    { 00755, AID_ROOT,   AID_SHELL,  0, "system/bin" },
    { 00755, AID_ROOT,   AID_SHELL,  0, "system/vendor" },
    { 00755, AID_ROOT,   AID_SHELL,  0, "system/xbin" },
    { 00755, AID_ROOT,   AID_ROOT,   0, "system/etc/ppp" },
    { 00755, AID_ROOT,   AID_SHELL,  0, "vendor" },
    { 00777, AID_ROOT,   AID_ROOT,   0, "sdcard" },
    { 00755, AID_ROOT,   AID_ROOT,   0, 0 },
};

/* Rules for files.
** These rules are applied based on "first match", so they
** should start with the most specific path and work their
** way up to the root. Prefixes ending in * denotes wildcard
** and will allow partial matches.
*/
static const char conf_dir[] = "/system/etc/fs_config_dirs";
static const char conf_file[] = "/system/etc/fs_config_files";

static const struct fs_path_config android_files[] = {
    { 00440, AID_ROOT,      AID_SHELL,     0, "system/etc/init.goldfish.rc" },
    { 00550, AID_ROOT,      AID_SHELL,     0, "system/etc/init.goldfish.sh" },
    { 00550, AID_ROOT,      AID_SHELL,     0, "system/etc/init.ril" },
    { 00550, AID_DHCP,      AID_SHELL,     0, "system/etc/dhcpcd/dhcpcd-run-hooks" },
    { 00555, AID_ROOT,      AID_ROOT,      0, "system/etc/ppp/*" },
    { 00555, AID_ROOT,      AID_ROOT,      0, "system/etc/rc.*" },
    { 00444, AID_ROOT,      AID_ROOT,      0, conf_dir },
    { 00444, AID_ROOT,      AID_ROOT,      0, conf_file },
    { 00644, AID_SYSTEM,    AID_SYSTEM,    0, "data/app/*" },
    { 00644, AID_MEDIA_RW,  AID_MEDIA_RW,  0, "data/media/*" },
    { 00644, AID_SYSTEM,    AID_SYSTEM,    0, "data/app-private/*" },
    { 00644, AID_APP,       AID_APP,       0, "data/data/*" },

    /* the following five files are INTENTIONALLY set-uid, but they
     * are NOT included on user builds. */
    { 04750, AID_ROOT,      AID_SHELL,     0, "system/xbin/su" },
    { 06755, AID_ROOT,      AID_ROOT,      0, "system/xbin/librank" },
    { 06755, AID_ROOT,      AID_ROOT,      0, "system/xbin/procrank" },
    { 06755, AID_ROOT,      AID_ROOT,      0, "system/xbin/procmem" },
    { 04770, AID_ROOT,      AID_RADIO,     0, "system/bin/pppd-ril" },

    /* the following files have enhanced capabilities and ARE included in user builds. */
    { 00750, AID_ROOT,      AID_SHELL,     (1ULL << CAP_SETUID) | (1ULL << CAP_SETGID), "system/bin/run-as" },
    { 00700, AID_SYSTEM,    AID_SHELL,     (1ULL << CAP_BLOCK_SUSPEND), "system/bin/inputflinger" },

    { 00750, AID_ROOT,      AID_ROOT,      0, "system/bin/uncrypt" },
    { 00750, AID_ROOT,      AID_ROOT,      0, "system/bin/install-recovery.sh" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/bin/*" },
    { 00755, AID_ROOT,      AID_ROOT,      0, "system/lib/valgrind/*" },
    { 00755, AID_ROOT,      AID_ROOT,      0, "system/lib64/valgrind/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/xbin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "system/vendor/bin/*" },
    { 00755, AID_ROOT,      AID_SHELL,     0, "vendor/bin/*" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "sbin/*" },
    { 00755, AID_ROOT,      AID_ROOT,      0, "bin/*" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "init*" },
    { 00750, AID_ROOT,      AID_SHELL,     0, "sbin/fs_mgr" },
    { 00640, AID_ROOT,      AID_SHELL,     0, "fstab.*" },
    { 00644, AID_ROOT,      AID_ROOT,      0, 0 },
};

static int fs_config_open(int dir)
{
    int fd = -1;

    const char *out = getenv("OUT");
    if (out && *out) {
        char *name = NULL;
        asprintf(&name, "%s%s", out, dir ? conf_dir : conf_file);
        if (name) {
            fd = TEMP_FAILURE_RETRY(open(name, O_RDONLY));
            free(name);
        }
    }
    if (fd < 0) {
        fd = TEMP_FAILURE_RETRY(open(dir ? conf_dir : conf_file, O_RDONLY));
    }
    return fd;
}

void fs_config(const char *path, int dir,
               unsigned *uid, unsigned *gid, unsigned *mode, uint64_t *capabilities)
{
    const struct fs_path_config *pc;
    int plen;
    struct stat st;
    void *address = NULL;

    int fd = fs_config_open(dir);
    if ((fd >= 0)
     && (TEMP_FAILURE_RETRY(fstat(fd, &st)) >= 0)
     && (size_t)st.st_size) {
        address = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (address == MAP_FAILED) {
            address = NULL;
        }
    } else if (fd >= 0) {
        close(fd);
    }

    if (path[0] == '/') {
        path++;
    }

    plen = strlen(path);

    if (address) {
        const struct fs_path_config_from_file *p = (const struct fs_path_config_from_file *)
            address;
        const char *end = (const char *)address + st.st_size;
        const struct fs_path_config_from_file *e = (const struct fs_path_config_from_file *)
            (end - sizeof(*p));
        for (; p < e; p = (const struct fs_path_config_from_file *)(((const char *)p) + p->len)) {
            ssize_t len, remainder = p->len - sizeof(*p);
            if (remainder < 0) {
                p = e;
                break;
            }
            if (remainder > (end - (const char *)p)) {
                remainder = (end - (const char *)p);
            }
            len = strnlen(p->prefix, remainder);
            if (len >= (end - (const char *)p)) {
                p = e;
                break;
            }
            if (dir) {
                if ((plen >= len) && !strncmp(p->prefix, path, len)) break;
            } else {
                /* If name ends in * then allow partial matches. */
                if (p->prefix[len - 1] == '*') {
                    if (!strncmp(p->prefix, path, len - 1)) break;
                } else if (plen == len) {
                    if (!strncmp(p->prefix, path, len)) break;
                }
            }
        }
        if (p < e) {
            *uid = p->uid;
            *gid = p->gid;
            *mode = (*mode & (~07777)) | p->mode;
            *capabilities = p->capabilities;
        }
        munmap(address, st.st_size);
        close(fd);
        if (p < e) {
            return;
        }
    }

    pc = dir ? android_dirs : android_files;
    for(; pc->prefix; pc++){
        int len = strlen(pc->prefix);
        if (dir) {
            if(plen < len) continue;
            if(!strncmp(pc->prefix, path, len)) break;
            continue;
        }
        /* If name ends in * then allow partial matches. */
        if (pc->prefix[len -1] == '*') {
            if(!strncmp(pc->prefix, path, len - 1)) break;
        } else if (plen == len){
            if(!strncmp(pc->prefix, path, len)) break;
        }
    }
    *uid = pc->uid;
    *gid = pc->gid;
    *mode = (*mode & (~07777)) | pc->mode;
    *capabilities = pc->capabilities;
}

ssize_t fs_config_generate(char *buffer, size_t length, const struct fs_path_config *pc)
{
    struct fs_path_config_from_file *p = (struct fs_path_config_from_file *)buffer;
    size_t len = (sizeof(*p) + strlen(pc->prefix) + sizeof(uint64_t)) & -sizeof(uint64_t);

    if (length < len) {
        return -ENOSPC;
    }
    memset(p, 0, len);
    p->len = len;
    p->mode = pc->mode;
    p->uid = pc->uid;
    p->gid = pc->gid;
    p->capabilities = pc->capabilities;
    strcpy(p->prefix, pc->prefix);
    return len;
}
