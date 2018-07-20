/*
 * Copyright (C) 2018 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <thread>
#include <vector>

#include <android-base/stringprintf.h>
#include <gtest/gtest.h>

#include "fastboot_driver.h"
#include "usb.h"

#include "extensions.h"
#include "fixtures.h"
#include "test_utils.h"
#include "usb_transport_sniffer.h"

namespace fastboot {

extension::Configuration config;  // The parsed XML config

// Annoying hack because gtest's INSTANTIATE_TEST_CASE_P() must be at global scope
std::vector<std::pair<std::string, extension::Configuration::GetVar>> GETVAR_XML_TESTS;
std::vector<std::tuple<std::string, bool, extension::Configuration::CommandTest>> OEM_XML_TESTS;
std::vector<std::pair<std::string, extension::Configuration::PartitionInfo>> PARTITION_XML_TESTS;
std::vector<std::pair<std::string, extension::Configuration::PartitionInfo>>
        PARTITION_XML_WRITEABLE;
std::vector<std::pair<std::string, extension::Configuration::PartitionInfo>>
        PARTITION_XML_WRITE_HASHABLE;
std::vector<std::pair<std::string, extension::Configuration::PartitionInfo>>
        PARTITION_XML_WRITE_PARSED;
std::vector<std::pair<std::string, extension::Configuration::PartitionInfo>>
        PARTITION_XML_WRITE_HASH_NONPARSED;

const std::string DEFAULT_OUPUT_PATH = "/tmp/out.img";
// const char scratch_partition[] = "userdata";
const std::vector<std::string> CMDS{"boot",        "continue", "download:", "erase:",
                                    "flash:",      "getvar:",  "powerdown", "reboot",
                                    "set_active:", "upload",   "verify"};

// For pretty printing we need all these overloads
::std::ostream& operator<<(::std::ostream& os, const RetCode& ret) {
    return os << FastBootDriver::RCString(ret);
}

bool PartitionHash(FastBootDriver* fb, const std::string& part, std::string* hash, int* retcode,
                   std::string* err_msg) {
    if (config.checksum.empty()) {
        return -1;
    }

    std::string resp;
    std::vector<std::string> info;
    const std::string cmd = config.checksum + ' ' + part;
    RetCode ret;
    if ((ret = fb->RawCommand(cmd, &resp, &info)) != SUCCESS) {
        *err_msg =
                android::base::StringPrintf("Hashing partition with command '%s' failed with: %s",
                                            cmd.c_str(), fb->RCString(ret).c_str());
        return false;
    }
    std::stringstream imploded;
    std::copy(info.begin(), info.end(), std::ostream_iterator<std::string>(imploded, "\n"));

    // If payload, we validate that as well
    const std::vector<std::string> args = SplitBySpace(config.checksum_parser);
    std::vector<std::string> prog_args(args.begin() + 1, args.end());
    prog_args.push_back(resp);            // Pass in the full command
    prog_args.push_back(imploded.str());  // Pass in the save location

    int pipe;
    pid_t pid = StartProgram(args[0], prog_args, &pipe);
    if (pid <= 0) {
        *err_msg = android::base::StringPrintf("Launching hash parser '%s' failed with: %s",
                                               config.checksum_parser.c_str(), strerror(errno));
        return false;
    }
    *retcode = WaitProgram(pid, pipe, hash);
    if (*retcode) {
        // In this case the stderr pipe is a log message
        *err_msg = android::base::StringPrintf("Hash parser '%s' failed with: %s",
                                               config.checksum_parser.c_str(), hash->c_str());
        return false;
    }
    return true;
}

// Only allow alphanumeric, _, -, and .
const auto not_allowed = [](char c) -> int {
    return !(isalnum(c) || c == '_' || c == '-' || c == '.');
};

// Test that USB even works
TEST(USBFunctionality, USBConnect) {
    const auto matcher = [](usb_ifc_info* info) -> int {
        return FastBootTest::MatchFastboot(info, nullptr);
    };
    Transport* transport = nullptr;
    for (int i = 0; i < FastBootTest::MAX_USB_TRIES && !transport; i++) {
        transport = usb_open(matcher);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_NE(transport, nullptr) << "Could not find the fastboot device after: "
                                  << 10 * FastBootTest::MAX_USB_TRIES << "ms";
    if (transport) {
        transport->Close();
        delete transport;
    }
}

// Conformance tests
TEST_F(Conformance, GetVar) {
    std::string product;
    EXPECT_EQ(fb->GetVar("product", &product), SUCCESS) << "getvar:product failed";
    EXPECT_NE(product, "") << "getvar:product response was empty string";
    EXPECT_EQ(std::count_if(product.begin(), product.end(), not_allowed), 0)
            << "getvar:product response contained illegal chars";
    EXPECT_LE(product.size(), FB_RESPONSE_SZ - 4) << "getvar:product response was too large";
}

TEST_F(Conformance, GetVarVersionBootloader) {
    std::string var;
    EXPECT_EQ(fb->GetVar("version-bootloader", &var), SUCCESS)
            << "getvar:version-bootloader failed";
    EXPECT_NE(var, "") << "getvar:version-bootloader response was empty string";
    EXPECT_EQ(std::count_if(var.begin(), var.end(), not_allowed), 0)
            << "getvar:version-bootloader response contained illegal chars";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4) << "getvar:version-bootloader response was too large";
    ;
}

TEST_F(Conformance, GetVarVersionBaseband) {
    std::string var;
    EXPECT_EQ(fb->GetVar("version-baseband", &var), SUCCESS) << "getvar:version-baseband failed";
    EXPECT_NE(var, "") << "getvar:version-baseband response was empty string";
    EXPECT_EQ(std::count_if(var.begin(), var.end(), not_allowed), 0)
            << "getvar:version-baseband response contained illegal chars";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4) << "getvar:version-baseband response was too large";
}

TEST_F(Conformance, GetVarSerialNo) {
    std::string var;
    EXPECT_EQ(fb->GetVar("serialno", &var), SUCCESS) << "getvar:serialno failed";
    EXPECT_NE(var, "") << "getvar:serialno can not be empty string";
    EXPECT_EQ(std::count_if(var.begin(), var.end(), isalnum), var.size())
            << "getvar:serialno must be alpha-numeric";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4) << "getvar:serialno response is too long";
}

TEST_F(Conformance, GetVarSecure) {
    std::string var;
    EXPECT_EQ(fb->GetVar("secure", &var), SUCCESS);
    EXPECT_TRUE(var == "yes" || var == "no");
}

TEST_F(Conformance, GetVarOffModeCharge) {
    std::string var;
    EXPECT_EQ(fb->GetVar("off-mode-charge", &var), SUCCESS) << "getvar:off-mode-charge failed";
    EXPECT_TRUE(var == "0" || var == "1") << "getvar:off-mode-charge response must be '0' or '1'";
}

TEST_F(Conformance, GetVarVariant) {
    std::string var;
    EXPECT_EQ(fb->GetVar("variant", &var), SUCCESS) << "getvar:variant failed";
    EXPECT_NE(var, "") << "getvar:variant response can not be empty";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4) << "getvar:variant response is too large";
}

TEST_F(Conformance, GetVarRevision) {
    std::string var;
    EXPECT_EQ(fb->GetVar("hw-revision", &var), SUCCESS) << "getvar:hw-revision failed";
    EXPECT_NE(var, "") << "getvar:battery-voltage response was empty";
    EXPECT_EQ(std::count_if(var.begin(), var.end(), not_allowed), 0)
            << "getvar:hw-revision contained illegal ASCII chars";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4) << "getvar:hw-revision response was too large";
}

TEST_F(Conformance, GetVarBattVoltage) {
    std::string var;
    EXPECT_EQ(fb->GetVar("battery-voltage", &var), SUCCESS) << "getvar:battery-voltage failed";
    EXPECT_NE(var, "") << "getvar:battery-voltage response was empty";
    EXPECT_EQ(std::count_if(var.begin(), var.end(), not_allowed), 0)
            << "getvar:battery-voltage response contains illegal ASCII chars";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4)
            << "getvar:battery-voltage response is too large: " + var;
}

TEST_F(Conformance, GetVarBattVoltageOk) {
    std::string var;
    EXPECT_EQ(fb->GetVar("battery-soc-ok", &var), SUCCESS) << "getvar:battery-soc-ok failed";
    EXPECT_TRUE(var == "yes" || var == "no") << "getvar:battery-soc-ok must be 'yes' or 'no'";
}

TEST_F(Conformance, GetVarDownloadSize) {
    std::string var;
    EXPECT_EQ(fb->GetVar("max-download-size", &var), SUCCESS) << "getvar:max-download-size failed";
    EXPECT_NE(var, "") << "getvar:max-download-size responded with empty string";
    // This must start with 0x
    EXPECT_FALSE(isspace(var.front()))
            << "getvar:max-download-size responded with a string with leading whitespace";
    EXPECT_FALSE(var.compare(0, 2, "0x"))
            << "getvar:max-download-size responded with a string that does not start with 0x...";
    int64_t size = strtoll(var.c_str(), nullptr, 16);
    EXPECT_GT(size, 0) << "'" + var + "' is not a valid response from getvar:max-download-size";
    // At most 32-bits
    EXPECT_LE(size, std::numeric_limits<uint32_t>::max())
            << "getvar:max-download-size must fit in a uint32_t";
    EXPECT_LE(var.size(), FB_RESPONSE_SZ - 4)
            << "getvar:max-download-size responded with too large of string: " + var;
}

TEST_F(Conformance, GetVarAll) {
    std::vector<std::string> vars;
    EXPECT_EQ(fb->GetVarAll(&vars), SUCCESS) << "getvar:all failed";
    EXPECT_GT(vars.size(), 0) << "getvar:all did not respond with any INFO responses";
    for (const auto s : vars) {
        EXPECT_LE(s.size(), FB_RESPONSE_SZ - 4)
                << "getvar:all included an INFO response: 'INFO" + s << "' which is too long";
    }
}

TEST_F(Conformance, PartitionInfo) {
    std::vector<std::tuple<std::string, uint32_t>> parts;
    EXPECT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed";
    EXPECT_GT(parts.size(), 0)
            << "getvar:all did not report any partition-size: through INFO responses";
    std::set<std::string> allowed{"ext4", "f2fs", "raw"};
    for (const auto p : parts) {
        EXPECT_GT(std::get<1>(p), 0);
        std::string part(std::get<0>(p));
        std::set<std::string> allowed{"ext4", "f2fs", "raw"};
        std::string resp;
        EXPECT_EQ(fb->GetVar("partition-type:" + part, &resp), SUCCESS);
        EXPECT_NE(allowed.find(resp), allowed.end()) << "getvar:partition-type:" + part << " was '"
                                                     << resp << "' this is not a valid type";
        const std::string cmd = "partition-size:" + part;
        EXPECT_EQ(fb->GetVar(cmd, &resp), SUCCESS);

        // This must start with 0x
        EXPECT_FALSE(isspace(resp.front()))
                << cmd + " responded with a string with leading whitespace";
        EXPECT_FALSE(resp.compare(0, 2, "0x"))
                << cmd + "responded with a string that does not start with 0x...";
        int64_t size = strtoll(resp.c_str(), nullptr, 16);
        EXPECT_GT(size, 0) << "'" + resp + "' is not a valid response from " + cmd;
    }
}

TEST_F(Conformance, Slots) {
    std::string var;
    ASSERT_EQ(fb->GetVar("slot-count", &var), SUCCESS) << "getvar:slot-count failed";
    ASSERT_EQ(std::count_if(var.begin(), var.end(), isdigit), var.size())
            << "'" << var << "' is not all digits which it should be for getvar:slot-count";
    int32_t num_slots = strtol(var.c_str(), nullptr, 10);

    // Can't run out of alphabet letters...
    ASSERT_LE(num_slots, 26) << "What?! You can't have more than 26 slots";

    std::vector<std::tuple<std::string, uint32_t>> parts;
    EXPECT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed";

    std::map<std::string, std::set<char>> part_slots;
    if (num_slots > 0) {
        EXPECT_EQ(fb->GetVar("current-slot", &var), SUCCESS) << "getvar:current-slot failed";

        for (const auto p : parts) {
            std::string part(std::get<0>(p));
            std::regex reg("([[:graph:]]*)_([[:lower:]])");
            std::smatch sm;

            if (std::regex_match(part, sm, reg)) {  // This partition has slots
                std::string part_base(sm[1]);
                std::string slot(sm[2]);
                EXPECT_EQ(fb->GetVar("has-slot:" + part_base, &var), SUCCESS)
                        << "'getvar:has-slot:" << part_base << "' failed";
                EXPECT_EQ(var, "yes") << "'getvar:has-slot:" << part_base << "' was not 'yes'";
                EXPECT_TRUE(islower(slot.front()))
                        << "'" << slot.front() << "' is an invalid slot-suffix for " << part_base;
                std::set<char> tmp{slot.front()};
                part_slots.emplace(part_base, tmp);
                part_slots.at(part_base).insert(slot.front());
            } else {
                EXPECT_EQ(fb->GetVar("has-slot:" + part, &var), SUCCESS)
                        << "'getvar:has-slot:" << part << "' failed";
                EXPECT_EQ(var, "no") << "'getvar:has-slot:" << part << "' should be no";
            }
        }
        // Ensure each partition has the correct slot suffix
        for (const auto iter : part_slots) {
            const std::set<char>& char_set = iter.second;
            std::string chars;
            for (char c : char_set) {
                chars += c;
                chars += ',';
            }
            EXPECT_EQ(char_set.size(), num_slots)
                    << "There should only be slot suffixes from a to " << 'a' + num_slots - 1
                    << " instead encountered: " << chars;
            for (const char c : char_set) {
                EXPECT_GE(c, 'a') << "Encountered invalid slot suffix of '" << c << "'";
                EXPECT_LT(c, 'a' + num_slots) << "Encountered invalid slot suffix of '" << c << "'";
            }
        }
    }
}

TEST_F(Conformance, SetActive) {
    std::string var;
    ASSERT_EQ(fb->GetVar("slot-count", &var), SUCCESS) << "getvar:slot-count failed";
    ASSERT_EQ(std::count_if(var.begin(), var.end(), isdigit), var.size())
            << "'" << var << "' is not all digits which it should be for getvar:slot-count";
    int32_t num_slots = strtol(var.c_str(), nullptr, 10);

    // Can't run out of alphabet letters...
    ASSERT_LE(num_slots, 26) << "What?! You can't have more than 26 slots";

    std::vector<std::tuple<std::string, uint32_t>> parts;
    ASSERT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed";

    if (num_slots > 0) {
        ASSERT_EQ(fb->GetVar("current-slot", &var), SUCCESS) << "getvar:current-slot failed";

        for (const auto p : parts) {
            std::string part(std::get<0>(p));
            std::regex reg("([[:graph:]]*)_([[:lower:]])");
            std::smatch sm;

            if (std::regex_match(part, sm, reg)) {  // This partition has slots
                std::string slot(sm[2]);
                ASSERT_EQ(fb->SetActive(part), SUCCESS) << "Set active failed";
                ASSERT_EQ(fb->GetVar("current-slot", &var), SUCCESS)
                        << "getvar:current-slot failed";
                EXPECT_EQ(slot, var)
                        << "getvar:current-slot repots incorrect slot after setting it";
            }
        }
    }
}

TEST_F(Conformance, LockAndUnlockPrompt) {
    std::string resp;
    ASSERT_EQ(fb->GetVar("unlocked", &resp), SUCCESS) << "getvar:unlocked failed";
    ASSERT_TRUE(resp == "yes" || resp == "no")
            << "Device did not respond with 'yes' or 'no' for getvar:unlocked";
    bool curr = resp == "yes";

    for (int i = 0; i < 2; i++) {
        std::string action = !curr ? "unlock" : "lock";
        printf("Device should prompt to '%s' bootloader, select 'no'\n", action.c_str());
        ChangeLockState(!curr, false);
        ASSERT_EQ(fb->GetVar("unlocked", &resp), SUCCESS) << "getvar:unlocked failed";
        ASSERT_EQ(resp, curr ? "yes" : "no") << "The locked/unlocked state of the bootloader "
                                                "incorrectly changed after selecting no";
        printf("Device should prompt to '%s' bootloader, select 'yes'\n", action.c_str());
        ChangeLockState(!curr, true);
        ASSERT_EQ(fb->GetVar("unlocked", &resp), SUCCESS) << "getvar:unlocked failed";
        ASSERT_EQ(resp, !curr ? "yes" : "no") << "The locked/unlocked state of the bootloader "
                                                 "failed to change after selecting yes";
        curr = !curr;
    }
}

TEST_F(UnlockPermissions, Download) {
    std::vector<char> buf{'a', 'o', 's', 'p'};
    EXPECT_EQ(fb->Download(buf), SUCCESS) << "Download 4-byte payload failed";
}

TEST_F(UnlockPermissions, DownloadFlash) {
    std::vector<char> buf{'a', 'o', 's', 'p'};
    EXPECT_EQ(fb->Download(buf), SUCCESS) << "Download failed in unlocked mode";
    ;
    std::vector<std::tuple<std::string, uint32_t>> parts;
    EXPECT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed in unlocked mode";
}

TEST_F(LockPermissions, DownloadFlash) {
    std::vector<char> buf{'a', 'o', 's', 'p'};
    EXPECT_EQ(fb->Download(buf), SUCCESS) << "Download failed in locked mode";
    std::vector<std::tuple<std::string, uint32_t>> parts;
    EXPECT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed in locked mode";
    std::string resp;
    for (const auto tup : parts) {
        EXPECT_EQ(fb->Flash(std::get<0>(tup), &resp), DEVICE_FAIL)
                << "Device did not respond with FAIL when trying to flash '" << std::get<0>(tup)
                << "' in locked mode";
        EXPECT_GT(resp.size(), 0)
                << "Device sent empty error message after FAIL";  // meaningful error message
    }
}

TEST_F(LockPermissions, Erase) {
    std::vector<std::tuple<std::string, uint32_t>> parts;
    EXPECT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed";
    std::string resp;
    for (const auto tup : parts) {
        EXPECT_EQ(fb->Erase(std::get<0>(tup), &resp), DEVICE_FAIL)
                << "Device did not respond with FAIL when trying to erase '" << std::get<0>(tup)
                << "' in locked mode";
        EXPECT_GT(resp.size(), 0) << "Device sent empty error message after FAIL";
    }
}

TEST_F(LockPermissions, SetActive) {
    std::vector<std::tuple<std::string, uint32_t>> parts;
    EXPECT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed";

    std::string resp;
    EXPECT_EQ(fb->GetVar("slot-count", &resp), SUCCESS) << "getvar:slot-count failed";
    int32_t num_slots = strtol(resp.c_str(), nullptr, 10);

    for (const auto tup : parts) {
        std::string part(std::get<0>(tup));
        std::regex reg("([[:graph:]]*)_([[:lower:]])");
        std::smatch sm;

        if (std::regex_match(part, sm, reg)) {  // This partition has slots
            std::string part_base(sm[1]);
            for (char c = 'a'; c < 'a' + num_slots; c++) {
                // We should not be able to SetActive any of these
                EXPECT_EQ(fb->SetActive(part_base + '_' + c, &resp), DEVICE_FAIL)
                        << "set:active:" << part_base + '_' + c << " did not fail in locked mode";
            }
        }
    }
}

TEST_F(LockPermissions, Boot) {
    std::vector<char> buf;
    buf.resize(1000);
    EXPECT_EQ(fb->Download(buf), SUCCESS) << "A 1000 byte download failed";
    std::string resp;
    ASSERT_EQ(fb->Boot(&resp), DEVICE_FAIL)
            << "The device did not respond with failure for 'boot' when locked";
    EXPECT_GT(resp.size(), 0) << "No error message was returned by device after FAIL";
}

TEST_F(Fuzz, DownloadSize) {
    std::string var;
    EXPECT_EQ(fb->GetVar("max-download-size", &var), SUCCESS) << "getvar:max-download-size failed";
    int64_t size = strtoll(var.c_str(), nullptr, 0);
    EXPECT_GT(size, 0) << '\'' << var << "' is not a valid response for getvar:max-download-size";

    EXPECT_EQ(DownloadCommand(size + 1), DEVICE_FAIL)
            << "Device reported max-download-size as '" << size
            << "' but did not reject a download of " << size + 1;

    std::vector<char> buf(size);
    EXPECT_EQ(fb->Download(buf), SUCCESS) << "Device reported max-download-size as '" << size
                                          << "' but downloading a payload of this size failed";
    ASSERT_TRUE(UsbStillAvailible()) << USB_PORT_GONE;
}

TEST_F(Fuzz, DownloadLargerBuf) {
    std::vector<char> buf{'a', 'o', 's', 'p'};
    ASSERT_EQ(DownloadCommand(buf.size() - 1), SUCCESS)
            << "Download command for " << buf.size() - 1 << " bytes failed";
    // There are two ways to handle this
    // Accept download, but send error response
    // Reject the download outright
    std::string resp;
    RetCode ret = SendBuffer(buf);
    EXPECT_TRUE(UsbStillAvailible()) << USB_PORT_GONE;
    if (ret == SUCCESS) {
        // If it accepts the buffer, it better send back an error response
        EXPECT_EQ(HandleResponse(&resp), DEVICE_FAIL)
                << "After sending too small of a payload for a download command, device accepted "
                   "payload and did not respond with FAIL";
    } else {
        EXPECT_EQ(ret, IO_ERROR) << "After sending too small of a payload for a download command, "
                                    "device did not return error";
    }

    ASSERT_TRUE(UsbStillAvailible()) << USB_PORT_GONE;
    // The device better still work after all that if we unplug and replug
    EXPECT_EQ(transport->Reset(), 0) << "USB reset failed";
    EXPECT_EQ(fb->GetVar("product", &resp), SUCCESS) << "getvar:product failed";
}

TEST_F(Fuzz, DownloadOverRun) {
    std::vector<char> buf(1000, 'F');
    ASSERT_EQ(DownloadCommand(10), SUCCESS) << "Device rejected download request for 10 bytes";
    // There are two ways to handle this
    // Accept download, but send error response
    // Reject the download outright
    std::string resp;
    RetCode ret = SendBuffer(buf);
    if (ret == SUCCESS) {
        // If it accepts the buffer, it better send back an error response
        EXPECT_EQ(HandleResponse(&resp), DEVICE_FAIL)
                << "After sending too large of a payload for a download command, device accepted "
                   "payload and did not respond with FAIL";
    } else {
        EXPECT_EQ(ret, IO_ERROR) << "After sending too large of a payload for a download command, "
                                    "device did not return error";
    }

    ASSERT_TRUE(UsbStillAvailible()) << USB_PORT_GONE;
    // The device better still work after all that if we unplug and replug
    EXPECT_EQ(transport->Reset(), 0) << "USB reset failed";
    EXPECT_EQ(fb->GetVar("product", &resp), SUCCESS)
            << "Device did not respond with SUCCESS to getvar:product.";
}

TEST_F(Fuzz, DownloadInvalid1) {
    EXPECT_EQ(DownloadCommand(0), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command 'download:0'";
}

TEST_F(Fuzz, DownloadInvalid2) {
    std::string cmd("download:1");
    EXPECT_EQ(fb->RawCommand("download:1"), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, DownloadInvalid3) {
    std::string cmd("download:-1");
    EXPECT_EQ(fb->RawCommand("download:-1"), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, DownloadInvalid4) {
    std::string cmd("download:-01000000");
    EXPECT_EQ(fb->RawCommand(cmd), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, DownloadInvalid5) {
    std::string cmd("download:-0100000");
    EXPECT_EQ(fb->RawCommand(cmd), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, DownloadInvalid6) {
    std::string cmd("download:");
    EXPECT_EQ(fb->RawCommand(cmd), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, DownloadInvalid7) {
    std::string cmd("download:01000000\0999", sizeof("download:01000000\0999"));
    EXPECT_EQ(fb->RawCommand(cmd), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, DownloadInvalid8) {
    std::string cmd("download:01000000\0dkjfvijafdaiuybgidabgybr",
                    sizeof("download:01000000\0dkjfvijafdaiuybgidabgybr"));
    EXPECT_EQ(fb->RawCommand(cmd), DEVICE_FAIL)
            << "Device did not respond with FAIL for malformed download command '" << cmd << "'";
}

TEST_F(Fuzz, GetVarAllSpam) {
    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed;
    unsigned i = 1;
    do {
        std::vector<std::string> vars;
        ASSERT_EQ(fb->GetVarAll(&vars), SUCCESS) << "Device did not respond with success after "
                                                 << i << "getvar:all commands in a row";
        ASSERT_GT(vars.size(), 0)
                << "Device did not send any INFO responses after getvar:all command";
        elapsed = std::chrono::high_resolution_clock::now() - start;
    } while (i++, elapsed.count() < 5);
}

TEST_F(Fuzz, BadCommandTooLarge) {
    std::string s1 = RandomString(1000, rand_legal);
    EXPECT_EQ(fb->RawCommand(s1), DEVICE_FAIL)
            << "Device did not respond with failure after sending length " << s1.size()
            << " string of random ASCII chars";
    std::string s2 = RandomString(1000, rand_illegal);
    EXPECT_EQ(fb->RawCommand(s2), DEVICE_FAIL)
            << "Device did not respond with failure after sending length " << s1.size()
            << " string of random non-ASCII chars";
    std::string s3 = RandomString(1000, rand_char);
    EXPECT_EQ(fb->RawCommand(s3), DEVICE_FAIL)
            << "Device did not respond with failure after sending length " << s1.size()
            << " string of random chars";
}

TEST_F(Fuzz, CommandTooLarge) {
    for (const std::string& s : CMDS) {
        std::string rs = RandomString(1000, rand_char);
        EXPECT_EQ(fb->RawCommand(s + rs), DEVICE_FAIL)
                << "Device did not respond with failure after '" << s + rs << "'";
        ASSERT_TRUE(UsbStillAvailible()) << USB_PORT_GONE;
        std::string resp;
        EXPECT_EQ(fb->GetVar("product", &resp), SUCCESS)
                << "Device is unresponsive to getvar command";
    }
}

TEST_F(Fuzz, CommandMissingArgs) {
    for (const std::string& s : CMDS) {
        if (s.back() == ':') {
            EXPECT_EQ(fb->RawCommand(s), DEVICE_FAIL)
                    << "Device did not respond with failure after '" << s << "'";
            std::string sub(s.begin(), s.end() - 1);
            EXPECT_EQ(fb->RawCommand(sub), DEVICE_FAIL)
                    << "Device did not respond with failure after '" << sub << "'";
        } else {
            std::string rs = RandomString(10, rand_illegal);
            EXPECT_EQ(fb->RawCommand(rs + s), DEVICE_FAIL)
                    << "Device did not respond with failure after '" << rs + s << "'";
        }
        std::string resp;
        EXPECT_EQ(fb->GetVar("product", &resp), SUCCESS)
                << "Device is unresponsive to getvar command";
    }
}

TEST_F(Fuzz, USBResetSpam) {
    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed;
    int i = 0;
    do {
        ASSERT_EQ(transport->Reset(), 0) << "USB Reset failed after " << i << " resets in a row";
        elapsed = std::chrono::high_resolution_clock::now() - start;
    } while (i++, elapsed.count() < 5);
    std::string resp;
    EXPECT_EQ(fb->GetVar("product", &resp), SUCCESS)
            << "getvar failed after " << i << " USB reset(s) in a row";
}

TEST_F(Fuzz, USBResetCommandSpam) {
    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed;
    do {
        std::string resp;
        std::vector<std::string> all;
        ASSERT_EQ(transport->Reset(), 0) << "USB Reset failed";
        EXPECT_EQ(fb->GetVarAll(&all), SUCCESS) << "getvar:all failed after USB reset";
        EXPECT_EQ(fb->GetVar("product", &resp), SUCCESS) << "getvar:product failed";
        elapsed = std::chrono::high_resolution_clock::now() - start;
    } while (elapsed.count() < 10);
}

TEST_F(Fuzz, USBResetAfterDownload) {
    std::vector<char> buf;
    buf.resize(1000000);
    EXPECT_EQ(DownloadCommand(buf.size()), SUCCESS) << "Download command failed";
    EXPECT_EQ(transport->Reset(), 0) << "USB Reset failed";
    std::vector<std::string> all;
    EXPECT_EQ(fb->GetVarAll(&all), SUCCESS) << "getvar:all failed after USB reset.";
}

// Getvar XML tests
TEST_P(ExtensionsGetVarConformance, VarExists) {
    std::string resp;
    EXPECT_EQ(fb->GetVar(GetParam().first, &resp), SUCCESS);
}

TEST_P(ExtensionsGetVarConformance, VarMatchesRegex) {
    std::string resp;
    ASSERT_EQ(fb->GetVar(GetParam().first, &resp), SUCCESS);
    std::smatch sm;
    std::regex_match(resp, sm, GetParam().second.regex);
    EXPECT_FALSE(sm.empty()) << "The regex did not match";
}

INSTANTIATE_TEST_CASE_P(XMLGetVar, ExtensionsGetVarConformance,
                        ::testing::ValuesIn(GETVAR_XML_TESTS));

TEST_P(AnyPartition, ReportedGetVarAll) {
    // As long as the partition is reported in INFO, it would be tested by generic Conformance
    std::vector<std::tuple<std::string, uint32_t>> parts;
    ASSERT_EQ(fb->Partitions(&parts), SUCCESS) << "getvar:all failed";
    const std::string name = GetParam().first;
    if (GetParam().second.slots) {
        auto matcher = [&](const std::tuple<std::string, uint32_t>& tup) {
            return std::get<0>(tup) == name + "_a";
        };
        EXPECT_NE(std::find_if(parts.begin(), parts.end(), matcher), parts.end())
                << "partition '" + name + "_a' not reported in getvar:all";
    } else {
        auto matcher = [&](const std::tuple<std::string, uint32_t>& tup) {
            return std::get<0>(tup) == name;
        };
        EXPECT_NE(std::find_if(parts.begin(), parts.end(), matcher), parts.end())
                << "partition '" + name + "' not reported in getvar:all";
    }
}

TEST_P(AnyPartition, Hashable) {
    const std::string name = GetParam().first;
    if (!config.checksum.empty()) {  // We can use hash to validate
        for (const auto& part_name : real_parts) {
            // Get hash
            std::string hash;
            int retcode;
            std::string err_msg;
            if (GetParam().second.hashable) {
                ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash, &retcode, &err_msg))
                        << err_msg;
                EXPECT_EQ(retcode, 0) << err_msg;
            } else {  // Make sure it fails
                const std::string cmd = config.checksum + ' ' + part_name;
                EXPECT_EQ(fb->RawCommand(cmd), DEVICE_FAIL)
                        << part_name + " is marked as non-hashable, but hashing did not fail";
            }
        }
    }
}

TEST_P(WriteablePartition, FlashCheck) {
    const std::string name = GetParam().first;
    auto part_info = GetParam().second;

    for (const auto& part_name : real_parts) {
        std::vector<char> buf = RandomBuf(max_flash, rand_char);
        EXPECT_EQ(fb->FlashPartition(part_name, buf), part_info.parsed ? DEVICE_FAIL : SUCCESS)
                << "A partition with a image parsed by the bootloader should reject random garbage "
                   "otherwise it should succeed";
    }
}

TEST_P(WriteablePartition, EraseCheck) {
    const std::string name = GetParam().first;

    for (const auto& part_name : real_parts) {
        ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
    }
}

TEST_P(WriteHashNonParsedPartition, EraseZerosData) {
    const std::string name = GetParam().first;

    for (const auto& part_name : real_parts) {
        std::string err_msg;
        int retcode;
        const std::vector<char> buf = RandomBuf(max_flash, rand_char);
        if (max_flash < part_size) {  // Partition is too big to write to entire thing
            std::string hash_before, hash_after;
            ASSERT_EQ(fb->FlashPartition(part_name, buf), SUCCESS);
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_before, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_NE(hash_before, hash_after)
                    << "The partition hash for " + part_name +
                               " did not change after erasing a known value";
        } else {
            std::string hash_zeros, hash_ones, hash_middle, hash_after;
            const std::vector<char> buf_zeros(max_flash, 0);
            const std::vector<char> buf_ones(max_flash, -1);  // All bits are set to 1
            ASSERT_EQ(fb->FlashPartition(part_name, buf_zeros), SUCCESS);
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_zeros, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            ASSERT_EQ(fb->FlashPartition(part_name, buf_ones), SUCCESS);
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_ones, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            ASSERT_NE(hash_zeros, hash_ones)
                    << "Hashes of partion should not be the same when all bytes are 0xFF or 0x00";
            ASSERT_EQ(fb->FlashPartition(part_name, buf), SUCCESS);
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_middle, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            ASSERT_NE(hash_zeros, hash_middle)
                    << "Hashes of partion are the same when all bytes are 0x00 or test payload";
            ASSERT_NE(hash_ones, hash_middle)
                    << "Hashes of partion are the same when all bytes are 0xFF or test payload";
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_TRUE(hash_zeros == hash_after || hash_ones == hash_after)
                    << "Erasing " + part_name + " should set all the bytes to 0xFF or 0x00";
        }
    }
}

// Only partitions that we can write and hash (name, fixture), TEST_P is (Fixture, test_name)
INSTANTIATE_TEST_CASE_P(XMLPartitionsWriteHashNonParsed, WriteHashNonParsedPartition,
                        ::testing::ValuesIn(PARTITION_XML_WRITE_HASH_NONPARSED));

INSTANTIATE_TEST_CASE_P(XMLPartitionsWriteHashable, WriteHashablePartition,
                        ::testing::ValuesIn(PARTITION_XML_WRITE_HASHABLE));

// only partitions writeable
INSTANTIATE_TEST_CASE_P(XMLPartitionsWriteable, WriteablePartition,
                        ::testing::ValuesIn(PARTITION_XML_WRITEABLE));

// Every partition
INSTANTIATE_TEST_CASE_P(XMLPartitionsAll, AnyPartition, ::testing::ValuesIn(PARTITION_XML_TESTS));

// Partition Fuzz tests
TEST_P(FuzzWriteablePartition, BoundsCheck) {
    const std::string name = GetParam().first;
    auto part_info = GetParam().second;

    for (const auto& part_name : real_parts) {
        // try and flash +1 too large, first erase and get a hash, make sure it does not change
        std::vector<char> buf = RandomBuf(max_flash + 1);  // One too large
        if (part_info.hashable) {
            std::string hash_before, hash_after, err_msg;
            int retcode;
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_before, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "Flashing an image 1 byte too large to " + part_name + " did not fail";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(hash_before, hash_after)
                    << "Flashing too large of an image resulted in a changed partition hash for " +
                               part_name;
        } else {
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "Flashing an image 1 byte too large to " + part_name + " did not fail";
        }
    }
}

INSTANTIATE_TEST_CASE_P(XMLFuzzPartitionsWriteable, FuzzWriteablePartition,
                        ::testing::ValuesIn(PARTITION_XML_WRITEABLE));

// A parsed partition should have magic and such that is checked by the bootloader
// Attempting to flash a random single byte should definately fail
TEST_P(FuzzWriteableParsedPartition, FlashGarbageImageSmall) {
    const std::string name = GetParam().first;
    auto part_info = GetParam().second;

    for (const auto& part_name : real_parts) {
        std::vector<char> buf = RandomBuf(1);
        if (part_info.hashable) {
            std::string hash_before, hash_after, err_msg;
            int retcode;
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_before, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should fail on a single byte";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(hash_before, hash_after)
                    << "Flashing a single byte to parsed partition  " + part_name +
                               " should fail and not change the partition hash";
        } else {
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "Flashing a 1 byte image to a parsed partition should fail";
        }
    }
}

TEST_P(FuzzWriteableParsedPartition, FlashGarbageImageLarge) {
    const std::string name = GetParam().first;
    auto part_info = GetParam().second;

    for (const auto& part_name : real_parts) {
        std::vector<char> buf = RandomBuf(max_flash);
        if (part_info.hashable) {
            std::string hash_before, hash_after, err_msg;
            int retcode;
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_before, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should not accept randomly generated images";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(hash_before, hash_after)
                    << "The hash of the partition has changed after attempting to flash garbage to "
                       "a parsed partition";
        } else {
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should not accept randomly generated images";
        }
    }
}

TEST_P(FuzzWriteableParsedPartition, FlashGarbageImageLarge2) {
    const std::string name = GetParam().first;
    auto part_info = GetParam().second;

    for (const auto& part_name : real_parts) {
        std::vector<char> buf(max_flash, -1);  // All 1's
        if (part_info.hashable) {
            std::string hash_before, hash_after, err_msg;
            int retcode;
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_before, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should not accept a image of all 0xFF";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(hash_before, hash_after)
                    << "The hash of the partition has changed after attempting to flash garbage to "
                       "a parsed partition";
        } else {
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should not accept a image of all 0xFF";
        }
    }
}

TEST_P(FuzzWriteableParsedPartition, FlashGarbageImageLarge3) {
    const std::string name = GetParam().first;
    auto part_info = GetParam().second;

    for (const auto& part_name : real_parts) {
        std::vector<char> buf(max_flash, 0);  // All 0's
        if (part_info.hashable) {
            std::string hash_before, hash_after, err_msg;
            int retcode;
            ASSERT_EQ(fb->Erase(part_name), SUCCESS) << "Erasing " + part_name + " failed";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_before, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should not accept a image of all 0x00";
            ASSERT_TRUE(PartitionHash(fb.get(), part_name, &hash_after, &retcode, &err_msg))
                    << err_msg;
            ASSERT_EQ(retcode, 0) << err_msg;
            EXPECT_EQ(hash_before, hash_after)
                    << "The hash of the partition has changed after attempting to flash garbage to "
                       "a parsed partition";
        } else {
            EXPECT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                    << "A parsed partition should not accept a image of all 0x00";
        }
    }
}

INSTANTIATE_TEST_CASE_P(XMLFuzzPartitionsWriteableParsed, FuzzWriteableParsedPartition,
                        ::testing::ValuesIn(PARTITION_XML_WRITEABLE));

// Make sure all attempts to flash things are rejected
TEST_P(FuzzAnyPartitionLocked, RejectFlash) {
    std::vector<char> buf = RandomBuf(5);
    for (const auto& part_name : real_parts) {
        ASSERT_EQ(fb->FlashPartition(part_name, buf), DEVICE_FAIL)
                << "Flashing a partition should always fail in locked mode";
    }
}

INSTANTIATE_TEST_CASE_P(XMLFuzzAnyPartitionLocked, FuzzAnyPartitionLocked,
                        ::testing::ValuesIn(PARTITION_XML_TESTS));

// OEM xml tests
TEST_P(ExtensionsOemConformance, RunOEMTest) {
    const std::string& cmd = std::get<0>(GetParam());
    // bool restricted = std::get<1>(GetParam());
    const extension::Configuration::CommandTest& test = std::get<2>(GetParam());

    const RetCode expect = (test.expect == test.FAIL) ? DEVICE_FAIL : SUCCESS;

    // Does the test require staging something?
    if (!test.input.empty()) {  // Non-empty string
        FILE* to_stage = fopen(test.input.c_str(), "rb");
        ASSERT_NE(to_stage, nullptr) << "'" << test.input << "'"
                                     << " failed to open for staging";
        int fd = fileno(to_stage);
        size_t fsize = lseek(fd, 0, SEEK_END);
        std::string var;
        EXPECT_EQ(fb->GetVar("max-download-size", &var), SUCCESS);
        int64_t size = strtoll(var.c_str(), nullptr, 16);
        EXPECT_LT(fsize, size) << "'" << test.input << "'"
                               << " is too large for staging";
        ASSERT_EQ(fb->Download(fd, fsize), SUCCESS) << "'" << test.input << "'"
                                                    << " failed to download for staging";
        fclose(to_stage);
    }
    // Run the command
    int dsize = -1;
    std::string resp;
    const std::string full_cmd = "oem " + cmd + " " + test.arg;
    ASSERT_EQ(fb->RawCommand(full_cmd, &resp, nullptr, &dsize), expect);

    // This is how we test if indeed data response
    if (test.expect == test.DATA) {
        EXPECT_GT(dsize, 0);
    }

    // Validate response if neccesary
    if (!test.regex_str.empty()) {
        std::smatch sm;
        std::regex_match(resp, sm, test.regex);
        EXPECT_FALSE(sm.empty()) << "The oem regex did not match";
    }

    // If payload, we validate that as well
    const std::vector<std::string> args = SplitBySpace(test.validator);
    if (args.size()) {
        // Save output
        const std::string save_loc = test.output.empty() ? DEFAULT_OUPUT_PATH : test.output;
        std::string resp;
        ASSERT_EQ(fb->Upload(save_loc, &resp), SUCCESS)
                << "Saving output file failed with (" << fb->Error() << ") " << resp;
        // Build the arguments to the validator
        std::vector<std::string> prog_args(args.begin() + 1, args.end());
        prog_args.push_back(full_cmd);  // Pass in the full command
        prog_args.push_back(save_loc);  // Pass in the save location
        // Run the validation program
        int pipe;
        const pid_t pid = StartProgram(args[0], prog_args, &pipe);
        ASSERT_GT(pid, 0) << "Failed to launch validation program: " << args[0];
        std::string error_msg;
        int ret = WaitProgram(pid, pipe, &error_msg);
        EXPECT_EQ(ret, 0) << error_msg;  // Program exited correctly
    }
}

INSTANTIATE_TEST_CASE_P(XMLOEM, ExtensionsOemConformance, ::testing::ValuesIn(OEM_XML_TESTS));

void GenerateXmlTests(const extension::Configuration& config) {
    // Build the getvar tests
    for (const auto it : config.getvars) {
        GETVAR_XML_TESTS.push_back(std::make_pair(it.first, it.second));
    }

    // Build the partition tests, to interface with gtest we need to do it this way
    for (const auto it : config.partitions) {
        PARTITION_XML_TESTS.push_back(std::make_tuple(it.first, it.second));  // All partitions

        if (it.second.test == it.second.YES) {
            PARTITION_XML_WRITEABLE.push_back(
                    std::make_tuple(it.first, it.second));  // All writeable partitions

            if (it.second.hashable) {
                PARTITION_XML_WRITE_HASHABLE.push_back(
                        std::make_tuple(it.first, it.second));  // All write and hashable
                if (!it.second.parsed) {
                    PARTITION_XML_WRITE_HASH_NONPARSED.push_back(std::make_tuple(
                            it.first, it.second));  // All write hashed and non-parsed
                }
            }
            if (it.second.parsed) {
                PARTITION_XML_WRITE_PARSED.push_back(
                        std::make_tuple(it.first, it.second));  // All write and parsed
            }
        }
    }

    // Build oem tests
    for (const auto it : config.oem) {
        auto oem_cmd = it.second;
        for (const auto& t : oem_cmd.tests) {
            OEM_XML_TESTS.push_back(std::make_tuple(it.first, oem_cmd.restricted, t));
        }
    }
}

}  // namespace fastboot

int main(int argc, char** argv) {
    if (!fastboot::extension::ParseXml("example.xml", &fastboot::config)) {
        return -1;
    }
    // To interface with gtest, must set global scope test variables
    fastboot::GenerateXmlTests(fastboot::config);
    setbuf(stdout, NULL);  // no buffering
    printf("<Waiting for Device>\n");
    const auto matcher = [](usb_ifc_info* info) -> int {
        return fastboot::FastBootTest::MatchFastboot(info, nullptr);
    };
    Transport* transport = nullptr;
    while (!transport) {
        transport = usb_open(matcher);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    transport->Close();

    // TODO add command line argv parsing and don't hardcode
    std::string serial_port = "/dev/ttyUSB0";
    fastboot::FastBootTest::serial_port = fastboot::ConfigureSerial(serial_port);

    ::testing::InitGoogleTest(&argc, argv);
    auto ret = RUN_ALL_TESTS();
    if (fastboot::FastBootTest::serial_port > 0) {
        close(fastboot::FastBootTest::serial_port);
    }
    return ret;
}
