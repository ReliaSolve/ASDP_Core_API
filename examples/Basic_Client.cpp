/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a basic client that sends commands to a server.  It opens a client that listens
// on the specified IP address, connects to and and sends commands to the first server to
// respond.  It does not wait for a response from the server.

#include <iostream>
#include <chrono>
#include <asdp_api.h>

using namespace asdp;

int main(int argc, char** argv)
{
  std::string ip_address;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.  There is a --serial flag to specify
  // the serial number of the server, which defaults to 1.
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " <ip_address>" << std::endl;
        return 2;
    }
  }
  if (realParams != 1) {
    std::cerr << "Usage: " << argv[0] << " <ip_address>" << std::endl;
    return 2;
  }

  // Open a client, specifying the IP address to listen on.
  CoreClient client(ip_address);
  if (client.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to open client: " << ErrorMessage(client.GetConstructorStatus()) << std::endl;
    return 3;
  }
  std::cout << "Listening for servers on " << ip_address << std::endl;

  // Wait for two seconds to allow servers to send Discovery messages and then check the status of
  // the Discover thread and find the servers.
  std::this_thread::sleep_for(std::chrono::seconds(2));
  std::vector<std::string> servers;
  Status threadStatus;
  Status status = client.GetDiscoveryThreadStatus(threadStatus);
  if (status != OKAY) {
    std::cerr << "Failed to get discovery thread status: " << ErrorMessage(status) << std::endl;
    return 4;
  }
  if (threadStatus != OKAY) {
    std::cerr << "Discovery thread status: " << ErrorMessage(threadStatus) << std::endl;
    return 5;
  }
  status = client.IdentifiedServers(servers);
  if (status != OKAY) {
    std::cerr << "Failed to get identified servers: " << ErrorMessage(status) << std::endl;
    return 6;
  }
  if (servers.empty()) {
    std::cerr << "No servers found" << std::endl;
    return 7;
  }
  std::cout << "Servers found: " << servers.size() << std::endl;
  for (const std::string& server : servers) {
    std::cout << "  " << server << std::endl;
  }

  // Connect to the first server found.
  std::cout << "Connecting to " << servers[0] << std::endl;
  uint16_t major, minor, patch;
  status = client.ConnectToServer(servers[0], major, minor, patch);
  if (status != OKAY) {
    std::cerr << "Failed to connect to server: " << ErrorMessage(status) << std::endl;
    return 8;
  }
  std::cout << "  Connected to server version " << major << "." << minor << "." << patch << std::endl;
  uint32_t serialNumber;
  status = client.GetServerSerialNumber(serialNumber);
  if (status != OKAY) {
    std::cerr << "Failed to get server serial number: " << ErrorMessage(status) << std::endl;
    return 9;
  }
  std::cout << "  Connected to server with serial number " << serialNumber << std::endl;

  // Get the main stream receiver
  std::shared_ptr<Receiver> receiver;
  status = client.GetMainStreamReceiver(receiver);
  if (status != OKAY) {
    std::cerr << "Failed to get main stream receiver: " << ErrorMessage(status) << std::endl;
    return 10;
  }

  // Send a few commands to the server, waiting a few seconds in between.
  std::shared_ptr<CommandPacket> command;
  auto start = std::chrono::high_resolution_clock::now();
  size_t i = 0;
  while (i < 3) {

    // See if it is time to send a command.
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - start;
    if (elapsed.count() >= 2.0) {
      std::cout << "  Sending state streaming interval interval command " << i << std::endl;
      status = client.SendCommandPacket(CommandPacketSetStreamStatePeriod(1.5));
      if (status != OKAY) {
        std::cerr << "Failed to send command: " << ErrorMessage(status) << std::endl;
        return 11;
      }
      start = std::chrono::high_resolution_clock::now();
      i++;
    }

    // If we receive a message, print it.
    std::shared_ptr<StreamPacket> response;
    status = receiver->ReceiveStreamPacket(0.01, response);
    while (status == OKAY) {
      std::shared_ptr<Message> message;
      status = response->GetNextMessage(message);
      if (status != OKAY) {
        std::cerr << "Failed to get message from stream packet: " << ErrorMessage(status) << std::endl;
        return 12;
      }
      MessageID type;
      status = message->GetType(type);
      if (status != OKAY) {
        std::cerr << "Failed to get message type: " << ErrorMessage(status) << std::endl;
        return 13;
      }
      std::cout<< "   Received message type: " << type << std::endl;
      status = receiver->ReceiveStreamPacket(0, response);
    }
    if (status != TIMEOUT) {
      std::cerr << "Failed to receive stream packet: " << ErrorMessage(status) << std::endl;
      return 14;
    }    
  }


  return 0;
}
