

/* net.c */
struct net_addr
{
	int ipver;
	int sockaddr_len;
	struct sockaddr_in sin4;
	struct sockaddr_in6 sin6;
	struct sockaddr *sockaddr;
};
inline bool operator<(const net_addr& l, const net_addr& r )
{
  return (l.ipver < r.ipver) &&
  	     (l.sin4.sin_family < r.sin4.sin_family) &&
	     (l.sin4.sin_addr.s_addr < l.sin4.sin_addr.s_addr) &&
	     (l.sin4.sin_port < l.sin4.sin_port);
}

void parse_addr(struct net_addr *netaddr, const char *addr, int port = 0);
const char *addr_to_str(const struct net_addr *addr);
int net_bind_udp(struct net_addr *addr);
void net_set_buffer_size(int cd, int max, int send);
void net_gethostbyname(struct net_addr *shost, const char *host, int port);
int net_connect_udp(struct net_addr *addr, int src_port);

