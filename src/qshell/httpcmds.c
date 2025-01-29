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
#include "qoraal/config.h"
#include "qoraal/qoraal.h"
#include "qoraal/svc/svc_shell.h"
#include "qoraal-http/httpdwnld.h"

SVC_SHELL_CMD_DECL("wsource", qshell_wsource, "<url>");

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
                script.dwnld.name, res) ;
        qoraal_free(script.heap, script.mem) ;

    } else  {
        svc_shell_print (pif, SVC_SHELL_OUT_STD,
                "script download %d bytes for %s\r\n",
                res, script.dwnld.name) ;

        svc_shell_script_clear_last_error (pif) ;
        res = svc_shell_script_run (pif, script.dwnld.name, script.mem, script.offset) ;

        svc_shell_print (pif, SVC_SHELL_OUT_STD,
                "script %s complete with %d\r\n",
                script.dwnld.name, res) ;

        qoraal_free(script.heap, script.mem) ;

    }

    return res >= EOK ? SVC_SHELL_CMD_E_OK : res ;

}

void
keep_httpcmds (void)
{
    (void)qshell_wsource ; 
}
