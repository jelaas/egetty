#ifndef SKBUFF_H
#define SKBUFF_H

/*
 * Excellent HOWTO by David Miller:
 *  http://vger.kernel.org/~davem/skb_data.html
 * (The basic functions are the same).
 */

struct sk_buff {
  unsigned int len;
  unsigned char *transport_header;
  unsigned char *network_header;
  unsigned char *mac_header;
  
  unsigned char *tail;
  unsigned char *end;
  unsigned char *head, *data;
};

struct sk_buff *alloc_skb(unsigned int size);
void free_skb(struct sk_buff *skb);
void skb_reset(struct sk_buff *skb);

/*
 * private struct but share data
 */
struct sk_buff *skb_clone(struct sk_buff *skb);

/*
 * make copy of skb keep headroom and tailroom
 */
struct sk_buff *skb_copy(const struct sk_buff *skb);

struct sk_buff *skb_copy_expand(const struct sk_buff *skb,
				int newheadroom, int newtailroom);

/* create headroom */
void skb_reserve(struct sk_buff *skb, int len);

/* query headroom */
unsigned int skb_headroom(const struct sk_buff *skb);

/* query tailroom */
int skb_tailroom(const struct sk_buff *skb);

/* append data */
unsigned char *skb_put(struct sk_buff *skb, unsigned int len);

/* prepend data */
unsigned char *skb_push(struct sk_buff *skb, unsigned int len);

/* remove data from head */
unsigned char *skb_pull(struct sk_buff *skb, unsigned int len);

/* set absolute length. Can be used tio remove data from tail */
void skb_trim(struct sk_buff *skb, unsigned int len);

#endif
