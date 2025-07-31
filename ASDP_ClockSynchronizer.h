/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

#include <memory>
#include <chrono>
#include <list>
#include <cstdint>
#include "ASDP_Core_API.h"

/**
* @file ASDP_ClockSynchronizer.h
* @brief Apache Strap-Down Pilotage utility class to provide robust Timer offset and gains
* based on CLOCK_SYNC messages.
*
* @author ReliaSolve.
* @date October 9, 2024.
*/

namespace asdp {

  /// @brief Provides robust Timer offset and gain compared to a local clock based on
  /// CLOCK_SYNC messages or local measurements of the Bsp time.
  class ClockSynchronizer {
  public:
    /// @brief Constructs a clock synchronizer and tells what Timer it should manage.
    /// @param timer The Timer to adjust
    /// @param transmissionDelay The expected delay between the server packing its CLOCK_SYNC
    /// message and the client unpacking it.
    ClockSynchronizer(std::shared_ptr<Timer> timer, Time transmissionDelay = {});

    /// @brief Destroys the clock synchronizer.
    virtual ~ClockSynchronizer() = default;

    /// @brief Clear the history of the synchronizer without changing its current gain and offset.
    void ClearHistory();

    /// @brief Add a new data point to the synchronizer.
    /// @details If this is the first data point since construction or ClearHistory(), the
    /// synchronizer will set its offset based on the single difference.
    /// Up to a block of 100 data points, it will base its offset on the maximum delay in the block
    /// or in up to 10 accumulated blocks.
    /// After 10 blocks of 100 data points, it will base its offset on a least-squares
    /// line fit to the whole set of blocks, keeping the maximums for at most 100 blocks and
    /// then dropping older values.
    /// @param serverTime The time in the CLOCK_SYNC message.
    /// @param localTime The time on the local clock when the client unpacked the CLOCK_SYNC message.
    /// @return True if the data point was added, false if there was an error.
    bool AddDataPoint(Time serverTime,
      const std::chrono::steady_clock::time_point localTime = std::chrono::steady_clock::now());

    /// @brief Test the ClockSynchronizer class.
    /// @return Empty string on success, descriptive error message on failure.
    static std::string Test();

  private:
    std::shared_ptr<Timer> m_timer; ///< The Timer to adjust.
    /// The expected delay in microseconds between the server packing its CLOCK_SYNC message and the client unpacking it.
    int64_t m_transmissionDelay;

    /// @brief Compute the positive or negative offset in microseconds between the server and local times.
    /// @details The offset is the difference between the server and the local times minus the transmission delay.
    /// @param serverTime The time in the CLOCK_SYNC message.
    /// @param localTime The time on the local clock when the client unpacked the CLOCK_SYNC message.
    /// @return The offset in microseconds, whether positive or negative.
    int64_t ComputeOffsetMicroseconds(Time serverTime, const std::chrono::steady_clock::time_point localTime) const;

    std::list<int64_t> m_offsetsInBlock; ///< The history of offsets within the current block.
    struct BlockRecord {
      std::chrono::steady_clock::time_point lastPointTime; ///< The time of the last point in the block.
      int64_t maxOffset; ///< The maximum offset in the block.
    };
    std::list<BlockRecord> m_BlockOffsets; ///< The history of offsets across blocks.
  };

}