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



#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h> 
#include <stdlib.h>

#include "qoraal/qoraal.h"
#include "qoraal-http/config.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpclient.h"
#include "qoraal-http/httpparse.h"
#include "qoraal-http/httpserver.h"
#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
#include "qoraal-http/mbedtls/mbedtlsutils.h"
#endif


const   HTTP_HEADER_T _http_headers[]   = {
        {"Server", HTTP_SERVER_NAME} ,
        {"Connection", "Keep-Alive"} ,
#if defined HTTP_SERVER_KEEPALIVE_HEADER
        {"Keep-Alive", HTTP_SERVER_KEEPALIVE_HEADER} ,
#endif
};

const   HTTP_HEADER_T _http_headers_chunked[]   = {
        {"Transfer-Encoding", "chunked"} ,
};

#define HTTPSERVER_RESPONSE_HEADER              \
        "HTTP/1.1 %u\r\n"


/**
 * @brief   httpserver_init
 * @details Initialize the HTTP server by creating and binding a server socket.
 *
 * @param[in] port   Port number to bind the server socket.
 * @param[in] ssl    Enable or disable SSL.
 *
 * @return                          The server socket descriptor (>= 0) or an error code.
 * @retval HTTP_SERVER_E_OK         Server initialized successfully.
 * @retval HTTP_SERVER_E_ERROR      Socket creation or binding error.
 *
 * @http
 */
int
httpserver_init (uint16_t port)
{
    int32_t     res ;
    struct sockaddr_in address ;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
#if QORAAL_CFG_USE_LWIP
    address.sin_len = sizeof(address);
#endif

    int server_sock = socket (AF_INET, SOCK_STREAM, 0);

    if (server_sock < 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                "HTTPD :E: failed to create socket");
        return HTTP_SERVER_E_ERROR;

    }

    int yes = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &yes, sizeof(yes));
    // optional on many systems: SO_REUSEPORT
    //setsockopt(server_sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));


    if ((res = bind (server_sock, (struct sockaddr *)&address, sizeof(address))) != EOK ) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTPD :E: failed %d to bind socket to port.", res);
        closesocket (server_sock) ;
        return HTTP_SERVER_E_ERROR ;

    }


    return server_sock ;
}

/**
 * @brief   httpserver_close
 * @details Close the server socket opened by httpserver_init().
 *
 * @param[in] server_sock   The server socket descriptor.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         Server socket was closed successfully.
 * @retval HTTP_SERVER_E_ERROR      Error while closing the socket.
 *
 * @http
 */
int32_t
httpserver_close (int server_sock)
{
    int32_t res = HTTP_SERVER_E_ERROR ;

    if (server_sock>=0) {
        res = closesocket (server_sock) ;
        server_sock = -1 ;

    }

#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
    mbedtls_release_server_config () ;
#endif

    return res ;
}

/**
 * @brief   httpserver_select
 * @details Wait for activity on the server socket within a specified timeout.
 *
 * @param[in] server_sock   The server socket descriptor.
 * @param[in] timeout       Timeout in milliseconds.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         Server socket is ready for activity.
 * @retval HTTP_SERVER_E_CONNECTION Connection error occurred.
 * @retval HTTP_SERVER_E_ERROR      Other errors during the operation.
 *
 * @http
 */
int32_t
httpserver_select (int server_sock, uint32_t timeout)
{
    int32_t result ;
     fd_set   fdread;
     fd_set   fdex;
    struct timeval tv;

    if (server_sock<0) {
        return HTTP_SERVER_E_CONNECTION ;

    }

    FD_ZERO(&fdread) ;
    FD_ZERO(&fdex) ;
    FD_SET(server_sock, &fdread);
    FD_SET(server_sock, &fdex);
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    result = select(server_sock+1, &fdread, 0, &fdex, &tv) ;

    if (FD_ISSET(server_sock, &fdex)) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTPD : : read exception on listening socket...");
        return HTTP_SERVER_E_CONNECTION ;


    } else if (result < 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTPD : : read select failed with %d", result);
        return HTTP_SERVER_E_CONNECTION ;

    } else if (!FD_ISSET(server_sock, &fdread)) {
        return HTTP_SERVER_E_ERROR ;

    }

    return HTTP_SERVER_E_OK ;
}

/**
 * @brief   httpserver_user_select
 * @details Wait for activity on a user's socket within a specified timeout.
 *
 * @param[in] user      Pointer to the user structure.
 * @param[in] timeout   Timeout in milliseconds.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         User socket is ready for activity.
 * @retval HTTP_SERVER_E_CONNECTION Connection error occurred.
 * @retval HTTP_SERVER_E_ERROR      Other errors during the operation.
 *
 * @http
 */
int32_t
httpserver_user_select (HTTP_USER_T* user, uint32_t timeout)
{
    int32_t res ;
     fd_set   fdread;
     fd_set   fdex;
    struct timeval tv;

    if (user->socket<0) {
        return HTTP_SERVER_E_ERROR ;
    }

    FD_ZERO(&fdread) ;
    FD_ZERO(&fdex) ;
    FD_SET(user->socket, &fdread);
    FD_SET(user->socket, &fdex);
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                "HTTPD : : user socket select %d...", user->socket);

    res = select(user->socket+1, &fdread, 0, &fdex, &tv) ;
    (void) res ;

    DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                "HTTPD : : user socket select %d complete.", user->socket);

    if (FD_ISSET(user->socket, &fdex)) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTPD : : user exception on select socket...");
        return HTTP_SERVER_E_CONNECTION ;

    } else if (!FD_ISSET(user->socket, &fdread)) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                    "HTTPD : : user select failed with %d", res);
        return HTTP_SERVER_E_CONNECTION ;

    }

    return EOK ;
}


/**
 * @brief   httpserver_listen
 * @details Puts the server socket into listening mode to accept incoming connections.
 *
 * @param[in] server_sock   The server socket descriptor.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         Successfully set to listening mode.
 * @retval HTTP_SERVER_E_ERROR      Error occurred while setting the socket.
 *
 * @http
 */
int32_t
httpserver_listen (int server_sock)
{
    int32_t res ;

    if (server_sock<0) {
        return HTTP_SERVER_E_CONNECTION ;

    }

    if ((res = listen (server_sock, HTTP_SERVER_LISTEN_BACKLOG)) != EOK ) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                "HTTPD :E: failed %d to put socket in listen mode", res);

    }

    return res ;
}

/**
 * @brief   httpserver_user_init
 * @details Initialize a user context for HTTP operations.
 *
 * @param[out] user   Pointer to the user structure to initialize.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         User context initialized successfully.
 * @retval HTTP_SERVER_E_ERROR      Error during initialization.
 *
 * @http
 */
int32_t
httpserver_user_init (HTTP_USER_T* user)
{
    memset (user, 0, sizeof(HTTP_USER_T)) ;
    return HTTP_SERVER_E_OK ;
}

/**
 * @brief   httpserver_user_accept
 * @details Waits for a client to connect and assigns it to the provided user structure.
 *
 * @param[in] server_sock   The server socket descriptor.
 * @param[out] user         Pointer to the user structure to hold the client data.
 * @param[in] timeout       Timeout in milliseconds.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         Client connection accepted successfully.
 * @retval HTTP_SERVER_E_ERROR      Error while accepting the client.
 *
 * @http
 */
int32_t
httpserver_user_accept (int server_sock, HTTP_USER_T* user, uint32_t timeout)
{
    if (server_sock<0) {
        return HTTP_SERVER_E_CONNECTION ;

    }

    user->timeout = timeout ;

    uint32_t addrlen = sizeof(user->address) ;
    user->socket = accept(server_sock, (struct sockaddr *)&user->address, &addrlen) ;

    if (user->socket < 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_WARNING, "HTTPD : : accept failed %d errno %d",
                user->socket,
#if QORAAL_CFG_USE_LWIP
                *__errno());
#else
                errno) ;
#endif
        return HTTP_SERVER_E_ERROR ;

    }

    DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_INFO,
                "HTTP  : : httpserver_user_accept %d", user->socket);

#if 0
    unsigned long flags = 1;
    int32_t status = ioctlsocket (user->socket, FIONBIO, &flags) ;
    if (status != 0) {
        DBG_MESSAGE_HTTP_CLIENT (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTPD :W: setting FIONBIO failed %d!", status);
        closesocket(user->socket);
        return HTTP_SERVER_E_CONNECTION;

    }
#endif
    return HTTP_SERVER_E_OK ;
}

/**
 * @brief   httpserver_user_ssl_accept
 * @details Perform an SSL handshake for a connected client.
 *
 * @param[in] user      Pointer to the user structure containing the client connection.
 * @param[in] timeout   Timeout in milliseconds.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         SSL handshake completed successfully.
 * @retval HTTP_SERVER_E_ERROR      Error during SSL handshake.
 *
 * @http
 */

int32_t
httpserver_user_ssl_accept (HTTP_USER_T* user, uint32_t timeout, void * ssl_config)
{
#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
    mbedtls_ssl_config * pssl = (mbedtls_ssl_config*) ssl_config ;
    if (pssl != 0) {
        if (!user->ssl) {
            user->ssl = HTTP_SERVER_MALLOC(sizeof(mbedtls_ssl_context)) ;
            if (!user->ssl) {
                return HTTP_SERVER_E_MEMORY ;

            }
            mbedtls_ssl_init((mbedtls_ssl_context *)user->ssl) ;
            if (mbedtls_ssl_setup( (mbedtls_ssl_context *)user->ssl, pssl ) != 0) {
                mbedtls_ssl_free ((mbedtls_ssl_context *)user->ssl) ;
                HTTP_SERVER_FREE(user->ssl) ;
                user->ssl = 0 ;
                return HTTP_SERVER_E_MEMORY ;

            }
            mbedtls_ssl_set_bio( (mbedtls_ssl_context *)user->ssl, (void*)(intptr_t)user->socket,
                mbedtls_net_send, mbedtls_net_recv, 0 /*mbedtls_net_recv_timeout*/ )  ;

       }
 
        if (user->ssl) {
            int32_t status ;

            do {

                status = mbedtls_ssl_handshake((mbedtls_ssl_context *)user->ssl);

                if (status == MBEDTLS_ERR_SSL_WANT_READ ||
                    status == MBEDTLS_ERR_SSL_WANT_WRITE) {

                    fd_set   rfds, wfds, fdex;
                    struct timeval tv;
                    
                    FD_ZERO(&fdex) ;
                    FD_ZERO(&rfds);
                    FD_ZERO(&wfds);

                    if (status == MBEDTLS_ERR_SSL_WANT_READ)  FD_SET(user->socket, &rfds);
                    if (status == MBEDTLS_ERR_SSL_WANT_WRITE) FD_SET(user->socket, &wfds);

                    FD_SET(user->socket, &fdex);
                    tv.tv_sec = 8 ;
                    tv.tv_usec = 0 ;

                    int received = select(user->socket+1, &rfds, &wfds, &fdex, &tv) ;
                    if (received == 0) {
                    	status = MBEDTLS_ERR_SSL_TIMEOUT ;

                    } else if (!(FD_ISSET(user->socket, &rfds) || FD_ISSET(user->socket, &wfds))) {
                        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                            "HTTP  : : select received %d", received);
                        status = MBEDTLS_ERR_NET_CONN_RESET ;

                    }
                    else if (FD_ISSET(user->socket, &fdex)) {
                        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_INFO,
                            "HTTP  : : select exception received %d", received);
                        status = MBEDTLS_ERR_NET_CONN_RESET ;

                    }
                }

            } while(status != 0 &&
                    (status == MBEDTLS_ERR_SSL_WANT_READ ||
                    status == MBEDTLS_ERR_SSL_WANT_WRITE ||
                    status == MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS ||
                    status == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS)) ;

            if (status < 0) {
                if ((status != MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) &&
                    (status != MBEDTLS_ERR_SSL_CONN_EOF) &&
                    (status != MBEDTLS_ERR_NET_CONN_RESET)) {
                    DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_WARNING,
                        "HTTP  : : mbedtls_ssl_handshake() returned -0x%04X", -status);
                }
                mbedtls_ssl_close_notify ((mbedtls_ssl_context *)user->ssl) ;

                return HTTP_SERVER_E_CONNECTION ;

            }

        }

    }

#endif

    return HTTP_SERVER_E_OK ;
}

/**
 * @brief   httpserver_is_connected
 * @details Simply do a _t_select() with 0 timeout to see if the socket was closed..
 *
 * @param[in] user
 *
 * @return                              1 if the connection is valid, zero otherwise.
 *
 * @http
 */
int32_t
httpserver_is_connected (HTTP_USER_T* user)
{
     fd_set   fdread;
    struct timeval tv;

    if (user->socket < 0) {
        return 0 ;
    }
    FD_ZERO(&fdread) ;
    FD_SET(user->socket, &fdread);
    tv.tv_sec = 0 ;
    tv.tv_usec = 0 ;

    if (select(user->socket+1, &fdread, 0, 0, &tv) < 0) {
        return 0 ;
    }

    return 1 ;
}

/**
 * @brief   httpserver_user_close
 * @details Close a user's connection and release resources.
 *
 * @param[in] user   Pointer to the user structure representing the connection.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         User connection closed successfully.
 * @retval HTTP_SERVER_E_ERROR      Error while closing the user connection.
 *
 * @http
 */
int32_t
httpserver_user_close (HTTP_USER_T* user)
{
    int32_t res ;
#if defined WSERVER_CLOSE_WAIT_TIME /*&& WSERVER_CLOSE_WAIT_TIME*/
    httpserver_user_select(user, WSERVER_CLOSE_WAIT_TIME) ;
#endif
#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
    if (user->ssl) {
        mbedtls_ssl_close_notify ((mbedtls_ssl_context *)user->ssl) ;

    }
#endif
    DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_INFO,
                    "HTTP  : : httpserver_user_close %d", user->socket);

    res = closesocket (user->socket);
    user->socket = -1 ;

#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
    if (user->ssl) {
        mbedtls_ssl_free ((mbedtls_ssl_context *)user->ssl) ;
        HTTP_SERVER_FREE(user->ssl) ;
        user->ssl = 0 ;

    }
#endif
    httpserver_free_request (user) ;

    return res ;
}

/**
 * @brief   httpserver_write
 * @details Write data to a user's connection.
 *
 * @param[in] user      Pointer to the user structure.
 * @param[in] buffer    Pointer to the data buffer to send.
 * @param[in] length    Length of the data to send.
 *
 * @return                          Number of bytes sent or an error code.
 * @retval HTTP_SERVER_E_CONNECTION Connection error during sending.
 *
 * @http
 */
int32_t
httpserver_write (HTTP_USER_T* user, const uint8_t* buffer, uint32_t length)
{
    int32_t sent_bytes ;
    uint32_t result = 0 ;
    uint32_t total = 0 ;


    while (length) {

         fd_set   fdwrite;
         fd_set   fdex;
        struct timeval tv;
        FD_ZERO(&fdwrite) ;
        FD_ZERO(&fdex) ;
        FD_SET(user->socket, &fdwrite);
        FD_SET(user->socket, &fdex);
        tv.tv_sec = user->timeout / 1000;
        tv.tv_usec = (user->timeout % 1000) * 1000;

        result = select(user->socket+1, 0, &fdwrite, &fdex, &tv) ;

        if (FD_ISSET(user->socket, &fdex)) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                        "HTTPD : : write exception on listening socket...");
            return HTTP_SERVER_E_CONNECTION ;

        } else if (result <= 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                        "HTTPD : : write select failed with %d", result);
            return HTTP_SERVER_E_CONNECTION ;

        }

    #if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
        if (user->ssl) {
            sent_bytes = mbedtls_ssl_write ((mbedtls_ssl_context *)user->ssl, (unsigned char*)&buffer[total], length) ;
             
        }
        else {
    #endif
            sent_bytes = send (user->socket, (unsigned char*)&buffer[total], length, 0 /*MSG_DONTWAIT*/);
    #if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
        }
    #endif
        if (sent_bytes <= 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTPD :E: socket 0x%x sent_bytes %d\r\n", user->socket, sent_bytes);
            return HTTP_SERVER_E_CONNECTION ;

        } else {
            user->write += sent_bytes ;
            total += sent_bytes ;
            if (length >=  (unsigned int)sent_bytes) length -= sent_bytes ;
            else break ;

        }
    }
    return total ;
}

static int32_t
httpserver_wait_read(HTTP_USER_T* user, uint32_t timeout)
{
    int32_t res ;
     fd_set   fdread;
     fd_set   fdex;
    struct timeval tv;

    FD_ZERO(&fdread) ;
    FD_SET(user->socket, &fdread);
    FD_ZERO(&fdex) ;
    FD_SET(user->socket, &fdex);
    tv.tv_sec = timeout/1000 ;
    tv.tv_usec = (timeout % 1000) * 1000 ;

    res = select(user->socket+1, &fdread, 0, &fdex, &tv) ;
    (void) res ;

    if (FD_ISSET(user->socket, &fdread)) {
        return EOK ;

    }
    if (FD_ISSET(user->socket, &fdex)) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                "HTTP  : : read select failed with exception");
        return HTTP_SERVER_E_CONNECTION ;

    }

    DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_DEBUG,
                "HTTP  : : read select failed with %d after %d", res, timeout);

    return HTTP_SERVER_E_ERROR ;
}

/**
 * @brief   httpserver_read
 * @details Read data from a user's connection.
 *
 * @param[in] user      Pointer to the user structure.
 * @param[in] buffer    Pointer to the buffer to store the data.
 * @param[in] length    Maximum number of bytes to read.
 * @param[in] timeout   Timeout in milliseconds.
 *
 * @return                          Number of bytes read or an error code.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during reading.
 *
 * @http
 */
int32_t
httpserver_read (HTTP_USER_T* user, void* buffer, uint32_t length, uint32_t timeout)
{
    int received = EOK;
#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
    if (user->ssl) {
        if (!mbedtls_ssl_get_bytes_avail ((mbedtls_ssl_context *)user->ssl)) {
            received = httpserver_wait_read (user, timeout) ;

        }

        while (received == EOK) {
            received = mbedtls_ssl_read ((mbedtls_ssl_context *)user->ssl, buffer, length) ;
            if (received == MBEDTLS_ERR_SSL_WANT_READ) {
                received = httpserver_wait_read (user, timeout) ;

            } else {
                break ;

            }

        }
    } else
#endif
    {
        received = httpserver_wait_read (user, timeout) ;
        if (received == EOK) {
            received = recv (user->socket, buffer, length, 0);

        }

    }

   if(received > 0) {
        user->read += received ;

    }

    return received ;
}

int32_t
httpserver_wait_close (HTTP_USER_T* user, uint32_t timeout)
{
    // ToDo: wait for actuall close or otherwise decrement the timer and continue
    return httpserver_wait_read (user, timeout) ;
}

void
alloc_headers (HTTP_USER_T* user)
{
    unsigned int i ;
    for (i=0; i<sizeof(user->headers)/sizeof(user->headers[0]); i++) {
        if (user->headers[i].key && user->headers[i].value) {
            char* value = (char*)HTTP_SERVER_MALLOC(strlen(user->headers[i].value) + 1) ;
            if (value) strcpy (value, user->headers[i].value ) ;
            user->headers[i].value = value ;

        }

    }
}

void
free_headers (HTTP_USER_T* user)
{
    unsigned int i ;
    for (i=0; i<sizeof(user->headers)/sizeof(user->headers[0]); i++) {
        if (user->headers[i].key && user->headers[i].value) {
            HTTP_SERVER_FREE((char*)user->headers[i].value) ;
            user->headers[i].value = 0 ;

        }

    }
}

static int32_t
httpserver_buffer_consume(HTTP_USER_T* user, char** buffer, uint32_t len)
{
    if (user->reader.payload && user->reader.payload_length) {
        uint32_t take = user->reader.payload_length;
        if (take > len) {
            take = len;
        }

        if (buffer) {
            *buffer = user->reader.payload;
        }

        user->reader.payload += take;
        user->reader.payload_length -= take;

        if (user->reader.payload_length == 0) {
            user->reader.payload = 0;
        }

        return (int32_t)take;
    }

    return 0;
}

static int32_t
httpserver_read_line(HTTP_USER_T* user, uint32_t timeout, char* line, uint32_t maxlen)
{
    uint32_t idx = 0;

    if (!line || maxlen < 2) {
        return HTTP_SERVER_E_ERROR;
    }

    while (idx + 1 < maxlen) {
        char* p = 0;
        int32_t got = httpserver_buffer_consume(user, &p, 1);

        if (got == 0) {
            got = httpserver_read(user, &line[idx], 1, timeout);
            if (got <= 0) {
                return got;
            }
        } else {
            line[idx] = *p;
        }

        idx++;

        if (idx >= 2 && line[idx - 2] == '\r' && line[idx - 1] == '\n') {
            line[idx - 2] = '\0';
            return (int32_t)(idx - 2);
        }
    }

    return HTTP_SERVER_E_LENGTH;
}

static int32_t
httpserver_read_exact(HTTP_USER_T* user, uint32_t timeout, char* buffer, uint32_t len)
{
    uint32_t total = 0;

    while (total < len) {
        char* p = 0;
        int32_t got = httpserver_buffer_consume(user, &p, len - total);

        if (got > 0) {
            memcpy(&buffer[total], p, (uint32_t)got);
            total += (uint32_t)got;
            continue;
        }

        got = httpserver_read(user, &buffer[total], len - total, timeout);
        if (got <= 0) {
            return got;
        }

        total += (uint32_t)got;
    }

    return (int32_t)total;
}

static int32_t
httpserver_read_chunked_content_ex(HTTP_USER_T* user, uint32_t timeout, char** request)
{
    int32_t received;
    char line[32];

    if (request) {
        *request = 0;
    }

    if (user->reader.chunk_complete) {
        return 0;
    }

    while (user->reader.chunk_left == 0) {
        unsigned long chunk_size;
        char* endptr;
        char* semi;

        received = httpserver_read_line(user, timeout, line, sizeof(line));
        if (received <= 0) {
            return received;
        }

        semi = strchr(line, ';');
        if (semi) {
            *semi = '\0';
        }

        chunk_size = strtoul(line, &endptr, 16);
        if (endptr == line || *endptr != '\0') {
            return HTTP_SERVER_E_HEADER;
        }

        if (chunk_size == 0) {
            do {
                received = httpserver_read_line(user, timeout, line, sizeof(line));
                if (received < 0) {
                    return received;
                }
            } while (received > 0);

            user->reader.chunk_complete = 1;
            return 0;
        }

        user->reader.chunk_left = (uint32_t)chunk_size;
        break;
    }

    if (user->reader.chunk_left) {
        uint32_t want = user->reader.chunk_left;
        if (want > HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH) {
            want = HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH;
        }

        received = httpserver_read_exact(user, timeout, user->rw_buffer, want);
        if (received <= 0) {
            return received;
        }

        user->reader.chunk_left -= (uint32_t)received;

        if (user->reader.chunk_left == 0) {
            char crlf[2];
            int32_t newline = httpserver_read_exact(user, timeout, crlf, 2);
            if (newline <= 0) {
                return newline;
            }
            if (crlf[0] != '\r' || crlf[1] != '\n') {
                return HTTP_SERVER_E_HEADER;
            }
        }

        if (request) {
            *request = user->rw_buffer;
        }

        return received;
    }

    return 0;
}

/**
 * @brief   httpserver_read_request_ex
 * @details Reads and parses an HTTP request from the user's connection. 
 *          Allocates and returns the request endpoint and method. In case 
 *          of an error, partial data may still be available in the user structure.
 *
 * @param[in] user         Pointer to the user structure representing the connection.
 * @param[in] timeout      Timeout in milliseconds for reading the request.
 * @param[out] endpoint    Pointer to store the endpoint string.
 * @param[out] method      Pointer to store the HTTP method.
 *
 * @return                          Number of bytes read in the request or an error code.
 * @retval HTTP_SERVER_E_OK         Request was successfully read and parsed.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during reading.
 * @retval HTTP_SERVER_E_CONNECTION Connection was closed during reading.
 * @retval HTTP_SERVER_E_HEADER     Error parsing the HTTP header.
 * @retval HTTP_SERVER_E_LENGTH     Request exceeded maximum allowed length.
 * @retval HTTP_SERVER_E_MEMORY     Failed to allocate memory for request data.
 *
 * @http
 */
int32_t
httpserver_read_request_ex (HTTP_USER_T* user, uint32_t timeout, char** endpoint, int32_t* method)
{
    int32_t         received ;
    int32_t         offset = 0 ;
    char*           content ;
    char*           pendpoint ;
    char*           payload ;
    unsigned int    content_length = 0 ;

    DBG_ASSERT_HTTP_SERVER (user->socket,
                    "HTTPD :A: unexpected - socket not valid.!") ;
    DBG_ASSERT_HTTP_SERVER (!user->content,
                    "HTTPD :A: unexpected - content not null.!") ;
     DBG_ASSERT_HTTP_SERVER (!user->endpoint,
                    "HTTPD :A: unexpected - endpoint not null.!") ;

    *endpoint = 0 ;
    *method = 0 ;
    user->endpoint = 0 ;

    if (user->rw_buffer == 0) {
        user->rw_buffer  = HTTP_SERVER_MALLOC(HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH) ;
        if(user->rw_buffer == 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTPD :E: write response buffer allocation failed");
            return HTTP_SERVER_E_MEMORY ;

        }

    }

    do {
        received = httpserver_read (user, &user->rw_buffer[offset], HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - offset, timeout) ;
        if(received <= 0) {
            return received ;

        }

        offset += received ;
        if (strnstr(user->rw_buffer, "\r\n\r\n", offset)) break ;

        if (offset >= HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTPD :E: HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH to small for HTTP headers!");
            return HTTP_SERVER_E_LENGTH ;

        }

    } while (offset < HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH) ;

    memset (user->headers, 0, sizeof(user->headers)) ;
    user->headers[0].key = HTTP_HEADER_KEY_CONTENT_TYPE ;
    user->headers[1].key = HTTP_HEADER_KEY_CONTENT_LENGTH ;
    user->headers[2].key = HTTP_HEADER_KEY_TRANSFER_ENCODING ;
    user->headers[3].key = HTTP_HEADER_KEY_CONNECTION ;
    user->headers[4].key = HTTP_HEADER_KEY_AUTHORIZATION ;
    user->headers[5].key = HTTP_HEADER_KEY_ACCEPT ;


    *method = httpparse_request(user->rw_buffer, offset, user->headers,
                6, &pendpoint, &content) ;
    if (*method <= 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTPD :W: httpserver_read_request_ex httpparse_request returned  %d", *method);
         return HTTP_SERVER_E_HEADER ;

    }

    if (user->headers[2].value && (strncmp(user->headers[2].value, "chunked", 7) == 0)) {
        user->reader.chunked = 1 ;
    } else {
        user->reader.chunked = 0 ;
    }

    if (pendpoint) {
        user->endpoint = (char*)HTTP_SERVER_MALLOC(strlen(pendpoint) + 1) ;
        if (user->endpoint == 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTP  :W: out of memory");
            return HTTP_SERVER_E_MEMORY ;

        }
        strcpy(user->endpoint, pendpoint) ;
        *endpoint = user->endpoint ;

    }

    user->reader.payload = 0 ;
    user->reader.payload_length = 0 ;
    user->content = 0 ;
    user->reader.content_length = 0 ;
    user->reader.chunk_complete = 0 ;
    user->reader.chunk_left = 0 ;

    alloc_headers (user) ;
    if (content) {

        int left = offset - (content - user->rw_buffer) ;

        if (user->reader.chunked) {
            if (left > 0) {
                user->reader.payload = content ;
                user->reader.payload_length = (uint32_t)left ;
            }
            return 0 ;
        }

        content_length = httpparse_content(content, left, user->headers,
                    sizeof(user->headers)/sizeof(user->headers[0]), &payload) ;

        if (content_length) {

                left = offset - (payload - user->rw_buffer) ;

                if (left) {
                    user->reader.payload = payload ;
                    user->reader.payload_length = (uint32_t)left ;

                }

        }

    }

    user->reader.content_length = content_length ;
    return content_length ;
}

/**
 * @brief Retrieve the Authorization header from the user's HTTP headers.
 *
 * @param[in] user      Pointer to the user structure containing the headers.
 * @return              Pointer to the Authorization header value, or NULL if not found.
 */
const char* 
httpserver_get_authorization_header (HTTP_USER_T* user) 
{
    // Assuming headers are stored in user->headers and HTTP_HEADER_KEY_AUTHORIZATION is defined.
    for (int i = 0; i < sizeof(user->headers) / sizeof(user->headers[0]); i++) {
        if ((uintptr_t)user->headers[i].key == (uintptr_t)HTTP_HEADER_KEY_AUTHORIZATION) {
            return user->headers[i].value;  // Return the value of the Authorization header.

        }

    }
    return NULL;  // Return NULL if the Authorization header is not found.
}

const char* 
httpserver_get_content_type_header (HTTP_USER_T* user) 
{
    // Assuming headers are stored in user->headers and HTTP_HEADER_KEY_CONTENT_TYPE is defined.
    for (int i = 0; i < sizeof(user->headers) / sizeof(user->headers[0]); i++) {
        if ((uintptr_t)user->headers[i].key == (uintptr_t)HTTP_HEADER_KEY_CONTENT_TYPE) {
            return user->headers[i].value;  // Return the value of the Content-Type header.

        }

    }
    return NULL;  // Return NULL if the Content-Type header is not found.
}

const char* 
httpserver_get_accept_header (HTTP_USER_T* user) 
{
    // Assuming headers are stored in user->headers and HTTP_HEADER_KEY_ACCEPT is defined.
    for (int i = 0; i < sizeof(user->headers) / sizeof(user->headers[0]); i++) {
        if ((uintptr_t)user->headers[i].key == (uintptr_t)HTTP_HEADER_KEY_ACCEPT) {
            return user->headers[i].value;  // Return the value of the Accept header.

        }

    }
    return NULL;  // Return NULL if the Accept header is not found.
}


/**
 * @brief   httpserver_read_content_ex
 * @details Reads the HTTP request body content for a user's connection. If payload 
 *          data is already available in the user structure, it returns the existing 
 *          data. Otherwise, it reads content from the connection.
 *
 * @param[in] user         Pointer to the user structure representing the connection.
 * @param[in] timeout      Timeout in milliseconds for reading the content.
 * @param[out] request     Pointer to the content buffer or payload.
 *
 * @return                          Number of bytes read from the request body.
 * @retval >0                       Number of bytes read successfully.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during reading.
 * @retval HTTP_SERVER_E_CONNECTION Connection was closed during reading.
 *
 * @http
 */
int32_t
httpserver_read_content_ex (HTTP_USER_T* user, uint32_t timeout, char** request)
{
    int32_t received ;

    if (user->reader.chunked) {
        return httpserver_read_chunked_content_ex(user, timeout, request);
    }

    if (user->reader.payload) {
        if (request) *request = user->reader.payload ;
        user->reader.payload = 0 ;
        received = user->reader.payload_length ;
        user->reader.payload_length = 0 ;
        user->reader.content_length -= received ;
        return received ;

    }

    if (request) *request = 0 ;
    if (user->reader.content_length <= 0) {
        return 0 ;

    }

    received = httpserver_read (user, user->rw_buffer, HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH, timeout) ;
    if (received <= 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_REPORT,
                    "HTTPD : : read error %d",
                        received);
        return  received;

    }

    user->reader.content_length -= received ;
    if (request) *request = user->rw_buffer ;
    return received ;
}

/**
 * @brief   httpserver_read_all_content_ex
 * @details Reads the entire HTTP request body content for a user's connection. 
 *          Allocates memory for the full content and stores it in the provided 
 *          request pointer. The function attempts to read until all content is 
 *          received or an error occurs.
 *
 * @param[in] user         Pointer to the user structure representing the connection.
 * @param[in] timeout      Timeout in milliseconds for reading the content.
 * @param[out] request     Pointer to the buffer that will store the complete content.
 *
 * @return                          Total number of bytes read from the request body.
 * @retval >0                       Total bytes read successfully.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during reading.
 * @retval HTTP_SERVER_E_MEMORY     Failed to allocate memory for the content.
 *
 * @http
 */
int32_t
httpserver_read_all_content_ex (HTTP_USER_T* user, uint32_t timeout, char** request)
{
    int32_t recvlen ;
    uint32_t total = 0 ;
    char* buffer ;

    if (request) {
        *request = 0 ;
    }

    if (user->reader.chunked) {
        char* content = 0 ;

        while ((recvlen = httpserver_read_content_ex(user, timeout, &buffer)) > 0) {
            char* newbuf = (char*)HTTP_SERVER_MALLOC(total + recvlen + 1) ;
            if (!newbuf) {
                if (content) {
                    HTTP_SERVER_FREE(content) ;
                }
                DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_WARNING,
                        "HTTPD :W: no memory for chunked content");
                return HTTP_SERVER_E_MEMORY ;
            }

            if (content && total) {
                memcpy(newbuf, content, total) ;
                HTTP_SERVER_FREE(content) ;
            }

            memcpy(&newbuf[total], buffer, recvlen) ;
            total += (uint32_t)recvlen ;
            newbuf[total] = 0 ;
            content = newbuf ;
        }

        if (recvlen < 0) {
            if (content) {
                HTTP_SERVER_FREE(content) ;
            }
            return recvlen ;
        }

        user->content = content ;
        if (request) {
            *request = content ;
        }
        return total ;
    }

    {
    uint32_t content_length = user->reader.content_length ;

    if (content_length) {
            user->content = HTTP_SERVER_MALLOC (content_length+1) ;
            if (user->content) {
                user->content[content_length] = 0 ;
            while (total < content_length) {
                if ((recvlen = httpserver_read_content_ex (user, timeout, &buffer)) <= 0) {
                    break ;

                }
                if (total + recvlen > content_length) recvlen = content_length - total ;
                memcpy (&user->content[total], buffer, recvlen) ;
                total += recvlen ;

            }

                if (request) {
                    *request = user->content ;
                }

        } else {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_WARNING,
                    "HTTPD :W: no memory for %d content bytes!",
                    content_length);

                return HTTP_SERVER_E_MEMORY ;
            }
        }
    }

    return total  ;
}

/**
 * @brief   httpserver_write_response
 * @details Write an HTTP response to a user's connection.
 *
 * @param[in] user            Pointer to the user structure.
 * @param[in] result          HTTP status code to send.
 * @param[in] content_type    Content-Type of the response.
 * @param[in] headers         Array of HTTP headers.
 * @param[in] headers_count   Number of headers in the array.
 * @param[in] content         Response content.
 * @param[in] length          Length of the response content.
 *
 * @return                          Number of bytes sent or an error code.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during sending.
 * @retval HTTP_SERVER_E_CONNECTION Connection was closed.
 * @retval HTTP_SERVER_E_MEMORY     Buffer allocation failed.
 *
 * @http
 */
int32_t
httpserver_write_response (HTTP_USER_T* user, uint32_t result, const char* content_type, HTTP_HEADER_T* headers, uint32_t headers_count, const char* content, uint32_t length)
{
    int32_t         sent_bytes = 0;
    int32_t         count ;
    int32_t         send_length ;
    int32_t         send_offset = 0 ;
    char            content_length_str[16] ;
    HTTP_HEADER_T _http_headers_content_type[]  = {
            {"Content-Type", content_type} ,
    };
    HTTP_HEADER_T _http_headers_content_length[]    = {
            {"Content-Length", content_length_str} ,
    };

    if (user->rw_buffer == 0) {
        user->rw_buffer  = HTTP_SERVER_MALLOC(HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH) ;
        if(user->rw_buffer == 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                        "HTTPD :E: write response buffer allocation failed");
            return HTTP_SERVER_E_MEMORY ;

        }

    }

    if (content == 0) content = "" ;

    send_length = 0 ;
    snprintf (content_length_str, 16, "%d", (int)length) ;

    count = snprintf(user->rw_buffer, HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH, HTTPSERVER_RESPONSE_HEADER, (unsigned int)result);

    if (headers) {
        count += httpparse_append_headers (user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - count,
            headers, headers_count, 0) ;
    }
    count += httpparse_append_headers (user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - count,
            _http_headers, sizeof(_http_headers)/sizeof(_http_headers[0]), 0) ;
    count += httpparse_append_headers (user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - count,
            _http_headers_content_type, 
            sizeof(_http_headers_content_type)/sizeof(_http_headers_content_type[0]), 0) ;
    count += httpparse_append_headers (user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - count,
            _http_headers_content_length, 
            sizeof(_http_headers_content_length)/sizeof(_http_headers_content_length[0]), 1) ;

    DBG_ASSERT_HTTP_SERVER (count < HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH ,
            "HTTPD :A: send buffer to small") ;

    sent_bytes = httpserver_write (user, (unsigned char*)user->rw_buffer, count) ;

    while ((sent_bytes > 0) && length) {
        if (HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH  > length) { send_length = length ; }
        else { send_length = HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH  ; }

        sent_bytes = httpserver_write (user, (const uint8_t*) &content[send_offset], send_length) ;

        send_offset += sent_bytes ;
        length -= sent_bytes ;

    }

    return sent_bytes ;
}

/**
 * @brief   httpserver_chunked_response
 * @details Starts an HTTP chunked transfer response for a user's connection.
 *          Sends the HTTP response headers with the "Transfer-Encoding: chunked"
 *          header, indicating that the response content will be sent in chunks.
 *
 * @param[in] user            Pointer to the user structure representing the connection.
 * @param[in] result          HTTP status code for the response (e.g., 200, 404).
 * @param[in] content_type    MIME type of the response content (e.g., "text/html").
 * @param[in] headers         Pointer to additional HTTP headers to include.
 * @param[in] headers_count   Number of additional headers to include.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         Headers sent successfully, ready for chunked content.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during sending headers.
 *
 * @http
 */
int32_t
httpserver_chunked_response (HTTP_USER_T* user, uint32_t result, const char* content_type, const HTTP_HEADER_T* headers, uint32_t headers_count)
{
    int32_t         send_bytes  ;

    HTTP_HEADER_T _http_headers_content_type[]  = {
            {"Content-Type", content_type} ,
    };

    user->write_idx = 0 ;

    user->write_idx = snprintf((char*)user->rw_buffer, HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH, HTTPSERVER_RESPONSE_HEADER, (unsigned int)result);

    if (user->write_idx <= 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_ERROR,
                    "HTTPD :E: header error");
        return HTTP_SERVER_E_ERROR ;

    }

    if (headers) {
        user->write_idx += httpparse_append_headers ((char*)user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - user->write_idx,
            headers, headers_count, 0) ;
    }
    user->write_idx += httpparse_append_headers ((char*)user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - user->write_idx,
            _http_headers, 
            sizeof(_http_headers)/sizeof(_http_headers[0]), 0) ;
    user->write_idx += httpparse_append_headers ((char*)user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - user->write_idx,
            _http_headers_chunked, 
            sizeof(_http_headers_chunked)/sizeof(_http_headers_chunked[0]), 0) ;
    user->write_idx += httpparse_append_headers ((char*)user->rw_buffer, 
            HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH - user->write_idx,
            _http_headers_content_type, 
            sizeof(_http_headers_content_type)/sizeof(_http_headers_content_type[0]), 1) ;

    DBG_ASSERT_HTTP_SERVER (user->write_idx < HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH,
            "HTTPD :A: HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH to small for HTTP headers!") ;

    send_bytes = httpserver_write (user, (unsigned char*)user->rw_buffer, user->write_idx) ;

    user->write_idx = 0 ;

    return send_bytes > 0 ? HTTP_SERVER_E_OK : HTTP_SERVER_E_ERROR ;
}

int32_t
_chunked_flush(HTTP_USER_T* user)
{
    int32_t bytes = 0 ;

    if (user->write_idx) {

        if (user->write_idx >= HTTP_SERVER_MAX_CHUNK_LENGTH-2) {
            return HTTP_SERVER_E_LENGTH ;
        }

        sprintf(&user->rw_buffer[0], "%.3x\r", (unsigned int)user->send_bytes) ;
        user->rw_buffer[4] = '\n' ; // remove the \0
        user->rw_buffer[user->write_idx++] = '\r' ;
        user->rw_buffer[user->write_idx++] = '\n' ;

        bytes = httpserver_write (user, (unsigned char*)user->rw_buffer, user->write_idx) ;

        if (bytes <= 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                "HTTPD : : chunk flush failed rc=%d buffered=%d payload=%d socket=%d",
                bytes, user->write_idx, user->send_bytes, user->socket);
        }

        user->write_idx = 0 ;
        user->send_bytes = 0 ;

    }

    return bytes ;
}

int32_t      
httpserver_chunked_flush (HTTP_USER_T* user) 
{
    return _chunked_flush (user) ;
}

int32_t
_chunked_alloc (HTTP_USER_T* user, int len, char** buffer)
{
    int32_t status = HTTP_SERVER_E_OK ;
    DBG_ASSERT_HTTP_SERVER (user->rw_buffer,
                    "HTTPD :A: _chunked_alloc unexpected!") ;

    if (user->write_idx  >= HTTP_SERVER_MAX_CHUNK_LENGTH - 32) {
        status = _chunked_flush (user) ;
        if (status < 0) {
            return status ;

        }
    }

    if (user->write_idx + len > HTTP_SERVER_MAX_CHUNK_LENGTH - 8) {
            len =  HTTP_SERVER_MAX_CHUNK_LENGTH - 8 - user->write_idx ;

    }
    if (user->write_idx == 0) {
            // placeholder for length
        user->write_idx = snprintf(&user->rw_buffer[0],
            HTTP_SERVER_MAX_CHUNK_LENGTH, "%03x\r\n", (unsigned int)0) ;

    }

    *buffer = &user->rw_buffer[user->write_idx] ;

    return len ;
}

void
_chunked_commit (HTTP_USER_T* user, int len)
{
    user->write_idx += len ;
    user->send_bytes += len ;
}

int32_t
httpserver_chunked_vappend_fmtstr (HTTP_USER_T* user, const char* format_str,  va_list  args)
{
    DBG_ASSERT_HTTP_SERVER (user->rw_buffer,
                  "HTTPD :A: httpserver_chunked_append_fmtstr unexpected!") ;

    va_list args_copy;
    va_copy(args_copy, args);      // Make a copy
    int32_t req = vsnprintf(NULL, 0, format_str, args_copy); 
    va_end(args_copy);             // Done with the copy

    if (req <= 0) {
        return  HTTP_SERVER_E_CONTENT;

    }
    if (user->write_idx &&
            (user->write_idx + req > HTTP_SERVER_MAX_CHUNK_LENGTH - 8)) {
        int32_t flush_rc = _chunked_flush (user) ;
        if (flush_rc < 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                "httpserver_chunked_vappend_fmtstr : preflush failed rc=%d", flush_rc);
            return flush_rc;
        }
    }
    char * buffer ;
    req = _chunked_alloc (user, req, &buffer) ;

    if (req <= 0) {
        DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG, 
                "httpserver_chunked_vappend_fmtstr : chunk alloc/flush failed rc=%d", req);
        return req;

    }

    vsnprintf(buffer, req+1, format_str, args) ;
    _chunked_commit(user, req) ;

    return req ;
}

/**
 * @brief   httpserver_chunked_append_str
 * @details Appends a string to the current chunk in a chunked HTTP response. 
 *          If the current chunk buffer is full, it automatically flushes the 
 *          buffer before appending the new data.
 *
 * @param[in] user    Pointer to the user structure representing the connection.
 * @param[in] str     Pointer to the string to append.
 * @param[in] len     Length of the string to append. If 0, the function calculates
 *                    the string length using `strlen`.
 *
 * @return                          Number of bytes successfully appended to the chunk.
 * @retval HTTP_SERVER_E_LENGTH     Chunk exceeds the maximum allowed size.
 * @retval HTTP_SERVER_E_ERROR      Error occurred during appending or flushing.
 *
 * @http
 */
int32_t
httpserver_chunked_append_str (HTTP_USER_T* user, const char* str, uint32_t len)
{
    int32_t         req = 0;
    DBG_ASSERT_HTTP_SERVER (user->rw_buffer,
                    "HTTPD :A: httpserver_chunked_append_str unexpected!") ;

    if (len == 0) len = strlen(str) ;

    while (len > 0) {
        char * buffer ;

        req = _chunked_alloc (user, len, &buffer) ;

        if (req <= 0) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                "HTTPD : : httpserver_chunked_append_str : chunk alloc/flush failed rc=%d", req);
            break ;

        }

        memcpy(buffer, str, req) ;
        _chunked_commit(user, req) ;
        str += req ;
        len -= req ;

    }

    return req ;
}

int32_t
httpserver_chunked_append_fmtstr (HTTP_USER_T* user, const char* format_str, ...)
{
    va_list args;
    va_start(args, format_str);
    int32_t res = httpserver_chunked_vappend_fmtstr (user, format_str, args) ;
    va_end(args);

    return res ;
}


/**
 * @brief   httpserver_chunked_complete
 * @note    Buffer including the HTTP is limited to a size of HTTP_SERVER_MAX_XMIT_CONTENT_LENGTH .
 *          ToDo: Fix the buffer size limitation.
 *
 * @param[in] user
 * @param[in] result
 * @param[in] content
 *
 * @return                              The number of bytes sent or if < 0 it indicates an error.
 * @retval HTTP_SERVER_E_ERROR          error occurred during send.
 * @retval HTTP_SERVER_E_CONNECTION     Socket was closed.
 *
 * @http
 */
int32_t
httpserver_chunked_complete (HTTP_USER_T* user)
{
    int32_t res  ;
    DBG_ASSERT_HTTP_SERVER (user->rw_buffer,
                  "HTTPD :A: httpserver_chunked_complete unexpected!\r\n") ;

    if (user->write_idx >= HTTP_SERVER_MAX_CHUNK_LENGTH-8) {
        _chunked_flush (user) ;

    } else if (user->write_idx) {
        sprintf(&user->rw_buffer[0], "%.3x\r", (unsigned int)user->send_bytes) ;
        user->rw_buffer[4] = '\n' ; // remove the \0
        user->rw_buffer[user->write_idx++] = '\r' ;
        user->rw_buffer[user->write_idx++] = '\n' ;

    }
    user->write_idx += sprintf(&user->rw_buffer[user->write_idx], "0\r\n\r\n") ;

    res = httpserver_write (user, (unsigned char*)user->rw_buffer, user->write_idx) ;

    user->write_idx = 0 ;
    user->send_bytes = 0 ;

    return res ;
}

/**
 * @brief   httpserver_free_request
 * @details Release resources allocated for an HTTP request.
 *
 * @param[in] user   Pointer to the user structure.
 *
 * @return                          The function status.
 * @retval HTTP_SERVER_E_OK         Request resources released successfully.
 * @retval HTTP_SERVER_E_ERROR      Error during resource release.
 *
 * @http
 */
int32_t
httpserver_free_request (HTTP_USER_T* user)
{
    int32_t status = HTTP_SERVER_E_OK ;

    if (user->rw_buffer) {
        HTTP_SERVER_FREE(user->rw_buffer) ;
        user->rw_buffer = 0 ;
        if ((uint32_t)user->reader.content_length > user->reader.payload_length ) {
            DBG_MESSAGE_HTTP_SERVER (DBG_MESSAGE_SEVERITY_LOG,
                "HTTPD : : httpserver_free_request : POST payload not read!");
            status = HTTP_SERVER_E_ERROR ;

        }

    }

    if (user->content) {
        HTTP_SERVER_FREE (user->content) ;
        user->content  = 0 ;

    }

    user->reader.content_length = 0 ;
    user->reader.payload = 0 ;
    user->reader.payload_length = 0 ;
    user->reader.chunked = 0 ;
    user->reader.chunk_complete = 0 ;
    user->reader.chunk_left = 0 ;

    if (user->endpoint) {
        HTTP_SERVER_FREE (user->endpoint) ;
        user->endpoint = 0 ;

    }

    free_headers (user) ;

    return status ;
}
