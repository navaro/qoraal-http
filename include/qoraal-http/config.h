/* CFG_LITTLEFS_DISABLE
    If defined, littlefs filesystem will be disabled.
*/
#if !defined(CFG_LITTLEFS_DISABLE)
#define CFG_LITTLEFS_DISABLE    1
#endif

/* CFG_JSON_DISABLE
    If defined, littlefs filesystem will be disabled.
*/
// #define CFG_JSON_DISABLE    1


/* CFG_HTTPSERVER_TLS_DISABLE 
    If defined, the http server will enable mbedtls.
*/
#if !defined(CFG_HTTPSERVER_TLS_DISABLE)
// #define CFG_HTTPSERVER_TLS_DISABLE    1
#endif

/* CFG_HTTPCLIENT_TLS_DISABLE 
    If defined, the http client will enable mbedtls.
*/
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE)
// #define CFG_HTTPCLIENT_TLS_DISABLE    1
#endif

/* CFG_MBEDTLS_PLATFORM_INIT_ENABLE
    If defined, the mbedtls platform init function will be called.
*/
// #define CFG_MBEDTLS_PLATFORM_INIT_ENABLE    1

/* CFG_HTTPSERVER_CONTENT_MAX
    Largest request body httpserver_read_all_content_ex() reads into memory.
    A larger one is refused before anything is allocated. Bodies read in
    pieces, such as uploads, are not limited.
*/
#if !defined(CFG_HTTPSERVER_CONTENT_MAX)
#define CFG_HTTPSERVER_CONTENT_MAX      (16*1024)
#endif

/* CFG_HTTPSERVER_HEADER_TIMEOUT
    Milliseconds a request header may take to arrive, from its first byte.
    A client that sends it slower is dropped. Without this limit a client
    that sends a byte now and then holds its worker until the watchdog
    resets the device.
*/
#if !defined(CFG_HTTPSERVER_HEADER_TIMEOUT)
#define CFG_HTTPSERVER_HEADER_TIMEOUT   10000
#endif

/* CFG_HTTPSERVER_CONTENT_TIMEOUT
    Milliseconds httpserver_read_all_content_ex() may take to read a whole
    body. With the header time it stays well below the worker's watchdog
    timeout (60 s). Bodies read in pieces, such as uploads, are bounded
    per read only.
*/
#if !defined(CFG_HTTPSERVER_CONTENT_TIMEOUT)
#define CFG_HTTPSERVER_CONTENT_TIMEOUT  20000
#endif

/* CFG_WSERVER_USER_THREAD_SIZE 
    Threadsize for incomming http connection
*/
#if !defined(CFG_WSERVER_USER_THREAD_SIZE)
#define CFG_WSERVER_USER_THREAD_SIZE    4032
#endif

/*
 * Max number of incomming http connections 
 */
#ifndef CFG_WSERVER_USER_THREAD_MAX
#if !defined(CFG_HTTPSERVER_TLS_DISABLE) || !CFG_HTTPSERVER_TLS_DISABLE
#define CFG_WSERVER_USER_THREAD_MAX     2
#else
#define CFG_WSERVER_USER_THREAD_MAX     4
#endif
#endif

/*
 * Max number of event streams (or other responses that hold their worker)
 * open at a time. One worker is always left for other requests.
 */
#ifndef CFG_HTTPSERVER_STREAMS_MAX
#define CFG_HTTPSERVER_STREAMS_MAX      (CFG_WSERVER_USER_THREAD_MAX - 1)
#endif
