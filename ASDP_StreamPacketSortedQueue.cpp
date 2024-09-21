/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include "ASDP_StreamPacketSortedQueue.h"

using namespace asdp;

StreamPacketSortedQueue::StreamPacketSortedQueue(size_t maxHeldCount)
  : m_maxHeldCount(maxHeldCount)
  , m_nextExpectedSequenceNumber(0)
{
}

StreamPacketSortedQueue::~StreamPacketSortedQueue()
{
}

static uint32_t SequenceNumber(std::shared_ptr<StreamPacket> streamPacket)
{
  uint32_t sequenceNumber = 0;
  if (streamPacket != nullptr) {
    streamPacket->GetSequenceNumber(sequenceNumber);
  }
  return sequenceNumber;
}

std::list< std::shared_ptr<StreamPacket> > StreamPacketSortedQueue::AddPacket(std::shared_ptr<StreamPacket> streamPacket)
{
  // If the sequence number is below the one we expect, we must have already assumed that it was not
  // coming, so we discard the packet.  This will not change the list, so we return an empty list.
  if (SequenceNumber(streamPacket) < m_nextExpectedSequenceNumber) {
    return std::list< std::shared_ptr<StreamPacket> >();
  }

  // Insert the packet into the list in order.
  auto iter = m_packetList.begin();
  while (iter != m_packetList.end() && SequenceNumber(*iter) < SequenceNumber(streamPacket)) {
    ++iter;
  }
  m_packetList.insert(iter, streamPacket);

  // If the first entry in the list is the one we expect (or if the expected number is 0), we return it
  // and all sequential ones and increment the expected number.  If the list is too long, we must have
  // missed some packets, so we do the same.  We set the next number to expect to the next one after
  // the last one we found.
  std::list< std::shared_ptr<StreamPacket> > returnList;
  if (SequenceNumber(m_packetList.front()) == m_nextExpectedSequenceNumber ||
      m_nextExpectedSequenceNumber  == 0 ||
      m_packetList.size() > m_maxHeldCount) {
    do {
      iter = m_packetList.begin();
      returnList.push_back(*iter);
      m_nextExpectedSequenceNumber = SequenceNumber(*iter) + 1;
      ++iter;
      m_packetList.pop_front();
    } while (iter != m_packetList.end() && SequenceNumber(*iter) == m_nextExpectedSequenceNumber);
    return returnList;
  }

  // We're not ready to send yet, return an empty list.
  return std::list< std::shared_ptr<StreamPacket> >();
}

std::string StreamPacketSortedQueue::Test()
{
  StreamPacketSortedQueue queue(10);

  // Test the case where we receive packets in order.  Each push should result in a single output.
  for (size_t i = 0; i < 50; i++) {
    // We must generate this using new because std::make_shared does not have access to the private constructor.
    StreamPacket *packetPtr = new StreamPacket;
    std::shared_ptr<StreamPacket> packet(packetPtr);
    packet->SetSequenceNumber(i+100);
    std::list< std::shared_ptr<StreamPacket> > output = queue.AddPacket(packet);
    if (output.size() != 1) {
      return "Failed to return a single packet when expected.";
    }
    uint32_t seqNum = SequenceNumber(output.front());
    if (seqNum != i+100) {
      return "Returned packet has the wrong sequence number: " + std::to_string(seqNum);
    }
  }
  
  // Test the case where packets are out of order (in this case, backwards).  We should not receive
  // any packets until we get to the end and then we should get them all.
  for (size_t i = 155; i >= 150; i--) {
    StreamPacket *packetPtr = new StreamPacket;
    std::shared_ptr<StreamPacket> packet(packetPtr);
    packet->SetSequenceNumber(i);
    std::list< std::shared_ptr<StreamPacket> > output = queue.AddPacket(packet);
    if (i == 150) {
      if (output.size() != 6) {
        return "Failed to return enough packets when expected: " + std::to_string(output.size());
      }
      uint32_t expected = 150;
      while (!output.empty()) {
        uint32_t seqNum = SequenceNumber(output.front());
        if (seqNum != expected) {
          return "Returned packet has the wrong sequence number: " + std::to_string(seqNum);
        }
        output.pop_front();
        expected++;
      }
    } else {
      if (output.size() != 0) {
        return "Returned packets when not expected.";
      }
    }
  }

  // Test squashing packets whose sequence number is less than the expected number.
  for (size_t i = 10; i < 20; i++) {
    StreamPacket *packetPtr = new StreamPacket;
    std::shared_ptr<StreamPacket> packet(packetPtr);
    packet->SetSequenceNumber(i);
    std::list< std::shared_ptr<StreamPacket> > output = queue.AddPacket(packet);
    if (output.size() != 0) {
      return "Returned packets when not expected.";
    }
  }
  if (!queue.m_packetList.empty()) {
    return "Packets were not squashed when expected.";
  }

  // Test the case where we exceed the maximum held count.  We should get all packets back when we do so.
  for (size_t i = 200; i < 210; i++) {
    StreamPacket *packetPtr = new StreamPacket;
    std::shared_ptr<StreamPacket> packet(packetPtr);
    packet->SetSequenceNumber(i);
    std::list< std::shared_ptr<StreamPacket> > output = queue.AddPacket(packet);
    if (output.size() != 0) {
      return "Returned packets when not expected before we exceed the maximum held length.";
    }
  }
  {
    StreamPacket *packetPtr = new StreamPacket;
    std::shared_ptr<StreamPacket> packet(packetPtr);
    packet->SetSequenceNumber(210);
    std::list< std::shared_ptr<StreamPacket> > output = queue.AddPacket(packet);
    if (output.size() != 11) {
      return "Failed to return all packets when expected when exceeding the maximum held length.";
    }
  }

  // Test getting a subset of the stored packets when we interleave correct and later packets.
  {
    uint32_t seqNum = 211;
    uint32_t bigSeqNum = 1000;
    for (size_t i = 0; i < 10; i++) {
      StreamPacket* packetPtr = new StreamPacket;
      std::shared_ptr<StreamPacket> packet(packetPtr);
      if (i % 2 == 0) {
        packet->SetSequenceNumber(bigSeqNum++);
      }
      else {
        packet->SetSequenceNumber(seqNum++);
      }
      std::list< std::shared_ptr<StreamPacket> > output = queue.AddPacket(packet);
      if (output.size() != i % 2) {
        return "Returned packet count not as expected when interleaving: " + std::to_string(output.size()) +
          " for packet " + std::to_string(i);
      }
    }
  }

  return "";
}
