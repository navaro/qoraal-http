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
 * @file        test_httpserver.c
 *
 * Host unit test of how the HTTP server reads a request, without a network.
 * httpserver.c is compiled into this file with its socket calls replaced by
 * fakes that play a scripted byte stream. The cases cover the Content-Length
 * values httpserver_read_request_ex() accepts and refuses, and the limit
 * httpserver_read_all_content_ex() puts on a body it reads into memory. The
 * build sets CFG_HTTPSERVER_CONTENT_MAX to 64 to keep the bodies short. A
 * server assert fails the test. Run with -v to see the server's log.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qoraal/qoraal.h"
#include "qoraal-http/config.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpparse.h"
#include "qoraal-http/httpserver.h"

/* The platform socket headers are in already, so these calls only change
 * in httpserver.c below. Function-like, as HTTP_USER_T has a member named
 * socket. */
#undef closesocket
#define recv(...)               fake_recv (__VA_ARGS__)
#define send(...)               fake_send (__VA_ARGS__)
#define select(...)             fake_select (__VA_ARGS__)
#define closesocket(...)        fake_closesocket (__VA_ARGS__)

static long fake_recv (int s, void* buf, size_t len, int flags) ;
static long fake_send (int s, const void* buf, size_t len, int flags) ;
static int  fake_select (int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* tv) ;
static int  fake_closesocket (int s) ;

#include "../../src/httpserver.c"

#define TEST_SOCKET     3

static const char*  _script ;
static size_t       _script_len ;
static size_t       _script_pos ;
static bool         _verbose ;
static int          _asserts ;
static int          _live ;
static size_t       _largest ;
static int          _failures ;
static size_t       _fail_size ;        /* an allocation of this size fails */

/* The clock, os_sys_timestamp(). A select() that times out moves it on by
 * its timeout, and one that waits for a byte to arrive, to that byte. */
static uint32_t     _clock ;
static uint32_t     _t0 ;               /* when the test started */
static size_t       _fast_len ;         /* script bytes there at _t0 */
static uint32_t     _byte_ms ;          /* each byte after them comes this much later; 0: all there */
static bool         _eof ;              /* the client closes after the script */
static int          _closes ;           /* closesocket() calls */
static size_t       _sent ;             /* bytes send() took */
static uint32_t     _send_ms ;          /* time each send() takes */
static size_t       _send_max ;         /* most bytes one send() takes; 0: all */

/* ------------------------------------------------------------------ fakes */

/* Script bytes that have arrived by now. */
static size_t
arrived (void)
{
    size_t n = _script_len ;

    if (_byte_ms) {
        n = _fast_len + (_clock - _t0) / _byte_ms ;
        if (n > _script_len) {
            n = _script_len ;
        }
    }
    return n ;
}

static long
fake_recv (int s, void* buf, size_t len, int flags)
{
    size_t n = arrived () - _script_pos ;

    (void)s ;
    (void)flags ;
    if (n > len) {
        n = len ;
    }
    memcpy (buf, &_script[_script_pos], n) ;
    _script_pos += n ;
    return (long)n ;    /* 0 only at the end with _eof */
}

static long
fake_send (int s, const void* buf, size_t len, int flags)
{
    size_t n = (_send_max && (len > _send_max)) ? _send_max : len ;

    (void)s ;
    (void)buf ;
    (void)flags ;
    _clock += _send_ms ;
    _sent += n ;
    return (long)n ;
}

/* Always writable. Readable once a script byte has arrived, or at the end
 * with _eof; else it waits for the next byte, up to its timeout. */
static int
fake_select (int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* tv)
{
    uint32_t wait = tv ? (uint32_t)(tv->tv_sec * 1000 + tv->tv_usec / 1000) : 0 ;

    (void)nfds ;
    if (exceptfds) {
        FD_ZERO (exceptfds) ;
    }
    if (writefds) {
        if (readfds) {
            FD_ZERO (readfds) ;
        }
        return 1 ;
    }
    if (_script_pos < _script_len) {
        if (arrived () > _script_pos) {
            return 1 ;
        }
        uint32_t next = _t0 + (uint32_t)(_script_pos - _fast_len + 1) * _byte_ms ;
        if (next - _clock <= wait) {
            _clock = next ;
            return 1 ;
        }
    } else if (_eof) {
        return 1 ;
    }
    _clock += wait ;
    if (readfds) {
        FD_ZERO (readfds) ;
    }
    return 0 ;
}

static int
fake_closesocket (int s)
{
    (void)s ;
    _closes++ ;
    return 0 ;
}

void*
qoraal_malloc (QORAAL_HEAP heap, size_t size)
{
    void* p ;

    (void)heap ;
    if (_fail_size && (size == _fail_size)) {
        return 0 ;
    }
    p = malloc (size ? size : 1) ;
    if (p) {
        _live++ ;
        if (size > _largest) {
            _largest = size ;
        }
    }
    return p ;
}

void
qoraal_free (QORAAL_HEAP heap, void* mem)
{
    (void)heap ;
    if (mem) {
        _live-- ;
        free (mem) ;
    }
}

void
debug_assert (const char* format, ...)
{
    va_list args ;

    _asserts++ ;
    printf ("  server assert: ") ;
    va_start (args, format) ;
    vprintf (format, args) ;
    va_end (args) ;
    printf ("\n") ;
}

void
svc_wdt_handler_kick (SVC_WDT_HANDLE_T * handler)
{
    (void)handler ;
}

void
os_sys_lock (void)
{
}

void
os_sys_unlock (void)
{
}

uint32_t
os_sys_timestamp (void)
{
    return _clock ;
}

int32_t
svc_logger_type_log (LOGGER_TYPE_T type, uint8_t facility, const char* str, ...)
{
    va_list args ;

    (void)type ;
    (void)facility ;
    if (_verbose) {
        printf ("  log: ") ;
        va_start (args, str) ;
        vprintf (str, args) ;
        va_end (args) ;
        printf ("\n") ;
    }
    return 0 ;
}

/* ------------------------------------------------------------------ helpers */

static void
check (bool ok, const char* name, const char* what)
{
    if (!ok) {
        printf ("FAIL %s: %s\n", name, what) ;
        _failures++ ;
    }
}

static void
start (HTTP_USER_T* user, const char* script, size_t len)
{
    httpserver_user_init (user) ;
    user->socket = TEST_SOCKET ;
    user->write_timeout = 4000 ;
    _script = script ;
    _script_len = len ;
    _script_pos = 0 ;
    _asserts = 0 ;
    _live = 0 ;
    _clock += 100000 ;
    _t0 = _clock ;
    _fast_len = len ;
    _byte_ms = 0 ;
    _eof = false ;
    _closes = 0 ;
    _sent = 0 ;
    _send_ms = 0 ;
    _send_max = 0 ;
}

/* Bytes after the first fast ones come one every byte_ms. */
static void
trickle (size_t fast, uint32_t byte_ms)
{
    _fast_len = fast ;
    _byte_ms = byte_ms ;
}

/* Read one request as wserver_m.c does: a read timeout (-1) is waited out
 * with another read, which asserts if the first one left the request set. */
static int32_t
read_request (HTTP_USER_T* user)
{
    char* endpoint ;
    int32_t method ;
    int32_t res = httpserver_read_request_ex (user, 500, &endpoint, &method) ;

    if (res == HTTP_SERVER_E_ERROR) {
        (void)httpserver_read_request_ex (user, 500, &endpoint, &method) ;
    }
    return res ;
}

/* ------------------------------------------------------------------ cases */

static const struct {
    const char* value ;
    int32_t     expect ;
} _lengths[] = {
    { "0",              0 },
    { "5",              5 },
    { "5 ",             5 },
    { "5\t",            5 },
    { "007",            7 },
    { "2147483647",     2147483647 },
    { "2147483648",     HTTP_SERVER_E_LENGTH },
    { "4294967294",     HTTP_SERVER_E_LENGTH },
    { "4294967295",     HTTP_SERVER_E_LENGTH },   /* was -1, a read timeout */
    { "4294967296",     HTTP_SERVER_E_LENGTH },   /* sscanf("%u") wrapped it to 0 */
    { "99999999999",    HTTP_SERVER_E_LENGTH },
    { "-1",             HTTP_SERVER_E_LENGTH },
    { "+5",             HTTP_SERVER_E_LENGTH },
    { "5x",             HTTP_SERVER_E_LENGTH },
    { "0x10",           HTTP_SERVER_E_LENGTH },
    { "1 2",            HTTP_SERVER_E_LENGTH },
    { "",               HTTP_SERVER_E_LENGTH },
} ;

static void
test_content_length (void)
{
    for (size_t i = 0; i < sizeof(_lengths) / sizeof(_lengths[0]); i++) {
        HTTP_USER_T user ;
        char request[160] ;
        char name[64] ;
        int len = snprintf (request, sizeof(request),
                "GET /status HTTP/1.1\r\nHost: device\r\nContent-Length: %s\r\n\r\n",
                _lengths[i].value) ;

        snprintf (name, sizeof(name), "Content-Length '%s'", _lengths[i].value) ;
        start (&user, request, (size_t)len) ;
        int32_t res = read_request (&user) ;
        if (res != _lengths[i].expect) {
            char what[64] ;
            snprintf (what, sizeof(what), "returned %d, expected %d", (int)res, (int)_lengths[i].expect) ;
            check (false, name, what) ;
        }
        check (_asserts == 0, name, "the server asserted") ;
        httpserver_user_close (&user) ;
        check (_live == 0, name, "memory left allocated") ;
    }
}

/* A body read with httpserver_read_all_content_ex(): returns its length, or
 * the error, and the largest allocation the read made. */
static int32_t
read_body (const char* name, const char* script, size_t len, char** body, size_t* largest)
{
    static HTTP_USER_T user ;
    char* content = 0 ;
    int32_t res ;

    start (&user, script, len) ;
    res = read_request (&user) ;
    if (res < 0) {
        httpserver_user_close (&user) ;
        return res ;
    }
    _largest = 0 ;
    res = httpserver_read_all_content_ex (&user, 500, &content) ;
    *largest = _largest ;
    if (body) {
        *body = 0 ;
        if ((res > 0) && content) {
            *body = malloc ((size_t)res + 1) ;
            memcpy (*body, content, (size_t)res + 1) ;
        }
    }
    check (_asserts == 0, name, "the server asserted") ;
    httpserver_user_close (&user) ;
    check (_live == 0, name, "memory left allocated") ;
    return res ;
}

static void
test_body_limit (void)
{
    char script[512] ;
    char data[CFG_HTTPSERVER_CONTENT_MAX + 2] ;
    size_t largest ;
    char* body ;
    int len ;
    int32_t res ;

    memset (data, 'a', sizeof(data)) ;

    /* Content-Length bodies: the limit itself, one byte over, and the
     * largest length the request reader takes, which is never allocated. */
    len = snprintf (script, sizeof(script),
            "POST /api HTTP/1.1\r\nContent-Length: %d\r\n\r\n%.*s",
            CFG_HTTPSERVER_CONTENT_MAX, CFG_HTTPSERVER_CONTENT_MAX, data) ;
    res = read_body ("body at the limit", script, (size_t)len, &body, &largest) ;
    check (res == CFG_HTTPSERVER_CONTENT_MAX, "body at the limit", "not read whole") ;
    check (body && (memcmp (body, data, CFG_HTTPSERVER_CONTENT_MAX) == 0), "body at the limit", "wrong content") ;
    free (body) ;

    len = snprintf (script, sizeof(script),
            "POST /api HTTP/1.1\r\nContent-Length: %d\r\n\r\n%.*s",
            CFG_HTTPSERVER_CONTENT_MAX + 1, CFG_HTTPSERVER_CONTENT_MAX + 1, data) ;
    res = read_body ("body over the limit", script, (size_t)len, 0, &largest) ;
    check (res == HTTP_SERVER_E_LENGTH, "body over the limit", "not refused") ;
    check (largest == 0, "body over the limit", "allocated before refusing") ;

    len = snprintf (script, sizeof(script),
            "POST /api HTTP/1.1\r\nContent-Length: 2147483647\r\n\r\n") ;
    res = read_body ("Content-Length INT32_MAX", script, (size_t)len, 0, &largest) ;
    check (res == HTTP_SERVER_E_LENGTH, "Content-Length INT32_MAX", "not refused") ;
    check (largest == 0, "Content-Length INT32_MAX", "allocated before refusing") ;

    /* Chunked bodies: two chunks that reach the limit, and a last byte that
     * takes them over it. */
    len = snprintf (script, sizeof(script),
            "POST /api HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
            "%x\r\n%.*s\r\n%x\r\n%.*s\r\n0\r\n\r\n",
            CFG_HTTPSERVER_CONTENT_MAX / 2, CFG_HTTPSERVER_CONTENT_MAX / 2, data,
            CFG_HTTPSERVER_CONTENT_MAX / 2, CFG_HTTPSERVER_CONTENT_MAX / 2, data) ;
    res = read_body ("chunked at the limit", script, (size_t)len, &body, &largest) ;
    check (res == CFG_HTTPSERVER_CONTENT_MAX, "chunked at the limit", "not read whole") ;
    check (body && (memcmp (body, data, CFG_HTTPSERVER_CONTENT_MAX) == 0), "chunked at the limit", "wrong content") ;
    free (body) ;

    len = snprintf (script, sizeof(script),
            "POST /api HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
            "%x\r\n%.*s\r\n1\r\na\r\n0\r\n\r\n",
            CFG_HTTPSERVER_CONTENT_MAX, CFG_HTTPSERVER_CONTENT_MAX, data) ;
    res = read_body ("chunked over the limit", script, (size_t)len, 0, &largest) ;
    check (res == HTTP_SERVER_E_LENGTH, "chunked over the limit", "not refused") ;
    check (largest <= CFG_HTTPSERVER_CONTENT_MAX + 1, "chunked over the limit", "allocated past the limit") ;
}

/* The endpoint copy fails: the request is refused, and closing the user
 * frees only what was allocated (the header values still point into the
 * read buffer then). */
static void
test_endpoint_alloc_fails (void)
{
    static const char request[] =
        "GET /endpoint-allocation-fails HTTP/1.1\r\nHost: device\r\nConnection: Keep-Alive\r\n\r\n" ;
    const char* name = "endpoint allocation fails" ;
    HTTP_USER_T user ;

    start (&user, request, sizeof(request) - 1) ;
    _fail_size = strlen ("/endpoint-allocation-fails") + 1 ;
    int32_t res = read_request (&user) ;
    if (res != HTTP_SERVER_E_MEMORY) {
        /* The endpoint may be kept without its slash. */
        httpserver_user_close (&user) ;
        start (&user, request, sizeof(request) - 1) ;
        _fail_size = strlen ("endpoint-allocation-fails") + 1 ;
        res = read_request (&user) ;
    }
    _fail_size = 0 ;
    check (res == HTTP_SERVER_E_MEMORY, name, "not refused") ;
    check (user.headers[3].value == 0, name, "header values left pointing into the read buffer") ;
    httpserver_user_close (&user) ;
    check (_asserts == 0, name, "the server asserted") ;
    check (_live == 0, name, "memory left allocated") ;
}

/* One request read as the worker does it, then freed. Returns the length
 * read_request_ex() returned and copies the endpoint and the body. */
static int32_t
next_request (HTTP_USER_T* user, char* endpoint_out, size_t endpoint_size, char* body_out, size_t body_size)
{
    char* endpoint = 0 ;
    char* content = 0 ;
    int32_t method ;
    int32_t res = httpserver_read_request_ex (user, 500, &endpoint, &method) ;

    endpoint_out[0] = 0 ;
    if (body_out) {
        body_out[0] = 0 ;
    }
    if (res >= 0) {
        snprintf (endpoint_out, endpoint_size, "%s", endpoint ? endpoint : "") ;
        if (res > 0) {
            int32_t len = httpserver_read_all_content_ex (user, 500, &content) ;
            if (body_out && (len > 0) && content) {
                snprintf (body_out, body_size, "%.*s", (int)len, content) ;
            }
        }
        (void)httpserver_free_request (user) ;
    }
    return res ;
}

static void
test_pipelined (void)
{
    char endpoint[64] ;
    char body[64] ;
    HTTP_USER_T user ;
    int32_t res ;

    /* Two GETs in one write. */
    static const char two_gets[] =
        "GET /first HTTP/1.1\r\nHost: device\r\n\r\n"
        "GET /second HTTP/1.1\r\nHost: device\r\n\r\n" ;
    start (&user, two_gets, sizeof(two_gets) - 1) ;
    res = next_request (&user, endpoint, sizeof(endpoint), 0, 0) ;
    check ((res == 0) && (strcmp (endpoint, "first") == 0), "two GETs", "first request wrong") ;
    res = next_request (&user, endpoint, sizeof(endpoint), 0, 0) ;
    check ((res == 0) && (strcmp (endpoint, "second") == 0), "two GETs", "second request lost") ;
    check (_asserts == 0, "two GETs", "the server asserted") ;
    httpserver_user_close (&user) ;
    check (_live == 0, "two GETs", "memory left allocated") ;

    /* A POST with a body, then a GET: the body must not take the GET. */
    static const char post_get[] =
        "POST /api HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello"
        "GET /after HTTP/1.1\r\nHost: device\r\n\r\n" ;
    start (&user, post_get, sizeof(post_get) - 1) ;
    res = next_request (&user, endpoint, sizeof(endpoint), body, sizeof(body)) ;
    check ((res == 5) && (strcmp (body, "hello") == 0), "POST then GET", "body wrong") ;
    res = next_request (&user, endpoint, sizeof(endpoint), 0, 0) ;
    check ((res == 0) && (strcmp (endpoint, "after") == 0), "POST then GET", "GET after the body lost") ;
    check (_asserts == 0, "POST then GET", "the server asserted") ;
    httpserver_user_close (&user) ;
    check (_live == 0, "POST then GET", "memory left allocated") ;

    /* The body arrives after the header, followed by the next request:
     * the body read must stop at the end of the body. */
    static const char header_then_body[] =
        "POST /api HTTP/1.1\r\nContent-Length: 5\r\n\r\n" ;
    static const char body_then_get[] =
        "POST /api HTTP/1.1\r\nContent-Length: 5\r\n\r\nworld"
        "GET /later HTTP/1.1\r\nHost: device\r\n\r\n" ;
    start (&user, body_then_get, sizeof(header_then_body) - 1) ;
    char* ep = 0 ;
    char* content = 0 ;
    int32_t method ;
    res = httpserver_read_request_ex (&user, 500, &ep, &method) ;
    _script_len = sizeof(body_then_get) - 1 ;
    int32_t len = httpserver_read_all_content_ex (&user, 500, &content) ;
    check ((res == 5) && (len == 5) && content && (memcmp (content, "world", 5) == 0),
            "body read from the socket", "body wrong") ;
    (void)httpserver_free_request (&user) ;
    res = next_request (&user, endpoint, sizeof(endpoint), 0, 0) ;
    check ((res == 0) && (strcmp (endpoint, "later") == 0), "body read from the socket", "next request lost") ;
    httpserver_user_close (&user) ;
    check (_live == 0, "body read from the socket", "memory left allocated") ;

    /* A header split by a read timeout is kept for the next read. */
    static const char split[] = "GET /split HTTP/1.1\r\nHost: device\r\n\r\n" ;
    start (&user, split, 12) ;
    res = httpserver_read_request_ex (&user, 500, &ep, &method) ;
    check (res == HTTP_SERVER_E_ERROR, "split header", "first part not a read timeout") ;
    _script_len = sizeof(split) - 1 ;
    res = next_request (&user, endpoint, sizeof(endpoint), 0, 0) ;
    check ((res == 0) && (strcmp (endpoint, "split") == 0), "split header", "first part lost") ;
    check (_asserts == 0, "split header", "the server asserted") ;
    httpserver_user_close (&user) ;
    check (_live == 0, "split header", "memory left allocated") ;
}

/* Streams leave one worker for requests: the one after the last slot is
 * refused with a 503 and its connection is not kept. */
static void
test_stream_limit (void)
{
    HTTP_USER_T users[CFG_HTTPSERVER_STREAMS_MAX + 1] ;
    const char* name = "stream limit" ;
    int i ;

    for (i = 0; i < CFG_HTTPSERVER_STREAMS_MAX + 1; i++) {
        start (&users[i], "", 0) ;
        users[i].keep_alive = 1 ;
    }
    for (i = 0; i < CFG_HTTPSERVER_STREAMS_MAX; i++) {
        check (httpserver_stream_open (&users[i]) == 1, name, "a free slot refused") ;
    }
    check (httpserver_stream_open (&users[i]) <= 0, name, "one stream too many opened") ;
    check (users[i].keep_alive == 0, name, "refused connection kept") ;
    httpserver_stream_close (&users[0]) ;
    check (httpserver_stream_open (&users[i]) == 1, name, "a closed slot not given back") ;
    for (i = 1; i < CFG_HTTPSERVER_STREAMS_MAX + 1; i++) {
        httpserver_stream_close (&users[i]) ;
    }
    for (i = 0; i < CFG_HTTPSERVER_STREAMS_MAX + 1; i++) {
        httpserver_user_close (&users[i]) ;
    }
    check (_live == 0, name, "memory left allocated") ;
}

/* A header that trickles in is dropped CFG_HTTPSERVER_HEADER_TIMEOUT after
 * its first byte, whether its bytes come within the read timeout or not.
 * The reads are made as the worker makes them: again after each read
 * timeout, for up to the keep-alive time. */
static void
test_header_deadline (void)
{
    static const uint32_t gaps[] = { 400, 700 } ;
    static char request[600] ;
    int len = snprintf (request, sizeof(request),
            "GET /%0500d HTTP/1.1\r\nHost: device\r\n\r\n", 0) ;

    for (size_t i = 0; i < sizeof(gaps) / sizeof(gaps[0]); i++) {
        HTTP_USER_T user ;
        char name[48] ;
        char* endpoint ;
        int32_t method ;
        int32_t idle = 30000 ;
        int32_t res ;

        snprintf (name, sizeof(name), "header at %u ms a byte", (unsigned)gaps[i]) ;
        start (&user, request, (size_t)len) ;
        trickle (0, gaps[i]) ;
        for (;;) {
            res = httpserver_read_request_ex (&user, 500, &endpoint, &method) ;
            if ((res != HTTP_SERVER_E_ERROR) || ((idle -= 500) <= 0)) {
                break ;
            }
        }
        check (res == HTTP_SERVER_E_TIMEOUT, name, "not dropped") ;
        check (_clock - _t0 <= CFG_HTTPSERVER_HEADER_TIMEOUT + 2 * gaps[i], name, "dropped late") ;
        check (_script_pos < (size_t)len, name, "read whole") ;
        check (_asserts == 0, name, "the server asserted") ;
        httpserver_user_close (&user) ;
        check (_live == 0, name, "memory left allocated") ;
    }
}

/* A body that trickles in: httpserver_read_all_content_ex() gives up
 * CFG_HTTPSERVER_CONTENT_TIMEOUT after it started, with a Content-Length
 * and chunked. The body fits CFG_HTTPSERVER_CONTENT_MAX of this test, and
 * would take twice the time. */
#define TRICKLED_BODY   60

static void
test_body_deadline (void)
{
    static const char* names[] = { "body trickled", "chunked body trickled" } ;
    static char script[512] ;

    for (int chunked = 0; chunked < 2; chunked++) {
        const char* name = names[chunked] ;
        HTTP_USER_T user ;
        char* endpoint ;
        char* content = (char*)1 ;
        int32_t method ;
        int32_t res ;
        int head = chunked ?
                snprintf (script, sizeof(script),
                    "POST /api HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n") :
                snprintf (script, sizeof(script),
                    "POST /api HTTP/1.1\r\nContent-Length: %d\r\n\r\n", TRICKLED_BODY) ;
        int len = head + (chunked ?
                snprintf (&script[head], sizeof(script) - (size_t)head,
                    "%x\r\n%0*d\r\n0\r\n\r\n", TRICKLED_BODY, TRICKLED_BODY, 0) :
                snprintf (&script[head], sizeof(script) - (size_t)head,
                    "%0*d", TRICKLED_BODY, 0)) ;

        start (&user, script, (size_t)len) ;
        trickle ((size_t)head, 2 * CFG_HTTPSERVER_CONTENT_TIMEOUT / TRICKLED_BODY) ;
        res = httpserver_read_request_ex (&user, 500, &endpoint, &method) ;
        check (res >= 0, name, "request not read") ;
        res = httpserver_read_all_content_ex (&user, 4000, &content) ;
        check ((res == HTTP_SERVER_E_TIMEOUT) && !content, name, "not dropped") ;
        check (_clock - _t0 <= CFG_HTTPSERVER_CONTENT_TIMEOUT + 1000, name, "dropped late") ;
        check (_asserts == 0, name, "the server asserted") ;
        httpserver_user_close (&user) ;
        check (_live == 0, name, "memory left allocated") ;
    }
}

/* A body that ends before its Content-Length is an error and none of it is
 * returned: the rest of its buffer would be old heap. The client stops
 * sending, or closes the connection. */
static void
test_short_body (void)
{
    static const char script[] = "POST /api HTTP/1.1\r\nContent-Length: 10\r\n\r\nhello" ;

    for (int eof = 0; eof < 2; eof++) {
        const char* name = eof ? "short body, closed" : "short body, stopped" ;
        HTTP_USER_T user ;
        char* endpoint ;
        char* content = (char*)1 ;
        int32_t method ;
        int32_t res ;

        start (&user, script, sizeof(script) - 1) ;
        _eof = eof ;
        user.keep_alive = 1 ;
        res = httpserver_read_request_ex (&user, 500, &endpoint, &method) ;
        check (res == 10, name, "request not read") ;
        res = httpserver_read_all_content_ex (&user, 500, &content) ;
        check (res == (eof ? HTTP_SERVER_E_CONNECTION : HTTP_SERVER_E_ERROR), name, "not an error") ;
        check (!content && !user.content, name, "content returned") ;
        check (user.keep_alive == 0, name, "connection kept with the rest of the body on it") ;
        check (_asserts == 0, name, "the server asserted") ;
        httpserver_user_close (&user) ;
        check (_live == 0, name, "memory left allocated") ;
    }
}

/* A stream lasts its time unless the client sends something or closes, the
 * writer drops it, or the server stops. It never reads the socket, and a
 * writer that drops it leaves the socket to the worker. */
static void
test_stream_wait (void)
{
    HTTP_USER_T user ;
    int32_t server_close = 0 ;
    int32_t res ;

    start (&user, "", 0) ;
    user.server_close = &server_close ;
    res = httpserver_stream_wait (&user, 5000) ;
    check ((res == HTTP_SERVER_E_OK) && (_clock - _t0 >= 5000) && (_clock - _t0 < 6000),
            "stream idle", "did not last its time") ;
    httpserver_user_close (&user) ;

    start (&user, "x", 1) ;
    trickle (0, 2500) ;
    user.server_close = &server_close ;
    res = httpserver_stream_wait (&user, 60000) ;
    check ((res == HTTP_SERVER_E_CONNECTION) && user.peer_closed && (_clock - _t0 < 4000),
            "stream client sends", "not ended") ;
    check (_script_pos == 0, "stream client sends", "the socket was read") ;
    httpserver_user_close (&user) ;

    start (&user, "", 0) ;
    _eof = true ;
    res = httpserver_stream_wait (&user, 60000) ;
    check ((res == HTTP_SERVER_E_CONNECTION) && (_clock == _t0), "stream client closes", "not ended") ;
    httpserver_user_close (&user) ;

    start (&user, "", 0) ;
    check (httpserver_user_abort (&user) == HTTP_SERVER_E_OK, "stream dropped", "abort failed") ;
    check ((user.socket == TEST_SOCKET) && (_closes == 0), "stream dropped", "the writer closed the socket") ;
    res = httpserver_stream_wait (&user, 60000) ;
    check ((res == HTTP_SERVER_E_CONNECTION) && (_clock == _t0), "stream dropped", "not ended") ;
    httpserver_user_close (&user) ;
    check (_closes == 1, "stream dropped", "the worker did not close the socket") ;

    start (&user, "", 0) ;
    server_close = 1 ;
    user.server_close = &server_close ;
    res = httpserver_stream_wait (&user, 60000) ;
    check ((res == HTTP_SERVER_E_OK) && (_clock == _t0), "stream at server stop", "not ended") ;
    httpserver_user_close (&user) ;
    check (_live == 0, "stream", "memory left allocated") ;
}

/* A write is bounded as a whole by the write timeout: a client that takes
 * a few bytes at a time is dropped then, and writes after it fail at once. */
static void
test_write_deadline (void)
{
    static uint8_t data[1000] ;
    HTTP_USER_T user ;
    int32_t res ;

    start (&user, "", 0) ;
    user.write_timeout = 500 ;
    _send_ms = 100 ;
    _send_max = 10 ;
    res = httpserver_write (&user, data, sizeof(data)) ;
    check ((res == HTTP_SERVER_E_CONNECTION) && user.peer_closed, "slow reader", "not dropped") ;
    check ((_sent < sizeof(data)) && (_clock - _t0 <= 600), "slow reader", "write took too long") ;
    res = httpserver_write (&user, data, 1) ;
    check ((res == HTTP_SERVER_E_CONNECTION) && (_sent < sizeof(data)), "slow reader", "written after the drop") ;
    httpserver_user_close (&user) ;

    start (&user, "", 0) ;
    user.write_timeout = 500 ;
    _send_ms = 1 ;
    _send_max = 100 ;
    res = httpserver_write (&user, data, sizeof(data)) ;
    check ((res == (int32_t)sizeof(data)) && !user.peer_closed, "reader keeps up", "write failed") ;
    httpserver_user_close (&user) ;
    check (_live == 0, "write", "memory left allocated") ;
}

int
main (int argc, char** argv)
{
    _verbose = (argc > 1) && (strcmp (argv[1], "-v") == 0) ;

    test_content_length () ;
    test_body_limit () ;
    test_endpoint_alloc_fails () ;
    test_pipelined () ;
    test_stream_limit () ;
    test_header_deadline () ;
    test_body_deadline () ;
    test_short_body () ;
    test_stream_wait () ;
    test_write_deadline () ;

    if (_failures) {
        printf ("%d failure(s)\n", _failures) ;
        return 1 ;
    }
    printf ("all passed\n") ;
    return 0 ;
}
