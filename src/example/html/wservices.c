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
#include "qoraal/svc/svc_services.h"
#include "qoraal-http/wserver.h"



const char*
wservices_ctrl (HTTP_USER_T *user, uint32_t method, char* endpoint, uint32_t type)
{
    if (type == WSERVER_CTRL_METADATA_HEADING) {
        return "Services" ;

    }
    if (type == WSERVER_CTRL_METADATA_HEADERS) {
        if (strchr (endpoint, '?')) {
            return "<meta http-equiv=\"refresh\" content=\"1;url=/services\">";

        }
    }

    return 0 ;
}

int32_t
wservices_handler (HTTP_USER_T *user, uint32_t method, char* endpoint)
{
    if (method == HTTP_HEADER_METHOD_GET) {

        SCV_SERVICE_HANDLE h ;
        uint32_t total_services = 0 ;
        uint32_t running_services = 0 ;

        char* request = strchr (endpoint, '?') ;
        if (request) {
            request++ ;

            //DBG_MESSAGE_WWW (DBG_MESSAGE_SEVERITY_INFO, 
            //        "service %s", request)

            for (h = svc_service_first(); h!=SVC_SERVICE_INVALID_HANDLE; ) {

                if (strncmp(request, svc_service_name(h), strlen(svc_service_name(h))) == 0) {

                    if (svc_service_status(h) != SVC_SERVICE_STATUS_STOPPED) {
                //        DBG_MESSAGE_WWW (DBG_MESSAGE_SEVERITY_INFO, 
                //                "stopping %s", svc_service_name(h))
                        svc_service_stop (h,0,0) ;

                    } else{
                //        DBG_MESSAGE_WWW (DBG_MESSAGE_SEVERITY_INFO, 
                //                "starting %s", svc_service_name(h))
                        svc_service_start (h, 0, 0) ;

                    }

                    break ;

                }

                h = svc_service_next(h);

            }
        }

        for (h = svc_service_first(); h!=SVC_SERVICE_INVALID_HANDLE; h = svc_service_next(h)) {
            total_services++ ;
            if (svc_service_status(h) != SVC_SERVICE_STATUS_STOPPED) {
                running_services++ ;
            }
        }

        httpserver_chunked_append_str (user,
                    "<style>\r\n"
                    ".svc-hero{padding:1.4rem 1.6rem;border:1px solid #d8ceb8;border-radius:14px;"
                    "background:linear-gradient(135deg,#fffdf8 0%,#efe4c9 100%);margin-bottom:1.3rem;}\r\n"
                    ".svc-hero h5{margin:0 0 .4rem 0;letter-spacing:.04em;text-transform:uppercase;color:#7b6a46;font-size:1.2rem;}\r\n"
                    ".svc-hero p{margin:0;color:#5f5a4b;}\r\n"
                    ".svc-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:1rem;margin-bottom:1.2rem;}\r\n"
                    ".svc-card{padding:1rem 1.1rem;border:1px solid #e2d7bf;border-radius:12px;background:#fffdfa;box-shadow:0 8px 18px rgba(65,49,17,.05);}\r\n"
                    ".svc-card h6{margin:0 0 .3rem 0;color:#7a6640;text-transform:uppercase;letter-spacing:.05em;font-size:1.05rem;}\r\n"
                    ".svc-card p{margin:0;color:#2f281c;font-size:2rem;line-height:1.1;}\r\n"
                    ".svc-table-wrap{padding:1rem 1.2rem;border:1px solid #e1d8c3;border-radius:12px;background:#fffdfa;box-shadow:0 8px 18px rgba(65,49,17,.04);margin-bottom:1rem;}\r\n"
                    ".svc-table{width:100%;border-collapse:collapse;margin-bottom:1rem;}\r\n"
                    ".svc-table th,.svc-table td{padding:.7rem .2rem;border-bottom:1px solid #eee4cf;text-align:left;vertical-align:middle;}\r\n"
                    ".svc-table th{color:#7a705d;font-size:1.1rem;text-transform:uppercase;letter-spacing:.05em;}\r\n"
                    ".svc-badge{display:inline-block;padding:.2rem .65rem;border-radius:999px;font-size:1.15rem;font-weight:600;}\r\n"
                    ".svc-badge.on{background:#e5f2df;color:#4f7242;}\r\n"
                    ".svc-badge.off{background:#efe7d8;color:#7e6c51;}\r\n"
                    ".svc-action{display:inline-block;padding:.35rem .8rem;border:1px solid #d7c8aa;border-radius:999px;background:#f7f1e3;color:#5f533d;text-decoration:none;font-weight:600;}\r\n"
                    ".svc-toolbar{padding:1rem 1.2rem;border:1px solid #e1d8c3;border-radius:12px;background:#f7f1e3;}\r\n"
                    ".svc-toolbar form{margin-bottom:0;}\r\n"
                    "</style>\r\n"
                    "<div class=\"svc-hero\">"
                    "<h5>Services</h5>"
                    "<p>Start, stop, and inspect the runtime building blocks behind the device.</p>"
                    "</div>\r\n", 0) ;

        httpserver_chunked_append_fmtstr (user,
                    "<div class=\"svc-grid\">"
                    "<div class=\"svc-card\"><h6>Total</h6><p>%u</p></div>"
                    "<div class=\"svc-card\"><h6>Running</h6><p>%u</p></div>"
                    "<div class=\"svc-card\"><h6>Stopped</h6><p>%u</p></div>"
                    "</div>\r\n",
                    total_services,
                    running_services,
                    total_services - running_services) ;

        httpserver_chunked_append_str (user,
                    "<div class=\"svc-table-wrap\"><table class=\"svc-table\">\r\n"
                    "<thead><tr>"
                    "<th>Service</th>"
                    "<th>Status</th>"
                    "<th>Action</th>"
                    "</tr></thead><tbody>\r\n", 0) ;


        for (h = svc_service_first(); h!=SVC_SERVICE_INVALID_HANDLE; ) {

            const char * name = svc_service_name(h) ;
            const char * next_state = svc_service_status(h) != SVC_SERVICE_STATUS_STOPPED ? "Stop" : "Start" ;
            const char* status_class = svc_service_status(h) == SVC_SERVICE_STATUS_STOPPED ? "off" : "on" ;
            const char* status_text = svc_service_status(h) == SVC_SERVICE_STATUS_STOPPED ? "Stopped" :
                                      (svc_service_status(h) == SVC_SERVICE_STATUS_STARTED) ? "Running" :
                                                                                              "Busy" ;

            httpserver_chunked_append_fmtstr (user,
                    "<tr><td>%s</td>"
                    "<td><span class=\"svc-badge %s\">%s</span></td>"
                    "<td><a class=\"svc-action\" href=\"/services?%s=%s\">%s</a></td>"
                    "</tr>\r\n",
                    name,
                    status_class,
                    status_text,
                    name,
                    next_state,
                    next_state) ;

            h = svc_service_next(h);


        }


        httpserver_chunked_append_str (user,
                "</tbody></table></div>\r\n"
                "<div class=\"svc-toolbar\">"
                "<form action=\"\" method=\"get\"><input type=\"submit\" name=\"refresh\" value=\"Refresh\"></form>"
                "</div>\r\n", 0) ;
        // "<form action=\"log/reset\" method=\"get\"><input type=\"submit\" value=\"Reset\"></form><br>\r\n"


    } else {
        return HTTP_SERVER_WSERVER_E_METHOD ;

    }

    return HTTP_SERVER_WSERVER_E_OK ;
}


