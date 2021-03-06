//
// Receives, processes and sends IPv4 datagrams.
//
// Copyright 2015 Raphael Javaux <raphaeljavaux@gmail.com>
// University of Liege.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __RUSTY_NET_IPV4_HPP__
#define __RUSTY_NET_IPV4_HPP__

#include <algorithm>            // min()
#include <cstring>
#include <functional>           // equal_to, hash
#include <vector>

#include <arpa/inet.h>          // inet_ntoa()
#include <net/ethernet.h>       // ETHERTYPE_IP
#include <netinet/in.h>         // in_addr, IPPROTO_TCP
#include <netinet/ip.h>         // IPDEFTTL, IPVERSION, IP_MF, IPPROTO_TCP,
                                // IPTOS_CLASS_DEFAULT

#include "net/checksum.hpp"     // checksum_t, partial_sum_t
#include "net/tcp.hpp"          // tcp_t
#include "util/macros.hpp"      // RUSTY_*, COLOR_*

using namespace std;

namespace rusty {
namespace net {

#define IPV4_COLOR     COLOR_CYN
#define IPV4_DEBUG(MSG, ...)                                                   \
    RUSTY_DEBUG("IPV4", IPV4_COLOR, MSG, ##__VA_ARGS__)
#define IPV4_ERROR(MSG, ...)                                                   \
    RUSTY_ERROR("IPV4", IPV4_COLOR, MSG, ##__VA_ARGS__)

struct ipv4_addr_t {
    uint32_t    value;

    inline ipv4_addr_t &operator=(ipv4_addr_t other)
    {
        value = other.value;
        return *this;
    }

    friend inline bool operator==(ipv4_addr_t a, ipv4_addr_t b)
    {
        return a.value == b.value;
    }

    friend inline bool operator!=(ipv4_addr_t a, ipv4_addr_t b)
    {
        return a.value != b.value;
    }

    // Converts the IPv4 address to a string in IPv4 dotted-decimal notation
    // into a statically allocated buffer.
    //
    // This method is typically called for debugging messages.
    static char *to_alpha(net_t<ipv4_addr_t> addr)
    {
        return inet_ntoa(ipv4_addr_t::to_in_addr(addr));
    }

    static net_t<ipv4_addr_t> from_in_addr(struct in_addr in_addr)
    {
        net_t<ipv4_addr_t> addr;
        addr.net.value = in_addr.s_addr;
        return addr;
    }

    static struct in_addr to_in_addr(net_t<ipv4_addr_t> addr)
    {
        struct in_addr in_addr;
        in_addr.s_addr = addr.net.value;
        return in_addr;
    }
} __attribute__ ((__packed__));

// IPv4 network layer able to process datagram from and to the specified
// data-link 'data_link_var_t' layer.
template <typename data_link_var_t, typename alloc_t = allocator<char *>>
struct ipv4_t {
    //
    // Member types
    //

    // Redefines 'data_link_var_t' as 'data_link_t' so it can be accessible as a
    // member type.
    typedef data_link_var_t                         data_link_t;

    typedef ipv4_t<data_link_t, alloc_t>            this_t;

    typedef ipv4_addr_t                             addr_t;

    typedef typename data_link_t::clock_t           clock_t;
    typedef typename data_link_t::cursor_t          cursor_t;
    typedef typename data_link_t::timer_manager_t   timer_manager_t;

    struct header_t {
        #if __BYTE_ORDER == __LITTLE_ENDIAN
            uint8_t ihl:4;
            uint8_t version:4;
        #elif __BYTE_ORDER == __BIG_ENDIAN
            uint8_t version:4;
            uint8_t ihl:4;
        #else
            #error "Please fix __BYTE_ORDER in <bits/endian.h>"
        #endif

        uint8_t         tos;
        net_t<uint16_t> tot_len;
        uint16_t        id;
        net_t<uint16_t> frag_off;
        uint8_t         ttl;
        uint8_t         protocol;
        checksum_t      check;
        net_t<addr_t>   saddr;
        net_t<addr_t>   daddr;
    } __attribute__ ((__packed__));

    // Lower layer address type.
    typedef typename data_link_t::addr_t            data_link_addr_t;

    // Upper layer protocol type.
    typedef tcp_t<this_t, alloc_t>                  tcp_ipv4_t;

    //
    // Static fields
    //

    // 'arp_t' requires the following static fields:
    static constexpr uint16_t   ARP_TYPE    = ETHERTYPE_IP;
    static constexpr size_t     ADDR_LEN    = 4;

    static constexpr size_t     HEADER_SIZE = sizeof (header_t);

    // Header size in 32 bit words.
    static constexpr size_t     HEADER_LEN  = HEADER_SIZE / sizeof (uint32_t);

    //
    // Fields
    //

    // Lower network layer instances.
    data_link_t                         *data_link;
    arp_t<data_link_t, this_t, alloc_t> *arp;

    // Upper protocol instances
    tcp_ipv4_t                          tcp;

    // Instance's IPv4 address
    net_t<addr_t>                       addr;

    // Maximum payload size. Doesn't change after intialization.
    size_t                              max_payload_size;

    // The current identification number used to indentify egressed datagrams.
    //
    // This counter is incremented by one each time a datagram is sent.
    uint16_t                            current_datagram_id = 0;

    //
    // Methods
    //

    // Creates an IPv4 environment without initializing it.
    //
    // One must call 'init()' before using any other method.
    ipv4_t(alloc_t _alloc = alloc_t()) : tcp(_alloc)
    {
    }

    // Creates an IPv4 environment for the given data-link layer instance and
    // IPv4 address.
    //
    // Does the same thing as creating the environment with 'ipv4_t()' and then
    // calling 'init()'.
    ipv4_t(
        data_link_t *_data_link, arp_t<data_link_t, this_t, alloc_t> *_arp,
        net_t<addr_t> _addr, timer_manager_t *_timers,
        alloc_t _alloc = alloc_t()
    ) : data_link(_data_link), arp(_arp), addr(_addr), tcp(_alloc)
    {
        // TCP must be initialized after max_payload_size
        max_payload_size = this->_max_payload_size();
        tcp.init(this, _timers);
    }

    // Initializes an IPv4 environment for the given data-link layer instance
    // and IPv4 address).
    void init(
        data_link_t *_data_link, arp_t<data_link_t, this_t, alloc_t> *_arp,
        net_t<addr_t> _addr, timer_manager_t *_timers
    )
    {
        data_link        = _data_link;
        arp              = _arp;
        addr             = _addr;
        max_payload_size = this->_max_payload_size();
        tcp.init(this, _timers);
    }

    // Processes an IPv4 datagram wich starts at the given cursor (data-link
    // layer payload without headers).
    void receive_datagram(cursor_t cursor)
    {
        size_t cursor_size = cursor.size();

        if (UNLIKELY(cursor_size < HEADER_SIZE)) {
            IPV4_ERROR("Datagram ignored: too small to hold an IPv4 header");
            return;
        }

        cursor.template read_with<header_t, void>(
        [this, cursor_size](const header_t *hdr, cursor_t payload) {
            #define IGNORE_DATAGRAM(WHY, ...)                                  \
                do {                                                           \
                    IPV4_ERROR(                                                \
                        "Datagram from %s ignored: " WHY,                      \
                        addr_t::to_alpha(hdr->saddr), ##__VA_ARGS__            \
                    );                                                         \
                    return;                                                    \
                } while (0)

            //
            // Checks datagram validity.
            //

            if (UNLIKELY(hdr->version != IPVERSION)) {
                IGNORE_DATAGRAM(
                    "invalid IP version (received %u, excpected %u)",
                    (unsigned int) hdr->version, IPVERSION
                );
            }

            if (hdr->ihl != HEADER_LEN)
                IGNORE_DATAGRAM("options are not supported");

            size_t header_size = hdr->ihl * sizeof (uint32_t),
                   total_size  = hdr->tot_len.host();

            if (UNLIKELY(total_size < header_size)) {
                IGNORE_DATAGRAM(
                    "total size (%zu) is less than header size (%zu)",
                    total_size, header_size
                );
            }

            if (UNLIKELY(cursor_size < total_size)) {
                IGNORE_DATAGRAM(
                    "datagram size (%zu) is less than total size (%zu)",
                    total_size, cursor_size
                );
            }

            uint16_t frag_off_host = hdr->frag_off.host();
            if (UNLIKELY(
                   frag_off_host & IP_MF            // More fragment.
                || (frag_off_host & IP_OFFMASK) > 0 // Not the first fragment.
            ))
                IGNORE_DATAGRAM("fragmented datagrams are not supported");

            if (UNLIKELY(hdr->daddr != addr))
                IGNORE_DATAGRAM("bad recipient");

            if (UNLIKELY(!checksum_t(hdr, HEADER_SIZE).is_valid()))
                IGNORE_DATAGRAM("invalid checksum");

            //
            // Processes the datagram.
            //

            // The Ethernet frame could contain a small padding at its end.
            payload = payload.take(total_size - header_size);

            if (hdr->protocol == IPPROTO_TCP) {
                IPV4_DEBUG(
                    "Receives an IPv4 datagram from %s",
                    addr_t::to_alpha(hdr->saddr)
                );
                this->tcp.receive_segment(hdr->saddr, payload);
            } else {
                IGNORE_DATAGRAM(
                    "unknown IPv4 protocol (%u)", (unsigned int) hdr->protocol
                );
            }

            #undef IGNORE_DATAGRAM
        });
    }

    // Creates and push an IPv4 datagram with its payload to the daya-link layer
    // (L2).
    //
    // 'payload_writer' execution could be delayed after this function returns,
    // if an ARP transaction is required to translate the IPv4 address to its
    // corresponding data-link address. One should take care of not using memory
    // which could be deallocated before the 'payload_writer' execution.
    //
    // Returns 'true' if the 'payload_writer' execution has not been delayed.
    bool send_payload(
        net_t<addr_t> dst, uint8_t protocol,
        size_t payload_size, function<void(cursor_t)> payload_writer
    )
    {
        assert(payload_size >= 0 && payload_size <= max_payload_size);

        return this->arp->with_data_link_addr(
        dst, [this, dst, protocol, payload_size, payload_writer](
            const net_t<data_link_addr_t> *data_link_dst
        ) {
            if (data_link_dst == nullptr) {
                IPV4_ERROR("Unreachable address: %s", addr_t::to_alpha(dst));
                return;
            }

            size_t datagram_size = HEADER_SIZE + payload_size;

            IPV4_DEBUG(
                "Sends a %zu bytes IPv4 datagram to %s with protocol "
                "%" PRIu16, datagram_size, addr_t::to_alpha(dst), protocol
            );

            // lock
            uint16_t datagram_id = current_datagram_id++;
            // unlock

            this->data_link->send_ip_payload(
            *data_link_dst, datagram_size,
            [this, dst, payload_writer, protocol, datagram_size, datagram_id]
            (cursor_t cursor) {
                cursor = _write_header(
                    cursor, datagram_size, datagram_id, protocol, dst
                );
                payload_writer(cursor);
            });
        });
    }

    // Equivalent to 'send_payload()' with 'protocol' equals to 'IPPROTO_TCP'.
    //
    // This method is typically called by the TCP instance when it wants to
    // send a TCP segment.
    inline void send_tcp_payload(
        net_t<addr_t> dst, size_t payload_size,
        function<void(cursor_t)> payload_writer
    )
    {
        send_payload(dst, IPPROTO_TCP, payload_size, payload_writer);
    }

    //
    // Static methods
    //

    // Computes the partial (check)sum of the pseudo TCP header.
    //
    // The TCP segment checksum is computed on the TCP segment and on a pseudo
    // header. This pseudo header will only be used to compute the checksum and
    // will not be transmitted.
    //
    // The pseudo header of a TCP segment transmitted over IPv4 is the
    // following:
    //
    // +--------------------------------------------+
    // |           Source network address           |
    // +--------------------------------------------+
    // |         Destination network address        |
    // +----------+----------+----------------------+
    // |   zero   | Protocol |   TCP segment size   |
    // +----------+----------+----------------------+
    //
    // This method will be called by the TCP transport layer. Its implementation
    // varies depending on the network layer protocol, which explains why it's
    // not defined in 'tcp_t'.
    static partial_sum_t tcp_pseudo_header_sum(
        net_t<addr_t> saddr, net_t<addr_t> daddr,
        net_t<typename tcp_ipv4_t::seg_size_t> seg_size
    )
    {
        static constexpr size_t PSEUDO_HEADER_SIZE = 12;
        char buffer[PSEUDO_HEADER_SIZE];

        mempcpy(&buffer[0], &saddr, sizeof saddr);
        mempcpy(&buffer[4], &daddr, sizeof daddr);
        buffer[8] = 0;
        buffer[9] = IPPROTO_TCP;
        mempcpy(&buffer[10], &seg_size, sizeof seg_size);

        return partial_sum_t(buffer, PSEUDO_HEADER_SIZE);
    }

private:

    // Writes the IPv4 header starting at the given buffer cursor.
    cursor_t _write_header(
        cursor_t cursor, size_t datagram_size, uint16_t datagram_id,
        uint8_t protocol, net_t<addr_t> dst
    )
    {
        static const net_t<uint16_t> FRAG_OFF_NET = IP_DF; // Don't fragment.

        return cursor.template write_with<header_t>(
        [this, datagram_size, datagram_id, protocol, dst](header_t *hdr) {
            hdr->version  = IPVERSION;
            hdr->ihl      = HEADER_LEN;
            hdr->tos      = IPTOS_CLASS_DEFAULT;
            hdr->tot_len  = datagram_size;
            hdr->id       = datagram_id;
            hdr->frag_off = FRAG_OFF_NET;
            hdr->ttl      = IPDEFTTL;
            hdr->protocol = protocol;
            hdr->check    = checksum_t::ZERO;
            hdr->saddr    = this->addr;
            hdr->daddr    = dst;

            hdr->check    = checksum_t(hdr, HEADER_SIZE);
        });
    }

    size_t _max_payload_size(void)
    {
        // IPv4 datagrams can't be larger than 65,535 bytes.
        return   min(this->data_link->max_payload_size, (size_t) 65535)
               - HEADER_SIZE;
    }
};

#undef IPV4_COLOR
#undef IPV4_DEBUG
#undef IPV4_ERROR

} } /* namespace rusty::net */

namespace std {

// 'std::hash<>' and 'std::equal_to<>' instances are required for IPv4 addresses
// to be used in unordered containers.

using namespace rusty::net;

template <>
struct hash<ipv4_addr_t> {
    inline size_t operator()(const ipv4_addr_t &addr) const
    {
        return hash<uint32_t>()(addr.value);
    }
};

template <>
struct equal_to<ipv4_addr_t> {
    inline bool operator()(const ipv4_addr_t& a,const ipv4_addr_t& b) const
    {
        return a == b;
    }
};

} /* namespace std */

#endif /* __RUSTY_NET_IPV4_HPP__ */
