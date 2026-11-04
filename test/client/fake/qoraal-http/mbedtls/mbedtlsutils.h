/*
    Copyright (C) 2026, Navaro, All Rights Reserved
    SPDX-License-Identifier: MIT

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
 */

/**
 * @file        mbedtlsutils.h
 *
 * Stands in for qoraal-http/mbedtls/mbedtlsutils.h in the TLS build of the
 * client unit test: the part of the mbedTLS API that httpclient.c uses, so
 * test_httpclient.c can fake it. The error values are those of mbedTLS 4.1.
 * The real header and mbedTLS are not needed on the host.
 */

#ifndef __MBEDTLSUTILS_H__
#define __MBEDTLSUTILS_H__

#include <stddef.h>
#include <stdint.h>

#define MBEDTLS_ERR_NET_RECV_FAILED                         -0x004C
#define MBEDTLS_ERR_NET_SEND_FAILED                         -0x004E
#define MBEDTLS_ERR_NET_CONN_RESET                          -0x0050
#define MBEDTLS_ERR_X509_CERT_VERIFY_FAILED                 -0x2700
#define MBEDTLS_ERR_SSL_CERTIFICATE_VERIFICATION_WITHOUT_HOSTNAME   -0x5D80
#define MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS                   -0x6500
#define MBEDTLS_ERR_SSL_TIMEOUT                             -0x6800
#define MBEDTLS_ERR_SSL_WANT_WRITE                          -0x6880
#define MBEDTLS_ERR_SSL_WANT_READ                           -0x6900
#define MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE                   -0x6A80
#define MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS                  -0x7000
#define MBEDTLS_ERR_SSL_CONN_EOF                            -0x7280
#define MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED                   -0x7680
#define MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE                 -0x7780
#define MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY                   -0x7880
#define MBEDTLS_ERR_SSL_BAD_CERTIFICATE                     -0x7A00
#define MBEDTLS_ERR_SSL_ALLOC_FAILED                        -141    /* PSA_ERROR_INSUFFICIENT_MEMORY */

typedef struct mbedtls_ssl_config {
    int                     unused ;
} mbedtls_ssl_config ;

typedef struct mbedtls_ssl_context {
    int                     unused ;
} mbedtls_ssl_context ;

typedef int mbedtls_ssl_send_t (void *ctx, const unsigned char *buf, size_t len) ;
typedef int mbedtls_ssl_recv_t (void *ctx, unsigned char *buf, size_t len) ;
typedef int mbedtls_ssl_recv_timeout_t (void *ctx, unsigned char *buf, size_t len, uint32_t timeout) ;

#ifdef __cplusplus
extern "C" {
#endif

    void        mbedtls_ssl_init (mbedtls_ssl_context *ssl) ;
    int         mbedtls_ssl_setup (mbedtls_ssl_context *ssl, const mbedtls_ssl_config *conf) ;
    void        mbedtls_ssl_set_bio (mbedtls_ssl_context *ssl, void *p_bio, mbedtls_ssl_send_t *f_send,
                                     mbedtls_ssl_recv_t *f_recv, mbedtls_ssl_recv_timeout_t *f_recv_timeout) ;
    int         mbedtls_ssl_set_hostname (mbedtls_ssl_context *ssl, const char *hostname) ;
    int         mbedtls_ssl_handshake (mbedtls_ssl_context *ssl) ;
    int         mbedtls_ssl_read (mbedtls_ssl_context *ssl, unsigned char *buf, size_t len) ;
    int         mbedtls_ssl_write (mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len) ;
    size_t      mbedtls_ssl_get_bytes_avail (const mbedtls_ssl_context *ssl) ;
    int         mbedtls_ssl_close_notify (mbedtls_ssl_context *ssl) ;
    void        mbedtls_ssl_free (mbedtls_ssl_context *ssl) ;

    int         mbedtls_net_send (void *ctx, const unsigned char *buf, size_t len) ;
    int         mbedtls_net_recv (void *ctx, unsigned char *buf, size_t len) ;

#ifdef __cplusplus
}
#endif

#endif /* __MBEDTLSUTILS_H__ */
