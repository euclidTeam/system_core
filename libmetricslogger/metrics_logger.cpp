/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "metricslogger/metrics_logger.h"

#include <cstdlib>

#include <log/event_tag_map.h>
#include <utils/SystemClock.h>
#include <utils/Timers.h>

using namespace android;

namespace {

const static int kStatsEventTag = 1937006964;
const static int kKeyValuePairAtomId = 83;
#ifdef __ANDROID__
EventTagMap* kEventTagMap = android_openEventTagMap(nullptr);
const int kSysuiMultiActionTag = android_lookupEventTagNum(
    kEventTagMap, "sysui_multi_action", "(content|4)", ANDROID_LOG_UNKNOWN);
#else
// android_openEventTagMap does not work on host builds.
const int kSysuiMultiActionTag = 0;
#endif

int64_t getElapsedRealtimeNano() {
#ifdef __ANDROID__
    struct timespec ts;
    ts.tv_sec = ts.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return nsecs_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
#else
    struct timespec ts;
    ts.tv_sec = ts.tv_nsec = 0;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return nsecs_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
#endif
}

}  // namespace

namespace android {
namespace metricslogger {

// Mirror com.android.internal.logging.MetricsLogger#histogram().
void LogHistogram(const std::string& event, int32_t data) {
    android_log_event_list log(kSysuiMultiActionTag);
    log << LOGBUILDER_CATEGORY << LOGBUILDER_HISTOGRAM << LOGBUILDER_NAME << event
        << LOGBUILDER_BUCKET << data << LOGBUILDER_VALUE << 1 << LOG_ID_EVENTS;

    stats_event_list stats_log(kStatsEventTag);
    stats_log << getElapsedRealtimeNano() << kKeyValuePairAtomId << LOGBUILDER_CATEGORY
              << LOGBUILDER_HISTOGRAM << LOGBUILDER_NAME << event << LOGBUILDER_BUCKET << data
              << LOGBUILDER_VALUE << 1;
    stats_log.write(LOG_ID_STATS);
}

// Mirror com.android.internal.logging.MetricsLogger#count().
void LogCounter(const std::string& name, int32_t val) {
    android_log_event_list log(kSysuiMultiActionTag);
    log << LOGBUILDER_CATEGORY << LOGBUILDER_COUNTER << LOGBUILDER_NAME << name << LOGBUILDER_VALUE
        << val << LOG_ID_EVENTS;

    stats_event_list stats_log(kStatsEventTag);
    stats_log << getElapsedRealtimeNano() << kKeyValuePairAtomId << LOGBUILDER_CATEGORY
              << LOGBUILDER_COUNTER << LOGBUILDER_NAME << name << LOGBUILDER_VALUE << val;
    stats_log.write(LOG_ID_STATS);
}

// Mirror com.android.internal.logging.MetricsLogger#action().
void LogMultiAction(int32_t category, int32_t field, const std::string& value) {
    android_log_event_list log(kSysuiMultiActionTag);
    log << LOGBUILDER_CATEGORY << category << LOGBUILDER_TYPE << TYPE_ACTION
        << field << value << LOG_ID_EVENTS;

    stats_event_list stats_log(kStatsEventTag);
    stats_log << getElapsedRealtimeNano() << kKeyValuePairAtomId << LOGBUILDER_CATEGORY << category
              << LOGBUILDER_TYPE << TYPE_ACTION << field << value;
    stats_log.write(LOG_ID_STATS);
}

ComplexEventLogger::ComplexEventLogger(int category)
    : logger(kSysuiMultiActionTag), stats_logger(kStatsEventTag) {
    logger << LOGBUILDER_CATEGORY << category;
    stats_logger << getElapsedRealtimeNano() << kKeyValuePairAtomId << LOGBUILDER_CATEGORY
                 << category;
}

void ComplexEventLogger::SetPackageName(const std::string& package_name) {
    logger << LOGBUILDER_PACKAGENAME << package_name;
    stats_logger << LOGBUILDER_PACKAGENAME << package_name;
}

void ComplexEventLogger::AddTaggedData(int tag, int32_t value) {
    logger << tag << value;
    stats_logger << tag << value;
}

void ComplexEventLogger::AddTaggedData(int tag, const std::string& value) {
    logger << tag << value;
    stats_logger << tag << value;
}

void ComplexEventLogger::AddTaggedData(int tag, int64_t value) {
    logger << tag << value;
    stats_logger << tag << value;
}

void ComplexEventLogger::AddTaggedData(int tag, float value) {
    logger << tag << value;
    stats_logger << tag << value;
}

void ComplexEventLogger::Record() {
    logger << LOG_ID_EVENTS;
    stats_logger.write(LOG_ID_STATS);
}

}  // namespace metricslogger
}  // namespace android
