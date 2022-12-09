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

#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <sched.h>

#include "common_params.h"

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define RX_BATCH_SIZE 64
#define INVALID_UMEM_FRAME UINT64_MAX

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *pin_basedir = "/sys/fs/bpf";

enum {
	k_instrument = true,
	k_instrument_detail = false,
	k_receive_tuntap = false,
	k_verify_umem = false,
	k_verbose = true,
	k_skipping = false,
	k_timestamp = true,
	k_showpacket = true,
	k_diagnose_setns = false
};

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

static bool global_exit;
static void exit_application(int sig)
{
	global_exit = true;
}
int main(int argc, char **argv)
{
	struct rlimit rlim = { RLIM_INFINITY, RLIM_INFINITY };
	struct config cfg = {
		.ifindex = -1,
		.redirect_ifindex = -1,
		.xsk_if_queue = 1,
		.do_unload = false,
		.filename = "",
		.progsec = "xdp_sock_0",
		.redirect_ifname_pid = -1
	};
	struct xdp_program *xdp_prog;
	struct bpf_object *bpf_object = NULL;
	int err;

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

//	struct bpf_map *xsks_map;
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
//	xsks_map = bpf_object__find_map_by_name(bpf_object, "xsks_map");
//	if (xsks_map == NULL) {
//		fprintf(stderr,
//			"ERROR:bpf_object__find_map_by_name returns NULL\n");
//		exit(EXIT_FAILURE);
//	}
//	err = bpf_map__set_max_entries(xsks_map, cfg.xsk_if_queue);
//	if (err != 0) {
//		fprintf(stderr,
//			"ERROR:bpf_map__set_max_entries returns %d %s\n", err,
//			strerror(err));
//		exit(EXIT_FAILURE);
//	}
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

	while ( ! global_exit )
		sleep(3600) ;

	xdp_program__detach(xdp_prog, cfg.ifindex, XDP_MODE_SKB, 0);
	xdp_program__close(xdp_prog);

	return EXIT_OK;
}
