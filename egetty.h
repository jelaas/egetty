#ifndef EGETTY_H
#define EGETTY_H

#define ETH_P_EGETTY 0x6811

enum { EGETTY_SCAN=0, EGETTY_KMSG, EGETTY_HUP, EGETTY_HELLO, EGETTY_IN, EGETTY_OUT, EGETTY_WINCH };

/*
 Format of packet:
 uint8_t type;
 uint8_t console_no;

 uint8_t data[];

 */

#endif
