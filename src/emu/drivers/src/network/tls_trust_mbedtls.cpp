// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-3.0-or-later

// The system's certificate check for the libretro core on Linux and Android,
// which has neither Qt Network (the Qt frontend's way on Linux) nor the
// Android app's Java side (its way on Android): the chain is checked with
// mbedtls against the certificate authorities the system keeps on disk.
#include "tls_trust.h"

#include <mbedtls/x509_crt.h>

namespace eka2l1::drivers {
    namespace {
        bool load_system_authorities(mbedtls_x509_crt &authorities) {
            // A bundle file where distributions keep one, else a directory of
            // PEM files (Android's, and some distributions')
            static const char *const bundles[] = {
                "/etc/ssl/certs/ca-certificates.crt",
                "/etc/pki/tls/certs/ca-bundle.crt",
                "/etc/ssl/ca-bundle.pem",
                "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
                "/etc/ssl/cert.pem",
            };
            for (const char *bundle : bundles) {
                if (mbedtls_x509_crt_parse_file(&authorities, bundle) >= 0 && authorities.version != 0)
                    return true;
            }
            static const char *const directories[] = {
                "/system/etc/security/cacerts",
                "/apex/com.android.conscrypt/cacerts",
                "/etc/ssl/certs",
            };
            for (const char *directory : directories) {
                if (mbedtls_x509_crt_parse_path(&authorities, directory) >= 0 && authorities.version != 0)
                    return true;
            }
            return false;
        }
    }

    bool verify_system_certificate(const tls_certificate_chain &chain, const std::string &hostname) {
        if (chain.empty())
            return false;

        mbedtls_x509_crt certificates;
        mbedtls_x509_crt authorities;
        mbedtls_x509_crt_init(&certificates);
        mbedtls_x509_crt_init(&authorities);

        bool valid = true;
        for (const auto &der : chain) {
            if (mbedtls_x509_crt_parse_der(&certificates, der.data(), der.size()) != 0) {
                valid = false;
                break;
            }
        }
        if (valid)
            valid = load_system_authorities(authorities);
        if (valid) {
            std::uint32_t flags = 0;
            valid = mbedtls_x509_crt_verify(&certificates, &authorities, nullptr,
                        hostname.empty() ? nullptr : hostname.c_str(), &flags, nullptr, nullptr) == 0;
        }

        mbedtls_x509_crt_free(&certificates);
        mbedtls_x509_crt_free(&authorities);
        return valid;
    }
}
