/*
 * tcpelement.hh - Provides several methods that can be used by elements to manage TCP packets
 * Romain Gaillard
 * Tom Barbette
 */

#ifndef MIDDLEBOX_TCPELEMENT_HH
#define MIDDLEBOX_TCPELEMENT_HH

#include <click/config.h>
#include <click/glue.hh>
#include <clicknet/tcp.h>
#include <clicknet/ip.h>
#include <click/element.hh>

#if HAVE_DPDK
#include <click/dpdkdevice.hh>
#endif
#include "ipelement.hh"
#if HAVE_DPDK
#include <rte_ip.h>
#endif

CLICK_DECLS

/**
 * @class TCPElement
 * @brief This class provides several methods that can be used by elements that inherits
 * from it to manage TCP packets.
 */

class TCPElement : public IPElement
{
public:
    TCPElement() CLICK_COLD;


    WritablePacket* forgeRst(Packet* packet) {
        // Get the information needed to ack the given packet
        uint32_t saddr = getDestinationAddress(packet);
        uint32_t daddr = getSourceAddress(packet);
        uint16_t sport = getDestinationPort(packet);
        uint16_t dport = getSourcePort(packet);
        tcp_seq_t ack = getSequenceNumber(packet);
        tcp_seq_t seq = getAckNumber(packet);
        uint8_t flag = TH_RST;

        // Craft the packet
        WritablePacket* forged = forgePacket(saddr, daddr, sport, dport, seq, ack, 0, flag);
        return forged;
    }

    /**
     * @brief Create a TCP packet
     * @param saddr Source IP address
     * @param daddr Destination IP address
     * @param sport Source port
     * @param dport Destination port
     * @param seq Sequence number
     * @param ack Ack number
     * @param windowSize Window size
     * @param flags The TCP flags on one byte
     * @param contentSize Extra space to allocate for the TCP payload
     * @return The created TCP packet
     */
    WritablePacket* forgePacket(uint32_t saddr, uint32_t daddr, uint16_t sport, uint16_t dport,
        tcp_seq_t seq, tcp_seq_t ack, uint16_t winSize, uint8_t flags,
        uint32_t contentSize = 0) const;

    /**
     * @brief Return the destination port of a packet
     * @param packet The packet
     * @return The destination port of the packet
     */
    uint16_t getDestinationPort(Packet* packet) const;

    /**
     * @brief Return the source port of a packet
     * @param packet The packet
     * @return The source port of the packet
     */
    uint16_t getSourcePort(Packet* packet) const;

    /**
     * @brief Return the sequence number of a packet
     * @param packet The packet
     * @return The sequence number of the packet
     */
    tcp_seq_t getSequenceNumber(Packet* packet) const;

    /**
     * @brief Return the ack number of a packet
     * @param packet The packet
     * @return The ack number of the packet
     */
    tcp_seq_t getAckNumber(Packet* packet) const;

    /**
     * @brief Return the sequence number of the packet that will be received after the given one
     * @param fcb A pointer to the FCB of the flow
     * @param packet The packet to check
     * @return The sequence number of the packet after the given one
     */
    inline tcp_seq_t getNextSequenceNumber(Packet* packet) const;

    /**
     * @brief Return the window size set in the header of a packet
     * @param packet The packet
     * @return The window size set in the header of the packet
     */
    uint16_t getWindowSize(Packet *packet) const;

    /**
     * @brief Set the window size in the TCP header of a packet
     * @param packet The packet
     * @param winSize The window size
     */
    void setWindowSize(WritablePacket *packet, uint16_t winSize) const;

    /**
     * @brief Indicate whether a packet is a SYN packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is a SYN packet
     */
    bool isSyn(Packet* packet) const;

    /**
     * @brief Indicate whether a packet is a FIN packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is a FIN packet
     */
    bool isFin(Packet* packet) const;

    /**
     * @brief Indicate whether a packet is a RST packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is a RST packet
     */
    bool isRst(Packet* packet) const;

    /**
     * @brief Indicate whether a packet is an ACK packet
     * @param packet The packet
     * @return A boolean indicating whether the packet is an ACK packet
     */
    bool isAck(Packet* packet) const;

    /**
     * @brief Check if a given flag is set in a packet
     * @param packet The packet
     * @param flag The offset of the flag
     * @return A boolean indicating whether the flag is set in the packet
     */
    bool checkFlag(Packet *packet, uint8_t flag) const;

    /**
     * @brief Return the length of the TCP payload of a packet
     * @param packet The packet
     * @return The length of the TCP payload of the packet
     */
    static unsigned getPayloadLength(Packet* packet);

    /**
     * @brief Return the payload of a TCP packet
     * @param packet The packet
     * @return The payload of the packet
     */
    unsigned char* getPayload(WritablePacket* packet) const;

    /**
     * @brief Return the payload of a TCP packet
     * @param packet The packet
     * @return The const payload of the packet
     */
    const unsigned char* getPayloadConst(Packet* packet) const;

    /**
     * @brief Return the offset of the payload in a TCP packet
     * @param packet The packet
     * @return The offset of the payload in a TCP packet
     */
    uint16_t getPayloadOffset(Packet* packet) const;

    /**
     * @brief Set the payload of a TCP packet
     * @param packet The packet
     * @param payload The new value of the payload
     * @param length The length of the payload
     */
    void setPayload(WritablePacket* packet, const unsigned char* payload, uint32_t length) const;

    /**
     * @brief Set the sequence number of a TCP packet
     * @param packet The packet
     * @param seq The sequence number of the packet
     */
    void setSequenceNumber(WritablePacket* packet, tcp_seq_t seq) const;

    /**
     * @brief Set the ack number of a TCP packet
     * @param packet The packet
     * @param ack The ack number of the packet
     */
    void setAckNumber(WritablePacket* packet, tcp_seq_t ack) const;

    /**
     * @brief Indicate whether the packet is just an ACK without any additional information
     * @param packet The packet
     * @return A boolean indicating whether the packet is just an ACK without any additional
     * information
     */
    static bool isJustAnAck(Packet* packet);

    /**
     * @brief Return the flags of a TCP packet
     * @param packet The packet
     * @return A byte containing the flags of the packet
     */
    uint8_t getFlags(Packet* packet) const;

    /**
     * @brief Compute the TCP checksum of a packet and set it in its header
     * @param packet The packet
     */
    inline void computeTCPChecksum(WritablePacket* packet) const;

    /**
     * @brief Reset the TCP checksum of a packet and set it in its header
     * @param packet The packet
     */
    inline void resetTCPChecksum(WritablePacket* packet) const;

    /*Element responsible of the timeout management.
     * to allow for a unique per-flow acquire/release and timeout management
     * elements that can be used without a real tcp context (such as TCPReorder)
     * can check if some TCP context is managed by a more tailored element
     */
    //FlowElement* _tcp_context;
};

inline tcp_seq_t
TCPElement::getNextSequenceNumber(Packet* packet) const
{
    tcp_seq_t currentSeq = getSequenceNumber(packet);

    // Compute the size of the current payload
    tcp_seq_t nextSeq = currentSeq + getPayloadLength(packet);

    // FIN and SYN packets count for one in the sequence number
    if(isFin(packet) || isSyn(packet))
        nextSeq++;

    return nextSeq;
}

inline void TCPElement::computeTCPChecksum(WritablePacket *packet) const
{
    click_ip *iph = packet->ip_header();
    click_tcp *tcph = packet->tcp_header();

    unsigned plen = ntohs(iph->ip_len) - (iph->ip_hl << 2);
    tcph->th_sum = 0;
    unsigned csum = click_in_cksum((unsigned char *)tcph, plen);
    tcph->th_sum = click_in_cksum_pseudohdr(csum, iph, plen);
}

inline void TCPElement::resetTCPChecksum(WritablePacket *packet) const
{
    click_ip *iph = packet->ip_header();
    click_tcp *tcph = packet->tcp_header();

    iph->ip_sum = 0;
    tcph->th_sum = 0;
#if HAVE_DPDK
    if (!DPDKDevice::is_dpdk_buffer(packet))
    {
        click_chatter("Not a DPDK buffer. For max performance, arrange TCP element to always work on DPDK buffers");
#else
    {
#endif
        computeTCPChecksum(packet);
        return;
    }
#if HAVE_DPDK
    rte_mbuf* mbuf = (struct rte_mbuf *) packet->destructor_argument();
    mbuf->l2_len = packet->mac_header_length();
    mbuf->l3_len = packet->network_header_length();
    mbuf->l4_len = tcph->th_off << 2;
        mbuf->ol_flags |= PKT_TX_TCP_CKSUM | PKT_TX_IP_CKSUM | PKT_TX_IPV4;
    tcph->th_sum = rte_ipv4_phdr_cksum((struct ipv4_hdr *)iph, mbuf->ol_flags);
#endif
}

inline void TCPElement::setSequenceNumber(WritablePacket* packet, tcp_seq_t seq) const
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_seq = htonl(seq);
}

inline tcp_seq_t TCPElement::getSequenceNumber(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_seq);
}

inline tcp_seq_t TCPElement::getAckNumber(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohl(tcph->th_ack);
}

inline void TCPElement::setAckNumber(WritablePacket* packet, tcp_seq_t ack) const
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_ack = htonl(ack);
}

inline uint16_t TCPElement::getWindowSize(Packet *packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_win);
}

inline void TCPElement::setWindowSize(WritablePacket *packet, uint16_t winSize) const
{
    click_tcp *tcph = packet->tcp_header();

    tcph->th_win = htons(winSize);
}

inline unsigned TCPElement::getPayloadLength(Packet* packet)
{
    const click_ip *iph = packet->ip_header();
    unsigned iph_len = iph->ip_hl << 2;
    uint16_t ip_len = ntohs(iph->ip_len);

    const click_tcp *tcph = packet->tcp_header();
    unsigned tcp_offset = tcph->th_off << 2;

    return ip_len - iph_len - tcp_offset;
}

inline unsigned char* TCPElement::getPayload(WritablePacket* packet) const
{
    click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    return (unsigned char*)packet->transport_header() + tcph_len;
}

inline const unsigned char* TCPElement::getPayloadConst(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    return (const unsigned char*)packet->transport_header() + tcph_len;
}

inline void TCPElement::setPayload(WritablePacket* packet, const unsigned char* payload,
    uint32_t length) const
{
    click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;

    unsigned char* payloadPtr = (unsigned char*)packet->transport_header() + tcph_len;
    memcpy(payloadPtr, payload, length);
}

inline uint16_t TCPElement::getPayloadOffset(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    // Compute the offset of the TCP payload
    unsigned tcph_len = tcph->th_off << 2;
    uint16_t offset = (uint16_t)(packet->transport_header() + tcph_len - packet->data());

    return offset;
}


inline uint16_t TCPElement::getSourcePort(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_sport);
}

inline uint16_t TCPElement::getDestinationPort(Packet* packet) const
{
    const click_tcp *tcph = packet->tcp_header();

    return ntohs(tcph->th_dport);
}

inline bool TCPElement::isSyn(Packet* packet) const
{
    return checkFlag(packet, TH_SYN);
}

inline bool TCPElement::isFin(Packet* packet) const
{
    return checkFlag(packet, TH_FIN);
}

inline bool TCPElement::isRst(Packet* packet) const
{
    return checkFlag(packet, TH_RST);
}

inline bool TCPElement::isAck(Packet* packet) const
{
    return checkFlag(packet, TH_ACK);
}

inline bool TCPElement::checkFlag(Packet *packet, uint8_t flag) const
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // Check if the packet has the given flag
    if(flags & flag)
        return true;
    else
        return false;
}

inline uint8_t TCPElement::getFlags(Packet *packet) const
{
    const click_tcp *tcph = packet->tcp_header();
    return tcph->th_flags;
}

inline bool TCPElement::isJustAnAck(Packet* packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;

    // If we have a payload, we are more than just an ACK
    if(getPayloadLength(packet) > 0)
        return false;

    //  If we have other flags, we are more than just an ACK
    if(flags == TH_ACK)
        return true;
    else
        return false;
}


CLICK_ENDDECLS

#endif
