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
#include <string.h>
#include "qoraal/qoraal.h"
#include "qoraal/common/rtclib.h"
#include "qoraal-http/wserver.h"



#define WSERVER_CHECK_SEND(x)   if ((x) < EOK) return -2


const char*
wsystem_ctrl (HTTP_USER_T *user, uint32_t method, char* endpoint, uint32_t type)
{
    if (type == WSERVER_CTRL_METADATA_HEADING) {
        return "System" ;
    }
    if (type == WSERVER_CTRL_METADATA_HEADERS) {
        char* groupname = strchr (endpoint, '?') ;
        if (groupname) {
            groupname++ ;
        }

        if (groupname && strncmp(groupname, "reset", 5) == 0) {

                return "<meta http-equiv=\"refresh\" content=\"5; url=/\">" ;

        }
    }

    return 0 ;
}

/**
 * @brief   Handler
 *
 * @app
 */
int32_t
wsystem_handler (HTTP_USER_T *user, uint32_t method, char* endpoint)
{

    if (method == HTTP_HEADER_METHOD_GET) {

        char* cmd[5]  = {0} ;
        cmd[0] = strchr (endpoint, '/') ;
        for (int i=0; i<4; i++) {
            if (cmd[i]) *(cmd[i])++ = 0 ;
            if (cmd[i]) cmd[i+1] = strchr (cmd[i], '/') ;
            if (cmd[i+1] == 0) break ;
        }

        WSERVER_CHECK_SEND(httpserver_chunked_append_str (user,
                    "<style>\r\n"
                    ".sys-hero{padding:1.4rem 1.6rem;border:1px solid #d8ceb8;border-radius:14px;"
                    "background:linear-gradient(135deg,#fffdf8 0%,#efe4c9 100%);margin-bottom:1.4rem;}\r\n"
                    ".sys-hero h5{margin:0 0 .4rem 0;letter-spacing:.04em;text-transform:uppercase;color:#7b6a46;font-size:1.2rem;}\r\n"
                    ".sys-hero p{margin:0;color:#5f5a4b;}\r\n"
                    ".sys-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:1.2rem;margin-bottom:1.4rem;}\r\n"
                    ".sys-card{padding:1.2rem 1.3rem;border:1px solid #ddd4bf;border-radius:12px;background:#fffdfa;box-shadow:0 8px 18px rgba(70,52,18,.06);}\r\n"
                    ".sys-label{margin:0 0 .35rem 0;font-size:1.1rem;text-transform:uppercase;letter-spacing:.05em;color:#8a7854;}\r\n"
                    ".sys-value{margin:0;font-size:2.1rem;line-height:1.1;color:#2f2a1f;}\r\n"
                    ".sys-note{margin:.45rem 0 0 0;color:#6b6558;font-size:1.3rem;}\r\n"
                    ".sys-meta{width:100%;border-collapse:collapse;margin-bottom:1rem;}\r\n"
                    ".sys-meta td{padding:.55rem 0;border-bottom:1px solid #eee4cf;vertical-align:top;}\r\n"
                    ".sys-meta td:first-child{width:36%;color:#7a705d;font-weight:600;}\r\n"
                    ".sys-actions{padding:1rem 1.2rem;border:1px solid #e1d8c3;border-radius:12px;background:#f7f1e3;}\r\n"
                    ".sys-actions p{margin:0 0 .8rem 0;color:#655f52;}\r\n"
                    "</style>\r\n"
                    "<div class=\"sys-hero\">"
                    "<h5>System Snapshot</h5>"
                    "<p>Core runtime details and service health in one quick view.</p>"
                    "</div>\r\n",
                    0)) ;

        WSERVER_CHECK_SEND(httpserver_chunked_append_fmtstr (user,
                    "<div class=\"sys-grid\">"
                    "<div class=\"sys-card\">"
                    "<p class=\"sys-label\">Version</p>"
                    "<p class=\"sys-value\">v%.2d.%.2d%c</p>"
                    "<p class=\"sys-note\">Build %.5d</p>"
                    "</div>"
                    "<div class=\"sys-card\">"
                    "<p class=\"sys-label\">Build Name</p>"
                    "<p class=\"sys-value\" style=\"font-size:1.9rem;\">%s</p>"
                    "<p class=\"sys-note\">HTTP example environment</p>"
                    "</div>",
                    0, 0, 'a' + 1, 99,
                    "Qoraal HTTP")) ;

    	RTCLIB_DATE_T   date ;
    	RTCLIB_TIME_T   time ;
        rtc_localtime (rtc_time(), &date, &time) ;

        uint32_t hours, minutes, seconds ;
        seconds = OS_TICKS2S(os_sys_ticks() ) ;
        minutes = seconds / 60 ;
        seconds = seconds % 60 ;
        hours = minutes / 60 ;
        minutes = minutes % 60 ;

        WSERVER_CHECK_SEND(httpserver_chunked_append_fmtstr (user,
                    "<div class=\"sys-card\">"
                    "<p class=\"sys-label\">Local Time</p>"
                    "<p class=\"sys-value\">%.2d:%.2d:%.2d</p>"
                    "<p class=\"sys-note\">%.4d-%.2d-%.2d</p>"
                    "</div>"
                    "<div class=\"sys-card\">"
                    "<p class=\"sys-label\">Uptime</p>"
                    "<p class=\"sys-value\">%u:%02u:%02u</p>"
                    "<p class=\"sys-note\">hours : minutes : seconds</p>"
                    "</div>"
                    "</div>\r\n",
                    (int32_t)time.hour, (int32_t)time.minute, (int32_t)time.second,
                    (int32_t)date.year, (int32_t)date.month, (int32_t)date.day,
                    hours, minutes, seconds)) ;

        WSERVER_CHECK_SEND(httpserver_chunked_append_fmtstr (user,
                "<table class=\"sys-meta\">\r\n"
                "<tbody>\r\n"
                "<tr><td>Service</td><td>Core system page</td></tr>\r\n"
                "<tr><td>Status</td><td>Running</td></tr>\r\n"
                "<tr><td>Refresh</td><td>Reload the page for a fresh snapshot</td></tr>\r\n"
                "</tbody></table>\r\n"
                "<div class=\"sys-actions\">"
                "<p>System-level actions live here. Keep them intentional.</p>"
                "<A style=\"width:100%%; margin-bottom:0;\" class=\"button button-primary\" href=\"system/shutdown\" >Shutdown</A>"
                "</div>\r\n"
             )) ;

        if (cmd[0] && strcmp(cmd[0], "shutdown") == 0) {
            // svc_service_stop_timeout (svc_service_get(QORAAL_SERVICE_WWW), 0) ;

        }
        
    } else {
        return HTTP_SERVER_WSERVER_E_METHOD ;

    }

    return HTTP_SERVER_WSERVER_E_OK ;
}
