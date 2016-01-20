/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "tcp.h"

#include <android-base/stringprintf.h>

namespace tcp {

static constexpr int kProtocolVersion = 1;
static constexpr size_t kHandshakeLength = 4;
static constexpr int kHandshakeTimeoutMs = 2000;

// Extract the big-endian 8-byte message length into a 64-bit number.
static uint64_t ExtractMessageLength(const void* buffer) {
    uint64_t ret = 0;
    for (int i = 0; i < 8; ++i) {
        ret |= uint64_t{reinterpret_cast<const uint8_t*>(buffer)[i]} << (56 - i * 8);
    }
    return ret;
}

// Encode the 64-bit number into a big-endian 8-byte message length.
static void EncodeMessageLength(uint64_t length, void* buffer) {
    for (int i = 0; i < 8; ++i) {
        reinterpret_cast<uint8_t*>(buffer)[i] = length >> (56 - i * 8);
    }
}

class TcpTransport : public Transport {
  public:
    // Factory function so we can return nullptr if initialization fails.
    static std::unique_ptr<TcpTransport> NewTransport(std::unique_ptr<Socket> socket,
                                                      std::string* error);

    ~TcpTransport() override = default;

    ssize_t Read(void* data, size_t length) override;
    ssize_t Write(const void* data, size_t length) override;
    int Close() override;

  private:
    TcpTransport(std::unique_ptr<Socket> sock) : socket_(std::move(sock)) {}

    // Connects to the device and performs the initial handshake. Returns an empty string on
    // success, an error message on failure.
    std::string InitializeProtocol();

    std::unique_ptr<Socket> socket_;
    uint64_t message_bytes_left_ = 0;

    DISALLOW_COPY_AND_ASSIGN(TcpTransport);
};

std::unique_ptr<TcpTransport> TcpTransport::NewTransport(std::unique_ptr<Socket> socket,
                                                         std::string* error) {
    std::unique_ptr<TcpTransport> transport(new TcpTransport(std::move(socket)));

    std::string result = transport->InitializeProtocol();
    if (!result.empty()) {
        if (error != nullptr) {
            *error = std::move(result);
        }
        return nullptr;
    }

    return transport;
}

// These error strings are checked in tcp_test.cpp and should be kept in sync.
std::string TcpTransport::InitializeProtocol() {
    std::string handshake_message(android::base::StringPrintf("FB%02d", kProtocolVersion));

    if (socket_->Send(handshake_message.c_str(), kHandshakeLength) != kHandshakeLength) {
        return "Failed to send initialization message";
    }

    char buffer[kHandshakeLength];
    if (socket_->ReceiveAll(buffer, kHandshakeLength, kHandshakeTimeoutMs) != kHandshakeLength) {
        return "Failed to receive initialization message; target may not support TCP fastboot";
    }

    if (memcmp(buffer, "FB", 2) != 0) {
        return "Unrecognized initialization message; target may not support TCP fastboot";
    }

    if (memcmp(buffer + 2, "01", 2) != 0) {
        return android::base::StringPrintf("Unknown TCP protocol version: %s (host version: %02d)",
                                           std::string(buffer + 2, 2).c_str(), kProtocolVersion);
    }

    return "";
}

ssize_t TcpTransport::Read(void* data, size_t length) {
    if (socket_ == nullptr) {
        return -1;
    }

    // Unless we're mid-message, read the next 8-byte message length.
    if (message_bytes_left_ == 0) {
        char buffer[8];
        if (socket_->ReceiveAll(buffer, 8, 0) != 8) {
            Close();
            return -1;
        }
        message_bytes_left_ = ExtractMessageLength(buffer);
    }

    // Now read the message.
    if (length > message_bytes_left_) {
        length = message_bytes_left_;
    }
    ssize_t bytes_read = socket_->ReceiveAll(data, length, 0);
    message_bytes_left_ -= bytes_read;
    return bytes_read;
}

// If we fail during packet send it's a non-recoverable error, so close the socket.
ssize_t TcpTransport::Write(const void* data, size_t length) {
    if (socket_ == nullptr) {
        return -1;
    }

    // Write the 8-byte message length first.
    char buffer[8];
    EncodeMessageLength(length, buffer);
    if (socket_->Send(buffer, 8) != 8) {
        Close();
        return -1;
    }

    // Now write the message itself.
    if (socket_->Send(data, length) != static_cast<ssize_t>(length)) {
        Close();
        return -1;
    }

    return length;
}

int TcpTransport::Close() {
    if (socket_ == nullptr) {
        return 0;
    }

    int result = socket_->Close();
    socket_.reset();
    return result;
}

std::unique_ptr<Transport> Connect(const std::string& hostname, int port, std::string* error) {
    return internal::Connect(Socket::NewClient(Socket::Protocol::kTcp, hostname, port, error),
                             error);
}

namespace internal {

std::unique_ptr<Transport> Connect(std::unique_ptr<Socket> sock, std::string* error) {
    if (sock == nullptr) {
        // If Socket creation failed |error| is already set.
        return nullptr;
    }

    // The main fastboot engine doesn't use smart pointers yet so we just return the raw pointer.
    return TcpTransport::NewTransport(std::move(sock), error);
}

}  // namespace internal

}  // namespace tcp
