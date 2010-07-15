#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include <linux/lisp.h>

#include "utils.h"

struct map {
  __be32	eid;
  __be32	rloc;
  int		prio;
  int		weight;
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
  struct nlattr *attrs[LISP_GNL_ATTR_MAX+1];

  // Validate message and parse attributes
  //genlmsg_parse(nlh, 0, attrs, LISP_MAP_A_MAX, policy);

  /*
  if (attrs[ATTR_FOO]) {
    uint32_t value = nla_get_u32(attrs[ATTR_FOO]);
    ...
      }
  */
  return 0;
}


int map_add_genl(int cmd, struct map *m)
{
  struct nl_handle *sock;
  struct nl_msg *msg;
  int family;

  printf("sending eid: %x rloc: %x prio: %d wei: %d\n", m->eid, m->rloc, m->prio, m->weight);

  sock = nl_handle_alloc();
  genl_connect(sock);

  family = genl_ctrl_resolve(sock, LISP_GNL_NAME);

  msg = nlmsg_alloc();
  genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO,
	      cmd, 1);

  nla_put_u32(msg, LISP_GNL_ATTR_EID, m->eid);
  nla_put_u32(msg, LISP_GNL_ATTR_RLOC, m->rloc);
  nla_put_u8(msg, LISP_GNL_ATTR_PRIO, m->prio);
  nla_put_u8(msg, LISP_GNL_ATTR_WEIGHT, m->weight);

  nl_send_auto_complete(sock, msg);

  nlmsg_free(msg);

  // Prepare socket to receive the answer by specifying the callback
  // function to be called for valid messages.
  nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, NULL);

  // Wait for the answer and receive it
  nl_recvmsgs_default(sock);

  return 0;
}


static int parse_args(int argc, char **argv, int cmd, struct map *m)
{
	int count = 0;
	int isatap = 0;

	memset(m, 0, sizeof(*m));

	while (argc > 0) {
		if (strcmp(*argv, "eid") == 0) {
			NEXT_ARG();
			m->eid = get_addr32(*argv);
		} else if (strcmp(*argv, "rloc") == 0) {
			NEXT_ARG();
			m->rloc = get_addr32(*argv);
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

	return 0;
}

static int do_show(int argc, char **argv)
{
	return -1;
}


static int do_add(int cmd, int argc, char **argv)
{
	struct map m;

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
		/*		if (matches(*argv, "change") == 0)
			return do_add(SIOCCHGTUNNEL, argc-1, argv+1);
		if (matches(*argv, "del") == 0)
			return do_del(argc-1, argv+1);
		if (matches(*argv, "show") == 0 ||
		    matches(*argv, "lst") == 0 ||
		    matches(*argv, "list") == 0)
			return do_show(argc-1, argv+1);
		*/
		if (matches(*argv, "help") == 0)
			usage();
	} else
		return do_show(0, NULL);

	fprintf(stderr, "Command \"%s\" is unknown, try \"ip map help\".\n", *argv);
	exit(-1);
}
