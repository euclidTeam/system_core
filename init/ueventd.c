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

#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <private/android_filesystem_config.h>

#include "ueventd.h"
#include "log.h"
#include "util.h"
#include "devices.h"
#include "ueventd_parser.h"

static char hardware[32];
static unsigned revision = 0;

static void import_kernel_nv(char *name)
{
    char *value = strchr(name, '=');

    if (value == 0) return;
    *value++ = 0;
    if (*name == 0) return;

    if (!strcmp(name,"androidboot.hardware"))
        strlcpy(hardware, value, sizeof(hardware));
}

static void import_kernel_cmdline(void)
{
    char cmdline[1024];
    char *ptr;
    int fd;

    fd = open("/proc/cmdline", O_RDONLY);
    if (fd >= 0) {
        int n = read(fd, cmdline, 1023);
        if (n < 0) n = 0;

        /* get rid of trailing newline, it happens */
        if (n > 0 && cmdline[n-1] == '\n') n--;

        cmdline[n] = 0;
        close(fd);
    } else {
        cmdline[0] = 0;
    }

    ptr = cmdline;
    while (ptr && *ptr) {
        char *x = strchr(ptr, ' ');
        if (x != 0) *x++ = 0;
        import_kernel_nv(ptr);
        ptr = x;
    }
}

int ueventd_main(int argc, char **argv)
{
    struct pollfd ufd;
    int nr;
    char tmp[32];

    open_devnull_stdio();
    log_init();

    INFO("starting ueventd\n");

    /* Since we cannot talk to the property service as
     * this is the init program instead of a stand-alone
     * executable, we have to pull the kernel commandline
     * file in to look for hardware name. */
    import_kernel_cmdline();

    get_hardware_name(hardware, &revision);

    ueventd_parse_config_file("/ueventd.rc");

    snprintf(tmp, sizeof(tmp), "/ueventd.%s.rc", hardware);
    ueventd_parse_config_file(tmp);

    device_init();

    ufd.events = POLLIN;
    ufd.fd = get_device_fd();

    while(1) {
        ufd.revents = 0;
        nr = poll(&ufd, 1, -1);
        if (nr <= 0)
            continue;
        if (ufd.revents == POLLIN)
               handle_device_fd();
    }
}

static int get_android_id(const char *id)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(android_ids); i++)
        if (!strcmp(id, android_ids[i].name))
            return android_ids[i].aid;
    return 0;
}

void set_device_permission(int nargs, char **args)
{
    char *name;
    char *attr = 0;
    mode_t perm;
    uid_t uid;
    gid_t gid;
    int prefix = 0;
    char *endptr;
    int ret;
    char *tmp = 0;

    if (nargs == 0)
        return;

    if (args[0][0] == '#')
        return;

    name = args[0];

    if (!strncmp(name,"/sys/", 5) && (nargs == 5)) {
        INFO("/sys/ rule %s %s\n",args[0],args[1]);
        attr = args[1];
        args++;
        nargs--;
    }

    if (nargs != 4) {
        ERROR("invalid line ueventd.rc line for '%s'\n", args[0]);
        return;
    }

    /* If path starts with mtd@ lookup the mount number. */
    if (!strncmp(name, "mtd@", 4)) {
        int n = mtd_name_to_number(name + 4);
        if (n >= 0)
            asprintf(&tmp, "/dev/mtd/mtd%d", n);
        name = tmp;
    } else {
        int len = strlen(name);
        if (name[len - 1] == '*') {
            prefix = 1;
            name[len - 1] = '\0';
        }
    }

    perm = strtol(args[1], &endptr, 8);
    if (!endptr || *endptr != '\0') {
        ERROR("invalid mode '%s'\n", args[1]);
        free(tmp);
        return;
    }

    ret = get_android_id(args[2]);
    if (ret < 0) {
        ERROR("invalid uid '%s'\n", args[2]);
        free(tmp);
        return;
    }
    uid = ret;

    ret = get_android_id(args[3]);
    if (ret < 0) {
        ERROR("invalid gid '%s'\n", args[3]);
        free(tmp);
        return;
    }
    gid = ret;

    add_dev_perms(name, attr, perm, uid, gid, prefix);
    free(tmp);
}
