#ifndef __NET_H__
#define __NET_H__


#include <string>

struct net_addr;

void parse_addr(net_addr *netaddr, const char *addr, int port = 0);
void net_gethostbyname(net_addr *saddr, const char *host, int port);

const char *addr_to_str(const net_addr *addr);

int net_bind_udp(const net_addr *addr);

void net_set_buffer_size(int cd, int max, int send);

int net_connect_udp(const net_addr *addr, int src_port);

struct net_addr
{
    int ipver;

    union {
        struct sockaddr_in sin4;
        struct sockaddr_in6 sin6;
    } sin;

    net_addr() : ipver(0)
    {
        memset(&sin, 0, sizeof(sin));
    }

    net_addr(const char* addr, uint16_t port) : ipver(0)
    {
        parse_addr(this, addr, port);
    }

    void from_str(const char* addr, uint16_t port)
    {
        parse_addr(this, addr, port);
    }

    net_addr(const struct msghdr* mhdr)
    {
        if (mhdr->msg_namelen == sizeof(sin.sin4)) {
            ipver = 4;
        } else if (mhdr->msg_namelen == sizeof(sin.sin6)) {
            ipver = 6;
        } else {
            //TODO: handle error
        }
        memcpy(get_sockaddr(), mhdr->msg_name, get_sockaddr_len());
    }

    int get_sockaddr_len() const
    {
        if (ipver == 4) {
            return sizeof(sin.sin4);
        } else {
            return sizeof(sin.sin6);
        }
    }

    struct sockaddr* get_sockaddr() const
    {
        if (ipver == 4) {
            return (struct sockaddr*) &sin.sin4;
        } else {
            return (struct sockaddr*) &sin.sin6;
        }
    }

    operator std::string() const
    {
        char dst[INET6_ADDRSTRLEN + 1];
        int port = 0;

        switch (ipver) {
        case 4: {
            inet_ntop(AF_INET, &sin.sin4.sin_addr, dst, INET6_ADDRSTRLEN);
            port = ntohs(sin.sin4.sin_port);
        } break;
        case 16: {
            inet_ntop(AF_INET6, &sin.sin6.sin6_addr, dst, INET6_ADDRSTRLEN);
            port = ntohs(sin.sin6.sin6_port);
        } break;
        default:
            dst[0] = '?';
            dst[1] = 0x00;
        }

        char buf[255];
        snprintf(buf, sizeof(buf), "%s:%i", dst, port);
        return buf;
    }
};

inline bool operator<(const net_addr& l, const net_addr& r )
{
    if (l.ipver == r.ipver) {
        if (l.ipver == 4) {
            return (l.sin.sin4.sin_addr.s_addr < r.sin.sin4.sin_addr.s_addr)
                    || (
                        (l.sin.sin4.sin_addr.s_addr == r.sin.sin4.sin_addr.s_addr)
                        && (l.sin.sin4.sin_port < r.sin.sin4.sin_port)
                        );
        } else {
            int v = memcmp(l.sin.sin6.sin6_addr.__in6_u.__u6_addr8, r.sin.sin6.sin6_addr.__in6_u.__u6_addr8, sizeof(l.sin.sin6.sin6_addr.__in6_u.__u6_addr8));
            return v < 0 || ((v == 0) && l.sin.sin6.sin6_port < r.sin.sin6.sin6_port);
        }
    } else {
        return l.ipver < r.ipver;
    }
}


#endif
