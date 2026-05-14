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

#include "qoraal-http/config.h"
#if !defined(CFG_JSON_DISABLE) || !CFG_JSON_DISABLE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "qoraal/qoraal.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpwebapi.h"
#include "qoraal-http/json/jsmn_tools.h"
#include "qoraal-http/json/frozen.h"


static QORAAL_PROP_RESOURCE_T *_webapi_property_resources ;
static QORAAL_HEAP _webapi_heap = QORAAL_HeapAuxiliary ;
static const char * _webapi_root = "api" ;
static qoraal_enum_resolver_t _enum_resolver = NULL ;

static void
webapi_property_resources_clear(void)
{
    _webapi_property_resources = NULL;
}

int32_t webapi_init (const char * root, QORAAL_HEAP heap)
{
    webapi_property_resources_clear();

    _webapi_root = root ? root : "api" ;
    _webapi_heap = heap ? heap : QORAAL_HeapAuxiliary ;
    return EOK ;
}

void webapi_set_enum_resolver(qoraal_enum_resolver_t resolver)
{
    _enum_resolver = resolver ;
}

static size_t
webapi_resource_count(void)
{
    size_t count = 0;
    QORAAL_PROP_RESOURCE_T *inst;

    if (_webapi_property_resources) {
        for (inst = _webapi_property_resources; inst; inst = inst->next) {
            count++;
        }
        return count;
    }

    return 0;
}

static QORAAL_PROP_RESOURCE_T *
webapi_resource_at(size_t index)
{
    QORAAL_PROP_RESOURCE_T *inst = _webapi_property_resources;

    while (inst) {
        if (index == 0) {
            return inst;
        }
        index--;
        inst = inst->next;
    }

    return 0;
}

int32_t
webapi_property_resources_set(const char *root, QORAAL_PROP_RESOURCE_T *resources)
{
    if (root && root[0]) {
        _webapi_root = root;
    }

    _webapi_property_resources = resources;

    return EOK;
}

static QORAAL_PROP_RESOURCE_T *  webapi_resource_get (const char * ep)
{
    size_t i;
    QORAAL_PROP_RESOURCE_T* current;

    if (!ep) {
        return 0;
    }

    for (i = 0; i < webapi_resource_count(); i++) {
        current = webapi_resource_at(i);
        if (current && current->ep && (strcmp(ep, current->ep) == 0)) return current ;
    }

    return 0 ;
}

bool webapi_ep_available(const char * ep)
{
    return webapi_resource_get (ep) ? true : false ;
}

static bool webapi_prop_is_action(QORAAL_PROP_T *prop)
{
    return prop && prop->type == QORAAL_PROP_ACTION;
}

static const char * webapi_type_to_string(QORAAL_PROP_TYPE_T type)
{
    return (type == QORAAL_PROP_STRING) ? "string" :
           (type == QORAAL_PROP_INTEGER) ? "integer" :
           (type == QORAAL_PROP_BOOLEAN) ? "boolean" :
           (type == QORAAL_PROP_ENUM) ? "string" : "action";
}

/* For OpenAPI schema generation, actions are exposed as write-only booleans */
static const char * webapi_openapi_type_to_string(QORAAL_PROP_TYPE_T type)
{
    return (type == QORAAL_PROP_STRING) ? "string" :
           (type == QORAAL_PROP_INTEGER) ? "integer" :
           (type == QORAAL_PROP_BOOLEAN) ? "boolean" :
           (type == QORAAL_PROP_ENUM) ? "string" : "boolean";
}

/* Returns false for enum props whose type cannot be resolved; such props are omitted from the schema. */
static bool webapi_prop_enum_resolvable(QORAAL_PROP_T *prop)
{
    if (prop->type != QORAAL_PROP_ENUM) {
        return true;
    }
    return _enum_resolver && prop->enum_name && _enum_resolver(prop->enum_name);
}

static size_t webapi_emit_enum_schema(struct json_out *out, const char *enum_name)
{
    size_t total_length = 0;
    const QORAAL_ENUM_TYPE_T *enum_info;
    int i;

    if (!enum_name || !_enum_resolver) {
        return 0;
    }

    enum_info = _enum_resolver(enum_name);
    if (!enum_info || !enum_info->values || enum_info->count == 0) {
        return 0;
    }

    total_length += json_printf(out, ",enum:[");
    for (i = 0; i < (int)enum_info->count; i++) {
        if (i > 0) total_length += json_printf(out, ",");
        total_length += json_printf(out, "%Q", enum_info->values[i].name);
    }
    total_length += json_printf(out, "]");
    total_length += json_printf(out, ",%Q:{", "x-enum-int-map");
    for (i = 0; i < (int)enum_info->count; i++) {
        if (i > 0) total_length += json_printf(out, ",");
        total_length += json_printf(out, "%Q:%d", enum_info->values[i].name, enum_info->values[i].value);
    }
    total_length += json_printf(out, "}");

    return total_length;
}

static bool webapi_resource_has_writable_props(QORAAL_PROP_RESOURCE_T *inst)
{
    size_t i;

    if (!inst || !inst->props) {
        return false;
    }

    for (i = 0; i < inst->props_count; i++) {
        if (inst->props[i].set_callback != NULL &&
            webapi_prop_enum_resolvable(&inst->props[i])) {
            return true ;
        }
    }

    return false ;
}

static const char *
webapi_resource_tag(QORAAL_PROP_RESOURCE_T *inst)
{
    if (inst && inst->tag && inst->tag[0]) {
        return inst->tag;
    }
    if (inst && inst->title && inst->title[0]) {
        return inst->title;
    }
    return "General";
}

static const char *
webapi_resource_get_summary(QORAAL_PROP_RESOURCE_T *inst)
{
    if (inst && inst->get_summary && inst->get_summary[0]) {
        return inst->get_summary;
    }
    if (inst && inst->title && inst->title[0]) {
        return inst->title;
    }
    return "Read configuration";
}

static const char *
webapi_resource_set_summary(QORAAL_PROP_RESOURCE_T *inst)
{
    if (inst && inst->set_summary && inst->set_summary[0]) {
        return inst->set_summary;
    }
    if (inst && inst->title && inst->title[0]) {
        return inst->title;
    }
    return "Update configuration";
}



static int json_printer_null(struct json_out *out, const char *buf, size_t len) {
    out->u.buf.len += len ;
    return len;
}

#define JSON_OUT_NULL() \
  {                            \
    json_printer_null, {        \
      { 0, 0, 0 }          \
    }                          \
  }

static size_t generate_openapi_json(struct json_out *out) {
    size_t total_length = 0;
    bool first_inst = true;

    total_length += json_printf(out,
        "{"
        "openapi:%Q,"
        "info:{"
          "title:%Q,"
          "version:%Q"
        "},"
        "paths:{",
        "3.0.0",
        "Device Control API",
        "1.0");

    size_t inst_idx;

    for (inst_idx = 0; inst_idx < webapi_resource_count(); inst_idx++) {
        QORAAL_PROP_RESOURCE_T* inst = webapi_resource_at(inst_idx);
        char path[128];
        size_t prop_idx;

        if (!first_inst) {
            total_length += json_printf(out, ",");
        }
        first_inst = false;

        snprintf(path, sizeof(path), "/%s/%s", _webapi_root, inst->ep);

        total_length += json_printf(out,
            "%Q:{"
              "get:{"
                "tags:[%Q],"
                "summary:%Q,"
                "responses:{"
                  "%Q:{"
                    "description:%Q,"
                    "content:{"
                      "%Q:{"
                        "schema:{"
                          "type:%Q,"
                          "properties:{",
            path,
            webapi_resource_tag(inst),
            webapi_resource_get_summary(inst),
            "200",
            "Successful response",
            "application/json",
            "object");

        bool first_prop = true;

        for (prop_idx = 0; prop_idx < inst->props_count; prop_idx++) {
            QORAAL_PROP_T* current = &inst->props[prop_idx];

            if (!webapi_prop_is_action(current) && webapi_prop_enum_resolvable(current)) {
                if (!first_prop) {
                    total_length += json_printf(out, ",");
                }
                first_prop = false;

                total_length += json_printf(out,
                    "%Q:{"
                      "type:%Q,"
                      "description:%Q",
                    current->name,
                    webapi_openapi_type_to_string(current->type),
                    current->description ? current->description : "");

                if (current->type == QORAAL_PROP_ENUM && current->enum_name) {
                    total_length += webapi_emit_enum_schema(out, current->enum_name);
                }

                if (current->set_callback == NULL) {
                    total_length += json_printf(out, ",readOnly:%B", 1);
                }

                total_length += json_printf(out, "}");
            }
        }

        total_length += json_printf(out,
                          "}"
                        "}"
                      "}"
                    "}"
                  "}"
                "}"
              "}");

        if (webapi_resource_has_writable_props(inst)) {
            total_length += json_printf(out,
              ",post:{"
                "tags:[%Q],"
                "summary:%Q,"
                "requestBody:{"
                  "required:%B,"
                  "content:{"
                    "%Q:{"
                      "schema:{"
                        "type:%Q,"
                        "properties:{",
              webapi_resource_tag(inst),
              webapi_resource_set_summary(inst),
              1,
              "application/json",
              "object");

            first_prop = true;

            for (prop_idx = 0; prop_idx < inst->props_count; prop_idx++) {
                QORAAL_PROP_T* current = &inst->props[prop_idx];

                if (current->set_callback != NULL && webapi_prop_enum_resolvable(current)) {
                    if (!first_prop) {
                        total_length += json_printf(out, ",");
                    }
                    first_prop = false;

                    total_length += json_printf(out,
                        "%Q:{"
                          "type:%Q,"
                          "description:%Q",
                        current->name,
                        webapi_openapi_type_to_string(current->type),
                        current->description ? current->description : "");

                    if (current->type == QORAAL_PROP_ENUM && current->enum_name) {
                        total_length += webapi_emit_enum_schema(out, current->enum_name);
                    }

                    if (webapi_prop_is_action(current)) {
                        total_length += json_printf(out, ",writeOnly:%B", 1);
                    }

                    total_length += json_printf(out, "}");
                }
            }

            total_length += json_printf(out,
                        "}"
                      "}"
                    "}"
                  "}"
                "},"
                "responses:{"
                  "%Q:{"
                    "description:%Q"
                  "}"
                "}"
              "}",
              "200",
              "Updated response");
        }

        total_length += json_printf(out, "}");
    }

    total_length += json_printf(out, "}}");

    return total_length;
}

char * webapi_openapi_json(void)
{
    struct json_out out_size = JSON_OUT_NULL();
    size_t json_length = generate_openapi_json(&out_size);

    char *json_buffer = (char *)qoraal_malloc(_webapi_heap, json_length + 1);
    if (json_buffer == NULL) {
        return NULL;
    }

    struct json_out out_buffer = JSON_OUT_BUF(json_buffer, json_length + 1);
    generate_openapi_json(&out_buffer);

    return json_buffer;
}

void webapi_openapi_json_free(char * buffer)
{
    qoraal_free(_webapi_heap, buffer);
}

static size_t generate_wot_json(struct json_out *out, const char * uri) {
    size_t total_length = 0;
    bool first_property = true;
    bool first_action = true;
    size_t inst_idx;

    total_length += json_printf(out, "%s", "{");
    total_length += json_printf(out, "%s", "\"@context\":");
    total_length += json_printf(out, "%Q", "https://www.w3.org/2022/wot/td/v1.1");
    total_length += json_printf(out, "%s", ",\"title\":");
    total_length += json_printf(out, "%Q", "Device");
    total_length += json_printf(out, "%s", ",\"base\":");
    total_length += json_printf(out, "%Q", uri);
    total_length += json_printf(out, "%s", ",\"securityDefinitions\":{\"nosec_sc\":{\"scheme\":\"nosec\"}}");
    total_length += json_printf(out, "%s", ",\"security\":[\"nosec_sc\"]");
    total_length += json_printf(out, "%s", ",\"properties\":{");

    for (inst_idx = 0; inst_idx < webapi_resource_count(); inst_idx++) {
        QORAAL_PROP_RESOURCE_T* inst = webapi_resource_at(inst_idx);
        size_t prop_idx;

        for (prop_idx = 0; prop_idx < inst->props_count; prop_idx++) {
            QORAAL_PROP_T* current = &inst->props[prop_idx];
            char href[192];

            if (!webapi_prop_is_action(current) && webapi_prop_enum_resolvable(current)) {
                snprintf(href, sizeof(href), "%s/%s/%s", _webapi_root, inst->ep, current->name);

                if (!first_property) {
                    total_length += json_printf(out, ",");
                }
                first_property = false;

                total_length += json_printf(out,
                    "%Q:{"
                      "type:%Q,"
                      "description:%Q",
                    current->name,
                    webapi_type_to_string(current->type),
                    current->description ? current->description : "");

                total_length += json_printf(out, ",readOnly:%B",
                    current->set_callback == NULL);

                if (current->type == QORAAL_PROP_ENUM && current->enum_name) {
                    total_length += webapi_emit_enum_schema(out, current->enum_name);
                }

                total_length += json_printf(out,
                      ",forms:[{"
                        "href:%Q,"
                        "contentType:%Q,"
                        "op:[%Q",
                        href,
                        "application/json",
                        "readproperty");

                if (current->set_callback != NULL) {
                    total_length += json_printf(out, ",%Q", "writeproperty");
                }

                total_length += json_printf(out,
                        "]"
                      "}]"
                    "}");
            }
        }
    }

    total_length += json_printf(out, "%s", "},\"actions\":{");

    for (inst_idx = 0; inst_idx < webapi_resource_count(); inst_idx++) {
        QORAAL_PROP_RESOURCE_T* inst = webapi_resource_at(inst_idx);
        size_t prop_idx;

        for (prop_idx = 0; prop_idx < inst->props_count; prop_idx++) {
            QORAAL_PROP_T* current = &inst->props[prop_idx];
            char href[192];

            if (webapi_prop_is_action(current)) {
                snprintf(href, sizeof(href), "%s/%s/%s", _webapi_root, inst->ep, current->name);

                if (!first_action) {
                    total_length += json_printf(out, ",");
                }
                first_action = false;

                total_length += json_printf(out,
                    "%Q:{"
                      "description:%Q,"
                      "forms:[{"
                        "href:%Q,"
                        "contentType:%Q,"
                        "op:[%Q]"
                      "}]"
                    "}",
                    current->name,
                    current->description ? current->description : "",
                    href,
                    "application/json",
                    "invokeaction");
            }
        }
    }

    total_length += json_printf(out, "}}");

    return total_length;
}

char * webapi_wot_json(const char * uri)
{
    struct json_out out_size = JSON_OUT_NULL();
    size_t json_length = generate_wot_json(&out_size, uri);

    char *json_buffer = (char *)qoraal_malloc(_webapi_heap, json_length + 1);
    if (json_buffer == NULL) {
        return NULL;
    }

    struct json_out out_buffer = JSON_OUT_BUF(json_buffer, json_length + 1);
    generate_wot_json(&out_buffer, uri);

    return json_buffer;
}

void webapi_wot_json_free(char * buffer)
{
    qoraal_free(_webapi_heap, buffer);
}

static QORAAL_PROP_T *
webapi_prop_get(QORAAL_PROP_RESOURCE_T *inst, const char *name)
{
    QORAAL_PROP_T *prop;
    size_t i;

    if (!inst || !name || !name[0]) {
        return 0;
    }

    if (!inst->props) {
        return 0;
    }

    for (i = 0; i < inst->props_count; i++) {
        prop = &inst->props[i];
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    return 0;
}

static size_t
webapi_print_prop_value(QORAAL_PROP_T *prop, struct json_out *out, bool is_json)
{
    int32_t res = EFAIL;
    size_t total_length = 0;
    uint32_t get_buffer[QORAAL_HTTP_API_BUFFER_MAX / sizeof(uint32_t)];
    int int_value;
    bool bool_value;

    if (!prop) {
        if (is_json) {
            total_length += json_printf(out, "%Q", 0);
        } else {
            total_length += json_printf(out, "%s", "");
        }
        return total_length;
    }

    if (prop->get_callback != NULL) {
        res = prop->get_callback((void *)get_buffer, prop);
    }

    if (res < EOK) {
        if (is_json) {
            total_length += json_printf(out, "%Q", 0);
        } else {
            total_length += json_printf(out, "%s", "");
        }
        return total_length;
    }

    switch (prop->type) {
        case QORAAL_PROP_STRING:
        case QORAAL_PROP_ENUM:
            if (is_json) {
                total_length += json_printf(out, "%Q", (const char *)get_buffer);
            } else {
                total_length += json_printf(out, "%s", (const char *)get_buffer);
            }
            break;

        case QORAAL_PROP_INTEGER:
            memcpy(&int_value, get_buffer, sizeof(int));
            total_length += json_printf(out, "%d", int_value);
            break;

        case QORAAL_PROP_BOOLEAN:
            memcpy(&bool_value, get_buffer, sizeof(bool));
            if (is_json) {
                total_length += json_printf(out, "%B", bool_value);
            } else {
                total_length += json_printf(out, "%s", bool_value ? "true" : "false");
            }
            break;

        default:
            if (is_json) {
                total_length += json_printf(out, "%Q", 0);
            } else {
                total_length += json_printf(out, "%s", "");
            }
            break;
    }

    return total_length;
}

static size_t
generate_simple_json(QORAAL_PROP_RESOURCE_T *inst, struct json_out *out)
{
    size_t total_length = 0;
    bool first = true;
    QORAAL_PROP_T *current;
    size_t i;

    total_length += json_printf(out, "{");

    if (!inst || !inst->props) {
        total_length += json_printf(out, "}");
        return total_length;
    }

    for (i = 0; i < inst->props_count; i++) {
        current = &inst->props[i];

        if (webapi_prop_is_action(current)) {
            continue;
        }

        if (!first) {
            total_length += json_printf(out, ",");
        }
        first = false;

        total_length += json_printf(out, "%Q:", current->name);
        total_length += webapi_print_prop_value(current, out, true);
    }

    total_length += json_printf(out, "}");
    return total_length;
}

static size_t
generate_simple_property_json(QORAAL_PROP_T *prop, struct json_out *out)
{
    return webapi_print_prop_value(prop, out, true);
}

static size_t
generate_simple_property_text(QORAAL_PROP_T *prop, struct json_out *out)
{
    return webapi_print_prop_value(prop, out, false);
}

char *
webapi_generate_simple_response(const char *ep, const char *property, bool is_json)
{
    QORAAL_PROP_RESOURCE_T *inst;
    QORAAL_PROP_T *prop = 0;
    struct json_out out_size = JSON_OUT_NULL();
    size_t response_length;
    char *buffer;

    if (!ep || !ep[0]) {
        return NULL;
    }

    inst = webapi_resource_get(ep);
    if (!inst) {
        return NULL;
    }

    if (property && property[0]) {
        prop = webapi_prop_get(inst, property);
        if (!prop || webapi_prop_is_action(prop)) {
            buffer = (char *)qoraal_malloc(_webapi_heap, 1);
            if (!buffer) {
                return NULL;
            }
            buffer[0] = '\0';
            return buffer;
        }

        if (is_json) {
            response_length = generate_simple_property_json(prop, &out_size);
        } else {
            response_length = generate_simple_property_text(prop, &out_size);
        }
    } else {
        response_length = generate_simple_json(inst, &out_size);
    }

    buffer = (char *)qoraal_malloc(_webapi_heap, response_length + 1);
    if (!buffer) {
        return NULL;
    }

    struct json_out out_buffer = JSON_OUT_BUF(buffer, response_length + 1);

    if (property && property[0]) {
        if (is_json) {
            generate_simple_property_json(prop, &out_buffer);
        } else {
            generate_simple_property_text(prop, &out_buffer);
        }
    } else {
        generate_simple_json(inst, &out_buffer);
    }

    return buffer;
}

void
webapi_simple_response_free(char *buffer)
{
    qoraal_free(_webapi_heap, buffer);
}

static int32_t
webapi_prop_set_from_cstr(QORAAL_PROP_T *prop, const char *value)
{
    long temp_long;
    char *endptr;
    int temp_int;
    bool temp_bool;

    if (!prop || !value) {
        return E_PARM;
    }

    if (prop->set_callback == NULL) {
        return E_UNEXP;
    }

    switch (prop->type) {
        case QORAAL_PROP_STRING:
        case QORAAL_PROP_ENUM:
            return prop->set_callback((void *) value, prop);

        case QORAAL_PROP_INTEGER:
            temp_long = strtol(value, &endptr, 10);
            if ((endptr == value) || (*endptr != '\0')) {
                return E_PARM;
            }
            temp_int = (int) temp_long;
            return prop->set_callback(&temp_int, prop);

        case QORAAL_PROP_BOOLEAN:
            if ((strcmp(value, "true") == 0) || (strcmp(value, "1") == 0)) {
                temp_bool = true;
            } else if ((strcmp(value, "false") == 0) || (strcmp(value, "0") == 0)) {
                temp_bool = false;
            } else {
                return E_PARM;
            }
            return prop->set_callback(&temp_bool, prop);

        case QORAAL_PROP_ACTION:
            return prop->set_callback((void *) value, prop);

        default:
            return E_PARM;
    }
}

typedef int32_t (*webapi_json_kv_cb_t)(const char *key, const char *value, void *arg);

static int32_t
webapi_json_object_foreach(const char *json, webapi_json_kv_cb_t cb, void *arg, int *count)
{
    int32_t len;
    int token_num;
    int32_t res = EOK;
    jsmn_parser parser;
    jsmntok_t *tokens = 0;
    jsmntok_t *key;
    jsmntok_t *val;
    int local_count = 0;

    if (!json || !cb) {
        return E_PARM;
    }

    if (count) {
        *count = 0;
    }

    len = strlen(json);
    if (len == 0) {
        return E_PARM;
    }

    jsmn_init(&parser);
    token_num = jsmn_parse(&parser, json, len, NULL, 0);
    if (token_num <= 0) {
        return E_PARM;
    }

    tokens = qoraal_malloc(_webapi_heap, token_num * sizeof(jsmntok_t));
    if (!tokens) {
        return E_NOMEM;
    }

    jsmn_init(&parser);
    token_num = jsmn_parse(&parser, json, len, tokens, token_num);
    if ((token_num <= 0) || (tokens[0].type != JSMN_OBJECT)) {
        qoraal_free(_webapi_heap, tokens);
        return E_PARM;
    }

    key = jsmn_get_next(tokens, 0);
    val = key ? (key + 1) : NULL;

    while (key && val) {
        char key_str[QORAAL_HTTP_API_BUFFER_MAX];
        char val_str[QORAAL_HTTP_API_BUFFER_MAX];

        if ((key->type != JSMN_STRING) ||
            ((val->type != JSMN_STRING) && (val->type != JSMN_PRIMITIVE))) {
            res = E_PARM;
            break;
        }

        if (jsmn_copy_str(json, key, key_str, sizeof(key_str)) >= (int)sizeof(key_str) ||
            jsmn_copy_str(json, val, val_str, sizeof(val_str)) >= (int)sizeof(val_str)) {
            res = E_PARM;
            break;
        }

        res = cb(key_str, val_str, arg);
        if (res < 0) {
            break;
        }

        local_count++;

        key = jsmn_get_next(tokens, key);
        val = key ? (key + 1) : NULL;
    }

    qoraal_free(_webapi_heap, tokens);

    if ((res >= 0) && count) {
        *count = local_count;
    }

    return res;
}

static int32_t
webapi_post_property_text(QORAAL_PROP_RESOURCE_T *inst, const char *property, const char *body)
{
    QORAAL_PROP_T *prop;

    if (!inst || !property || !property[0] || !body) {
        return E_PARM;
    }

    prop = webapi_prop_get(inst, property);
    if (!prop) {
        return E_NOTFOUND;
    }

    return webapi_prop_set_from_cstr(prop, body);
}

typedef struct  {
    QORAAL_PROP_RESOURCE_T *inst;
    bool actions_only;
} webapi_post_bulk_json_ctx_t;

static int32_t
webapi_post_bulk_json_cb(const char *key, const char *value, void *arg)
{
    webapi_post_bulk_json_ctx_t *ctx = (webapi_post_bulk_json_ctx_t *) arg;
    QORAAL_PROP_T *prop;

    if (!ctx || !ctx->inst) {
        return E_PARM;
    }

    prop = webapi_prop_get(ctx->inst, key);
    if (!prop) {
        return EOK;  /* skip unknown keys for forward compatibility */
    }

    if (webapi_prop_is_action(prop) != ctx->actions_only) {
        return EOK;
    }

    return webapi_prop_set_from_cstr(prop, value);
}

static int32_t
webapi_post_bulk_json(QORAAL_PROP_RESOURCE_T *inst, const char *json)
{
    if (!inst || !json) {
        return E_PARM;
    }

    webapi_post_bulk_json_ctx_t ctx = { inst, false };
    int count = 0;

    /* Two-pass processing ensures correct ordering: data properties are always
     * committed to the registry before any ACTION callback fires, regardless
     * of the key order in the incoming JSON body.
     *
     * NOTE: ACTION callbacks here are non-blocking , so the response is sent
     * normally after this function returns.  If an action would disrupt the
     * connection before the response is sent (e.g. reboot, firmware-swap,
     * WiFi reconfiguration),  it must NOT be executed here.  Instead,
     * schedule it for after the response.
     *
     * Pass 1: apply all non-ACTION properties first so registry values are
     * committed before any action callback (e.g. start) reads them. */
    int res = webapi_json_object_foreach(json, webapi_post_bulk_json_cb, &ctx, &count);
    if (res < 0) {
        return res;
    }

    /* Pass 2: now trigger any ACTION properties. */
    ctx.actions_only = true;
    int action_count = 0;
    res = webapi_json_object_foreach(json, webapi_post_bulk_json_cb, &ctx, &action_count);
    if (res < 0) {
        return res;
    }

    if ((count + action_count) == 0) {
        return E_PARM;
    }

    return EOK;
}

static int32_t
webapi_json_value_to_cstr(QORAAL_PROP_T *prop, const char *body, char *out, size_t out_len)
{
    size_t len;

    if (!prop || !body || !out || out_len == 0) {
        return E_PARM;
    }

    len = strlen(body);

    if (((prop->type == QORAAL_PROP_STRING) || (prop->type == QORAAL_PROP_ENUM)) &&
        (len >= 2) &&
        (body[0] == '"') &&
        (body[len - 1] == '"')) {

        size_t inner_len = len - 2;
        if (inner_len >= out_len) {
            return E_PARM;
        }

        memcpy(out, body + 1, inner_len);
        out[inner_len] = '\0';
        return EOK;
    }

    if (len >= out_len) {
        return E_PARM;
    }

    memcpy(out, body, len + 1);
    return EOK;
}

static int32_t
webapi_post_property_json(QORAAL_PROP_RESOURCE_T *inst, const char *property, const char *body)
{
    QORAAL_PROP_T *prop;
    char value_str[QORAAL_HTTP_API_BUFFER_MAX];
    int32_t res;

    if (!inst || !property || !property[0] || !body) {
        return E_PARM;
    }

    prop = webapi_prop_get(inst, property);
    if (!prop) {
        return E_NOTFOUND;
    }

    res = webapi_json_value_to_cstr(prop, body, value_str, sizeof(value_str));
    if (res < 0) {
        return res;
    }

    return webapi_prop_set_from_cstr(prop, value_str);
}

static int32_t
webapi_invoke_action(QORAAL_PROP_RESOURCE_T *inst, const char *property, const char *body, bool is_json)
{
    QORAAL_PROP_T *prop;
    char value_str[QORAAL_HTTP_API_BUFFER_MAX];
    const char *value = "true";
    int32_t res;

    if (!inst || !property || !property[0]) {
        return E_PARM;
    }

    prop = webapi_prop_get(inst, property);
    if (!prop) {
        return E_NOTFOUND;
    }

    if (!webapi_prop_is_action(prop)) {
        return E_PARM;
    }

    if (body && body[0]) {
        if (is_json) {
            res = webapi_json_value_to_cstr(prop, body, value_str, sizeof(value_str));
            if (res < 0) {
                return res;
            }
            value = value_str;
        } else {
            value = body;
        }
    }

    return webapi_prop_set_from_cstr(prop, value);
}

/*
 * is_json == true:
 *   property == NULL  -> bulk update with JSON object
 *   property != NULL  -> single-property update with raw JSON value
 *
 * is_json == false:
 *   property must be != NULL and body is treated as plain text
 *
 * Actions:
 *   property refers to an action -> invoke action
 *   empty body is allowed and treated as "true"
 */
int32_t
webapi_post(const char *ep, const char *property, const char *body, bool is_json)
{
    QORAAL_PROP_RESOURCE_T *inst;
    QORAAL_PROP_T *prop = 0;

    if (!ep || !ep[0]) {
        return E_PARM;
    }

    inst = webapi_resource_get(ep);
    if (!inst) {
        return E_NOTFOUND;
    }

    if (property && property[0]) {
        prop = webapi_prop_get(inst, property);
        if (!prop) {
            return E_NOTFOUND;
        }

        if (webapi_prop_is_action(prop)) {
            return webapi_invoke_action(inst, property, body, is_json);
        }

        if (!body) {
            return E_PARM;
        }

        if (is_json) {
            return webapi_post_property_json(inst, property, body);
        } else {
            return webapi_post_property_text(inst, property, body);
        }
    }

    if (!body) {
        return E_PARM;
    }

    if (!is_json) {
        return E_PARM;
    }

    return webapi_post_bulk_json(inst, body);
}

#endif /* CFG_JSON_DISABLE */
