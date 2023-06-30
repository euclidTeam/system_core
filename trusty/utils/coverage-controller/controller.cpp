/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <getopt.h>
#include <trusty/line-coverage/coverage.h>
#include <trusty/tipc.h>
#include <array>
#include <memory>
#include <vector>

#include "controller.h"

namespace android {
namespace trusty {
namespace controller {

using ::android::trusty::line_coverage::CoverageRecord;

void Controller::run(std::string output_dir) {
    connectCoverageServer();
    struct control *control;
    uint64_t complete_cnt = 0, start_cnt = 0;

    while(1) {
        setUpShm();

        for (int index = 0; index < record_list_.size() ; index++) {
            control = (struct control *)record_list_[index]->getShm();
            start_cnt = READ_ONCE((control->write_buffer_start_count));
            complete_cnt = READ_ONCE(control->write_buffer_complete_count);

            if (complete_cnt != counters[index] && start_cnt == complete_cnt) {
                WRITE_ONCE(control->cntrl_flags, FLAG_NONE);
                std::string fmt = "/%d.%lu.profraw";
                int sz = std::snprintf(nullptr, 0, fmt.c_str(), index, counters[index]);
                std::string filename;
                std::sprintf(filename.data(), fmt.c_str(), index, counters[index]);
                filename.insert(0, output_dir);
                android::base::Result<void> res = record_list_[index]->SaveFile(filename);
                counters[index]++;
            }
            if(complete_cnt == counters[index] &&
                READ_ONCE(control->cntrl_flags) != FLAG_RUN) {
                WRITE_ONCE(control->cntrl_flags, FLAG_RUN);
            }
        }
    }
}

void Controller::connectCoverageServer() {
    coverage_srv_fd = tipc_connect(TIPC_DEV, LINE_COVERAGE_CLIENT_PORT);
    if (coverage_srv_fd < 0) {
        fprintf(stderr, \
                "Error: Failed to connect to Trusty coverage server: %d\n", coverage_srv_fd);
        return;
    }
}

void Controller::setUpShm() {
    struct line_coverage_client_req req;
    struct line_coverage_client_resp resp;
    uint32_t cur_index = record_list_.size();
    struct uuid zero_uuid = {0, 0, 0, { 0 }};
    req.hdr.cmd = LINE_COVERAGE_CLIENT_CMD_SEND_LIST;
    int rc = write(coverage_srv_fd, &req, sizeof(req));
        if (rc != (int)sizeof(req)) {
            fprintf(stderr, "failed to send request to coverage server: %d\n", rc);
            return;
    }

    while(1) {
        rc = read(coverage_srv_fd, &resp, sizeof(resp));
        if (rc != (int)sizeof(resp)) {
            fprintf(stderr, "failed to read reply from coverage server:: %d\n", rc);
        }

        if (resp.hdr.cmd == (req.hdr.cmd | LINE_COVERAGE_CLIENT_CMD_RESP_BIT)) {
            if (!memcmp(&resp.send_list_args.uuid, &zero_uuid, sizeof(struct uuid))) {
                break;
            }
            uuid_list_.push_back(resp.send_list_args.uuid);
            record_list_.push_back(std::make_unique<CoverageRecord>(TIPC_DEV,
                                                                &uuid_list_.back()));
            counters.push_back(0);
            record_list_.back()->Open(coverage_srv_fd);
        }
        else {
            fprintf(stderr, "Unknown response header\n");
        }
        cur_index++;
        req.hdr.cmd = LINE_COVERAGE_CLIENT_CMD_SEND_LIST;
        req.send_list_args.index = cur_index;
        int rc = write(coverage_srv_fd, &req, sizeof(req));
        if (rc != (int)sizeof(req)) {
            fprintf(stderr, "failed to send request to coverage server:: %d\n", rc);
        }
    }
}


}  // namespace controller
}  // namespace trusty
}  // namespace android

int main(int argc, char* argv[]) {

    std::string optarg = "";
    do {
        int c;
        c = getopt(argc, argv, "o");

        if (c == -1) {
            break;
        }

        switch (c) {
        case 'o':
            break;
        default:
            fprintf(stderr, "usage: %s -o [output_directory]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    } while (1);

    if (argc > optind + 1) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (argc > optind)
        optarg = argv[optind];
    if (optarg.size()==0) {
        optarg = "./";
    }

    android::trusty::controller::Controller cur;
    cur.run(optarg);

    return EXIT_SUCCESS;
}
