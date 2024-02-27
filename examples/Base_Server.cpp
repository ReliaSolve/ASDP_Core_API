/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a more complex server than the Basic_Server.cpp program.  It instantates
// a CoreServerBase object, which provides more functionality.

#include <iostream>
#include <chrono>
#include <ASDP_Core_API.h>

using namespace asdp;

void Usage(const char* name)
{
  std::cerr << "Usage: " << name << " [--serial N] [--verbosity V] <ip_address>" << std::endl;
  exit(1);
}

int main(int argc, char** argv)
{
  std::string ip_address;
  uint32_t serial_number = 1;
  int verbosity = 100;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to broadcast on.  There is a --serial flag to specify
  // the serial number of the server, which defaults to 1.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--serial") {
      if (i + 1 < argc) {
        serial_number = std::stoi(argv[i + 1]);
        ++i;
      } else {
        std::cerr << "--serial flag requires an argument" << std::endl;
        return 1;
      }
    } else if (std::string(argv[i]) == "--verbosity") {
      if (i + 1 < argc) {
        verbosity = std::stoi(argv[i + 1]);
        ++i;
      }
      else {
        std::cerr << "--verbosity flag requires an argument" << std::endl;
        return 1;
      }
    } else if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        Usage(argv[0]);
        return 2;
    }
  }
  if (realParams != 1) {
    Usage(argv[0]);
    return 2;
  }

  // Open a server, specifying the serial number and IP address to broadcast on.
  CoreServerBase server(serial_number, ip_address, 10102, 10101, 9000 - 28, verbosity);
  if (server.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to open server: " << ErrorMessage(server.GetConstructorStatus()) << std::endl;
    return 3;
  }
  std::cout << "Server opened on " << ip_address << " with serial number " << serial_number << std::endl;
  std::string ret = server.run();
  if (ret.size() > 0) {
    std::cerr << "Server failed: " << ret << std::endl;
    return 4;
  }

  return 0;
}
