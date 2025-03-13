/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <string>
#include <thread>
#include <ASDP_Core_API.h>

int main(int argc, char** argv)
{
  // Parse the command line. It should have three entries, the first two being the string IP address
  // of the NIC to use, the second being the multicast address to use, and the third being the integer
  // port number to listen on.
  if (argc != 4)
  {
    std::cerr << "Usage: " << argv[0] << " <NIC IP Address> <Multicast Address> <Port Number>" << std::endl;
    return -1;
  }
  std::string nic_ip = argv[1];
  std::string multicast_address = argv[2];
  uint16_t port = std::stoi(argv[3]);

  //==========================================================================================
  // Test the case where it should work because we're listening on a multicast address.

  {
    // Open a multicast UDP_Receiver with the specified info and verify that it is working.
    asdp::ReceiverUDP recv(nic_ip, port, 2000, false, multicast_address);
    asdp::Status status = recv.GetConstructorStatus();
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not open listening multicast socket: " << asdp::ErrorMessage(status) << std::endl;
      return 1;
    }

    // Open a multicast UDP_Sender with the specified info and verify that it is working.
    asdp::SenderUDP send(nic_ip, port, false, nic_ip, multicast_address);
    status = send.GetConstructorStatus();
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not open sending multicast socket: " << asdp::ErrorMessage(status) << std::endl;
      return 2;
    }

    // Send a packet and make sure that it is properly received.
    std::string message = "Hello, World!";
    status = send.Send(message.c_str(), message.size());
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not send message: " << asdp::ErrorMessage(status) << std::endl;
      return 3;
    }

    bool packetAvailable = false;
    status = recv.IsPacketAvailable(1, packetAvailable);
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not check for packet availability: " << asdp::ErrorMessage(status) << std::endl;
      return 4;
    }
    if (!packetAvailable)
    {
      std::cerr << "Error: Packet was not available." << std::endl;
      return 5;
    }
    uint8_t buffer[1024];
    size_t bytes_received = 1024;
    status = recv.ReceiveBuffer(buffer, bytes_received);
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not receive message: " << asdp::ErrorMessage(status) << std::endl;
      return 6;
    }
    if (bytes_received != message.size())
    {
      std::cerr << "Error: Received message size does not match sent message size." << std::endl;
      return 7;
    }
    if (memcmp(buffer, message.c_str(), message.size()) != 0)
    {
      std::cerr << "Error: Received message does not match sent message." << std::endl;
      return 8;
    }
  }

  //==========================================================================================
  // Test the case where it should not work because we're listening on a unicast address.

  {
    // Open a unnicast UDP_Receiver with the specified info and verify that it is working.
    asdp::ReceiverUDP recv(nic_ip, port, 2000, false);
    asdp::Status status = recv.GetConstructorStatus();
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not open listening unicast socket: " << asdp::ErrorMessage(status) << std::endl;
      return 11;
    }

    // Open a multicast UDP_Sender with the specified info and verify that it is working.
    asdp::SenderUDP send(nic_ip, port, false, nic_ip, multicast_address);
    status = send.GetConstructorStatus();
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not open sending multicast socket for unicast test: " << asdp::ErrorMessage(status) << std::endl;
      return 12;
    }

    // Send a packet and make sure that it is not received.
    std::string message = "Hello, World!";
    status = send.Send(message.c_str(), message.size());
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not send message for unicast test: " << asdp::ErrorMessage(status) << std::endl;
      return 13;
    }

    bool packetAvailable = false;
    status = recv.IsPacketAvailable(1, packetAvailable);
    if (status != asdp::Status::OKAY)
    {
      std::cerr << "Error: Could not check for packet availability for unicast test: " << asdp::ErrorMessage(status) << std::endl;
      return 14;
    }
    if (packetAvailable)
    {
      std::cerr << "Error: Packet was improperly available." << std::endl;
      return 15;
    }
  }

  std::cout << "Success" << std::endl;
  return 0;
}
