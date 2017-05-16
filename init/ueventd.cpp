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

#include "ueventd.h"

#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <thread>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <selinux/android.h>
#include <selinux/selinux.h>

#include "coldboot.h"
#include "devices.h"
#include "firmware_handler.h"
#include "log.h"
#include "uevent_listener.h"
#include "ueventd_parser.h"
#include "util.h"

// At a high level, ueventd listens for uevent messages generated by the kernel through a netlink
// socket.  When ueventd receives such a message it handles it by taking appropriate actions,
// which can typically be creating a device node in /dev, setting file permissions, setting selinux
// labels, etc.
// Ueventd also handles loading of firmware that the kernel requests, and creates symlinks for block
// and character devices.

// When ueventd starts, ueventd 'cold boots' the /sys filesystem, meaning that it traverses /sys,
// writing 'add' to each 'uevent' file that it finds.  This causes the kernel to generate and resend
// uevent messages for all of the currently registered devices.  This is done, because ueventd would
// not have been running when these devices were registered and therefore was unable to receive
// their uevent messages and handle them appropriately.

// 'init' currently waits synchronously on the cold boot process of ueventd before it continues
// its boot process.  For this reason, coldboot should be as quick as possible.  One way to achieve
// a speed up here is to parallelize the handling of ueventd messages, which consume the bulk of the
// time during cold boot.

// Handling of uevent messages has two unique properties:
// 1) It can be done in isolation; it doesn't need to read or write any status once it is started.
// 2) It uses setegid() and setfscreatecon() so either care (aka locking) must be taken to ensure
//    that no file system operations are done while the uevent process has an abnormal egid or
//    fscreatecon or this handling must happen in a separate process.
// Given the above two properties, it is best to fork() subprocesses to handle the uevents.  This
// reduces the overhead and complexity that would be required in a solution with threads and locks.
// In testing, a racy multithreaded solution has the same performance as the fork() solution, so
// there is no reason to deal with the complexity of the former.

// One other important caveat during the boot process is the handling of SELinux restorecon.
// Since many devices have child devices, calling selinux_android_restorecon() recursively for each
// device when its uevent is handled, results in multiple restorecon operations being done on a
// given file.  It is more efficient to simply do restorecon recursively on /sys during coldboot,
// than to do restorecon on each device as its uevent is handled.  This only applies to coldboot;
// once that has completed, restorecon is done for each device as its uevent is handled.

// With all of the above considered, the coldboot process has the below steps:
// 1) ueventd allocates 'n' separate vectors of uevents corresponding to 'n' separate uevent handler
//    subprocesses.  'n' is set to std::thread::hardware_concurrency(), which is typically the
//    number of cores on the system.  It defaults to 4 if that cannot be determined.
//
// 2) ueventd does the cold boot traversal of /sys, listens to the netlink socket for uevents,
//    and writes these uevents evenly into the 'n' separate vectors of uevents.
//
// 3) ueventd forks 'n' separate uevent handler subprocesses and has each of them to handle the
//    uevents in their given vector.  Note that no IPC happens at this point and only const
//    functions from DeviceHandler should be called from this context.
//
// 4) In parallel to the subprocesses handling the uevents, the main thread of ueventd calls
//    selinux_android_restorecon() recursively on /sys/class, /sys/block, and /sys/devices.
//
// 5) Once the restorecon operation finishes, the main thread calls wait() to wait for all
//    subprocess handlers to complete and exit.  Once this happens, it marks coldboot as having
//    completed.
//
// At this point, ueventd is single threaded, poll()'s and then handled any future uevents.

// Lastly, it should be noted that uevents that occur during the coldboot process are handled
// without issue after the coldboot process completes.  This is because the uevent listener is
// paused while the uevent handler and restorecon actions take place.  Once coldboot completes,
// the uevent listener resumes in polling mode and will handle the uevents that occurred during
// coldboot.

class Ueventd {
  public:
    Ueventd()
        : device_handler_(CreateDeviceHandler()),
          num_handler_subprocesses_(std::thread::hardware_concurrency() ?: 4),
          uevent_queue_per_process_(num_handler_subprocesses_) {}

    int Run();

  private:
    static DeviceHandler CreateDeviceHandler();
    void UeventHandlerMain(unsigned int process_num);
    void DoColdBoot();

    UeventListener uevent_listener_;
    DeviceHandler device_handler_;

    unsigned int num_handler_subprocesses_;
    std::vector<std::vector<Uevent>> uevent_queue_per_process_;
};

DeviceHandler Ueventd::CreateDeviceHandler() {
    Parser parser;

    std::vector<Subsystem> subsystems;
    parser.AddSectionParser("subsystem", std::make_unique<SubsystemParser>(&subsystems));

    using namespace std::placeholders;
    std::vector<SysfsPermissions> sysfs_permissions;
    std::vector<Permissions> dev_permissions;
    parser.AddSingleLineParser(
        "/sys/", std::bind(ParsePermissionsLine, _1, _2, &sysfs_permissions, nullptr));
    parser.AddSingleLineParser("/dev/",
                               std::bind(ParsePermissionsLine, _1, _2, nullptr, &dev_permissions));

    parser.ParseConfig("/ueventd.rc");
    parser.ParseConfig("/vendor/ueventd.rc");
    parser.ParseConfig("/odm/ueventd.rc");

    /*
     * keep the current product name base configuration so
     * we remain backwards compatible and allow it to override
     * everything
     * TODO: cleanup platform ueventd.rc to remove vendor specific
     * device node entries (b/34968103)
     */
    std::string hardware = android::base::GetProperty("ro.hardware", "");
    parser.ParseConfig("/ueventd." + hardware + ".rc");

    return DeviceHandler(std::move(dev_permissions), std::move(sysfs_permissions),
                         std::move(subsystems), true);
}

void Ueventd::UeventHandlerMain(unsigned int process_num) {
    auto& queue = uevent_queue_per_process_[process_num];
    int i = 0;
    for (const Uevent& uevent : queue) {
        ++i;
        if (uevent.action == "add" || uevent.action == "change" || uevent.action == "online") {
            device_handler_.FixupSysPermissions(uevent.path, uevent.subsystem);
        }

        if (uevent.subsystem == "block") {
            device_handler_.HandleBlockDeviceEvent(uevent);
        } else {
            device_handler_.HandleGenericDeviceEvent(uevent);
        }
    }
}

void Ueventd::DoColdBoot() {
    Timer cold_boot_timer;

    int uevents_seen = 0;

    ColdBoot([this, &uevents_seen]() {
        Uevent uevent;
        while (uevent_listener_.ReadUevent(&uevent)) {
            ++uevents_seen;
            HandleFirmwareEvent(uevent);

            // This is the one mutable part of DeviceHandler, in which platform devices are
            // added to a vector for later reference.  Since there is no communication after
            // fork()'ing subprocess handlers, all platform devices must be in the vector before
            // we fork, and therefore they must be handled in this loop.
            if (uevent.subsystem == "platform") {
                device_handler_.HandlePlatformDeviceEvent(uevent);
            }

            unsigned int queue_num = uevents_seen % num_handler_subprocesses_;
            uevent_queue_per_process_[queue_num].emplace_back(std::move(uevent));
        }
        return false;
    });

    for (unsigned int i = 0; i < num_handler_subprocesses_; ++i) {
        auto pid = fork();
        if (pid < 0) {
            PLOG(FATAL) << "fork() failed!";
        } else if (pid == 0) {
            UeventHandlerMain(i);
            _exit(1);
        }
    }

    for (const char* path : kColdBootPaths) {
        selinux_android_restorecon(path, SELINUX_ANDROID_RESTORECON_RECURSE);
    }

    // wait() with SIGCHLD ignored blocks until all children have terminated.
    int status;
    wait(&status);

    device_handler_.set_is_booting(false);

    close(open(COLDBOOT_DONE, O_WRONLY | O_CREAT | O_CLOEXEC, 0000));
    LOG(INFO) << "Coldboot took " << cold_boot_timer;
}

int Ueventd::Run() {
    /*
     * init sets the umask to 077 for forked processes. We need to
     * create files with exact permissions, without modification by
     * the umask.
     */
    umask(000);

    /* Prevent fire-and-forget children from becoming zombies.
     * If we should need to wait() for some children in the future
     * (as opposed to none right now), double-forking here instead
     * of ignoring SIGCHLD may be the better solution.
     */
    signal(SIGCHLD, SIG_IGN);

    LOG(INFO) << "ueventd started!";

    selinux_callback cb;
    cb.func_log = selinux_klog_callback;
    selinux_set_callback(SELINUX_CB_LOG, cb);

    if (access(COLDBOOT_DONE, F_OK) != 0) DoColdBoot();

    pollfd ufd;
    ufd.events = POLLIN;
    ufd.fd = uevent_listener_.device_fd();

    while (true) {
        ufd.revents = 0;
        int nr = poll(&ufd, 1, -1);
        if (nr <= 0) {
            continue;
        }
        if (ufd.revents & POLLIN) {
            // We're non-blocking, so if we receive a poll event keep processing until there
            // we have exhausted all uevent messages.
            Uevent uevent;
            while (uevent_listener_.ReadUevent(&uevent)) {
                HandleFirmwareEvent(uevent);
                device_handler_.HandleDeviceEvent(uevent);
            }
        }
    }

    return 0;
}

int ueventd_main(int argc, char** argv) {
    InitKernelLogging(argv);

    Ueventd ueventd;
    return ueventd.Run();
}
