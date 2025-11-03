/*
 * Copyright (C) 2025: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first Analysis server it encounters and prints every message.

#include <iostream>
#include <ASDP_Core_API.h>

using namespace asdp;

//==============================================================================

void usage(const char* name)
{
  std::cerr << "Usage: " << name << " [options]" << std::endl;
  std::cerr << "  options:" << std::endl;
  std::cerr << "    --server URL         Server to connect to, default tcp://localhost:10103" << std::endl;
  std::cerr << "                         (e.g. file://infile.json)" << std::endl;
}

int main(int argc, char** argv)
{
  std::string serverURL = "tcp://localhost:10103";
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == std::string("--server")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --server" << std::endl;
        usage(argv[0]);
        return 1;
      }
      serverURL = argv[i];
    }
    else if (argv[i][0] == '-') {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      usage(argv[0]);
      return 1;
    }
    else switch (realParams++) {
    case 0:
    default:
      usage(argv[0]);
      return 2;
    }
  }
  if (realParams != 0) {
    usage(argv[0]);
    return 2;
  }

  // Create the client
  std::shared_ptr<JSONStringReceiver> client;
  Status s = JSONStringReceiver::Create(serverURL, client);
  if (s != OKAY) {
    std::cerr << "Error creating JSONStringReceiver: " << ErrorMessage(s) << std::endl;
    std::cerr << "    (Did you start the server?)" << std::endl;
    return 3;
  }
  std::cout << "Connected to server at " << serverURL << std::endl;

  // Loop forever, printing every message received
  while (true) {
    std::string jsonString;
    s = client->Receive(0.5, jsonString);
    if (s == TIMEOUT) {
      continue;
    }
    if (s != OKAY) {
      std::cerr << "Error getting next JSON string: " << ErrorMessage(s) << std::endl;
      return 4;
    }
    std::cout << "Received JSON string: " << jsonString << std::endl;
  }
}
