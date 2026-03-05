#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

/* Minimal lwIP config for CYW43 station-mode bring-up on RP2350. */
#define NO_SYS                      1
#define LWIP_DHCP                   1
#define LWIP_RAW                    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_DNS                    1
#define LWIP_ALTCP                 1
#define LWIP_ALTCP_TLS             1
#define LWIP_ALTCP_TLS_MBEDTLS     1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (64 * 1024)

#endif /* _LWIPOPTS_H */
