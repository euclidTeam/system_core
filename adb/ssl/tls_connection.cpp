/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "adb/ssl/tls_connection.h"

#include <vector>

#include <android-base/logging.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace adb {
namespace ssl {

namespace {

class TlsConnectionImpl : public TlsConnection {
  public:
    explicit TlsConnectionImpl(Role role, std::string_view cert, std::string_view priv_key);
    virtual ~TlsConnectionImpl();

    virtual void enableCertificateVerification(bool enable) override;
    virtual bool addTrustedCertificate(std::string_view cert) override;
    virtual bool doHandshake(int fd) override;
    virtual std::vector<uint8_t> readFully(int size) override;
    virtual bool writeFully(std::string_view data) override;

    // Checks if our private key and certificate are valid.
    bool hasValidKey();

  private:
    static bssl::UniquePtr<EVP_PKEY> EvpPkeyFromPEM(std::string_view pem);
    static bssl::UniquePtr<CRYPTO_BUFFER> bufferFromPEM(std::string_view pem);
    static bssl::UniquePtr<X509> x509FromBuffer(bssl::UniquePtr<CRYPTO_BUFFER> buffer);
    static const char* SSLErrorString();
    void invalidate();

    Role role_;
    bssl::UniquePtr<EVP_PKEY> priv_key_;
    bssl::UniquePtr<CRYPTO_BUFFER> cert_;

    bssl::UniquePtr<SSL_CTX> ssl_ctx_;
    bssl::UniquePtr<SSL> ssl_;
    std::vector<bssl::UniquePtr<X509>> known_certificates_;
    bool enable_cert_verification_ = true;
};  // TlsConnectionImpl

TlsConnectionImpl::TlsConnectionImpl(Role role, std::string_view cert, std::string_view priv_key)
    : role_(role) {
    CHECK(!cert.empty() && !priv_key.empty());
    LOG(INFO) << "Initializing adbwifi TlsConnection";
    // Init SSL library. Registers all available SSL/TLS ciphers and digests.
    SSL_library_init();
    SSL_load_error_strings();
    cert_ = bufferFromPEM(cert);
    priv_key_ = EvpPkeyFromPEM(priv_key);
}

TlsConnectionImpl::~TlsConnectionImpl() {
    // shutdown the SSL connection
    if (ssl_ != nullptr) {
        SSL_shutdown(ssl_.get());
    }
}

// static
const char* TlsConnectionImpl::SSLErrorString() {
    auto sslerr = ERR_get_error();
    return ERR_reason_error_string(sslerr);
}

// static
bssl::UniquePtr<EVP_PKEY> TlsConnectionImpl::EvpPkeyFromPEM(std::string_view pem) {
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem.data(), pem.size()));
    return bssl::UniquePtr<EVP_PKEY>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

// static
bssl::UniquePtr<CRYPTO_BUFFER> TlsConnectionImpl::bufferFromPEM(std::string_view pem) {
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem.data(), pem.size()));
    char* name = nullptr;
    char* header = nullptr;
    uint8_t* data = nullptr;
    long data_len = 0;

    if (!PEM_read_bio(bio.get(), &name, &header, &data, &data_len)) {
        LOG(ERROR) << "Failed to read certificate";
        return nullptr;
    }
    OPENSSL_free(name);
    OPENSSL_free(header);

    auto ret = bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(data, data_len, nullptr));
    OPENSSL_free(data);
    return ret;
}

// static
bssl::UniquePtr<X509> TlsConnectionImpl::x509FromBuffer(bssl::UniquePtr<CRYPTO_BUFFER> buffer) {
    if (!buffer) {
        return nullptr;
    }
    const uint8_t* derp = CRYPTO_BUFFER_data(buffer.get());
    return bssl::UniquePtr<X509>(d2i_X509(nullptr, &derp, CRYPTO_BUFFER_len(buffer.get())));
}

bool TlsConnectionImpl::hasValidKey() {
    return (cert_ != nullptr && priv_key_ != nullptr);
}

void TlsConnectionImpl::enableCertificateVerification(bool enable) {
    enable_cert_verification_ = enable;
}

bool TlsConnectionImpl::addTrustedCertificate(std::string_view cert) {
    if (cert.empty()) {
        LOG(ERROR) << "Certificate is empty";
        return false;
    }
    // Create X509 buffer from the certificate string
    auto buf = x509FromBuffer(bufferFromPEM(cert));
    if (buf == nullptr) {
        LOG(ERROR) << "Failed to create a X509 buffer for the certificate.";
        return false;
    }
    known_certificates_.push_back(std::move(buf));
    return true;
}

bool TlsConnectionImpl::doHandshake(int fd) {
    // Don't even try if we don't have valid certificate and key.
    if (!hasValidKey()) {
        return false;
    }

    int err = -1;
    LOG(INFO) << "Starting adbwifi tls handshake";
    switch (role_) {
        case Role::Server:
            ssl_ctx_.reset(SSL_CTX_new(TLSv1_2_server_method()));
            break;
        case Role::Client:
            ssl_ctx_.reset(SSL_CTX_new(TLSv1_2_client_method()));
            break;
    }

    if (enable_cert_verification_) {
        // Register every certificate in our keystore. This will restrict
        // connnections to only these known certificates.
        for (auto const& cert : known_certificates_) {
            if (X509_STORE_add_cert(SSL_CTX_get_cert_store(ssl_ctx_.get()), cert.get()) == 0) {
                LOG(ERROR) << "Unable to add certificates into the X509_STORE";
                return false;
            }
        }
    } else {
        // Allow any certificate
        SSL_CTX_set_cert_verify_callback(
                ssl_ctx_.get(), [](X509_STORE_CTX*, void*) -> int { return 1; }, nullptr);
    }

    // Set automatic curve selection for |ssl_ctx_|. It will select the
    // highest preference curve for the ECDH temp keys during key exchange.
    if (!SSL_CTX_set_ecdh_auto(ssl_ctx_.get(), 1)) {
        LOG(ERROR) << "SSL_CTX-set_ecdh_auto() failed";
        invalidate();
        return false;
    }

    // Register our certificate and private key.
    std::vector<CRYPTO_BUFFER*> cert_chain = {
            cert_.get(),
    };
    if (!(err = SSL_CTX_set_chain_and_key(ssl_ctx_.get(), &cert_chain[0], 1, priv_key_.get(),
                                          nullptr))) {
        LOG(ERROR) << "Unable to register the certificate chain file and private key ["
                   << SSLErrorString() << "]";
        invalidate();
        return false;
    }

    SSL_CTX_set_verify(ssl_ctx_.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);

    // Okay! Let's try to do the handshake!
    ssl_.reset(SSL_new(ssl_ctx_.get()));
    SSL_set_fd(ssl_.get(), fd);
    switch (role_) {
        case Role::Server:
            err = SSL_accept(ssl_.get());
            break;
        case Role::Client:
            err = SSL_connect(ssl_.get());
            break;
    }
    if (err != 1) {
        LOG(ERROR) << "Handshake failed in SSL_accept/SSL_connect [" << SSLErrorString() << "]";
        invalidate();
        return false;
    }

    LOG(INFO) << "Handshake succeeded.";
    return true;
}

void TlsConnectionImpl::invalidate() {
    ssl_.reset();
    ssl_ctx_.reset();
}

std::vector<uint8_t> TlsConnectionImpl::readFully(int size) {
    CHECK_GT(size, 0);
    if (!ssl_) {
        LOG(ERROR) << "Tried to read on a null SSL connection";
        return {};
    }

    std::vector<uint8_t> buf(size);
    size_t offset = 0;
    while (size > 0) {
        int bytes_read = SSL_read(ssl_.get(), buf.data() + offset, size);
        if (bytes_read <= 0) {
            LOG(WARNING) << "SSL_read failed [" << SSLErrorString() << "]";
            return {};
        }
        size -= bytes_read;
        offset += bytes_read;
    }
    return buf;
}

bool TlsConnectionImpl::writeFully(std::string_view data) {
    CHECK(!data.empty());
    if (!ssl_) {
        LOG(ERROR) << "Tried to read on a null SSL connection";
        return false;
    }

    size_t offset = 0;
    int size = data.size();
    while (size > 0) {
        int bytes_out = SSL_write(ssl_.get(), data.data() + offset, size);
        if (bytes_out <= 0) {
            LOG(WARNING) << "SSL_write failed [" << SSLErrorString() << "]";
            return false;
        }
        size -= bytes_out;
        offset += bytes_out;
    }
    return true;
}
}  // namespace

// static
std::unique_ptr<TlsConnection> TlsConnection::create(TlsConnection::Role role,
                                                     std::string_view cert,
                                                     std::string_view priv_key) {
    if (cert.empty() || priv_key.empty()) {
        return nullptr;
    }

    auto* p = new TlsConnectionImpl(role, cert, priv_key);
    if (!p->hasValidKey()) {
        delete p;
        return nullptr;
    }
    return std::unique_ptr<TlsConnection>(p);
}

}  // namespace ssl
}  // namespace adb
