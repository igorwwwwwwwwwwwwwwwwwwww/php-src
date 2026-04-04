#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#include <stdint.h>

void rp2350_sntp_set_system_time_us(uint32_t sec, uint32_t us);

/* Minimal lwIP config for CYW43 station-mode bring-up on RP2350. */
#define NO_SYS                      1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   1
#define LWIP_ICMP                   1
#define LWIP_ICMP6                  1
#define LWIP_DNS_ADDRTYPE_DEFAULT   3 /* LWIP_DNS_ADDRTYPE_IPV6_IPV4 */
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
#define TCP_WND                     (32 * TCP_MSS)
#define PBUF_POOL_SIZE              64
#define MEMP_NUM_TCP_SEG            64

/* httpd */
#define LWIP_HTTPD                  1
#define LWIP_HTTPD_CUSTOM_FILES     1
#define LWIP_HTTPD_DYNAMIC_HEADERS  1
#define LWIP_HTTPD_DYNAMIC_FILE_READ 1
#define HTTPD_SERVER_AGENT          "php-mcu/1"
#define LWIP_HTTPD_MAX_REQUEST_URI_LEN 256

/* TCP PCB pool -- default 5 is too small once httpd + outbound conns coexist */
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_TCP_PCB_LISTEN     4

#endif /* _LWIPOPTS_H */
