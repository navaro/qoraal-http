/*
 * css.h
 *
 *  Created on: 8 May 2015
 *      Author: natie
 */

#ifndef SERVICES_WWW_CSS_H_
#define SERVICES_WWW_CSS_H_



#include "qoraal-http/httpserver.h"
#include <stdint.h>


extern int32_t              wcss_handler(HTTP_USER_T *user, uint32_t method, char* endpoint) ;


#endif /* SERVICES_WWW_CSS_H_ */
