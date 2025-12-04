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
    If defined, the http server will enable mbedtls.
*/
#if !defined(CFG_HTTPCLIENT_TLS_DISABLE)
// #define CFG_HTTPCLIENT_TLS_DISABLE    1
#endif

/* CFG_MBEDTLS_PLATFORM_INIT_ENABLE
    If defined, the mbedtls platform init function will be called.
*/
// #define CFG_MBEDTLS_PLATFORM_INIT_ENABLE    1

