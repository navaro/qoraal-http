
#include "qoraal/qoraal.h"
#include "qoraal-http/qoraal.h"

extern void    keep_httpcmds (void) ;

int32_t qoraal_http_init_default ()
{
    keep_httpcmds () ;
#if !(defined(CFG_HTTPSERVER_TLS_DISABLE) && defined(CFG_HTTPCLIENT_TLS_DISABLE))
    mbedtlsutils_init () ;
#endif
    return EOK ;
}
