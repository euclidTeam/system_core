/*
 * Copyright (C) 2022 The Android Open Source Project
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
#include <errno.h>
#include <getopt.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include <condition_variable>
#include <mutex>
#include <queue>

#include <android-base/expected.h>
#include <android-base/logging.h>
#include <android/frameworks/stats/BnStats.h>
#include <android/frameworks/stats/IStats.h>
#include <android/frameworks/stats/setter/IStatsSetter.h>
#include <binder/RpcServer.h>
#include <binder/RpcSession.h>
#include <binder/RpcTransportRaw.h>
#include <binder/RpcTransportTipcAndroid.h>
#include <binder/RpcTrusty.h>
#include <trusty/tipc.h>

/** DOC:
 * ./build-root/build-qemu-generic-arm64-test-debug/run \
 *       --android $HOME/depot/android/aosp \
 *       --headless --shell-command "/data/nativetest64/vendor/trusty_stats_test/trusty_stats_test"
 * adb -s emulator-5554 shell /data/nativetest64/vendor/trusty_stats_test/trusty_stats_test
 */
using namespace android;
using android::base::unique_fd;
using ::binder::Status;
using ::frameworks::stats::BnStats;
using ::frameworks::stats::IStats;
using ::frameworks::stats::VendorAtom;
using ::frameworks::stats::VendorAtomValue;
using ::frameworks::stats::setter::IStatsSetter;

constexpr const char kTrustyDefaultDeviceName[] = "/dev/trusty-ipc-dev0";
constexpr const char kTrustyStatsSetterTest[] =
        "com.android.frameworks.stats.trusty.test.relayer.istats_setter";
constexpr const char kTrustyStatsSetterMetrics[] =
        "com.android.frameworks.stats.trusty.metrics.istats_setter";
constexpr const char kTrustyStatsPortTest[] = "com.android.trusty.stats.test";
constexpr const char kTrustyCrashPortTest[] = "com.android.trusty.crashtest";
constexpr const char kTrustyCrasherUuid[] = "7ee4dddc-177a-420a-96ea-5d413d88228e:crasher";

enum TrustyAtoms : int32_t {
    TrustyAppCrashed = 100072,
    TrustyError = 100145,
    TrustyStorageError = 100146
};

enum TestMsgHeader : int32_t {
    TEST_PASSED = 0,
    TEST_FAILED = 1,
    TEST_MESSAGE = 2,
};

namespace android {
namespace trusty {
namespace stats {

class Stats : public BnStats {
  public:
    Stats() : BnStats() {}
    Status reportVendorAtom(const ::VendorAtom& vendorAtom) {
        const char* atomIdStr = vendorAtomStr(vendorAtom.atomId);
        ALOGD("%s\n", atomIdStr);
        write(STDOUT_FILENO, atomIdStr, strlen(atomIdStr));
        std::lock_guard lock(mLock);
        mQueueVendorAtom.push(vendorAtom);
        mCondVar.notify_one();
        return Status::ok();
    }

    status_t getVendorAtom(VendorAtom* pVendorAtom, int64_t waitForMs) {
        std::unique_lock lock(mLock);
        while (mQueueVendorAtom.empty()) {
            auto rc = mCondVar.wait_for(lock, std::chrono::milliseconds(waitForMs));
            if (rc == std::cv_status::timeout) {
                return TIMED_OUT;
            }
        }
        *pVendorAtom = mQueueVendorAtom.front();
        mQueueVendorAtom.pop();
        return NO_ERROR;
    }

  private:
    const char* vendorAtomStr(int32_t atomId) {
        switch (atomId) {
            case TrustyAtoms::TrustyAppCrashed:
                return "TrustyAtoms::TrustyAppCrashed\n";
            case TrustyAtoms::TrustyError:
                return "TrustyAtoms::TrustyError\n";
            case TrustyAtoms::TrustyStorageError:
                return "TrustyAtoms::TrustyStorageError\n";
            default:
                return "TrustyAtoms::UNKNOWN\n";
        }
    }
    std::mutex mLock;
    std::condition_variable mCondVar;
    std::queue<VendorAtom> mQueueVendorAtom;
};

class TrustyStatsTestBase : public ::testing::Test {
  protected:
    TrustyStatsTestBase(std::string&& portNameStatsSetter, std::string&& portNamePortTest)
        : mPortTestFd(-1),
          mPortNameStatsSetter(std::move(portNameStatsSetter)),
          mPortNamePortTest(std::move(portNamePortTest)) {}

    void SetUp() override {
        // Commenting out the server portion because we do not have any direct incoming call
        // Calls from TA are currently being handle on the extra thread on the session.
        // android::sp<::android::RpcServer> server =
        // ::android::RpcServer::make(::android::RpcTransportCtxFactoryRaw::make());

        mStats = android::sp<Stats>::make();
        // Increasing number of incoming threads on session to be able to receive callbacks
        auto session_initializer = [](sp<RpcSession>& session) {
            session->setMaxIncomingThreads(1);
        };

        ASSERT_FALSE(mSession);
        mSession = RpcTrustyConnectWithSessionInitializer(
                kTrustyDefaultDeviceName, mPortNameStatsSetter.c_str(), session_initializer);
        ASSERT_TRUE(mSession);
        auto root = mSession->getRootObject();
        ASSERT_TRUE(root);
        auto statsSetter = IStatsSetter::asInterface(root);
        ASSERT_TRUE(statsSetter);
        statsSetter->setInterface(mStats);
    }
    void TearDown() override {
        /* close connection to unitest app */
        if (mPortTestFd != -1) {
            tipc_close(mPortTestFd);
        }
        mPortTestFd = -1;
        if (mSession) {
            // shutdownAndWait here races with sending out the DecStrong
            // messages after reportVendorAtom returns, so we delay it a little
            // bit to give the messages time to go out over the transport
            usleep(50000);
            ASSERT_TRUE(mSession->shutdownAndWait(true));
        }
        mSession.clear();
        mStats.clear();
    }
    void StartPortTest() {
        /* connect to unitest app */
        mPortTestFd = tipc_connect(kTrustyDefaultDeviceName, mPortNamePortTest.c_str());
        if (mPortTestFd < 0) {
            ALOGE("failed to connect to '%s' app: %s\n", kTrustyStatsPortTest,
                  strerror(-mPortTestFd));
        }
        ASSERT_GT(mPortTestFd, 0);
    }
    void WaitPortTestDone() {
        /* wait for test to complete */
        char rxBuf[1024];
        const char prolog[] = "Trusty PORT_TEST:";
        strncpy(rxBuf, prolog, sizeof(prolog) - 1);
        char* pRxBuf = rxBuf + sizeof(prolog) - 1;
        ASSERT_NE(mPortTestFd, -1);
        for (;;) {
            int rc = read(mPortTestFd, pRxBuf, sizeof(rxBuf) - sizeof(prolog) - 1);
            ASSERT_GT(rc, 0);
            ASSERT_LT(rc, (int)(sizeof(rxBuf) - sizeof(prolog) - 1));
            if (pRxBuf[0] == TEST_PASSED) {
                break;
            } else if (pRxBuf[0] == TEST_FAILED) {
                break;
            } else if (pRxBuf[0] == TEST_MESSAGE) {
                pRxBuf[0] = ' ';
                write(STDOUT_FILENO, rxBuf, rc + sizeof(prolog) - 1);
            } else {
                ALOGE("Bad message header: %d\n", rxBuf[0]);
                break;
            }
        }
        ASSERT_EQ(pRxBuf[0], TEST_PASSED);
    }
    android::sp<Stats> mStats;
    android::sp<RpcSession> mSession;
    int mPortTestFd;
    const std::string mPortNameStatsSetter;
    const std::string mPortNamePortTest;
};

class TrustyStatsTest : public TrustyStatsTestBase {
  protected:
    TrustyStatsTest() : TrustyStatsTestBase(kTrustyStatsSetterTest, kTrustyStatsPortTest) {}
};

class TrustyMetricsCrashTest : public TrustyStatsTestBase {
  protected:
    TrustyMetricsCrashTest()
        : TrustyStatsTestBase(kTrustyStatsSetterMetrics, kTrustyCrashPortTest) {}
};

TEST_F(TrustyStatsTest, CheckAtoms) {
    VendorAtom vendorAtom;
    int expectedAtomCnt = 2;
    int atomAppCrashedCnt = 0;
    int atomStorageErrorCnt = 0;
    int atomTrustyErrorCnt = 0;
    uint64_t blockForMs = 500;
    StartPortTest();
    WaitPortTestDone();
    while (expectedAtomCnt--) {
        ASSERT_EQ(NO_ERROR, mStats->getVendorAtom(&vendorAtom, blockForMs));
        ASSERT_THAT(vendorAtom.atomId,
                    ::testing::AnyOf(::testing::Eq(TrustyAtoms::TrustyAppCrashed),
                                     ::testing::Eq(TrustyAtoms::TrustyError),
                                     ::testing::Eq(TrustyAtoms::TrustyStorageError)));
        ASSERT_STREQ(String8(vendorAtom.reverseDomainName), "google.android.trusty");
        if (vendorAtom.atomId == TrustyAtoms::TrustyAppCrashed) {
            ++atomAppCrashedCnt;
            ASSERT_STREQ(String8(vendorAtom.values[0].get<VendorAtomValue::stringValue>()),
                         "5247d19b-cf09-4272-a450-3ef20dbefc14");
        } else if (vendorAtom.atomId == TrustyAtoms::TrustyStorageError) {
            ++atomStorageErrorCnt;
            ASSERT_EQ(vendorAtom.values[0].get<VendorAtomValue::intValue>(), 5);
            ASSERT_STREQ(String8(vendorAtom.values[1].get<VendorAtomValue::stringValue>()),
                         "5247d19b-cf09-4272-a450-3ef20dbefc14");
            ASSERT_STREQ(String8(vendorAtom.values[2].get<VendorAtomValue::stringValue>()),
                         "5247d19b-cf09-4272-a450-3ef20dbefc14");
            ASSERT_EQ(vendorAtom.values[3].get<VendorAtomValue::intValue>(), 1);
            ASSERT_EQ(vendorAtom.values[4].get<VendorAtomValue::intValue>(), 3);
            ASSERT_EQ(vendorAtom.values[5].get<VendorAtomValue::longValue>(), 0x4BCDEFABBAFEDCBALL);
            ASSERT_EQ(vendorAtom.values[6].get<VendorAtomValue::intValue>(), 4);
            ASSERT_EQ(vendorAtom.values[7].get<VendorAtomValue::longValue>(), 1023);
        } else if (vendorAtom.atomId == TrustyAtoms::TrustyError) {
            ++atomTrustyErrorCnt;
        } else {
            FAIL() << "We shouldn't get here.";
        }
    };
    ASSERT_EQ(atomAppCrashedCnt, 1);
    ASSERT_EQ(atomStorageErrorCnt, 1);
    ASSERT_EQ(atomTrustyErrorCnt, 0);
}

static const std::vector<uint32_t> kExpectedCrashReasons{
        0x00000001U,  // exit_failure (twice)
        0x00000001U,
        0x92000004U,  // read_null_ptr
        0xf200002aU,  // brk_instruction
        0x92000004U,  // read_bad_ptr
        0x92000044U,  // crash_write_bad_ptr
        0x9200004fU,  // crash_write_ro_ptr
        0x8200000fU,  // crash_exec_rodata
        0x8200000fU,  // crash_exec_data
};

TEST_F(TrustyMetricsCrashTest, CheckTrustyCrashAtoms) {
    VendorAtom vendorAtom;
    int expectedAtomCnt = 8;
    int atomAppCrashedCnt = 0;
    int atomStorageErrorCnt = 0;
    int atomTrustyErrorCnt = 0;
    std::vector<uint32_t> atomCrashReasons;
    uint64_t blockForMs = 500;
#if __aarch64__
    // __aarch64__ enables brk_instruction
    ++expectedAtomCnt;
#endif
    int atomCnt = expectedAtomCnt;
    StartPortTest();
    WaitPortTestDone();
    while (atomCnt--) {
        ASSERT_EQ(NO_ERROR, mStats->getVendorAtom(&vendorAtom, blockForMs));
        ASSERT_THAT(vendorAtom.atomId,
                    ::testing::AnyOf(::testing::Eq(TrustyAtoms::TrustyAppCrashed),
                                     ::testing::Eq(TrustyAtoms::TrustyError),
                                     ::testing::Eq(TrustyAtoms::TrustyStorageError)));
        ASSERT_STREQ(String8(vendorAtom.reverseDomainName), "google.android.trusty");
        if (vendorAtom.atomId == TrustyAtoms::TrustyAppCrashed) {
            ++atomAppCrashedCnt;
            ASSERT_STREQ(String8(vendorAtom.values[0].get<VendorAtomValue::stringValue>()),
                         kTrustyCrasherUuid);
            atomCrashReasons.push_back(vendorAtom.values[1].get<VendorAtomValue::intValue>());
        } else if (vendorAtom.atomId == TrustyAtoms::TrustyStorageError) {
            ++atomStorageErrorCnt;
        } else if (vendorAtom.atomId == TrustyAtoms::TrustyError) {
            ++atomTrustyErrorCnt;
            ASSERT_STREQ(String8(vendorAtom.values[1].get<VendorAtomValue::stringValue>()), "");
        } else {
            FAIL() << "We shouldn't get here.";
        }
    }
    ASSERT_GE(atomAppCrashedCnt, expectedAtomCnt - 1);
    ASSERT_EQ(atomStorageErrorCnt, 0);
    // There is one dropped event left over from Trusty boot,
    // it may show up here
    ASSERT_LE(atomTrustyErrorCnt, 1);
    ASSERT_EQ(atomCrashReasons, kExpectedCrashReasons);
};

}  // namespace stats
}  // namespace trusty
}  // namespace android
