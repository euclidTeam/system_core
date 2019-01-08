/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <android-base/cmsg.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <gtest/gtest.h>

#if !defined(_WIN32)

using android::base::ReceiveFileDescriptors;
using android::base::SendFileDescriptors;
using android::base::unique_fd;

static ino_t GetInode(int fd) {
  struct stat st;
  if (fstat(fd, &st) != 0) {
    PLOG(FATAL) << "fstat failed";
  }

  return st.st_ino;
}

struct CmsgTest : ::testing::TestWithParam<bool> {
  bool Seqpacket() { return GetParam(); }

  void SetUp() override {
    ASSERT_TRUE(
        android::base::Socketpair(Seqpacket() ? SOCK_SEQPACKET : SOCK_STREAM, &send, &recv));
    int dup1 = dup(tmp1.fd);
    ASSERT_NE(-1, dup1);
    int dup2 = dup(tmp2.fd);
    ASSERT_NE(-1, dup2);

    fd1.reset(dup1);
    fd2.reset(dup2);

    ino1 = GetInode(dup1);
    ino2 = GetInode(dup2);
  }

  unique_fd send;
  unique_fd recv;

  TemporaryFile tmp1;
  TemporaryFile tmp2;

  unique_fd fd1;
  unique_fd fd2;

  ino_t ino1;
  ino_t ino2;
};

TEST_P(CmsgTest, smoke) {
  ASSERT_EQ(1, SendFileDescriptors(send.get(), "x", 1, std::move(fd1)));
  ASSERT_EQ(-1, fd1.get());

  char buf[2];
  unique_fd received;
  ASSERT_EQ(1, ReceiveFileDescriptors(recv.get(), buf, 2, &received));
  ASSERT_EQ('x', buf[0]);
  ASSERT_NE(-1, received.get());

  ASSERT_EQ(ino1, GetInode(received.get()));
}

TEST_P(CmsgTest, msg_trunc) {
  ASSERT_EQ(2, SendFileDescriptors(send.get(), "ab", 2, std::move(fd1), std::move(fd2)));

  char buf[2];
  unique_fd received1, received2;

  ssize_t rc = ReceiveFileDescriptors(recv.get(), buf, 1, &received1, &received2);
  if (Seqpacket()) {
    ASSERT_EQ(-1, rc);
    ASSERT_EQ(ERANGE, errno);
    ASSERT_EQ(-1, received1.get());
    ASSERT_EQ(-1, received2.get());
  } else {
    ASSERT_EQ(1, rc);
    ASSERT_NE(-1, received1.get());
    ASSERT_NE(-1, received2.get());
    ASSERT_EQ(ino1, GetInode(received1.get()));
    ASSERT_EQ(ino2, GetInode(received2.get()));
    ASSERT_EQ(1, read(recv.get(), buf, 2));
  }
}

TEST_P(CmsgTest, msg_ctrunc) {
  ASSERT_EQ(1, SendFileDescriptors(send.get(), "a", 1, std::move(fd1), std::move(fd2)));

  char buf[2];
  unique_fd received;
  ASSERT_EQ(-1, ReceiveFileDescriptors(recv.get(), buf, 1, &received));
  ASSERT_EQ(ERANGE, errno);
  ASSERT_EQ(-1, received.get());
}

TEST_P(CmsgTest, multiple_fd_ordering) {
  ASSERT_EQ(1, SendFileDescriptors(send.get(), "a", 1, std::move(fd1), std::move(fd2)));

  char buf[2];
  unique_fd received1, received2;
  ASSERT_EQ(1, ReceiveFileDescriptors(recv.get(), buf, 1, &received1, &received2));

  ASSERT_NE(-1, received1.get());
  ASSERT_NE(-1, received2.get());

  ASSERT_EQ(ino1, GetInode(received1.get()));
  ASSERT_EQ(ino2, GetInode(received2.get()));
}

TEST_P(CmsgTest, separate_fd_ordering) {
  ASSERT_EQ(1, SendFileDescriptors(send.get(), "a", 1, std::move(fd1)));
  ASSERT_EQ(1, SendFileDescriptors(send.get(), "b", 1, std::move(fd2)));

  char buf[2];
  unique_fd received1, received2;
  ASSERT_EQ(1, ReceiveFileDescriptors(recv.get(), buf, 1, &received1));
  ASSERT_EQ(1, ReceiveFileDescriptors(recv.get(), buf, 1, &received2));

  ASSERT_NE(-1, received1.get());
  ASSERT_NE(-1, received2.get());

  ASSERT_EQ(ino1, GetInode(received1.get()));
  ASSERT_EQ(ino2, GetInode(received2.get()));
}

TEST_P(CmsgTest, separate_fds_no_coalescing) {
  unique_fd sent1(dup(tmp1.fd));
  unique_fd sent2(dup(tmp2.fd));

  ASSERT_EQ(1, SendFileDescriptors(send.get(), "", 1, std::move(fd1)));
  ASSERT_EQ(1, SendFileDescriptors(send.get(), "", 1, std::move(fd2)));

  char buf[2];
  unique_fd received1, received2;
  ASSERT_EQ(-1, ReceiveFileDescriptors(recv.get(), buf, 2, &received1, &received2));
  ASSERT_EQ(ENOMSG, errno);
  ASSERT_EQ(-1, received1.get());
  ASSERT_EQ(-1, received2.get());
}

INSTANTIATE_TEST_CASE_P(CmsgTest, CmsgTest, testing::Bool());

#endif
