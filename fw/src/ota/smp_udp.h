/* MCUmgr (SMP) over UDP, development builds (see smp_udp.c). */
#ifndef WS_SMP_UDP_H_
#define WS_SMP_UDP_H_

#include <stdint.h>

/* Binds the port and starts the receive thread; 0 if already open. */
int ws_smp_udp_open(uint16_t port);

#endif
