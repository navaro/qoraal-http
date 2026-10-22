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


/**
 * @file        https_client.h
 * @author      Natie van Rooyen <natie@navaro.nl>
 * @date        January 1, 2015
 * @version     0.0.0.1 (alpha)
 *
 * @section DESCRIPTION
 *
 * HTTP & HTTPS Client.
 */

#ifndef __HTTPCLIENT_H__
#define __HTTPCLIENT_H__

#ifndef HTTP_WEBSOCK
#define HTTP_WEBSOCK                    0
#endif

#include <stdint.h>
#include "qoraal/qoraal.h"
#include "qoraal-http/qoraal.h"

/*===========================================================================*/
/* Client constants.                                                         */
/*===========================================================================*/

/**
 * @name    Error Codes
 * @{
 */
#define HTTP_CLIENT_E_OK                                 0
#define HTTP_CLIENT_E_ERROR                             -1 /* A_ERROR */
#define HTTP_CLIENT_E_CONNECTION                        -2 /* A_INVALID */
#define HTTP_CLIENT_E_HEADER                            -3
#define HTTP_CLIENT_E_CONTENT                           -4
#define HTTP_CLIENT_E_LENGTH                            -5
#define HTTP_CLIENT_E_MEMORY                            -6
#define HTTP_CLIENT_E_SSL_CONNECT                       -7
#define HTTP_CLIENT_E_SSL_TRUST                         -8
#define HTTP_CLIENT_E_HOST                              -9
/** @} */

/*===========================================================================*/
/* Client pre-compile time settings.                                         */
/*===========================================================================*/

/**
 * @name    Debug Level
 * @{
 */
#define DBG_MESSAGE_HTTP_CLIENT(severity, fmt_str, ...)         DBG_MESSAGE_T_LOG(SVC_LOGGER_TYPE(severity,0), 0, fmt_str, ##__VA_ARGS__)
#define DBG_ASSERT_HTTP_CLIENT                                  DBG_ASSERT_T
#define DBG_CHECK_HTTP_CLIENT                                   DBG_CHECK_T
/** @} */

/**
 * @name    Max Buffer Sizes
 * @{
 */
#define HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH                  (1280)
#define HTTP_CLIENT_MAX_RECV_HEADER_LENGTH                  (1280)
#define HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH                  (4*1024)
#define HTTP_CLIENT_MAX_RECV_BUFFER_PADDING                 (32)
#define MAX_IPv4_STR_LEN                                    32 /* a.b.c.d:port */
#define MAX_HOSTNAME_LEN                                    64 /* xxx.com */
#define MAX_CONTENT_TYPE_LEN                                64 /* application/pkcs10 */
#ifndef HTTP_CLIENT_MAX_RESPONSE_LENGTH
#define HTTP_CLIENT_MAX_RESPONSE_LENGTH                     (16*1024) /* largest body httpclient_read_response() accepts */
#endif
#ifndef HTTP_CLIENT_RESPONSE_INITIAL_LENGTH
#define HTTP_CLIENT_RESPONSE_INITIAL_LENGTH                 (1024) /* first buffer for a body without Content-Length */
#endif
/** @} */

/**
 * @name    Features
 * @brief   Lets callers that also build against older qoraal-http versions
 *          test for these additions.
 * @{
 */
#define HTTP_CLIENT_FEATURE_CONTENT_TYPE                    1 /* httpclient_set_content_type() */
#define HTTP_CLIENT_FEATURE_RETRY_AFTER                     1 /* HTTP_CLIENT_T.retry_after */
#define HTTP_CLIENT_FEATURE_READ_ALLOC                      1 /* httpclient_read_response_alloc() */
/** @} */

/**
 * @name    Timeouts
 * @{
 */
#define HTTP_CLIENT_CONNECT_TIMEOUT                         (8000)
/** @} */


/**
 * @name    Name
 * @{
 */
#define HTTP_CLIENT_AGENT_NAME                              "QORAAL"
/** @} */

/**
 * @name    websocket protocol
 * @{
 */
#define HTTP_CLIENT_WEBSOCKET_PROTOCOL                      "chat"
/** @} */

/**
 * @name    Malloc
 * @{
 */
#define HTTP_CLIENT_MALLOC(size)                qoraal_malloc (QORAAL_HeapAuxiliary, size)
#define HTTP_CLIENT_FREE(mem)                   qoraal_free (QORAAL_HeapAuxiliary, mem)
/** @} */

/**
 * @name    Rand
 * @{
 */
#define HTTP_CLIENT_RAND                qoraal_rand
/** @} */

/*===========================================================================*/
/* Client data structures and types.                                         */
/*===========================================================================*/

/**
 * @brief Client Instance.
 */
typedef struct HTTP_CLIENT_S {
    int32_t                socket ;
    void                    *ssl ;

    uint8_t                 *read_buffer ;
    int32_t                 payload_length ;
    int32_t                 content_length ;
    uint32_t                chunked ;
    int32_t                 chunk_length ;
    int32_t                 read ;

    char                    host[MAX_IPv4_STR_LEN] ;
    char                    hostname[MAX_HOSTNAME_LEN] ;
    char                    content_type[MAX_CONTENT_TYPE_LEN] ; /* POST Content-Type; empty = text/plain */
    uint32_t                retry_after ;   /* seconds from the last response's Retry-After, 0 if absent */
    uint32_t                total_read ;

    int32_t                 mss ;           /* maximum segment size */

#if HTTP_WEBSOCK
    uint32_t                status ;        /* Status of close handshake */
    uint32_t                key[4] ;        /* Sec-WebSocket-Key */
#endif
} HTTP_CLIENT_T ;

/**
 * @name    Client status codes
 * @{
 */
#define HTTP_CLIENT_STATUS_CLOSE_SERVER                             (1 << 0)
#define HTTP_CLIENT_STATUS_CLOSE_CLIENT                             (1 << 1)
/** @} */

typedef int32_t (*HTTP_STREAM_WRITE)(uint32_t ctx, const void* data, uint32_t len) ;
typedef int32_t (*HTTP_STREAM_NEXT_T)(HTTP_STREAM_WRITE stream_write, uint32_t ctx, uint32_t parm, uint32_t len) ;


/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

    extern int32_t      httpclient_init (HTTP_CLIENT_T* client, int32_t mss) ;
    extern int32_t      httpclient_connect (HTTP_CLIENT_T* client, const struct sockaddr_in* addr, void * ssl_config ) ;
    extern int32_t      httpclient_set_hostname (HTTP_CLIENT_T* client, const char* hostname) ;
    extern int32_t      httpclient_set_content_type (HTTP_CLIENT_T* client, const char* content_type) ;
    extern int32_t      httpclient_is_connected (HTTP_CLIENT_T* client) ;
    extern void         httpclient_close (HTTP_CLIENT_T* client) ;

    extern int32_t      httpclient_get (HTTP_CLIENT_T* client, const char* endpoint, const char *credentials) ;
    extern int32_t      httpclient_post (HTTP_CLIENT_T* client, const char* endpoint, const char* request, int32_t len, const char *credentials) ;
    extern int32_t      httpclient_post_chunked (HTTP_CLIENT_T* client, const char* endpoint, const char* request, int32_t len, const char *credentials) ;
    extern int32_t      httpclient_post_stream (HTTP_CLIENT_T* client, const char* endpoint, HTTP_STREAM_NEXT_T stream, uint32_t parm, int32_t len, const char *credentials) ;
    extern int32_t      httpclient_read_response (HTTP_CLIENT_T* client, uint32_t timeout, char** response, int32_t* status) ;
    extern int32_t      httpclient_read_response_alloc (HTTP_CLIENT_T* client, uint32_t timeout, char** response, int32_t* status,
                                                        uint32_t initial, uint32_t grow, uint32_t max) ;
    extern void         httpclient_free_response (HTTP_CLIENT_T* client, void* reponse) ;

    extern int32_t      httpclient_read_response_ex (HTTP_CLIENT_T* client, uint32_t timeout, int32_t* status) ;
    extern int32_t      httpclient_read_next_ex (HTTP_CLIENT_T* client, uint32_t timeout, uint8_t** recv) ;
    extern int32_t      httpclient_read_complete_ex (HTTP_CLIENT_T* client) ;

    extern int32_t      httpclient_websock_get (HTTP_CLIENT_T* client, const char* endpoint, const char *credentials) ;
    extern int32_t      httpclient_websock_read_response (HTTP_CLIENT_T* client, uint32_t timeout) ;
    extern int32_t      httpclient_websock_read (HTTP_CLIENT_T* client, char** buffer, uint32_t timeout) ;
    extern void         httpclient_websock_free (HTTP_CLIENT_T* client, char* buffer) ;
    extern int32_t      httpclient_websock_write_text (HTTP_CLIENT_T* client,  const char* payload, uint32_t length) ;
    extern int32_t      httpclient_websock_write_binary (HTTP_CLIENT_T* client,  const char* payload, uint32_t length) ;
    extern int32_t      httpclient_websock_ping (HTTP_CLIENT_T* client) ;
    extern int32_t      httpclient_websock_initiate_close (HTTP_CLIENT_T* client,  uint16_t reason) ;


#ifdef __cplusplus
}
#endif


#endif /* __HTTPCLIENT_H__ */

