/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <cutils/ashmem.h>

/*
 * Implementation of the user-space ashmem API for devices, which have our
 * ashmem-enabled kernel. See ashmem-sim.c for the "fake" tmp-based version,
 * used by the simulator.
 */
#define LOG_TAG "ashmem"

#ifndef __ANDROID_VNDK__
#include <dlfcn.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <linux/ashmem.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <log/log.h>

#define ASHMEM_DEVICE "/dev/ashmem"

/* ashmem identity */
static dev_t __ashmem_rdev;
/*
 * If we trigger a signal handler in the middle of locked activity and the
 * signal handler calls ashmem, we could get into a deadlock state.
 */
static pthread_mutex_t __ashmem_lock = PTHREAD_MUTEX_INITIALIZER;

#ifndef __ANDROID_VNDK__
using openFdType = int (*)();

openFdType initOpenAshmemFd() {
    openFdType openFd = nullptr;
    void* handle = dlopen("libashmemd_client.so", RTLD_NOW);
    if (!handle) {
        ALOGE("Failed to dlopen() libashmemd_client.so: %s", dlerror());
        return openFd;
    }

    openFd = reinterpret_cast<openFdType>(dlsym(handle, "openAshmemdFd"));
    if (!openFd) {
        ALOGE("Failed to dlsym() openAshmemdFd() function: %s", dlerror());
    }
    return openFd;
}
#endif

/* logistics of getting file descriptor for ashmem */
static int __ashmem_open_locked()
{
    int ret;
    struct stat st;

#ifdef __ANDROID_VNDK__
    int fd = TEMP_FAILURE_RETRY(open(ASHMEM_DEVICE, O_RDWR | O_CLOEXEC));
#else
    static auto openFd = initOpenAshmemFd();
    int fd = -1;
    if (openFd) {
        fd = openFd();
    }
#endif
    if (fd < 0) {
        return fd;
    }

    ret = TEMP_FAILURE_RETRY(fstat(fd, &st));
    if (ret < 0) {
        int save_errno = errno;
        close(fd);
        errno = save_errno;
        return ret;
    }
    if (!S_ISCHR(st.st_mode) || !st.st_rdev) {
        close(fd);
        errno = ENOTTY;
        return -1;
    }

    __ashmem_rdev = st.st_rdev;
    return fd;
}

static int __ashmem_open()
{
    int fd;

    pthread_mutex_lock(&__ashmem_lock);
    fd = __ashmem_open_locked();
    pthread_mutex_unlock(&__ashmem_lock);

    return fd;
}

/* Make sure file descriptor references ashmem, negative number means false */
static int __ashmem_is_ashmem(int fd, int fatal)
{
    dev_t rdev;
    struct stat st;

    if (fstat(fd, &st) < 0) {
        return -1;
    }

    rdev = 0; /* Too much complexity to sniff __ashmem_rdev */
    if (S_ISCHR(st.st_mode) && st.st_rdev) {
        pthread_mutex_lock(&__ashmem_lock);
        rdev = __ashmem_rdev;
        if (rdev) {
            pthread_mutex_unlock(&__ashmem_lock);
        } else {
            int fd = __ashmem_open_locked();
            if (fd < 0) {
                pthread_mutex_unlock(&__ashmem_lock);
                return -1;
            }
            rdev = __ashmem_rdev;
            pthread_mutex_unlock(&__ashmem_lock);

            close(fd);
        }

        if (st.st_rdev == rdev) {
            return 0;
        }
    }

    if (fatal) {
        if (rdev) {
            LOG_ALWAYS_FATAL("illegal fd=%d mode=0%o rdev=%d:%d expected 0%o %d:%d",
              fd, st.st_mode, major(st.st_rdev), minor(st.st_rdev),
              S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IRGRP,
              major(rdev), minor(rdev));
        } else {
            LOG_ALWAYS_FATAL("illegal fd=%d mode=0%o rdev=%d:%d expected 0%o",
              fd, st.st_mode, major(st.st_rdev), minor(st.st_rdev),
              S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IRGRP);
        }
        /* NOTREACHED */
    }

    errno = ENOTTY;
    return -1;
}

static int __ashmem_check_failure(int fd, int result)
{
    if (result == -1 && errno == ENOTTY) __ashmem_is_ashmem(fd, 1);
    return result;
}

int ashmem_valid(int fd)
{
    return __ashmem_is_ashmem(fd, 0) >= 0;
}

/*
 * ashmem_create_region - creates a new ashmem region and returns the file
 * descriptor, or <0 on error
 *
 * `name' is an optional label to give the region (visible in /proc/pid/maps)
 * `size' is the size of the region, in page-aligned bytes
 */
int ashmem_create_region(const char *name, size_t size)
{
    int ret, save_errno;

    int fd = __ashmem_open();
    if (fd < 0) {
        return fd;
    }

    if (name) {
        char buf[ASHMEM_NAME_LEN] = {0};

        strlcpy(buf, name, sizeof(buf));
        ret = TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_SET_NAME, buf));
        if (ret < 0) {
            goto error;
        }
    }

    ret = TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_SET_SIZE, size));
    if (ret < 0) {
        goto error;
    }

    return fd;

error:
    save_errno = errno;
    close(fd);
    errno = save_errno;
    return ret;
}

int ashmem_set_prot_region(int fd, int prot)
{
    return __ashmem_check_failure(fd, TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_SET_PROT_MASK, prot)));
}

int ashmem_pin_region(int fd, size_t offset, size_t len)
{
    // TODO: should LP64 reject too-large offset/len?
    ashmem_pin pin = { static_cast<uint32_t>(offset), static_cast<uint32_t>(len) };

    return __ashmem_check_failure(fd, TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_PIN, &pin)));
}

int ashmem_unpin_region(int fd, size_t offset, size_t len)
{
    // TODO: should LP64 reject too-large offset/len?
    ashmem_pin pin = { static_cast<uint32_t>(offset), static_cast<uint32_t>(len) };

    return __ashmem_check_failure(fd, TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_UNPIN, &pin)));
}

int ashmem_get_size_region(int fd)
{
    return __ashmem_check_failure(fd, TEMP_FAILURE_RETRY(ioctl(fd, ASHMEM_GET_SIZE, NULL)));
}
