#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#include <stdint.h>

void rp2350_sntp_set_system_time_us(uint32_t sec, uint32_t us);

/* Minimal lwIP config for CYW43 station-mode bring-up on RP2350. */
#define NO_SYS                      1
#define LWIP_DHCP                   1
#define LWIP_RAW                    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_DNS                    1
#define LWIP_SNTP                   1
#define SNTP_SERVER_DNS             1
#define SNTP_STARTUP_DELAY          0
#define SNTP_SET_SYSTEM_TIME(sec)   rp2350_sntp_set_system_time_us((sec), 0u)
#define SNTP_SET_SYSTEM_TIME_US(sec, us) rp2350_sntp_set_system_time_us((sec), (us))
#define LWIP_ALTCP                 1
#define LWIP_ALTCP_TLS             1
#define LWIP_ALTCP_TLS_MBEDTLS     1
#define ALTCP_MBEDTLS_AUTHMODE     2 /* MBEDTLS_SSL_VERIFY_REQUIRED */
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (64 * 1024)

#endif /* _LWIPOPTS_H */
