/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

/**
* @file ASDP_BufferPool.h
* @brief Apache Strap-Down Pilotage utility class to provide a pre-allocated pool of buffers.
*
* @author ReliaSolve.
* @date March 14, 2024.
*/

#include <vector>
#include <list>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <string>
#include <atomic>

namespace asdp {

  /// @brief Manages a thread-safe pre-allocated pool of buffers.
  class BufferPool {
  public:
    /// @brief Constructs a buffer pool with the given buffer size and initial number of buffers.
    /// @param bufferSize The size of each buffer in bytes.
    /// @param bufferCount The initial number of buffers in the pool.
    BufferPool(size_t bufferSize, size_t bufferCount);

    /// @brief Destroys the buffer pool after waiting for all outstanding buffers to return to the pool.
    ~BufferPool();

    /// @brief Returns a buffer from the pool, or nullptr if the pool is being destroyed.
    /// @details Returns a buffer from the pool, creating a new buffer if necessary.
    /// When the shared_ptr is destroyed, the buffer is automatically returned to the pool.
    /// The nullptr is returned if the pool is being destroyed.
    /// This method is thread-safe.
    /// @return A buffer from the pool, or nullptr if the pool is being destroyed.
    std::shared_ptr<std::vector<uint8_t>> GetBuffer();

    /// @brief Test the BufferPool class.
    /// @return Empty string on success, descriptive error message on failure.
    static std::string Test();

  private:
    size_t m_bufferSize;
    std::list<std::shared_ptr<std::vector<uint8_t>>> m_freeBuffers;
    size_t m_totalBuffers;
    std::mutex mtx;
    std::atomic_bool m_done;
  };

}