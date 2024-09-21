/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <ASDP_Core_API.h>
#include <ASDP_BufferPool.h>
#include <ASDP_ImageSource.h>
#include <ASDP_StreamPacketSortedQueue.h>
#include <ASDP_SpinFreeQueue.hpp>
#include <ASDP_SpinFreeAccurateTimer.hpp>

int main(int argc, char** argv)
{
  // Test the core API
  std::string ret = asdp::Test();
  if (ret.size() > 0) {
    std::cerr << "Core Error: " << ret << std::endl;
    return 1;
  }

  // Test all utility classes.
  ret = asdp::BufferPool::Test();
  if (ret.size() > 0) {
    std::cerr << "BufferPool Error: " << ret << std::endl;
    return 2;
  }
  ret = asdp::ImageSource::Test();
  if (ret.size() > 0) {
    std::cerr << "ImageSource Error: " << ret << std::endl;
    return 3;
  }
  ret = asdp::StreamPacketSortedQueue::Test();
  if (ret.size() > 0) {
    std::cerr << "StreamPacketSortedQueue Error: " << ret << std::endl;
    return 4;
  }
  ret = asdp::SpinFreeQueue_Test();
  if (ret.size() > 0) {
    std::cerr << "SpinFreeQueue Error: " << ret << std::endl;
    return 5;
  }
  ret = asdp::SpinFreeAccurateTimer_Test();
  if (ret.size() > 0) {
    std::cerr << "SpinFreeAccurateTimer Error: " << ret << std::endl;
    return 6;
  }

  std::cout << "Success" << std::endl;
  return 0;
}
