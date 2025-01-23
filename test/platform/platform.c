#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include "qoraal/common/rtclib.h"
#include "qoraal/common/dictionary.h"


#define PLATFORM_FLASH_SIZE     (1024*1024*10)


static SVC_SERVICES_T _platform_complete_service_id = SVC_SERVICES_INVALID ;

int32_t         
platform_init ()
{
    return 0 ;
}

int32_t         
platform_start ()
{
    os_thread_sleep (100) ;
    return 0 ;
}


void *      
platform_malloc (QORAAL_HEAP heap, size_t size)
{
    return malloc (size) ;
}
void        

platform_free (QORAAL_HEAP heap, void *mem)
{
    free (mem) ;
}

void
platform_print (const char *format)
{
    printf ("%s", format) ;
}

void
platform_assert (const char *format)
{
    printf ("%s", format) ;
    abort () ;
}

uint32_t    
platform_current_time (void)
{
    return os_sys_timestamp () / 1000 ;
}

uint32_t 
platform_wdt_kick (void)
{
    return 20 ;
}


static p_sem_t    _main_stop_sem ;
void 
status_callback (SVC_SERVICES_T  id, int32_t status)
{
    if (status == SVC_SERVICE_STATUS_STOPPED && id == _platform_complete_service_id) {
        os_sem_signal (&_main_stop_sem) ;
    }
}

void
platform_wait_for_exit (SVC_SERVICES_T service_id)
{
    _platform_complete_service_id = service_id ;
    os_sem_create (&_main_stop_sem, 0) ;
    SVC_SERVICE_HANDLER_T  handler ;
    svc_service_register_handler (&handler, status_callback) ;
    os_sem_wait (&_main_stop_sem) ;
    svc_service_unregister_handler (&handler) ;
    os_sem_delete (&_main_stop_sem) ;
}
