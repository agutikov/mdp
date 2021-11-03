#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "net.h"



#define ERRORF(x...) fprintf(stderr, x)

#define FATAL(x...)                                                            \
	do {                                                                   \
		ERRORF("[-] PROGRAM ABORT : " x);                              \
		ERRORF("\n\tLocation : %s(), %s:%u\n\n", __FUNCTION__,         \
		       __FILE__, __LINE__);                                    \
		exit(EXIT_FAILURE);                                            \
	} while (0)

#define PFATAL(x...)                                                           \
	do {                                                                   \
		ERRORF("[-] SYSTEM ERROR : " x);                               \
		ERRORF("\n\tLocation : %s(), %s:%u\n", __FUNCTION__, __FILE__, \
		       __LINE__);                                              \
		perror("      OS message ");                                   \
		ERRORF("\n");                                                  \
		exit(EXIT_FAILURE);                                            \
	} while (0)

#define TIMESPEC_NSEC(ts) ((ts)->tv_sec * 1000000000ULL + (ts)->tv_nsec)
#define TIMEVAL_NSEC(ts)                                                       \
	((ts)->tv_sec * 1000000000ULL + (ts)->tv_usec * 1000ULL)
#define NSEC_TIMESPEC(ns)                                                      \
	(struct timespec) { (ns) / 1000000000ULL, (ns) % 1000000000ULL }
#define NSEC_TIMEVAL(ns)                                                       \
	(struct timeval)                                                       \
	{                                                                      \
		(ns) / 1000000000ULL, ((ns) % 1000000000ULL) / 1000ULL         \
	}
#define MSEC_NSEC(ms) ((ms)*1000000ULL)



const char *str_quote(const char *s)
{
	static char buf[1024];
	int r = snprintf(buf, sizeof(buf), "\"%.*s\"", (int)sizeof(buf) - 4, s);
	if (r >= (int)sizeof(buf)) {
		buf[sizeof(buf) - 1] = 0;
	}
	return buf;
}


void net_set_buffer_size(int cd, int max, int send)
{
	int i, flag;

	if (send) {
		flag = SO_SNDBUF;
	} else {
		flag = SO_RCVBUF;
	}

	for (i = 0; i < 10; i++) {
		int bef = 0;
		socklen_t size = sizeof(bef);
		if (getsockopt(cd, SOL_SOCKET, flag, &bef, &size) < 0) {
			PFATAL("getsockopt(SOL_SOCKET)");
			break;
		}
		if (bef >= max) {
			break;
		}

		bef *= 2;
		if (setsockopt(cd, SOL_SOCKET, flag, &bef, sizeof(size)) < 0) {
			// don't log error, just break
			break;
		}
	}
}

void parse_addr(net_addr *netaddr, const char *addr, int port) {
	const char *colon = addr + strlen(addr);
	if (port == 0) {
		colon = strrchr(addr, ':');
		if (colon == NULL) {
			FATAL("You forgot to specify port");
		}
		port = atoi(colon+1);
		if (port < 0 || port > 65535) {
			FATAL("Invalid port number %d", port);
		}
	}
	char host[255];
	int addr_len = colon-addr > 254 ? 254 : colon-addr;
	strncpy(host, addr, addr_len);
	host[addr_len] = '\0';
	net_gethostbyname(netaddr, host, port);
}

void net_gethostbyname(net_addr *saddr, const char *host, int port)
{
	memset(saddr, 0, sizeof(*saddr));

	struct in_addr in_addr;
	struct in6_addr in6_addr;

	/* Try ipv4 address first */
	if (inet_pton(AF_INET, host, &in_addr) == 1) {
		goto got_ipv4;
	}

	/* Then ipv6 */
	if (inet_pton(AF_INET6, host, &in6_addr) == 1) {
		goto got_ipv6;
	}

	FATAL("inet_pton(%s)", str_quote(host));
	return;

got_ipv4:
	saddr->ipver = 4;
	saddr->sin.sin4.sin_family = AF_INET;
	saddr->sin.sin4.sin_port = htons(port);
	saddr->sin.sin4.sin_addr = in_addr;
	return;

got_ipv6:
	saddr->ipver = 6;
	saddr->sin.sin6.sin6_family = AF_INET6;
	saddr->sin.sin6.sin6_port = htons(port);
	saddr->sin.sin6.sin6_addr = in6_addr;
	return;
}

const char *addr_to_str(const net_addr *addr) {
	char dst[INET6_ADDRSTRLEN + 1];
	int port = 0;

	switch (addr->ipver) {
	case 4: {
		inet_ntop(AF_INET, &addr->sin.sin4.sin_addr, dst, INET6_ADDRSTRLEN);
		port = ntohs(addr->sin.sin4.sin_port);
	} break;
	case 16: {
		inet_ntop(AF_INET6, &addr->sin.sin6.sin6_addr, dst, INET6_ADDRSTRLEN);
		port = ntohs(addr->sin.sin6.sin6_port);
	} break;
	default:
		dst[0] = '?';
		dst[1] = 0x00;
	}

	static char buf[255];
	snprintf(buf, sizeof(buf), "%s:%i", dst, port);
	return buf;
}


int net_bind_udp(const net_addr *saddr)
{
	int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sd < 0) {
		PFATAL("socket()");
	}

	int one = 1;
	int r = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, (char *)&one,
			   sizeof(one));
	if (r < 0) {
		PFATAL("setsockopt(SO_REUSEADDR)");
	}

	if (bind(sd, saddr->get_sockaddr(), saddr->get_sockaddr_len()) < 0) {
		PFATAL("bind()");
	}

	return sd;
}


int net_connect_udp(const net_addr *saddr, int src_port)
{
	int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sd < 0) {
		PFATAL("socket()");
	}

	int one = 1;
	int r = setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, (char *)&one,
			   sizeof(one));
	if (r < 0) {
		PFATAL("setsockopt(SO_REUSEADDR)");
	}

	if (src_port > 1 && src_port < 65536) {
		net_addr src("0.0.0.0", src_port);
		if (bind(sd, src.get_sockaddr(), src.get_sockaddr_len()) < 0) {
			PFATAL("bind()");
		}
	}

	if (-1 == connect(sd, saddr->get_sockaddr(), saddr->get_sockaddr_len())) {
		/* is non-blocking, so we don't get error at that point yet */
		if (EINPROGRESS != errno) {
			PFATAL("connect()");
			return -1;
		}
	}

	return sd;
}
