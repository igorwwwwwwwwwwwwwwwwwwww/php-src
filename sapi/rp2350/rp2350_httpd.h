#ifndef RP2350_HTTPD_H
#define RP2350_HTTPD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after Wi-Fi is up and Zend is started. */
void rp2350_httpd_init(void);

/* Call from the main loop to process pending HTTP requests.
 * This is where PHP actually executes; it must be called from
 * the main core, not from an lwIP callback. */
void rp2350_httpd_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* RP2350_HTTPD_H */
