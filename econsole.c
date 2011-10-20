/*
 * File: econsole.c
 * Implements: ethernet console client
 *
 * Copyright: Jens Låås, 2011
 * Copyright license: According to GPL, see file COPYING in this directory.
 *
 */

#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h> /* the L2 protocols */

#include <stdio.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <poll.h>

#include <termios.h> /* for tcgetattr */
#include <sys/ioctl.h> /* for winsize */
#include <signal.h>

#include <time.h>

#include <net/if.h>

#include "egetty.h"
#include "skbuff.h"
#include "jelopt.h"

struct {
	int debug;
	int console;
	int devsocket;
	int scan;
	int ucast;
	int row, col;
	int s;
	int ifindex;
	
	struct sockaddr_ll dest;
	
	struct termios term;
} conf;

int console_send(int s, int ifindex, struct sk_buff *skb)
{
	struct sockaddr_ll dest;
	socklen_t destlen = sizeof(dest);
	int rc;

	memset(&dest, 0, sizeof(dest));

	dest.sll_family = AF_PACKET;
	dest.sll_halen = 6;
	dest.sll_protocol = htons(ETH_P_EGETTY);
	dest.sll_ifindex = ifindex;
	if(conf.ucast)
		memcpy(dest.sll_addr, conf.dest.sll_addr, 6);
	else
		memset(dest.sll_addr, 255, 6);
	
	rc = sendto(s, skb->data, skb->len, 0, (const struct sockaddr *)&dest, destlen);
	if(rc == -1) {
		return -1;
	}
	if(conf.debug) {
		int i;
		printf("sent %d bytes to ", skb->len);
		for(i=0;i<6;i++)
			printf("%02x%s", dest.sll_addr[i], i==5?"":":");
		printf("\n");
	}
	return 0;
}

int console_put(int s, int ifindex, struct sk_buff *skb)
{
	int rc;
	uint8_t *p;

	p = skb_push(skb, 2);
	*p++ = EGETTY_IN;
	*p = conf.console;
	
	rc = console_send(s, ifindex, skb);
	if(rc == -1) {
		printf("sendto failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int console_winch(int s, int ifindex, int row, int col)
{
	int rc;
	uint8_t *p;
	struct sk_buff *skb = alloc_skb(64);

	p = skb_put(skb, 0);
	*p++ = EGETTY_WINCH;
	*p++ = conf.console;
	*p++ = row;
	*p++ = col;
	p = skb_put(skb, 4);
	
	rc = console_send(s, ifindex, skb);
	if(rc == -1) {
		printf("sendto failed: %s\n", strerror(errno));
		free_skb(skb);
		return -1;
	}
	free_skb(skb);
	return 0;
}

static void winch_handler(int sig)
{
	struct winsize winp;

	if(!ioctl( 0, TIOCGWINSZ, &winp))
	{
		conf.row = winp.ws_row;
		conf.col = winp.ws_col;
		console_winch(conf.s, conf.ifindex, conf.row, conf.col);
	}
}

static int signals_init()
{
	static struct sigaction act;
	int rc = 0;

	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_handler = winch_handler;
	act.sa_flags = 0;
	rc |= sigaction(SIGWINCH, &act, NULL);

	return rc;
}

int terminal_settings()
{
	struct termios term;
	
	tcgetattr(0, &conf.term);
	
	if(tcgetattr(0, &term))
		if(conf.debug) fprintf(stderr, "ERROR tcgetattr!\n");
	
	term.c_lflag &= ~ICANON;
	term.c_lflag &= ~ECHO;
	term.c_lflag &= ~ISIG;
	
	return tcsetattr(0, TCSANOW, &term);
}

static int devsocket(void)
{
	/* we couldn't care less which one; just want to talk to the kernel! */
	static int dumb[3] = { AF_INET, AF_PACKET, AF_INET6 };
	int i, fd;
  
	for(i=0; i<3; i++)
		if((fd = socket(dumb[i], SOCK_DGRAM, 0)) >= 0)
			return fd;
	return -1;
}


/* Set a certain interface flag. */
static int set_flag(char *ifname, short flag)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	
	strcpy(ifr.ifr_name, ifname);
	
	if (ioctl(conf.devsocket, SIOCGIFFLAGS, &ifr) < 0) {
		return -1;
	}
	strcpy(ifr.ifr_name, ifname);
	ifr.ifr_flags |= flag;
	if (ioctl(conf.devsocket, SIOCSIFFLAGS, &ifr) < 0) {
		return -1;
	}
	return 0;
}

int console_bcast(int s, int ifindex, struct sk_buff *skb)
{
	struct sockaddr_ll dest;
	socklen_t destlen = sizeof(dest);

	memset(&dest, 0, sizeof(dest));

	dest.sll_family = AF_PACKET;
	dest.sll_halen = 6;
	dest.sll_protocol = htons(ETH_P_EGETTY);
	dest.sll_ifindex = ifindex;
	memset(dest.sll_addr, 255, 6);
	
	return sendto(s, skb->data, skb->len, 0, (const struct sockaddr *)&dest, destlen);
}

int console_scan(int s, int ifindex, struct sk_buff *skb)
{
	int rc;
	uint8_t *p;

	p = skb_push(skb, 2);
	*p++ = EGETTY_SCAN;
	*p = conf.console;
	
	rc = console_bcast(s, ifindex, skb);
	if(rc == -1) {
		fprintf(stderr, "sendto failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}


int main(int argc, char **argv)
{
	struct sockaddr_ll from;
	socklen_t fromlen = sizeof(from);
	char *device = "eth0", *ps;
	uint8_t *buf, *p;
	int n, i, err=0;
	struct sk_buff *skb;

	conf.ifindex=-1;
	conf.debug = 0;

	if(jelopt(argv, 'h', "help", NULL, &err)) {
		printf("econsole [DEV] [CONSOLE] [DESTMAC] [(scan|debug)]\n"); 
		exit(0);
	}
	argc = jelopt_final(argv, &err);
	if(err) {
		printf("Syntax error in arguments.\n");
		exit(2);
	}

	while(--argc > 0) {
		if(strcmp(argv[argc], "scan")==0) {
			printf("Scanning for econsoles\n");
			conf.scan = 1;
			continue;
		}
		if(strcmp(argv[argc], "debug")==0) {
			printf("Debug mode\n");
			conf.debug = 1;
			continue;
		}
		if( (strlen(argv[argc]) < 3) && isdigit(*argv[argc])) {
			conf.console = atoi(argv[argc]);
			continue;
		}
		if(strchr(argv[argc], ':' )) {
			unsigned int a;
			ps = argv[argc];
			for(i=0;i<6;i++) {
				sscanf(ps, "%x", &a);
				conf.dest.sll_addr[i] = a;
				ps = strchr(ps, ':');
				if(!ps) break;
				ps++;
			}
			conf.ucast = 1;
			continue;
		} else {
			device = argv[argc];
		}
	}
	
	conf.devsocket = devsocket();
	
	while(set_flag(device, (IFF_UP | IFF_RUNNING))) {
		printf("Waiting for interface to be available\n");
		sleep(1);
	}
	
	if(device)
	{
		conf.ifindex = if_nametoindex(device);
		if(!conf.ifindex)
		{
			fprintf(stderr, "no such device %s\n", device);
			exit(1);
		}
	}

	conf.s = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_EGETTY));
	if(conf.s == -1)
	{
		fprintf(stderr, "socket(): %s\n", strerror(errno));
		exit(1);
	}


	if(conf.ifindex >= 0)
	{
		struct sockaddr_ll addr;
		memset(&addr, 0, sizeof(addr));
		
		addr.sll_family = AF_PACKET;
		addr.sll_protocol = htons(ETH_P_EGETTY);
		addr.sll_ifindex = conf.ifindex;
		
		if(bind(conf.s, (const struct sockaddr *)&addr, sizeof(addr)))
		{
			fprintf(stderr, "bind failed: %s\n", strerror(errno));
			exit(1);
		}
	}

	if(!conf.scan) {
		terminal_settings();
		signals_init();
		winch_handler(0);
		fprintf(stderr, "Use CTRL-] to close connection.\n");
	}

	skb = alloc_skb(1500);

	if(conf.scan)
		console_scan(conf.s, conf.ifindex, skb);

	while(1)
	{
		struct pollfd fds[2];

		fds[0].fd = 0;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		
		fds[1].fd = conf.s;
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		n = poll(fds, 2, -1);
		if(n == 0) {
			printf("timeout\n");
			continue;
		}

		if(fds[0].revents & POLLIN) {
			skb_reset(skb);
			skb_reserve(skb, 2);
			buf = skb_put(skb, 0);
			n = read(0, buf, skb_tailroom(skb));
			if(n == -1) {
				fprintf(stderr, "read() failed\n");
				exit(1);
			}
			if(n == 0) {
				fprintf(stderr, "read() EOF\n");
				exit(0);
			}
			buf[n] = 0;
			if(n==1 && buf[0] == 0x1d) {
				tcsetattr(0, TCSANOW, &conf.term);
				exit(0);
			}
			skb_put(skb, n);
			if(!conf.scan) console_put(conf.s, conf.ifindex, skb);
		}
		if(fds[1].revents) {
			skb_reset(skb);
			buf = skb_put(skb, 0);
			n = recvfrom(conf.s, buf, skb_tailroom(skb), 0, (struct sockaddr *)&from, &fromlen);
			if(n == -1) {
				fprintf(stderr, "recvfrom() failed. ifconfig up?\n");
				continue;
			}
			skb_put(skb, n);
			
			if(ntohs(from.sll_protocol) == ETH_P_EGETTY) {
				if(conf.debug) printf("Received EGETTY\n");
				p = skb->data;
				if(*p == EGETTY_HELLO) {
					if(conf.scan) {
						p++;
						printf("Console: %d ", *p);
						for(i=0;i<6;i++)
							printf("%02x%s", from.sll_addr[i], i==5?"":":");
						printf("\n");
					}
					continue;
				}
				if(*p != EGETTY_OUT) continue;
				p++;
				if(*p++ != conf.console) continue;
				skb_pull(skb, 2);
				write(1, skb->data, skb->len);
				continue;
			}
		}
		
	}
	
	exit(0);
}
