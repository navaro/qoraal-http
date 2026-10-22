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
#include <stdbool.h>
#include <stdint.h>
#include "qoraal/qoraal.h"
#include "qoraal-http/config.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpclient.h"
#include "qoraal-http/httpparse.h"
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
#include "qoraal-http/mbedtls/mbedtlsutils.h"
#endif

#define HTTP_GET_REQ            "GET /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Accept: */*\r\n" \
                                "Connection: keep-alive\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                "\r\n"

#define HTTP_GET_REQ_BASIC      "GET /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Accept: */*\r\n" \
                                "Connection: keep-alive\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                "Authorization: Basic %s\r\n" \
                                "\r\n"

/* Used when no httpclient_set_content_type() is in effect, as before it existed. */
#define HTTP_POST_DEFAULT_CONTENT_TYPE  "text/plain"

/* The POST requests take: endpoint, host, content type, [credentials], [length]. */
#define HTTP_POST_REQ           "POST /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Content-Type: %s\r\n" \
                                "X-Content-Type-Options: nosniff\r\n" \
                                "Accept: */*\r\n" \
                                "Connection: keep-alive\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                "Content-Length: %d\r\n" \
                                "\r\n"

#define HTTP_POST_CHUNKED_REQ   "POST /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Content-Type: %s\r\n" \
                                "X-Content-Type-Options: nosniff\r\n" \
                                "Accept: */*\r\n" \
                                "Connection: keep-alive\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                "Transfer-Encoding: chunked\r\n" \
                                "\r\n"

#define HTTP_POST_REQ_BASIC     "POST /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Content-Type: %s\r\n" \
                                "X-Content-Type-Options: nosniff\r\n" \
                                "Accept: */*\r\n" \
                                "Connection: keep-alive\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                "Authorization: Basic %s\r\n" \
                                "Content-Length: %d\r\n" \
                                "\r\n"

#define HTTP_POST_CHUNKED_REQ_BASIC     "POST /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Content-Type: %s\r\n" \
                                "X-Content-Type-Options: nosniff\r\n" \
                                "Accept: */*\r\n" \
                                "Connection: keep-alive\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                "Authorization: Basic %s\r\n" \
                                "Transfer-Encoding: chunked\r\n" \
                                "\r\n"

#define HTTP_WEBSOCKET_REQ      "GET /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Sec-WebSocket-Key: %s\r\n" \
                                "Upgrade: websocket\r\n" \
                                "Connection: Upgrade\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                /*"Origin: http://example.com\r\n"*/ \
                                "Sec-WebSocket-Protocol: " HTTP_CLIENT_WEBSOCKET_PROTOCOL "\r\n" \
                                "Sec-WebSocket-Version: 13\r\n" \
                                "\r\n"

#define HTTP_WEBSOCKET_REQ_BASIC "GET /%s HTTP/1.1\r\n" \
                                "Host: %s\r\n" \
                                "Sec-WebSocket-Key: %s\r\n" \
                                "Upgrade: websocket\r\n" \
                                "Connection: Upgrade\r\n" \
                                "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n" \
                                /*"Origin: http://example.com\r\n"*/ \
                                "Sec-WebSocket-Protocol: " HTTP_CLIENT_WEBSOCKET_PROTOCOL "\r\n" \
                                "Sec-WebSocket-Version: 13\r\n" \
                                "Authorization: Basic %s\r\n" \
                                "\r\n"


/**
 * @brief   httpclient_init
 * @details The instance 'client' to be used in all calls from this API is initialised to zero.
 *
 * @param[in] client
 *
 * @return                          The function status.
 * @retval HTTP_CLIENT_E_OK         client was initialised
 *
 * @http
 */
int32_t
httpclient_init (HTTP_CLIENT_T* client, int32_t mss)
{
    memset (client, 0, sizeof (HTTP_CLIENT_T)) ;
    client->mss = mss ;
    client->socket = -1 ;

    return HTTP_CLIENT_E_OK ;
}

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
 * @retval HTTP_CLIENT_E_ERROR          socket error.
 * @retval HTTP_CLIENT_E_CONNECTION     TCP connect or SSL handshake failed.
 * @retval HTTP_CLIENT_E_SSL_TRUST      The server certificate was rejected: it did not
 *                                      verify, its key usage or curve is not acceptable,
 *                                      or no CA is set although verification is required.
 * @retval E_NOMEM                      The SSL context could not be set up.
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

    DBG_CHECK_HTTP_CLIENT (client->socket == -1, EFAIL,
                    "HTTP  :E: httpclient_connect unexpected\r\n!") ;

    client->socket = socket (AF_INET, SOCK_STREAM, 0) ;

    if (client->socket < 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTP  :E: failed to create socket");
        client->socket = -1 ;
        return HTTP_CLIENT_E_ERROR;
    }

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (pssl_config) {
        if (!client->ssl) {
            client->ssl = HTTP_CLIENT_MALLOC(sizeof(mbedtls_ssl_context)) ;
            if (!client->ssl) {
                return E_NOMEM ;

            }
            mbedtls_ssl_init((mbedtls_ssl_context *)client->ssl) ;
            if (mbedtls_ssl_setup( (mbedtls_ssl_context *)client->ssl, pssl_config) != 0) {
                HTTP_CLIENT_FREE(client->ssl) ;
                client->ssl = 0 ;
                return E_NOMEM ;

            }
        }
        mbedtls_ssl_set_bio( (mbedtls_ssl_context *)client->ssl, (void*)(intptr_t)client->socket,
                mbedtls_net_send, mbedtls_net_recv, 0 /*mbedtls_net_recv_timeout*/ )  ;

        if (client->hostname[0]) {
            mbedtls_ssl_set_hostname ((mbedtls_ssl_context *)client->ssl, client->hostname) ;
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
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTP  : : Connect to %s:%d failed %d!",
                    client->host,(int)ntohs((uint16_t)addr->sin_port), status);
        closesocket(client->socket);
        /* The descriptor may be reused at once, httpclient_close() must not close it again. */
        client->socket = -1 ;
        return HTTP_CLIENT_E_CONNECTION ;

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
        do {
             fd_set   fdread;
             fd_set   fdex;
            struct timeval tv;

            status = mbedtls_ssl_handshake((mbedtls_ssl_context *)client->ssl);

            if (status == MBEDTLS_ERR_SSL_WANT_READ ||
                status == MBEDTLS_ERR_SSL_WANT_WRITE ) {

                FD_ZERO(&fdread) ;
                FD_ZERO(&fdex) ;
                FD_SET(client->socket, &fdread);
                FD_SET(client->socket, &fdex);
                tv.tv_sec = 8 ;
                tv.tv_usec = 0 ;

                int received = select(client->socket+1, &fdread, 0, &fdex, &tv) ;
                (void) received ;

                if (!FD_ISSET(client->socket, &fdread)) {
                       DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                                    "HTTP  : : read select failed with %d", received);
                        return HTTP_CLIENT_E_CONNECTION ;

                }
                else if (FD_ISSET(client->socket, &fdex)) {
                    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                                "HTTP  : : read exception on listening socket...");
                    return HTTP_CLIENT_E_CONNECTION ;

                }
            }

            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                "ongoing mbedtls_ssl_handshake() returned -0x%04X", -status);

        } while(status == MBEDTLS_ERR_SSL_WANT_READ ||
                status == MBEDTLS_ERR_SSL_WANT_WRITE ||
                status == MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS ||
                status == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS);

        if (status < 0) {
            if (status != MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "mbedtls_ssl_handshake() returned -0x%04X", -status);

            }
            /* ssl.h only pulls in x509.h when certificate support is built in. */
#if defined(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
            if (status == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                return HTTP_CLIENT_E_SSL_TRUST ;

            }
#endif
            /* BAD_CERTIFICATE: mbedTLS refused the certificate itself, its
             * key usage or curve for example, even if its chain verified.
             * CA_CHAIN_REQUIRED: verification is required but no CA is set. */
            if ((status == MBEDTLS_ERR_SSL_BAD_CERTIFICATE) ||
                (status == MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED)) {
                return HTTP_CLIENT_E_SSL_TRUST ;

            }
            return HTTP_CLIENT_E_CONNECTION ;

        }

    }
#endif
    return EOK ;

}

/**
 * @brief   httpclient_set_hostname
 * @details Set the hostname to be used in the http request headers. Is not set
 *          the IP address is used.
 * @notes   Typically used for virtual servers.
 *
 * @param[in] client
 * @param[in] hostname

 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 *
 * @http
 */
int32_t
httpclient_set_hostname (HTTP_CLIENT_T* client, const char* hostname)
{
    strncpy (client->hostname, hostname, MAX_HOSTNAME_LEN-1) ;
    client->hostname[MAX_HOSTNAME_LEN-1] = '\0';

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
 * @retval HTTP_CLIENT_E_LENGTH         Does not fit MAX_CONTENT_TYPE_LEN including
 *                                      the terminator, the current value is kept.
 * @retval HTTP_CLIENT_E_HEADER         Contains CR or LF, which would inject header
 *                                      lines, the current value is kept.
 *
 * @http
 */
int32_t
httpclient_set_content_type (HTTP_CLIENT_T* client, const char* content_type)
{
    size_t len ;

    if (!content_type || !content_type[0]) {
        client->content_type[0] = '\0' ;
        return HTTP_CLIENT_E_OK ;

    }

    for (len = 0; content_type[len] && (len < MAX_CONTENT_TYPE_LEN); len++) {
        if ((content_type[len] == '\r') || (content_type[len] == '\n')) {
            return HTTP_CLIENT_E_HEADER ;

        }
    }

    if (len >= MAX_CONTENT_TYPE_LEN) {
        return HTTP_CLIENT_E_LENGTH ;

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

    if (client->socket < 0) {
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

}


/**
 * @brief httpclient_write
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length
 *
 * @return                              Number of bytes sent.
 * @retval HTTP_CLIENT_E_ERROR          error occured during send.
 *
 * @http
 */
static int32_t
httpclient_write (HTTP_CLIENT_T* client, uint8_t* buffer, uint32_t length, uint32_t timeout)
{
    int32_t sent_bytes = 0;
    uint32_t total = 0 ;

    setsockopt (client->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    while (length) {
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
        if (client->ssl) {
            sent_bytes = mbedtls_ssl_write ((mbedtls_ssl_context *)client->ssl, (unsigned char*)&buffer[total], length) ;
        } else
#endif
        {
            sent_bytes = send (client->socket, (unsigned char*)&buffer[total], length, 0);

        }

        if (sent_bytes <= 0) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "httpserver_write : ERROR: socket 0x%x sent_bytes %d", client->socket, sent_bytes);
            return HTTP_CLIENT_E_CONNECTION ;

        } else {
            total += sent_bytes ;
            if (length >= (unsigned int)sent_bytes) length -= sent_bytes ;
            else break ;

        }

    }

    return total ;
}

/**
 * @brief httpclient_write_chunked
 * @details Sends buffer as one chunk: size line, data and CRLF. A length of
 *          0 sends the last chunk.
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length
 *
 * @return                              Number of data bytes sent, 0 for the last chunk.
 * @retval HTTP_CLIENT_E_CONNECTION     error occurred during send.
 *
 * @http
 */
static int32_t
httpclient_write_chunked (HTTP_CLIENT_T* client, uint8_t* buffer, uint32_t length, uint32_t timeout)
{
    int32_t sent_bytes = 0;
    unsigned int i ;
    uint32_t total = 0 ;
    char chunkhead[16] ;

    setsockopt (client->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    i = snprintf (chunkhead, 16, "%x\r\n", (unsigned int)length) ;
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (client->ssl) {
        sent_bytes = mbedtls_ssl_write ((mbedtls_ssl_context *)client->ssl,
                    (const unsigned char *)chunkhead, i) ;
    } else
#endif
    {
        sent_bytes = send (client->socket, chunkhead, i, 0);
    }
    if (sent_bytes <= 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTP  :E: chunk head socket 0x%x sent_bytes %d",
                client->socket, sent_bytes);
        return HTTP_CLIENT_E_CONNECTION ;

    }

    while (length) {

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
        if (client->ssl) {
            sent_bytes = mbedtls_ssl_write ((mbedtls_ssl_context *)client->ssl,
            		(unsigned char*)&buffer[total], length) ;
        } else
#endif
        {
            sent_bytes = send (client->socket, (unsigned char*)&buffer[total], length, 0);
        }

        if (sent_bytes <= 0) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "httpserver_write : ERROR: socket 0x%x sent_bytes %d",
					client->socket, sent_bytes);
            return HTTP_CLIENT_E_CONNECTION ;

        } else {
            total += sent_bytes ;
            if (length >= (unsigned int)sent_bytes) length -= sent_bytes ;
            else break ;

        }
    }
    /* Without its CRLF the chunk, and for the last chunk the body, is not
     * complete, so the server would wait for it instead of answering. */
    if (httpclient_write (client, (uint8_t*)"\r\n", 2, timeout) != 2) {
        return HTTP_CLIENT_E_CONNECTION ;

    }
    return total ;
}

static int32_t
socket_send(uint32_t ctx, const void* data, int32_t len)
{
    return send ((int)ctx, data, len, 0);
}

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
static int32_t
ssl_socket_send(uint32_t ctx, const  void* data, int32_t len)
{
    return mbedtls_ssl_write ((mbedtls_ssl_context *)ctx, data, len);
}
#endif

/**
 * @brief httpclient_write
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length
 *
 * @return                              Number of bytes sent.
 * @retval HTTP_CLIENT_E_ERROR          error occured during send.
 *
 * @http
 */
static int32_t
httpclient_write_stream (HTTP_CLIENT_T* client, HTTP_STREAM_NEXT_T stream, uint32_t parm, int32_t len, uint32_t timeout)
{
    int32_t sent_bytes = 0;
    int32_t total = 0 ;

    setsockopt (client->socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

    do {
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
        if (client->ssl) {
            sent_bytes = stream((HTTP_STREAM_WRITE)ssl_socket_send,
                    (uint32_t) client->ssl, parm, len-total) ;

        } else
#endif
        {
            sent_bytes = stream((HTTP_STREAM_WRITE)socket_send,
                    (uint32_t) client->socket, parm, len-total) ;
 
         }

        if (sent_bytes <= 0) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "httpserver_write : ERROR: socket 0x%x sent_bytes %d", 
                    client->socket, sent_bytes);
            return HTTP_CLIENT_E_CONNECTION ;

        } else {
            total += sent_bytes ;

        }
    } while (total < len) ;

    return total ;
}

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
    (void) res ;

    if (FD_ISSET(client->socket, &fdread)) {
        return EOK ;

    }
    if (FD_ISSET(client->socket, &fdex)) {
           DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                        "HTTP  : : read select failed with exception");
            return HTTP_CLIENT_E_CONNECTION ;

    }

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTP  : : read select failed with %d after %d", res, timeout);

    return HTTP_CLIENT_E_ERROR ;
}

/**
 * @brief httpclient_read
 *
 * @param[in] client
 * @param[in] buffer
 * @param[in] length
 *
 * @return                              Number of bytes read.
 * @retval HTTP_CLIENT_E_ERROR          error occurred during read.
 *
 * @http
 */
static int32_t
httpclient_read (HTTP_CLIENT_T* client, void* buffer, uint32_t length, uint32_t timeout)
{
    int32_t received = EOK ;

#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (client->ssl) {
        if (!mbedtls_ssl_get_bytes_avail ((mbedtls_ssl_context *)client->ssl)) {
            received = httpclient_wait_read (client, timeout) ;
        }
        while (received == EOK) {
            received = mbedtls_ssl_read ((mbedtls_ssl_context *)client->ssl, buffer, length) ;
            if (received == MBEDTLS_ERR_SSL_WANT_READ) {
                received = httpclient_wait_read (client, timeout) ;
            } else {
                break ;
            }

        }
    } else
#endif
    {
        received = httpclient_wait_read (client, timeout) ;
        if (received == EOK) {
            received = recv (client->socket, buffer, length, 0);

        }

    }

    if(received <= 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                "HTTP  : : httpclient_read  %d (-0x%x)", received, -received);

    }

    return received ;
}

/**
 * @brief   httpclient_basic_auth
 * @details Base64 encodes "username:password" for an Authorization: Basic header
 *          into the tail of the request buffer, so no second buffer is needed.
 *          base64_encode() does not terminate its output, so a NUL is added
 *          after it. The request must then be formatted into the first *room
 *          bytes only: snprintf() would otherwise write over the encoding while
 *          it is still reading it.
 *
 * @param[in] buffer                    request buffer of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH bytes
 * @param[in] credentials               username:password, not empty
 * @param[out] room                     bytes in front of the encoding, for the request
 *
 * @return                              The NUL-terminated encoding inside buffer,
 *                                      0 when it does not fit.
 *
 * @http
 */
static char*
httpclient_basic_auth (char* buffer, const char *credentials, size_t* room)
{
    size_t  len = strlen (credentials) ;
    size_t  enc_len ;
    char*   enc ;

    /* Checked before BASE64_OUTPUT_LENGTH() so a huge length cannot wrap. */
    if (len >= HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH) {
        return 0 ;
    }
    enc_len = BASE64_OUTPUT_LENGTH(len) ;
    if (enc_len + 1 >= HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH) {
        return 0 ;
    }

    enc = buffer + HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH - enc_len - 1 ;
    base64_encode (credentials, len, enc, enc_len) ;
    enc[enc_len] = '\0' ;
    *room = HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH - enc_len - 1 ;

    return enc ;
}

/**
 * @brief   httpclient_get
 * @details Starts a HTTP GET request. The response should be read with httpclient_read_response.
 * @note    Buffer including the HTTP is limited to a size of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *          Fixed: Fix the buffer size limitation.
 *
 * @param[in] client
 * @param[in] endpoint
 * @param[in] credentials               username:password, or 0 or "" for none
 *
 * @return                              The number of bytes sent or if < 0 it indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          error occurred during send.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 * @retval HTTP_CLIENT_E_LENGTH         The request does not fit HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH.
 *
 * @http
 */
int32_t
httpclient_get (HTTP_CLIENT_T* client, const char* endpoint, const char *credentials)
{
    int32_t         sent_bytes = 0;
    int32_t         count ;
    char*           buffer = 0;
    char* host ;
    size_t          room = HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH ;

    DBG_CHECK_HTTP_CLIENT (client->socket>=0, EFAIL,
                    "HTTP  :E: unexpected - socket not valid!") ;

    if(!httpclient_is_connected(client)) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTP  : : socket closed!");
        return HTTP_CLIENT_E_CONNECTION ;

    }

    buffer = HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH);
    if(buffer == NULL) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: Memory allocation failed");
        return HTTP_CLIENT_E_MEMORY ;
    }

    if (client->hostname[0]) {
        host = client->hostname ;

    } else {
        host = client->host ;

    }

    /* An empty string is no credentials: "Authorization: Basic " with an
     * empty value is not a valid header. */
    if ((credentials == 0) || (credentials[0] == '\0')) {
        count = snprintf((char*)buffer, room,
                            HTTP_GET_REQ, endpoint ? endpoint : "", host);

    } else {
        char* auth = httpclient_basic_auth (buffer, credentials, &room) ;
        if (auth == NULL) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: credentials too long for HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH!");
            HTTP_CLIENT_FREE (buffer) ;
            return HTTP_CLIENT_E_LENGTH ;

        }
        count = snprintf((char*)buffer, room,
                            HTTP_GET_REQ_BASIC, endpoint ? endpoint : "", host, auth);

    }

    if ((count < 0) || ((size_t)count >= room)) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH to small for HTTP headers!");
        HTTP_CLIENT_FREE (buffer) ;
        return HTTP_CLIENT_E_LENGTH ;

    }

    sent_bytes = httpclient_write(client, (unsigned char*)buffer , count, 8000);
    HTTP_CLIENT_FREE (buffer) ;

    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : send %d bytes", sent_bytes);

    return sent_bytes ;
}


/**
 * @brief   httpclient_post
 * @details Starts a HTTP POST request. The response should be read with httpclient_read_response().
 * @note    Buffer including the HTTP is limited to a size of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *          Fixed: Fix the buffer size limitation.
 *
 * @param[in] client
 * @param[in] endpoint
 * @param[in] reponse
 *
 * @return                              The number of bytes sent or if < 0 it indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          error occurred during send.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t
httpclient_post_start (HTTP_CLIENT_T* client, const char* endpoint, int32_t len, const char *credentials, uint32_t chunked)
{
    int32_t         sent_bytes = 0;
    int32_t         count ;
    char*           buffer = 0;
    char* host ;
    const char*     content_type ;
    size_t          room = HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH ;

    DBG_CHECK_HTTP_CLIENT (client->socket>=0, EFAIL,
                    "HTTP  :E: unexpected - socket not valid!") ;

    if(!httpclient_is_connected(client)) {
         DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTP  : : socket closed!");
        return HTTP_CLIENT_E_CONNECTION ;

    }

    buffer = HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH);
    if(buffer == NULL) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: Memory allocation failed");
        return HTTP_CLIENT_E_MEMORY ;

    }

    if (client->hostname[0]) {
        host = client->hostname ;

    } else {
        host = client->host ;

    }

    content_type = client->content_type[0] ? client->content_type : HTTP_POST_DEFAULT_CONTENT_TYPE ;

    /* An empty string is no credentials, as in httpclient_get(). */
    if ((credentials == 0) || (credentials[0] == '\0')) {
        count = snprintf((char*)buffer, room,
                            chunked ? HTTP_POST_CHUNKED_REQ : HTTP_POST_REQ,
                            endpoint ? endpoint : "", host, content_type, (int)len);

    } else {
        char* auth = httpclient_basic_auth (buffer, credentials, &room) ;
        if (auth == NULL) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: credentials too long for HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH!");
            HTTP_CLIENT_FREE (buffer) ;
            return HTTP_CLIENT_E_LENGTH ;

        }
        count = snprintf((char*)buffer, room,
                            chunked ? HTTP_POST_CHUNKED_REQ_BASIC : HTTP_POST_REQ_BASIC,
                            endpoint ? endpoint : "", host, content_type, auth,
                            (int)len);

    }

    if (count <= 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: header error");
        HTTP_CLIENT_FREE (buffer) ;
        return HTTP_CLIENT_E_HEADER ;

    }

    if ((size_t)count >= room) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH to small for HTTP headers!");
        HTTP_CLIENT_FREE (buffer) ;
        return HTTP_CLIENT_E_LENGTH ;

    }

    sent_bytes = httpclient_write(client, (unsigned char*)buffer, count, 8000);
    HTTP_CLIENT_FREE (buffer) ;
    if (sent_bytes < count) {
        return HTTP_CLIENT_E_CONNECTION ;

    }

    return sent_bytes ;
}


/**
 * @brief   httpclient_post
 * @details Starts a HTTP POST request. The response should be read with httpclient_read_response().
 * @note    Buffer including the HTTP is limited to a size of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *          Fixed: Fix the buffer size limitation.
 *
 * @param[in] client
 * @param[in] endpoint
 * @param[in] reponse
 *
 * @return                              The number of bytes sent or if < 0 it indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          error occurred during send.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t
httpclient_post (HTTP_CLIENT_T* client, const char* endpoint, const char* request, int32_t len, const char *credentials)
{
    int32_t status = httpclient_post_start (client, endpoint, len, credentials, 0) ;

    if (status >= 0) {
        status = httpclient_write(client, (unsigned char*)request, len, 8000);

    }

    return status ;
}

/**
 * @brief   httpclient_post
 * @details Starts a HTTP POST request. The response should be read with httpclient_read_response().
 * @note    Buffer including the HTTP is limited to a size of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *          Fixed: Fix the buffer size limitation.
 *
 * @param[in] client
 * @param[in] endpoint
 * @param[in] reponse
 *
 * @return                              The number of bytes sent or if < 0 it indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          error occurred during send.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t
httpclient_post_chunked (HTTP_CLIENT_T* client, const char* endpoint, const char* request, int32_t len, const char *credentials)
{
    int32_t status = httpclient_post_start (client, endpoint, len, credentials, 1) ;

    if ((status >= 0) && (len > 0)) {
        status = httpclient_write_chunked(client, (unsigned char*)request, len, 8000);

    }
    if (status >= 0) {
        status = httpclient_write_chunked(client, (unsigned char*)0, 0, 8000);

    }

    return status ;
}


/**
 * @brief   httpclient_post_stream
 * @details Starts a HTTP POST request. The response should be read with httpclient_read_response().
 * @note    Buffer including the HTTP is limited to a size of HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH (1280).
 *          Fixed: Fix the buffer size limitation.
 *
 * @param[in] client
 * @param[in] endpoint
 * @param[in] reponse
 *
 * @return                              The number of bytes sent or if < 0 it indicates an error.
 * @retval HTTP_CLIENT_E_ERROR          error occurred during send.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t
httpclient_post_stream (HTTP_CLIENT_T* client, const char* endpoint, HTTP_STREAM_NEXT_T stream, uint32_t parm, int32_t len, const char *credentials)
{
    int32_t status = httpclient_post_start (client, endpoint, len, credentials, 0) ;

    if (status >= 0) {
        status = httpclient_write_stream(client, stream, parm, len, 8000) ;

    }

    return status ;
}

/**
 * @brief   httpclient_free_response
 *
 * @param[in] client
 * @param[out] response
 *
 *
 * @http
 */
void
httpclient_free_response (HTTP_CLIENT_T* client, void* response)
{
    if (response) {
        HTTP_CLIENT_FREE (response) ;
        
    }
}

/**
 * @brief   httpclient_parse_retry_after
 * @details Parses the delay-seconds form of a Retry-After header value
 *          (RFC 9110, section 10.2.3). The HTTP-date form would need a wall
 *          clock, so it gives 0, the same as an absent or malformed header.
 *          A delay too large for uint32_t saturates.
 *
 * @param[in] value                     Header value from httpparse_response(),
 *                                      NUL-terminated, leading spaces skipped.
 *
 * @return                              Delay in seconds, 0 if not usable.
 *
 * @http
 */
static uint32_t
httpclient_parse_retry_after (const char* value)
{
    uint32_t seconds = 0 ;

    /* An HTTP-date starts with the day name, so a leading digit tells the forms apart. */
    if (!value || (*value < '0') || (*value > '9')) {
        return 0 ;

    }

    for ( ; (*value >= '0') && (*value <= '9'); value++) {
        uint32_t digit = (uint32_t)(*value - '0') ;

        if (seconds > (UINT32_MAX - digit) / 10) {
            seconds = UINT32_MAX ;

        } else {
            seconds = seconds * 10 + digit ;

        }
    }

    while ((*value == ' ') || (*value == '\t')) {
        value++ ;

    }

    return (*value == '\0') ? seconds : 0 ;
}

/* The body is allocated with one byte more and its length returned as int32_t. */
#if (HTTP_CLIENT_MAX_RESPONSE_LENGTH) >= 0x7FFFFFFF
#error "HTTP_CLIENT_MAX_RESPONSE_LENGTH must be smaller than INT32_MAX"
#endif

/**
 * @brief   httpclient_peer_closed
 * @details Whether a read that returned 'received' <= 0 saw the server close
 *          the connection cleanly: end of stream on a plain socket, or a TLS
 *          close_notify. A TCP close without close_notify over TLS is not
 *          clean, because it may cut the body short (RFC 9112, section 9.8).
 *
 * @param[in] client
 * @param[in] received                  httpclient_read() result, <= 0.
 *
 * @return                              true for a clean close.
 *
 * @http
 */
static bool
httpclient_peer_closed (HTTP_CLIENT_T* client, int32_t received)
{
    if (received == 0) {
        return true ;

    }
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (client->ssl && (received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)) {
        return true ;

    }
#else
    (void) client ;
#endif

    return false ;
}

/**
 * @brief   httpclient_body_error
 * @details The result for a read that returned 'received' <= 0 before the
 *          body was complete. A close by the server cut the body short; any
 *          other failure, a timeout for example, keeps its own code.
 *
 * @param[in] client
 * @param[in] received                  httpclient_read() result, <= 0.
 *
 * @return                              HTTP_CLIENT_E_CONTENT, or received.
 *
 * @http
 */
static int32_t
httpclient_body_error (HTTP_CLIENT_T* client, int32_t received)
{
    return httpclient_peer_closed (client, received) ? HTTP_CLIENT_E_CONTENT : received ;
}

/**
 * @brief   httpclient_parse_content_length
 * @details Unlike sscanf("%u"), refuses a sign and anything but whitespace
 *          after the digits. A value too large for uint32_t saturates, so
 *          the caller refuses it as too long.
 *
 * @param[in] value                     Header value from httpparse_response().
 * @param[out] length
 *
 * @return                              The function status.
 * @retval HTTP_CLIENT_E_OK
 * @retval HTTP_CLIENT_E_HEADER         Not a valid Content-Length.
 *
 * @http
 */
static int32_t
httpclient_parse_content_length (const char* value, uint32_t* length)
{
    uint32_t n = 0 ;

    if ((*value < '0') || (*value > '9')) {
        return HTTP_CLIENT_E_HEADER ;

    }

    for ( ; (*value >= '0') && (*value <= '9'); value++) {
        uint32_t digit = (uint32_t)(*value - '0') ;

        if (n > (UINT32_MAX - digit) / 10) {
            n = UINT32_MAX ;

        } else {
            n = n * 10 + digit ;

        }
    }

    while ((*value == ' ') || (*value == '\t')) {
        value++ ;

    }
    if (*value != '\0') {
        return HTTP_CLIENT_E_HEADER ;

    }

    *length = n ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_parse_chunk_size
 * @details Parses a chunk size line (RFC 9112, section 7.1): hex digits,
 *          optional spaces or tabs, an optional extension, an optional CR,
 *          then the '\n' at end. Unlike sscanf("%x"), it never reads past end,
 *          since the receive buffer is not NUL-terminated there, and it skips
 *          no leading whitespace, so a mis-framed body is not read as a size.
 *          A value too large for uint32_t saturates, so it is refused as too
 *          long rather than wrapping. Extensions are skipped, as this client
 *          uses none.
 *
 * @param[in] line                      First byte of the size line.
 * @param[in] end                       The '\n' that ends it.
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
httpclient_parse_chunk_size (const char* line, const char* end, int32_t* length)
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

        if (n > (UINT32_MAX >> 4)) {
            n = UINT32_MAX ;

        } else {
            n = (n << 4) | digit ;

        }
    }

    if (q == line) {
        return HTTP_CLIENT_E_CONTENT ;

    }

    while ((q < end) && ((*q == ' ') || (*q == '\t'))) {
        q++ ;

    }
    if ((q < end) && (*q == ';')) {
        q = end ;

    }
    if ((q < end) && (*q == '\r')) {
        q++ ;

    }
    if (q != end) {
        return HTTP_CLIENT_E_CONTENT ;

    }

    if (n > INT32_MAX) {
        return HTTP_CLIENT_E_LENGTH ;

    }

    *length = (int32_t)n ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_read_response_alloc
 * @details Reads the response from a get or a post into one allocated buffer,
 *          whatever the framing: Content-Length, chunked, or until the server
 *          closes the connection. It reads through httpclient_read_response_ex()
 *          and httpclient_read_next_ex(), so chunks are decoded by the same
 *          code that streams downloads.
 *          With a Content-Length the buffer is allocated once at that size.
 *          Otherwise it starts at 'initial' bytes and grows by 'grow' as the
 *          body arrives. qoraal has no realloc, so growing copies the body into
 *          a new buffer, and the old and new buffer exist together for a
 *          moment.
 *          The response is NUL-terminated after the body, and
 *          client->retry_after is set from a Retry-After header (0 if none).
 * @note    The timeout applies to each read, not to the whole response.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] response                 The body, to be freed with httpclient_free_response().
 *                                      0 when there is no body, and on error.
 * @param[out] status                   HTTP status, like 200 for OK. Set as soon as the
 *                                      header is parsed, also when the body then fails.
 * @param[in] initial                   First buffer size when there is no Content-Length,
 *                                      0 for HTTP_CLIENT_RESPONSE_INITIAL_LENGTH.
 * @param[in] grow                      Bytes added each time the buffer is full, 0 to
 *                                      double it.
 * @param[in] max                       Largest body accepted, 0 for
 *                                      HTTP_CLIENT_MAX_RESPONSE_LENGTH.
 *
 * @return                              Number of bytes in response, or < 0 for an error:
 *                                      one listed here, or the read error (an mbedTLS code
 *                                      over TLS, such as MBEDTLS_ERR_NET_CONN_RESET for a
 *                                      close without close_notify; HTTP_CLIENT_E_ERROR for
 *                                      a timeout).
 * @retval HTTP_CLIENT_E_CONNECTION     The server closed the connection (a TCP EOF, or a TLS
 *                                      close_notify) before the header was complete, or
 *                                      select reported an exception on the socket.
 * @retval HTTP_CLIENT_E_HEADER         Error parsing HTTP header, or invalid Content-Length.
 * @retval HTTP_CLIENT_E_CONTENT        The server closed the connection (a TCP EOF, or a TLS
 *                                      close_notify) before the body was complete, or the
 *                                      chunked encoding is malformed.
 * @retval HTTP_CLIENT_E_LENGTH         Header larger than HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH,
 *                                      body larger than max, or a chunk size line or chunk
 *                                      size too large (see httpclient_read_next_ex()).
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t
httpclient_read_response_alloc (HTTP_CLIENT_T* client, uint32_t timeout, char** response, int32_t* status,
                                uint32_t initial, uint32_t grow, uint32_t max)
{
    char*           body ;
    uint8_t*        data ;
    uint32_t        size ;
    uint32_t        length = 0 ;
    int32_t         res ;

    DBG_CHECK_HTTP_CLIENT (response, EFAIL,
                    "HTTP  :E: unexpected - response not valid!") ;

    *response = 0 ;

    if (max == 0) {
        max = HTTP_CLIENT_MAX_RESPONSE_LENGTH ;

    }
    if (max >= INT32_MAX) {
        /* The length is returned as int32_t, with one byte more allocated. */
        max = INT32_MAX - 1 ;

    }

    res = httpclient_read_response_ex (client, timeout, status) ;
    if (res != HTTP_CLIENT_E_OK) {
        return res ;

    }

    if (!client->chunked && (client->content_length >= 0)) {
        if ((uint32_t)client->content_length > max) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTP  :E: content length %d larger than %u",
                (int)client->content_length, (unsigned)max);
            httpclient_read_complete_ex (client) ;
            return HTTP_CLIENT_E_LENGTH ;

        }
        size = (uint32_t)client->content_length ;

    } else {
        size = initial ? initial : HTTP_CLIENT_RESPONSE_INITIAL_LENGTH ;
        if (size > max) {
            size = max ;

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

        if ((uint32_t)res > max - length) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTP  :E: response larger than %u", (unsigned)max);
            res = HTTP_CLIENT_E_LENGTH ;
            break ;

        }

        if ((uint32_t)res > size - length) {
            uint32_t    need = length + (uint32_t)res ;
            uint32_t    next ;
            char*       p ;

            /* size <= max < INT32_MAX, so doubling cannot wrap. */
            if (grow == 0) {
                next = 2 * size ;
            } else if (grow > max - size) {
                next = max ;
            } else {
                next = size + grow ;
            }
            if (next < need) {
                next = need ;
            }
            if (next > max) {
                next = max ;
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

    if ((res < 0) || (length == 0)) {
        HTTP_CLIENT_FREE(body) ;
        return (res < 0) ? res : 0 ;

    }

    body[length] = '\0' ;
    *response = body ;

    return (int32_t)length ;
}

/**
 * @brief   httpclient_read_response
 * @details Reads the response from a get or a post: httpclient_read_response_alloc()
 *          with the default buffer sizes.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] response                 The body, to be freed with httpclient_free_response().
 *                                      0 when there is no body, and on error.
 * @param[out] status                   HTTP status, like 200 for OK
 *
 * @return                              Number of bytes in response or < 0 indicates an error;
 *                                      see httpclient_read_response_alloc().
 *
 * @http
 */
int32_t
httpclient_read_response (HTTP_CLIENT_T* client, uint32_t timeout, char** response, int32_t* status)
{
    return httpclient_read_response_alloc (client, timeout, response, status, 0, 0, 0) ;
}

/**
 * @brief   httpclient_read_response_ex
 * @details Reads the response header from a get or a post.
 *          client->retry_after is set from a Retry-After header (0 if none).
 * @note    If the response has content it should be read with calls to httpclient_read_next_ex.
 *          A 1xx, 204 or 304 response has no body. Otherwise the body is
 *          framed by chunked encoding or Content-Length, and without either it
 *          runs until the server closes the connection.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] status                   HTTP status, like 200 for OK
 *
 * @return                              HTTP_CLIENT_E_OK, or < 0 for an error: one listed
 *                                      here, or the read error (an mbedTLS code over TLS,
 *                                      such as MBEDTLS_ERR_NET_CONN_RESET for a close
 *                                      without close_notify).
 * @retval HTTP_CLIENT_E_ERROR          Error occurred during select, possible timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     The server closed the connection (a TCP EOF, or a TLS
 *                                      close_notify) before the header was complete, or
 *                                      select reported an exception on the socket.
 * @retval HTTP_CLIENT_E_HEADER         Error parsing HTTP header, or invalid Content-Length.
 * @retval HTTP_CLIENT_E_LENGTH         Header larger than HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t
httpclient_read_response_ex (HTTP_CLIENT_T* client, uint32_t timeout, int32_t* status)
{
#define READ_RESPONSE_PEEK          0
    int32_t         received ;
#if READ_RESPONSE_PEEK
    int32_t         received_left = 0 ;
#endif
    uint8_t*        content ;
    int             offset = 0 ;
    HTTP_HEADER_T headers[] = {
            {HTTP_HEADER_KEY_CONTENT_TYPE, 0},
            {HTTP_HEADER_KEY_CONTENT_LENGTH, 0},
            {HTTP_HEADER_KEY_TRANSFER_ENCODING, 0},
            {HTTP_HEADER_KEY_AUTHENTICATE, 0},
            {HTTP_HEADER_KEY_RETRY_AFTER, 0},
    } ;

    client->retry_after = 0 ;

    DBG_CHECK_HTTP_CLIENT (client->socket>=0, EFAIL,
                    "HTTP  :E: unexpected - socket not valid.!") ;
    DBG_CHECK_HTTP_CLIENT (status, EFAIL,
                    "HTTP  :E: unexpected - status not valid.!") ;
    DBG_CHECK_HTTP_CLIENT (client->read_buffer == 0, EFAIL,
                    "HTTP  :E: unexpected - client->read_buffer.!") ;

    *status = 0 ;
    client->payload_length = 0 ;
    client->content_length = 0 ;
    client->chunked = 0 ;
    client->chunk_length = 0 ;
    client->read = 0 ;
    client->read_buffer = (unsigned char*)HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH + HTTP_CLIENT_MAX_RECV_BUFFER_PADDING) ;
    if (client->read_buffer == 0) {
        return HTTP_CLIENT_E_MEMORY ;
    }

    do {

        received = httpclient_read(client, &client->read_buffer[offset], HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH - offset, timeout); //1550 is RX size
        if(received <= 0) {

            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTP  : : httpclient_read_response_ex  %d",
                    received);

            HTTP_CLIENT_FREE(client->read_buffer) ;
            client->read_buffer = 0 ;
            /* A close gives 0, which a caller would take for HTTP_CLIENT_E_OK. */
            return httpclient_peer_closed (client, received) ? HTTP_CLIENT_E_CONNECTION : received ;
        }

        client->total_read += received ;
        offset += received ;
        /* Fits in the padding; keeps the header NUL-terminated for the sscanf() in the parser. */
        client->read_buffer[offset] = '\0' ;
        if (strnstr((char*)client->read_buffer, "\r\n\r\n", offset)) {
            break ;
        }
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : incomplete header received %d bytes",
                    offset);

        if (offset >= HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH) {

            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTP  :E: HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH to small for HTTP headers!");

            HTTP_CLIENT_FREE(client->read_buffer) ;
            client->read_buffer = 0 ;
            return HTTP_CLIENT_E_LENGTH ;
        }

    } while (1) ;


    *status = httpparse_response((char*)client->read_buffer, offset, headers,
            sizeof(headers)/sizeof(headers[0]), (char**)&content) ;
    if ((*status == 0) || (content == 0)) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTP  :W: incomplete header received %d bytes",
                    offset);
        HTTP_CLIENT_FREE(client->read_buffer) ;
        client->read_buffer = 0 ;
        return HTTP_CLIENT_E_HEADER ;

    }

    /* The header values point into read_buffer, parse before the memmove() below. */
    client->retry_after = httpclient_parse_retry_after (headers[4].value) ;

    if (((*status >= 100) && (*status < 200)) || (*status == 204) || (*status == 304)) {
        /* RFC 9112, section 6.3: never a body, whatever the header says. */
        client->content_length = 0 ;

    } else if (headers[2].value && (strnicmp(headers[2].value, "chunked", 7) == 0)) {
        /* Transfer-Encoding overrides a Content-Length. */
        client->chunked = 1 ;

    } else if (headers[1].value) {
        uint32_t length ;

        if ((httpclient_parse_content_length (headers[1].value, &length) != HTTP_CLIENT_E_OK) ||
                (length > INT32_MAX)) {
            DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTP  :E: invalid content length");
            HTTP_CLIENT_FREE(client->read_buffer) ;
            client->read_buffer = 0 ;
            return HTTP_CLIENT_E_HEADER ;

        }
        client->content_length = (int32_t)length ;

    } else {
        /* Neither: the body runs until the server closes the connection.
         * httpclient_read_next_ex() takes a negative length for that. */
        client->content_length = -1 ;

    }

    client->payload_length = offset - (content - client->read_buffer) ;
    memmove (client->read_buffer, content, client->payload_length) ;

    return HTTP_CLIENT_E_OK ;
}

/**
 * @brief   httpclient_read_next_ex
 * @details Reads the next part of the content from a get or a post.
 * @note    recv points into the client's receive buffer and is valid until the
 *          next call. When all data is read httpclient_read_complete_ex()
 *          should be called.
 *
 * @param[in] client
 * @param[in] timeout
 * @param[out] recv                 Pointer to received data
 *
 * @return                              Number of bytes in content OR 0 for no more available OR < 0 for error:
 *                                      one listed here, or the read error (an mbedTLS code
 *                                      over TLS, such as MBEDTLS_ERR_NET_CONN_RESET for a
 *                                      close without close_notify).
 * @retval HTTP_CLIENT_E_ERROR          Error occurred during select, possible timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     select reported an exception on the socket.
 * @retval HTTP_CLIENT_E_CONTENT        The server closed the connection (a TCP EOF, or a TLS
 *                                      close_notify) before a body with a Content-Length or
 *                                      a chunked body was complete, or the chunked encoding
 *                                      is malformed.
 * @retval HTTP_CLIENT_E_LENGTH         A chunk size above INT32_MAX, or a chunk size line
 *                                      that does not fit HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH.
 *
 * @http
 */
int32_t
httpclient_read_next_ex (HTTP_CLIENT_T* client, uint32_t timeout, uint8_t** recv)
{
    int32_t received ;

    if (!client->chunked) {
        /* content_length < 0: no Content-Length, the body ends when the
         * server closes the connection. */
        bool        to_close = client->content_length < 0 ;
        uint32_t    left = to_close ? HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH :
                                (uint32_t)(client->content_length - client->read) ;

        if (!to_close && (client->read >= client->content_length)) {
            return 0 ;
        }

        if (!client->payload_length) {
            /* Not past the body: on a kept-alive connection the next bytes
             * belong to the next response. */
            received = httpclient_read(client, client->read_buffer,
                            (left < HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH) ? left : HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH,
                            timeout); //1550 is RX size
            if (received <= 0) {
                if (to_close && httpclient_peer_closed (client, received)) {
                    return 0 ;

                }
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                        "HTTP  : : httpclient_read_next_ex %d",
                        received);
                return httpclient_body_error (client, received) ;

            }
            client->payload_length = received ;

        }

        /* Body bytes that came with the header can run past a short body. */
        received = ((uint32_t)client->payload_length > left) ? (int32_t)left : client->payload_length ;

        client->total_read += received ;
        client->read += received ;
        client->payload_length = 0 ;
        *recv = client->read_buffer ;

    } else {

        client->chunked++ ;

        if (client->read) {
            client->payload_length -= client->read ;
            memmove (client->read_buffer, &client->read_buffer[client->read], client->payload_length) ;

            if (!client->chunk_length) {
                /* The chunk data ends with CRLF. Anything else means the
                 * chunk size did not match the data sent. */
                while (client->payload_length < 2) {
                    received = httpclient_read(client, &client->read_buffer[client->payload_length],
                            HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH - client->payload_length, timeout); //1550 is RX size
                    if (received <= 0) {
                        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                                "HTTP  : : httpclient_read_next_ex failed with %d",
                                        received);
                        return httpclient_body_error (client, received) ;

                    }
                    client->payload_length += received ;

                }

                if ((client->read_buffer[0] != '\r') || (client->read_buffer[1] != '\n')) {
                    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                            "HTTP  : : httpclient_read_next_ex no CRLF after chunk");
                    return HTTP_CLIENT_E_CONTENT ;

                }
                client->payload_length -= 2 ;
                memmove (client->read_buffer, &client->read_buffer[2], client->payload_length) ;

            }

        }

        if (!client->chunk_length) {
            int32_t res ;

            char *p = strnchr ((char*)client->read_buffer,'\n', client->payload_length)  ;
            while (!p) {
                /* Chunk extensions make the line any length, so only the
                 * buffer bounds it. */
                if (client->payload_length >= HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH) {
                    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                            "HTTP  : : httpclient_read_next_ex chunk size line too long");
                    return HTTP_CLIENT_E_LENGTH ;

                }
                received = httpclient_read(client, &client->read_buffer[client->payload_length],
                        HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH - client->payload_length, timeout); //1550 is RX size

                if (received <= 0) {
                    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                            "HTTP  : : httpclient_read_next_ex received %d",
                                    received);
                    return httpclient_body_error (client, received) ;

                }
                client->payload_length += received ;

                p = strnchr ((char*)client->read_buffer,'\n', client->payload_length)  ;

            }

            res = httpclient_parse_chunk_size ((char*)client->read_buffer, p, &client->chunk_length) ;
            if (res != HTTP_CLIENT_E_OK) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                        "HTTP  : : httpclient_read_next_ex invalid chunk size line %d", res);
                return res ;

            }
            if (client->chunk_length == 0) {
                return 0 ;
            }

            p++ ;
            uint32_t offset = (uint32_t) ((uintptr_t)p - (uintptr_t)client->read_buffer) ;
            client->payload_length -= offset ;
            memmove (client->read_buffer, &client->read_buffer[offset], client->payload_length) ;

        }


        if (!client->payload_length) {
            received = httpclient_read(client, client->read_buffer, HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH, timeout); //1550 is RX size
            if (received <= 0) {
                DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_REPORT,
                        "HTTP  : : httpclient_read_next_ex payload_length %d",
                        received);
                return httpclient_body_error (client, received) ;

            }
            client->payload_length = received ;

        }

        if (client->payload_length < client->chunk_length) {
            client->read = client->payload_length ;

        } else {
            client->read = client->chunk_length ;
            
        }

        client->total_read += client->read ;
        *recv = client->read_buffer ;
        client->chunk_length -= client->read ;

        received = client->read ;

    }

    if (received > HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_ERROR,
            "HTTP  :E: read %d", received);

    }

    return received ;
}

/**
 * @brief   httpclient_read_complete_ex
 * @details Called at the end of a get ot post.
 *
 * @param[in] client
 *
 * @return                              EOK.
 *
 * @http
 */
int32_t
httpclient_read_complete_ex (HTTP_CLIENT_T* client)
{
    DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_INFO,
        "HTTP  : : read complete %d bytes in %d chunks",
                client->read, client->chunked);


    if (client->read_buffer) {
        HTTP_CLIENT_FREE(client->read_buffer) ;
        client->read_buffer = 0 ;
    }

    return EOK ;
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
 * @return 								The number of bytes sent or < 0 for error codes.
 * @retval HTTP_CLIENT_E_ERROR         	Error occurred during send.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t httpclient_websock_get(HTTP_CLIENT_T* client, const char* endpoint, const char* credentials)
{
    int32_t sent_bytes = 0;
    int32_t count;
    char* buffer = 0;
    char* host;
    char key[BASE64_OUTPUT_LENGTH(16) + 1];
    size_t room = HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH;

    DBG_CHECK_HTTP_CLIENT(client->socket, EFAIL, "httpclient_websock_get : CHECK : unexpected - socket not valid.\r\n!");
    DBG_CHECK_HTTP_CLIENT(endpoint, EFAIL, "httpclient_websock_get : CHECK : unexpected - endpoint not valid.\r\n!");

    buffer = HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH);
    if (buffer == NULL)
    {
        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_ERROR, "HTTP  :E: Memory allocation failed\r\n");
        return HTTP_CLIENT_E_MEMORY;
    }

    /*
     * If routed by the server use the httpclient_set_hostname()
     * to set the specific hostname before calling this function.
     */
    if (client->hostname[0])
    {
        host = client->hostname;
    }
    else
    {
        host = client->host;
    }

    /*
     * Create the Sec-WebSocket-Key for this connection
     */
    for (count = 0; count < 4; count++)
    {
        client->key[count] = HTTP_CLIENT_RAND();
    }
    base64_encode((char*)client->key, 16, key, BASE64_OUTPUT_LENGTH(16));
    key[BASE64_OUTPUT_LENGTH(16)] = '\0';

    /*
     * An empty string is no credentials, as in httpclient_get(). The
     * encoding goes into the tail of the buffer rather than into a stack
     * array sized by the credentials.
     */
    if ((credentials == 0) || (credentials[0] == '\0'))
    {
        count = snprintf((char*)buffer, room, HTTP_WEBSOCKET_REQ, endpoint, host, key);
    }
    else
    {
        char* auth = httpclient_basic_auth(buffer, credentials, &room);
        if (auth == NULL)
        {
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_ERROR,
                                    "HTTP  :E: credentials too long for HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH!\r\n");
            HTTP_CLIENT_FREE(buffer);
            return HTTP_CLIENT_E_LENGTH;
        }
        count = snprintf((char*)buffer, room, HTTP_WEBSOCKET_REQ_BASIC, endpoint, host, key, auth);
    }

    /*
     * If the header was too big just return an error.
     */
    if ((count < 0) || ((size_t)count >= room))
    {
        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_ERROR,
                                "HTTP  :E: HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH to small for HTTP headers!\r\n");
        HTTP_CLIENT_FREE(buffer);
        return HTTP_CLIENT_E_LENGTH;
    }

    /*
     * Send the request to the server.
     */
    sent_bytes = httpclient_write(client, (unsigned char*)buffer, count, 2000);
    HTTP_CLIENT_FREE(buffer);

    DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_INFO, "HTTP  : : send %d bytes\r\n", sent_bytes);

    return sent_bytes;
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
 * @retval HTTP_CLIENT_E_ERROR         	Error occurred during select, possible timeout.
 * @retval HTTP_CLIENT_E_CONNECTION     Socket was closed.
 * @retval HTTP_CLIENT_E_HEADER         Error parsing HTTP header.
 * @retval HTTP_CLIENT_E_CONTENT        Error reading content, unexpected length.
 * @retval HTTP_CLIENT_E_MEMORY         Failed to allocate a receive buffer.
 *
 * @http
 */
int32_t httpclient_websock_read_response(HTTP_CLIENT_T* client, uint32_t timeout)
{
    int32_t received;
    char* content;
    int rx_bytes = 0;
    int status;
    char* header;
    HTTP_HEADER_T headers[] = {
        {HTTP_HEADER_KEY_CONTENT_TYPE, 0},    {HTTP_HEADER_KEY_CONTENT_LENGTH, 0}, {HTTP_HEADER_KEY_CONNECTION, 0},
        {HTTP_HEADER_KEY_WEBSOCK_UPGRADE, 0}, {HTTP_HEADER_KEY_WEBSOCK_ACCEPT, 0}, {HTTP_HEADER_KEY_WEBSOCK_PROT, 0},
        //{HTTP_HEADER_KEY_TRANSFER_ENCODING, 0},
        //{HTTP_HEADER_KEY_AUTHENTICATE, 0},
    };

    DBG_CHECK_HTTP_CLIENT(client->socket, EFAIL,
                          "httpclient_websock_read_response : CHECK : unexpected - socket not valid.\r\n!");

    /*
     * Preallocate a buffer to receive the header in.
     */
    header = (char*)HTTP_CLIENT_MALLOC(HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH);
    if (header == 0)
    {
        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_INFO, "HTTP  : : out of memory \r\n");
        return HTTP_CLIENT_E_MEMORY;
    }

    /*
     * Read the complete header up including the "\r\n\r\n" marking the start of the content if any.
     */
    do
    {

        received = httpclient_read(client, (void*)&header[rx_bytes], HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH - rx_bytes,
                                   timeout);  // 1550 is RX size
        if (received <= 0)
        {
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_WARNING, "HTTP  :E: httpclient_read failed %d\r\n", received);
            HTTP_CLIENT_FREE(header);
            return received;
        }
        client->total_read += received;

        rx_bytes += received;
        if (strnstr(header, "\r\n\r\n", rx_bytes)) break;

        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  : : incomplete header received %d bytes\r\n",
                                rx_bytes);

        if (rx_bytes >= HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH)
        {
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_ERROR,
                                    "HTTP  :E: HTTP_CLIENT_MAX_HEADER_LENGTH to small for HTTP headers!\r\n");
            HTTP_CLIENT_FREE(header);
            return HTTP_CLIENT_E_LENGTH;
        }

    } while (rx_bytes < HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH);

    /*
     * Parse the received headers.
     */
    status = httpparse_response(header, rx_bytes, headers, sizeof(headers) / sizeof(headers[0]), &content);

    /*
     * Process the returned status
     */
    if (status <= 0)
    {
        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_ERROR, "HTTP  :E: status code %d\r\n", status);
        status = HTTP_CLIENT_E_HEADER;
    }
    else if (status != 101)
    {
        // ToDo: This if clause (for status != 101) is not tested

        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_WARNING, "HTTP  :W: error status code %d\r\n", status);
        DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_INFO, "hHTTP  : : : %s\r\n", content);

        /*
         * This is a failure. Read the content (if any) to empty the socket.
         */
        /*
   if (headers[1].value) { // content-length
       uint32_t length ;

               if (sscanf(headers[1].value, "%u", (unsigned int*)&length)) {
                       received = httpclient_read(client, (void**)&buffer, length); // peek to find the length of the
   header while ((received > 0) && (length > 0)) { length -= received ; zero_copy_free(buffer) ; received =
   httpclient_read(client, (void**)&buffer, 0); // peek to find the length of the header
                       }

               }

   }
       */
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
        if (!headers[2].value || strnicmp(headers[2].value, "Upgrade", 7))
        {  // Connection header
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG,
                                    "HTTP  :E: httpclient_websock_read_response connection header invalid!");
            status = HTTP_CLIENT_E_HEADER;
        }
        if (!headers[3].value || strnicmp(headers[3].value, "websocket", 9))
        {  // Upgrade header
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG,
                                    "HTTP  :E: httpclient_websock_read_response upgrade header invalid!");
            status = HTTP_CLIENT_E_HEADER;
        }
        if (!headers[4].value)
        {  // Sec-WebSocket-Accept header
            DBG_MESSAGE_HTTP_CLIENT(DBG_MESSAGE_SEVERITY_LOG, "HTTP  :E: Sec-WebSocket-Accept header missing!");
            status = HTTP_CLIENT_E_HEADER;
        }
        else
        {
            // ToDo: Validate correct SHA-1 hash in Sec-WebSocket-Accept header
        }
        if (!headers[5].value)
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

    HTTP_CLIENT_FREE(header);

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
        return status;
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

    if (payload_length == E_TIMEOUT)
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
                    return read_len;
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
            // return A_SOCK_INVALID so the user can shutdown the socket.
            payload_length = -2;  // A_SOCK_INVALID ;
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


