#ifndef __TELNETD_H__
#define __TELNETD_H__

#include "uip/uipopt.h"

void telnetd_appcall(void);

#define uip_tcp_appstate_t u8_t
#ifndef UIP_APPCALL
#define UIP_APPCALL     telnetd_appcall
#endif

#define TCPTXBUFSIZE 7

#endif /* __TELNETD_H__ */
