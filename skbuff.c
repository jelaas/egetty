/*
 * File: skbuff.c
 * Implements: sk_buff handling a la Linux kernel
 *
 * Copyright: Jens Låås, 2011
 * Copyright license: According to GPL, see file COPYING in this directory.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "skbuff.h"

struct sk_buff *alloc_skb(unsigned int size)
{
	struct sk_buff *skb;
	void *data;

	data = malloc(size);
	if(!data) return NULL;
	
	skb = malloc(sizeof(struct sk_buff));
	if(!skb) {
		free(data);
		return NULL;
	}
	memset(skb, 0, sizeof(struct sk_buff));
	skb->head = data;
	skb->data = skb->tail = skb->head;
	skb->end = skb->head + size;
	return skb;
}

void skb_reset(struct sk_buff *skb)
{
	skb->data = skb->head;
	skb->tail = skb->head;
	skb->len = 0;
}

void free_skb(struct sk_buff *skb)
{
	free(skb->head);
	memset(skb, 0, sizeof(struct sk_buff));
	free(skb);
}

struct sk_buff *skb_clone(struct sk_buff *skb)
{
	struct sk_buff *nskb;

	nskb = malloc(sizeof(struct sk_buff));
	if(!nskb) {
		return NULL;
	}
	memcpy(nskb, skb, sizeof(struct sk_buff));
	return nskb;
}

struct sk_buff *skb_copy_expand(const struct sk_buff *skb,
				int newheadroom, int newtailroom)
{
	struct sk_buff *nskb;
	void *ndata;

	ndata = malloc(skb->len + newheadroom + newtailroom);
	if(!ndata) return NULL;
	
	nskb = malloc(sizeof(struct sk_buff));
	if(!nskb) {
		free(ndata);
		return NULL;
	}
	memcpy(nskb, skb, sizeof(struct sk_buff));
	memcpy(ndata + newheadroom, skb->data, skb->len);
	
	nskb->head = ndata;
	nskb->data = ndata + newheadroom;
	nskb->end = ndata + skb->len + newheadroom + newtailroom;
	nskb->tail = ndata + skb->len + newheadroom;
	
	return nskb;
}

/*
 * make copy of skb keep headroom and tailroom
 */
struct sk_buff *skb_copy(const struct sk_buff *skb)
{
	struct sk_buff *nskb;
	void *ndata;

	ndata = malloc(skb->end - skb->head);
	if(!ndata) return NULL;
	
	nskb = malloc(sizeof(struct sk_buff));
	if(!nskb) {
		free(ndata);
		return NULL;
	}
	memcpy(nskb, skb, sizeof(struct sk_buff));
	memcpy(ndata, skb->head, skb->end - skb->head);
	
	nskb->head = ndata;
	nskb->data = ndata + skb_headroom(skb);
	nskb->end = ndata + (skb->end - skb->head);
	nskb->tail = nskb->data + skb->len;
	
	return nskb;
}

/* create headroom */
void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data += len;
	skb->tail += len;
}

/* query headroom */
unsigned int skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}

/* query tailroom */
int skb_tailroom(const struct sk_buff *skb)
{
	return skb->end - skb->tail;
}

/* append data */
unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb->tail;
	skb->tail += len;
	skb->len  += len;
	return tmp;
}

/* prepend data */
unsigned char *skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len  += len;
	return skb->data;
}

/* remove data from head */
unsigned char *skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	return skb->data += len;
}

/* set absolute length. Can be used tio remove data from tail */
void skb_trim(struct sk_buff *skb, unsigned int len)
{
	skb->len = len;
	skb->tail = skb->data + len;
}
