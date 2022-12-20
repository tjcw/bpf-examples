/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/socket.h>

#include <bpf/bpf.h>
#include <xdp/xsk.h>
#include <xdp/libxdp.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/icmpv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/icmp.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <sys/time.h>

#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <sched.h>

#include "common_params.h"
#include "common_user_bpf_xdp.h"
#include <common/common_libbpf.h>

#include "af_xdp_kern_shared.h"

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define RX_BATCH_SIZE 64
#define INVALID_UMEM_FRAME UINT64_MAX
#define IF_QUEUE_MAX 64

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *pin_basedir = "/sys/fs/bpf";

enum {
	k_instrument = false, // Whether to display trace
	k_instrument_detail = false, // Whether to display detailed trace
	k_receive_tuntap = false, // Whether to write packets to a tun interface
	k_verify_umem = false, // Whether to check umem usage
	k_verbose = false, // Whether to give verbose output
	k_timestamp = false, // Whether to put timestamps on trace output
	k_showpacket = false, // Whether to display packet contents
	k_diagnose_setns = false, // Whether to trace setns processing
	k_share_rxtx_umem =
		true, // Whether to share receive and transmit buffers
	k_rewrite_mac_addresses =
		false // Whether to rewrite the source and destinationMAC addresses
};

struct xsk_umem_info {
	struct xsk_umem *umem;
	struct xsk_ring_prod umem_fq;
	struct xsk_ring_cons umem_cq;
	void *buffer;
	uint32_t umem_frame_free;
	uint64_t allocation_count;
	uint64_t free_count;
	char *mark_buffer;
	uint64_t umem_frame_count;
	uint64_t umem_frame_addr[NUM_FRAMES * 2 * (IF_QUEUE_MAX+1)];
};

struct stats_record {
	uint64_t timestamp;
	uint64_t rx_packets;
	uint64_t rx_bytes;
	uint64_t tx_packets;
	uint64_t tx_bytes;
	uint64_t rx_outofsequence;
	uint64_t rx_duplicate;
	uint64_t rx_batch_count;
	uint64_t filter_passes[256];
	uint64_t filter_drops[256];
};

struct transfer_state {
	uint64_t udp_packet_count;
};
struct xsk_socket_info {
	struct xsk_ring_cons rxq;
	struct xsk_ring_prod txq;
	struct xsk_ring_prod umem_fq;
	struct xsk_ring_cons umem_cq;
	struct xsk_socket *xsk;
	int outstanding_tx;
};

struct all_socket_info {
	struct xsk_socket_info *xsk_socket_info[k_rx_queue_count_max];
};

struct tx_socket_info {
	struct xsk_socket_info socket_info;
	unsigned char src_mac[ETH_ALEN];
	unsigned char dst_mac[ETH_ALEN];
};

struct socket_stats {
	struct stats_record stats;
	struct stats_record prev_stats;
	struct transfer_state trans;
	struct timeval start_time;
	uint8_t prev_sequence;
};

struct fivetuple {
	__u32 saddr; // Source address (network byte order)
	__u32 daddr; // Destination address (network byte order)
	__u16 sport; // Source port (network byte order) use 0 for ICMP
	__u16 dport; // Destination port (network byte order) use 0 for ICMP
	__u16 protocol; // Protocol
	__u16 padding;
};

enum { k_hashmap_size = 64 };

const struct bpf_map_info map_expect = { .key_size = sizeof(struct fivetuple),
					 .value_size = sizeof(enum xdp_action),
					 .max_entries = k_hashmap_size,
					 .type = BPF_MAP_TYPE_LRU_HASH };
struct bpf_map_info info = { 0 };

static const char *__doc__ = "AF_XDP kernel bypass example\n";

static const struct option_wrapper long_options[] = {

	{ { "help", no_argument, NULL, 'h' }, "Show help", false },

	{ { "dev", required_argument, NULL, 'd' },
	  "Operate on device <ifname>",
	  "<ifname>",
	  true },
	{ { "redirect-dev", required_argument, NULL, 'r' },
	  "Redirect first packets to this output device",
	  "<ifname>",
	  true },
	{ { "redirect-pid", required_argument, NULL, 'P' },
	  "Redirect first packets to the namespace of this pid",
	  "<pid>",
	  true },

	{ { "skb-mode", no_argument, NULL, 'S' },
	  "Install XDP program in SKB (AKA generic) mode" },

	{ { "native-mode", no_argument, NULL, 'N' },
	  "Install XDP program in native mode" },

	{ { "auto-mode", no_argument, NULL, 'A' },
	  "Auto-detect SKB or native mode" },

	{ { "force", no_argument, NULL, 'F' },
	  "Force install, replacing existing program on interface" },

	{ { "copy", no_argument, NULL, 'c' }, "Force copy mode" },

	{ { "zero-copy", no_argument, NULL, 'z' }, "Force zero-copy mode" },

	{ { "queue-count", required_argument, NULL, 'Q' },
	  "Configure interface receive queue count for AF_XDP" },

	{ { "poll-mode", no_argument, NULL, 'p' },
	  "Use the poll() API waiting for packets to arrive" },

	{ { "unload", no_argument, NULL, 'U' },
	  "Unload XDP program instead of loading" },

	{ { "quiet", no_argument, NULL, 'q' }, "Quiet mode (no output)" },

	{ { "filename", required_argument, NULL, 1 },
	  "Load program from <file>",
	  "<file>" },

	{ { "progsec", required_argument, NULL, 2 },
	  "Load program in <section> of the ELF file",
	  "<section>" },

	{ { 0, 0, NULL, 0 }, NULL, false }
};

enum { k_bytesperline = 16 };
static void hexdump1(FILE *out, const unsigned char *data, unsigned long offset,
		     unsigned long length)
{
	fprintf(out, "\n0x%04lx", offset);
	for (int a = 0; a < length; a += 1)
		fprintf(out, " %02x", data[offset + a]);
}
static void hexdump(FILE *out, const void *data, unsigned long length)
{
	const unsigned char *cdata = data;
	unsigned long fullcount = length / k_bytesperline;
	unsigned int tailcount = length % k_bytesperline;
	for (unsigned long i = 0; i < fullcount; i += 1) {
		hexdump1(out, cdata, (i * k_bytesperline), k_bytesperline);
	}
	if (tailcount > 0)
		hexdump1(out, cdata, (fullcount * k_bytesperline), tailcount);
	fprintf(out, "\n");
}
static void show_mac(unsigned char macaddr[ETH_ALEN])
{
	fprintf(stderr, "%02x:%02x:%02x:%02x:%02x:%02x", macaddr[0], macaddr[1],
		macaddr[2], macaddr[3], macaddr[4], macaddr[5]);
}

static bool global_exit;

static struct xsk_umem_info *configure_xsk_umem(struct xsk_umem_info *umem,
						void *buffer, uint64_t size,
						struct xsk_ring_prod *fq,
						struct xsk_ring_cons *cq,
						unsigned long frame_count )
{
	int ret;
	unsigned long i;
	umem->umem_frame_count = frame_count ;

	ret = xsk_umem__create(&umem->umem, buffer, size, fq, cq, NULL);
	if (ret) {
		errno = -ret;
		return NULL;
	}

	umem->buffer = buffer;
	umem->mark_buffer = k_verify_umem ? calloc(size, 1) : NULL;
	/* Initialize umem frame allocation */

	for (i = 0; i < frame_count; i++)
		umem->umem_frame_addr[i] = i * FRAME_SIZE;

	umem->umem_frame_free = frame_count;
	return umem;
}

static uint64_t umem_alloc_umem_frame(struct xsk_umem_info *umem)
{
	uint64_t frame;
	assert(umem->umem_frame_free > 0);
	if (umem->umem_frame_free == 0)
		return INVALID_UMEM_FRAME;

	frame = umem->umem_frame_addr[--umem->umem_frame_free];
	if (k_verify_umem) {
		uint64_t aligned_frame = frame & ~(FRAME_SIZE - 1);
		assert(umem->mark_buffer[aligned_frame] == 0);
		umem->mark_buffer[aligned_frame] = 1;
	}
	umem->umem_frame_addr[umem->umem_frame_free] = INVALID_UMEM_FRAME;
	umem->allocation_count += 1;
	if (k_instrument_detail)
		printf("umem_alloc_umem_frame umem=%p allocation_count=%ld free_count=%ld frame=0x%lx\n",
		       umem, umem->allocation_count, umem->free_count, frame);
	return frame;
}

static void umem_free_umem_frame(struct xsk_umem_info *umem, uint64_t frame)
{
	if (k_instrument)
		printf("xsk_free_umem_frame xsk=%p allocation_count=%ld free_count=%ld frame=0x%lx\n",
		       umem, umem->allocation_count, umem->free_count, frame);
	if (k_verify_umem) {
		uint64_t aligned_frame = frame & ~(FRAME_SIZE - 1);
		assert(umem->mark_buffer[aligned_frame] == 1);
		umem->mark_buffer[aligned_frame] = 0;
	}
	assert(umem->umem_frame_free < umem->umem_frame_count);

	umem->umem_frame_addr[umem->umem_frame_free++] = frame;
	umem->free_count += 1;
}

static uint64_t xsk_umem_free_frames(struct xsk_umem_info *umem)
{
	return umem->umem_frame_free;
}

static struct xsk_socket_info *
xsk_configure_socket(struct config *cfg, int xsks_map_fd, int if_queue,
		     struct xsk_umem_info *umem_info)
{
	struct xsk_socket_config xsk_cfg;
	struct xsk_socket_info *xsk_info;
	int ret;

	xsk_info = calloc(1, sizeof(*xsk_info));
	if (!xsk_info)
		return NULL;

	/* Allocate memory for NUM_FRAMES of the default XDP frame size */
	int packet_buffer_size = NUM_FRAMES * FRAME_SIZE * 2;
	void *packet_buffer;
	if (posix_memalign(&packet_buffer,
			   getpagesize(), /* PAGE_SIZE aligned */
			   packet_buffer_size)) {
		fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
	xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	xsk_cfg.libbpf_flags = 0;
	xsk_cfg.xdp_flags = cfg->xdp_flags;
	xsk_cfg.bind_flags = cfg->xsk_bind_flags;
	xsk_cfg.libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD;
	ret = xsk_socket__create_shared(&xsk_info->xsk, cfg->ifname, if_queue,
					umem_info->umem, &xsk_info->rxq,
					&xsk_info->txq, &(xsk_info->umem_fq),
					&(xsk_info->umem_cq), &xsk_cfg);

	printf("xsk_socket__create_shared_named_prog returns %d\n", ret);
	if (ret)
		goto error_exit;
	if (xsks_map_fd != -1) {
		int socket_fd = xsk_socket__fd(xsk_info->xsk);
		printf("bpf_map_update_elem(%d,%p,%p,%u)\n", xsks_map_fd,
		       &if_queue, &socket_fd, BPF_ANY);
		ret = bpf_map_update_elem(xsks_map_fd, &if_queue, &socket_fd,
					  BPF_ANY);
		printf("bpf_map_update_elem returns %d\n", ret);
		if (ret)
			goto error_exit;
	}

	/* Stuff the receive path with buffers, we assume we have enough */
	__u32 idx;
	ret = xsk_ring_prod__reserve(&xsk_info->umem_fq,
				     XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);

	printf("xsk_ring_prod__reserve returns %d, XSK_RING_PROD__DEFAULT_NUM_DESCS is %d\n",
	       ret, XSK_RING_PROD__DEFAULT_NUM_DESCS);
	if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
		goto error_exit;

	for (int i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
		*xsk_ring_prod__fill_addr(&xsk_info->umem_fq, idx++) =
			umem_alloc_umem_frame(umem_info);

	xsk_ring_prod__submit(&xsk_info->umem_fq,
			      XSK_RING_PROD__DEFAULT_NUM_DESCS);

	return xsk_info;

error_exit:
	errno = -ret;
	return NULL;
}

static struct all_socket_info *
xsk_configure_socket_all(struct config *cfg, int xsks_map_fd,
			 struct xsk_umem_info *umem_info)
{
	struct all_socket_info *xsk_info_all = calloc(1, sizeof(*xsk_info_all));
	int queue_count = cfg->xsk_if_queue;
	if (queue_count <= 0 || queue_count > k_rx_queue_count_max) {
		fprintf(stderr, "ERROR: queue_count (%d) out of range\n",
			queue_count);
		return NULL;
	}
	for (int q = 0; q < queue_count; q += 1) {
		xsk_info_all->xsk_socket_info[q] =
			xsk_configure_socket(cfg, xsks_map_fd, q, umem_info);
		if (xsk_info_all->xsk_socket_info[q] == NULL) {
			fprintf(stderr, "ERROR: Cannot set up socket %d\n", q);
			return NULL;
		}
	}
	return xsk_info_all;
}

static struct tx_socket_info *xsk_configure_socket_tx(struct config *cfg,
						      struct xsk_umem *umem)
{
	struct tx_socket_info *tx_info =
		calloc(1, sizeof(struct tx_socket_info));
	tx_info->socket_info.outstanding_tx = 0;

	struct xsk_socket_config xsk_cfg;
	int ret;

	xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
	xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	xsk_cfg.libbpf_flags = 0;
	xsk_cfg.xdp_flags = cfg->xdp_flags;
	xsk_cfg.bind_flags = cfg->xsk_bind_flags;
	xsk_cfg.libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD;
	ret = xsk_socket__create_shared(
		&tx_info->socket_info.xsk, cfg->redirect_ifname, 0, umem,
		&tx_info->socket_info.rxq, &tx_info->socket_info.txq,
		&tx_info->socket_info.umem_fq, &tx_info->socket_info.umem_cq,
		&xsk_cfg);

	printf("xsk_socket__create_shared_named_prog returns %d\n", ret);
	if (ret)
		goto error_exit;

	return tx_info;

error_exit:
	errno = -ret;
	return NULL;
}

static void show_fivetuple(struct fivetuple *f)
{
	if (k_verbose) {
		fprintf(stderr,
			"fivetuple saddr=%08x daddr=%08x sport=%04x dport=%04x protocol=%04x padding=%u\n",
			f->saddr, f->daddr, f->sport, f->dport, f->protocol,
			f->padding);
	}
}
static bool filter_pass_tcp(int accept_map_fd, __u32 saddr, __u32 daddr,
			    __u16 sport, __u16 dport)
{
	struct fivetuple f;
	enum xdp_action a = 0;
	f.saddr = htonl(saddr);
	f.daddr = htonl(daddr);
	f.sport = htons(sport);
	f.dport = htons(dport);
	f.protocol = IPPROTO_TCP;
	f.padding = 0;
	show_fivetuple(&f);
	int ret = bpf_map_lookup_elem(accept_map_fd, &f, &a);
	if (ret == 0) {
		if (k_verbose)
			fprintf(stderr, "Value %d found in map\n", a);
		return a == XDP_PASS;
	}
	a = XDP_PASS;
	if (k_verbose)
		fprintf(stderr, "No value in map, setting to %d\n", a);
	ret = bpf_map_update_elem(accept_map_fd, &f, &a, BPF_ANY);
	return true;
}
static bool filter_pass_udp(int accept_map_fd, __u32 saddr, __u32 daddr,
			    __u16 sport, __u16 dport)
{
	struct fivetuple f;
	enum xdp_action a = 0;
	f.saddr = htonl(saddr);
	f.daddr = htonl(daddr);
	f.sport = htons(sport);
	f.dport = htons(dport);
	f.protocol = IPPROTO_UDP;
	f.padding = 0;
	show_fivetuple(&f);
	int ret = bpf_map_lookup_elem(accept_map_fd, &f, &a);
	if (ret == 0) {
		if (k_verbose)
			fprintf(stderr, "Value %d found in map\n", a);
		return a == XDP_PASS;
	}
	a = XDP_PASS;
	if (k_verbose)
		fprintf(stderr, "No value in map, setting to %d\n", a);
	ret = bpf_map_update_elem(accept_map_fd, &f, &a, BPF_ANY);
	return true;
}
static bool filter_pass_icmp(int accept_map_fd, __u32 saddr, __u32 daddr,
			     int type, int code)
{
	struct fivetuple f;
	enum xdp_action a = 0;
	f.saddr = htonl(saddr);
	f.daddr = htonl(daddr);
	f.sport = 0;
	f.dport = 0;
	f.protocol = IPPROTO_ICMP;
	f.padding = 0;
	show_fivetuple(&f);
	int ret = bpf_map_lookup_elem(accept_map_fd, &f, &a);
	if (ret == 0) {
		if (k_verbose)
			fprintf(stderr, "Value %d found in map\n", a);
		return a == XDP_PASS;
	}
	a = XDP_PASS;
	if (k_verbose)
		fprintf(stderr, "No value in map, setting to %d\n", a);
	ret = bpf_map_update_elem(accept_map_fd, &f, &a, BPF_ANY);
	if (k_verbose)
		fprintf(stderr, "bpf_map_update_elem returns %d\n", ret);
	return true;
}
static bool process_packet(struct xsk_socket_info *xsk_src,
			   struct tx_socket_info *xsk_tx, uint64_t addr,
			   uint32_t len, struct socket_stats *stats, int tun_fd,
			   int accept_map_fd, struct xsk_umem_info *umem_info)
{
	uint8_t *pkt = xsk_umem__get_data(umem_info->buffer, addr);
	bool pass = false;
	uint32_t tx_idx = 0;

	struct ethhdr *eth = (struct ethhdr *)pkt;
	struct iphdr *ip = (struct iphdr *)(eth + 1);
	if (ntohs(eth->h_proto) == ETH_P_IP &&
	    len > (sizeof(*eth) + sizeof(*ip))) {
		__u8 protocol = ip->protocol;
		__u32 saddr = ntohl(ip->saddr);
		__u32 daddr = ntohl(ip->daddr);
		if (k_showpacket)
			hexdump(stderr, ip, (len < 32) ? len : 32);
		if (k_timestamp) {
			struct timeval tv;
			gettimeofday(&tv, NULL);
			double elapsed =
				(tv.tv_sec - stats->start_time.tv_sec) +
				1e-6 * (tv.tv_usec - stats->start_time.tv_usec);
			fprintf(stderr, "timestamp %15.6f\n", elapsed);
		}
		if (k_instrument)
			fprintf(stderr,
				"iphdr ihl=0x%01x version=0x%01x tos=0x%02x "
				"tot_len=0x%04x id=0x%04x flags=0x%02x frag_off=0x%04x ttl=0x%02x "
				"protocol=0x%02x check=0x%04x saddr=0x%08x daddr=0x%08x\n",
				ip->ihl, ip->version, ip->tos,
				ntohs(ip->tot_len), ntohs(ip->id),
				ntohs(ip->frag_off) >> 13,
				ntohs(ip->frag_off) & 0x1fff, ip->ttl,
				ip->protocol, ip->check, ntohl(ip->saddr),
				ntohl(ip->daddr));

		if (protocol == IPPROTO_TCP) {
			struct tcphdr *tcp = (struct tcphdr *)(ip + 1);
			__u32 sourceport = ntohs(tcp->source);
			__u32 destport = ntohs(tcp->dest);
			pass = filter_pass_tcp(accept_map_fd, saddr, daddr,
					       sourceport, destport);
		} else if (protocol == IPPROTO_UDP) {
			struct udphdr *tcp = (struct udphdr *)(ip + 1);
			__u32 sourceport = ntohs(tcp->source);
			__u32 destport = ntohs(tcp->dest);
			pass = filter_pass_udp(accept_map_fd, saddr, daddr,
					       sourceport, destport);
		} else if (protocol == IPPROTO_ICMP) {
			struct icmphdr *icmp = (struct icmphdr *)(ip + 1);
			int type = icmp->type;
			int code = icmp->code;
			pass = filter_pass_icmp(accept_map_fd, saddr, daddr,
						type, code);
		}
		if (pass) {
			stats->stats.filter_passes[protocol] += 1;
			uint8_t *write_addr = (uint8_t *)ip;
			size_t write_len = len - sizeof(struct ethhdr);
			if (tun_fd != -1) {
				ssize_t ret =
					write(tun_fd, write_addr, write_len);
				if (k_instrument) {
					hexdump(stderr, write_addr,
						(write_len < 32) ? write_len :
								   32);
					fprintf(stderr,
						"Write length %lu actual %ld\n",
						write_len, ret);
				}
				if (ret != write_len) {
					fprintf(stderr,
						"Error. %lu bytes requested, %ld bytes delivered, errno=%d %s\n",
						write_len, ret, errno,
						strerror(errno));
					exit(EXIT_FAILURE);
				}
			} else if (k_share_rxtx_umem) {
				uint64_t tx_addr = addr;
				struct ethhdr *tx_eth = eth;

				if (k_instrument) {
					hexdump(stderr, write_addr,
						(write_len < 32) ? write_len :
								   32);
					fprintf(stderr, "Write length %lu\n",
						write_len);
				}

				if (k_rewrite_mac_addresses) {
					/* Swap in the revised mac addresses */
					if (k_instrument) {
						fprintf(stderr,
							"Swapping source mac from ");
						show_mac(eth->h_source);
						fprintf(stderr, " to ");
						show_mac(xsk_tx->src_mac);
						fprintf(stderr,
							" and dst mac from ");
						show_mac(eth->h_dest);
						fprintf(stderr, " to ");
						show_mac(xsk_tx->dst_mac);
						fprintf(stderr, "\n");
					}
					memcpy(tx_eth->h_source,
					       xsk_tx->src_mac, ETH_ALEN);
					memcpy(tx_eth->h_dest, xsk_tx->dst_mac,
					       ETH_ALEN);
				}
				xsk_ring_prod__tx_desc(
					&(xsk_tx->socket_info.txq), tx_idx)
					->addr = tx_addr;
				xsk_ring_prod__tx_desc(
					&(xsk_tx->socket_info.txq), tx_idx)
					->len = len;
				xsk_ring_prod__submit(
					&(xsk_tx->socket_info.txq), 1);
				xsk_tx->socket_info.outstanding_tx++;

				stats->stats.tx_bytes += len;
				stats->stats.tx_packets += 1;
				return true; // Using the rx umem for transmission
			} else {
				/* Writing to output queue */
				/* Here we sent the packet out of the receive port. Note that
				 * we allocate one entry and schedule it. Your design would be
				 * faster if you do batch processing/transmission */
				{
					uint64_t tx_addr =
						umem_alloc_umem_frame(
							umem_info);
					uint8_t *tx_pkt = xsk_umem__get_data(
						umem_info->buffer, tx_addr);

					memcpy(tx_pkt, pkt,
					       len); // Copy the packet from the receive buffer to the transmit buffer
					struct ethhdr *tx_eth =
						(struct ethhdr *)tx_pkt;
					ssize_t ret = xsk_ring_prod__reserve(
						&(xsk_tx->socket_info.txq), 1,
						&tx_idx);
					if (k_instrument) {
						hexdump(stderr, write_addr,
							(write_len < 32) ?
								write_len :
								32);
						fprintf(stderr,
							"Write length %lu ret=%ld\n",
							write_len, ret);
					}
					if (ret != 1) {
						/* No more transmit slots, drop the packet */
						return false;
					}

					if (k_rewrite_mac_addresses) {
						/* Swap in the revised mac addresses */
						if (k_instrument) {
							fprintf(stderr,
								"Swapping source mac from ");
							show_mac(eth->h_source);
							fprintf(stderr, " to ");
							show_mac(
								xsk_tx->src_mac);
							fprintf(stderr,
								" and dst mac from ");
							show_mac(eth->h_dest);
							fprintf(stderr, " to ");
							show_mac(
								xsk_tx->dst_mac);
							fprintf(stderr, "\n");
						}
						memcpy(tx_eth->h_source,
						       xsk_tx->src_mac,
						       ETH_ALEN);
						memcpy(tx_eth->h_dest,
						       xsk_tx->dst_mac,
						       ETH_ALEN);
					}

					xsk_ring_prod__tx_desc(
						&(xsk_tx->socket_info.txq),
						tx_idx)
						->addr = tx_addr;
					xsk_ring_prod__tx_desc(
						&(xsk_tx->socket_info.txq),
						tx_idx)
						->len = len;
					xsk_ring_prod__submit(
						&(xsk_tx->socket_info.txq), 1);
					xsk_tx->socket_info.outstanding_tx++;

					stats->stats.tx_bytes += len;
					stats->stats.tx_packets += 1;
					return false; // Not using the rx umem for transmission
				}
			}

		} else {
			stats->stats.filter_drops[protocol] += 1;
		}
	}
	return false; // Not transmitting anything
}

static void complete_tx(struct tx_socket_info *xsk_tx,
			struct xsk_umem_info *umem)
{
	unsigned int completed;
	uint32_t idx_cq;
	if (k_instrument) {
		fprintf(stderr, "complete_tx entry, outstanding_tx=%d\n",
			xsk_tx->socket_info.outstanding_tx);
	}
	if (!xsk_tx->socket_info.outstanding_tx)
		return;

	sendto(xsk_socket__fd(xsk_tx->socket_info.xsk), NULL, 0, MSG_DONTWAIT,
	       NULL, 0);

	/* Collect/free completed TX buffers */
	completed = xsk_ring_cons__peek(&xsk_tx->socket_info.umem_cq,
					XSK_RING_CONS__DEFAULT_NUM_DESCS,
					&idx_cq);

	if (k_instrument) {
		fprintf(stderr, "complete_tx completed=%u\n", completed);
	}

	if (completed > 0) {
		for (int i = 0; i < completed; i++) {
			if (k_instrument) {
				fprintf(stderr,
					"calling umem_free_umem_frame i=%d\n",
					i);
			}
			umem_free_umem_frame(
				umem, *xsk_ring_cons__comp_addr(
					      &xsk_tx->socket_info.umem_cq,
					      idx_cq++));
		}

		xsk_ring_cons__release(&xsk_tx->socket_info.umem_cq, completed);
		xsk_tx->socket_info.outstanding_tx -=
			completed < xsk_tx->socket_info.outstanding_tx ?
				completed :
				xsk_tx->socket_info.outstanding_tx;
	}
	if (k_instrument) {
		fprintf(stderr, "complete_tx exit, outstanding_tx=%d\n",
			xsk_tx->socket_info.outstanding_tx);
	}
}

static void handle_receive_packets(struct xsk_socket_info *xsk_src,
				   struct tx_socket_info *xsk_tx,
				   struct socket_stats *stats, int tun_fd,
				   int accept_map_fd,
				   struct xsk_umem_info *umem_info)
{
	unsigned int rcvd, stock_frames, i;
	uint32_t idx_rx = 0, idx_fq = 0;
	int ret;

	rcvd = xsk_ring_cons__peek(&xsk_src->rxq, RX_BATCH_SIZE, &idx_rx);
	if (!rcvd)
		return;

	/* Stuff the ring with as much frames as possible */
	stock_frames = xsk_prod_nb_free(&xsk_src->umem_fq,
					xsk_umem_free_frames(umem_info));

	if (stock_frames > 0) {
		ret = xsk_ring_prod__reserve(&xsk_src->umem_fq, stock_frames,
					     &idx_fq);

		/* This should not happen, but just in case */
		while (ret != stock_frames)
			ret = xsk_ring_prod__reserve(&xsk_src->umem_fq, rcvd,
						     &idx_fq);

		for (i = 0; i < stock_frames; i++)
			*xsk_ring_prod__fill_addr(&xsk_src->umem_fq, idx_fq++) =
				umem_alloc_umem_frame(umem_info);

		xsk_ring_prod__submit(&xsk_src->umem_fq, stock_frames);
	}

	/* Process received packets */
	for (i = 0; i < rcvd; i++) {
		uint64_t addr =
			xsk_ring_cons__rx_desc(&xsk_src->rxq, idx_rx)->addr;
		uint32_t len =
			xsk_ring_cons__rx_desc(&xsk_src->rxq, idx_rx++)->len;

		bool transmitted = process_packet(xsk_src, xsk_tx, addr, len,
						  stats, tun_fd, accept_map_fd,
						  umem_info);

		if (k_instrument)
			printf("addr=0x%lx len=%u transmitted=%u\n", addr, len,
			       transmitted);
		if (!transmitted) {
			printf("addr=0x%lx len=%u transmitted=%u calling umem_free_umem_frame\n",
			       addr, len, transmitted);
			umem_free_umem_frame(umem_info, addr);
		}

		stats->stats.rx_bytes += len;
		stats->stats.rx_packets += 1;
	}

	stats->stats.rx_batch_count += 1;
	xsk_ring_cons__release(&xsk_src->rxq, rcvd);
	/* Do we need to wake up the kernel for transmission */
	complete_tx(xsk_tx, umem_info);
}

static void rx_and_process(struct config *cfg,
			   struct all_socket_info *all_socket_info,
			   struct tx_socket_info *tx_socket_info,
			   struct socket_stats *stats, int tun_fd,
			   int accept_map_fd, struct xsk_umem_info *umem_info)
{
	struct pollfd fds[k_rx_queue_count_max];
	int ret, nfds = cfg->xsk_if_queue;

	memset(fds, 0, sizeof(fds));
	for (int q = 0; q < nfds; q += 1) {
		fds[q].fd = xsk_socket__fd(
			all_socket_info->xsk_socket_info[q]->xsk);
		fds[q].events = POLLIN;
	}

	while (!global_exit) {
		ret = poll(fds, nfds, -1);
		if (k_instrument)
			fprintf(stderr, "rx_and_process poll returns %d\n",
				ret);
		if (ret <= 0 || ret > nfds)
			continue;
		for (int q = 0; q < nfds; q += 1) {
			if (fds[q].revents & POLLIN) {
				if (k_instrument)
					fprintf(stderr, "rx_and_process q=%d\n",
						q);
				handle_receive_packets(
					all_socket_info->xsk_socket_info[q],
					tx_socket_info, stats, tun_fd,
					accept_map_fd, umem_info);
			}
		}
	}
}

#define NANOSEC_PER_SEC 1000000000 /* 10^9 */
static uint64_t gettime(void)
{
	struct timespec t;
	int res;

	res = clock_gettime(CLOCK_MONOTONIC, &t);
	if (res < 0) {
		fprintf(stderr, "Error with gettimeofday! (%i)\n", res);
		exit(EXIT_FAIL);
	}
	return (uint64_t)t.tv_sec * NANOSEC_PER_SEC + t.tv_nsec;
}

static double calc_period(struct stats_record *r, struct stats_record *p)
{
	double period_ = 0;
	__u64 period = 0;

	period = r->timestamp - p->timestamp;
	if (period > 0)
		period_ = ((double)period / NANOSEC_PER_SEC);

	return period_;
}

static void stats_print(struct stats_record *stats_rec,
			struct stats_record *stats_prev)
{
	uint64_t packets, bytes;
	double period;
	double pps; /* packets per sec */
	double bps; /* bits per sec */

	char *fmt = "%-12s %'11lld pkts (%'10.0f pps)"
		    " %'11lld Kbytes (%'6.0f Mbits/s)"
		    " %lu dups %lu out of seqs %lu batches"
		    " period:%f\n";

	period = calc_period(stats_rec, stats_prev);
	if (period == 0)
		period = 1;

	packets = stats_rec->rx_packets - stats_prev->rx_packets;
	pps = packets / period;

	bytes = stats_rec->rx_bytes - stats_prev->rx_bytes;
	bps = (bytes * 8) / period / 1000000;

	printf(fmt, "AF_XDP RX:", stats_rec->rx_packets, pps,
	       stats_rec->rx_bytes / 1000, bps, stats_rec->rx_duplicate,
	       stats_rec->rx_outofsequence, stats_rec->rx_batch_count, period);

	for (int proto = 0; proto < 256; proto += 1) {
		uint64_t passes = stats_rec->filter_passes[proto];
		uint64_t drops = stats_rec->filter_drops[proto];
		if (passes + drops > 0) {
			printf("passes[%d]=%lu drops[%d]=%lu total[%d]=%lu\n",
			       proto, passes, proto, drops, proto,
			       passes + drops);
		}
	}
	printf("\n");
}

static void *stats_poll(void *arg)
{
	unsigned int interval = 2;
	struct socket_stats *stats = arg;
	static struct stats_record previous_stats = { 0 };

	previous_stats.timestamp = gettime();

	/* Trick to pretty printf with thousands separators use %' */
	setlocale(LC_NUMERIC, "en_US");

	while (!global_exit) {
		sleep(interval);
		stats->stats.timestamp = gettime();
		stats_print(&(stats->stats), &previous_stats);
		stats->prev_stats = stats->stats;
	}
	return NULL;
}

enum { k_buffersize = 4096 };
static void *tun_read(void *arg)
{
	int *tun_fd_p = arg;
	int tun_fd = *tun_fd_p;
	char buffer[k_buffersize];
	fprintf(stderr, "tun_read thread running\n");
	while (!global_exit) {
		ssize_t count = read(tun_fd, buffer, k_buffersize);
		if (count < 0) {
			int err = errno;
			fprintf(stderr, "ERROR:tun_read gives errno=%d %s\n",
				err, strerror(err));
			exit(EXIT_FAILURE);
		} else if (count == 0) {
			fprintf(stderr,
				"tun_read unexpected zero length read\n");
		} else {
			fprintf(stderr, "tun_read\n");
			hexdump(stderr, buffer, count);
		}
	}
	return NULL;
}
static void exit_application(int sig)
{
	global_exit = true;
}

int tun_alloc(char *dev)
{
	struct ifreq ifr;
	int fd, err;

	if ((fd = open("/dev/net/tun", O_RDWR)) < 0)
		return fd;

	memset(&ifr, 0, sizeof(ifr));

	/* Flags: IFF_TUN   - TUN device (no Ethernet headers)
       *        IFF_TAP   - TAP device
       *
       *        IFF_NO_PI - Do not provide packet information
       */
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	if (*dev)
		strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);

	if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
		close(fd);
		return err;
	}
	strcpy(dev, ifr.ifr_name);
	return fd;
}

static int open_bpf_map_file(const char *pin_dir, const char *mapname,
			     struct bpf_map_info *info)
{
	char filename[PATH_MAX];
	int err, len, fd;
	__u32 info_len = sizeof(*info);

	len = snprintf(filename, PATH_MAX, "%s/%s", pin_dir, mapname);
	if (len < 0) {
		fprintf(stderr, "ERR: constructing full mapname path\n");
		return -1;
	}

	fd = bpf_obj_get(filename);
	if (fd < 0) {
		fprintf(stderr,
			"WARN: Failed to open bpf map file:%s err(%d):%s\n",
			filename, errno, strerror(errno));
		return fd;
	}

	if (info) {
		err = bpf_obj_get_info_by_fd(fd, info, &info_len);
		if (err) {
			fprintf(stderr, "ERR: %s() can't get info - %s\n",
				__func__, strerror(errno));
			return EXIT_FAIL_BPF;
		}
	}

	return fd;
}

static void set_mac(unsigned char macaddr[ETH_ALEN], const char *env)
{
	memset(macaddr, 0, ETH_ALEN);
	if (env != NULL) {
		unsigned int mac0, mac1, mac2, mac3, mac4, mac5;
		sscanf(env, "%02x:%02x:%02x:%02x:%02x:%02x", &mac0, &mac1,
		       &mac2, &mac3, &mac4, &mac5);
		macaddr[0] = mac0;
		macaddr[1] = mac1;
		macaddr[2] = mac2;
		macaddr[3] = mac3;
		macaddr[4] = mac4;
		macaddr[5] = mac5;
		if (k_instrument) {
			fprintf(stderr, "mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
				macaddr[0], macaddr[1], macaddr[2], macaddr[3],
				macaddr[4], macaddr[5]);
		}
	}
}

const char *pin_dir = "/sys/fs/bpf";
const char *map_name = "accept_map";

int main(int argc, char **argv)
{
	int ret;
	struct rlimit rlim = { RLIM_INFINITY, RLIM_INFINITY };
	struct config cfg = { .ifindex = -1,
			      .redirect_ifindex = -1,
			      .xsk_if_queue = 1,
			      .do_unload = false,
			      .filename = "",
			      .progsec = "xdp_sock_0",
			      .redirect_ifname_pid = -1 };
	struct all_socket_info *all_socket_info;
	struct xdp_program *xdp_prog;
	struct bpf_object *bpf_object = NULL;
	int err;
	pthread_t stats_poll_thread;
	pthread_t tun_read_thread;
	struct socket_stats stats;
	int tun_fd;
	char tun_name[IFNAMSIZ];
	int xsks_map_fd;

	int accept_map_fd;
	struct tx_socket_info *tx_socket_info;

	memset(&stats, 0, sizeof(stats));

	/* Global shutdown handler */
	signal(SIGINT, exit_application);

	/* Cmdline options can change progsec */
	parse_cmdline_args(argc, argv, long_options, &cfg, __doc__);

	/* Required option */
	if (cfg.ifindex == -1) {
		fprintf(stderr, "ERROR: Required option --dev missing\n\n");
		usage(argv[0], __doc__, long_options, (argc == 1));
		return EXIT_FAIL_OPTION;
	}

	struct bpf_map *xsks_map;
	/* Load custom program if configured */
	fprintf(stderr, "main cfg.filename=%s\n", cfg.filename);
	if (cfg.filename[0] == 0) {
		fprintf(stderr, "main No program file\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "main Opening program file %s\n", cfg.filename);
	xdp_prog = xdp_program__open_file(cfg.filename, NULL, NULL);
	fprintf(stderr, "main xdp_prog=%p\n", xdp_prog);
	if (xdp_prog == NULL) {
		fprintf(stderr, "ERROR:xdp_program__open_file returns NULL\n");
		exit(EXIT_FAILURE);
	}
	bpf_object = xdp_program__bpf_obj(xdp_prog);
	fprintf(stderr, "main bpf_object=%p\n", bpf_object);
	if (bpf_object == NULL) {
		fprintf(stderr, "ERROR:xdp_program__bpf_obj returns NULL\n");
		exit(EXIT_FAILURE);
	}
	xsks_map = bpf_object__find_map_by_name(bpf_object, "xsks_map");
	if (xsks_map == NULL) {
		fprintf(stderr,
			"ERROR:bpf_object__find_map_by_name returns NULL\n");
		exit(EXIT_FAILURE);
	}
	err = bpf_map__set_max_entries(xsks_map, cfg.xsk_if_queue);
	if (err != 0) {
		fprintf(stderr,
			"ERROR:bpf_map__set_max_entries returns %d %s\n", err,
			strerror(err));
		exit(EXIT_FAILURE);
	}
	/* Allow unlimited locking of memory, so all memory needed for packet
	 * buffers can be locked.
	 */
	if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
		fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	err = xdp_program__attach(xdp_prog, cfg.ifindex, XDP_MODE_SKB, 0);
	if (err) {
		fprintf(stderr, "ERROR:xdp_program__attach returns %d\n", err);
		exit(EXIT_FAILURE);
	}
	/* Open and configure the AF_XDP (xsk) socket */
	xsks_map_fd = bpf_map__fd(xsks_map);
	if (xsks_map_fd < 0) {
		fprintf(stderr, "ERROR:bpf_map__fd returns %d %s\n",
			xsks_map_fd, strerror(-xsks_map_fd));
		exit(EXIT_FAILURE);
	}
	/* Set up receive sockets */
	assert(cfg.xsk_if_queue <= IF_QUEUE_MAX) ;
	int packet_buffer_size =
		NUM_FRAMES * FRAME_SIZE * 2 * (cfg.xsk_if_queue + 1);
	void *packet_buffer;
	if (posix_memalign(&packet_buffer,
			   getpagesize(), /* PAGE_SIZE aligned */
			   packet_buffer_size)) {
		fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}
	struct xsk_umem_info umem_info;
	configure_xsk_umem(&umem_info, packet_buffer, packet_buffer_size,
			   &(umem_info.umem_fq), &(umem_info.umem_cq), NUM_FRAMES * 2 * (cfg.xsk_if_queue + 1));

	all_socket_info =
		xsk_configure_socket_all(&cfg, xsks_map_fd, &umem_info);
	if (all_socket_info == NULL) {
		fprintf(stderr, "ERROR: Can't setup AF_XDP sockets \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Start thread to do statistics display */
	if (verbose && 0 == k_instrument) {
		ret = pthread_create(&stats_poll_thread, NULL, stats_poll,
				     &stats);
		if (ret) {
			fprintf(stderr,
				"ERROR: Failed creating statistics thread "
				"\"%s\"\n",
				strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (cfg.redirect_ifname_pid == -1) {
		/* First frames to be handled by TUN */
		/* Start TUN */
		strcpy(tun_name, "tun0");
		tun_fd = tun_alloc(tun_name);
		if (tun_fd < 0) {
			err = errno;
			fprintf(stderr, "ERROR:tun_alloc gives errno=%d %s\n",
				err, strerror(err));
			exit(EXIT_FAILURE);
		}
		fprintf(stderr, "tun_fd=%d\n", tun_fd);

		if (k_receive_tuntap) {
			// Start thread to read from the tun
			ret = pthread_create(&tun_read_thread, NULL, tun_read,
					     &tun_fd);
			if (ret) {
				fprintf(stderr,
					"ERROR: Failed creating tun_read thread "
					"\"%s\"\n",
					strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
		tx_socket_info = NULL;
	} else {
		/* First frames to be handled by AF_XDP send */
		tun_fd = -1;
		/* A pid of 1 would mean 'init', where the setns would be to the root namespace */
		/* Interpret this as a dummy */
		if (cfg.redirect_ifname_pid != 1) {
			int setns_fd = syscall(SYS_pidfd_open,
					       cfg.redirect_ifname_pid, 0);
			if (setns_fd == -1) {
				fprintf(stderr,
					"ERROR: Failed calling pidfd_open) "
					"\"%s\"\n",
					strerror(errno));
				exit(EXIT_FAILURE);
			}
			fprintf(stderr, "setns_fd=%d\n", setns_fd);
			err = setns(setns_fd, CLONE_NEWNET);
			if (err == -1) {
				fprintf(stderr,
					"ERROR: Failed calling setns) "
					"\"%s\"\n",
					strerror(errno));
				exit(EXIT_FAILURE);
			}
			fprintf(stderr, "setns returns %d\n", err);
			if (k_diagnose_setns) {
				int rc;
				fprintf(stderr, "pid=%d about to sleep(3600)\n",
					getpid());
				errno = 0;
				rc = sleep(3600);
				fprintf(stderr, "sleep returns %d, errno=%d\n",
					rc, errno);
				fprintf(stderr, "About to call /bin/bash\n");
				errno = 0;
				rc = system("/bin/bash");
				fprintf(stderr, "bash returns %d, errno=%d\n",
					rc, errno);
			}
		}
		tx_socket_info = xsk_configure_socket_tx(&cfg, umem_info.umem);
		if (tx_socket_info == NULL) {
			fprintf(stderr,
				"ERROR: Failed calling xsk_configure_socket_tx "
				"\"%s\"\n",
				strerror(errno));
			exit(EXIT_FAILURE);
		}
		set_mac(tx_socket_info->src_mac, getenv("SRC_MAC"));
		set_mac(tx_socket_info->dst_mac, getenv("DST_MAC"));
	}

	accept_map_fd = open_bpf_map_file(pin_basedir, "accept_map", &info);
	if (accept_map_fd < 0) {
		exit(EXIT_FAILURE);
	}

	/* check map info, e.g. datarec is expected size */
	err = check_map_fd_info(&info, &map_expect);
	if (err) {
		fprintf(stderr, "ERR: map via FD not compatible\n");
		close(accept_map_fd);
		exit(EXIT_FAILURE);
	}
	/* Receive and count packets than drop them */
	gettimeofday(&(stats.start_time), NULL);
	rx_and_process(&cfg, all_socket_info, tx_socket_info, &stats, tun_fd,
		       accept_map_fd, &umem_info);

	/* Cleanup */
	if (tun_fd != -1)
		close(tun_fd);
	for (int q = 0; q < cfg.xsk_if_queue; q += 1) {
		xsk_socket__delete(all_socket_info->xsk_socket_info[q]->xsk);
	}
	xdp_program__detach(xdp_prog, cfg.ifindex, XDP_MODE_SKB, 0);
	xdp_program__close(xdp_prog);

	return EXIT_OK;
}
