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

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "adb/pairing/pairing_connection.h"

extern "C" {

// PairingServer is the server side of the PairingConnection protocol. It will
// listen for incoming PairingClient connections, and allocate a new
// PairingConnection per client for processing. PairingServer can handle multiple
// connections, but the first one to establish the pairing will be the only one
// to succeed. All others will be disconnected.
//
// See pairing_connection_test.cpp for example usage.
//
struct PairingServerCtx;
typedef void (*pairing_server_result_cb)(const PeerInfo*, void*);

// Starts the pairing server. This call is non-blocking. Upon completion,
// if the pairing was successful, then |cb| will be called with the PeerInfo
// containing the info of the trusted peer. Otherwise, |cb| will be
// called with an empty value. Start can only be called once in the lifetime
// of this object.
//
// Returns the port number if PairingServer was successfully started. Otherwise,
// returns 0.
uint16_t pairing_server_start(PairingServerCtx* ctx, pairing_server_result_cb cb, void* opaque);

// Creates a new PairingServer instance. May return null if unable
// to create an instance. |pswd|, |certificate| and |priv_key| cannot
// be empty. |port| is the port PairingServer will listen to PairingClient
// connections on. |peer_info| must contain non-empty strings for the guid
// and name fields.
PairingServerCtx* pairing_server_new(const uint8_t* pswd, size_t pswd_len,
                                     const PeerInfo& peer_info, const uint8_t* x509_cert_pem,
                                     size_t x509_size, const uint8_t* priv_key_pem,
                                     size_t priv_size, uint16_t port);

void pairing_server_destroy(PairingServerCtx* ctx);

}  // extern "C"
