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
 * @file        test_httpclient.c
 *
 * Host unit test of the HTTP client that needs no network. httpclient.c is
 * compiled into this file with its socket calls, and in the TLS build its
 * mbedTLS calls, replaced by fakes that play a scripted byte stream and keep
 * what is sent. Every response in the table is read whole, cut in two at
 * every byte, and one byte at a time, with both the allocating and the
 * streaming reader. Run with -v to see the client's log.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "qoraal/qoraal.h"
#include "qoraal-http/config.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpclient.h"
#include "qoraal-http/httpparse.h"
#if TEST_TLS
#include "qoraal-http/mbedtls/mbedtlsutils.h"
#endif

/* The platform socket headers are in already, so these calls only change
 * in httpclient.c below. Function-like, as HTTP_CLIENT_T has a member
 * named socket. */
#undef closesocket
#define socket(...)             fake_socket (__VA_ARGS__)
#define connect(...)            fake_connect (__VA_ARGS__)
#define send(...)               fake_send (__VA_ARGS__)
#define recv(...)               fake_recv (__VA_ARGS__)
#define select(...)             fake_select (__VA_ARGS__)
#define setsockopt(...)         fake_setsockopt (__VA_ARGS__)
#define shutdown(...)           fake_shutdown (__VA_ARGS__)
#define closesocket(...)        fake_closesocket (__VA_ARGS__)
#define inet_ntop(...)          fake_inet_ntop (__VA_ARGS__)
#if defined(_WIN32)
#define WSAGetLastError()       (fake_error)
static int fake_error ;
#endif

static int          fake_socket (int domain, int type, int protocol) ;
static int          fake_connect (int s, const struct sockaddr* addr, size_t len) ;
static long         fake_send (int s, const void* buf, size_t len, int flags) ;
static long         fake_recv (int s, void* buf, size_t len, int flags) ;
static int          fake_select (int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* tv) ;
static int          fake_setsockopt (int s, int level, int name, const void* value, size_t len) ;
static int          fake_shutdown (int s, int how) ;
static int          fake_closesocket (int s) ;
static const char*  fake_inet_ntop (int af, const void* src, char* dst, size_t size) ;

#include "../../src/httpclient.c"

#undef socket
#undef connect
#undef send
#undef recv
#undef select
#undef setsockopt
#undef shutdown
#undef closesocket
#undef inet_ntop

/*===========================================================================*/
/* The fake connection.                                                      */
/*===========================================================================*/

#define EV_DATA                 0   /* bytes */
#define EV_CLOSE                1   /* clean close: end of stream, over TLS a close_notify */
#define EV_EOF_ZERO             2   /* TCP close without close_notify, as mbedtls_ssl_read() gives it: 0 */
#define EV_EOF_RESET            3   /* the same through qoraal's BIO: MBEDTLS_ERR_NET_CONN_RESET */
#define EV_RESET                4   /* the connection is reset */
#define EV_TIMEOUT              5   /* select() times out once */
#define EV_WANT_READ            6   /* mbedtls_ssl_read() gives WANT_READ once; a plain socket skips it */
#define EV_NONE                 -1  /* nothing more: every select() times out */

typedef struct {
    int             kind ;
    const char*     data ;
    size_t          len ;
} EVENT_T ;

#define MAX_EVENTS              24000

static EVENT_T      events[MAX_EVENTS] ;
static int          event_count ;
static int          event_next ;
static size_t       event_used ;        /* bytes of events[event_next] read already */

static char         sent[32768] ;
static size_t       sent_len ;
static size_t       send_max ;          /* largest piece one send takes, 0 for all */
static int          send_fail ;         /* the next send fails: 0, EV_TIMEOUT or EV_RESET */
static int          connect_fail ;      /* connect() fails: 0, EV_TIMEOUT or EV_RESET */
static bool         sndtimeo_fail ;     /* setsockopt (SO_SNDTIMEO) fails */
static int          sndtimeo_calls ;
static size_t       sndtimeo_len ;      /* optlen of the last SO_SNDTIMEO */
static uint32_t     sndtimeo_ms ;       /* and its value */

static long         allocs ;            /* allocations not freed yet */
static long         alloc_count ;       /* allocations so far */
static long         alloc_fail_at ;     /* this allocation fails, 0 for none */

static bool         verbose ;
static int          checks ;
static int          failures ;

#define CHECK(cond, ...)        check_that ((cond), __LINE__, __VA_ARGS__)

static void
check_that (bool ok, int line, const char* fmt, ...)
{
    va_list args ;

    checks++ ;
    if (!ok) {
        failures++ ;
        printf ("  FAIL line %d: ", line) ;
        va_start (args, fmt) ;
        vprintf (fmt, args) ;
        va_end (args) ;
        printf ("\n") ;

    }
}

static void
fake_set_error (int posix, int win)
{
#if defined(_WIN32)
    (void) posix ;
    fake_error = win ;
#else
    (void) win ;
    errno = posix ;
#endif
}

static void
script_reset (void)
{
    event_count = 0 ;
    event_next = 0 ;
    event_used = 0 ;
    sent_len = 0 ;
    send_max = 0 ;
    send_fail = 0 ;
    connect_fail = 0 ;
    sndtimeo_fail = false ;
    sndtimeo_calls = 0 ;
    sndtimeo_len = 0 ;
    sndtimeo_ms = 0 ;
    alloc_fail_at = 0 ;
    alloc_count = 0 ;

}

static void
script_add (int kind, const char* data, size_t len)
{
    if ((kind == EV_NONE) || ((kind == EV_DATA) && (len == 0))) {
        return ;

    }
    if (event_count >= MAX_EVENTS) {
        printf ("  script too long\n") ;
        exit (2) ;

    }
    events[event_count].kind = kind ;
    events[event_count].data = data ;
    events[event_count].len = len ;
    event_count++ ;
}

/* The next event a plain socket sees. */
static EVENT_T*
fake_next_plain (void)
{
    while ((event_next < event_count) && (events[event_next].kind == EV_WANT_READ)) {
        event_next++ ;

    }

    return (event_next < event_count) ? &events[event_next] : 0 ;
}

static size_t
fake_take (void* buf, size_t len)
{
    EVENT_T*    ev = &events[event_next] ;
    size_t      n = ev->len - event_used ;

    if (n > len) {
        n = len ;

    }
    memcpy (buf, ev->data + event_used, n) ;
    event_used += n ;
    if (event_used == ev->len) {
        event_next++ ;
        event_used = 0 ;

    }

    return n ;
}

static size_t
fake_keep (const void* buf, size_t len)
{
    if (send_max && (len > send_max)) {
        len = send_max ;

    }
    if (len > sizeof (sent) - sent_len) {
        len = sizeof (sent) - sent_len ;

    }
    memcpy (&sent[sent_len], buf, len) ;
    sent_len += len ;

    return len ;
}

static int
fake_socket (int domain, int type, int protocol)
{
    (void) domain ;
    (void) type ;
    (void) protocol ;

    return 3 ;
}

static int
fake_connect (int s, const struct sockaddr* addr, size_t len)
{
    (void) s ;
    (void) addr ;
    (void) len ;

    if (connect_fail == EV_TIMEOUT) {
#if defined(_WIN32)
        fake_set_error (0, WSAETIMEDOUT) ;
#else
        fake_set_error (ETIMEDOUT, 0) ;
#endif
        return -1 ;

    }
    if (connect_fail) {
#if defined(_WIN32)
        fake_set_error (0, WSAECONNREFUSED) ;
#else
        fake_set_error (ECONNREFUSED, 0) ;
#endif
        return -1 ;

    }

    return 0 ;
}

static long
fake_send (int s, const void* buf, size_t len, int flags)
{
    (void) s ;
    (void) flags ;

    if (send_fail) {
        int kind = send_fail ;

        send_fail = 0 ;
#if defined(_WIN32)
        fake_set_error (0, (kind == EV_TIMEOUT) ? WSAETIMEDOUT : WSAECONNRESET) ;
#else
        fake_set_error ((kind == EV_TIMEOUT) ? EAGAIN : ECONNRESET, 0) ;
#endif
        return -1 ;

    }

    return (long)fake_keep (buf, len) ;
}

static long
fake_recv (int s, void* buf, size_t len, int flags)
{
    EVENT_T* ev = fake_next_plain () ;

    (void) s ;
    (void) flags ;

    if (!ev) {
#if defined(_WIN32)
        fake_set_error (0, WSAEWOULDBLOCK) ;
#else
        fake_set_error (EAGAIN, 0) ;
#endif
        return -1 ;

    }
    if (ev->kind == EV_DATA) {
        return (long)fake_take (buf, len) ;

    }
    if (ev->kind == EV_TIMEOUT) {
        /* httpclient_read() waits in select() first, which takes the timeout. */
#if defined(_WIN32)
        fake_set_error (0, WSAEWOULDBLOCK) ;
#else
        fake_set_error (EAGAIN, 0) ;
#endif
        return -1 ;

    }
    if (ev->kind == EV_RESET) {
#if defined(_WIN32)
        fake_set_error (0, WSAECONNRESET) ;
#else
        fake_set_error (ECONNRESET, 0) ;
#endif
        return -1 ;

    }

    /* Every kind of close is the end of the stream to a plain socket. */
    return 0 ;
}

static int
fake_select (int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* tv)
{
    EVENT_T* ev ;

    (void) nfds ;
    (void) writefds ;
    (void) tv ;

    if (!exceptfds) {
        /* httpclient_is_connected(): nothing to read is fine. */
        FD_ZERO (readfds) ;
        return 0 ;

    }
    FD_ZERO (exceptfds) ;

#if TEST_TLS
    ev = (event_next < event_count) ? &events[event_next] : 0 ;
    if (ev && (ev->kind == EV_WANT_READ)) {
        /* mbedTLS gets a WANT_READ first, the select() after it finds data. */
        return 1 ;

    }
#else
    ev = fake_next_plain () ;
#endif
    if (!ev || (ev->kind == EV_TIMEOUT)) {
        if (ev) {
            event_next++ ;

        }
        FD_ZERO (readfds) ;
        return 0 ;

    }

    return 1 ;
}

static int
fake_setsockopt (int s, int level, int name, const void* value, size_t len)
{
    (void) s ;

    if ((level != SOL_SOCKET) || (name != SO_SNDTIMEO)) {
        return 0 ;

    }
    sndtimeo_calls++ ;
    sndtimeo_len = len ;
#if defined(_WIN32)
    if (len == sizeof (DWORD)) {
        sndtimeo_ms = (uint32_t)*(const DWORD*)value ;

    }
#else
    if (len == sizeof (struct timeval)) {
        const struct timeval* tv = (const struct timeval*)value ;

        sndtimeo_ms = (uint32_t)(tv->tv_sec * 1000 + tv->tv_usec / 1000) ;

    }
#endif
    if (sndtimeo_fail) {
#if defined(_WIN32)
        fake_set_error (0, WSAENOPROTOOPT) ;
#else
        fake_set_error (ENOPROTOOPT, 0) ;
#endif
        return -1 ;

    }

    return 0 ;
}

static int
fake_shutdown (int s, int how)
{
    (void) s ;
    (void) how ;

    return 0 ;
}

static int
fake_closesocket (int s)
{
    (void) s ;

    return 0 ;
}

static const char*
fake_inet_ntop (int af, const void* src, char* dst, size_t size)
{
    (void) af ;
    (void) src ;
    snprintf (dst, size, "192.0.2.1") ;

    return dst ;
}

#if TEST_TLS
/*===========================================================================*/
/* The fake mbedTLS.                                                         */
/*===========================================================================*/

static mbedtls_ssl_config   fake_conf ;
static int                  handshake_results[4] ;
static int                  handshake_count ;
static int                  handshake_next ;
static int                  setup_result ;
static char                 tls_hostname[MAX_HOSTNAME_LEN + 16] ;
static int                  close_notify_sent ;

void
mbedtls_ssl_init (mbedtls_ssl_context *ssl)
{
    memset (ssl, 0, sizeof (*ssl)) ;
}

int
mbedtls_ssl_setup (mbedtls_ssl_context *ssl, const mbedtls_ssl_config *conf)
{
    (void) ssl ;
    (void) conf ;

    return setup_result ;
}

void
mbedtls_ssl_set_bio (mbedtls_ssl_context *ssl, void *p_bio, mbedtls_ssl_send_t *f_send,
                     mbedtls_ssl_recv_t *f_recv, mbedtls_ssl_recv_timeout_t *f_recv_timeout)
{
    (void) ssl ;
    (void) p_bio ;
    (void) f_send ;
    (void) f_recv ;
    (void) f_recv_timeout ;
}

int
mbedtls_ssl_set_hostname (mbedtls_ssl_context *ssl, const char *hostname)
{
    (void) ssl ;
    snprintf (tls_hostname, sizeof (tls_hostname), "%s", hostname ? hostname : "") ;

    return 0 ;
}

int
mbedtls_ssl_handshake (mbedtls_ssl_context *ssl)
{
    (void) ssl ;

    return (handshake_next < handshake_count) ? handshake_results[handshake_next++] : 0 ;
}

int
mbedtls_ssl_read (mbedtls_ssl_context *ssl, unsigned char *buf, size_t len)
{
    EVENT_T* ev ;

    (void) ssl ;

    if (event_next >= event_count) {
        return MBEDTLS_ERR_SSL_WANT_READ ;

    }
    ev = &events[event_next] ;
    switch (ev->kind) {
    case EV_DATA:
        return (int)fake_take (buf, len) ;

    case EV_CLOSE:
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ;

    case EV_EOF_ZERO:
        return 0 ;

    case EV_EOF_RESET:
    case EV_RESET:
        return MBEDTLS_ERR_NET_CONN_RESET ;

    case EV_WANT_READ:
        event_next++ ;
        return MBEDTLS_ERR_SSL_WANT_READ ;

    default:
        /* EV_TIMEOUT: the select() after this one times out. */
        return MBEDTLS_ERR_SSL_WANT_READ ;

    }
}

int
mbedtls_ssl_write (mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len)
{
    (void) ssl ;

    if (send_fail) {
        int kind = send_fail ;

        send_fail = 0 ;
        return (kind == EV_TIMEOUT) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_CONN_RESET ;

    }

    return (int)fake_keep (buf, len) ;
}

size_t
mbedtls_ssl_get_bytes_avail (const mbedtls_ssl_context *ssl)
{
    (void) ssl ;

    /* The rest of a record that was read in part. */
    if ((event_next < event_count) && (events[event_next].kind == EV_DATA) && event_used) {
        return events[event_next].len - event_used ;

    }

    return 0 ;
}

int
mbedtls_ssl_close_notify (mbedtls_ssl_context *ssl)
{
    (void) ssl ;
    close_notify_sent++ ;

    return 0 ;
}

void
mbedtls_ssl_free (mbedtls_ssl_context *ssl)
{
    (void) ssl ;
}

int
mbedtls_net_send (void *ctx, const unsigned char *buf, size_t len)
{
    (void) ctx ;
    (void) buf ;

    return (int)len ;
}

int
mbedtls_net_recv (void *ctx, unsigned char *buf, size_t len)
{
    (void) ctx ;
    (void) buf ;
    (void) len ;

    return 0 ;
}

#define SSL_CONFIG              (&fake_conf)
#else
#define SSL_CONFIG              0
#endif

/*===========================================================================*/
/* What qoraal provides.                                                     */
/*===========================================================================*/

const QORAAL_CFG_T * _qoraal_instance = 0 ;

void *
qoraal_malloc (QORAAL_HEAP heap, size_t size)
{
    (void) heap ;

    alloc_count++ ;
    if (alloc_fail_at && (alloc_count == alloc_fail_at)) {
        return 0 ;

    }
    allocs++ ;

    return malloc (size) ;
}

void
qoraal_free (QORAAL_HEAP heap, void *mem)
{
    (void) heap ;

    if (mem) {
        allocs-- ;
        free (mem) ;

    }
}

int32_t
svc_logger_type_log (LOGGER_TYPE_T type, uint8_t facility, const char *str, ...)
{
    va_list args ;

    (void) type ;
    (void) facility ;
    if (verbose) {
        printf ("    log: ") ;
        va_start (args, str) ;
        vprintf (str, args) ;
        va_end (args) ;
        printf ("\n") ;

    }

    return 0 ;
}

void
debug_assert (const char *format, ...)
{
    (void) format ;
    printf ("  assert\n") ;
    failures++ ;
}

int
debug_printf (const char *format, ...)
{
    (void) format ;

    return 0 ;
}

/*===========================================================================*/
/* Reading scripted responses.                                               */
/*===========================================================================*/

#define SAME                    0x7FFFFFF0  /* the result is the same as res */

typedef struct {
    const char*     name ;
    const char*     wire ;              /* the response bytes */
    size_t          wire_len ;          /* 0 for strlen (wire) */
    int             end ;               /* what follows the bytes: EV_* or EV_NONE */
    int32_t         res ;               /* body length or error of httpclient_read_response() */
    int32_t         status ;
    const char*     body ;              /* when res >= 0 */
    uint32_t        retry_after ;
    int32_t         res_tls ;           /* the result over TLS, or SAME */
    int32_t         res_stream ;        /* the result of the streaming reader, or SAME */
} CASE_T ;

typedef struct {
    int32_t         res ;
    int32_t         status ;
    uint32_t        retry_after ;
    size_t          len ;
    bool            allocated ;         /* httpclient_read_response() gave a buffer */
    char            body[24 * 1024] ;
} OUTCOME_T ;

static OUTCOME_T    outcome ;
static size_t       cut_step = 1 ;      /* cut a response at every cut_step bytes */

static int32_t
client_open (HTTP_CLIENT_T* client)
{
    struct sockaddr_in addr ;

    memset (&addr, 0, sizeof (addr)) ;
    addr.sin_family = AF_INET ;
    addr.sin_port = htons (443) ;
    httpclient_init (client, 0) ;
    httpclient_set_hostname (client, "est.example.com") ;

    return httpclient_connect (client, &addr, SSL_CONFIG) ;
}

static void
read_whole (OUTCOME_T* out)
{
    HTTP_CLIENT_T   client ;
    char*           body = 0 ;
    int32_t         res ;

    memset (out, 0, sizeof (OUTCOME_T) - sizeof (out->body)) ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_OK, "connect gave %d", res) ;

    out->res = httpclient_read_response (&client, 1000, &body, &out->status) ;
    out->retry_after = client.retry_after ;
    out->allocated = (body != 0) ;
    if ((out->res > 0) && body && ((size_t)out->res < sizeof (out->body))) {
        memcpy (out->body, body, (size_t)out->res) ;
        out->len = (size_t)out->res ;
        CHECK (body[out->res] == '\0', "body not NUL-terminated") ;

    }
    /* The body outlives the client. */
    httpclient_close (&client) ;
    httpclient_free_response (0, body) ;
}

static void
read_stream (OUTCOME_T* out)
{
    HTTP_CLIENT_T   client ;
    uint8_t*        data ;
    int32_t         res ;

    memset (out, 0, sizeof (OUTCOME_T) - sizeof (out->body)) ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_OK, "connect gave %d", res) ;

    res = httpclient_read_response_ex (&client, 1000, &out->status) ;
    out->retry_after = client.retry_after ;
    if (res == HTTP_CLIENT_E_OK) {
        while ((res = httpclient_read_next_ex (&client, 1000, &data)) > 0) {
            if (out->len + (size_t)res <= sizeof (out->body)) {
                memcpy (&out->body[out->len], data, (size_t)res) ;

            }
            out->len += (size_t)res ;

        }
        /* After the end, and after an error, every call gives the same. */
        CHECK (httpclient_read_next_ex (&client, 1000, &data) == res,
                "httpclient_read_next_ex() after %d gave something else", res) ;
        res = (res == 0) ? (int32_t)out->len : res ;

    } else {
        CHECK (httpclient_read_next_ex (&client, 1000, &data) == res,
                "httpclient_read_next_ex() after a failed header gave something else") ;

    }
    out->res = res ;

    httpclient_read_complete_ex (&client) ;
    CHECK (httpclient_read_next_ex (&client, 1000, &data) == HTTP_CLIENT_E_PARM,
            "httpclient_read_next_ex() after httpclient_read_complete_ex()") ;
    httpclient_close (&client) ;
}

static void
check_outcome (const CASE_T* c, const char* how, const OUTCOME_T* out, int32_t expected, const char* body)
{
    CHECK (out->res == expected, "%s (%s): result %d, expected %d", c->name, how, (int)out->res, (int)expected) ;
    CHECK (out->status == c->status, "%s (%s): status %d, expected %d", c->name, how, (int)out->status, (int)c->status) ;
    CHECK (out->retry_after == c->retry_after, "%s (%s): retry_after %u, expected %u", c->name, how,
            (unsigned)out->retry_after, (unsigned)c->retry_after) ;
    if ((expected >= 0) && (out->res == expected)) {
        CHECK ((out->len == (size_t)expected) && (memcmp (out->body, body, out->len) == 0),
                "%s (%s): body differs", c->name, how) ;

    }
    CHECK (allocs == 0, "%s (%s): %ld allocations not freed", c->name, how, allocs) ;
    allocs = 0 ;
}

static void
script_response (const char* wire, size_t len, size_t cut, int end, bool bytes)
{
    size_t i ;

    script_reset () ;
    if (bytes) {
        for (i = 0; i < len; i++) {
            script_add (EV_DATA, &wire[i], 1) ;

        }
    } else if (cut) {
        script_add (EV_DATA, wire, cut) ;
        script_add (EV_WANT_READ, 0, 0) ;
        script_add (EV_DATA, &wire[cut], len - cut) ;

    } else {
        script_add (EV_DATA, wire, len) ;

    }
    script_add (end, 0, 0) ;
}

static void
run_case (const CASE_T* c, const char* body)
{
    size_t      len = c->wire_len ? c->wire_len : strlen (c->wire) ;
    int32_t     expected = c->res ;
    int32_t     expected_stream ;
    size_t      cut ;
    char        how[48] ;

#if TEST_TLS
    if (c->res_tls != SAME) {
        expected = c->res_tls ;

    }
#endif
    expected_stream = (c->res_stream != SAME) ? c->res_stream : expected ;
    if (!body) {
        body = c->body ? c->body : "" ;

    }

    for (cut = 0; cut <= len; cut = ((cut + cut_step < len) || (cut == len)) ? cut + cut_step : len) {
        bool bytes = (cut == len) ;

        if (bytes) {
            snprintf (how, sizeof (how), "byte by byte") ;

        } else {
            snprintf (how, sizeof (how), cut ? "cut at %u" : "whole", (unsigned)cut) ;

        }

        script_response (c->wire, len, cut, c->end, bytes) ;
        read_whole (&outcome) ;
        CHECK ((outcome.res >= 0) == outcome.allocated, "%s (%s): buffer given %d for result %d",
                c->name, how, outcome.allocated, (int)outcome.res) ;
        check_outcome (c, how, &outcome, expected, body) ;

        script_response (c->wire, len, cut, c->end, bytes) ;
        read_stream (&outcome) ;
        check_outcome (c, how, &outcome, expected_stream, body) ;

    }
}

#define BAD_STATUS(line)    { "status line " line, line "\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE, \
                              HTTP_CLIENT_E_HEADER, 0, 0, 0, SAME, SAME }
#define GOOD_STATUS(line, status) \
                            { "status line " line, line "\r\nContent-Length: 2\r\n\r\nok", 0, EV_NONE, \
                              2, status, "ok", 0, SAME, SAME }
#define BAD_FIELD(name, fields) \
                            { name, "HTTP/1.1 200 OK\r\n" fields "\r\n\r\n", 0, EV_NONE, \
                              HTTP_CLIENT_E_HEADER, 200, 0, 0, SAME, SAME }
#define TE(name, value, res) \
                            { name, "HTTP/1.1 200 OK\r\nTransfer-Encoding:" value "\r\n\r\n5\r\nhello\r\n0\r\n\r\n", \
                              0, EV_NONE, res, 200, "hello", 0, SAME, SAME }
#define CL(name, value, res) \
                            { name, "HTTP/1.1 200 OK\r\nContent-Length:" value "\r\n\r\nhello", \
                              0, EV_NONE, res, 200, "hello", 0, SAME, SAME }
#define CHUNKED(name, body, res, text) \
                            { name, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" body, \
                              0, EV_CLOSE, res, 200, text, 0, SAME, SAME }

static const CASE_T cases[] = {
    /* Content-Length */
    { "content-length", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", 0, EV_NONE,
      5, 200, "hello", 0, SAME, SAME },
    { "content-length, bytes after the body", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhelloEXTRA", 0, EV_NONE,
      5, 200, "hello", 0, SAME, SAME },
    { "content-length 0", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE,
      0, 200, "", 0, SAME, SAME },
    { "content-length, closed early", "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello", 0, EV_CLOSE,
      HTTP_CLIENT_E_CONNECTION, 200, 0, 0, SAME, SAME },
    { "content-length, reset early", "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello", 0, EV_RESET,
      HTTP_CLIENT_E_CONNECTION, 200, 0, 0, SAME, SAME },
    { "content-length, server stops sending", "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello", 0, EV_NONE,
      HTTP_CLIENT_E_TIMEOUT, 200, 0, 0, SAME, SAME },
    CL ("content-length with whitespace", "   5  ", 5),
    CL ("content-length invalid", " 5a", HTTP_CLIENT_E_CONTENT),
    CL ("content-length negative", " -5", HTTP_CLIENT_E_CONTENT),
    CL ("content-length with a sign", " +5", HTTP_CLIENT_E_CONTENT),
    CL ("content-length empty", "", HTTP_CLIENT_E_CONTENT),
    CL ("content-length list", " 5, 5", HTTP_CLIENT_E_CONTENT),
    CL ("content-length above INT32_MAX", " 2147483648", HTTP_CLIENT_E_LENGTH),
    CL ("content-length above UINT32_MAX", " 99999999999999999999", HTTP_CLIENT_E_LENGTH),
    CL ("content-length twice, the same", " 5\r\nContent-Length: 5", 5),
    CL ("content-length twice, different", " 5\r\nContent-Length: 6", HTTP_CLIENT_E_CONTENT),

    /* chunked */
    CHUNKED ("chunked", "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n", 11, "hello world"),
    CHUNKED ("chunked, extensions", "5;name=value\r\nhello\r\n6 ; x=\"y;z\"\r\n world\r\n0;last\r\n\r\n",
             11, "hello world"),
    CHUNKED ("chunked, trailers", "5\r\nhello\r\n0\r\nExpires: never\r\nX-Sum: 1\r\n\r\n", 5, "hello"),
    CHUNKED ("chunked, hex digits", "000A\r\n0123456789\r\na\r\nabcdefghij\r\n0\r\n\r\n",
             20, "0123456789abcdefghij"),
    CHUNKED ("chunked, LF line ends", "5\nhello\r\n0\n\n", 5, "hello"),
    CHUNKED ("chunked, size above INT32_MAX", "80000000\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_LENGTH, 0),
    CHUNKED ("chunked, size over 32 bits", "100000005\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_LENGTH, 0),
    CHUNKED ("chunked, size over 64 bits", "FFFFFFFFFFFFFFFFFFFF\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_LENGTH, 0),
    CHUNKED ("chunked, size not hex", "zz\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_CONTENT, 0),
    CHUNKED ("chunked, space before the size", " 5\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_CONTENT, 0),
    CHUNKED ("chunked, junk after the size", "5x\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_CONTENT, 0),
    CHUNKED ("chunked, empty size line", "\r\nhello\r\n0\r\n\r\n", HTTP_CLIENT_E_CONTENT, 0),
    CHUNKED ("chunked, data longer than its size", "5\r\nhelloX\r\n0\r\n\r\n", HTTP_CLIENT_E_CONTENT, 0),
    CHUNKED ("chunked, other bytes for the CRLF", "5\r\nhelloXY0\r\n\r\n", HTTP_CLIENT_E_CONTENT, 0),
    CHUNKED ("chunked, closed in the data", "5\r\nhel", HTTP_CLIENT_E_CONNECTION, 0),
    CHUNKED ("chunked, closed before the last chunk", "5\r\nhello\r\n", HTTP_CLIENT_E_CONNECTION, 0),
    CHUNKED ("chunked, closed in the trailer", "5\r\nhello\r\n0\r\nX: y\r\n", HTTP_CLIENT_E_CONNECTION, 0),
    CHUNKED ("chunked, closed before the final CRLF", "5\r\nhello\r\n0\r\n", HTTP_CLIENT_E_CONNECTION, 0),

    /* Transfer-Encoding lists */
    TE ("chunked over Content-Length", " chunked\r\nContent-Length: 100", 5),
    TE ("chunked beside an invalid Content-Length", " chunked\r\nContent-Length: x", 5),
    TE ("chunked, case and whitespace", "  CHUNKED  ", 5),
    TE ("chunked, empty list elements", " , chunked ,", 5),
    TE ("gzip, chunked", " gzip, chunked", HTTP_CLIENT_E_CONTENT),
    TE ("codings over two fields", " gzip\r\nTransfer-Encoding: chunked", HTTP_CLIENT_E_CONTENT),
    TE ("chunked twice", " chunked, chunked", HTTP_CLIENT_E_CONTENT),
    TE ("chunked not last", " chunked, gzip", HTTP_CLIENT_E_CONTENT),
    TE ("identity", " identity", HTTP_CLIENT_E_CONTENT),
    TE ("chunked with a parameter", " chunked;q=1", HTTP_CLIENT_E_CONTENT),
    TE ("no coding", " ", HTTP_CLIENT_E_CONTENT),
    TE ("folded list", " gzip,\r\n chunked", HTTP_CLIENT_E_CONTENT),

    /* a body without a length */
    { "close-delimited, clean close", "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nhello", 0, EV_CLOSE,
      5, 200, "hello", 0, SAME, SAME },
    { "close-delimited, TCP close mbedTLS gives as 0", "HTTP/1.1 200 OK\r\n\r\nhello", 0, EV_EOF_ZERO,
      5, 200, "hello", 0, HTTP_CLIENT_E_CONNECTION, SAME },
    { "close-delimited, TCP close qoraal's BIO gives as a reset", "HTTP/1.1 200 OK\r\n\r\nhello", 0, EV_EOF_RESET,
      5, 200, "hello", 0, HTTP_CLIENT_E_CONNECTION, SAME },
    { "close-delimited, reset", "HTTP/1.1 200 OK\r\n\r\nhello", 0, EV_RESET,
      HTTP_CLIENT_E_CONNECTION, 200, 0, 0, SAME, SAME },
    { "close-delimited, empty", "HTTP/1.1 200 OK\r\n\r\n", 0, EV_CLOSE,
      0, 200, "", 0, SAME, SAME },

    /* interim responses */
    { "interim responses skipped",
      "HTTP/1.1 100 Continue\r\n\r\n"
      "HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n"
      "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 0, EV_NONE,
      2, 200, "ok", 0, SAME, SAME },
    { "interim response with framing fields",
      "HTTP/1.1 102 Processing\r\nContent-Length: 10\r\nRetry-After: 5\r\n\r\n"
      "HTTP/1.1 204 No Content\r\n\r\n", 0, EV_NONE,
      0, 204, "", 0, SAME, SAME },
    { "interim response, then closed", "HTTP/1.1 100 Continue\r\n\r\n", 0, EV_CLOSE,
      HTTP_CLIENT_E_CONNECTION, 0, 0, 0, SAME, SAME },
    { "101 is final", "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
      0, EV_NONE, 0, 101, "", 0, SAME, SAME },

    /* statuses without a body, and error statuses */
    { "204 has no body", "HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n", 0, EV_NONE,
      0, 204, "", 0, SAME, SAME },
    { "304 has no body", "HTTP/1.1 304 Not Modified\r\nTransfer-Encoding: chunked\r\n\r\n", 0, EV_NONE,
      0, 304, "", 0, SAME, SAME },
    { "error status with a body", "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nbad request", 0, EV_NONE,
      11, 400, "bad request", 0, SAME, SAME },
    { "error status, close-delimited", "HTTP/1.1 500 Oops\r\n\r\nstack trace", 0, EV_CLOSE,
      11, 500, "stack trace", 0, SAME, SAME },

    /* Retry-After */
    { "retry-after", "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 120\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE,
      0, 503, "", 120, SAME, SAME },
    { "retry-after with 202", "HTTP/1.1 202 Accepted\r\nretry-after:30\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE,
      0, 202, "", 30, SAME, SAME },
    { "retry-after as a date", "HTTP/1.1 503 x\r\nRetry-After: Fri, 31 Dec 1999 23:59:59 GMT\r\nContent-Length: 0\r\n\r\n",
      0, EV_NONE, 0, 503, "", 0, SAME, SAME },
    { "retry-after too large", "HTTP/1.1 503 x\r\nRetry-After: 99999999999\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE,
      0, 503, "", UINT32_MAX, SAME, SAME },
    { "retry-after malformed", "HTTP/1.1 503 x\r\nRetry-After: 12s\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE,
      0, 503, "", 0, SAME, SAME },
    { "retry-after of an interim response",
      "HTTP/1.1 100 Continue\r\nRetry-After: 5\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 0, EV_NONE,
      0, 200, "", 0, SAME, SAME },

    /* status lines */
    BAD_STATUS ("HTTP/1.1 2000 OK"),
    BAD_STATUS ("HTTP/1.1 20 OK"),
    BAD_STATUS ("HTTP/1.1 20a OK"),
    BAD_STATUS ("HTTP/1.1 -20 OK"),
    BAD_STATUS ("HTTP/1.1 099 Low"),
    BAD_STATUS ("HTTP/1.1 600 High"),
    BAD_STATUS ("HTTP/1.1  200 OK"),
    BAD_STATUS ("HTTP/1.1200 OK"),
    BAD_STATUS ("HTTP/2.0 200 OK"),
    BAD_STATUS ("HTTX/1.1 200 OK"),
    BAD_STATUS ("http/1.1 200 OK"),
    BAD_STATUS ("garbage"),
    BAD_STATUS (""),
    GOOD_STATUS ("HTTP/1.0 200 OK", 200),
    GOOD_STATUS ("HTTP/1.9 200 OK", 200),
    GOOD_STATUS ("HTTP/1.1 200", 200),
    GOOD_STATUS ("HTTP/1.1 599 Odd", 599),

    /* header fields */
    BAD_FIELD ("field without a colon", "Content-Length 5"),
    BAD_FIELD ("space before the colon", "Content-Length : 5"),
    BAD_FIELD ("field without a name", ": 5"),
    BAD_FIELD ("whitespace before the first field", " X: y\r\nContent-Length: 0"),
    /* Found while looking for the end of the head, before the status line is taken. */
    { "bare LF in the header", "HTTP/1.1 200 OK\r\nX: y\nZ: w\r\n\r\n", 0, EV_NONE,
      HTTP_CLIENT_E_HEADER, 0, 0, 0, SAME, SAME },
    { "bare CR in the header", "HTTP/1.1 200 OK\r\nX: y\rZ: w\r\n\r\n", 0, EV_NONE,
      HTTP_CLIENT_E_HEADER, 0, 0, 0, SAME, SAME },
    { "NUL in the header", "HTTP/1.1 200 OK\r\nX: a\0b\r\nContent-Length: 0\r\n\r\n",
      sizeof ("HTTP/1.1 200 OK\r\nX: a\0b\r\nContent-Length: 0\r\n\r\n") - 1, EV_NONE,
      HTTP_CLIENT_E_HEADER, 0, 0, 0, SAME, SAME },
    { "folded field", "HTTP/1.1 200 OK\r\nX-Long: a\r\n\tb\r\nContent-Length: 2\r\n\r\nok", 0, EV_NONE,
      2, 200, "ok", 0, SAME, SAME },

    /* no response */
    { "no response, closed", "", 0, EV_CLOSE, HTTP_CLIENT_E_CONNECTION, 0, 0, 0, SAME, SAME },
    { "no response, reset", "", 0, EV_RESET, HTTP_CLIENT_E_CONNECTION, 0, 0, 0, SAME, SAME },
    { "no response, timeout", "", 0, EV_NONE, HTTP_CLIENT_E_TIMEOUT, 0, 0, 0, SAME, SAME },
    { "header cut short", "HTTP/1.1 200 OK\r\nContent-Le", 0, EV_CLOSE,
      HTTP_CLIENT_E_CONNECTION, 0, 0, 0, SAME, SAME },
} ;

static void
test_table (void)
{
    size_t i ;

    for (i = 0; i < sizeof (cases) / sizeof (cases[0]); i++) {
        run_case (&cases[i], 0) ;

    }
    printf ("response table: %u responses, each whole, cut at every byte and byte by byte\n",
            (unsigned)(sizeof (cases) / sizeof (cases[0]))) ;
}

/* Responses too large to write out. */
static char         big_wire[48 * 1024] ;
static char         big_body[24 * 1024] ;

static void
test_large (void)
{
    CASE_T      c ;
    size_t      len ;
    size_t      n ;
    uint32_t    i ;

    memset (&c, 0, sizeof (c)) ;
    c.wire = big_wire ;
    c.end = EV_NONE ;
    c.res_tls = SAME ;
    c.res_stream = SAME ;
    for (i = 0; i < sizeof (big_body); i++) {
        big_body[i] = (char)('a' + i % 26) ;

    }

    /* A header of exactly HTTP_CLIENT_MAX_RECV_HEADER_LENGTH bytes, and one more. */
    for (n = 0; n < 2; n++) {
        len = (size_t)snprintf (big_wire, sizeof (big_wire), "HTTP/1.1 200 OK\r\nX-Pad: ") ;
        while (len < HTTP_CLIENT_MAX_RECV_HEADER_LENGTH + n - 23) {
            big_wire[len++] = 'p' ;

        }
        len += (size_t)snprintf (&big_wire[len], sizeof (big_wire) - len, "\r\nContent-Length: 2\r\n\r\nok") ;
        c.name = n ? "header one byte over the limit" : "header at the limit" ;
        c.res = n ? HTTP_CLIENT_E_LENGTH : 2 ;
        c.status = n ? 0 : 200 ;
        c.body = "ok" ;
        CHECK (len - 2 == HTTP_CLIENT_MAX_RECV_HEADER_LENGTH + n, "header test is %u bytes", (unsigned)(len - 2)) ;
        run_case (&c, 0) ;

    }

    /* Interim heads count towards the limit. */
    for (n = 0; n < 2; n++) {
        len = (size_t)snprintf (big_wire, sizeof (big_wire), "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nX-Pad: ") ;
        while (len < HTTP_CLIENT_MAX_RECV_HEADER_LENGTH + n - 23) {
            big_wire[len++] = 'p' ;

        }
        len += (size_t)snprintf (&big_wire[len], sizeof (big_wire) - len, "\r\nContent-Length: 2\r\n\r\nok") ;
        c.name = n ? "interim and final header one byte over the limit" : "interim and final header at the limit" ;
        c.res = n ? HTTP_CLIENT_E_LENGTH : 2 ;
        c.status = n ? 0 : 200 ;
        run_case (&c, 0) ;

    }

    /* A chunk size line that does not fit the receive buffer. */
    len = (size_t)snprintf (big_wire, sizeof (big_wire), "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;x=") ;
    while (len < 100 + HTTP_CLIENT_MAX_RECV_BUFFER_LENGTH) {
        big_wire[len++] = 'e' ;

    }
    len += (size_t)snprintf (&big_wire[len], sizeof (big_wire) - len, "\r\nhello\r\n0\r\n\r\n") ;
    c.name = "chunk size line longer than the receive buffer" ;
    c.res = HTTP_CLIENT_E_LENGTH ;
    c.status = 200 ;
    run_case (&c, 0) ;

    /* A chunked body that makes the buffer grow, in chunks of 1000. */
    len = (size_t)snprintf (big_wire, sizeof (big_wire), "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") ;
    for (i = 0; i < 5; i++) {
        len += (size_t)snprintf (&big_wire[len], sizeof (big_wire) - len, "3e8\r\n%.1000s\r\n", &big_body[i * 1000]) ;

    }
    len += (size_t)snprintf (&big_wire[len], sizeof (big_wire) - len, "0\r\n\r\n") ;
    c.name = "chunked body of 5000 bytes" ;
    c.res = 5000 ;
    c.wire_len = len ;
    run_case (&c, big_body) ;

    /* Bodies over HTTP_CLIENT_MAX_RESPONSE_LENGTH: only the streaming reader
     * takes them. The cuts that matter are tested above, so not every byte. */
    cut_step = 251 ;
    len = (size_t)snprintf (big_wire, sizeof (big_wire), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n",
            (unsigned)HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1) ;
    memcpy (&big_wire[len], big_body, HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1) ;
    len += HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1 ;
    c.name = "content-length over the response limit" ;
    c.wire_len = len ;
    c.res = HTTP_CLIENT_E_LENGTH ;
    c.res_stream = HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1 ;
    run_case (&c, big_body) ;

    len = (size_t)snprintf (big_wire, sizeof (big_wire), "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%x\r\n",
            (unsigned)HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1) ;
    memcpy (&big_wire[len], big_body, HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1) ;
    len += HTTP_CLIENT_MAX_RESPONSE_LENGTH + 1 ;
    len += (size_t)snprintf (&big_wire[len], sizeof (big_wire) - len, "\r\n0\r\n\r\n") ;
    c.name = "chunked body over the response limit" ;
    c.wire_len = len ;
    run_case (&c, big_body) ;
    cut_step = 1 ;

    printf ("large responses: header limit %u bytes, interim heads included, long chunk lines, growth, response limit %u bytes\n",
            (unsigned)HTTP_CLIENT_MAX_RECV_HEADER_LENGTH, (unsigned)HTTP_CLIENT_MAX_RESPONSE_LENGTH) ;
}

/* A timeout leaves the body reader where it was, so the next call goes on. */
static void
test_timeout_resume (void)
{
    static const char   head[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r" ;
    static const char   rest[] = "\nhello\r\n0\r\n\r\n" ;
    HTTP_CLIENT_T       client ;
    uint8_t*            data ;
    int32_t             status = 0 ;
    int32_t             res ;

    script_reset () ;
    script_add (EV_DATA, head, sizeof (head) - 1) ;
    script_add (EV_TIMEOUT, 0, 0) ;
    script_add (EV_DATA, rest, sizeof (rest) - 1) ;
    client_open (&client) ;
    res = httpclient_read_response_ex (&client, 1000, &status) ;
    CHECK ((res == HTTP_CLIENT_E_OK) && (status == 200), "header %d, status %d", res, status) ;
    res = httpclient_read_next_ex (&client, 1000, &data) ;
    CHECK (res == HTTP_CLIENT_E_TIMEOUT, "first read %d, expected a timeout", res) ;
    res = httpclient_read_next_ex (&client, 1000, &data) ;
    CHECK ((res == 5) && (memcmp (data, "hello", 5) == 0), "read after the timeout %d", res) ;
    res = httpclient_read_next_ex (&client, 1000, &data) ;
    CHECK (res == 0, "end %d", res) ;
    res = httpclient_read_next_ex (&client, 1000, &data) ;
    CHECK (res == 0, "after the end %d", res) ;
    httpclient_close (&client) ;
    CHECK (allocs == 0, "%ld allocations not freed", allocs) ;
    allocs = 0 ;

    printf ("timeout in a body: the next read goes on, after the end every read gives 0\n") ;
}

/*===========================================================================*/
/* Requests.                                                                 */
/*===========================================================================*/

static const char   stream_source[] = "0123456789" ;

static int32_t
stream_body (HTTP_STREAM_WRITE stream_write, uintptr_t ctx, uint32_t parm, uint32_t len)
{
    (void) parm ;

    /* At most 3 bytes at a time, so it takes several calls. */
    return stream_write (ctx, &stream_source[sizeof (stream_source) - 1 - len], (len < 3) ? len : 3) ;
}

static int32_t
stream_too_much (HTTP_STREAM_WRITE stream_write, uintptr_t ctx, uint32_t parm, uint32_t len)
{
    (void) parm ;
    (void) len ;

    return stream_write (ctx, "toolong", 7) ;
}

static void
check_sent (const char* name, int32_t res, const char* expected)
{
    size_t len = strlen (expected) ;

    CHECK (res == (int32_t)len, "%s: returned %d, expected %u", name, (int)res, (unsigned)len) ;
    CHECK ((sent_len == len) && (memcmp (sent, expected, len) == 0), "%s: sent\n%.*s", name, (int)sent_len, sent) ;
}

static void
test_requests (void)
{
    HTTP_CLIENT_T   client ;
    char            endpoint[HTTP_CLIENT_MAX_XMIT_BUFFER_LENGTH + 8] ;
    int32_t         res ;

    script_reset () ;
    send_max = 7 ;
    client_open (&client) ;
    res = httpclient_get (&client, ".well-known/est/cacerts", 0) ;
    check_sent ("GET", res,
            "GET /.well-known/est/cacerts HTTP/1.1\r\n"
            "Host: est.example.com\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n"
            "\r\n") ;
    httpclient_close (&client) ;

    script_reset () ;
    client_open (&client) ;
    httpclient_set_hostname (&client, "") ;
    res = httpclient_get (&client, 0, "") ;
    check_sent ("GET by address, empty credentials", res,
            "GET / HTTP/1.1\r\n"
            "Host: 192.0.2.1\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n"
            "\r\n") ;
    httpclient_close (&client) ;

    script_reset () ;
    client_open (&client) ;
    httpclient_set_content_type (&client, "application/pkcs10") ;
    res = httpclient_post (&client, ".well-known/est/simpleenroll", "hello", 5, 0) ;
    check_sent ("POST", res,
            "POST /.well-known/est/simpleenroll HTTP/1.1\r\n"
            "Host: est.example.com\r\n"
            "Content-Type: application/pkcs10\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "hello") ;
    httpclient_close (&client) ;

    script_reset () ;
    client_open (&client) ;
    res = httpclient_post (&client, "upload", "hello", 5, "user:pass") ;
    check_sent ("POST with Basic auth", res,
            "POST /upload HTTP/1.1\r\n"
            "Host: est.example.com\r\n"
            "Content-Type: text/plain\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n"
            "Authorization: Basic dXNlcjpwYXNz\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "hello") ;
    httpclient_close (&client) ;

    script_reset () ;
    client_open (&client) ;
    res = httpclient_post_chunked (&client, "upload", "hello", 5, 0) ;
    check_sent ("chunked POST", res,
            "POST /upload HTTP/1.1\r\n"
            "Host: est.example.com\r\n"
            "Content-Type: text/plain\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "5\r\nhello\r\n"
            "0\r\n\r\n") ;
    httpclient_close (&client) ;

    script_reset () ;
    client_open (&client) ;
    res = httpclient_post_stream (&client, "upload", stream_body, 0, 10, 0) ;
    check_sent ("streamed POST", res,
            "POST /upload HTTP/1.1\r\n"
            "Host: est.example.com\r\n"
            "Content-Type: text/plain\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
            "User-Agent: " HTTP_CLIENT_AGENT_NAME "\r\n"
            "Content-Length: 10\r\n"
            "\r\n"
            "0123456789") ;
    res = httpclient_post_stream (&client, "upload", stream_too_much, 0, 5, 0) ;
    CHECK (res == HTTP_CLIENT_E_ERROR, "stream writing more than its length gave %d", res) ;
    httpclient_close (&client) ;

    script_reset () ;
    client_open (&client) ;
    res = httpclient_get (&client, "a b", 0) ;
    CHECK (res == HTTP_CLIENT_E_PARM, "endpoint with a space gave %d", res) ;
    res = httpclient_get (&client, "a\r\nX: y", 0) ;
    CHECK (res == HTTP_CLIENT_E_PARM, "endpoint with CRLF gave %d", res) ;
    memset (endpoint, 'e', sizeof (endpoint) - 1) ;
    endpoint[sizeof (endpoint) - 1] = '\0' ;
    res = httpclient_get (&client, endpoint, 0) ;
    CHECK (res == HTTP_CLIENT_E_LENGTH, "endpoint over the buffer gave %d", res) ;
    res = httpclient_get (&client, "x", endpoint) ;
    CHECK (res == HTTP_CLIENT_E_LENGTH, "credentials over the buffer gave %d", res) ;
    res = httpclient_post (&client, "x", 0, -1, 0) ;
    CHECK (res == HTTP_CLIENT_E_PARM, "negative length gave %d", res) ;
    CHECK (sent_len == 0, "a refused request sent %u bytes", (unsigned)sent_len) ;
    httpclient_close (&client) ;
    res = httpclient_get (&client, "x", 0) ;
    CHECK (res == HTTP_CLIENT_E_PARM, "GET without a connection gave %d", res) ;

    CHECK (allocs == 0, "%ld allocations not freed", allocs) ;
    allocs = 0 ;
    printf ("request heads: GET, POST, POST with Basic auth, chunked and streamed POST, refused endpoints\n") ;
}

/*===========================================================================*/
/* Send timeouts, connect, setters.                                          */
/*===========================================================================*/

static void
test_send_timeout (void)
{
    HTTP_CLIENT_T   client ;
    int32_t         res ;

    script_reset () ;
    client_open (&client) ;
    res = httpclient_get (&client, "x", 0) ;
    CHECK (res > 0, "GET gave %d", res) ;
#if defined(_WIN32)
    CHECK (sndtimeo_len == sizeof (DWORD), "SO_SNDTIMEO size %u, Winsock takes a DWORD", (unsigned)sndtimeo_len) ;
#else
    CHECK (sndtimeo_len == sizeof (struct timeval), "SO_SNDTIMEO size %u, expected a struct timeval", (unsigned)sndtimeo_len) ;
#endif
    CHECK (sndtimeo_ms == HTTP_CLIENT_SEND_TIMEOUT, "SO_SNDTIMEO %u ms", (unsigned)sndtimeo_ms) ;
    res = httpclient_post (&client, "x", "abc", 3, 0) ;
    CHECK (res > 0, "POST gave %d", res) ;
    CHECK (sndtimeo_calls == 1, "SO_SNDTIMEO set %d times for one timeout", sndtimeo_calls) ;

    send_fail = EV_TIMEOUT ;
    res = httpclient_get (&client, "x", 0) ;
    CHECK (res == HTTP_CLIENT_E_TIMEOUT, "send timing out gave %d", res) ;
    send_fail = EV_RESET ;
    res = httpclient_get (&client, "x", 0) ;
    CHECK (res == HTTP_CLIENT_E_CONNECTION, "send reset gave %d", res) ;
    httpclient_close (&client) ;

    /* A stack without SO_SNDTIMEO still sends. */
    script_reset () ;
    sndtimeo_fail = true ;
    client_open (&client) ;
    res = httpclient_get (&client, "x", 0) ;
    CHECK (res > 0, "GET without SO_SNDTIMEO gave %d", res) ;
    httpclient_close (&client) ;

    CHECK (allocs == 0, "%ld allocations not freed", allocs) ;
    allocs = 0 ;
    printf ("send timeout: SO_SNDTIMEO as the platform takes it, set once, timeout and reset codes\n") ;
}

static void
test_connect (void)
{
    HTTP_CLIENT_T       client ;
    struct sockaddr_in  addr ;
    char                name[MAX_HOSTNAME_LEN + 1] ;
    int32_t             res ;

    script_reset () ;
    connect_fail = EV_TIMEOUT ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_TIMEOUT, "connect timing out gave %d", res) ;
    CHECK (client.socket == -1, "socket left open") ;
    httpclient_close (&client) ;

    script_reset () ;
    connect_fail = EV_RESET ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_CONNECTION, "connect refused gave %d", res) ;
    httpclient_close (&client) ;

    script_reset () ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_OK, "connect gave %d", res) ;
    memset (&addr, 0, sizeof (addr)) ;
    res = httpclient_connect (&client, &addr, SSL_CONFIG) ;
    CHECK (res == HTTP_CLIENT_E_PARM, "connecting twice gave %d", res) ;
    httpclient_close (&client) ;

#if !TEST_TLS
    httpclient_init (&client, 0) ;
    res = httpclient_connect (&client, &addr, (void*)&client) ;
    CHECK (res == HTTP_CLIENT_E_PARM, "a TLS configuration without TLS built in gave %d", res) ;
    httpclient_close (&client) ;
#endif

    /* httpclient_set_hostname() refuses, and leaves the name empty. */
    httpclient_init (&client, 0) ;
    memset (name, 'h', sizeof (name)) ;
    name[MAX_HOSTNAME_LEN - 1] = '\0' ;
    res = httpclient_set_hostname (&client, name) ;
    CHECK ((res == HTTP_CLIENT_E_OK) && (strcmp (client.hostname, name) == 0), "longest host name gave %d", res) ;
    name[MAX_HOSTNAME_LEN - 1] = 'h' ;
    name[MAX_HOSTNAME_LEN] = '\0' ;
    res = httpclient_set_hostname (&client, name) ;
    CHECK ((res == HTTP_CLIENT_E_PARM) && !client.hostname[0], "host name one too long gave %d", res) ;
    httpclient_set_hostname (&client, "a.example") ;
    res = httpclient_set_hostname (&client, "a.example\r\nX: y") ;
    CHECK ((res == HTTP_CLIENT_E_PARM) && !client.hostname[0], "host name with CRLF gave %d", res) ;
    res = httpclient_set_hostname (&client, 0) ;
    CHECK ((res == HTTP_CLIENT_E_PARM) && !client.hostname[0], "host name 0 gave %d", res) ;
    res = httpclient_set_content_type (&client, "text/plain\r\nX: y") ;
    CHECK (res == HTTP_CLIENT_E_PARM, "content type with CRLF gave %d", res) ;

    /* A 0 client. */
    CHECK (httpclient_init (0, 0) == HTTP_CLIENT_E_PARM, "httpclient_init (0)") ;
    CHECK (httpclient_set_hostname (0, "x") == HTTP_CLIENT_E_PARM, "httpclient_set_hostname (0)") ;
    CHECK (httpclient_connect (0, &addr, 0) == HTTP_CLIENT_E_PARM, "httpclient_connect (0)") ;
    CHECK (httpclient_get (0, "x", 0) == HTTP_CLIENT_E_PARM, "httpclient_get (0)") ;
    CHECK (httpclient_read_response (0, 1, 0, 0) == HTTP_CLIENT_E_PARM, "httpclient_read_response (0)") ;
    CHECK (httpclient_read_response_ex (0, 1, 0) == HTTP_CLIENT_E_PARM, "httpclient_read_response_ex (0)") ;
    CHECK (httpclient_read_complete_ex (0) == HTTP_CLIENT_E_PARM, "httpclient_read_complete_ex (0)") ;
    httpclient_close (0) ;
    httpclient_free_response (0, 0) ;

    CHECK (allocs == 0, "%ld allocations not freed", allocs) ;
    allocs = 0 ;
    printf ("connect and setters: timeout and refusal codes, host name refused instead of truncated, 0 client\n") ;
}

static void
test_memory (void)
{
    static const char   wire[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n" ;
    HTTP_CLIENT_T       client ;
    char*               body ;
    int32_t             status ;
    int32_t             res ;
    long                fail ;

    /* Fail each allocation of a request and response in turn. */
    for (fail = 1; fail <= 4; fail++) {
        script_reset () ;
        script_add (EV_DATA, wire, sizeof (wire) - 1) ;
        client_open (&client) ;
        alloc_count = 0 ;
        alloc_fail_at = fail ;
        res = httpclient_get (&client, "x", 0) ;
        if (res > 0) {
            body = (char*)1 ;
            res = httpclient_read_response (&client, 1000, &body, &status) ;
            CHECK ((res == 5) || ((res == HTTP_CLIENT_E_MEMORY) && !body),
                    "allocation %ld failing gave %d", fail, res) ;
            httpclient_free_response (0, body) ;

        } else {
            CHECK (res == HTTP_CLIENT_E_MEMORY, "allocation %ld failing for the request gave %d", fail, res) ;

        }
        httpclient_close (&client) ;
        CHECK (allocs == 0, "allocation %ld failing: %ld allocations not freed", fail, allocs) ;
        allocs = 0 ;

    }

    printf ("memory: each allocation failing in turn gives HTTP_CLIENT_E_MEMORY and frees the rest\n") ;
}

#if TEST_TLS
static void
test_tls (void)
{
    static const struct {
        const char* name ;
        int         result ;
        int32_t     res ;
    } handshakes[] = {
        { "verify failed", MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, HTTP_CLIENT_E_SSL_TRUST },
        { "bad certificate", MBEDTLS_ERR_SSL_BAD_CERTIFICATE, HTTP_CLIENT_E_SSL_TRUST },
        { "no CA", MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED, HTTP_CLIENT_E_PARM },
        { "no host name", MBEDTLS_ERR_SSL_CERTIFICATE_VERIFICATION_WITHOUT_HOSTNAME, HTTP_CLIENT_E_PARM },
        { "alert", MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE, HTTP_CLIENT_E_SSL_CONNECT },
        { "handshake failure", MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE, HTTP_CLIENT_E_SSL_CONNECT },
        { "reset", MBEDTLS_ERR_NET_CONN_RESET, HTTP_CLIENT_E_CONNECTION },
        { "closed", MBEDTLS_ERR_SSL_CONN_EOF, HTTP_CLIENT_E_CONNECTION },
        { "send timeout", MBEDTLS_ERR_SSL_WANT_WRITE, HTTP_CLIENT_E_TIMEOUT },
        { "out of memory", MBEDTLS_ERR_SSL_ALLOC_FAILED, HTTP_CLIENT_E_MEMORY },
    } ;
    HTTP_CLIENT_T   client ;
    size_t          i ;
    int32_t         res ;

    for (i = 0; i < sizeof (handshakes) / sizeof (handshakes[0]); i++) {
        script_reset () ;
        handshake_results[0] = handshakes[i].result ;
        handshake_count = 1 ;
        handshake_next = 0 ;
        res = client_open (&client) ;
        CHECK (res == handshakes[i].res, "handshake %s gave %d, expected %d",
                handshakes[i].name, res, handshakes[i].res) ;
        CHECK (client.ssl != 0, "handshake %s: TLS context gone before httpclient_close()", handshakes[i].name) ;
        httpclient_close (&client) ;

    }

    /* Waiting for the server during the handshake. */
    script_reset () ;
    script_add (EV_DATA, "x", 1) ;
    handshake_results[0] = MBEDTLS_ERR_SSL_WANT_READ ;
    handshake_results[1] = 0 ;
    handshake_count = 2 ;
    handshake_next = 0 ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_OK, "handshake after WANT_READ gave %d", res) ;
    CHECK (sndtimeo_ms == HTTP_CLIENT_CONNECT_TIMEOUT, "SO_SNDTIMEO %u ms during the handshake", (unsigned)sndtimeo_ms) ;
    CHECK (strcmp (tls_hostname, "est.example.com") == 0, "SNI %s", tls_hostname) ;
    httpclient_close (&client) ;
    CHECK (close_notify_sent > 0, "no close_notify sent") ;

    script_reset () ;
    handshake_results[0] = MBEDTLS_ERR_SSL_WANT_READ ;
    handshake_count = 1 ;
    handshake_next = 0 ;
    res = client_open (&client) ;
    CHECK (res == HTTP_CLIENT_E_TIMEOUT, "handshake without an answer gave %d", res) ;
    httpclient_close (&client) ;
    handshake_count = 0 ;

    script_reset () ;
    setup_result = MBEDTLS_ERR_SSL_ALLOC_FAILED ;
    res = client_open (&client) ;
    CHECK ((res == HTTP_CLIENT_E_MEMORY) && !client.ssl, "mbedtls_ssl_setup() out of memory gave %d", res) ;
    httpclient_close (&client) ;
    setup_result = 0 ;

    CHECK (allocs == 0, "%ld allocations not freed", allocs) ;
    allocs = 0 ;
    printf ("TLS: handshake errors mapped, WANT_READ waits, SNI set, close_notify sent\n") ;
}
#endif

int
main (int argc, char* argv[])
{
    verbose = (argc > 1) && (strcmp (argv[1], "-v") == 0) ;

#if TEST_TLS
    printf ("httpclient unit test, over a fake TLS connection\n") ;
#else
    printf ("httpclient unit test, over a fake plain TCP connection\n") ;
#endif

    test_table () ;
    test_large () ;
    test_timeout_resume () ;
    test_requests () ;
    test_send_timeout () ;
    test_connect () ;
    test_memory () ;
#if TEST_TLS
    test_tls () ;
#endif

    printf ("%d checks, %d failed\n", checks, failures) ;

    return failures ? 1 : 0 ;
}
