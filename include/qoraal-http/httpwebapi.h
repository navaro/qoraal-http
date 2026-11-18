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

/**
 * Streams a generated JSON fragment to a caller-provided destination.
 *
 * Return a negative error to stop delivery. Any non-negative value indicates
 * that the fragment was accepted.
 */
typedef int32_t (*webapi_json_stream_callback_t)(const char *data,
                                                 size_t length,
                                                 void *context);

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
    /* Getters in these resources must have no side effects:
     * webapi_generate_simple_response() calls each getter once to size the
     * response, again to render it, and again on a retry if a value grew.
     * A property with a setter and no getter is write-only: a GET leaves it
     * out, and OpenAPI and the Thing Description mark it writeOnly. */
    int32_t webapi_property_resources_set(const char *root, QORAAL_PROP_RESOURCE_T *resources) ;
    bool webapi_ep_available(const char * ep) ;
    /* The module id that ep names in event filters: the module of a
     * QORAAL_PROP_RESOURCE_FLAG_EVENTS resource; -1 otherwise. */
    int32_t webapi_ep_module(const char *ep) ;

    /* Must be set before publishing enum properties so schema emitters can
     * look up enum values. */
    void webapi_set_enum_resolver(qoraal_enum_resolver_t resolver) ;

    /* Writes the API version into buf, NUL terminated, and returns its
     * length. The documents carry it as OpenAPI info.version and as the TD's
     * version.instance. Without a callback info.version is "1.0" and the TD
     * has no version. */
    typedef size_t (*webapi_version_callback_t)(char *buf, size_t size) ;
    void webapi_set_version_callback(webapi_version_callback_t callback) ;

/* New: OpenAPI JSON support for browser tooling */
char * webapi_openapi_json(void);
void webapi_openapi_json_free(char * buffer);
/** Streams the OpenAPI document without allocating a response buffer. */
int32_t webapi_openapi_json_stream(webapi_json_stream_callback_t callback,
                                   void *context);

/* Existing simple endpoint JSON */
    char * webapi_generate_simple_response(const char *ep, const char *property, bool is_json) ;
    void webapi_simple_response_free(char *buffer) ;
    /* A GET of <ep> or <ep>/<property>. A property with a response callback
     * answers through ctx itself, and *response is NULL. Otherwise *response
     * is the simple response: send it, then webapi_simple_response_free() it.
     * An E_* result means nothing was sent. */
    int32_t webapi_generate_response(const char *ep, const char *property, char *query,
                                     bool is_json, void *ctx, char **response) ;

/* New WoT (https://playground.thingweb.io)
 * uri becomes the TD's base. Pass NULL or "" to leave base out: every href is
 * an absolute path, so a consumer then resolves it against the URL it fetched
 * the TD from, keeping that URL's scheme, host and port. */
    char * webapi_wot_json(const char * uri) ;
    void webapi_wot_json_free(char * buffer) ;
    /** Streams the WoT Thing Description without allocating a response buffer. */
    int32_t webapi_wot_json_stream(const char *uri,
                                   webapi_json_stream_callback_t callback,
                                   void *context) ;

    int32_t webapi_post(const char *ep, const char *property, const char *body, bool is_json) ;

#ifdef __cplusplus
}
#endif

#endif /* __QORAAL_HTTP_API_H__ */
