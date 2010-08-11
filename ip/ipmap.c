#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/lisp.h>
#include <errno.h>

#include "utils.h"

#define PREFLEN 19
#define ADDRLEN 16

struct map {
	inet_prefix	eid;
	inet_prefix	rloc;
	int		prio;
	int		weight;
	unsigned char	flags;
};

static void usage(void) __attribute__((noreturn));

static void usage(void)
{
	fprintf(stderr, "Usage: ip map { add | change | del | show }\n");
	fprintf(stderr, "          [ eid ADDR ] [ rloc ADDR ]\n");
	fprintf(stderr, "          [ prio PRIO ] [ weight WEIGHT ]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Where: ADDR   := { IP_ADDRESS }\n");
	fprintf(stderr, "       PRIO   := { 0..255 }\n");
	fprintf(stderr, "       WEIGHT := { 0..255 }\n");
	exit(-1);
}

static int parse_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);

	printf("response type: %d\n", nlh->nlmsg_type);
	return 0;
}

static int parse_show(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[LISP_GNL_ATTR_MAX+1], *att;
	int err, cnt;
	char eid[PREFLEN + 4];
	char saddr[ADDRLEN];

	struct in_addr addr;

	err = genlmsg_parse(nlh, 0, attrs, LISP_GNL_ATTR_MAX, lisp_gnl_policy);

	nla_for_each_nested(att, attrs[LISP_GNL_ATTR_MAP], cnt) {
		switch(nla_type(att)) {
		case LISP_GNL_ATTR_MAP_EID:
			addr.s_addr = nla_get_u32(att);
			format_host(AF_INET, 0, &addr, saddr, 16);
			break;
		case LISP_GNL_ATTR_MAP_EIDLEN:
			sprintf(eid, "eid %s/%d", saddr, nla_get_u8(att));
			break;
		case LISP_GNL_ATTR_MAP_RLOC:
			addr.s_addr = nla_get_u32(att);
			printf("%s via rloc %s\n", eid, format_host(AF_INET, 0, &addr, saddr, 16));
			break;
		}
	}

	return NL_OK;
}

static int add_error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			     void *arg)
{
	switch(err->error){
	case -EEXIST:
		printf("EID prefix already exists.\n");
		exit(1);
	}
	return 0;
}


int map_add_genl(int cmd, struct map *m)
{
	struct nl_handle *sock;
	struct nl_msg *msg;
	int family;
	struct nlattr *at;
	struct nl_cb *cb;

	printf("Adding map: %x/%d via %x\n", m->eid.data[0], m->eid.bitlen, m->rloc.data[0]);

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	nl_cb_err(cb, NL_CB_CUSTOM, add_error_handler, NULL);

	sock = nl_handle_alloc();
	genl_connect(sock);

	family = genl_ctrl_resolve(sock, LISP_GNL_NAME);

	msg = nlmsg_alloc();

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0,
		    NLM_F_REQUEST | NLM_F_ACK, cmd, 1);
	nla_put_u8(msg, LISP_GNL_ATTR_MAPF, m->flags);

	at = nla_nest_start(msg, LISP_GNL_ATTR_MAP);

	nla_put_u32(msg, LISP_GNL_ATTR_MAP_EID, m->eid.data[0]);
	nla_put_u32(msg, LISP_GNL_ATTR_MAP_EIDLEN, m->eid.bitlen);
	nla_put_u32(msg, LISP_GNL_ATTR_MAP_RLOC, m->rloc.data[0]);
	nla_put_u8(msg, LISP_GNL_ATTR_MAP_PRIO, m->prio);
	nla_put_u8(msg, LISP_GNL_ATTR_MAP_WEIGHT, m->weight);

	nla_nest_end(msg, at);

	nl_send_auto_complete(sock, msg);
	nlmsg_free(msg);

	return nl_recvmsgs(sock, cb);
}

int map_show_genl(int cmd)
{
	struct nl_handle *sock;
	struct nl_msg *msg;
	int family;
	struct nlattr *at;
	struct nl_cb *cb;

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, parse_show, NULL);
	nl_cb_err(cb, NL_CB_CUSTOM, add_error_handler, NULL);

	sock = nl_handle_alloc();
	genl_connect(sock);

	family = genl_ctrl_resolve(sock, LISP_GNL_NAME);

	msg = nlmsg_alloc();
	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0,
		    NLM_F_DUMP, cmd, 1);

	nl_send_auto_complete(sock, msg);
	nlmsg_free(msg);

	return nl_recvmsgs(sock, cb);
}


static int parse_args(int argc, char **argv, int cmd, struct map *m)
{
	int count = 0;
	int isatap = 0;

	memset(m, 0, sizeof(*m));

	while (argc > 0) {
		if (strcmp(*argv, "eid") == 0) {
			NEXT_ARG();
			get_prefix(&m->eid, *argv, preferred_family);
		} else if (strcmp(*argv, "rloc") == 0) {
			NEXT_ARG();
			get_addr(&m->rloc, *argv, preferred_family);
		} else if (strcmp(*argv, "prio") == 0) {
			unsigned uval;
			NEXT_ARG();
			if (get_unsigned(&uval, *argv, 0))
				invarg("invalid PRIO\n", *argv);
			if (uval > 255)
				invarg("TTL must be <=255\n", *argv);
			m->prio = uval;
		} else if (strcmp(*argv, "weight") == 0) {
			unsigned uval;
			NEXT_ARG();
			if (get_unsigned(&uval, *argv, 0))
				invarg("invalid WEIGHT\n", *argv);
			if (uval > 255)
				invarg("TTL must be <=255\n", *argv);
			m->weight = uval;
		} else {
			if (matches(*argv, "help") == 0)
				usage();
		}
		count++;
		argc--; argv++;
	}

	if (m->eid.data[0] == 0) {
		fprintf(stderr, "EID prefix not specified.\n");
		exit(1);
	}

	if (m->rloc.data[0] == 0) {
		fprintf(stderr, "RLOC not specified.\n");
		exit(1);
	}

	return 0;
}

static int do_show(int cmd, int argc, char **argv)
{
	return map_show_genl(cmd);
}

static int do_add(int cmd, int argc, char **argv)
{
	struct map m;

	m.prio = 0;
	m.weight = 0;
	m.flags = LISP_MAP_F_LOCAL;

	if (parse_args(argc, argv, cmd, &m) < 0)
		return -1;

	return map_add_genl(cmd, &m);
}

int do_map(int argc, char **argv)
{
	switch (preferred_family) {
	case AF_UNSPEC:
		preferred_family = AF_INET;
		break;
	case AF_INET:
		break;
	default:
		fprintf(stderr, "Unsupported family:%d\n", preferred_family);
		exit(-1);
	}

	if (argc > 0) {
		if (matches(*argv, "add") == 0)
		  return do_add(LISP_GNL_CMD_ADDMAP, argc-1, argv+1);
		/* TODO: del */
		if (matches(*argv, "show") == 0 ||
		    matches(*argv, "lst") == 0 ||
		    matches(*argv, "list") == 0)
		  return do_show(LISP_GNL_CMD_SHOWMAP, argc-1, argv+1);

		if (matches(*argv, "help") == 0)
			usage();
	} else
		return do_show(LISP_GNL_CMD_SHOWMAP, 0, NULL);

	fprintf(stderr, "Command \"%s\" is unknown, try \"ip map help\".\n", *argv);
	exit(-1);
}
