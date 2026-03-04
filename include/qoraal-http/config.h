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

/* CFG_WSERVER_USER_THREAD_SIZE 
    Threadsize for incomming http connection
*/
#if !defined(CFG_WSERVER_USER_THREAD_SIZE)
#define CFG_WSERVER_USER_THREAD_SIZE    4000
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