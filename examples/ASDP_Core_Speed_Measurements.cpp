/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <chrono>
#include <asdp_api.h>

using namespace asdp;

int main(int argc, char** argv)
{
  const int totalIterations = 1000000;

  std::cout << "Timing CommandPacketStreamSubregions construct/destroy" << std::endl;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < totalIterations; ++i) {
    uint32_t IP = 0x01020304;
    uint16_t port = 1234;
    SubregionDescription region1 = { 1, 2, 3, 4, 5, 6 };
    SubregionDescription region2 = { 7, 8, 9,10,11,12 };
    std::vector<SubregionDescription> regions = { region1, region2 };
    CommandPacketStreamSubregions packet({ IP, port }, regions);
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  std::cout << "  Duration: " << duration.count() << " seconds" << std::endl;
  double fps = totalIterations / duration.count();
  std::cout << "  Average frames per second: " << fps << std::endl;

  return 0;
}
