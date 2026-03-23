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

#ifndef __HTTPWWEBSERVICE_H__
#define __HTTPWWEBSERVICE_H__

#include <stdint.h>
#include <stdbool.h>
#include "qoraal/qoraal.h"
#include "qoraal/common/lists.h"

#define DBG_MESSAGE_HTTPWEBSERVICE(severity, fmt_str, ...) \
    DBG_MESSAGE_T_LOG(SVC_LOGGER_TYPE(severity,0), 0, fmt_str, ##__VA_ARGS__)
#define DBG_ASSERT_HTTPWEBSERVICE                    DBG_ASSERT


/*===========================================================================*/
/* Client constants.                                                         */
/*===========================================================================*/

#define WEBAPI_GET_BUFFER_MAX               128

/*===========================================================================*/
/* Client pre-compile time settings.                                         */
/*===========================================================================*/


/*===========================================================================*/
/* Client data structures and types.                                         */
/*===========================================================================*/

typedef struct WEBAPI_INST_S {
    struct WEBAPI_INST_S * next;
    const char * title;          /* Human title: "Device PKI Management API" */
    const char * version;        /* API version */
    const char * ep;             /* Endpoint base: "pki" */

    const char * tag;            /* OpenAPI tag/group: "PKI" */
    const char * description;    /* Section description */
    const char * get_summary;    /* GET operation summary */
    const char * post_summary;   /* POST operation summary */

    linked_t props_list;
} WEBAPI_INST_T;

#define WEBAPI_INST_DECL(name, title_, version_, ep_) \
    WEBAPI_INST_T name = {                            \
        0,                                            \
        title_,                                       \
        version_,                                     \
        ep_,                                          \
        title_,                                       \
        0,                                            \
        title_,                                       \
        title_,                                       \
        {0, 0}                                        \
    }


#define WEBAPI_INST_DECL_EX(name, title_, version_, ep_, tag_, desc_, get_sum_, post_sum_) \
    WEBAPI_INST_T name = {                                                                  \
        0,                                                                                  \
        title_,                                                                             \
        version_,                                                                           \
        ep_,                                                                                \
        tag_,                                                                               \
        desc_,                                                                              \
        get_sum_,                                                                           \
        post_sum_,                                                                          \
        {0, 0}                                                                              \
    }


typedef enum {
    PROPERTY_TYPE_STRING,
    PROPERTY_TYPE_INTEGER,
    PROPERTY_TYPE_BOOLEAN,
    PROPERTY_TYPE_ACTION,
} WEBAPI_PROP_TYPE;

typedef struct WEBAPI_PROP_S {
    const char * name;  // Property name (e.g., "state")
    WEBAPI_PROP_TYPE type;  // Data type of the property (string, integer, boolean)
    const char * description;  // Description for Swagger documentation
    int32_t (*add_callback)(void*, struct WEBAPI_PROP_S*);  // Callback function when added/initialized to an instance (optional)
    int32_t (*get_callback)(void*, struct WEBAPI_PROP_S*);  // Callback function for GET requests
    int32_t (*set_callback)(void*, struct WEBAPI_PROP_S*);  // Callback function for POST/PUT requests
    uintptr_t arg;
    struct WEBAPI_PROP_S* next;  // Pointer to next property (for linked list structure)
} WEBAPI_PROP_T;

#define WEBAPI_PROP_DECL(name, prop_, type_, description_, add_, get_, set_, arg_) \
    WEBAPI_PROP_T  name = {                                 \
        prop_, \
        type_, \
        description_, \
        add_, \
        get_, \
        set_, \
        arg_, \
        0                                                   \
    }




/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

    int32_t webapi_init (const char * root, QORAAL_HEAP heap) ;
    int32_t webapi_inst_add(WEBAPI_INST_T * inst) ;
    int32_t webapi_add_property(WEBAPI_INST_T *inst, WEBAPI_PROP_T *prop) ;
    bool webapi_ep_available(const char * ep) ;

/* Existing YAML support */
    char* webapi_swagger_yaml(void)  ;
    void webapi_swagger_yaml_free(char * buffer) ;

/* New: OpenAPI JSON support for browser tooling */
char * webapi_openapi_json(void);
void webapi_openapi_json_free(char * buffer);

/* Existing simple endpoint JSON */
    char * webapi_generate_simple_response(const char *ep, const char *property, bool is_json) ;
    void webapi_simple_response_free(char *buffer) ;

/* New WoT */
    char * webapi_wot_json(const char * uri) ;
    void webapi_wot_json_free(char * buffer) ;

    int32_t webapi_post(const char *ep, const char *property, const char *body, bool is_json) ;

#ifdef __cplusplus
}
#endif

#endif /* __HTTPWWEBSERVICE_H__ */