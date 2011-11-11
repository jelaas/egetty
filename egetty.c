/*
 * File: egetty.c
 * Implements: ethernet getty
 *
 * Copyright: Jens Låås, 2011
 * Copyright license: According to GPL, see file COPYING in this directory.
 *
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <unistd.h>
#include <sys/wait.h>

#include <sys/socket.h>
#include <netpacket/packet.h>
#include <net/ethernet.h> /* the L2 protocols */
#include <net/if_arp.h>

#include <stdio.h>
#include <ctype.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <time.h>

#include <net/if.h>

#include "egetty.h"

#include "skbuff.h"

static char **envp;

struct {
	int console;
	char *device;
	int kmsg;
	int debug;
	struct sockaddr_ll client;
	int devsocket;
} conf;

char *indextoname(unsigned int ifindex)
{
	static char ifname[IF_NAMESIZE+1];
	
	if(if_indextoname(ifindex, ifname))
		return ifname;
	return "ENXIO";
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

int putfd(int fd, char *s)
{
	if(s)
		write(fd, s, strlen(s));
	return 0;
}

pid_t login(int *fd)
{
	pid_t pid;
	int amaster, tty, rc=0;
	char name[256];

	pid = forkpty(&amaster, name,
		      NULL, NULL);
	if(pid == 0) {
		/* child */
		if(conf.kmsg) {
			if ((rc=ioctl(0, TIOCCONS, 0))) {
				if(conf.debug) {
					putfd(1, "TIOCCONS: ");
					putfd(1, strerror(errno));
				}
				if (errno == EBUSY) {
					tty = open("/dev/tty0", O_WRONLY);
					if (tty >= 0) {
						if ((rc=ioctl(tty, TIOCCONS, 0))==0) {
							rc=ioctl(0, TIOCCONS, 0);
							if(conf.debug && rc) {
								putfd(1, "TIOCCONS: ");
								putfd(1, strerror(errno));
							}
						}
						close(tty);
					} else
						rc = -1;
				}
			}
			if(conf.debug) {
				if(rc)
					putfd(1, "failed to redirect console\n");
				else
					putfd(1, "redirected console\n");
			}
		}

		char *argv[]={"/bin/login", "--", 0, 0};
		(void) execve( argv[0], argv, envp );
		printf("execve failed\n");
		exit(1);
	}
	if(pid == -1) return -1;
	*fd = amaster;

	return pid;
}

int console_ucast(int s, int ifindex, struct sk_buff *skb)
{
	struct sockaddr_ll dest;
	socklen_t destlen = sizeof(dest);

	memset(&dest, 0, sizeof(dest));

	dest.sll_family = AF_PACKET;
	dest.sll_halen = 6;
	dest.sll_protocol = htons(ETH_P_EGETTY);
	dest.sll_ifindex = ifindex;
	memcpy(dest.sll_addr, conf.client.sll_addr, 6);
	
	return sendto(s, skb->data, skb->len, 0, (const struct sockaddr *)&dest, destlen);
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

int console_put(int s, int ifindex, struct sk_buff *skb)
{
	int rc;
	uint8_t *p;

	p = skb_push(skb, 4);
	*p++ = EGETTY_OUT;
	*p++ = conf.console;
	*p++ = skb->len >> 8;
	*p = skb->len & 0xff;

	rc = console_ucast(s, ifindex, skb);
	if(rc == -1) {
		printf("sendto failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int console_hello(int s, int ifindex, struct sk_buff *skb)
{
	int rc;
	uint8_t *p;

	p = skb_push(skb, 4);
	*p++ = EGETTY_HELLO;
	*p++ = conf.console;
	*p++ = skb->len >> 8;
	*p = skb->len & 0xff;

	rc = console_bcast(s, ifindex, skb);
	if(rc == -1) {
		printf("sendto failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char **argv, char **arge)
{
	int s;
	struct sockaddr_ll from;
	socklen_t fromlen = sizeof(from);
	int ifindex=-1;
	uint8_t *buf, *p;
	ssize_t n;
	unsigned int len;
	int count=1;
	int timeout = -1;
	int loginfd = -1;
	pid_t pid=-1;
	struct sk_buff *skb;
	
	envp = arge;
	conf.debug = 0;
	conf.device = "eth0";
	conf.devsocket = -1;
	
	while(--argc > 0) {
		if(strcmp(argv[argc], "debug")==0) {
			printf("Debug mode\n");
			conf.debug = 1;
			continue;
		}
		if(strcmp(argv[argc], "console")==0) {
			conf.kmsg = 1;
			continue;
		}
		if( (strlen(argv[argc]) < 3) && isdigit(*argv[argc])) {
			conf.console = atoi(argv[argc]);
			continue;
		}
		conf.device = argv[argc];
	}

	conf.devsocket = devsocket();
	
	/* wait for interface to become available */
	while(set_flag(conf.device, (IFF_UP | IFF_RUNNING))) {
		sleep(1);
	}
	
	memset(conf.client.sll_addr, 255, 6);
	
	s = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_EGETTY));
	if(s == -1)
	{
		fprintf(stderr, "socket(): %s\n", strerror(errno));
		exit(1);
	}
	
	if(conf.device)
	{
		ifindex = if_nametoindex(conf.device);
		if(!ifindex)
		{
			fprintf(stderr, "Not an interface\n");
			exit(1);
		}
	}
	
	
	if(ifindex >= 0)
	{
		struct sockaddr_ll addr;
		memset(&addr, 0, sizeof(addr));
		
		addr.sll_family = AF_PACKET;
		addr.sll_protocol = htons(ETH_P_EGETTY);
		addr.sll_ifindex = ifindex;
		
		if(bind(s, (const struct sockaddr *)&addr, sizeof(addr)))
		{
			fprintf(stderr, "bind() to interface failed\n");
			exit(1);
		}
	}
	
	skb = alloc_skb(1500);

	while(count)
	{
		struct pollfd fds[2];

		if(pid) {
			int status;
			if(waitpid(-1, &status, WNOHANG)>0) {
				pid = -1;
				close(loginfd);
			}
		}
		if(pid == -1) {
			pid = login(&loginfd);
			if(pid == -1)
				exit(1);
			if(conf.debug) {
				printf("child pid = %d\n", pid);
				printf("loginfd = %d\n", loginfd);
			}
		}

		fds[0].fd = s;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		
		fds[1].fd = loginfd;
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		n = poll(fds, 2, timeout);
		if(n == 0) {
			printf("timeout\n");
			exit(1);
		}

		if(fds[1].revents & POLLIN) {
			if(conf.debug) printf("POLLIN child\n");
			skb_reset(skb);
			skb_reserve(skb, 2);
			buf = skb_put(skb, 0);
			n = read(loginfd, buf, skb_tailroom(skb));
			if(n == -1) {
				fprintf(stderr, "read() failed\n");
				exit(1);
			}
			
			buf[n] = 0;
			if(conf.debug)
				printf("child: %d bytes\n", n);
			skb_put(skb, n);
			console_put(s, ifindex, skb);
		}
		if(fds[0].revents) {
			skb_reset(skb);
			buf = skb_put(skb, 0);
			n = recvfrom(s, buf, skb_tailroom(skb), 0, (struct sockaddr *)&from, &fromlen);
			if(n == -1) {
				fprintf(stderr, "recvfrom() failed. ifconfig up?\n");
				exit(1);
			}

			skb_put(skb, n);
			if(conf.debug) printf("received packet %d bytes\n", skb->len);
			
			if(ntohs(from.sll_protocol) == ETH_P_EGETTY) {
				if(conf.debug)
					printf("Received EGETTY\n");
				
				p = skb->data;
				if(*p == EGETTY_HUP) {
					if(pid != -1) kill(pid, 9);
					continue;
				}
				
				if(*p == EGETTY_SCAN) {
					skb_reset(skb);
					skb_reserve(skb, 2);
					console_hello(s, ifindex, skb);
					continue;
				}
				
				if(*p == EGETTY_WINCH) {
					p++;
					if(*p != conf.console) {
						if(conf.debug)
							printf("Wrong console %d not %d\n", *p, conf.console);
						continue;
					}
					p++;
					{
						struct winsize winp;
						winp.ws_row = *p++;
						winp.ws_col = *p++;
						winp.ws_xpixel = 0;
						winp.ws_ypixel = 0;
						ioctl(loginfd, TIOCSWINSZ, &winp);
						if(conf.debug)
							printf("WINCH to %d, %d\n", winp.ws_row, winp.ws_col);
					}
					continue;
				}
				
				if(*p != EGETTY_IN) {
					if(conf.debug)
						printf("Not EGETTY_IN: %d\n", *p);
					continue;
				}
				p++;
				if(*p != conf.console) {
					if(conf.debug)
						printf("Wrong console %d not %d\n", *p, conf.console);
					continue;
				}
				memcpy(conf.client.sll_addr, from.sll_addr, 6);
				p++;
				len = *p++ << 8;
				len += *p;
				if(len > n) {
					printf("Length field too long: %d\n", len);
					continue;
				}
				skb_trim(skb, len);
				skb_pull(skb, 4);
				if(conf.debug) printf("Sent %d bytes to child\n", skb->len);
				write(loginfd, skb->data, skb->len);
				continue;
			}
		}
		
	}
  exit(0);
}
