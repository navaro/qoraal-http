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

#define QORAAL_HTTP_API_BUFFER_MAX        128

/*===========================================================================*/
/* Client pre-compile time settings.                                         */
/*===========================================================================*/


/*===========================================================================*/
/* Client data structures and types.                                         */
/*===========================================================================*/

typedef enum {
    QORAAL_PROP_STRING,
    QORAAL_PROP_INTEGER,
    QORAAL_PROP_BOOLEAN,
    QORAAL_PROP_ACTION,
    QORAAL_PROP_ENUM,
} QORAAL_PROP_TYPE_T;

typedef QORAAL_ENUM_TYPE_T QORAAL_PROP_ENUM_INFO_T;

typedef struct QORAAL_PROP_S QORAAL_PROP_T;

typedef int32_t (*qoraal_prop_callback_t)(void *val, QORAAL_PROP_T *prop);

struct QORAAL_PROP_S {
    const char * name;  // Property name (e.g., "state")
    QORAAL_PROP_TYPE_T type;  // Data type of the property (string, integer, boolean, enum)
    const char * description;  // Description for Swagger documentation
    const QORAAL_PROP_ENUM_INFO_T *enum_info;  // Enum metadata (NULL if not QORAAL_PROP_ENUM)
    qoraal_prop_callback_t add_callback;  // Callback function when added/initialized to an instance (optional)
    qoraal_prop_callback_t get_callback;  // Callback function for GET requests (returns string for enum)
    qoraal_prop_callback_t set_callback;  // Callback function for POST/PUT requests (receives string for enum)
    uintptr_t arg;
};

typedef struct QORAAL_INST_S {
    const char * title;          /* Human title: "Device PKI Management API" */
    const char * version;        /* API version */
    const char * ep;             /* Endpoint base: "pki" */

    const char * tag;            /* OpenAPI tag/group: "PKI" */
    const char * description;    /* Section description */
    const char * get_summary;    /* GET operation summary */
    const char * post_summary;   /* POST operation summary */

    QORAAL_PROP_T *props;
    size_t props_count;
} QORAAL_INST_T;

typedef struct QORAAL_MODEL_S {
    const char *root;
    QORAAL_INST_T *instances;
    size_t instances_count;
} QORAAL_MODEL_T;

#define QORAAL_ARRAY_SIZE(array_) \
    (sizeof(array_) / sizeof((array_)[0]))

#define QORAAL_PROP_INIT(prop_, type_, description_, add_, get_, set_, arg_) \
    {                                                   \
        prop_, \
        type_, \
        description_, \
        0, \
        add_, \
        get_, \
        set_, \
        arg_                                                \
    }

#define QORAAL_PROP_ENUM_INIT(prop_, description_, enum_info_, add_, get_, set_, arg_) \
    {                                                   \
        prop_, \
        QORAAL_PROP_ENUM, \
        description_, \
        enum_info_, \
        add_, \
        get_, \
        set_, \
        arg_                                                \
    }

#define QORAAL_PROP_DECL(name, prop_, type_, description_, add_, get_, set_, arg_) \
    QORAAL_PROP_T  name = QORAAL_PROP_INIT(prop_, type_, description_, add_, get_, set_, arg_)

#define QORAAL_PROP_ENUM_DECL(name, prop_, description_, enum_info_, add_, get_, set_, arg_) \
    QORAAL_PROP_T  name = QORAAL_PROP_ENUM_INIT(prop_, description_, enum_info_, add_, get_, set_, arg_)

#define QORAAL_INST_INIT(title_, version_, ep_, tag_, desc_, get_sum_, post_sum_, props_) \
    {                                                                  \
        title_,                                                        \
        version_,                                                      \
        ep_,                                                           \
        tag_,                                                          \
        desc_,                                                         \
        get_sum_,                                                      \
        post_sum_,                                                     \
        props_,                                                        \
        QORAAL_ARRAY_SIZE(props_)                                      \
    }

#define QORAAL_INST_DECL_EX(name, title_, version_, ep_, tag_, desc_, get_sum_, post_sum_, props_) \
    QORAAL_INST_T name = QORAAL_INST_INIT(title_, version_, ep_, tag_, desc_, get_sum_, post_sum_, props_)

#define QORAAL_MODEL_INIT(root_, instances_) \
    {                                        \
        root_,                               \
        instances_,                          \
        QORAAL_ARRAY_SIZE(instances_)        \
    }

#define QORAAL_MODEL_DECL(name, root_, instances_) \
    QORAAL_MODEL_T name = QORAAL_MODEL_INIT(root_, instances_)

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

    int32_t webapi_init (const char * root, QORAAL_HEAP heap) ;
    int32_t webapi_model_set(QORAAL_MODEL_T *model) ;
    bool webapi_ep_available(const char * ep) ;

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
