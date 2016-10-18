/*
 * Copyright (C) 2013-2014 The Android Open Source Project
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

#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <log/log.h>

#define BIG_BUFFER (5 * 1024)

// enhanced version of LOG_FAILURE_RETRY to add support for EAGAIN and
// non-syscall libs. Since we are only using this in the emergency of
// a signal to stuff a terminating code into the logs, we will spin rather
// than try a usleep.
#define LOG_FAILURE_RETRY(exp) ({  \
    typeof (exp) _rc;              \
    do {                           \
        _rc = (exp);               \
    } while (((_rc == -1)          \
           && ((errno == EINTR)    \
            || (errno == EAGAIN))) \
          || (_rc == -EINTR)       \
          || (_rc == -EAGAIN));    \
    _rc; })

static const char begin[] = "--------- beginning of ";

TEST(logcat, buckets) {
    FILE *fp;

    ASSERT_TRUE(NULL != (fp = popen(
      "logcat -b radio -b events -b system -b main -d 2>/dev/null",
      "r")));

    char buffer[BIG_BUFFER];

    int ids = 0;
    int count = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strncmp(begin, buffer, sizeof(begin) - 1)) {
            while (char *cp = strrchr(buffer, '\n')) {
                *cp = '\0';
            }
            log_id_t id = android_name_to_log_id(buffer + sizeof(begin) - 1);
            ids |= 1 << id;
            ++count;
        }
    }

    pclose(fp);

    EXPECT_EQ(15, ids);

    EXPECT_EQ(4, count);
}

TEST(logcat, event_tag_filter) {
    FILE *fp;

    ASSERT_TRUE(NULL != (fp = popen(
      "logcat -b events -d -s auditd am_proc_start am_pss am_proc_bound dvm_lock_sample am_wtf 2>/dev/null",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        ++count;
    }

    pclose(fp);

    EXPECT_LT(4, count);
}

TEST(logcat, year) {

    if (android_log_clockid() == CLOCK_MONOTONIC) {
        fprintf(stderr, "Skipping test, logd is monotonic time\n");
        return;
    }

    FILE *fp;

    char needle[32];
    time_t now;
    time(&now);
    struct tm *ptm;
#if !defined(_WIN32)
    struct tm tmBuf;
    ptm = localtime_r(&now, &tmBuf);
#else
    ptm = localtime(&&now);
#endif
    strftime(needle, sizeof(needle), "[ %Y-", ptm);

    ASSERT_TRUE(NULL != (fp = popen(
      "logcat -v long -v year -b all -t 3 2>/dev/null",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strncmp(buffer, needle, strlen(needle))) {
            ++count;
        }
    }

    pclose(fp);

    ASSERT_EQ(3, count);
}

// Return a pointer to each null terminated -v long time field.
char *fgetLongTime(char *buffer, size_t buflen, FILE *fp) {
    while (fgets(buffer, buflen, fp)) {
        char *cp = buffer;
        if (*cp != '[') {
            continue;
        }
        while (*++cp == ' ') {
            ;
        }
        char *ep = cp;
        while (isdigit(*ep)) {
            ++ep;
        }
        if ((*ep != '-') && (*ep != '.')) {
           continue;
        }
        // Find PID field
        while (((ep = strchr(ep, ':'))) && (*++ep != ' ')) {
            ;
        }
        if (!ep) {
            continue;
        }
        ep -= 7;
        *ep = '\0';
        return cp;
    }
    return NULL;
}

TEST(logcat, tz) {

    if (android_log_clockid() == CLOCK_MONOTONIC) {
        fprintf(stderr, "Skipping test, logd is monotonic time\n");
        return;
    }

    int tries = 3; // in case run too soon after system start or buffer clear
    int count;

    do {
        FILE *fp;

        ASSERT_TRUE(NULL != (fp = popen(
          "logcat -v long -v America/Los_Angeles -b all -t 3 2>/dev/null",
          "r")));

        char buffer[BIG_BUFFER];

        count = 0;

        while (fgetLongTime(buffer, sizeof(buffer), fp)) {
            if (strstr(buffer, " -0700") || strstr(buffer, " -0800")) {
                ++count;
            }
        }

        pclose(fp);

    } while ((count < 3) && --tries && (sleep(1), true));

    ASSERT_EQ(3, count);
}

TEST(logcat, ntz) {
    FILE *fp;

    ASSERT_TRUE(NULL != (fp = popen(
      "logcat -v long -v America/Los_Angeles -v zone -b all -t 3 2>/dev/null",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;

    while (fgetLongTime(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, " -0700") || strstr(buffer, " -0800")) {
            ++count;
        }
    }

    pclose(fp);

    ASSERT_EQ(0, count);
}

void do_tail(int num) {
    int tries = 3; // in case run too soon after system start or buffer clear
    int count;

    do {
        char buffer[BIG_BUFFER];

        snprintf(buffer, sizeof(buffer),
          "logcat -v long -b radio -b events -b system -b main -t %d 2>/dev/null",
          num);

        FILE *fp;
        ASSERT_TRUE(NULL != (fp = popen(buffer, "r")));

        count = 0;

        while (fgetLongTime(buffer, sizeof(buffer), fp)) {
            ++count;
        }

        pclose(fp);

    } while ((count < num) && --tries && (sleep(1), true));

    ASSERT_EQ(num, count);
}

TEST(logcat, tail_3) {
    do_tail(3);
}

TEST(logcat, tail_10) {
    do_tail(10);
}

TEST(logcat, tail_100) {
    do_tail(100);
}

TEST(logcat, tail_1000) {
    do_tail(1000);
}

TEST(logcat, tail_time) {
    FILE *fp;

    ASSERT_TRUE(NULL != (fp = popen("logcat -v long -b all -t 10 2>&1", "r")));

    char buffer[BIG_BUFFER];
    char *last_timestamp = NULL;
    char *first_timestamp = NULL;
    int count = 0;

    char *cp;
    while ((cp = fgetLongTime(buffer, sizeof(buffer), fp))) {
        ++count;
        if (!first_timestamp) {
            first_timestamp = strdup(cp);
        }
        free(last_timestamp);
        last_timestamp = strdup(cp);
    }
    pclose(fp);

    EXPECT_EQ(10, count);
    EXPECT_TRUE(last_timestamp != NULL);
    EXPECT_TRUE(first_timestamp != NULL);

    snprintf(buffer, sizeof(buffer), "logcat -v long -b all -t '%s' 2>&1",
             first_timestamp);
    ASSERT_TRUE(NULL != (fp = popen(buffer, "r")));

    int second_count = 0;
    int last_timestamp_count = -1;

    while ((cp = fgetLongTime(buffer, sizeof(buffer), fp))) {
        ++second_count;
        if (first_timestamp) {
            // we can get a transitory *extremely* rare failure if hidden
            // underneath the time is *exactly* XX-XX XX:XX:XX.XXX000000
            EXPECT_STREQ(cp, first_timestamp);
            free(first_timestamp);
            first_timestamp = NULL;
        }
        if (!strcmp(cp, last_timestamp)) {
            last_timestamp_count = second_count;
        }
    }
    pclose(fp);

    free(last_timestamp);
    last_timestamp = NULL;
    free(first_timestamp);

    EXPECT_TRUE(first_timestamp == NULL);
    EXPECT_LE(count, second_count);
    EXPECT_LE(count, last_timestamp_count);
}

TEST(logcat, End_to_End) {
    pid_t pid = getpid();

    log_time ts(CLOCK_MONOTONIC);

    ASSERT_LT(0, __android_log_btwrite(0, EVENT_TYPE_LONG, &ts, sizeof(ts)));

    FILE *fp;
    ASSERT_TRUE(NULL != (fp = popen(
      "logcat -v brief -b events -t 100 2>/dev/null",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        int p;
        unsigned long long t;

        if ((2 != sscanf(buffer, "I/[0]     ( %d): %llu", &p, &t))
         || (p != pid)) {
            continue;
        }

        log_time tx((const char *) &t);
        if (ts == tx) {
            ++count;
        }
    }

    pclose(fp);

    ASSERT_EQ(1, count);
}

int get_groups(const char *cmd) {
    FILE *fp;

    // NB: crash log only available in user space
    EXPECT_TRUE(NULL != (fp = popen(cmd, "r")));

    if (fp == NULL) {
        return 0;
    }

    char buffer[BIG_BUFFER];

    int count = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        int size, consumed, max, payload;
        char size_mult[3], consumed_mult[3];
        long full_size, full_consumed;

        size = consumed = max = payload = 0;
        // NB: crash log can be very small, not hit a Kb of consumed space
        //     doubly lucky we are not including it.
        if (6 != sscanf(buffer, "%*s ring buffer is %d%2s (%d%2s consumed),"
                                " max entry is %db, max payload is %db",
                                &size, size_mult, &consumed, consumed_mult,
                                &max, &payload)) {
            fprintf(stderr, "WARNING: Parse error: %s", buffer);
            continue;
        }
        full_size = size;
        switch(size_mult[0]) {
        case 'G':
            full_size *= 1024;
            /* FALLTHRU */
        case 'M':
            full_size *= 1024;
            /* FALLTHRU */
        case 'K':
            full_size *= 1024;
            /* FALLTHRU */
        case 'b':
            break;
        }
        full_consumed = consumed;
        switch(consumed_mult[0]) {
        case 'G':
            full_consumed *= 1024;
            /* FALLTHRU */
        case 'M':
            full_consumed *= 1024;
            /* FALLTHRU */
        case 'K':
            full_consumed *= 1024;
            /* FALLTHRU */
        case 'b':
            break;
        }
        EXPECT_GT((full_size * 9) / 4, full_consumed);
        EXPECT_GT(full_size, max);
        EXPECT_GT(max, payload);

        if ((((full_size * 9) / 4) >= full_consumed)
         && (full_size > max)
         && (max > payload)) {
            ++count;
        }
    }

    pclose(fp);

    return count;
}

TEST(logcat, get_size) {
    ASSERT_EQ(4, get_groups(
      "logcat -v brief -b radio -b events -b system -b main -g 2>/dev/null"));
}

// duplicate test for get_size, but use comma-separated list of buffers
TEST(logcat, multiple_buffer) {
    ASSERT_EQ(4, get_groups(
      "logcat -v brief -b radio,events,system,main -g 2>/dev/null"));
}

TEST(logcat, bad_buffer) {
    ASSERT_EQ(0, get_groups(
      "logcat -v brief -b radio,events,bogo,system,main -g 2>/dev/null"));
}

static void caught_blocking(int /*signum*/)
{
    unsigned long long v = 0xDEADBEEFA55A0000ULL;

    v += getpid() & 0xFFFF;

    LOG_FAILURE_RETRY(__android_log_btwrite(0, EVENT_TYPE_LONG, &v, sizeof(v)));
}

TEST(logcat, blocking) {
    FILE *fp;
    unsigned long long v = 0xDEADBEEFA55F0000ULL;

    pid_t pid = getpid();

    v += pid & 0xFFFF;

    LOG_FAILURE_RETRY(__android_log_btwrite(0, EVENT_TYPE_LONG, &v, sizeof(v)));

    v &= 0xFFFFFFFFFFFAFFFFULL;

    ASSERT_TRUE(NULL != (fp = popen(
      "( trap exit HUP QUIT INT PIPE KILL ; sleep 6; echo DONE )&"
      " logcat -v brief -b events 2>&1",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;

    int signals = 0;

    signal(SIGALRM, caught_blocking);
    alarm(2);
    while (fgets(buffer, sizeof(buffer), fp)) {

        if (!strncmp(buffer, "DONE", 4)) {
            break;
        }

        ++count;

        int p;
        unsigned long long l;

        if ((2 != sscanf(buffer, "I/[0] ( %u): %lld", &p, &l))
         || (p != pid)) {
            continue;
        }

        if (l == v) {
            ++signals;
            break;
        }
    }
    alarm(0);
    signal(SIGALRM, SIG_DFL);

    // Generate SIGPIPE
    fclose(fp);
    caught_blocking(0);

    pclose(fp);

    EXPECT_LE(2, count);

    EXPECT_EQ(1, signals);
}

static void caught_blocking_tail(int /*signum*/)
{
    unsigned long long v = 0xA55ADEADBEEF0000ULL;

    v += getpid() & 0xFFFF;

    LOG_FAILURE_RETRY(__android_log_btwrite(0, EVENT_TYPE_LONG, &v, sizeof(v)));
}

TEST(logcat, blocking_tail) {
    FILE *fp;
    unsigned long long v = 0xA55FDEADBEEF0000ULL;

    pid_t pid = getpid();

    v += pid & 0xFFFF;

    LOG_FAILURE_RETRY(__android_log_btwrite(0, EVENT_TYPE_LONG, &v, sizeof(v)));

    v &= 0xFFFAFFFFFFFFFFFFULL;

    ASSERT_TRUE(NULL != (fp = popen(
      "( trap exit HUP QUIT INT PIPE KILL ; sleep 6; echo DONE )&"
      " logcat -v brief -b events -T 5 2>&1",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;

    int signals = 0;

    signal(SIGALRM, caught_blocking_tail);
    alarm(2);
    while (fgets(buffer, sizeof(buffer), fp)) {

        if (!strncmp(buffer, "DONE", 4)) {
            break;
        }

        ++count;

        int p;
        unsigned long long l;

        if ((2 != sscanf(buffer, "I/[0] ( %u): %lld", &p, &l))
         || (p != pid)) {
            continue;
        }

        if (l == v) {
            if (count >= 5) {
                ++signals;
            }
            break;
        }
    }
    alarm(0);
    signal(SIGALRM, SIG_DFL);

    // Generate SIGPIPE
    fclose(fp);
    caught_blocking_tail(0);

    pclose(fp);

    EXPECT_LE(2, count);

    EXPECT_EQ(1, signals);
}

// meant to be handed to ASSERT_FALSE / EXPECT_FALSE to expand the message
static testing::AssertionResult IsFalse(int ret, const char* command) {
    return ret ?
        (testing::AssertionSuccess() <<
            "ret=" << ret << " command=\"" << command << "\"") :
        testing::AssertionFailure();
}

TEST(logcat, logrotate) {
    static const char form[] = "/data/local/tmp/logcat.logrotate.XXXXXX";
    char buf[sizeof(form)];
    ASSERT_TRUE(NULL != mkdtemp(strcpy(buf, form)));

    static const char comm[] = "logcat -b radio -b events -b system -b main"
                                     " -d -f %s/log.txt -n 7 -r 1";
    char command[sizeof(buf) + sizeof(comm)];
    snprintf(command, sizeof(command), comm, buf);

    int ret;
    EXPECT_FALSE(IsFalse(ret = system(command), command));
    if (!ret) {
        snprintf(command, sizeof(command), "ls -s %s 2>/dev/null", buf);

        FILE *fp;
        EXPECT_TRUE(NULL != (fp = popen(command, "r")));
        if (fp) {
            char buffer[BIG_BUFFER];
            int count = 0;

            while (fgets(buffer, sizeof(buffer), fp)) {
                static const char total[] = "total ";
                int num;
                char c;

                if ((2 == sscanf(buffer, "%d log.tx%c", &num, &c)) &&
                        (num <= 40)) {
                    ++count;
                } else if (strncmp(buffer, total, sizeof(total) - 1)) {
                    fprintf(stderr, "WARNING: Parse error: %s", buffer);
                }
            }
            pclose(fp);
            if ((count != 7) && (count != 8)) {
                fprintf(stderr, "count=%d\n", count);
            }
            EXPECT_TRUE(count == 7 || count == 8);
        }
    }
    snprintf(command, sizeof(command), "rm -rf %s", buf);
    EXPECT_FALSE(IsFalse(system(command), command));
}

TEST(logcat, logrotate_suffix) {
    static const char tmp_out_dir_form[] = "/data/local/tmp/logcat.logrotate.XXXXXX";
    char tmp_out_dir[sizeof(tmp_out_dir_form)];
    ASSERT_TRUE(NULL != mkdtemp(strcpy(tmp_out_dir, tmp_out_dir_form)));

    static const char logcat_cmd[] = "logcat -b radio -b events -b system -b main"
                                     " -d -f %s/log.txt -n 10 -r 1";
    char command[sizeof(tmp_out_dir) + sizeof(logcat_cmd)];
    snprintf(command, sizeof(command), logcat_cmd, tmp_out_dir);

    int ret;
    EXPECT_FALSE(IsFalse(ret = system(command), command));
    if (!ret) {
        snprintf(command, sizeof(command), "ls %s 2>/dev/null", tmp_out_dir);

        FILE *fp;
        EXPECT_TRUE(NULL != (fp = popen(command, "r")));
        char buffer[BIG_BUFFER];
        int log_file_count = 0;

        while (fgets(buffer, sizeof(buffer), fp)) {
            static const char rotated_log_filename_prefix[] = "log.txt.";
            static const size_t rotated_log_filename_prefix_len =
                strlen(rotated_log_filename_prefix);
            static const char log_filename[] = "log.txt";

            if (!strncmp(buffer, rotated_log_filename_prefix, rotated_log_filename_prefix_len)) {
              // Rotated file should have form log.txt.##
              char* rotated_log_filename_suffix = buffer + rotated_log_filename_prefix_len;
              char* endptr;
              const long int suffix_value = strtol(rotated_log_filename_suffix, &endptr, 10);
              EXPECT_EQ(rotated_log_filename_suffix + 2, endptr);
              EXPECT_LE(suffix_value, 10);
              EXPECT_GT(suffix_value, 0);
              ++log_file_count;
              continue;
            }

            if (!strncmp(buffer, log_filename, strlen(log_filename))) {
              ++log_file_count;
              continue;
            }

            fprintf(stderr, "ERROR: Unexpected file: %s", buffer);
            ADD_FAILURE();
        }
        pclose(fp);
        EXPECT_EQ(11, log_file_count);
    }
    snprintf(command, sizeof(command), "rm -rf %s", tmp_out_dir);
    EXPECT_FALSE(IsFalse(system(command), command));
}

TEST(logcat, logrotate_continue) {
    static const char tmp_out_dir_form[] = "/data/local/tmp/logcat.logrotate.XXXXXX";
    char tmp_out_dir[sizeof(tmp_out_dir_form)];
    ASSERT_TRUE(NULL != mkdtemp(strcpy(tmp_out_dir, tmp_out_dir_form)));

    static const char log_filename[] = "log.txt";
    static const char logcat_cmd[] = "logcat -b all -d -f %s/%s -n 256 -r 1024";
    static const char cleanup_cmd[] = "rm -rf %s";
    char command[sizeof(tmp_out_dir) + sizeof(logcat_cmd) + sizeof(log_filename)];
    snprintf(command, sizeof(command), logcat_cmd, tmp_out_dir, log_filename);

    int ret;
    EXPECT_FALSE(IsFalse(ret = system(command), command));
    if (ret) {
        snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
        EXPECT_FALSE(IsFalse(system(command), command));
        return;
    }
    FILE *fp;
    snprintf(command, sizeof(command), "%s/%s", tmp_out_dir, log_filename);
    EXPECT_TRUE(NULL != ((fp = fopen(command, "r"))));
    if (!fp) {
        snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
        EXPECT_FALSE(IsFalse(system(command), command));
        return;
    }
    char *line = NULL;
    char *last_line = NULL; // this line is allowed to stutter, one-line overlap
    char *second_last_line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fp) != -1) {
        free(second_last_line);
        second_last_line = last_line;
        last_line = line;
        line = NULL;
    }
    fclose(fp);
    free(line);
    if (second_last_line == NULL) {
        fprintf(stderr, "No second to last line, using last, test may fail\n");
        second_last_line = last_line;
        last_line = NULL;
    }
    free(last_line);
    EXPECT_TRUE(NULL != second_last_line);
    if (!second_last_line) {
        snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
        EXPECT_FALSE(IsFalse(system(command), command));
        return;
    }
    // re-run the command, it should only add a few lines more content if it
    // continues where it left off.
    snprintf(command, sizeof(command), logcat_cmd, tmp_out_dir, log_filename);
    EXPECT_FALSE(IsFalse(ret = system(command), command));
    if (ret) {
        snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
        EXPECT_FALSE(IsFalse(system(command), command));
        return;
    }
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(tmp_out_dir), closedir);
    EXPECT_NE(nullptr, dir);
    if (!dir) {
        snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
        EXPECT_FALSE(IsFalse(system(command), command));
        return;
    }
    struct dirent *entry;
    unsigned count = 0;
    while ((entry = readdir(dir.get()))) {
        if (strncmp(entry->d_name, log_filename, sizeof(log_filename) - 1)) {
            continue;
        }
        snprintf(command, sizeof(command), "%s/%s", tmp_out_dir, entry->d_name);
        EXPECT_TRUE(NULL != ((fp = fopen(command, "r"))));
        if (!fp) {
            fprintf(stderr, "%s ?\n", command);
            continue;
        }
        line = NULL;
        size_t number = 0;
        while (getline(&line, &len, fp) != -1) {
            ++number;
            if (!strcmp(line, second_last_line)) {
                EXPECT_TRUE(++count <= 1);
                fprintf(stderr, "%s(%zu):\n", entry->d_name, number);
            }
        }
        fclose(fp);
        free(line);
        unlink(command);
    }
    if (count > 1) {
        char *brk = strpbrk(second_last_line, "\r\n");
        if (!brk) {
            brk = second_last_line + strlen(second_last_line);
        }
        fprintf(stderr, "\"%.*s\" occured %u times\n",
            (int)(brk - second_last_line), second_last_line, count);
    }
    free(second_last_line);

    snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
    EXPECT_FALSE(IsFalse(system(command), command));
}

TEST(logcat, logrotate_clear) {
    static const char tmp_out_dir_form[] = "/data/local/tmp/logcat.logrotate.XXXXXX";
    char tmp_out_dir[sizeof(tmp_out_dir_form)];
    ASSERT_TRUE(NULL != mkdtemp(strcpy(tmp_out_dir, tmp_out_dir_form)));

    static const char log_filename[] = "log.txt";
    static const unsigned num_val = 32;
    static const char logcat_cmd[] = "logcat -b all -d -f %s/%s -n %d -r 1";
    static const char clear_cmd[] = " -c";
    static const char cleanup_cmd[] = "rm -rf %s";
    char command[sizeof(tmp_out_dir) + sizeof(logcat_cmd) + sizeof(log_filename) + sizeof(clear_cmd) + 32];

    // Run command with all data
    {
        snprintf(command, sizeof(command) - sizeof(clear_cmd),
                 logcat_cmd, tmp_out_dir, log_filename, num_val);

        int ret;
        EXPECT_FALSE(IsFalse(ret = system(command), command));
        if (ret) {
            snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
            EXPECT_FALSE(IsFalse(system(command), command));
            return;
        }
        std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(tmp_out_dir), closedir);
        EXPECT_NE(nullptr, dir);
        if (!dir) {
            snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
            EXPECT_FALSE(IsFalse(system(command), command));
            return;
        }
        struct dirent *entry;
        unsigned count = 0;
        while ((entry = readdir(dir.get()))) {
            if (strncmp(entry->d_name, log_filename, sizeof(log_filename) - 1)) {
                continue;
            }
            ++count;
        }
        EXPECT_EQ(count, num_val + 1);
    }

    {
        // Now with -c option tacked onto the end
        strcat(command, clear_cmd);

        int ret;
        EXPECT_FALSE(IsFalse(ret = system(command), command));
        if (ret) {
            snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
            EXPECT_FALSE(system(command));
            EXPECT_FALSE(IsFalse(system(command), command));
            return;
        }
        std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(tmp_out_dir), closedir);
        EXPECT_NE(nullptr, dir);
        if (!dir) {
            snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
            EXPECT_FALSE(IsFalse(system(command), command));
            return;
        }
        struct dirent *entry;
        unsigned count = 0;
        while ((entry = readdir(dir.get()))) {
            if (strncmp(entry->d_name, log_filename, sizeof(log_filename) - 1)) {
                continue;
            }
            fprintf(stderr, "Found %s/%s!!!\n", tmp_out_dir, entry->d_name);
            ++count;
        }
        EXPECT_EQ(count, 0U);
    }

    snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
    EXPECT_FALSE(IsFalse(system(command), command));
}

static int logrotate_count_id(const char *logcat_cmd, const char *tmp_out_dir) {

    static const char log_filename[] = "log.txt";
    char command[strlen(tmp_out_dir) + strlen(logcat_cmd) + strlen(log_filename) + 32];

    snprintf(command, sizeof(command), logcat_cmd, tmp_out_dir, log_filename);

    int ret;
    EXPECT_FALSE(IsFalse(ret = system(command), command));
    if (ret) {
        return -1;
    }
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(tmp_out_dir), closedir);
    EXPECT_NE(nullptr, dir);
    if (!dir) {
        return -1;
    }
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir.get()))) {
        if (strncmp(entry->d_name, log_filename, sizeof(log_filename) - 1)) {
            continue;
        }
        ++count;
    }
    return count;
}

TEST(logcat, logrotate_id) {
    static const char logcat_cmd[] = "logcat -b all -d -f %s/%s -n 32 -r 1 --id=test";
    static const char logcat_short_cmd[] = "logcat -b all -t 10 -f %s/%s -n 32 -r 1 --id=test";
    static const char tmp_out_dir_form[] = "/data/local/tmp/logcat.logrotate.XXXXXX";
    static const char log_filename[] = "log.txt";
    char tmp_out_dir[strlen(tmp_out_dir_form) + 1];
    ASSERT_TRUE(NULL != mkdtemp(strcpy(tmp_out_dir, tmp_out_dir_form)));

    EXPECT_EQ(34, logrotate_count_id(logcat_cmd, tmp_out_dir));
    EXPECT_EQ(34, logrotate_count_id(logcat_short_cmd, tmp_out_dir));

    char id_file[strlen(tmp_out_dir_form) + strlen(log_filename) + 5];
    snprintf(id_file, sizeof(id_file), "%s/%s.id", tmp_out_dir, log_filename);
    if (getuid() != 0) {
        chmod(id_file, 0);
        EXPECT_EQ(34, logrotate_count_id(logcat_short_cmd, tmp_out_dir));
    }
    unlink(id_file);
    EXPECT_EQ(34, logrotate_count_id(logcat_short_cmd, tmp_out_dir));

    FILE *fp = fopen(id_file, "w");
    if (fp) {
        fprintf(fp, "not_a_test");
        fclose(fp);
    }
    if (getuid() != 0) {
        chmod(id_file, 0); // API to preserve content even with signature change
        ASSERT_EQ(34, logrotate_count_id(logcat_short_cmd, tmp_out_dir));
        chmod(id_file, 0600);
    }

    int new_signature;
    EXPECT_LE(2, (new_signature = logrotate_count_id(logcat_short_cmd, tmp_out_dir)));
    EXPECT_GT(34, new_signature);

    static const char cleanup_cmd[] = "rm -rf %s";
    char command[strlen(cleanup_cmd) + strlen(tmp_out_dir_form)];
    snprintf(command, sizeof(command), cleanup_cmd, tmp_out_dir);
    EXPECT_FALSE(IsFalse(system(command), command));
}

TEST(logcat, logrotate_nodir) {
    // expect logcat to error out on writing content and exit(1) for nodir
    EXPECT_EQ(W_EXITCODE(1, 0),
              system("logcat -b all -d"
                     " -f /das/nein/gerfingerpoken/logcat/log.txt"
                     " -n 256 -r 1024"));
}

static void caught_blocking_clear(int /*signum*/) {
    unsigned long long v = 0xDEADBEEFA55C0000ULL;

    v += getpid() & 0xFFFF;

    LOG_FAILURE_RETRY(__android_log_btwrite(0, EVENT_TYPE_LONG, &v, sizeof(v)));
}

TEST(logcat, blocking_clear) {
    FILE *fp;
    unsigned long long v = 0xDEADBEEFA55C0000ULL;

    pid_t pid = getpid();

    v += pid & 0xFFFF;

    // This test is racey; an event occurs between clear and dump.
    // We accept that we will get a false positive, but never a false negative.
    ASSERT_TRUE(NULL != (fp = popen(
      "( trap exit HUP QUIT INT PIPE KILL ; sleep 6; echo DONE )&"
      " logcat -b events -c 2>&1 ;"
      " logcat -b events -g 2>&1 ;"
      " logcat -v brief -b events 2>&1",
      "r")));

    char buffer[BIG_BUFFER];

    int count = 0;
    int minus_g = 0;

    int signals = 0;

    signal(SIGALRM, caught_blocking_clear);
    alarm(2);
    while (fgets(buffer, sizeof(buffer), fp)) {

        if (!strncmp(buffer, "clearLog: ", 10)) {
            fprintf(stderr, "WARNING: Test lacks permission to run :-(\n");
            count = signals = 1;
            break;
        }

        if (!strncmp(buffer, "DONE", 4)) {
            break;
        }

        int size, consumed, max, payload;
        char size_mult[3], consumed_mult[3];
        size = consumed = max = payload = 0;
        if (6 == sscanf(buffer, "events: ring buffer is %d%2s (%d%2s consumed),"
                                " max entry is %db, max payload is %db",
                                &size, size_mult, &consumed, consumed_mult,
                                &max, &payload)) {
            long full_size = size, full_consumed = consumed;

            switch(size_mult[0]) {
            case 'G':
                full_size *= 1024;
                /* FALLTHRU */
            case 'M':
                full_size *= 1024;
                /* FALLTHRU */
            case 'K':
                full_size *= 1024;
                /* FALLTHRU */
            case 'b':
                break;
            }
            switch(consumed_mult[0]) {
            case 'G':
                full_consumed *= 1024;
                /* FALLTHRU */
            case 'M':
                full_consumed *= 1024;
                /* FALLTHRU */
            case 'K':
                full_consumed *= 1024;
                /* FALLTHRU */
            case 'b':
                break;
            }
            EXPECT_GT(full_size, full_consumed);
            EXPECT_GT(full_size, max);
            EXPECT_GT(max, payload);
            EXPECT_GT(max, full_consumed);

            ++minus_g;
            continue;
        }

        ++count;

        int p;
        unsigned long long l;

        if ((2 != sscanf(buffer, "I/[0] ( %u): %lld", &p, &l))
         || (p != pid)) {
            continue;
        }

        if (l == v) {
            if (count > 1) {
                fprintf(stderr, "WARNING: Possible false positive\n");
            }
            ++signals;
            break;
        }
    }
    alarm(0);
    signal(SIGALRM, SIG_DFL);

    // Generate SIGPIPE
    fclose(fp);
    caught_blocking_clear(0);

    pclose(fp);

    EXPECT_LE(1, count);
    EXPECT_EQ(1, minus_g);

    EXPECT_EQ(1, signals);
}

static bool get_white_black(char **list) {
    FILE *fp;

    fp = popen("logcat -p 2>/dev/null", "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: logcat -p 2>/dev/null\n");
        return false;
    }

    char buffer[BIG_BUFFER];

    while (fgets(buffer, sizeof(buffer), fp)) {
        char *hold = *list;
        char *buf = buffer;
	while (isspace(*buf)) {
            ++buf;
        }
        char *end = buf + strlen(buf);
        while (isspace(*--end) && (end >= buf)) {
            *end = '\0';
        }
        if (end < buf) {
            continue;
        }
        if (hold) {
            asprintf(list, "%s %s", hold, buf);
            free(hold);
        } else {
            asprintf(list, "%s", buf);
        }
    }
    pclose(fp);
    return *list != NULL;
}

static bool set_white_black(const char *list) {
    FILE *fp;

    char buffer[BIG_BUFFER];

    snprintf(buffer, sizeof(buffer), "logcat -P '%s' 2>&1", list ? list : "");
    fp = popen(buffer, "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: %s\n", buffer);
        return false;
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        char *buf = buffer;
	while (isspace(*buf)) {
            ++buf;
        }
        char *end = buf + strlen(buf);
        while ((end > buf) && isspace(*--end)) {
            *end = '\0';
        }
        if (end <= buf) {
            continue;
        }
        fprintf(stderr, "%s\n", buf);
        pclose(fp);
        return false;
    }
    return pclose(fp) == 0;
}

TEST(logcat, white_black_adjust) {
    char *list = NULL;
    char *adjust = NULL;

    get_white_black(&list);

    static const char adjustment[] = "~! 300/20 300/25 2000 ~1000/5 ~1000/30";
    ASSERT_EQ(true, set_white_black(adjustment));
    ASSERT_EQ(true, get_white_black(&adjust));
    EXPECT_STREQ(adjustment, adjust);
    free(adjust);
    adjust = NULL;

    static const char adjustment2[] = "300/20 300/21 2000 ~1000";
    ASSERT_EQ(true, set_white_black(adjustment2));
    ASSERT_EQ(true, get_white_black(&adjust));
    EXPECT_STREQ(adjustment2, adjust);
    free(adjust);
    adjust = NULL;

    ASSERT_EQ(true, set_white_black(list));
    get_white_black(&adjust);
    EXPECT_STREQ(list ? list : "", adjust ? adjust : "");
    free(adjust);
    adjust = NULL;

    free(list);
    list = NULL;
}

TEST(logcat, regex) {
    FILE *fp;
    int count = 0;

    char buffer[BIG_BUFFER];

    snprintf(buffer, sizeof(buffer), "logcat --pid %d -d -e logcat_test_a+b", getpid());

    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test_ab"));
    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test_b"));
    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test_aaaab"));
    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test_aaaa"));

    // Let the logs settle
    sleep(1);

    ASSERT_TRUE(NULL != (fp = popen(buffer, "r")));

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strncmp(begin, buffer, sizeof(begin) - 1)) {
            continue;
        }

        EXPECT_TRUE(strstr(buffer, "logcat_test_") != NULL);

        count++;
    }

    pclose(fp);

    ASSERT_EQ(2, count);
}

TEST(logcat, maxcount) {
    FILE *fp;
    int count = 0;

    char buffer[BIG_BUFFER];

    snprintf(buffer, sizeof(buffer), "logcat --pid %d -d --max-count 3", getpid());

    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test"));
    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test"));
    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test"));
    LOG_FAILURE_RETRY(__android_log_print(ANDROID_LOG_WARN, "logcat_test", "logcat_test"));

    // Let the logs settle
    sleep(1);

    ASSERT_TRUE(NULL != (fp = popen(buffer, "r")));

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (!strncmp(begin, buffer, sizeof(begin) - 1)) {
            continue;
        }

        count++;
    }

    pclose(fp);

    ASSERT_EQ(3, count);
}

static bool End_to_End(const char* tag, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((__format__(printf, 2, 3)))
#endif
    ;

static bool End_to_End(const char* tag, const char* fmt, ...) {
    FILE *fp = popen("logcat -v brief -b events -v descriptive -t 100 2>/dev/null", "r");
    if (!fp) return false;

    char buffer[BIG_BUFFER];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    char *str = NULL;
    asprintf(&str, "I/%s ( %%d): %s%%c", tag, buffer);
    std::string expect(str);
    free(str);

    int count = 0;
    pid_t pid = getpid();
    std::string lastMatch;
    while (fgets(buffer, sizeof(buffer), fp)) {
        char newline;
        int p;
        int ret = sscanf(buffer, expect.c_str(), &p, &newline);
        if ((2 == ret) && (p == pid) && (newline == '\n')) ++count;
        else if ((1 == ret) && (p == pid) && (count == 0)) lastMatch = buffer;
    }

    pclose(fp);

    if ((count == 0) && (lastMatch.length() > 0)) {
        // Help us pinpoint where things went wrong ...
        fprintf(stderr, "Closest match for\n    %s\n  is\n    %s",
                expect.c_str(), lastMatch.c_str());
    }

    return count == 1;
}

TEST(logcat, descriptive) {
    struct tag {
        uint32_t tagNo;
        const char* tagStr;
    };

    {
        static const struct tag hhgtg = { 42, "answer" };
        android_log_event_context ctx(hhgtg.tagNo);
        static const char theAnswer[] = "what is five by seven";
        ctx << theAnswer;
        ctx.write();
        EXPECT_TRUE(End_to_End(hhgtg.tagStr,
                               "to life the universe etc=%s", theAnswer));
    }

    {
        static const struct tag sync = { 2720, "sync" };
        static const char id[] = "logcat.decriptive";
        {
            android_log_event_context ctx(sync.tagNo);
            ctx << id << (int32_t)42 << (int32_t)-1 << (int32_t)0;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr,
                                   "[id=%s,event=42,source=-1,account=0]",
                                   id));
        }

        // Partial match to description
        {
            android_log_event_context ctx(sync.tagNo);
            ctx << id << (int32_t)43 << (int64_t)-1 << (int32_t)0;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr,
                                   "[id=%s,event=43,-1,0]",
                                   id));
        }

        // Negative Test of End_to_End, ensure it is working
        {
            android_log_event_context ctx(sync.tagNo);
            ctx << id << (int32_t)44 << (int32_t)-1 << (int64_t)0;
            ctx.write();
            fprintf(stderr, "Expect a \"Closest match\" message\n");
            EXPECT_FALSE(End_to_End(sync.tagStr,
                                    "[id=%s,event=44,source=-1,account=0]",
                                    id));
        }
    }

    {
        static const struct tag sync = { 2747, "contacts_aggregation" };
        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint64_t)30 << (int32_t)2;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr,
                                   "[aggregation time=30ms,count=2]"));
        }

        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint64_t)31570 << (int32_t)911;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr,
                                   "[aggregation time=31.57s,count=911]"));
        }
    }

    {
        static const struct tag sync = { 75000, "sqlite_mem_alarm_current" };
        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint32_t)512;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr, "current=512B"));
        }

        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint32_t)3072;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr, "current=3KB"));
        }

        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint32_t)2097152;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr, "current=2MB"));
        }

        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint32_t)2097153;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr, "current=2097153B"));
        }

        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint32_t)1073741824;
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr, "current=1GB"));
        }

        {
            android_log_event_context ctx(sync.tagNo);
            ctx << (uint32_t)3221225472; // 3MB, but on purpose overflowed
            ctx.write();
            EXPECT_TRUE(End_to_End(sync.tagStr, "current=-1GB"));
        }
    }

}
