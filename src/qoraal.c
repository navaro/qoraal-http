
#include "qoraal/qoraal.h"
#include "qoraal-http/qoraal.h"

extern void    keep_httpcmds (void) ;

int32_t qoraal_http_init ()
{
    keep_httpcmds () ;
    return EOK ;
}
