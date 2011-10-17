#ifndef EGETTY_H
#define EGETTY_H

#define ETH_P_EGETTY 0x6811

enum { EGETTY_SCAN, EGETTY_DMESG, EGETTY_HUP, EGETTY_HELLO, EGETTY_IN, EGETTY_OUT };

/*
 Format of packet:
 uint8_t type;
 uint8_t console_no;

 uint8_t data[];

 */

#endif
