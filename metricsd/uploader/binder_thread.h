/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef METRICSD_UPLOADER_BINDER_THREAD_H_
#define METRICSD_UPLOADER_BINDER_THREAD_H_

#include <utils/Thread.h>

#include "uploader/binder_service.h"

class BinderThread : public android::Thread {
 public:
  explicit BinderThread(const std::shared_ptr<CrashCounters>);

  // Runs the main loop.
  virtual bool threadLoop() override;

 private:
  std::unique_ptr<BinderService> binder_service_;
};

#endif  // METRICSD_UPLOADER_BINDER_THREAD_H_
