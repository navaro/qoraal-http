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


// Define the type of property (e.g., string, integer)
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

// Function to create a new ServiceProperty and return a pointer to it
int32_t webapi_add_property(WEBAPI_INST_T *inst, WEBAPI_PROP_T *prop)
{
    linked_add_tail(&inst->props_list, prop, OFFSETOF(WEBAPI_PROP_T, next));

    if (prop->add_callback) {
        return prop->add_callback(inst, prop);
    }

    return EOK;
}

static const char * webapi_type_to_string(WEBAPI_PROP_TYPE type)
{
    return (type == PROPERTY_TYPE_STRING) ? "string" :
           (type == PROPERTY_TYPE_INTEGER) ? "integer" :
           (type == PROPERTY_TYPE_BOOLEAN) ? "boolean" : "action";
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

// Helper function to generate or calculate the length of the Swagger YAML
static size_t swagger_yaml(char* buffer, size_t len) {
    size_t total_length = 0;

    // Static YAML header
    total_length += snprintf(buffer ? buffer : NULL, len,
        "openapi: 3.0.0\n"
        "info:\n"
        "  title: API Documentation\n"
        "  version: '1.0'\n"
        "paths:\n");

    // Iterate over all instances
    WEBAPI_INST_T* inst = (WEBAPI_INST_T*) linked_head(&_webapi_inst_list);
    while (inst != NULL) {
        // For each instance, generate the path
        total_length += snprintf(buffer ? buffer + total_length : NULL, buffer ? len - total_length : 0,
            "  /%s/%s:\n"
            "    get:\n"
            "      summary: Get all properties of %s\n"
            "      responses:\n"
            "        '200':\n"
            "          description: Successful response\n"
            "          content:\n"
            "            application/json:\n"
            "              schema:\n"
            "                type: object\n"
            "                properties:\n",
            _webapi_root, inst->ep, inst->title ? inst->title : inst->ep);

        // Iterate through the properties of the instance
        WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);
        while (current != NULL) {
            // Property details
            total_length += snprintf(buffer ? buffer + total_length : NULL,
                                     buffer ? len - total_length : 0,
                "                  %s:\n"
                "                    type: %s\n"
                "                    description: %s\n",
                current->name,
                webapi_type_to_string(current->type),
                current->description ? current->description : ""
            );

            current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
        }

        if (webapi_inst_has_writable_props(inst)) {
            total_length += snprintf(buffer ? buffer + total_length : NULL,
                                     buffer ? len - total_length : 0,
                "    post:\n"
                "      summary: Update writable properties of %s\n"
                "      requestBody:\n"
                "        required: true\n"
                "        content:\n"
                "          application/json:\n"
                "            schema:\n"
                "              type: object\n"
                "              properties:\n",
                inst->title ? inst->title : inst->ep);

            current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);
            while (current != NULL) {
                if (current->set_callback != NULL) {
                    total_length += snprintf(buffer ? buffer + total_length : NULL,
                                     buffer ? len - total_length : 0,
                "                  %s:\n"
                "                    type: %s\n"
                "                    description: %s\n",
                current->name,
                        webapi_type_to_string(current->type),
                        current->description ? current->description : ""
            );
                }

                current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
            }

            total_length += snprintf(buffer ? buffer + total_length : NULL,
                                     buffer ? len - total_length : 0,
                "      responses:\n"
                "        '200':\n"
                "          description: Updated response\n");
        }

        // Move to the next instance
        inst = linked_next(inst, OFFSETOF(WEBAPI_INST_T, next));
    }

    return total_length;
}

// Function to generate Swagger YAML for all properties
char* webapi_swagger_yaml(void) {
    // First pass: Get the required buffer size by calling swagger_yaml() with NULL
    size_t yaml_length = swagger_yaml(NULL, 0);

    // Allocate the buffer
    char* yaml_buffer = (char*) qoraal_malloc(_webapi_heap, yaml_length + 1);  // +1 for null terminator
    if (yaml_buffer == NULL) {
        return NULL;  // Handle memory allocation failure
    }

    // Second pass: Generate the YAML by calling swagger_yaml() with the allocated buffer
    swagger_yaml(yaml_buffer, yaml_length+1);

    return yaml_buffer;  // Return the generated YAML string
}


void webapi_swagger_yaml_free(char * buffer)
{
    qoraal_free(_webapi_heap, buffer);
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
        "API Documentation",
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
            inst->title ? inst->title : inst->ep,
            "200",
            "Successful response",
            "application/json",
            "object");

        WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);
        bool first_prop = true;

        while (current != NULL) {
            if (!first_prop) {
                total_length += json_printf(out, ",");
            }
            first_prop = false;

            total_length += json_printf(out,
                "%Q:{"
                  "type:%Q,"
                  "description:%Q",
                current->name,
                webapi_type_to_string(current->type),
                current->description ? current->description : "");

            if (current->set_callback == NULL) {
                total_length += json_printf(out, ",readOnly:%B", 1);
            }

            total_length += json_printf(out, "}");

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
                "summary:%Q,"
                "requestBody:{"
                  "required:%B,"
                  "content:{"
                    "%Q:{"
                      "schema:{"
                        "type:%Q,"
                        "properties:{",
              inst->title ? inst->title : inst->ep,
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
                          "description:%Q"
                        "}",
                        current->name,
                        webapi_type_to_string(current->type),
                        current->description ? current->description : "");
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

static bool webapi_prop_is_action(WEBAPI_PROP_T *prop)
{
    return prop && prop->type == PROPERTY_TYPE_ACTION;
}

static size_t generate_wot_json(struct json_out *out, const char * ip) {
    size_t total_length = 0;
    bool first_property = true;
    bool first_action = true;
    char base[128];

    if (!ip || !ip[0]) {
        ip = "127.0.0.1";
    }

    snprintf(base, sizeof(base), "https://%s/", ip);

    total_length += json_printf(out, "%s", "{");
    total_length += json_printf(out, "%s", "\"@context\":");
    total_length += json_printf(out, "%Q", "https://www.w3.org/2022/wot/td/v1.1");
    total_length += json_printf(out, "%s", ",\"title\":");
    total_length += json_printf(out, "%Q", "Device");
    total_length += json_printf(out, "%s", ",\"base\":");
    total_length += json_printf(out, "%Q", base);
    total_length += json_printf(out, "%s", ",\"securityDefinitions\":{\"nosec_sc\":{\"scheme\":\"nosec\"}}");
    total_length += json_printf(out, "%s", ",\"security\":\"nosec_sc\"");
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

    total_length += json_printf(out, "},actions:{");

    inst = (WEBAPI_INST_T*) linked_head(&_webapi_inst_list);
    while (inst != NULL) {
        WEBAPI_PROP_T* current = (WEBAPI_PROP_T*) linked_head(&inst->props_list);

        while (current != NULL) {
            char href[192];

            if (webapi_prop_is_action(current)) {
                snprintf(href, sizeof(href), "%s/%s", _webapi_root, inst->ep);

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

char * webapi_wot_json(const char * ip)
{
    struct json_out out_size = JSON_OUT_NULL();
    size_t json_length = generate_wot_json(&out_size, ip);

    char *json_buffer = (char *)qoraal_malloc(_webapi_heap, json_length + 1);
    if (json_buffer == NULL) {
        return NULL;
    }

    struct json_out out_buffer = JSON_OUT_BUF(json_buffer, json_length + 1);
    generate_wot_json(&out_buffer, ip);

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
        if (current->type == PROPERTY_TYPE_ACTION) {
            current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
            continue;
        }

        if (!first) {
            total_length += json_printf(out, ",");
        }
        first = false;

        total_length += json_printf(out, "%s:", current->name);
        total_length += webapi_print_prop_value(current, out, true);

        current = linked_next(current, OFFSETOF(WEBAPI_PROP_T, next));
    }

    total_length += json_printf(out, "}");
    return total_length;
}

static size_t
generate_simple_property_json(WEBAPI_PROP_T *prop, struct json_out *out)
{
    size_t total_length = 0;

    total_length += json_printf(out, "{");
    total_length += json_printf(out, "value:");
    total_length += webapi_print_prop_value(prop, out, true);
    total_length += json_printf(out, "}");

    return total_length;
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
        if (!prop) {
            return NULL;
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
webapi_post_property_json_cb(const char *key, const char *value, void *arg)
{
    WEBAPI_PROP_T *prop = (WEBAPI_PROP_T *) arg;

    if (!prop) {
        return E_PARM;
    }

    if (strcmp(key, "value") != 0) {
        return E_PARM;
    }

    return webapi_prop_set_from_cstr(prop, value);
}

static int32_t
webapi_post_property_json(WEBAPI_INST_T *inst, const char *property, const char *json)
{
    WEBAPI_PROP_T *prop;
    int32_t res;
    int count = 0;

    if (!inst || !property || !property[0] || !json) {
        return E_PARM;
    }

    prop = webapi_prop_get(inst, property);
    if (!prop) {
        return E_NOTFOUND;
    }

    res = webapi_json_object_foreach(json, webapi_post_property_json_cb, prop, &count);
    if (res < 0) {
        return res;
    }

    if (count != 1) {
        return E_PARM;
    }

    return EOK;
}

/*
 * is_json == true:
 *   property == NULL  -> bulk update with JSON object
 *   property != NULL  -> single-property update with {"value": ...}
 *
 * is_json == false:
 *   property must be != NULL and body is treated as plain text
 */
int32_t
webapi_post(const char *ep, const char *property, const char *body, bool is_json)
{
    WEBAPI_INST_T *inst;

    if (!ep || !ep[0] || !body) {
        return E_PARM;
    }

    inst = webapi_inst_get(ep);
    if (!inst) {
        return E_NOTFOUND;
    }

    if (property && property[0]) {
        if (is_json) {
            return webapi_post_property_json(inst, property, body);
        } else {
            return webapi_post_property_text(inst, property, body);
        }
    }

    if (!is_json) {
        return E_PARM;
    }

    return webapi_post_bulk_json(inst, body);
}

#endif /* CFG_JSON_DISABLE */