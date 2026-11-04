/*
    Copyright (C) 2015-2025, Navaro, All Rights Reserved
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


#include <string.h>
 #include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "qoraal/qoraal.h"
#include "qoraal-http/config.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpclient.h"
#include "qoraal-http/httpparse.h"
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
#include "qoraal-http/mbedtls/mbedtlsutils.h"
#endif

/* Used when no httpclient_set_content_type() is in effect, as before it existed. */
#define HTTP_POST_DEFAULT_CONTENT_TYPE  "text/plain"

/* The body framing of a request, for httpclient_send_head(). */
#define HTTP_CLIENT_REQ_NO_BODY         0
#define HTTP_CLIENT_REQ_LENGTH          1   /* Content-Type and Content-Length */
#define HTTP_CLIENT_REQ_CHUNKED         2   /* Content-Type and chunked transfer coding */
#define HTTP_CLIENT_REQ_WEBSOCK         3   /* no body, the websocket upgrade fields */

/* HTTP_CLIENT_T.body_state: where httpclient_read_next_ex() is in the body. */
#define HTTP_CLIENT_BODY_NONE           0   /* no response is being read */
#define HTTP_CLIENT_BODY_LENGTH         1   /* body_left bytes to go of a Content-Length body */
#define HTTP_CLIENT_BODY_CLOSE          2   /* the body ends when the server closes the connection */
#define HTTP_CLIENT_BODY_CHUNK_SIZE     3   /* a chunk size line is next */
#define HTTP_CLIENT_BODY_CHUNK_DATA     4   /* body_left bytes to go of the chunk data */
#define HTTP_CLIENT_BODY_CHUNK_END      5   /* the CRLF after the chunk data is next */
#define HTTP_CLIENT_BODY_TRAILER        6   /* trailer fields, up to the empty line */
#define HTTP_CLIENT_BODY_DONE           7   /* the body is complete */
#define HTTP_CLIENT_BODY_ERROR          8   /* the body failed with body_error */

/* The lowest HTTP_CLIENT_E_* code, to tell them from the codes of a callback. */
#define HTTP_CLIENT_E_LOWEST            HTTP_CLIENT_E_PARM

/* The head is parsed in place in the receive buffer. */
#if (HTTP_CLIENT_MAX_RECV_HEADER_LENGTH) > (HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH)
#error "HTTP_CLIENT_MAX_RECV_HEADER_LENGTH must not be larger than HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH"
#endif

/* The body is allocated with one byte more and its length returned as int32_t. */
#if (HTTP_CLIENT_MAX_RESPONSE_LENGTH) >= 0x7FFFFFFF
#error "HTTP_CLIENT_MAX_RESPONSE_LENGTH must be smaller than INT32_MAX"
#endif

/* The framing fields of a response head, from httpclient_parse_fields(). */
typedef struct HTTP_CLIENT_FRAMING_S {
    bool                    has_coding ;    /* there was a Transfer-Encoding field */
    uint32_t                codings ;       /* transfer codings in all Transfer-Encoding fields */
    bool                    chunked ;       /* the last of them is chunked */
    bool                    has_length ;    /* there was a Content-Length field */
    int32_t                 length_error ;  /* HTTP_CLIENT_E_OK, or why the Content-Length is not usable */
    uint32_t                length ;        /* the Content-Length */
} HTTP_CLIENT_FRAMING_T ;


/**
 * @brief   httpclient_init
 * @details The instance 'client' to be used in all calls from this API is initialised to zero.
 *
 * @param[in] client
 *
 * @return                          The function status.
 * @retval HTTP_CLIENT_E_OK         client was initialised
 * @retval HTTP_CLIENT_E_PARM       client is 0.
 *
 * @http
 */
int32_t
httpclient_init (HTTP_CLIENT_T* client, int32_t mss)
{
    if (!client) {
        return HTTP_CLIENT_E_PARM ;

    }
    memset (client, 0, sizeof (HTTP_CLIENT_T)) ;
    client->mss = mss ;
    client->socket = -1 ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_socket_error
 * @details The error of the socket call that just failed.
 *
 * @http
 */
static int
httpclient_socket_error (void)
{
#if defined(_WIN32)
    return WSAGetLastError () ;
#else
    return errno ;
#endif
}

/**
 * @brief   httpclient_socket_timed_out
 * @details Whether a socket error means the call ran out of time: send()
 *          after SO_SNDTIMEO, or connect() after the stack's own limit.
 *
 * @param[in] err                       From httpclient_socket_error().
 *
 * @http
 */
static bool
httpclient_socket_timed_out (int err)
{
#if defined(_WIN32)
    return (err == WSAETIMEDOUT) || (err == WSAEWOULDBLOCK) ;
#else
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
    if (err == EWOULDBLOCK) {
        return true ;

    }
#endif
    return (err == EAGAIN) || (err == ETIMEDOUT) ;
#endif
}

/**
 * @brief   httpclient_set_send_timeout
 * @details Sets SO_SNDTIMEO, so a send that makes no progress fails after
 *          'timeout' ms instead of blocking for as long as the peer does not
 *          read. Zephyr's setsockopt() takes a struct zsock_timeval, which is
 *          struct timeval, and refuses any other size; Winsock takes a DWORD
 *          in milliseconds.
 *          A stack without the option refuses it, Zephyr without
 *          CONFIG_NET_CONTEXT_SNDTIMEO for one. That is logged and the send
 *          goes ahead with only the stack's own limit, rather than every
 *          request failing on such a build.
 *          The socket keeps the option, so it is only set when it changes.
 *
 * @param[in] client
 * @param[in] timeout                   In ms.
 *
 * @http
 */
static void
httpclient_set_send_timeout (HTTP_CLIENT_T* client, uint32_t timeout)
{
#if defined(_WIN32)
    DWORD           tv = timeout ;
#else
    struct timeval  tv ;

    tv.tv_sec = timeout / 1000 ;
    tv.tv_usec = (timeout % 1000) * 1000 ;
#endif

    if (client->send_timeout == timeout) {
        return ;

    }
    client->send_timeout = timeout ;

    if (setsockopt (client->socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof (tv)) != 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                "HTTP  :W: SO_SNDTIMEO of %u ms not set (%d), a send has no timeout",
                (unsigned)timeout, httpclient_socket_error ()) ;

    }
}

/**
 * @brief   httpclient_wait_read
 * @details Waits up to 'timeout' ms until the socket can be read.
 *
 * @param[in] client
 * @param[in] timeout
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK             There is something to read, maybe the end of the stream.
 * @retval HTTP_CLIENT_E_TIMEOUT        Nothing arrived within the timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     select() reported an exception on the socket.
 * @retval HTTP_CLIENT_E_ERROR          select() failed.
 *
 * @http
 */
static int32_t
httpclient_wait_read(HTTP_CLIENT_T* client, uint32_t timeout)
{
    int32_t res ;
    fd_set   fdread;
    fd_set   fdex;
    struct timeval tv;

    FD_ZERO(&fdread) ;
    FD_SET(client->socket, &fdread);
    FD_ZERO(&fdex) ;
    FD_SET(client->socket, &fdex);
    tv.tv_sec = timeout/1000 ;
    tv.tv_usec = (timeout % 1000) * 1000 ;

    res = select(client->socket+1, &fdread, 0, &fdex, &tv) ;
    if (res < 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                        "HTTP  : : read select failed with %d (%d)", res, httpclient_socket_error ());
        return HTTP_CLIENT_E_ERROR ;

    }

    if (FD_ISSET(client->socket, &fdread)) {
        return HTTP_CLIENT_E_OK ;

    }
    if (FD_ISSET(client->socket, &fdex)) {
           DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                        "HTTP  : : read select failed with exception");
            return HTTP_CLIENT_E_CONNECTION ;

    }

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTP  : : read select timed out after %u ms", (unsigned)timeout);

    return HTTP_CLIENT_E_TIMEOUT ;
}

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
/**
 * @brief   httpclient_tls_error
 * @details Maps an mbedTLS failure to read or write on the connection. The
 *          BIO sends blocking, so a WANT_WRITE means the send timeout ran out.
 *
 * @param[in] status                    The mbedTLS error, < 0.
 *
 * @return                              HTTP_CLIENT_E_TIMEOUT, HTTP_CLIENT_E_MEMORY or
 *                                      HTTP_CLIENT_E_CONNECTION.
 *
 * @http
 */
static int32_t
httpclient_tls_error (int32_t status)
{
    if ((status == MBEDTLS_ERR_SSL_WANT_WRITE) || (status == MBEDTLS_ERR_SSL_TIMEOUT)) {
        return HTTP_CLIENT_E_TIMEOUT ;

    }
    if (status == MBEDTLS_ERR_SSL_ALLOC_FAILED) {
        return HTTP_CLIENT_E_MEMORY ;

    }

    return HTTP_CLIENT_E_CONNECTION ;
}

/**
 * @brief   httpclient_handshake_error
 * @details Maps the failure of mbedtls_ssl_handshake().
 *
 * @param[in] status                    The mbedTLS error, < 0.
 *
 * @return                              The HTTP_CLIENT_E_* code.
 *
 * @http
 */
static int32_t
httpclient_handshake_error (int32_t status)
{
    /* ssl.h only pulls in x509.h when certificate support is built in. */
#if defined(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
    if (status == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        return HTTP_CLIENT_E_SSL_TRUST ;

    }
#endif
    /* mbedTLS refused the certificate itself, its key usage or curve for
     * example, even if its chain verified. */
    if (status == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) {
        return HTTP_CLIENT_E_SSL_TRUST ;

    }
    /* Local setup errors, not the server's: verification is required but
     * no CA is set, or there is no host name to check the certificate for. */
    if (status == MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED) {
        return HTTP_CLIENT_E_PARM ;

    }
#if defined(MBEDTLS_ERR_SSL_CERTIFICATE_VERIFICATION_WITHOUT_HOSTNAME)
    if (status == MBEDTLS_ERR_SSL_CERTIFICATE_VERIFICATION_WITHOUT_HOSTNAME) {
        return HTTP_CLIENT_E_PARM ;

    }
#endif
    /* The connection failed under TLS, or ran out of time or memory. */
    if ((status == MBEDTLS_ERR_NET_CONN_RESET) || (status == MBEDTLS_ERR_NET_RECV_FAILED) ||
            (status == MBEDTLS_ERR_NET_SEND_FAILED) || (status == MBEDTLS_ERR_SSL_CONN_EOF) ||
            (status == MBEDTLS_ERR_SSL_WANT_WRITE) || (status == MBEDTLS_ERR_SSL_TIMEOUT) ||
            (status == MBEDTLS_ERR_SSL_ALLOC_FAILED)) {
        return httpclient_tls_error (status) ;

    }

    return HTTP_CLIENT_E_SSL_CONNECT ;
}
#endif

/**
 * @brief   httpclient_connect
 * @details Connect a TCP/IP socket to the address. If SSL is required, the
 *          SSL connection is also initialised and the handshaking performed.
 * @note    Call httpclient_close() after every call, also when it failed.
 *          After a failed handshake client->ssl stays allocated until then,
 *          so mbedtls_ssl_get_verify_result() can still tell why the server
 *          certificate was rejected.
 *
 * @param[in] client
 * @param[in] addr
 * @param[in] ssl_config                mbedtls_ssl_config, or 0 for plain TCP.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK             client was connected.
 * @retval HTTP_CLIENT_E_PARM           client or addr is 0, the client is connected
 *                                      already, ssl_config was given to a build without
 *                                      TLS, or the TLS setup is not usable: verification is
 *                                      required but no CA is set, or no host name was set
 *                                      to check the server certificate for.
 * @retval HTTP_CLIENT_E_ERROR          No socket could be created.
 * @retval HTTP_CLIENT_E_MEMORY         The TLS context could not be set up.
 * @retval HTTP_CLIENT_E_TIMEOUT        The TCP connect or a step of the handshake timed out.
 * @retval HTTP_CLIENT_E_CONNECTION     The TCP connect failed, or the connection failed
 *                                      during the handshake.
 * @retval HTTP_CLIENT_E_SSL_TRUST      The server certificate was rejected: it did not
 *                                      verify, or its key usage or curve is not acceptable.
 * @retval HTTP_CLIENT_E_SSL_CONNECT    The handshake failed otherwise, for example on an
 *                                      alert from the server.
 *
 * @http
 */
int32_t
httpclient_connect (HTTP_CLIENT_T* client, const struct sockaddr_in* addr, void * ssl_config)
{
    int32_t status ;
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    mbedtls_ssl_config * pssl_config = (mbedtls_ssl_config *)ssl_config ;
#endif

    if (!client || !addr) {
        return HTTP_CLIENT_E_PARM ;

    }
    DBG_CHECK_HTTP_CLIENT (client->socket == -1, HTTP_CLIENT_E_PARM,
                    "HTTP  :E: httpclient_connect unexpected\r\n!") ;
#if defined(CFG_HTTPCLIENT_TLS_DISABLE) && CFG_HTTPCLIENT_TLS_DISABLE
    /* Connecting in plain text instead would hide that TLS was asked for. */
    DBG_CHECK_HTTP_CLIENT (!ssl_config, HTTP_CLIENT_E_PARM,
                    "HTTP  :E: httpclient_connect TLS not built in\r\n!") ;
#endif

    client->socket = socket (AF_INET, SOCK_STREAM, 0) ;

    if (client->socket < 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTP  :E: failed to create socket");
        client->socket = -1 ;
        return HTTP_CLIENT_E_ERROR;
    }
    client->send_timeout = 0 ;

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (pssl_config) {
        if (!client->ssl) {
            client->ssl = HTTP_CLIENT_MALLOC(sizeof(mbedtls_ssl_context)) ;
            if (!client->ssl) {
                return HTTP_CLIENT_E_MEMORY ;

            }
            mbedtls_ssl_init((mbedtls_ssl_context *)client->ssl) ;
            status = mbedtls_ssl_setup( (mbedtls_ssl_context *)client->ssl, pssl_config) ;
            if (status != 0) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "mbedtls_ssl_setup() returned -0x%04X", -status);
                mbedtls_ssl_free ((mbedtls_ssl_context *)client->ssl) ;
                HTTP_CLIENT_FREE(client->ssl) ;
                client->ssl = 0 ;
                return (status == MBEDTLS_ERR_SSL_ALLOC_FAILED) ? HTTP_CLIENT_E_MEMORY : HTTP_CLIENT_E_PARM ;

            }
        }
        mbedtls_ssl_set_bio( (mbedtls_ssl_context *)client->ssl, (void*)(intptr_t)client->socket,
                mbedtls_net_send, mbedtls_net_recv, 0 /*mbedtls_net_recv_timeout*/ )  ;

        if (client->hostname[0]) {
            /* Without it the server certificate is not checked against the name. */
            status = mbedtls_ssl_set_hostname ((mbedtls_ssl_context *)client->ssl, client->hostname) ;
            if (status != 0) {
                return (status == MBEDTLS_ERR_SSL_ALLOC_FAILED) ? HTTP_CLIENT_E_MEMORY : HTTP_CLIENT_E_PARM ;

            }
        }

    }
#endif

    inet_ntop (AF_INET, &addr->sin_addr, client->host, MAX_IPv4_STR_LEN) ;

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : connect to %s:%d (%d) using %s",
                    client->host, (int)ntohs((uint16_t)addr->sin_port),
                    client->socket, ssl_config ? "SSL" : "NO SSL");

#if 0
    if (client->mss > 0) {
        int mss  = client->mss ;
        status = setsockopt(client->socket, SOL_SOCKET, TCP_MAXSEG, (uint8_t*)&mss, sizeof(int) );
        if (status != A_OK) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                            ("HTTP  :W: failed setting TCP_MAXSEG")) ;

        }
    }
#endif

    status = connect (client->socket, (const struct sockaddr *) addr, sizeof(struct sockaddr_in));
    if (status != 0) {
        int err = httpclient_socket_error () ;

        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTP  : : Connect to %s:%d failed %d (%d)!",
                    client->host,(int)ntohs((uint16_t)addr->sin_port), status, err);
        closesocket(client->socket);
        /* The descriptor may be reused at once, httpclient_close() must not close it again. */
        client->socket = -1 ;
        return httpclient_socket_timed_out (err) ? HTTP_CLIENT_E_TIMEOUT : HTTP_CLIENT_E_CONNECTION ;

    }
#if 0
    unsigned long flags = 1;
    status = ioctlsocket (client->socket, FIONBIO, &flags) ;
    if (status != 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTP  :W: setting FIONBIOfailed %d!", status);
        closesocket(client->socket);
        return HTTP_CLIENT_E_CONNECTION;
    }
#endif
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (client->ssl) {
        /* The handshake sends blocking; without a send timeout a server that
         * stops reading would hold it for ever. */
        httpclient_set_send_timeout (client, HTTP_CLIENT_CONNECT_TIMEOUT) ;

        do {
            status = mbedtls_ssl_handshake((mbedtls_ssl_context *)client->ssl);

            if (status == MBEDTLS_ERR_SSL_WANT_READ) {
                int32_t res = httpclient_wait_read (client, HTTP_CLIENT_CONNECT_TIMEOUT) ;

                if (res != HTTP_CLIENT_E_OK) {
                    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                                "HTTP  : : handshake read wait failed with %d", res);
                    return res ;

                }
            }

            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                "ongoing mbedtls_ssl_handshake() returned -0x%04X", -status);

        } while(status == MBEDTLS_ERR_SSL_WANT_READ ||
                status == MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS ||
                status == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS);

        if (status != 0) {
            if (status != MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "mbedtls_ssl_handshake() returned -0x%04X", -status);

            }
            return httpclient_handshake_error (status) ;

        }

    }
#endif
    return HTTP_CLIENT_E_OK ;

}

/**
 * @brief   httpclient_set_hostname
 * @details Set the hostname to be used in the http request headers, and over
 *          TLS for SNI and to check the server certificate against. If not
 *          set the IP address is used in the headers.
 * @notes   Typically used for virtual servers.
 *
 * @param[in] client
 * @param[in] hostname                  "" clears it.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_PARM           client or hostname is 0, hostname does not fit
 *                                      MAX_HOSTNAME_LEN including the terminator, or it holds
 *                                      a space or control character, which would split or
 *                                      inject header lines. The hostname is then empty.
 *
 * @http
 */
int32_t
httpclient_set_hostname (HTTP_CLIENT_T* client, const char* hostname)
{
    size_t len ;

    if (!client) {
        return HTTP_CLIENT_E_PARM ;

    }
    /* A truncated name would be checked against the server certificate. */
    client->hostname[0] = '\0' ;
    if (!hostname) {
        return HTTP_CLIENT_E_PARM ;

    }

    for (len = 0; hostname[len] && (len < MAX_HOSTNAME_LEN); len++) {
        if (((unsigned char)hostname[len] <= ' ') || (hostname[len] == 0x7F)) {
            return HTTP_CLIENT_E_PARM ;

        }
    }

    if (len >= MAX_HOSTNAME_LEN) {
        return HTTP_CLIENT_E_PARM ;

    }

    memcpy (client->hostname, hostname, len + 1) ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_set_content_type
 * @details Set the Content-Type header sent with every following POST on
 *          this client. NULL or "" restores the default "text/plain".
 *
 * @param[in] client
 * @param[in] content_type              For example "application/pkcs10".
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_PARM           client is 0, or content_type does not fit
 *                                      MAX_CONTENT_TYPE_LEN including the terminator, or
 *                                      contains CR or LF, which would inject header lines.
 *                                      The current value is kept.
 *
 * @http
 */
int32_t
httpclient_set_content_type (HTTP_CLIENT_T* client, const char* content_type)
{
    size_t len ;

    if (!client) {
        return HTTP_CLIENT_E_PARM ;

    }

    if (!content_type || !content_type[0]) {
        client->content_type[0] = '\0' ;
        return HTTP_CLIENT_E_OK ;

    }

    for (len = 0; content_type[len] && (len < MAX_CONTENT_TYPE_LEN); len++) {
        if ((content_type[len] == '\r') || (content_type[len] == '\n')) {
            return HTTP_CLIENT_E_PARM ;

        }
    }

    if (len >= MAX_CONTENT_TYPE_LEN) {
        return HTTP_CLIENT_E_PARM ;

    }

    memcpy (client->content_type, content_type, len + 1) ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_is_connected
 * @details Simply do a _t_select() with 0 timeout to see if the socket was closed..
 *
 * @param[in] client
 *
 * @return                              1 if the connection is valid, zero otherwise.
 *
 * @http
 */
int32_t
httpclient_is_connected (HTTP_CLIENT_T* client)
{
     fd_set   fdread;
    struct timeval tv;

    if (!client || (client->socket < 0)) {
        return 0 ;
    }
    FD_ZERO(&fdread) ;
    FD_SET(client->socket, &fdread);
    tv.tv_sec = 0 ;
    tv.tv_usec = 0 ;

    if (select(client->socket+1, &fdread, 0, 0, &tv) < 0) {
        return 0 ;
    }

    return 1 ;
}


/**
 * @brief   httpclient_close
 * @details Closes connection including SSL if required and frees the memory
 *          associated with 'client'. Also to be called after a failed
 *          httpclient_connect(), which can leave the SSL context without a socket.
 *
 * @param[in] client
 *
 * @http
 */
void
httpclient_close (HTTP_CLIENT_T* client)
{
    if (!client) {
        return ;

    }

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : closesocket sock 0x%x", client->socket) ;

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (client->ssl) {
        if (client->socket>=0) {
            mbedtls_ssl_close_notify ((mbedtls_ssl_context *)client->ssl) ;

        }
        mbedtls_ssl_free ((mbedtls_ssl_context *)client->ssl) ;
        HTTP_CLIENT_FREE(client->ssl) ;
        client->ssl = 0 ;

    }
#endif
    if (client->socket>=0) {
        shutdown (client->socket, /*SHUT_RDWR*/2) ;
        closesocket (client->socket) ;
        client->socket = -1 ;

    }
    if (client->read_buffer) {
        HTTP_CLIENT_FREE(client->read_buffer) ;
        client->read_buffer = 0 ;
    }
    client->read_start = 0 ;
    client->read_end = 0 ;
    client->body_state = HTTP_CLIENT_BODY_NONE ;

}


/**
 * @brief httpclient_write
 * @details Sends all of buffer, over TLS when the connection uses it. Each
 *          send may wait up to 'timeout' ms for progress.
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length
 * @param[in] timeout
 *
 * @return                              Number of bytes sent, or < 0:
 * @retval HTTP_CLIENT_E_TIMEOUT        A send made no progress within the timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     The connection failed.
 * @retval HTTP_CLIENT_E_MEMORY         mbedTLS ran out of memory.
 *
 * @http
 */
static int32_t
httpclient_write (HTTP_CLIENT_T* client, const uint8_t* buffer, uint32_t length, uint32_t timeout)
{
    int32_t sent_bytes = 0;
    uint32_t total = 0 ;

    httpclient_set_send_timeout (client, timeout) ;

    while (total < length) {
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
        if (client->ssl) {
            sent_bytes = mbedtls_ssl_write ((mbedtls_ssl_context *)client->ssl, &buffer[total], length - total) ;
            if (sent_bytes < 0) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "httpclient_write : ERROR: socket 0x%x mbedtls_ssl_write -0x%x",
                        client->socket, -sent_bytes);
                return httpclient_tls_error (sent_bytes) ;

            }
        } else
#endif
        {
            sent_bytes = send (client->socket, (const char *)&buffer[total], length - total, 0);
            if (sent_bytes < 0) {
                int err = httpclient_socket_error () ;

                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "httpclient_write : ERROR: socket 0x%x send failed (%d)", client->socket, err);
                return httpclient_socket_timed_out (err) ? HTTP_CLIENT_E_TIMEOUT : HTTP_CLIENT_E_CONNECTION ;

            }
        }

        if (sent_bytes == 0) {
            /* Neither returns 0 for a non-empty buffer, so nothing will move. */
            return HTTP_CLIENT_E_CONNECTION ;

        }
        total += (uint32_t)sent_bytes ;

    }

    return (int32_t)total ;
}

/**
 * @brief httpclient_write_chunked
 * @details Sends buffer as one chunk: size line, data and CRLF. A length of
 *          0 sends the last chunk.
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length
 * @param[in] timeout
 *
 * @return                              Number of bytes sent with the framing, or < 0, see
 *                                      httpclient_write().
 *
 * @http
 */
static int32_t
httpclient_write_chunked (HTTP_CLIENT_T* client, const uint8_t* buffer, uint32_t length, uint32_t timeout)
{
    char chunkhead[16] ;
    int32_t total ;
    int32_t res ;
    int i ;

    i = snprintf (chunkhead, sizeof(chunkhead), "%x\r\n", (unsigned int)length) ;
    total = httpclient_write (client, (const uint8_t*)chunkhead, (uint32_t)i, timeout) ;
    if (total < 0) {
        return total ;

    }
    res = httpclient_write (client, buffer, length, timeout) ;
    if (res < 0) {
        return res ;

    }
    total += res ;
    /* Without its CRLF the chunk, and for the last chunk the body, is not
     * complete, so the server would wait for it instead of answering. */
    res = httpclient_write (client, (const uint8_t*)"\r\n", 2, timeout) ;
    if (res < 0) {
        return res ;

    }

    return total + res ;
}

/**
 * @brief httpclient_stream_write
 * @details The HTTP_STREAM_WRITE given to the stream of httpclient_post_stream():
 *          ctx is the client, and the data goes through httpclient_write()
 *          with the send timeout of the post.
 *
 * @http
 */
static int32_t
httpclient_stream_write (uintptr_t ctx, const void* data, uint32_t len)
{
    HTTP_CLIENT_T* client = (HTTP_CLIENT_T*)ctx ;

    return httpclient_write (client, (const uint8_t*)data, len, client->send_timeout) ;
}

/**
 * @brief httpclient_write_stream
 * @details Has the stream send the len bytes of the body, as many calls as
 *          it takes.
 *
 * @param[in] client
 * @param[in] stream
 * @param[in] parm                      Passed to stream.
 * @param[in] len
 * @param[in] timeout
 *
 * @return                              Number of bytes sent, or < 0: the HTTP_CLIENT_E_*
 *                                      code from httpclient_write() that the stream
 *                                      returned, else HTTP_CLIENT_E_ERROR.
 *
 * @http
 */
static int32_t
httpclient_write_stream (HTTP_CLIENT_T* client, HTTP_STREAM_NEXT_T stream, uint32_t parm, int32_t len, uint32_t timeout)
{
    int32_t sent_bytes = 0;
    int32_t total = 0 ;

    httpclient_set_send_timeout (client, timeout) ;

    while (total < len) {
        sent_bytes = stream (httpclient_stream_write, (uintptr_t)client, parm, (uint32_t)(len - total)) ;

        if (sent_bytes <= 0) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "httpclient_write_stream : ERROR: socket 0x%x sent_bytes %d",
                    client->socket, sent_bytes);
            return ((sent_bytes < 0) && (sent_bytes >= HTTP_CLIENT_E_LOWEST)) ?
                    sent_bytes : HTTP_CLIENT_E_ERROR ;

        }
        total += sent_bytes ;

    }

    if (total > len) {
        /* The server takes the rest for the start of another request. */
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                "httpclient_write_stream : ERROR: %d bytes sent for a body of %d",
                total, len);
        return HTTP_CLIENT_E_ERROR ;

    }

    return total ;
}

/**
 * @brief httpclient_read
 * @details Reads what the connection has, waiting up to 'timeout' ms for the
 *          first byte. This is where a close by the server is judged: only
 *          the end of the stream on a plain socket, or a TLS close_notify, is
 *          a clean close. Over TLS a TCP close without close_notify may have
 *          cut the response short (RFC 9112, section 9.8), so it is
 *          HTTP_CLIENT_E_CONNECTION. mbedtls_ssl_read() reports that close as
 *          0 when the BIO gives 0 at the end of the stream, and qoraal's BIO
 *          gives MBEDTLS_ERR_NET_CONN_RESET instead; both are an error here.
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length                    More than 0.
 * @param[in] timeout
 *
 * @return                              Number of bytes read, 0 for a clean close, or < 0:
 * @retval HTTP_CLIENT_E_TIMEOUT        No data within the timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     The connection was reset or closed without
 *                                      close_notify, or failed otherwise.
 * @retval HTTP_CLIENT_E_MEMORY         mbedTLS ran out of memory.
 * @retval HTTP_CLIENT_E_ERROR          select() failed.
 *
 * @http
 */
static int32_t
httpclient_read (HTTP_CLIENT_T* client, void* buffer, uint32_t length, uint32_t timeout)
{
    int32_t received = HTTP_CLIENT_E_OK ;

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (client->ssl) {
        if (!mbedtls_ssl_get_bytes_avail ((mbedtls_ssl_context *)client->ssl)) {
            received = httpclient_wait_read (client, timeout) ;
        }
        while (received == HTTP_CLIENT_E_OK) {
            received = mbedtls_ssl_read ((mbedtls_ssl_context *)client->ssl, buffer, length) ;
            if (received == MBEDTLS_ERR_SSL_WANT_READ) {
                received = httpclient_wait_read (client, timeout) ;
            } else if (received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                return 0 ;
            } else if (received == 0) {
                received = HTTP_CLIENT_E_CONNECTION ;
            } else if (received < 0) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                        "HTTP  : : mbedtls_ssl_read -0x%x", -received);
                received = httpclient_tls_error (received) ;
            }

        }
    } else
#endif
    {
        received = httpclient_wait_read (client, timeout) ;
        if (received == HTTP_CLIENT_E_OK) {
            received = recv (client->socket, buffer, length, 0);
            if (received < 0) {
                int err = httpclient_socket_error () ;

                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                        "HTTP  : : recv failed (%d)", err);
                received = httpclient_socket_timed_out (err) ? HTTP_CLIENT_E_TIMEOUT : HTTP_CLIENT_E_CONNECTION ;
            }

        }

    }

    if(received < 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                "HTTP  : : httpclient_read  %d", received);

    }

    return received ;
}

/**
 * @brief   httpclient_head_add
 * @details Appends to the request head in buffer. Once something did not
 *          fit, *pos is size and every later call does nothing, so the
 *          caller checks once at the end.
 *
 * @param[in] buffer
 * @param[in] size                      Of buffer.
 * @param[in,out] pos                   Bytes in buffer.
 * @param[in] fmt                       printf format.
 *
 * @http
 */
static void
httpclient_head_add (char* buffer, uint32_t size, uint32_t* pos, const char* fmt, ...)
{
    va_list     args ;
    int         n ;

    if (*pos >= size) {
        return ;

    }

    va_start (args, fmt) ;
    n = vsnprintf (&buffer[*pos], size - *pos, fmt, args) ;
    va_end (args) ;

    if ((n < 0) || ((uint32_t)n >= size - *pos)) {
        *pos = size ;

    } else {
        *pos += (uint32_t)n ;

    }
}

/**
 * @brief   httpclient_head_add_basic
 * @details Appends an Authorization: Basic field for "username:password".
 *          base64_encode() writes exactly BASE64_OUTPUT_LENGTH() characters
 *          and no terminator, so it encodes straight into the head.
 *
 * @param[in] buffer
 * @param[in] size                      Of buffer.
 * @param[in,out] pos                   Bytes in buffer, size when it did not fit.
 * @param[in] credentials               username:password, not empty.
 *
 * @http
 */
static void
httpclient_head_add_basic (char* buffer, uint32_t size, uint32_t* pos, const char* credentials)
{
    size_t  len = strlen (credentials) ;
    size_t  enc_len ;

    /* Checked before BASE64_OUTPUT_LENGTH() so a huge length cannot wrap. */
    if (len >= size) {
        *pos = size ;
        return ;

    }
    enc_len = BASE64_OUTPUT_LENGTH(len) ;

    httpclient_head_add (buffer, size, pos, "Authorization: Basic ") ;
    /* The CRLF and the terminator of httpclient_head_add() must fit after it. */
    if (*pos + enc_len + 2 >= size) {
        *pos = size ;
        return ;

    }
    base64_encode (credentials, len, &buffer[*pos], enc_len) ;
    *pos += (uint32_t)enc_len ;
    httpclient_head_add (buffer, size, pos, "\r\n") ;
}

/**
 * @brief   httpclient_send_head
 * @details Builds and sends the head of every request this client makes: the
 *          request line, then Host (the name from httpclient_set_hostname(),
 *          else the address), Accept and Connection: keep-alive, User-Agent,
 *          Authorization: Basic when there are credentials, and the body
 *          framing: Content-Type with Content-Length or with chunked transfer
 *          coding. A websocket upgrade sends its key, Upgrade, Connection:
 *          Upgrade and its protocol fields instead of Accept and keep-alive.
 *
 * @param[in] client
 * @param[in] method                    "GET" or "POST".
 * @param[in] endpoint                  Path without its leading '/', 0 for "/".
 * @param[in] credentials               username:password, or 0 or "" for none.
 * @param[in] framing                   HTTP_CLIENT_REQ_*.
 * @param[in] length                    The Content-Length for HTTP_CLIENT_REQ_LENGTH.
 * @param[in] timeout                   For each send.
 *
 * @return                              Number of bytes sent, or < 0:
 * @retval HTTP_CLIENT_E_PARM           Not connected, or the endpoint holds a space or
 *                                      control character, which would end the request
 *                                      line early or inject header lines.
 * @retval HTTP_CLIENT_E_CONNECTION     The socket is closed, or the send failed.
 * @retval HTTP_CLIENT_E_TIMEOUT        The send made no progress within the timeout.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate the request buffer.
 * @retval HTTP_CLIENT_E_LENGTH         The head does not fit HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH.
 *
 * @http
 */
static int32_t
httpclient_send_head (HTTP_CLIENT_T* client, const char* method, const char* endpoint,
                      const char* credentials, uint32_t framing, int32_t length, uint32_t timeout)
{
    const uint32_t  size = HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH ;
    const char*     host = client->hostname[0] ? client->hostname : client->host ;
    const char*     p ;
    char*           buffer ;
    uint32_t        pos = 0 ;
    int32_t         res ;

    DBG_CHECK_HTTP_CLIENT (client->socket>=0, HTTP_CLIENT_E_PARM,
                    "HTTP  :E: unexpected - socket not valid!") ;

    if (!endpoint) {
        endpoint = "" ;

    }
    for (p = endpoint; *p; p++) {
        if (((unsigned char)*p <= ' ') || (*p == 0x7F)) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: endpoint holds a space or control character!");
            return HTTP_CLIENT_E_PARM ;

        }
    }

    if(!httpclient_is_connected(client)) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTP  : : socket closed!");
        return HTTP_CLIENT_E_CONNECTION ;

    }

    buffer = HTTP_CLIENT_MALLOC(size);
    if(buffer == NULL) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: Memory allocation failed");
        return HTTP_CLIENT_E_MEMORY ;

    }

    httpclient_head_add (buffer, size, &pos, "%s /%s HTTP/1.1\r\nHost: %s\r\n", method, endpoint, host) ;
    if ((framing == HTTP_CLIENT_REQ_LENGTH) || (framing == HTTP_CLIENT_REQ_CHUNKED)) {
        httpclient_head_add (buffer, size, &pos, "Content-Type: %s\r\n",
                client->content_type[0] ? client->content_type : HTTP_POST_DEFAULT_CONTENT_TYPE) ;

    }
#if HTTP_WEBSOCK
    if (framing == HTTP_CLIENT_REQ_WEBSOCK) {
        char key[BASE64_OUTPUT_LENGTH(sizeof (client->key)) + 1] ;

        base64_encode ((const char*)client->key, sizeof (client->key), key, BASE64_OUTPUT_LENGTH(sizeof (client->key))) ;
        key[BASE64_OUTPUT_LENGTH(sizeof (client->key))] = '\0' ;
        httpclient_head_add (buffer, size, &pos,
                "Sec-WebSocket-Key: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n", key) ;

    } else
#endif
    {
        httpclient_head_add (buffer, size, &pos, "Accept: */*\r\nConnection: keep-alive\r\n") ;

    }
    httpclient_head_add (buffer, size, &pos, "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n") ;
#if HTTP_WEBSOCK
    if (framing == HTTP_CLIENT_REQ_WEBSOCK) {
        httpclient_head_add (buffer, size, &pos,
                "Sec-WebSocket-Protocol: " HTTP_CLIENT_WEBSOCKET_PROTOCOL "\r\n"
                "Sec-WebSocket-Version: 13\r\n") ;

    }
#endif
    /* An empty string is no credentials: "Authorization: Basic " with an
     * empty value is not a valid header. */
    if (credentials && credentials[0]) {
        httpclient_head_add_basic (buffer, size, &pos, credentials) ;

    }
    if (framing == HTTP_CLIENT_REQ_LENGTH) {
        httpclient_head_add (buffer, size, &pos, "Content-Length: %d\r\n", (int)length) ;

    } else if (framing == HTTP_CLIENT_REQ_CHUNKED) {
        httpclient_head_add (buffer, size, &pos, "Transfer-Encoding: chunked\r\n") ;

    }
    httpclient_head_add (buffer, size, &pos, "\r\n") ;

    if (pos >= size) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH to small for HTTP headers!");
        res = HTTP_CLIENT_E_LENGTH ;

    } else {
        res = httpclient_write (client, (const uint8_t*)buffer, pos, timeout) ;

    }
    HTTP_CLIENT_FREE (buffer) ;

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : send %d bytes", res);

    return res ;
}

/**
 * @brief   httpclient_get
 * @details Starts a HTTP GET request. The response should be read with httpclient_read_response.
 * @note    The request head is limited to HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *
 * @param[in] client
 * @param[in] endpoint                  Path without its leading '/'.
 * @param[in] credentials               username:password, or 0 or "" for none
 *
 * @return                              The number of bytes sent or if < 0 it indicates an
 *                                      error, see httpclient_send_head().
 *
 * @http
 */
int32_t
httpclient_get (HTTP_CLIENT_T* client, const char* endpoint, const char *credentials)
{
    if (!client) {
        return HTTP_CLIENT_E_PARM ;

    }

    return httpclient_send_head (client, "GET", endpoint, credentials,
                    HTTP_CLIENT_REQ_NO_BODY, 0, HTTP_CLIENT_SEND_TIMEOUT) ;
}

/**
 * @brief   httpclient_post_start
 * @details Sends the head of a POST request with a body of len bytes,
 *          framed by Content-Length or by chunked transfer coding.
 *
 * @param[in] client
 * @param[in] endpoint
 * @param[in] len                       Body length, 0 or more.
 * @param[in] credentials
 * @param[in] framing                   HTTP_CLIENT_REQ_LENGTH or HTTP_CLIENT_REQ_CHUNKED.
 *
 * @return                              The number of bytes sent or < 0, see
 *                                      httpclient_send_head().
 *
 * @http
 */
static int32_t
httpclient_post_start (HTTP_CLIENT_T* client, const char* endpoint, int32_t len, const char *credentials, uint32_t framing)
{
    if (!client || (len < 0)) {
        return HTTP_CLIENT_E_PARM ;

    }

    return httpclient_send_head (client, "POST", endpoint, credentials,
                    framing, len, HTTP_CLIENT_SEND_TIMEOUT) ;
}


/**
 * @brief   httpclient_post
 * @details Sends a HTTP POST request with a Content-Length. The response should be read with httpclient_read_response().
 * @note    The request head is limited to HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *
 * @param[in] client
 * @param[in] endpoint                  Path without its leading '/'.
 * @param[in] request                   The body.
 * @param[in] len                       Body length.
 * @param[in] credentials               username:password, or 0 or "" for none
 *
 * @return                              The number of bytes sent, head and body, or < 0:
 *                                      see httpclient_send_head(); HTTP_CLIENT_E_PARM also
 *                                      for a negative len.
 *
 * @http
 */
int32_t
httpclient_post (HTTP_CLIENT_T* client, const char* endpoint, const char* request, int32_t len, const char *credentials)
{
    int32_t head ;
    int32_t res ;

    if (!request && (len > 0)) {
        return HTTP_CLIENT_E_PARM ;

    }

    head = httpclient_post_start (client, endpoint, len, credentials, HTTP_CLIENT_REQ_LENGTH) ;
    if (head < 0) {
        return head ;

    }
    res = httpclient_write (client, (const uint8_t*)request, (uint32_t)len, HTTP_CLIENT_SEND_TIMEOUT) ;

    return (res < 0) ? res : head + res ;
}

/**
 * @brief   httpclient_post_chunked
 * @details Sends a HTTP POST request with chunked transfer coding: the
 *          request as one chunk, then the last chunk. The response should be
 *          read with httpclient_read_response().
 * @note    The request head is limited to HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *
 * @param[in] client
 * @param[in] endpoint                  Path without its leading '/'.
 * @param[in] request                   The body.
 * @param[in] len                       Body length.
 * @param[in] credentials               username:password, or 0 or "" for none
 *
 * @return                              The number of bytes sent, head and framing included,
 *                                      or < 0, see httpclient_post().
 *
 * @http
 */
int32_t
httpclient_post_chunked (HTTP_CLIENT_T* client, const char* endpoint, const char* request, int32_t len, const char *credentials)
{
    int32_t total ;
    int32_t res ;

    if (!request && (len > 0)) {
        return HTTP_CLIENT_E_PARM ;

    }

    total = httpclient_post_start (client, endpoint, len, credentials, HTTP_CLIENT_REQ_CHUNKED) ;
    if (total < 0) {
        return total ;

    }
    if (len > 0) {
        res = httpclient_write_chunked (client, (const uint8_t*)request, (uint32_t)len, HTTP_CLIENT_SEND_TIMEOUT) ;
        if (res < 0) {
            return res ;

        }
        total += res ;

    }
    res = httpclient_write_chunked (client, 0, 0, HTTP_CLIENT_SEND_TIMEOUT) ;

    return (res < 0) ? res : total + res ;
}


/**
 * @brief   httpclient_post_stream
 * @details Sends a HTTP POST request with a Content-Length of len, its body
 *          sent by stream. The response should be read with httpclient_read_response().
 * @note    The request head is limited to HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *
 * @param[in] client
 * @param[in] endpoint                  Path without its leading '/'.
 * @param[in] stream                    Sends the body, see HTTP_STREAM_NEXT_T.
 * @param[in] parm                      Passed to stream.
 * @param[in] len                       Body length.
 * @param[in] credentials               username:password, or 0 or "" for none
 *
 * @return                              The number of bytes sent, head and body, or < 0, see
 *                                      httpclient_post() and httpclient_write_stream().
 *
 * @http
 */
int32_t
httpclient_post_stream (HTTP_CLIENT_T* client, const char* endpoint, HTTP_STREAM_NEXT_T stream, uint32_t parm, int32_t len, const char *credentials)
{
    int32_t head ;
    int32_t res ;

    if (!stream && (len > 0)) {
        return HTTP_CLIENT_E_PARM ;

    }

    head = httpclient_post_start (client, endpoint, len, credentials, HTTP_CLIENT_REQ_LENGTH) ;
    if (head < 0) {
        return head ;

    }
    res = httpclient_write_stream (client, stream, parm, len, HTTP_CLIENT_SEND_TIMEOUT) ;

    return (res < 0) ? res : head + res ;
}

/**
 * @brief   httpclient_free_response
 * @details Frees a response from httpclient_read_response(). The client is
 *          not used, so it may be 0, for example when the body outlives it.
 *
 * @param[in] client                    Not used, may be 0.
 * @param[in] response                  May be 0.
 *
 *
 * @http
 */
void
httpclient_free_response (HTTP_CLIENT_T* client, void* response)
{
    (void) client ;

    if (response) {
        HTTP_CLIENT_FREE (response) ;

    }
}

/**
 * @brief   httpclient_parse_decimal
 * @details Parses a field value that is 1*DIGIT, as Content-Length and the
 *          delay-seconds form of Retry-After are. Unlike sscanf("%u"), it
 *          refuses a sign, whitespace and anything after the digits; the
 *          header parser removed the whitespace around the value already. A
 *          value above max is refused at the digit that takes it there,
 *          instead of wrapping or saturating.
 *
 * @param[in] value                     NUL-terminated field value.
 * @param[in] max                       Largest value accepted.
 * @param[out] number                   Set only on success.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_CONTENT        Not 1*DIGIT.
 * @retval HTTP_CLIENT_E_LENGTH         Above max.
 *
 * @http
 */
static int32_t
httpclient_parse_decimal (const char* value, uint32_t max, uint32_t* number)
{
    uint32_t n = 0 ;

    if ((*value < '0') || (*value > '9')) {
        return HTTP_CLIENT_E_CONTENT ;

    }

    for ( ; (*value >= '0') && (*value <= '9'); value++) {
        uint32_t digit = (uint32_t)(*value - '0') ;

        if (n > (max - digit) / 10) {
            return HTTP_CLIENT_E_LENGTH ;

        }
        n = n * 10 + digit ;

    }

    if (*value != '\0') {
        return HTTP_CLIENT_E_CONTENT ;

    }

    *number = n ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_parse_retry_after
 * @details Parses the delay-seconds form of a Retry-After header value
 *          (RFC 9110, section 10.2.3). The HTTP-date form would need a wall
 *          clock, so it gives 0, the same as an absent or malformed header.
 *          A delay too large for uint32_t gives UINT32_MAX.
 *
 * @param[in] value                     Header value from httpclient_parse_fields(), or 0.
 *
 * @return                              Delay in seconds, 0 if not usable.
 *
 * @http
 */
static uint32_t
httpclient_parse_retry_after (const char* value)
{
    uint32_t seconds = 0 ;
    int32_t res ;

    if (!value) {
        return 0 ;

    }

    res = httpclient_parse_decimal (value, UINT32_MAX, &seconds) ;
    if (res == HTTP_CLIENT_E_LENGTH) {
        return UINT32_MAX ;

    }

    return (res == HTTP_CLIENT_E_OK) ? seconds : 0 ;
}

/**
 * @brief   httpclient_parse_chunk_size
 * @details Parses a chunk size line (RFC 9112, section 7.1) without its line
 *          end: hex digits, then optionally spaces or tabs and a chunk
 *          extension, which is skipped as this client uses none. Unlike
 *          sscanf("%x"), it never reads past end, since the receive buffer
 *          is not NUL-terminated there, and it skips no leading whitespace,
 *          so a mis-framed body is not read as a size. A size above
 *          INT32_MAX is refused at the digit that takes it there, instead of
 *          wrapping or saturating.
 *
 * @param[in] line                      First byte of the size line.
 * @param[in] end                       Its line end, the CR or the LF.
 * @param[out] length                   The chunk size, set only on success.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_CONTENT        Not a valid chunk size line.
 * @retval HTTP_CLIENT_E_LENGTH         The chunk size is above INT32_MAX.
 *
 * @http
 */
static int32_t
httpclient_parse_chunk_size (const char* line, const char* end, uint32_t* length)
{
    const char* q = line ;
    uint32_t    n = 0 ;

    for ( ; q < end; q++) {
        uint32_t digit ;

        if ((*q >= '0') && (*q <= '9')) {
            digit = (uint32_t)(*q - '0') ;

        } else if ((*q >= 'a') && (*q <= 'f')) {
            digit = (uint32_t)(*q - 'a') + 10 ;

        } else if ((*q >= 'A') && (*q <= 'F')) {
            digit = (uint32_t)(*q - 'A') + 10 ;

        } else {
            break ;

        }

        if (n > ((uint32_t)INT32_MAX - digit) / 16) {
            return HTTP_CLIENT_E_LENGTH ;

        }
        n = n * 16 + digit ;

    }

    if (q == line) {
        return HTTP_CLIENT_E_CONTENT ;

    }

    while ((q < end) && ((*q == ' ') || (*q == '\t'))) {
        q++ ;

    }
    if ((q < end) && (*q != ';')) {
        return HTTP_CLIENT_E_CONTENT ;

    }

    *length = n ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_parse_transfer_coding
 * @details Adds the codings of one Transfer-Encoding field value, a comma
 *          separated list (RFC 9110, section 5.6.1), to those of the fields
 *          before it, as repeated fields make one list. Case and the
 *          whitespace around a coding are ignored, and so are empty list
 *          elements.
 *
 * @param[in] value                     NUL-terminated field value.
 * @param[in,out] framing               codings counts the codings, chunked is whether
 *                                      the last one is chunked.
 *
 * @http
 */
static void
httpclient_parse_transfer_coding (const char* value, HTTP_CLIENT_FRAMING_T* framing)
{
    const char* start ;
    const char* end ;

    framing->has_coding = true ;

    for (;;) {
        while ((*value == ' ') || (*value == '\t') || (*value == ',')) {
            value++ ;

        }
        if (*value == '\0') {
            break ;

        }

        start = value ;
        while (*value && (*value != ',')) {
            value++ ;

        }
        end = value ;
        while ((end[-1] == ' ') || (end[-1] == '\t')) {
            end-- ;

        }

        framing->codings++ ;
        framing->chunked = ((end - start) == 7) && (strnicmp (start, "chunked", 7) == 0) ;

    }
}

/**
 * @brief   httpclient_parse_status_line
 * @details Parses "HTTP/1.x SP 3DIGIT SP reason" (RFC 9112, section 4) up to
 *          the CR that ends it. The reason phrase is ignored, so it may also
 *          be missing with the space before it. Any minor version of HTTP/1
 *          is read as the one this client speaks (RFC 9112, section 2.3).
 *
 * @param[in] line                      Ends with a CR.
 *
 * @return                              The status code, 100 to 599, or 0 when the line
 *                                      is not a valid status line.
 *
 * @http
 */
static int32_t
httpclient_parse_status_line (const char* line)
{
    int32_t status ;

    if ((strncmp (line, "HTTP/1.", 7) != 0) || (line[7] < '0') || (line[7] > '9') ||
            (line[8] != ' ')) {
        return 0 ;

    }
    line += 9 ;
    if ((line[0] < '0') || (line[0] > '9') || (line[1] < '0') || (line[1] > '9') ||
            (line[2] < '0') || (line[2] > '9') || ((line[3] != ' ') && (line[3] != '\r'))) {
        return 0 ;

    }

    status = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0') ;

    return ((status >= 100) && (status <= 599)) ? status : 0 ;
}

/**
 * @brief   httpclient_field_is
 * @details Whether the field name in [name, name + len) is key, ignoring case.
 *
 * @http
 */
static bool
httpclient_field_is (const char* name, size_t len, const char* key)
{
    return (strlen (key) == len) && (strnicmp (name, key, len) == 0) ;
}

/**
 * @brief   httpclient_parse_fields
 * @details Splits the header fields of a complete head into names and values
 *          in place: each value is NUL-terminated, without the whitespace
 *          around it. An obsolete line folding is replaced with spaces first,
 *          as RFC 9112, section 5.2 asks of a user agent. headers keeps the
 *          first field of each of its names. Transfer-Encoding and
 *          Content-Length go to framing instead, every one of them, as a
 *          repeated field changes the framing.
 *
 * @param[in] fields                    The line after the status line.
 * @param[in] end                       The CR of the empty line that ends the head.
 * @param[in,out] headers               The names to find; each value is 0 or its field.
 * @param[in] count                     Elements in headers.
 * @param[in,out] framing               Cleared by the caller.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_HEADER         A line is not a field, or whitespace precedes its
 *                                      colon (RFC 9112, section 5.1) or the first field.
 *
 * @http
 */
static int32_t
httpclient_parse_fields (char* fields, char* end, HTTP_HEADER_T* headers, int count,
                         HTTP_CLIENT_FRAMING_T* framing)
{
    char*   p ;
    char*   line ;
    int     i ;

    /* httpclient_find_head_end() made sure every CR is followed by LF. */
    for (p = fields; p + 2 < end; p++) {
        if ((p[0] == '\r') && ((p[2] == ' ') || (p[2] == '\t'))) {
            p[0] = ' ' ;
            p[1] = ' ' ;

        }
    }

    for (line = fields; line < end; ) {
        char*   eol = line ;
        char*   colon ;
        char*   value ;
        char*   value_end ;
        size_t  name_len ;

        while (*eol != '\r') {
            eol++ ;

        }
        for (colon = line; (colon < eol) && (*colon != ':'); colon++) {
            if ((*colon == ' ') || (*colon == '\t')) {
                return HTTP_CLIENT_E_HEADER ;

            }
        }
        if ((colon == line) || (colon == eol)) {
            return HTTP_CLIENT_E_HEADER ;

        }
        name_len = (size_t)(colon - line) ;

        value = colon + 1 ;
        while ((value < eol) && ((*value == ' ') || (*value == '\t'))) {
            value++ ;

        }
        value_end = eol ;
        while ((value_end > value) && ((value_end[-1] == ' ') || (value_end[-1] == '\t'))) {
            value_end-- ;

        }
        *value_end = '\0' ;

        if (httpclient_field_is (line, name_len, HTTP_HEADER_KEY_TRANSFER_ENCODING)) {
            httpclient_parse_transfer_coding (value, framing) ;

        } else if (httpclient_field_is (line, name_len, HTTP_HEADER_KEY_CONTENT_LENGTH)) {
            uint32_t length = 0 ;
            int32_t res = httpclient_parse_decimal (value, INT32_MAX, &length) ;

            if (framing->length_error == HTTP_CLIENT_E_OK) {
                if (res != HTTP_CLIENT_E_OK) {
                    framing->length_error = res ;

                } else if (framing->has_length && (framing->length != length)) {
                    /* RFC 9110, section 8.6: differing values make the framing unknown. */
                    framing->length_error = HTTP_CLIENT_E_CONTENT ;

                }
            }
            framing->has_length = true ;
            framing->length = length ;

        } else {
            for (i = 0; i < count; i++) {
                if (headers[i].key && !headers[i].value &&
                        httpclient_field_is (line, name_len, headers[i].key)) {
                    headers[i].value = value ;

                }
            }

        }

        line = eol + 2 ;

    }

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_find_head_end
 * @details Looks for the empty line that ends a response head in the first
 *          len bytes, going on from *scanned, which moves past what was
 *          checked. A NUL, or a CR or LF that is not a CRLF, makes the head
 *          malformed (RFC 9110, section 5.5).
 *
 * @param[in] head
 * @param[in] len                       Bytes of head received, up to the limit.
 * @param[in,out] scanned               Bytes checked already.
 *
 * @return                              The length of the head with its empty line, 0 while
 *                                      it is not complete, or HTTP_CLIENT_E_HEADER.
 *
 * @http
 */
static int32_t
httpclient_find_head_end (const char* head, uint32_t len, uint32_t* scanned)
{
    uint32_t i ;

    for (i = *scanned; i < len; i++) {
        if (head[i] == '\0') {
            return HTTP_CLIENT_E_HEADER ;

        }
        if (head[i] == '\r') {
            if (i + 1 >= len) {
                /* Its LF has not arrived yet. */
                break ;

            }
            if (head[i + 1] != '\n') {
                return HTTP_CLIENT_E_HEADER ;

            }

        } else if (head[i] == '\n') {
            if ((i == 0) || (head[i - 1] != '\r')) {
                return HTTP_CLIENT_E_HEADER ;

            }
            /* The CR before i - 2 was checked when i - 2 was. */
            if ((i >= 3) && (head[i - 2] == '\n')) {
                *scanned = i + 1 ;
                return (int32_t)(i + 1) ;

            }

        }
    }

    *scanned = i ;

    return 0 ;
}

/**
 * @brief   httpclient_read_more
 * @details Reads more of the response after the bytes not yet used in
 *          read_buffer, first moving those to the start of the buffer when
 *          they reach its end.
 *
 * @param[in] client
 * @param[in] max                       Read no more than this, 0 for what fits.
 * @param[in] timeout
 *
 * @return                              Bytes added, 0 for a clean close, or < 0:
 *                                      HTTP_CLIENT_E_LENGTH when the unused bytes fill
 *                                      the buffer, or the error of httpclient_read().
 *
 * @http
 */
static int32_t
httpclient_read_more (HTTP_CLIENT_T* client, uint32_t max, uint32_t timeout)
{
    uint32_t    room ;
    int32_t     received ;

    if (client->read_start == client->read_end) {
        client->read_start = 0 ;
        client->read_end = 0 ;

    } else if (client->read_end >= HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH) {
        if (client->read_start == 0) {
            return HTTP_CLIENT_E_LENGTH ;

        }
        memmove (client->read_buffer, &client->read_buffer[client->read_start],
                client->read_end - client->read_start) ;
        client->read_end -= client->read_start ;
        client->read_start = 0 ;

    }

    room = HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH - client->read_end ;
    if (max && (max < room)) {
        room = max ;

    }

    received = httpclient_read (client, &client->read_buffer[client->read_end], room, timeout) ;
    if (received > 0) {
        client->total_read += received ;
        client->read_end += (uint32_t)received ;

    }

    return received ;
}

/**
 * @brief   httpclient_read_head
 * @details Reads the head of the final response into a new read_buffer: the
 *          status line and the header fields up to the empty line. Interim
 *          1xx responses (RFC 9110, section 15.2) are read and dropped, head
 *          and all. 101 is final here, since after it the connection no
 *          longer speaks HTTP. The heads together, interim ones included,
 *          may take up to HTTP_CLIENT_MAX_RECV_HEADER_LENGTH bytes.
 *          Afterwards read_start is at the first byte of the body, which may
 *          be in the buffer already. On an error the buffer is freed again.
 *
 * @param[in] client
 * @param[in] timeout                   For each read.
 * @param[out] status                   The final status, 0 until a valid one was read.
 * @param[in,out] headers               The fields to find, see httpclient_parse_fields().
 * @param[in] count                     Elements in headers.
 * @param[out] framing                  Transfer-Encoding and Content-Length.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate the receive buffer.
 * @retval HTTP_CLIENT_E_CONNECTION     The connection closed or failed before the head
 *                                      was complete, also before any response.
 * @retval HTTP_CLIENT_E_TIMEOUT        No data within the timeout.
 * @retval HTTP_CLIENT_E_HEADER         Not a status line, or a malformed field.
 * @retval HTTP_CLIENT_E_LENGTH         The heads take more than
 *                                      HTTP_CLIENT_MAX_RECV_HEADER_LENGTH bytes.
 * @retval HTTP_CLIENT_E_ERROR          select() failed.
 *
 * @http
 */
static int32_t
httpclient_read_head (HTTP_CLIENT_T* client, uint32_t timeout, int32_t* status,
                      HTTP_HEADER_T* headers, int count, HTTP_CLIENT_FRAMING_T* framing)
{
    uint32_t    limit = HTTP_CLIENT_MAX_RECV_HEADER_LENGTH ;
    uint32_t    scanned = 0 ;
    int32_t     length = 0 ;
    int32_t     code = 0 ;
    int32_t     res ;
    char*       head ;

    *status = 0 ;
    memset (framing, 0, sizeof (HTTP_CLIENT_FRAMING_T)) ;

    client->read_buffer = (uint8_t*)HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH + HTTP_CLIENT_MAX_RECV_BUFFER_PADDING) ;
    if (client->read_buffer == 0) {
        return HTTP_CLIENT_E_MEMORY ;

    }
    client->read_start = 0 ;
    client->read_end = 0 ;
    head = (char*)client->read_buffer ;

    for (;;) {
        length = httpclient_find_head_end (head,
                        (client->read_end < limit) ? client->read_end : limit, &scanned) ;
        if (length < 0) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTP  :W: NUL or stray CR or LF in the header");
            res = length ;
            break ;

        }

        if (length == 0) {
            if (client->read_end >= limit) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: HTTP_CLIENT_MAX_RECV_HEADER_LENGTH to small for HTTP headers!");
                res = HTTP_CLIENT_E_LENGTH ;
                break ;

            }
            res = httpclient_read_more (client, 0, timeout) ;
            if (res <= 0) {
                /* A close gives 0, which a caller would take for HTTP_CLIENT_E_OK. */
                res = res ? res : HTTP_CLIENT_E_CONNECTION ;
                break ;

            }
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                        "HTTP  : : incomplete header received %d bytes",
                        (int)client->read_end);
            continue ;

        }

        code = httpclient_parse_status_line (head) ;
        if (code == 0) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                        "HTTP  :W: invalid status line");
            res = HTTP_CLIENT_E_HEADER ;
            break ;

        }
        if ((code >= 200) || (code == 101)) {
            res = HTTP_CLIENT_E_OK ;
            break ;

        }

        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : interim response %d skipped", (int)code);
        memmove (head, &head[length], client->read_end - (uint32_t)length) ;
        client->read_end -= (uint32_t)length ;
        limit -= (uint32_t)length ;
        scanned = 0 ;

    }

    if (res == HTTP_CLIENT_E_OK) {
        /* The status line is valid, so a CRLF ends it within the head. */
        char* fields = (char*)memchr (head, '\r', (size_t)length) + 2 ;

        *status = code ;
        res = httpclient_parse_fields (fields, &head[length - 2], headers, count, framing) ;
        if (res != HTTP_CLIENT_E_OK) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                        "HTTP  :W: malformed header field");

        }
    }

    if (res != HTTP_CLIENT_E_OK) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                "HTTP  : : httpclient_read_head  %d", res);
        HTTP_CLIENT_FREE(client->read_buffer) ;
        client->read_buffer = 0 ;
        client->read_start = 0 ;
        client->read_end = 0 ;
        return res ;

    }

    client->read_start = (uint32_t)length ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_body_framing
 * @details Decides how the body of the response is framed (RFC 9112,
 *          section 6.3) and sets the body reader up for it.
 *
 * @param[in] client
 * @param[in] status                    The final status.
 * @param[in] framing                   From the head.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_CONTENT        A transfer coding other than chunked alone, or an
 *                                      invalid or conflicting Content-Length.
 * @retval HTTP_CLIENT_E_LENGTH         The Content-Length is above INT32_MAX.
 *
 * @http
 */
static int32_t
httpclient_body_framing (HTTP_CLIENT_T* client, int32_t status, const HTTP_CLIENT_FRAMING_T* framing)
{
    client->content_length = 0 ;
    client->body_left = 0 ;
    client->body_read = 0 ;
    client->chunks = 0 ;

    if ((status < 200) || (status == 204) || (status == 304)) {
        /* Never a body, whatever the header says. Of the 1xx, only 101 gets here. */
        client->body_state = HTTP_CLIENT_BODY_DONE ;

    } else if (framing->has_coding) {
        /* Transfer-Encoding overrides a Content-Length. This client only
         * decodes chunked, which must then be the one coding. */
        if ((framing->codings != 1) || !framing->chunked) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: unsupported transfer coding");
            return HTTP_CLIENT_E_CONTENT ;

        }
        client->content_length = -1 ;
        client->body_state = HTTP_CLIENT_BODY_CHUNK_SIZE ;

    } else if (framing->has_length) {
        if (framing->length_error != HTTP_CLIENT_E_OK) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: invalid content length");
            return framing->length_error ;

        }
        client->content_length = (int32_t)framing->length ;
        client->body_left = framing->length ;
        client->body_state = framing->length ? HTTP_CLIENT_BODY_LENGTH : HTTP_CLIENT_BODY_DONE ;

    } else {
        /* Neither: the body runs until the server closes the connection. */
        client->content_length = -1 ;
        client->body_state = HTTP_CLIENT_BODY_CLOSE ;

    }

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_read_response
 * @details Reads the response from a get or a post into one allocated buffer.
 *          It reads through httpclient_read_response_ex() and
 *          httpclient_read_next_ex(), so every framing is handled by the same
 *          code that streams downloads. With a Content-Length the buffer is
 *          allocated once at that size. Otherwise it starts at
 *          HTTP_CLIENT_RESPONSE_INITIAL_LENGTH bytes and doubles as the body
 *          arrives. qoraal has no realloc, so growing copies the body into a
 *          new buffer, and the old and new buffer exist together for a moment.
 *          The response is NUL-terminated after the body, and
 *          client->retry_after is set from a Retry-After header (0 if none).
 * @note    The timeout applies to each read, not to the whole response.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] response                 The body, NUL-terminated, also for any status other
 *                                      than 200, and an empty string when there is none.
 *                                      To be freed with httpclient_free_response(). 0 on error.
 * @param[out] status                   HTTP status of the final response, like 200 for OK.
 *                                      Set as soon as the header is parsed, also when the
 *                                      body then fails; 0 when no valid status line was read.
 *
 * @return                              Number of bytes in response, or < 0: an error of
 *                                      httpclient_read_response_ex() or
 *                                      httpclient_read_next_ex(), or
 * @retval HTTP_CLIENT_E_PARM           client, response or status is 0.
 * @retval HTTP_CLIENT_E_LENGTH         The body is larger than HTTP_CLIENT_MAX_RESPONSE_LENGTH.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate the body.
 *
 * @http
 */
int32_t
httpclient_read_response (HTTP_CLIENT_T* client, uint32_t timeout, char** response, int32_t* status)
{
    char*           body ;
    uint8_t*        data ;
    uint32_t        size ;
    uint32_t        length = 0 ;
    int32_t         res ;

    if (response) {
        *response = 0 ;

    }
    if (status) {
        *status = 0 ;

    }
    if (!client || !response || !status) {
        return HTTP_CLIENT_E_PARM ;

    }

    res = httpclient_read_response_ex (client, timeout, status) ;
    if (res != HTTP_CLIENT_E_OK) {
        return res ;

    }

    if (client->content_length >= 0) {
        if ((uint32_t)client->content_length > HTTP_CLIENT_MAX_RESPONSE_LENGTH) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTP  :E: content length %d larger than %u",
                (int)client->content_length, (unsigned)HTTP_CLIENT_MAX_RESPONSE_LENGTH);
            httpclient_read_complete_ex (client) ;
            return HTTP_CLIENT_E_LENGTH ;

        }
        size = (uint32_t)client->content_length ;

    } else {
        size = HTTP_CLIENT_RESPONSE_INITIAL_LENGTH ;
        if (size > HTTP_CLIENT_MAX_RESPONSE_LENGTH) {
            size = HTTP_CLIENT_MAX_RESPONSE_LENGTH ;

        }
    }

    body = (char*)HTTP_CLIENT_MALLOC(size + 1) ;
    if (body == 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
            "HTTP  :E: out of memory for a %u byte response", (unsigned)size);
        httpclient_read_complete_ex (client) ;
        return HTTP_CLIENT_E_MEMORY ;

    }

    while ((res = httpclient_read_next_ex (client, timeout, &data)) > 0) {

        if ((uint32_t)res > HTTP_CLIENT_MAX_RESPONSE_LENGTH - length) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTP  :E: response larger than %u", (unsigned)HTTP_CLIENT_MAX_RESPONSE_LENGTH);
            res = HTTP_CLIENT_E_LENGTH ;
            break ;

        }

        if ((uint32_t)res > size - length) {
            /* size <= HTTP_CLIENT_MAX_RESPONSE_LENGTH < INT32_MAX, so doubling cannot wrap. */
            uint32_t    next = 2 * size ;
            char*       p ;

            if (next < length + (uint32_t)res) {
                next = length + (uint32_t)res ;
            }
            if (next > HTTP_CLIENT_MAX_RESPONSE_LENGTH) {
                next = HTTP_CLIENT_MAX_RESPONSE_LENGTH ;
            }

            p = (char*)HTTP_CLIENT_MALLOC(next + 1) ;
            if (p == 0) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: out of memory growing the response to %u bytes", (unsigned)next);
                res = HTTP_CLIENT_E_MEMORY ;
                break ;

            }
            memcpy (p, body, length) ;
            HTTP_CLIENT_FREE(body) ;
            body = p ;
            size = next ;

        }

        memcpy (&body[length], data, (size_t)res) ;
        length += (uint32_t)res ;

    }

    httpclient_read_complete_ex (client) ;

    if (res < 0) {
        HTTP_CLIENT_FREE(body) ;
        return res ;

    }

    body[length] = '\0' ;
    *response = body ;

    return (int32_t)length ;
}

/**
 * @brief   httpclient_read_response_ex
 * @details Reads the response header from a get or a post.
 *          client->retry_after is set from a Retry-After header (0 if none).
 * @note    If the response has content it should be read with calls to
 *          httpclient_read_next_ex(), then httpclient_read_complete_ex().
 *          Interim 1xx responses are skipped. A 101, 204 or 304 response has
 *          no body. Otherwise chunked transfer coding or Content-Length
 *          frames the body. Without either it runs until the server closes
 *          the connection; over TLS only a close_notify ends it.
 *
 * @param[in] client
 * @param[in] timeout                   For each read.
 * @param[out] status                   HTTP status of the final response, like 200 for OK;
 *                                      0 when no valid status line was read.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_PARM           client or status is 0, the client is not connected,
 *                                      or the last response was not completed with
 *                                      httpclient_read_complete_ex().
 * @retval HTTP_CLIENT_E_TIMEOUT        No data within the timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     The connection closed or failed before the header
 *                                      was complete, also when no response came at all.
 * @retval HTTP_CLIENT_E_HEADER         Not a status line, or a malformed header field.
 * @retval HTTP_CLIENT_E_CONTENT        A transfer coding other than chunked, or an invalid
 *                                      or conflicting Content-Length.
 * @retval HTTP_CLIENT_E_LENGTH         The header is larger than
 *                                      HTTP_CLIENT_MAX_RECV_HEADER_LENGTH, or the
 *                                      Content-Length above INT32_MAX.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 * @retval HTTP_CLIENT_E_ERROR          select() failed.
 *
 * @http
 */
int32_t
httpclient_read_response_ex (HTTP_CLIENT_T* client, uint32_t timeout, int32_t* status)
{
    HTTP_CLIENT_FRAMING_T framing ;
    HTTP_HEADER_T headers[] = {
            {HTTP_HEADER_KEY_RETRY_AFTER, 0},
    } ;
    int32_t res ;

    if (status) {
        *status = 0 ;

    }
    if (!client || !status) {
        return HTTP_CLIENT_E_PARM ;

    }

    client->retry_after = 0 ;

    DBG_CHECK_HTTP_CLIENT (client->socket>=0, HTTP_CLIENT_E_PARM,
                    "HTTP  :E: unexpected - socket not valid.!") ;
    DBG_CHECK_HTTP_CLIENT (client->read_buffer == 0, HTTP_CLIENT_E_PARM,
                    "HTTP  :E: unexpected - client->read_buffer.!") ;

    res = httpclient_read_head (client, timeout, status, headers,
                    sizeof(headers)/sizeof(headers[0]), &framing) ;
    if (res == HTTP_CLIENT_E_OK) {
        /* The header values point into read_buffer. */
        client->retry_after = httpclient_parse_retry_after (headers[0].value) ;
        res = httpclient_body_framing (client, *status, &framing) ;
        if (res != HTTP_CLIENT_E_OK) {
            HTTP_CLIENT_FREE(client->read_buffer) ;
            client->read_buffer = 0 ;

        }
    }

    if (res != HTTP_CLIENT_E_OK) {
        client->body_state = HTTP_CLIENT_BODY_ERROR ;
        client->body_error = res ;

    }

    return res ;
}

/**
 * @brief   httpclient_read_line
 * @details Finds the next line of the chunked framing, a chunk size line or
 *          a trailer field, reading until its LF is in read_buffer, and moves
 *          read_start past it. Chunk extensions and trailers make a line any
 *          length, so only the receive buffer bounds it.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] line                     The first byte of the line.
 * @param[out] eol                      Its end: the CR of a CRLF, else the LF.
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_LENGTH         The line does not fit HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH.
 * @retval HTTP_CLIENT_E_CONNECTION     The connection closed or failed before the line was
 *                                      complete.
 * @retval HTTP_CLIENT_E_TIMEOUT        No data within the timeout.
 *
 * @http
 */
static int32_t
httpclient_read_line (HTTP_CLIENT_T* client, uint32_t timeout, char** line, char** eol)
{
    int32_t res ;

    for (;;) {
        char*   start = (char*)&client->read_buffer[client->read_start] ;
        char*   lf = (char*)memchr (start, '\n', client->read_end - client->read_start) ;

        if (lf) {
            *line = start ;
            *eol = ((lf > start) && (lf[-1] == '\r')) ? lf - 1 : lf ;
            client->read_start = (uint32_t)(lf + 1 - (char*)client->read_buffer) ;
            return HTTP_CLIENT_E_OK ;

        }

        res = httpclient_read_more (client, 0, timeout) ;
        if (res <= 0) {
            return res ? res : HTTP_CLIENT_E_CONNECTION ;

        }
    }
}

/**
 * @brief   httpclient_read_next_ex
 * @details Reads the next part of the content from a get or a post.
 * @note    recv points into the client's receive buffer and is valid until the
 *          next call. When all data is read httpclient_read_complete_ex()
 *          should be called.
 *          After the end of the body every call returns 0, and after an
 *          error every call returns that error again, except after a
 *          timeout: then a call goes on where the last one stopped.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] recv                 Pointer to received data
 *
 * @return                              Number of bytes in content, 0 when the body is
 *                                      complete, or < 0:
 * @retval HTTP_CLIENT_E_PARM           client or recv is 0, or no response is being read.
 * @retval HTTP_CLIENT_E_TIMEOUT        No data within the timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     The connection closed or failed before the body was
 *                                      complete. A body that runs until the server closes
 *                                      ends only on a clean close, see httpclient_read().
 * @retval HTTP_CLIENT_E_CONTENT        The chunked encoding is malformed.
 * @retval HTTP_CLIENT_E_LENGTH         A chunk size above INT32_MAX, or a chunk size line or
 *                                      trailer field that does not fit
 *                                      HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH.
 * @retval HTTP_CLIENT_E_MEMORY         mbedTLS ran out of memory.
 * @retval HTTP_CLIENT_E_ERROR          select() failed.
 *
 * @http
 */
int32_t
httpclient_read_next_ex (HTTP_CLIENT_T* client, uint32_t timeout, uint8_t** recv)
{
    int32_t     res = HTTP_CLIENT_E_OK ;
    uint32_t    avail ;
    uint32_t    size ;
    char*       line ;
    char*       eol ;

    if (!client || !recv) {
        return HTTP_CLIENT_E_PARM ;

    }

    while (res >= HTTP_CLIENT_E_OK) {
        avail = client->read_end - client->read_start ;

        switch (client->body_state) {
        case HTTP_CLIENT_BODY_DONE:
            return 0 ;

        case HTTP_CLIENT_BODY_ERROR:
            return client->body_error ;

        case HTTP_CLIENT_BODY_LENGTH:
        case HTTP_CLIENT_BODY_CLOSE:
        case HTTP_CLIENT_BODY_CHUNK_DATA:
            if (avail == 0) {
                /* Not past a Content-Length body: on a kept-alive connection
                 * the next bytes belong to the next response. */
                res = httpclient_read_more (client,
                            (client->body_state == HTTP_CLIENT_BODY_LENGTH) ? client->body_left : 0,
                            timeout) ;
                if (res == 0) {
                    if (client->body_state == HTTP_CLIENT_BODY_CLOSE) {
                        client->body_state = HTTP_CLIENT_BODY_DONE ;

                    } else {
                        res = HTTP_CLIENT_E_CONNECTION ;

                    }
                }
                break ;

            }

            /* Body bytes that came with the header can run past a short body. */
            size = avail ;
            if ((client->body_state != HTTP_CLIENT_BODY_CLOSE) && (size > client->body_left)) {
                size = client->body_left ;

            }
            *recv = &client->read_buffer[client->read_start] ;
            client->read_start += size ;
            client->body_read += size ;
            if (client->body_state != HTTP_CLIENT_BODY_CLOSE) {
                client->body_left -= size ;
                if (client->body_left == 0) {
                    client->body_state = (client->body_state == HTTP_CLIENT_BODY_LENGTH) ?
                                HTTP_CLIENT_BODY_DONE : HTTP_CLIENT_BODY_CHUNK_END ;

                }
            }
            return (int32_t)size ;

        case HTTP_CLIENT_BODY_CHUNK_SIZE:
            res = httpclient_read_line (client, timeout, &line, &eol) ;
            if (res == HTTP_CLIENT_E_OK) {
                res = httpclient_parse_chunk_size (line, eol, &client->body_left) ;

            }
            if (res == HTTP_CLIENT_E_OK) {
                client->chunks++ ;
                client->body_state = client->body_left ?
                            HTTP_CLIENT_BODY_CHUNK_DATA : HTTP_CLIENT_BODY_TRAILER ;

            }
            break ;

        case HTTP_CLIENT_BODY_CHUNK_END:
            if (avail < 2) {
                res = httpclient_read_more (client, 0, timeout) ;
                if (res == 0) {
                    res = HTTP_CLIENT_E_CONNECTION ;

                }
                break ;

            }
            /* The chunk data ends with CRLF. Anything else means the chunk
             * size did not match the data sent. */
            if ((client->read_buffer[client->read_start] != '\r') ||
                    (client->read_buffer[client->read_start + 1] != '\n')) {
                res = HTTP_CLIENT_E_CONTENT ;
                break ;

            }
            client->read_start += 2 ;
            client->body_state = HTTP_CLIENT_BODY_CHUNK_SIZE ;
            break ;

        case HTTP_CLIENT_BODY_TRAILER:
            /* Trailer fields are skipped, this client uses none. The empty
             * line after them completes the body. */
            res = httpclient_read_line (client, timeout, &line, &eol) ;
            if ((res == HTTP_CLIENT_E_OK) && (eol == line)) {
                client->body_state = HTTP_CLIENT_BODY_DONE ;

            }
            break ;

        default:
            return HTTP_CLIENT_E_PARM ;

        }
    }

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
            "HTTP  : : httpclient_read_next_ex %d after %u bytes",
            res, (unsigned)client->body_read);

    /* A timeout leaves the reader where it was, so another call can go on
     * waiting. After any other error the framing is lost. */
    if (res != HTTP_CLIENT_E_TIMEOUT) {
        client->body_state = HTTP_CLIENT_BODY_ERROR ;
        client->body_error = res ;

    }

    return res ;
}

/**
 * @brief   httpclient_read_complete_ex
 * @details Called at the end of a get ot post, also when the body was not
 *          read to its end. Frees the receive buffer.
 *
 * @param[in] client
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_PARM           client is 0.
 *
 * @http
 */
int32_t
httpclient_read_complete_ex (HTTP_CLIENT_T* client)
{
    if (!client) {
        return HTTP_CLIENT_E_PARM ;

    }

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
        "HTTP  : : read complete %u bytes in %u chunks",
                (unsigned)client->body_read, (unsigned)client->chunks);


    if (client->read_buffer) {
        HTTP_CLIENT_FREE(client->read_buffer) ;
        client->read_buffer = 0 ;
    }
    client->read_start = 0 ;
    client->read_end = 0 ;
    client->body_state = HTTP_CLIENT_BODY_NONE ;

    return HTTP_CLIENT_E_OK ;
}

#if HTTP_WEBSOCK

/**
 * @brief 	httpclient_websock_get
 * @details	Starts a HTTP GET request to open a websocket.
 * @note	The response should be read with httpclient_websock_read_response.
 *
 * @param[in] client					http client instance
 * @param[in] endpoint					url
 * @param[in] credentials				username:password
 *
 * @return 								The number of bytes sent or < 0 for error codes, see
 *                                      httpclient_send_head().
 * @retval HTTP_CLIENT_E_PARM           client or endpoint is 0.
 *
 * @http
 */
int32_t httpclient_websock_get(HTTP_CLIENT_T* client, const char* endpoint, const char* credentials)
{
    int32_t count;

    DBG_CHECK_HTTP_CLIENT(client, HTTP_CLIENT_E_PARM, "httpclient_websock_get : CHECK : unexpected - client not valid.\r\n!");
    DBG_CHECK_HTTP_CLIENT(endpoint, HTTP_CLIENT_E_PARM, "httpclient_websock_get : CHECK : unexpected - endpoint not valid.\r\n!");

    /*
     * Create the Sec-WebSocket-Key for this connection
     */
    for (count = 0; count < 4; count++)
    {
        client->key[count] = HTTP_CLIENT_RAND();
    }

    /*
     * If routed by the server use the httpclient_set_hostname()
     * to set the specific hostname before calling this function.
     */
    return httpclient_send_head(client, "GET", endpoint, credentials, HTTP_CLIENT_REQ_WEBSOCK, 0, 2000);
}

/**
 * @brief 	httpclient_read_response
 * @details Reads the response after a httpclient_websock_get().
 * @note	If the connection was successfully upgraded to a websocket a HTTP status
 *			code of 101 should be returned.
 *
 * @param[in] client					http client instance.
 * @param[in] timeout					Timeout in ms to wait for the response.
 *
 * @return 								The HTTP status code or < 0 for error codes.
 * @retval HTTP_CLIENT_E_PARM           client is 0, not connected, or a response is being read.
 * @retval HTTP_CLIENT_E_TIMEOUT        No data within the timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_HEADER         Error parsing HTTP header.
 * @retval HTTP_CLIENT_E_LENGTH         The header is larger than HTTP_CLIENT_MAX_RECV_HEADER_LENGTH.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t httpclient_websock_read_response(HTTP_CLIENT_T* client, uint32_t timeout)
{
    HTTP_CLIENT_FRAMING_T framing;
    int32_t status;
    int32_t res;
    HTTP_HEADER_T headers[] = {
        {HTTP_HEADER_KEY_CONNECTION, 0},     {HTTP_HEADER_KEY_WEBSOCK_UPGRADE, 0},
        {HTTP_HEADER_KEY_WEBSOCK_ACCEPT, 0}, {HTTP_HEADER_KEY_WEBSOCK_PROT, 0},
    };

    DBG_CHECK_HTTP_CLIENT(client && (client->socket >= 0), HTTP_CLIENT_E_PARM,
                          "httpclient_websock_read_response : CHECK : unexpected - socket not valid.\r\n!");
    DBG_CHECK_HTTP_CLIENT(client->read_buffer == 0, HTTP_CLIENT_E_PARM,
                          "httpclient_websock_read_response : CHECK : unexpected - client->read_buffer.\r\n!");

    /*
     * Read the complete header up including the "\r\n\r\n" marking the start of the content if any.
     */
    res = httpclient_read_head(client, timeout, &status, headers, sizeof(headers) / sizeof(headers[0]), &framing);
    if (res != HTTP_CLIENT_E_OK)
    {
        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_WARNING, "HTTP  :E: httpclient_read_head failed %d\r\n", res);
        return res;
    }

    /*
     * Process the returned status
     */
    if (status != 101)
    {
        // ToDo: This if clause (for status != 101) is not tested

        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_WARNING, "HTTP  :W: error status code %d\r\n", status);
    }
    else
    {

        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_REPORT,
                                "HTTP  : : httpclient_websock_read_response status code %d\r\n", status);

        // for debugging purposes print out all the received headers.
        unsigned int i;
        for (i = 0; i < sizeof(headers) / sizeof(headers[0]); i++)
        {
            if (headers[i].value)
            {
                DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_INFO,
                                        "HTTP  : : httpclient_websock_read_response header %s: %s\r\n", headers[i].key,
                                        headers[i].value ? headers[i].value : "");
            }
        }

        /*
         * Check required header fields as specified in RFC6455
         */
        if (!headers[0].value || strnicmp(headers[0].value, "Upgrade", 7))
        {  // Connection header
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG,
                                    "HTTP  :E: httpclient_websock_read_response connection header invalid!");
            status = HTTP_CLIENT_E_HEADER;
        }
        if (!headers[1].value || strnicmp(headers[1].value, "websocket", 9))
        {  // Upgrade header
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG,
                                    "HTTP  :E: httpclient_websock_read_response upgrade header invalid!");
            status = HTTP_CLIENT_E_HEADER;
        }
        if (!headers[2].value)
        {  // Sec-WebSocket-Accept header
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  :E: Sec-WebSocket-Accept header missing!");
            status = HTTP_CLIENT_E_HEADER;
        }
        else
        {
            // ToDo: Validate correct SHA-1 hash in Sec-WebSocket-Accept header
        }
        if (!headers[3].value)
        {  // Sec-WebSocket-Protocol header
            // RFC 6455 spec says this is an optional response from the server
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_WARNING, "HTTP  :E: Sec-WebSocket-Protocol header missing!");
            /////// ignore			status =   HTTP_CLIENT_E_HEADER ;
        }
        else
        {
            // ToDo: Validate that HTTP_CLIENT_WEBSOCKET_PROTOCOL is returned.
        }
    }

    /* The header values point into the receive buffer this frees. */
    httpclient_read_complete_ex(client);

    return status;
}

/**
 * @brief 	websock_decode
 * @details Decodes websocket frame received from server.
 *
 * @param[in] client					http client instance
 * @param[out] opcode					Opcode of websocket frame.
 * @param[out] fin						FIN flag of websocket frame.
 * @param[in] timeout					Timeout im ms to read from socket.
 *
 * @return 								Number of bytes in payload or < 0 indicates an
 * error.
 * @retval HTTP_CLIENT_E_ERROR         	Socket timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     Remote connection closed.
 * @retval HTTP_CLIENT_E_LENGTH         Buffer or payload length error.
 * @retval HTTP_CLIENT_E_HEADER     	Protocol error.
 *
 * @websockets
 */
static int32_t websock_decode(HTTP_CLIENT_T* client, uint8_t* opcode, uint8_t* fin, uint32_t timeout)
{
    int32_t status;
    uint32_t payload_length = 0;
    uint8_t header[6] = {0};

#    define WEBSOCK_FIN(buffer) ((buffer[0] >> 7) & 0x1)
#    define WEBSOCK_OPCODE(buffer) (buffer[0] & 0xF)
#    define WEBSOCK_MASK(buffer) ((buffer[1] >> 7) & 0x1)
#    define WEBSOCK_PAYLOAD_LENGTH(buffer) (buffer[1] & 0x7F)

    *opcode = 0;
    *fin = 0;

    // status = t_recv (athwifi_get_handle(), client->socket, &pbuffer, 0, 0);
    status = httpclient_read(client, (void*)header, 2, timeout);  //

    if (status <= 0)
    {
        // A close is no frame, but the caller must not wait for one either.
        return status ? status : HTTP_CLIENT_E_CONNECTION;
    }

    if (WEBSOCK_MASK(header))
    {
        /*
         * RFC Section 5.1: "A server MUST NOT mask any frames that it sends to the client."
         */
        //	httpclient_read(client, (void*)&pbuffer, -1);
        return HTTP_CLIENT_E_HEADER;
    }

    *opcode = WEBSOCK_OPCODE(header);
    *fin = WEBSOCK_FIN(header);
    payload_length = WEBSOCK_PAYLOAD_LENGTH(header);

    if (payload_length == 126)
    {
        uint16_t tmp;
        status = httpclient_read(client, (void*)header, 2, timeout);
        if (status > 0)
        {
            memcpy(&tmp, &header[0], 2);
            payload_length = ntohs(tmp);
        }
    }
    else if (payload_length == 127)
    {
        status = httpclient_read(client, (void*)header, 4, timeout);
        if (status > 0)
        {
            memcpy(&payload_length, &header[0], 4);
            payload_length = ntohl(payload_length);
        }
    }
    else
    {
        // status = httpclient_read(client, (void*)&header, 2) ;
    }

    return payload_length;
}

/**
 * @brief 	websock_write
 * @details Write the frame header followed by the payload to the websocket.
 *
 * @param[in] client					http client instance.
 * @param[in] opcode					Opcode for frame.
 * @param[in] payload					payload.
 * @param[in] length					payload length.
 *
 * @return 								Number of bytes in payload or <= 0 indicates an
 * error.
 * @retval HTTP_CLIENT_E_ERROR          Socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     Connection closed by server.
 * @retval HTTP_CLIENT_E_MEMORY     	Out of memory.
 *
 * @websockets
 */
static int32_t websock_write(HTTP_CLIENT_T* client, uint8_t opcode, const char* payload, uint32_t length)
{
    char* buffer;

    // pre-allocate maximum send buffer
    buffer = HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH);
    if (buffer)
    {
        uint32_t offset = 0;
        uint32_t mask;
        uint32_t i;
        int32_t res = 0;

        // Set the opcode (text or binary) and the FIN bit.
        buffer[offset++] = (1 << 7) | opcode;

        // Write the length and mask bit to the header.
        if (length < 126)
        {
            buffer[offset++] = (length & 0x7F) | (1 << 7);
        }
        else if (length < (uint16_t)-1)
        {
            uint16_t tmp = htons(length);
            buffer[offset++] = 126 | (1 << 7);
            memcpy(&buffer[offset], &tmp, 2);
            ;
            offset += 2;
        }
        else
        {
            uint32_t tmp = htons(length);
            buffer[offset++] = 127 | (1 << 7);
            memcpy(&buffer[offset], &tmp, 4);
            ;
            offset += 4;
        }

        // Write the mask to the header
        do
        {
            mask = HTTP_CLIENT_RAND();
        } while (mask == 0);
        memcpy(&buffer[offset], &mask, 4);
        offset += 4;
        // encode the payload with the mask.
        for (i = 0; i < length; i++)
        {
            if (offset >= HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH)
            {
                // send in chunks of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH
                if ((res = httpclient_write(client, (unsigned char*)buffer, offset, 100)) <= 0)
                {
                    break;
                }
                offset = 0;
            }
            buffer[offset++] = payload[i] ^ (((char*)&mask)[i % 4]);
        }

        if (offset && (res >= 0)) res = httpclient_write(client, (unsigned char*)buffer, offset, 100);
        HTTP_CLIENT_FREE(buffer);

        return res > 0 ? (int32_t)i : res;
    }

    return HTTP_CLIENT_E_MEMORY;
}

/**
 * @brief 	httpclient_websock_read
 * @details Read one frame from the websocket.
 * @note	If timeout is zzero the function will poll for data with t_select_ver1(). If there is
 *			data on the socket it will continue to read the header. If the complete header is
 *			not available it will use the timeout HTTP_CLIENT_READMORE_TIMEOUT to read the data
 *			from the socket.
 *
 * @param[in] client					http client instance
 * @param[in] buffer					Pointer to frame received.
 * @param[in] timeout					Timeout wait for frame from server.
 *
 * @return 								Number of bytes in payload or <= 0 indicates an
 *error.
 * @retval HTTP_CLIENT_E_ERROR          Socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     Connection closed by server.
 * @retval HTTP_CLIENT_E_MEMORY     	Out of memory.
 * @retval HTTP_CLIENT_E_HEADER     	Unexpected opcode received.
 *
 * @websockets
 */
int32_t httpclient_websock_read(HTTP_CLIENT_T* client, char** buffer, uint32_t timeout)
{
    // char* read ;
    int32_t read_len;
    uint8_t opcode;
    uint8_t fin;

    *buffer = 0;

    int32_t payload_length = websock_decode(client, &opcode, &fin, timeout);

    if (payload_length == HTTP_CLIENT_E_TIMEOUT)
    {
        payload_length = 0;
    }

    DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_REPORT, "HTTP  : : httpclient_websock_read payload length %d\r\n",
                            payload_length);

    if (payload_length > 0)
    {

        uint32_t offset = 0;

        if (payload_length > 0)
        {
            *buffer = (char*)HTTP_CLIENT_MALLOC(payload_length);
            if (*buffer == 0)
            {
                return HTTP_CLIENT_E_MEMORY;
            }

            // read the complete payload
            while (offset < (uint32_t)payload_length)
            {

                read_len = httpclient_read(client, (void*)&(*buffer)[offset], payload_length - offset, timeout);
                if (read_len <= 0)
                {
                    HTTP_CLIENT_FREE(*buffer);
                    *buffer = 0;
                    return read_len ? read_len : HTTP_CLIENT_E_CONNECTION;
                }

                offset += read_len;
            }
        }

        switch (opcode)
        {
        case 0:  // continuation
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : websock_read header continuation");
            break;

        case 1:  // text
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_INFO, "HTTP  : : websock_read header text");
            break;

        case 2:  // binary
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : websock_read header binary");
            break;

        /*
         * Control Frames:
         */
        case 8:  // connection closesocket
        {
            uint16_t code = 1000;

            if (payload_length >= 2)
            {

                memcpy(&code, *buffer, 2);
                code = ntohs(code);
                DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG,
                                        "HTTP  : : websock_read header connection closed with status %d\r\n", code);
            }
            else
            {
                DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_REPORT, "HTTP  : : websock_read header connection closed");
            }
            client->status |= HTTP_CLIENT_STATUS_CLOSE_SERVER;
            if (!(client->status & HTTP_CLIENT_STATUS_CLOSE_CLIENT))
            {
                // if the closesocket was initiated from the serve, we send the expected closesocket response
                // and wait for the server to closesocket the socket.
                httpclient_websock_initiate_close(client, code);

            }
            // return HTTP_CLIENT_E_CONNECTION so the user can shutdown the socket.
            payload_length = HTTP_CLIENT_E_CONNECTION;
            HTTP_CLIENT_FREE(*buffer);
        }
        break;

        case 9:  // ping
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : websock_read header ping");
            // ToDo: Implement ping and continue to receive next frame.
            payload_length = 0;
            HTTP_CLIENT_FREE(*buffer);
            break;

        case 10:  // pong
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : websock_read header pong");
            payload_length = 0;
            HTTP_CLIENT_FREE(*buffer);
            break;

        default:  // unexpected
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : websock_read header unexpected opcode %d\r\n",
                                    (int)opcode);
            payload_length = HTTP_CLIENT_E_HEADER;
            HTTP_CLIENT_FREE(*buffer);
            break;
        }
    }

    return payload_length;
}

/**
 * @brief 	httpclient_websock_free
 * @details Free a frame received by a call to httpclient_websock_read().
 * @note
 *
 * @param[in] client					http client instance
 * @param[in] buffer					Pointer to frame.
 *
 * @return 								void.
 *
 * @websockets
 */
void httpclient_websock_free(HTTP_CLIENT_T* client, char* buffer)
{
    if (buffer)
    {
        HTTP_CLIENT_FREE(buffer);
    }
}

/**
 * @brief 	httpclient_websock_write_text
 * @details Write a websocket frame and mark it as text.
 * @note
 *
 * @param[in] client					http client instance.
 * @param[in] payload					Payload of frame.
 * @param[in] length					Length of payload.
 *
 * @return 								Number of bytes send or <= 0 indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          Socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     Connection closed by server.
 *
 * @websockets
 */
int32_t httpclient_websock_write_text(HTTP_CLIENT_T* client, const char* payload, uint32_t length)
{
    DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_REPORT, "HTTP  : : websock_write_text payload length %d\r\n", length);

    return websock_write(client, 1, payload, length);
}

/**
 * @brief 	httpclient_websock_write_binary
 * @details Write a websocket frame and mark it as binary.
 * @note
 *
 * @param[in] client					http client instance.
 * @param[in] payload					Payload of frame.
 * @param[in] length					Length of payload.
 *
 * @return 								Number of bytes send or <= 0 indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          Socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     Connection closed by server.
 *
 * @websockets
 */
int32_t httpclient_websock_write_binary(HTTP_CLIENT_T* client, const char* payload, uint32_t length)
{
    DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_REPORT, "HTTP  : : websock_write_binary payload length %d\r\n",
                            length);

    return websock_write(client, 2, payload, length);
}

/**
 * @brief 	httpclient_websock_ping
 * @details Send a websocket ping.
 * @note
 *
 * @param[in] client					http client instance.
 *
 * @return 								Number of bytes send or <= 0 indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          Socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     Connection closed by server.
 *
 * @websockets
 */
int32_t httpclient_websock_ping(HTTP_CLIENT_T* client)
{
    DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_REPORT, "HTTP  : : websock_ping payload length %d\r\n", 0);

    return websock_write(client, 9, 0, 0);
}

/**
 * @brief 	httpclient_websock_close
 * @details Initiate the closesocket handshake for the websocket.
 * @note	The client should call httpclient_websock_read to read the servers closesocket response.
 *
 * @param[in] client					http client instance.
 *
 * @return 								Number of bytes send or <= 0 indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          Socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     Connection closed by server.
 *
 * @websockets
 */
int32_t httpclient_websock_initiate_close(HTTP_CLIENT_T* client, uint16_t code)
{
    DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : websock_close closing connection with %d\r\n", code);

    code = htons(code);
    client->status |= HTTP_CLIENT_STATUS_CLOSE_CLIENT;
    return websock_write(client, 8, (char*)&code, 2);
}

#endif /* HTTP_WEBSOCK */


