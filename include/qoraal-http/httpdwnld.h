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

#ifndef __DWNLDMAN_H__
#define __DWNLDMAN_H__

#include <stdint.h>
#include "config.h"
#include "qoraal/qoraal.h"
#include "qoraal/qfs.h"
#include "qoraal-http/httpclient.h"

#define DBG_MESSAGE_HTTPDWNLD(severity, fmt_str, ...)           \
    DBG_MESSAGE_T_LOG(SVC_LOGGER_TYPE(severity,0), 0, fmt_str, ##__VA_ARGS__)
#define DBG_ASSERT_HTTPDWNLD                    DBG_ASSERT

#define HTTPDWNLD_STATUS_IDLE                   0
#define HTTPDWNLD_STATUS_STARTED                1
#define HTTPDWNLD_STATUS_ERROR                  2
#define HTTPDWNLD_STATUS_COMPLETE               3

typedef int32_t (*HTTP_CLIENT_DOWNLOAD_CB) (int32_t status,
                                            uint8_t * buffer,
                                            uint32_t len,
                                            uintptr_t parm) ;

typedef struct HTTPDWNLD_S {

    int             https ;
    int             port  ;
    char *          host ;
    char *          name /* endpoint */ ;
    char *          credentials ;

    HTTP_STREAM_NEXT_T  stream ;
    uint32_t            parm ;
    uint32_t        postlen ;

    uint32_t        mss ;
    uint32_t        bytes ;
    uint32_t        cancel ;
    uint32_t        cnt ;

} HTTPDWNLD_T ;

typedef struct HTTPDWNLD_MEM_S {

    HTTPDWNLD_T     dwnld ;
    QORAAL_HEAP     heap ;
    uint32_t        offset ;
    uint32_t        grow ;
    uint32_t        capacity ;
    char*           mem ;

} HTTPDWNLD_MEM_T ;


/* Filesystem-backed download using qfs abstraction */
typedef struct HTTPDWNLD_FS_S {

    HTTPDWNLD_T     dwnld ;

    qfs_file_t *    file ;
    int             opened ;

    uint32_t        capacity ;   /* optional max size; 0 = unlimited */
    const char*     path ;       /* target path (relative or absolute for qfs) */

} HTTPDWNLD_FS_T ;

#ifdef __cplusplus
extern "C" {
#endif

void        httpdwnld_init (HTTPDWNLD_T * dwnld) ;
void        httpdwnld_mem_init (HTTPDWNLD_MEM_T * dwnld) ;
void        httpdwnld_fs_init     (HTTPDWNLD_FS_T * dwnld, const char * path) ;

void        httpdwnld_cancel (HTTPDWNLD_T * dwnld) ;
uint32_t    httpdwnld_bytes (HTTPDWNLD_T * dwnld) ;

int32_t     httpdwnld_mem_download (char* url,
                                    HTTPDWNLD_MEM_T* file,
                                    uint32_t timeout) ;

int32_t     httpdwnld_fs_download  (char* url,
                                    HTTPDWNLD_FS_T* fs,
                                    uint32_t timeout) ;

int32_t     httpdwnld_test_download(char* url,
                                    HTTPDWNLD_T* dwmld,
                                    uint32_t timeout) ;

int32_t     httpdwnld_mem_post     (char* url,
                                    HTTP_STREAM_NEXT_T stream,
                                    uint32_t parm,
                                    uint32_t len,
                                    HTTPDWNLD_MEM_T* mem,
                                    uint32_t timeout) ;

#ifdef __cplusplus
}
#endif

#endif /* __DWNLDMAN_H__ */
