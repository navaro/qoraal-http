#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "qoraal/qoraal.h"
#include "qoraal/svc/svc_services.h"
#include "qoraal/svc/svc_tasks.h"
#include "qoraal/svc/svc_shell.h"
#include "services.h"



/*===========================================================================*/
/* Macros and Defines                                                        */
/*===========================================================================*/

#define DBG_MESSAGE_LWIP(severity, fmt_str, ...)   DBG_MESSAGE_T_LOG (SVC_LOGGER_TYPE(severity,0), QORAAL_SERVICE_LWIP, fmt_str, ##__VA_ARGS__)

#define SHELL_VERSION_STR   "Navaro Qoraal Demo v '" __DATE__ "'"
#define SHELL_HELLO         "Enter 'help' or '?' to view available commands. "
#define SHELL_PROMPT        "[Qoraal] #> "

/*===========================================================================*/
/* Service Local Functions                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Service Local Variables and Types                                         */
/*===========================================================================*/
static p_sem_t              _lwip_stop_sem ;

/*===========================================================================*/
/* Service Functions                                                         */
/*===========================================================================*/

/**
 * @brief       lwip_service_ctrl
 * @details
 * @note        For code SVC_SERVICE_CTRL_STATUS, if the return value is E_NOIMPL
 *              the status will be determined by the svc_services module.
 *
 * @param[in] code
 * @param[in] arg
 *
 * @return      status
 *
 * @services
 */
int32_t
lwip_service_ctrl (uint32_t code, uintptr_t arg)
{
    int32_t res = EOK ;

    switch (code) {
    case SVC_SERVICE_CTRL_INIT:
        break ;

    case SVC_SERVICE_CTRL_START:
        os_sem_create (&_lwip_stop_sem, 0) ;
        break ;

    case SVC_SERVICE_CTRL_STOP:
        os_sem_signal (&_lwip_stop_sem) ;
        break ;

    case SVC_SERVICE_CTRL_STATUS:
    default:
        res = E_NOIMPL ;
        break ;

    }

    return res ;
}

/**
 * @brief       lwip_service_run
 * @details     
 *
 * @param[in]   arg     Argument passed to the service.
 *
 * @return      status  The result of the shell execution.
 */
int32_t
lwip_service_run (uintptr_t arg)
{

    while (1) {
        if (os_sem_wait_timeout (&_lwip_stop_sem, OS_S2TICKS(120)) == EOK) {
            break ;
        }


    }

    os_sem_delete (&_lwip_stop_sem) ;

    return EOK ;
}
