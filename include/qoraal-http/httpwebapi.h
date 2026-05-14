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

#ifndef __QORAAL_HTTP_API_H__
#define __QORAAL_HTTP_API_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "qoraal/qoraal.h"
#include "qoraal/common/types.h"

#define DBG_MESSAGE_HTTPWEBSERVICE(severity, fmt_str, ...) \
    DBG_MESSAGE_T_LOG(SVC_LOGGER_TYPE(severity,0), 0, fmt_str, ##__VA_ARGS__)
#define DBG_ASSERT_HTTPWEBSERVICE                    DBG_ASSERT


/*===========================================================================*/
/* Client constants.                                                         */
/*===========================================================================*/

#define QORAAL_HTTP_API_BUFFER_MAX        QORAAL_PROP_VALUE_BUFFER_MAX

/*===========================================================================*/
/* Client pre-compile time settings.                                         */
/*===========================================================================*/


/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

    int32_t webapi_init (const char * root, QORAAL_HEAP heap) ;
    int32_t webapi_property_resources_set(const char *root, QORAAL_PROP_RESOURCE_T *resources) ;
    bool webapi_ep_available(const char * ep) ;

    /* Must be set before publishing enum properties so schema emitters can
     * look up enum values. */
    void webapi_set_enum_resolver(qoraal_enum_resolver_t resolver) ;

/* New: OpenAPI JSON support for browser tooling */
char * webapi_openapi_json(void);
void webapi_openapi_json_free(char * buffer);

/* Existing simple endpoint JSON */
    char * webapi_generate_simple_response(const char *ep, const char *property, bool is_json) ;
    void webapi_simple_response_free(char *buffer) ;

/* New WoT (https://playground.thingweb.io) */
    char * webapi_wot_json(const char * uri) ;
    void webapi_wot_json_free(char * buffer) ;

    int32_t webapi_post(const char *ep, const char *property, const char *body, bool is_json) ;

#ifdef __cplusplus
}
#endif

#endif /* __QORAAL_HTTP_API_H__ */
