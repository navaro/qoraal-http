/*
 * wcss.c
 *
 *  Created on: 11 May 2015
 *      Author: natie
 */

#include "qoraal-http/httpserver.h"


#include <stdio.h>
#include <string.h>
#include "../wserver_inst.h"
#include "wcss.h"
#include "skeleton_css.c"




/**
 * @brief   Handler 
 *
 * @app
 */
int32_t
wcss_handler(HTTP_USER_T *user, uint32_t method, char* endpoint)
{
    HTTP_HEADER_T css_headers[] = {
            {"Cache-Control",  "public, max-age=604800"} ,
    };

    if (method == HTTP_HEADER_METHOD_GET) {

        if (strcmp(endpoint, "css/default.css") == 0) {
            httpserver_write_response (user, 200, HTTP_SERVER_CONTENT_TYPE_CSS,
                    css_headers, sizeof(css_headers)/sizeof(css_headers[0]),
                    sekleton_css_data, sizeof(sekleton_css_data)) ;
        }
        else {
            httpserver_write_response (user, WSERVER_RESP_CODE_400, HTTP_SERVER_CONTENT_TYPE_HTML,
                    0, 0, WSERVER_RESP_CONTENT_400, strlen(WSERVER_RESP_CONTENT_400)) ;

        }

    } else {
        httpserver_write_response (user, WSERVER_RESP_CODE_400, HTTP_SERVER_CONTENT_TYPE_HTML,
                0, 0, WSERVER_RESP_CONTENT_400, strlen(WSERVER_RESP_CONTENT_400)) ;
    }

    return HTTP_SERVER_WSERVER_E_OK ;
}

#endif /* CFG_SERVICE_WSERVER */

