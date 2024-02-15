/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a basic server that listens for commands from a client.  It opens a server
// on the specified IP address and listens for commands from the client.  It prints
// the op code of each command it receives.

#include <iostream>
#include <chrono>
#include <algorithm>
#include <asdp_api.h>

using namespace asdp;

int main(int argc, char** argv)
{
  std::string ip_address;
  uint32_t serial_number = 1;
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
    } else if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " [--serial N] <ip_address>" << std::endl;
        return 2;
    }
  }
  if (realParams != 1) {
    std::cerr << "Usage: " << argv[0] << " [--serial N] <ip_address>" << std::endl;
    return 2;
  }

  // Open a server, specifying the serial number and IP address to broadcast on.
  CoreServer server(serial_number, ip_address);
  if (server.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to open server: " << ErrorMessage(server.GetConstructorStatus()) << std::endl;
    return 3;
  }
  std::cout << "Server opened on " << ip_address << " with serial number " << serial_number << std::endl;

  // Currently-active clients.
  std::vector<std::shared_ptr<CoreServer::ClientInfo>> myClients;

  // Loop forever, getting Commands from the client and printing them.
  while (true) {
    Status status;


    // If the discovery thread fails, we should stop.
    Status discoveryStatus;
    status = server.GetDiscoveryThreadStatus(discoveryStatus);
    if (status != OKAY) {
      std::cerr << "Failed to get discovery thread status: " << ErrorMessage(status) << std::endl;
      return 5;
    }
    if (discoveryStatus != OKAY) {
      std::cerr << "Discovery thread failed: " << ErrorMessage(discoveryStatus) << std::endl;
      return 6;
    }

    // See if we have any new clients.
    std::vector<std::shared_ptr<CoreServer::ClientInfo>> newClients;
    status = server.GetNewTCPLinks(newClients);
    if (status != OKAY) {
      std::cerr << "Failed to get new TCP links: " << ErrorMessage(status) << std::endl;
      return 7;
    }
    for (auto newClient : newClients) {
      std::cout << "Got new client, version "
        << newClient->major << "." << newClient->minor << "." << newClient->patch << std::endl;
      myClients.push_back(newClient);
    }

    // Busy wait on commands for all clients.
    for (auto receiver : myClients) {
      std::shared_ptr<CommandPacket> command;
      status = receiver->stream->ReceiveCommandPacket(0.5, command);
      if (status == TIMEOUT) {
        // Loop again to try to get a command.
        continue;
      }
      if (status != OKAY) {
        // This client has closed, so we should remove it from the list.
        std::cout << "Closing client connection. An actual client should close all related image streams as well." << std::endl;
        myClients.erase(std::remove(myClients.begin(), myClients.end(), receiver), myClients.end());
        continue;
      }
      OpCode opCode;
      status = command->GetOpCode(opCode);
      if (status != OKAY) {
        std::cerr << "Failed to get op code: " << ErrorMessage(status) << std::endl;
        return 8;
      }
      std::cout << "  Received command code: " << opCode << std::endl;
    }
  }

  return 0;
}
