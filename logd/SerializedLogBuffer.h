/*
 * Copyright (C) 2020 The Android Open Source Project
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

#pragma once

#include <atomic>
#include <bitset>
#include <list>
#include <mutex>
#include <queue>
#include <vector>

#include "LogBuffer.h"
#include "LogReaderList.h"
#include "LogStatistics.h"
#include "LogTags.h"
#include "SerializedLogChunk.h"
#include "SerializedLogEntry.h"

class SerializedLogBuffer : public LogBuffer {
  public:
    SerializedLogBuffer(LogReaderList* reader_list, LogTags* tags, LogStatistics* stats);
    ~SerializedLogBuffer();
    void Init() override;

    int Log(log_id_t log_id, log_time realtime, uid_t uid, pid_t pid, pid_t tid, const char* msg,
            uint16_t len) override;
    std::unique_ptr<FlushToState> CreateFlushToState(uint64_t start, LogMask log_mask) override;
    bool FlushTo(LogWriter* writer, FlushToState& state,
                 const std::function<FilterResult(log_id_t log_id, pid_t pid, uint64_t sequence,
                                                  log_time realtime)>& filter) override;

    bool Clear(log_id_t id, uid_t uid) override;
    unsigned long GetSize(log_id_t id) override;
    int SetSize(log_id_t id, unsigned long size) override;

    uint64_t sequence() const override { return sequence_.load(std::memory_order_relaxed); }

  private:
    bool ShouldLog(log_id_t log_id, const char* msg, uint16_t len);
    void MaybePrune(log_id_t log_id);
    bool Prune(log_id_t log_id, size_t bytes_to_free, uid_t uid);
    void KickReader(LogReaderThread* reader, log_id_t id, size_t bytes_to_free);
    void DeleteLogChunks(std::list<SerializedLogChunk>&& chunks, log_id_t log_id);

    LogReaderList* reader_list_;
    LogTags* tags_;
    LogStatistics* stats_;

    unsigned long max_size_[LOG_ID_MAX] = {};
    std::list<SerializedLogChunk> logs_[LOG_ID_MAX];
    std::mutex logs_lock_;

    std::atomic<uint64_t> sequence_ = 1;
};
