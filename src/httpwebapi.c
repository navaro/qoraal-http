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
#include "qoraal/common/lists.h"
#include "qoraal-http/qoraal.h"
#include "qoraal-http/httpwebapi.h"
#include "qoraal-http/json/jsmn_tools.h"
#include "qoraal-http/json/frozen.h"


static linked_t _webapi_inst_list ;
static QORAAL_HEAP _webapi_heap = QORAAL_HeapAuxiliary ;
static const char * _webapi_root = "api" ;

int32_t webapi_init (const char * root, QORAAL_HEAP heap)
{
    _webapi_root = root ;
    _webapi_heap = heap ;
    linked_init (&_webapi_inst_list) ;
    return EOK ;
}

int32_t webapi_inst_add (WEBAPI_INST_T * inst)
{
    linked_init (&inst->props_list) ;
    linked_add_tail (&_webapi_inst_list, inst, OFFSETOF(WEBAPI_INST_T, next)) ;
    return EOK ;
}

WEBAPI_INST_T *  webapi_inst_get (const char * ep)
{
    WEBAPI_INST_T* current = linked_head(&_webapi_inst_list);
    while (current != NULL) {
        if (strcmp(ep, current->ep) == 0) return current ;
        current = linked_next(current, OFFSETOF(WEBAPI_INST_T, next));
    }

    return 0 ;
}

bool webapi_ep_available(const char * ep)
{
    return webapi_inst_get (ep) ? true : false ;
}

int32_t webapi_add_property(WEBAPI_INST_T *inst, WEBAPI_PROP_T *prop)
{
    linked_add_tail(&inst->props_list, prop, OFFSETOF(WEBAPI_PROP_T, next));

    if (prop->add_callback) {
        return prop->add_callback(inst, prop);
    }

    return EOK;
}

static bool webapi_prop_is_action(WEBAPI_PROP_T *prop)
{
    return prop && prop->type == PROPERTY_TYPE_ACTION;
}

static const char * webapi_type_to_string(WEBAPI_PROP_TYPE type)
{
    return (type == PROPERTY_TYPE_STRING) ? "string" :
           (type == PROPERTY_TYPE_INTEGER) ? "integer" :
           (type == PROPERTY_TYPE_BOOLEAN) ? "boolean" :
           (type == PROPERTY_TYPE_ENUM) ? "string" : "action";
}

/* For OpenAPI / legacy side, actions are exposed as write-only booleans */
static const char * webapi_openapi_type_to_string(WEBAPI_PROP_TYPE type)
{
    return (type == PROPERTY_TYPE_STRING) ? "string" :
           (type == PROPERTY_TYPE_INTEGER) ? "integer" :
           (type == PROPERTY_TYPE_BOOLEAN) ? "boolean" :
           (type == PROPERTY_TYPE_ENUM) ? "string" : "boolean";
}

static size_t webapi_emit_enum_schema(struct json_out *out, const WEBAPI_ENUM_INFO_T *enum_info)
{
    size_t total_length = 0;
    int i;

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

static bool webapi_inst_has_writable_props(WEBAPI_INST_T *inst)
{
    WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);
    while (current != NULL) {
        if (current->set_callback != NULL) {
            return true ;
        }
        current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
    }

    return false ;
}

static const char *
webapi_inst_tag(WEBAPI_INST_T *inst)
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
webapi_inst_get_summary(WEBAPI_INST_T *inst)
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
webapi_inst_post_summary(WEBAPI_INST_T *inst)
{
    if (inst && inst->post_summary && inst->post_summary[0]) {
        return inst->post_summary;
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

    WEBAPI_INST_T* inst = (WEBAPI_INST_T*) linked_head(&_webapi_inst_list);
    while (inst != NULL) {
        char path[128];

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
            webapi_inst_tag(inst),
            webapi_inst_get_summary(inst),
            "200",
            "Successful response",
            "application/json",
            "object");

        WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);
        bool first_prop = true;

        while (current != NULL) {
            if (!webapi_prop_is_action(current)) {
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

                if (current->type == PROPERTY_TYPE_ENUM && current->enum_info) {
                    total_length += webapi_emit_enum_schema(out, current->enum_info);
                }

                if (current->set_callback == NULL) {
                    total_length += json_printf(out, ",readOnly:%B", 1);
                }

                total_length += json_printf(out, "}");
            }

            current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
        }

        total_length += json_printf(out,
                          "}"
                        "}"
                      "}"
                    "}"
                  "}"
                "}"
              "}");

        if (webapi_inst_has_writable_props(inst)) {
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
              webapi_inst_tag(inst),
              webapi_inst_post_summary(inst),
              1,
              "application/json",
              "object");

            current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);
            first_prop = true;

            while (current != NULL) {
                if (current->set_callback != NULL) {
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

                    if (current->type == PROPERTY_TYPE_ENUM && current->enum_info) {
                        total_length += webapi_emit_enum_schema(out, current->enum_info);
                    }

                    if (webapi_prop_is_action(current)) {
                        total_length += json_printf(out, ",writeOnly:%B", 1);
                    }

                    total_length += json_printf(out, "}");
                }

                current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
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

        inst = linked_next(inst, OFFSETOF(WEBAPI_INST_T, next));
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

    WEBAPI_INST_T* inst = (WEBAPI_INST_T*) linked_head(&_webapi_inst_list);
    while (inst != NULL) {
        WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);

        while (current != NULL) {
            char href[192];

            if (!webapi_prop_is_action(current)) {
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

                if (current->type == PROPERTY_TYPE_ENUM && current->enum_info) {
                    total_length += webapi_emit_enum_schema(out, current->enum_info);
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

            current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
        }

        inst = linked_next(inst, OFFSETOF(WEBAPI_INST_T, next));
    }

    total_length += json_printf(out, "%s", "},\"actions\":{");

    inst = (WEBAPI_INST_T*) linked_head(&_webapi_inst_list);
    while (inst != NULL) {
        WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);

        while (current != NULL) {
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

            current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
        }

        inst = linked_next(inst, OFFSETOF(WEBAPI_INST_T, next));
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

static WEBAPI_PROP_T *
webapi_prop_get(WEBAPI_INST_T *inst, const char *name)
{
    WEBAPI_PROP_T *prop;

    if (!inst || !name || !name[0]) {
        return 0;
    }

    prop = (WEBAPI_PROP_T *) linked_head(&inst->props_list);
    while (prop != NULL) {
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
        prop = linked_next(prop, OFFSETOF(WEBAPI_PROP_T, next));
    }

    return 0;
}

static size_t
webapi_print_prop_value(WEBAPI_PROP_T *prop, struct json_out *out, bool is_json)
{
    int32_t res = EFAIL;
    size_t total_length = 0;
    uint32_t get_buffer[WEBAPI_GET_BUFFER_MAX / sizeof(uint32_t)];
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
        case PROPERTY_TYPE_STRING:
        case PROPERTY_TYPE_ENUM:
            if (is_json) {
                total_length += json_printf(out, "%Q", (const char *)get_buffer);
            } else {
                total_length += json_printf(out, "%s", (const char *)get_buffer);
            }
            break;

        case PROPERTY_TYPE_INTEGER:
            memcpy(&int_value, get_buffer, sizeof(int));
            total_length += json_printf(out, "%d", int_value);
            break;

        case PROPERTY_TYPE_BOOLEAN:
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
generate_simple_json(WEBAPI_INST_T *inst, struct json_out *out)
{
    size_t total_length = 0;
    bool first = true;
    WEBAPI_PROP_T *current;

    total_length += json_printf(out, "{");

    if (!inst) {
        total_length += json_printf(out, "}");
        return total_length;
    }

    current = (WEBAPI_PROP_T *)linked_head(&inst->props_list);
    while (current != NULL) {
        if (webapi_prop_is_action(current)) {
            current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
            continue;
        }

        if (!first) {
            total_length += json_printf(out, ",");
        }
        first = false;

        total_length += json_printf(out, "%Q:", current->name);
        total_length += webapi_print_prop_value(current, out, true);

        current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
    }

    total_length += json_printf(out, "}");
    return total_length;
}

static size_t
generate_simple_property_json(WEBAPI_PROP_T *prop, struct json_out *out)
{
    return webapi_print_prop_value(prop, out, true);
}

static size_t
generate_simple_property_text(WEBAPI_PROP_T *prop, struct json_out *out)
{
    return webapi_print_prop_value(prop, out, false);
}

char *
webapi_generate_simple_response(const char *ep, const char *property, bool is_json)
{
    WEBAPI_INST_T *inst;
    WEBAPI_PROP_T *prop = 0;
    struct json_out out_size = JSON_OUT_NULL();
    size_t response_length;
    char *buffer;

    if (!ep || !ep[0]) {
        return NULL;
    }

    inst = webapi_inst_get(ep);
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
webapi_prop_set_from_cstr(WEBAPI_PROP_T *prop, const char *value)
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
        case PROPERTY_TYPE_STRING:
        case PROPERTY_TYPE_ENUM:
            return prop->set_callback((void *) value, prop);

        case PROPERTY_TYPE_INTEGER:
            temp_long = strtol(value, &endptr, 10);
            if ((endptr == value) || (*endptr != '\0')) {
                return E_PARM;
            }
            temp_int = (int) temp_long;
            return prop->set_callback(&temp_int, prop);

        case PROPERTY_TYPE_BOOLEAN:
            if ((strcmp(value, "true") == 0) || (strcmp(value, "1") == 0)) {
                temp_bool = true;
            } else if ((strcmp(value, "false") == 0) || (strcmp(value, "0") == 0)) {
                temp_bool = false;
            } else {
                return E_PARM;
            }
            return prop->set_callback(&temp_bool, prop);

        case PROPERTY_TYPE_ACTION:
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
        char key_str[WEBAPI_GET_BUFFER_MAX];
        char val_str[WEBAPI_GET_BUFFER_MAX];

        if ((key->type != JSMN_STRING) ||
            ((val->type != JSMN_STRING) && (val->type != JSMN_PRIMITIVE))) {
            res = E_PARM;
            break;
        }

        jsmn_copy_str(json, key, key_str, sizeof(key_str));
        jsmn_copy_str(json, val, val_str, sizeof(val_str));

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
webapi_post_property_text(WEBAPI_INST_T *inst, const char *property, const char *body)
{
    WEBAPI_PROP_T *prop;

    if (!inst || !property || !property[0] || !body) {
        return E_PARM;
    }

    prop = webapi_prop_get(inst, property);
    if (!prop) {
        return E_NOTFOUND;
    }

    return webapi_prop_set_from_cstr(prop, body);
}

static int32_t
webapi_post_bulk_json_cb(const char *key, const char *value, void *arg)
{
    WEBAPI_INST_T *inst = (WEBAPI_INST_T *) arg;
    WEBAPI_PROP_T *prop;

    if (!inst) {
        return E_PARM;
    }

    prop = webapi_prop_get(inst, key);
    if (!prop) {
        return E_NOTFOUND;
    }

    return webapi_prop_set_from_cstr(prop, value);
}

static int32_t
webapi_post_bulk_json(WEBAPI_INST_T *inst, const char *json)
{
    if (!inst || !json) {
        return E_PARM;
    }
    int count = 0;

    int res = webapi_json_object_foreach(json, webapi_post_bulk_json_cb, inst, &count);
    if (res < 0) {
        return res;
    }

    if (count == 0) {
        return E_PARM;
    }

    return EOK;
}

static int32_t
webapi_json_value_to_cstr(WEBAPI_PROP_T *prop, const char *body, char *out, size_t out_len)
{
    size_t len;

    if (!prop || !body || !out || out_len == 0) {
        return E_PARM;
    }

    len = strlen(body);

    if (((prop->type == PROPERTY_TYPE_STRING) || (prop->type == PROPERTY_TYPE_ENUM)) &&
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
webapi_post_property_json(WEBAPI_INST_T *inst, const char *property, const char *body)
{
    WEBAPI_PROP_T *prop;
    char value_str[WEBAPI_GET_BUFFER_MAX];
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
webapi_invoke_action(WEBAPI_INST_T *inst, const char *property, const char *body, bool is_json)
{
    WEBAPI_PROP_T *prop;
    char value_str[WEBAPI_GET_BUFFER_MAX];
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
    WEBAPI_INST_T *inst;
    WEBAPI_PROP_T *prop = 0;

    if (!ep || !ep[0]) {
        return E_PARM;
    }

    inst = webapi_inst_get(ep);
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