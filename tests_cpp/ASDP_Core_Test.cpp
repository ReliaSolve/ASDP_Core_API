/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <ASDP_Core_API.h>
#include <ASDP_BufferPool.h>

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
    std::cerr << "Buffer_Pool Error: " << ret << std::endl;
    return 2;
  }

  std::cout << "Success" << std::endl;
  return 0;
}
