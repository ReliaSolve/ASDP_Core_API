/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include "ASDP_BufferPool.h"
#include <thread>

using namespace asdp;

BufferPool::BufferPool(size_t bufferSize, size_t bufferCount)
  : m_bufferSize(bufferSize)
  , m_totalBuffers(bufferCount)
  , m_done(false)
{
  // Fill the buffer pool with buffers.  The count has been set above.
  for (size_t i = 0; i < bufferCount; ++i) {
    m_freeBuffers.push_back(std::make_shared<std::vector<uint8_t>>(bufferSize));
  }
}

BufferPool::~BufferPool()
{
  // Wait for all the buffers to be returned to the pool.  Because we are setting
  // m_done to true, no more buffers will be allocated in any threads while we're
  // waiting for the existing buffers to be returned.
  m_done = true;
  while (m_freeBuffers.size() != m_totalBuffers) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Clear the buffer pool while holding the lock.
  std::lock_guard<std::mutex> lock(mtx);
  m_freeBuffers.clear();
}

std::shared_ptr<std::vector<uint8_t>> BufferPool::GetBuffer()
{
  // If we are being destroyed, then we can't return any more buffers
  if (m_done) {
    return nullptr;
  }

  // If the buffer pool is empty, then we need to allocate a new buffer.
  std::lock_guard<std::mutex> lock(mtx);
  if (m_freeBuffers.empty()) {
    m_freeBuffers.push_back(std::make_shared<std::vector<uint8_t>>(m_bufferSize));
    m_totalBuffers++;
  }

  // Get a buffer from the pool and return it to the caller.
  std::shared_ptr<std::vector<uint8_t>> buffer = m_freeBuffers.front();
  m_freeBuffers.pop_front();

  // Make a shared_ptr that will return the buffer to the pool when it is destroyed.
  return std::shared_ptr<std::vector<uint8_t>>(buffer.get(), [this, buffer](std::vector<uint8_t>*) {
      std::lock_guard<std::mutex> lock(mtx);
      m_freeBuffers.push_back(buffer);
    });
}

static void TestBufferPoolAllocationThread(BufferPool* pool, double tryPeriod, int tryTimes,
  int* successCount, std::atomic_bool *running)
{
  *running = true;
  std::vector< std::shared_ptr< std::vector<uint8_t> > > buffers;
  for (int i = 0; i < tryTimes; i++) {
    std::shared_ptr<std::vector<uint8_t>> buffer = pool->GetBuffer();
    buffers.push_back(buffer);
    if (buffer != nullptr) {
      (*successCount)++;
    }
    std::this_thread::sleep_for(std::chrono::microseconds((int)(tryPeriod * 1e6)));
  }

  // Buffers will be cleared when the function returns.
}

std::string BufferPool::Test()
{
  // Single-threaded test of the buffer pool.  Verify that the buffer pool size
  // adjusts as expected.
  {
    BufferPool pool(100, 10);
    if (pool.m_freeBuffers.size() != 10) {
      return "BufferPool::Test() failed: pool.m_freeBuffers.size() != 10";
    }

    // Check as we allocate free buffers and then get more than are available.
    std::vector < std::shared_ptr< std::vector<uint8_t> > > buffers;
    for (size_t i = 0; i < 20; i++) {
      size_t expected = (i < 9) ? 9 - i : 0;
      std::shared_ptr<std::vector<uint8_t>> buffer = pool.GetBuffer();
      buffers.push_back(buffer);
      if (pool.m_freeBuffers.size() != expected) {
        return "Allocation failed: pool.m_freeBuffers.size() != " + std::to_string(expected);
      }
      if (buffer->size() != 100) {
        return "Allocation failed: buffer->size() != 100";
      }
    }

    // Check after we return all buffers.
    buffers.clear();
    if (pool.m_freeBuffers.size() != 20) {
      return "Freeing failed: pool.m_freeBuffers.size() != 20";
    }
  }

  // Multi-threaded test of the buffer pool.  Verify that the GetBuffer() method
  // returns nullptr when the pool is being destroyed and that the destructor
  // waits for all buffers to be returned to the pool.
  {
    BufferPool *pool = new BufferPool(100, 10);
    int worked = 0;
    std::atomic_bool running(false);

    // Start a thread that will run for one second and allocate one buffer per tenth of a second
    std::thread getThread(TestBufferPoolAllocationThread, pool, 0.1, 10, &worked, &running);
    while (!running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Wait half a second and then destroy the pool.  The thread should continue to run
    // and try to allocate buffers, but the pool should not allocate any more buffers.
    std::this_thread::sleep_for(std::chrono::microseconds(500000));
    delete pool;
    getThread.join();

    // Verify that the thread allocated some but not all of the buffers.
    if ((worked == 0) || (worked == 10)) {
      return "Multi-threaded test failed: worked = " + std::to_string(worked);
    }
  }

  // Everything worked.
  return "";
}
