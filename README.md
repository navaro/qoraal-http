# Qoraal HTTP Module

## Features

- **BSD Socket Network Interface** – Provides a lightweight and efficient networking stack.
- **MBedTLS Integration** – Optional SSL/TLS support for secure communication.
- **Authentication Backend Plugin** – Easily integrate custom authentication mechanisms.
- **Basic Authentication Support (HTTP)** – Built-in support for HTTP Basic Authentication.
- **Customizable HTTP Headers** – Fully customizable HTTP request and response headers in the API.
- **WebSockets Support** – Optional WebSockets integration for real-time communication.
- **Multithreaded Webserver** – Included implementation to handle multiple requests concurrently.
- **Low Memory Footprint** – Optimized for embedded systems with constrained resources.
- **Flexible Modular Design** – Framework supports modular development for maintainable and scalable applications.

## Overview
The **Qoraal HTTP Module** is a lightweight and efficient HTTP implementation designed for embedded systems. It consists of three core components:

- **HTTP Server** – A minimal and efficient HTTP server implementation.
- **HTTP Client** – A small-footprint HTTP client, with optional WebSockets support.
- **HTTP Parser** – A shared module used by both the server and the client.

Additionally, a **multithreaded webserver implementation** is included, built using the HTTP Server API, making it easy to develop scalable network applications.

This module is optimized for small memory usage, making it well-suited for embedded applications. Below is a memory usage comparison when compiled with and without WebSockets support:

### Memory Usage Summary

| Module | TEXT (bytes) | DATA (bytes) | Without WebSockets | With WebSockets |
|--------|------------|------------|------------------|----------------|
| HTTP Client | 2764  | 1051 | ✅ | ❌ |
| HTTP Client (Including WebSockets) | 3900 | 1541 | ❌ | ✅ |
| HTTP Parser | 2074  | 276 | ✅ | ✅ |
| HTTP Server | 2902  | 190 | ✅ | ✅ |
| Multithreaded Webserver | 1102 | 307 | ✅ | ✅ |

With WebSockets enabled, the HTTP Client increases in size but remains compact and efficient. The other components remain unchanged, making the module an ideal choice for resource-constrained systems.

## Using the Web Server

![Home Page Example](home.png)

### Setting Up the Server
The web server is created using the **HTTP Server API** by defining handlers and callbacks for headers and footers to build the framework. Here's an example of setting up the server:

```c
int32_t
wserver_start (uintptr_t arg)
{
    uint32_t port = 8080; // Specify the port
    bool ssl = false;     // SSL configuration

    // Define standard headers and footers
    static const WSERVER_FRAMEWORK wserver_std_headers[] = {
            wserver_header_start,
            wserver_handler_framework_start,
            0
    };

    static const WSERVER_FRAMEWORK wserver_std_footers[] = {
            wserver_handler_framework_end,
            wserver_footer_end,
            0
    };

    // Define endpoint handlers
    static WSERVER_HANDLERS_START(handlers)
    WSERVER_HANDLER              ("image",   wimage_handler,            WSERVER_ENDPOINT_ACCESS_OPEN,   0)
    WSERVER_HANDLER              ("css",     wcss_handler,              WSERVER_ENDPOINT_ACCESS_OPEN,   0)
    WSERVER_FRAMEWORK_HANDLER    ("",        windex_handler,            WSERVER_ENDPOINT_ACCESS_OPEN,   0, windex_ctrl,    wserver_std)
    WSERVER_FRAMEWORK_HANDLER    ("about",   wabout_handler,            WSERVER_ENDPOINT_ACCESS_OPEN,   0, wabout_ctrl,    wserver_std)
    WSERVER_FRAMEWORK_HANDLER    ("system",  wsystem_handler,           WSERVER_ENDPOINT_ACCESS_OPEN,   0, wsystem_ctrl,   wserver_std)
    WSERVER_FRAMEWORK_HANDLER    ("log",     wnlog_handler,             WSERVER_ENDPOINT_ACCESS_OPEN,   0, wnlog_ctrl,     wserver_std)
    WSERVER_FRAMEWORK_HANDLER    ("services", wservices_handler,        WSERVER_ENDPOINT_ACCESS_ADMIN,  0, wservices_ctrl, wserver_std)
    WSERVER_FRAMEWORK_HANDLER    ("shell",   wshell_handler,            WSERVER_ENDPOINT_ACCESS_ADMIN,  0, wshell_ctrl,    wserver_std)
    WSERVER_HANDLERS_END()

    _wserver_inst = httpserver_wserver_create (port, ssl, handlers, wserver_authenticate);
    return _wserver_inst ? EOK : EFAIL;
}
```

### Handling HTTP Requests
Inside the registered callback handlers, you can respond to HTTP requests using the **HTTP Server API**. Here's an example of a handler implementation:

```c
int32_t
windex_handler (HTTP_USER_T *user, uint32_t method, char* endpoint)
{
    static const char home_content[] =
            "<br><p align=\"center\" style=\"color:grey\">"
            "<b>" WSERVER_TITLE_TEXT "</b><br>"
            "Use the menu to configure this " WSERVER_TITLE_TEXT "."
            "</p>";

    int32_t res;

    if (method == HTTP_HEADER_METHOD_GET) {
        res = httpserver_chunked_append_str (user, home_content, sizeof(home_content) - 1);
    } else {
        return HTTP_SERVER_WSERVER_E_METHOD;
    }

    return res;
}
```

### Modular Page Rendering
The example web page shown above is rendered modularly by the sample multithreaded web server implementation. Each section of the page—headers, content, and footers—is defined and handled independently, allowing for a clean and flexible structure. We will explore this modular rendering in the next sections.

## HTTP Client - Fetching Data with Ease

The **Qoraal HTTP Client** makes HTTP requests simple and efficient, designed for embedded systems where minimal memory usage is crucial. Below is a straightforward example demonstrating how to perform an HTTP **GET** request and read the full response.

### **Simple HTTP GET Request**
```c
#include "qoraal-http/httpclient.h"
#include "qoraal-http/httpdwnld.h"
#include <stdio.h>
#include <stdlib.h>

int32_t simple_http_get(const char *url)
{
    HTTP_CLIENT_T client;
    int32_t status;
    uint8_t *response;
    int32_t res;

    // Parse the URL
    int https, port;
    char *host, *path, *credentials;
    res = httpdwnld_url_parse((char *)url, &https, &port, &host, &path, &credentials);
    if (res != EOK) {
        printf("Failed to parse URL: %s\n", url);
        return res;
    }

    // Initialize HTTP client
    httpclient_init(&client, 0);

    // Set hostname
    httpclient_set_hostname(&client, host);

    // Connect to server
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);
    res = httpclient_connect(&client, &addr, https);
    if (res != HTTP_CLIENT_E_OK) {
        printf("Failed to connect to server\n");
        httpclient_close(&client);
        return res;
    }

    // Send GET request
    res = httpclient_get(&client, path, credentials);
    if (res < 0) {
        printf("GET request failed\n");
        httpclient_close(&client);
        return res;
    }

    // Read response headers
    res = httpclient_read_response_ex(&client, 5000, &status);
    if (res < 0 || status / 100 != 2) {
        printf("Failed to read response\n");
        httpclient_close(&client);
        return res;
    }

    // Read response body
    while ((res = httpclient_read_next_ex(&client, 5000, &response)) > 0) {
        fwrite(response, 1, res, stdout);
    }

    // Close connection
    httpclient_close(&client);

    return EOK;
}
```

### **How It Works**
1. **Parses the URL** using `httpdwnld_url_parse` to extract host, port, and endpoint.
2. **Initializes the HTTP client** using `httpclient_init`.
3. **Connects to the server** with `httpclient_connect`.
4. **Sends a GET request** using `httpclient_get`.
5. **Reads the response headers** with `httpclient_read_response_ex`.
6. **Reads the response body** using `httpclient_read_next_ex`.
7. **Closes the connection** with `httpclient_close`.

This makes fetching data incredibly simple and efficient. Whether you're retrieving configuration files, logging data, or fetching updates, the **Qoraal HTTP Client** provides a lightweight, modular way to handle HTTP requests.
Alternatively, if there is enough memory available,the complete response can be read with `httpclient_read_response`.

## WebAPI - JSON Interface with Swagger Documentation

The **Qoraal WebAPI** takes the pain out of API development. It provides an easy-to-use **JSON API** for getting and setting values dynamically while **automatically generating Swagger documentation** for seamless integration. No complex configuration—just define properties, plug it into the web server, and you're ready to go.

### **How It Works (Example)**
1. **Define the API Instance & Properties** – Set up the API and its properties:
   ```c
   static WEBAPI_INST_DECL(_wupgrade_api, "UPGRADE API", "1.0", "upgrade");
   static WEBAPI_PROP_DECL(_wupgrade_prop_start, "start", PROPERTY_TYPE_BOOLEAN, "start upgrade", upgrade_start_get, upgrade_start_set);
   static WEBAPI_PROP_DECL(_wupgrade_prop_url, "url", PROPERTY_TYPE_STRING, "upgrade-config url", upgrade_url_get, upgrade_url_set);
   static WEBAPI_PROP_DECL(_wupgrade_prop_status, "status", PROPERTY_TYPE_INTEGER, "system status", upgrade_status_get, 0);
   static WEBAPI_PROP_DECL(_wupgrade_prop_version, "version", PROPERTY_TYPE_STRING, "current version", upgrade_version_get, 0);
   ```
   These get/set functions needs to be implemented seperately.
   
2. **Register the API in the Web Server** – Just a few calls to integrate it:
   ```c
   webapi_init("webapi", QORAAL_HeapAuxiliary);
   webapi_inst_add(&_wupgrade_api);
   webapi_add_property(&_wupgrade_api, &_wupgrade_prop_version);
   webapi_add_property(&_wupgrade_api, &_wupgrade_prop_url);
   webapi_add_property(&_wupgrade_api, &_wupgrade_prop_start);
   webapi_add_property(&_wupgrade_api, &_wupgrade_prop_status);
   ```
3. **Plug It Into the Web Server** – Expose the API via an endpoint:
   ```c
   WSERVER_HANDLER("webapi", wwebapi_handler, WSERVER_ENDPOINT_ACCESS_ADMIN, 0);
   ```
4. **Magic Happens in the Handler** – Handles GET and POST requests dynamically:
   ```c
    // helper function to send endpoint json to the user
    int32_t write_response (HTTP_USER_T *user, char * ep) {
        // generate json for the specified endpoint
        char * json = webapi_generate_simple_json (ep) ;
        if (!json) {
            return httpserver_write_response (user, WSERVER_RESP_CODE_500, HTTP_SERVER_CONTENT_TYPE_HTML,
                0, 0, WSERVER_RESP_CONTENT_500, strlen(WSERVER_RESP_CONTENT_500)) ;
    
        }

        // send the json to the user
        int32_t res = httpserver_write_response (user, 200, HTTP_SERVER_CONTENT_TYPE_JSON,
                0, 0, json, strlen (json)) ;
    
        webapi_simple_json_free (json) ;
        return  res ; 
    }
   
   int32_t wwebapi_handler(HTTP_USER_T *user, uint32_t method, char* endpoint) {
       if (method == HTTP_HEADER_METHOD_GET) {
           return write_response(user, endpoint);
       } else {
           // we received JSON on the post request, update the webapi using webapi_post()
           char *content;
           int32_t len = httpserver_read_all_content_ex(user, 1000, &content);
           return (len > 0 && webapi_post(endpoint, content) == EOK) ? 
                   write_response(user, endpoint) : HTTP_SERVER_E_CONTENT;
       }
   }
   ```
5. **Access the Swagger Docs** – Automatically generated at:
   ```
   https://192.168.2.242/webapi/swagger
   ```
### **Swagger API Specification for this example**
```yaml
openapi: 3.0.0
info:
  title: API Documentation
  version: '1.0'
paths:
  webapi/upgrade:
    get:
      summary: Get all properties of UPGRADE API
      responses:
        '200':
          description: Successful response
          content:
            application/json:
              schema:
                type: object
                properties:
                  version:
                    type: string
                    description: current version
                  url:
                    type: string
                    description: upgrade-config url
                  start:
                    type: boolean
                    description: start upgrade
                  status:
                    type: integer
                    description: system status
```


### **Effortless API Development**
With Qoraal WebAPI, you don’t need to manually define endpoints or worry about handling requests and responses. Everything is dynamically handled, making it **perfect for embedded systems** where simplicity and efficiency are key.

A full implementation of this example is in the `test\services\www\html\wwebapi.h/c` file.



