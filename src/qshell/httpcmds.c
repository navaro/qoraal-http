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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "qoraal/config.h"
#include "qoraal/qoraal.h"
#include "qoraal/qfs.h"
#include "qoraal-http/qoraal.h"
#include "qoraal/svc/svc_shell.h"
#include "qoraal-http/httpdwnld.h"
#include "qoraal-http/httpparse.h"
#if defined(CFG_OS_ZEPHYR)
#include <zephyr/fs/fs.h>
#endif


SVC_SHELL_CMD_DECL("wsource", qshell_wsource, "<url>");
#if defined(CFG_OS_POSIX) || defined(CFG_OS_ZEPHYR)
SVC_SHELL_CMD_DECL("wget", qshell_wget, "<url>");
#endif


typedef struct qfs_stream_s {
#if defined(CFG_OS_ZEPHYR) || defined(CFG_OS_ZEPHYR)
    struct fs_file_t zf;
#else
    FILE *fp;
#endif
    char abs[QFS_PATH_MAX];
} qfs_stream_t;

/* Open a stream for binary write at a path (normalized through qfs_make_abs) */
static int qfs_stream_open(qfs_stream_t *st, const char *path)
{
    memset(st, 0, sizeof(*st));
    if (qfs_make_abs(st->abs, sizeof(st->abs), path) != 0) {
        return -1;
    }

#if defined(CFG_OS_ZEPHYR) 
    fs_file_t_init(&st->zf);
    int rc = fs_open(&st->zf, st->abs, FS_O_CREATE | FS_O_WRITE);
    if (rc) return rc;
    /* Truncate if pre-existing */
    rc = fs_truncate(&st->zf, 0);
    return rc;
#else
    st->fp = fopen(st->abs, "wb");
    return (st->fp != NULL) ? 0 : -1;
#endif
}

static int qfs_stream_write(qfs_stream_t *st, const void *buf, size_t len)
{
#if defined(CFG_OS_ZEPHYR)
    ssize_t w = fs_write(&st->zf, buf, len);
    return (w < 0) ? (int)w : 0;
#else
    size_t w = fwrite(buf, 1, len, st->fp);
    return (w == len) ? 0 : -1;
#endif
}

static void qfs_stream_close(qfs_stream_t *st)
{
#if defined(CFG_OS_ZEPHYR)
    (void)fs_close(&st->zf);
#else
    if (st->fp) fclose(st->fp);
#endif
}

static int32_t
qshell_wsource (SVC_SHELL_IF_T * pif, char** argv, int argc)
{
    int32_t res ;
    HTTPDWNLD_MEM_T script ;

    if (argc < 2) {
        return SVC_SHELL_CMD_E_PARMS ;
        
    }

    httpdwnld_mem_init (&script) ;
    script.grow = 8*1024 ;
    script.heap = QORAAL_HeapAuxiliary ;

    res = httpdwnld_mem_download (argv[1], &script, 4000) ;
    if (res < 0) {
        svc_shell_print (pif, SVC_SHELL_OUT_STD,
                "ERROR: script downloading %s failed with %d!\r\n",
                script.dwnld.name?script.dwnld.name:"unknown", res) ;
        qoraal_free(script.heap, script.mem) ;

    } else  {
        svc_shell_print (pif, SVC_SHELL_OUT_STD,
                "script download %d bytes for %s\r\n",
                res, script.dwnld.name) ;

        svc_shell_script_clear_last_error (pif) ;
        res = svc_shell_script_run (pif, script.dwnld.name, script.mem, script.offset) ;

        svc_shell_print (pif, SVC_SHELL_OUT_STD,
                "script %s complete with %d\r\n",
                script.dwnld.name?script.dwnld.name:"unknown", res) ;

        qoraal_free(script.heap, script.mem) ;

    }

    return res >= EOK ? SVC_SHELL_CMD_E_OK : res ;

}



static int resolve_hostname(const char *hostname, uint32_t *ip)
{
    struct addrinfo hints, *res0 = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;      /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */

    int rc = getaddrinfo(hostname, NULL, &hints, &res0);
    if (rc != 0) return HTTP_CLIENT_E_HOST;

    struct sockaddr_in *addr = (struct sockaddr_in *)res0->ai_addr;
    *ip = addr->sin_addr.s_addr;
    freeaddrinfo(res0);
    return EOK;
}

int32_t qshell_wget(SVC_SHELL_IF_T *pif, char **argv, int argc)
{
    HTTP_CLIENT_T client;
    int32_t status;
    uint8_t *response;
    int32_t res;
    uint32_t ip;
    struct sockaddr_in addr;
    int https, port;
    char *host, *path, *credentials;

    if (argc < 2) {
        return SVC_SHELL_CMD_E_PARMS;
    }

    /* Parse the URL */
    res = httpparse_url_parse(argv[1], &https, &port, &host, &path, &credentials);
    if (res != EOK) {
        svc_shell_print(pif, SVC_SHELL_OUT_STD, "Failed to parse URL: %s\r\n", argv[1]);
        return res;
    }

    /* Derive a file name from the URL path */
    const char *leaf = "index.html";
    if (path && *path) {
        const char *slash = strrchr(path, '/');
        leaf = (slash && *(slash + 1)) ? (slash + 1) : path;
    }

    /* Build absolute path via qfs and open stream */
    qfs_stream_t st;
    if (qfs_stream_open(&st, leaf) != 0) {
        svc_shell_print(pif, SVC_SHELL_OUT_STD, "Failed to open file for write: %s\r\n", leaf);
        return -1;
    }

    /* Resolve hostname */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (resolve_hostname(host, &ip) != EOK) {
        svc_shell_print(pif, SVC_SHELL_OUT_STD, "HTTP: resolving %s failed!\r\n", host);
        qfs_stream_close(&st);
        return HTTP_CLIENT_E_HOST;
    }
    addr.sin_addr.s_addr = ip;

    /* Init client */
    httpclient_init(&client, 0);
    httpclient_set_hostname(&client, host);

    void *pssl_config = 0;
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE) || !CFG_HTTPCLIENT_TLS_DISABLE
    if (https) {
        pssl_config = mbedtlsutils_get_client_config();
        if (!pssl_config) {
            svc_shell_print(pif, SVC_SHELL_OUT_STD, "ssl config failed!\r\n");
            qfs_stream_close(&st);
            return HTTP_CLIENT_E_SSL_CONNECT;
        }
    }
#endif

    res = httpclient_connect(&client, &addr, pssl_config);
    if (res != HTTP_CLIENT_E_OK) {
        svc_shell_print(pif, SVC_SHELL_OUT_STD, "Failed to connect to server\r\n");
        qfs_stream_close(&st);
        httpclient_close(&client);
        return res;
    }

    /* GET */
    res = httpclient_get(&client, path, credentials);
    if (res < 0) {
        svc_shell_print(pif, SVC_SHELL_OUT_STD, "GET %s failed\r\n", path);
        qfs_stream_close(&st);
        httpclient_close(&client);
        return res;
    }

    /* Read status/headers */
    res = httpclient_read_response_ex(&client, 5000, &status);
    if (res < 0 || status / 100 != 2) {
        svc_shell_print(pif, SVC_SHELL_OUT_STD,
                        "Failed to read response (status %d, rc %d)\r\n", status, res);
        qfs_stream_close(&st);
        httpclient_close(&client);
        return (res < 0) ? res : -1;
    }

    /* Stream body to file using qfs-normalized path */
    while ((res = httpclient_read_next_ex(&client, 5000, &response)) > 0) {
        int wrc = qfs_stream_write(&st, response, (size_t)res);
        if (wrc != 0) {
            svc_shell_print(pif, SVC_SHELL_OUT_STD, "Write failed to %s\r\n", st.abs);
            httpclient_close(&client);
            qfs_stream_close(&st);
            return -1;
        }
    }

    qfs_stream_close(&st);
    httpclient_close(&client);

    svc_shell_print(pif, SVC_SHELL_OUT_STD, "Download complete: %s\r\n", leaf);
    return res >= EOK ? SVC_SHELL_CMD_E_OK : res;
}


/* -------------------------------------------------------------------------- */
/* Keep                                                                       */
/* -------------------------------------------------------------------------- */

void keep_httpcmds(void)
{
    (void)qshell_wsource;
#if defined(CFG_OS_POSIX) || defined(CFG_OS_ZEPHYR)
    (void)qshell_wget;
#endif
}
