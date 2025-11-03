/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

/**
* @file ASDP_StreamPacketSortedQueue.h
* @brief Apache Strap-Down Pilotage utility class to fully sort an incoming stream of StreamPackets.
*
* @author ReliaSolve.
* @date September 20th, 2024.
*/

#include <string>
#include <list>
#include <memory>
#include "ASDP_Core_API.h"

namespace asdp {

  /// @brief Handles the fact that UDP packets can be reordered during send/receive.
  /// @details This class is used to fully sort an incoming stream of StreamPackets.
  /// It handles the fact that UDP packets can be reordered during send/receive.
  /// It is fed StreamPackets in any order, and it will return them in order.
  /// It has a threshold for the maximum number of packets that can be held before
  /// delivery so that it will eventually skip dropped packets.
  /// This class is not thread-safe
  class StreamPacketSortedQueue {
  public:
    /// @brief Constructs a buffer pool with the given buffer size and initial number of buffers.
    /// @param maxHeldCount The maximum number of packets that can be held before delivery.
    StreamPacketSortedQueue(size_t maxHeldCount);

    /// @brief Destroys the queue and all packets in it.
    ~StreamPacketSortedQueue();

    /// @brief Adds a packet to the queue.
    /// @details Adds a packet to the queue, which will be returned in order.
    /// @return A vector of packets that are now in order and ready to be processed.  May be empty.
    /// This method is not thread-safe.
    std::list< std::shared_ptr<StreamPacket> > AddPacket(std::shared_ptr<StreamPacket> streamPacket);

    /// @brief Test the StreamPacketSortedQueue class.
    /// @return Empty string on success, descriptive error message on failure.
    static std::string Test();

  protected:
    size_t m_maxHeldCount;  ///< Maximum number of packets to hold before delivery.
    uint32_t m_nextExpectedSequenceNumber;  ///< Next expected sequence number.
    std::list< std::shared_ptr<StreamPacket> > m_packetList;  ///< List of packets with increasing sequence number.
  };

} // namespace asdp
