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
#define select(...)             fake_select (__VA_ARGS__)
#define closesocket(...)        fake_closesocket (__VA_ARGS__)

static long fake_recv (int s, void* buf, size_t len, int flags) ;
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

/* ------------------------------------------------------------------ fakes */

static long
fake_recv (int s, void* buf, size_t len, int flags)
{
    size_t n = _script_len - _script_pos ;

    (void)s ;
    (void)flags ;
    if (n > len) {
        n = len ;
    }
    memcpy (buf, &_script[_script_pos], n) ;
    _script_pos += n ;
    return (long)n ;
}

/* Readable while script bytes are left, else a timeout. */
static int
fake_select (int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* tv)
{
    (void)nfds ;
    (void)writefds ;
    (void)tv ;
    if (exceptfds) {
        FD_ZERO (exceptfds) ;
    }
    if (_script_pos < _script_len) {
        return 1 ;
    }
    if (readfds) {
        FD_ZERO (readfds) ;
    }
    return 0 ;
}

static int
fake_closesocket (int s)
{
    (void)s ;
    return 0 ;
}

void*
qoraal_malloc (QORAAL_HEAP heap, size_t size)
{
    void* p = malloc (size ? size : 1) ;

    (void)heap ;
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
    _script = script ;
    _script_len = len ;
    _script_pos = 0 ;
    _asserts = 0 ;
    _live = 0 ;
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

int
main (int argc, char** argv)
{
    _verbose = (argc > 1) && (strcmp (argv[1], "-v") == 0) ;

    test_content_length () ;
    test_body_limit () ;

    if (_failures) {
        printf ("%d failure(s)\n", _failures) ;
        return 1 ;
    }
    printf ("all passed\n") ;
    return 0 ;
}
