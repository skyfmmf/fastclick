#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <clicknet/tcp.h>
#include <click/timestamp.hh>
#include <click/multithread.hh>
#include "tcpin.hh"
#include "ipelement.hh"

CLICK_DECLS

TCPIn::TCPIn() : outElement(NULL), returnElement(NULL),
    poolModificationNodes(MODIFICATIONNODES_POOL_SIZE),
    poolModificationLists(MODIFICATIONLISTS_POOL_SIZE),
    poolFcbTcpCommon(TCPCOMMON_POOL_SIZE),
    tableFcbTcpCommon(NULL)
{
}

int TCPIn::configure(Vector<String> &conf, ErrorHandler *errh)
{
    String returnName = "";
    String outName = "";
    int flowDirectionParam = -1;

    if(Args(conf, this, errh)
    .read_mp("FLOWDIRECTION", flowDirectionParam)
    .read_mp("OUTNAME", outName)
    .read_mp("RETURNNAME", returnName)
    .complete() < 0)
        return -1;

    Element* returnElement = this->router()->find(returnName, errh);
    Element* outElement = this->router()->find(outName, errh);

    if(returnElement == NULL)
    {
        click_chatter("Error: Could not find TCPIn element "
            "called \"%s\".", returnName.c_str());
        return -1;
    }
    else if(outElement == NULL)
    {
        click_chatter("Error: Could not find TCPOut element "
            "called \"%s\".", outName.c_str());
        return -1;
    }
    else if(strcmp("TCPIn", returnElement->class_name()) != 0)
    {
        click_chatter("Error: Element \"%s\" is not a TCPIn element "
            "but a %s element.", returnName.c_str(), returnElement->class_name());
        return -1;
    }
    else if(strcmp("TCPOut", outElement->class_name()) != 0)
    {
        click_chatter("Error: Element \"%s\" is not a TCPOut element "
            "but a %s element.", outName.c_str(), outElement->class_name());
        return -1;
    }
    else if(flowDirectionParam != 0 && flowDirectionParam != 1)
    {
        click_chatter("Error: FLOWDIRECTION %u is not valid.", flowDirectionParam);
        return -1;
    }
    else
    {
        this->returnElement = (TCPIn*)returnElement;
        this->outElement = (TCPOut*)outElement;
        this->outElement->setInElement(this);
        setFlowDirection((unsigned int)flowDirectionParam);
        this->outElement->setFlowDirection(getFlowDirection());
    }

    return 0;
}

Packet* TCPIn::processPacket(struct fcb *fcb, Packet* p)
{
    // Ensure that the pointers in the FCB are set
    if(fcb->tcpin.poolModificationNodes == NULL)
        fcb->tcpin.poolModificationNodes = &poolModificationNodes;

    if(fcb->tcpin.poolModificationLists == NULL)
        fcb->tcpin.poolModificationLists = &poolModificationLists;

    // Assign the tcp_common structure if not already done
    if(fcb->tcp_common == NULL)
    {
        if(!assignTCPCommon(fcb, p))
        {
            // The allocation failed, meaning that the packet is not a SYN
            // packet. This is not supposed to happen and it means that
            // the first two packets of the connection are not SYN packets
            click_chatter("Warning: Trying to assign a common tcp memory area"
                " for a non-SYN packet");
            p->kill();

            return NULL;
        }
    }
    else
    {
        // The structure has been assigned so the three-way handshake is over
        // Check that the packet is not a SYN packet
        if(isSyn(p))
        {
            click_chatter("Unexpected SYN packet. Dropping it");
            p->kill();

            return NULL;
        }
    }

    if(!checkConnectionClosed(fcb, p))
    {
        p->kill();
        return NULL;
    }

    WritablePacket *packet = p->uniqueify();

    // Compute the offset of the TCP payload
    uint16_t offset = getPayloadOffset(packet);
    setContentOffset(packet, offset);

    tcp_seq_t seqNumber = getSequenceNumber(packet);
    tcp_seq_t lastAckSentOtherSide = fcb->tcp_common->maintainers[getOppositeFlowDirection()].getLastAckSent();

    if(!isSyn(packet) && SEQ_LT(seqNumber, lastAckSentOtherSide))
    {
        // We receive content that has already been ACKed.
        // This case occurs when the ACK is lost between the middlebox and
        // the destination.
        // In this case, we ACK the content and we discard it
        click_chatter("Lost ACK detected: %u, resending it");
        ackPacket(fcb, packet, false);
        packet->kill();
        return NULL;
    }

    // Take care of the ACK value in the packet
    bool isAnAck = isAck(packet);
    tcp_seq_t ackNumber = 0;
    tcp_seq_t newAckNumber = 0;
    if(isAnAck)
    {
        // Map the ack number according to the bytestreammaintainer of the other direction
        ackNumber = getAckNumber(packet);
        newAckNumber = fcb->tcp_common->maintainers[getOppositeFlowDirection()].mapAck(ackNumber);

        // Update the value of the last ACK received
        fcb->tcp_common->maintainers[getFlowDirection()].setLastAckReceived(ackNumber);

        // Prune the ByteStreamMaintainer of the other side
        fcb->tcp_common->maintainers[getOppositeFlowDirection()].prune(ackNumber);

        // Update the statistics regarding the RTT
        // And potentially update the retransmission timer
        fcb->tcp_common->retransmissionTimings[getOppositeFlowDirection()].signalAck(fcb, ackNumber);


        // Check if the current packet is just an ACK without more information
        if(isJustAnAck(packet))
        {
            // If it is the case, check that the ACK value is greater than what
            // we have already sent earlier
            if(SEQ_LT(newAckNumber, fcb->tcp_common->maintainers[getFlowDirection()].getLastAckSent()))
            {
                // If this is not the case, the packet does not give any information
                // We can drop it
                click_chatter("Received an ACK for a sequence number already ACKed. Dropping it (%u ; %u).", newAckNumber, fcb->tcp_common->maintainers[getFlowDirection()].getLastAckSent());
                packet->kill();
                return NULL;
            }
        }

        // If needed, update the ACK value in the packet with the mapped one
        if(ackNumber != newAckNumber)
        {
            click_chatter("Ack number %u becomes %u in flow %u", ackNumber, newAckNumber, flowDirection);

            setAckNumber(packet, newAckNumber);
            setPacketDirty(fcb, packet);
        }
        else
        {
            click_chatter("Ack number %u stays the same in flow %u", ackNumber, flowDirection);
        }
    }

    return packet;
}

TCPOut* TCPIn::getOutElement()
{
    return outElement;
}

TCPIn* TCPIn::getReturnElement()
{
    return returnElement;
}

void TCPIn::closeConnection(struct fcb* fcb, WritablePacket *packet, bool graceful, bool bothSides)
{
    uint8_t newFlag = 0;

    if(graceful)
        newFlag = TH_FIN;
    else
        newFlag = TH_RST;

    click_tcp *tcph = packet->tcp_header();

    // Change the flags of the packet
    tcph->th_flags = tcph->th_flags | newFlag;

    TCPClosingState::Value newStateSelf = TCPClosingState::BEING_CLOSED_GRACEFUL;
    TCPClosingState::Value newStateOther = TCPClosingState::CLOSED_GRACEFUL;

    if(!graceful)
    {
        newStateSelf = TCPClosingState::BEING_CLOSED_UNGRACEFUL;
        newStateOther = TCPClosingState::CLOSED_UNGRACEFUL;
    }
    fcb->tcp_common->closingStates[getFlowDirection()] = newStateSelf;

    if(bothSides)
    {
        fcb->tcp_common->closingStates[getOppositeFlowDirection()] = newStateOther;

        // Get the information needed to ack the given packet
        uint32_t saddr = getDestinationAddress(packet);
        uint32_t daddr = getSourceAddress(packet);
        uint16_t sport = getDestinationPort(packet);
        uint16_t dport = getSourcePort(packet);
        // The SEQ value is the initial ACK value in the packet sent
        // by the source.
        tcp_seq_t seq = getAckNumber(packet);
        // As the ACK has been mapped, we map it back to get its initial value
        seq = fcb->tcp_common->maintainers[getOppositeFlowDirection()].mapSeq(seq);

        // The ACK is the sequence number sent by the source
        // to which we add the size of the payload to acknowledge it
        tcp_seq_t ack = getSequenceNumber(packet) + getPayloadLength(packet);

        if(isFin(packet) || isSyn(packet))
            ack++;

        // Craft and send the ack
        outElement->sendClosingPacket(fcb->tcp_common->maintainers[getOppositeFlowDirection()], saddr, daddr, sport, dport, seq, ack, graceful);
    }

    click_chatter("Closing connection on flow %u (graceful: %u, both sides: %u)", getFlowDirection(), graceful, bothSides);

    StackElement::closeConnection(fcb, packet, graceful, bothSides);
}

ModificationList* TCPIn::getModificationList(struct fcb *fcb, WritablePacket* packet)
{
    HashTable<tcp_seq_t, ModificationList*> &modificationLists = fcb->tcpin.modificationLists;

    ModificationList* list = NULL;

    // Search the modification list in the hashtable
    HashTable<tcp_seq_t, ModificationList*>::iterator it = modificationLists.find(getSequenceNumber(packet));

    // If we could find the element
    if(it != modificationLists.end())
        list = it.value();

    // If no list was assigned to this packet, create a new one
    if(list == NULL)
    {
        ModificationList* listPtr = fcb->tcpin.poolModificationLists->getMemory();
        // Call the constructor manually to have a clean object
        list = new(listPtr) ModificationList(&poolModificationNodes);
        modificationLists.set(getSequenceNumber(packet), list);
    }

    return list;
}

bool TCPIn::hasModificationList(struct fcb *fcb, Packet* packet)
{
    HashTable<tcp_seq_t, ModificationList*> &modificationLists = fcb->tcpin.modificationLists;
    HashTable<tcp_seq_t, ModificationList*>::iterator it = modificationLists.find(getSequenceNumber(packet));

    return (it != modificationLists.end());
}

void TCPIn::removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    //click_chatter("Removing %u bytes", length);
    ModificationList* list = getModificationList(fcb, packet);

    tcp_seq_t seqNumber = getSequenceNumber(packet);

    uint16_t tcpOffset = getPayloadOffset(packet);
    uint16_t contentOffset = getContentOffset(packet);
    position += contentOffset;
    list->addModification(seqNumber + position - tcpOffset, -((int)length));

    unsigned char *source = packet->data();
    uint32_t bytesAfter = packet->length() - position;

    memmove(&source[position], &source[position + length], bytesAfter);
    packet->take(length);

    // Continue in the stack function
    StackElement::removeBytes(fcb, packet, position, length);
}

WritablePacket* TCPIn::insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position, uint32_t length)
{
    tcp_seq_t seqNumber = getSequenceNumber(packet);

    uint16_t tcpOffset = getPayloadOffset(packet);
    uint16_t contentOffset = getContentOffset(packet);
    position += contentOffset;
    getModificationList(fcb, packet)->addModification(seqNumber + position - tcpOffset, (int)length);

    uint32_t bytesAfter = packet->length() - position;

    WritablePacket *newPacket = packet->put(length);
    assert(newPacket != NULL);
    unsigned char *source = newPacket->data();

    memmove(&source[position + length], &source[position], bytesAfter);

    return newPacket;
}

void TCPIn::requestMorePackets(struct fcb *fcb, Packet *packet)
{
    ackPacket(fcb, packet, true);

    // Continue in the stack function
    StackElement::requestMorePackets(fcb, packet);
}

void TCPIn::ackPacket(struct fcb *fcb, Packet* packet, bool ackMapped)
{
    // Get the information needed to ack the given packet
    uint32_t saddr = getDestinationAddress(packet);
    uint32_t daddr = getSourceAddress(packet);
    uint16_t sport = getDestinationPort(packet);
    uint16_t dport = getSourcePort(packet);
    // The SEQ value is the initial ACK value in the packet sent
    // by the source.
    tcp_seq_t seq = getAckNumber(packet);
    // If the ACK has been mapped, we map it back to get its initial value
    if(ackMapped)
        seq = fcb->tcp_common->maintainers[getOppositeFlowDirection()].mapSeq(seq);

    // The ACK is the sequence number sent by the source
    // to which we add the size of the payload to acknowledge it
    tcp_seq_t ack = getSequenceNumber(packet) + getPayloadLength(packet);

    if(isFin(packet) || isSyn(packet))
        ack++;

    // Craft and send the ack
    outElement->sendAck(fcb->tcp_common->maintainers[getOppositeFlowDirection()], saddr, daddr, sport, dport, seq, ack);
}

void TCPIn::setPacketDirty(struct fcb *fcb, WritablePacket* packet)
{
    // Annotate the packet to indicate it has been modified
    // While going through "out elements", the checksum will be recomputed
    setAnnotationDirty(packet, true);

    // Continue in the stack function
    StackElement::setPacketDirty(fcb, packet);
}

bool TCPIn::checkConnectionClosed(struct fcb* fcb, Packet *packet)
{
    TCPClosingState::Value closingState = fcb->tcp_common->closingStates[getFlowDirection()];
    if(closingState != TCPClosingState::OPEN)
    {
        if(closingState == TCPClosingState::BEING_CLOSED_GRACEFUL || closingState == TCPClosingState::CLOSED_GRACEFUL)
        {
            // We ACK every packet and we discard it
            if(isFin(packet) || isSyn(packet) || getPayloadLength(packet) > 0)
                ackPacket(fcb, packet, false);
        }

        return false;
    }

    return true;
}

unsigned int TCPIn::determineFlowDirection()
{
    return getFlowDirection();
}

bool TCPIn::assignTCPCommon(struct fcb *fcb, Packet *packet)
{
    const click_tcp *tcph = packet->tcp_header();
    uint8_t flags = tcph->th_flags;
    const click_ip *iph = packet->ip_header();

    // Check if we are in the first two steps of the three-way handshake
    // (SYN packet)
    if(!(flags & TH_SYN))
        return false;

    // Check if we are the side initiating the connection or not
    // (if ACK flag, we are not the initiator)
    if(flags & TH_ACK)
    {
        // Getting the flow ID for the opposite side of the connection
        IPFlowID flowID(iph->ip_dst, tcph->th_dport, iph->ip_src, tcph->th_sport);

        // Get the struct allocated by the initiator
        fcb->tcp_common = returnElement->getTCPCommon(flowID);
        // Initialize the RBT with the RBTManager
        fcb->tcp_common->maintainers[getFlowDirection()].initialize(&rbtManager);
        fcb->tcpin.inChargeOfTcpCommon = false;
    }
    else
    {
        IPFlowID flowID(iph->ip_src, tcph->th_sport, iph->ip_dst, tcph->th_dport);
        // We are the initiator, we need to allocate memory
        struct fcb_tcp_common *allocated = poolFcbTcpCommon.getMemory();
        // Call the constructor with some parameters that will be used
        // to free the memory of the structure when not needed anymore
        allocated = new(allocated) struct fcb_tcp_common();

        // Add an entry if the hashtable
        tableFcbTcpCommon.set(flowID, allocated);

        // Set the pointer in the structure
        fcb->tcp_common = allocated;
        // Initialize the RBT with the RBTManager
        fcb->tcp_common->maintainers[getFlowDirection()].initialize(&rbtManager);

        // Store in our structure information needed to free the memory
        // of the common structure
        fcb->tcpin.inChargeOfTcpCommon = true;
        fcb->tcpin.flowID = flowID;
        fcb->tcpin.tableTcpCommon = &tableFcbTcpCommon;
        fcb->tcpin.poolTcpCommon = &poolFcbTcpCommon;
    }

    // Set information about the flow
    fcb->tcp_common->maintainers[getFlowDirection()].setIpSrc(getSourceAddress(packet));
    fcb->tcp_common->maintainers[getFlowDirection()].setIpDst(getDestinationAddress(packet));
    fcb->tcp_common->maintainers[getFlowDirection()].setPortSrc(getDestinationPort(packet));
    fcb->tcp_common->maintainers[getFlowDirection()].setPortDst(getSourcePort(packet));

    return true;
}


struct fcb_tcp_common* TCPIn::getTCPCommon(IPFlowID flowID)
{
    HashTable<IPFlowID, struct fcb_tcp_common*>::iterator it = tableFcbTcpCommon.find(flowID);

    if(it == tableFcbTcpCommon.end())
        return NULL; // Not in the table
    else
        return it.value();
}

TCPIn::~TCPIn()
{

}

CLICK_ENDDECLS
ELEMENT_REQUIRES(ByteStreamMaintainer)
ELEMENT_REQUIRES(ModificationList)
ELEMENT_REQUIRES(TCPElement)
ELEMENT_REQUIRES(RetransmissionTiming)
EXPORT_ELEMENT(TCPIn)
//ELEMENT_MT_SAFE(TCPIn)
