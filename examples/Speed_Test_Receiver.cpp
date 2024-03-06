/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <mutex>
#include <string.h>
#include <ASDP_Core_API.h>
using namespace asdp;

static void receiveDataThread(ReceiverUDP& receiveSocket, size_t bytesPerPacket, size_t totalPackets,
  std::mutex& printMutex, std::atomic<bool> &broken, std::string fileName)
{
  std::vector<uint8_t> buffer(bytesPerPacket);
  unsigned packetsReceived = 0;
  std::vector<char> copyBuffer(bytesPerPacket);

  std::shared_ptr<asdp::SenderFile> sender;
  if (fileName.size() > 0) {
    sender = std::make_shared<asdp::SenderFile>(fileName);
    if (sender->GetConstructorStatus() != OKAY) {
      std::cerr << "Error creating sender to file " << fileName
        << ": " << ErrorMessage(sender->GetConstructorStatus()) << std::endl;
      broken = true;
      return;
    }
  }

  // Loop through and receive packets until we've gotten them all or an error occurs
  while (packetsReceived < totalPackets) {
    Status status = receiveSocket.ReceiveBuffer(buffer);
    if (status != OKAY) {
      std::cerr << "Error receiving data: " << ErrorMessage(status) << std::endl;
      return;
    }

    // Process the received data (replace this with your processing logic).
    // Here, we check the data and then copy it to an external buffer on the heap, which would be a
    // pinned GPU memory buffer for the real code.
    if (buffer[0] != (packetsReceived % 256)) {
      std::lock_guard<std::mutex> lock(printMutex);
      std::cerr << "Error: Expected " << (packetsReceived % 256) << " but got " << (int)buffer[0] << std::endl;
      broken = true;
      return;
    }

    if (sender) {
      sender->Send(buffer.data(), bytesPerPacket);
    } else {
      memcpy(copyBuffer.data(), buffer.data(), bytesPerPacket);
    }

    // Increment the number of packets received
    packetsReceived++;
  }
}

int main(int argc, char* argv[])
{
  int cameras = 25;
  float fps = 60.0;
  int secondsWorth = 10;
  std::string IP = "localhost";
  int port = 12000;
  std::string directory;
  size_t realParams = 0;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--cameras") {
      cameras = std::stoi(argv[++i]);
    } else if (arg == "--fps") {
      fps = std::stof(argv[++i]);
    } else if (arg == "--secondsWorth") {
      secondsWorth = std::stoi(argv[++i]);
    } else if (arg == "--IP") {
      IP = argv[++i];
    } else if (arg == "--port") {
      port = std::stoi(argv[++i]);
    } else if (arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << std::endl;
      return 1;
    } else {
      ++realParams;
      switch (realParams) {
      case 1:
        directory = arg;
        break;
      default:
        std::cerr << "Unexpected argument: " << arg << std::endl;
        return 1;
      }
    }
  }

  std::cout << "ASDP Speed Test Receiver" << std::endl;
  std::cout << "Listens for data from the Speed_Test_Sender and checks for dropped packets" << std::endl;
  std::cout << "Run this before running the sender." << std::endl;
  std::cout << "Usage: Speed_Test_Receiver [--cameras <number>] [--fps <number>] [--secondsWorth <number>] [--IP <string>] [--port <number>] [directory]" << std::endl;
  std::cout << "       It listens on the port specified and a number above it for each camera." << std::endl;
  std::cout << "The parameters here must match those used by the sender." << std::endl;
  std::cout << "If directory is not specified, the data is copied to a memory buffer" << std::endl;
  std::cout << "If directory is /dev/null or NUL:, the data is written to the null device" << std::endl;
  std::cout << "If directory is specified, the data is written to files in that directory" << std::endl;
  std::cout << std::endl;
  std::cout << "Cameras: " << cameras << std::endl;
  std::cout << "FPS: " << fps << std::endl;
  std::cout << "Seconds worth of data: " << secondsWorth << std::endl;
  std::cout << "Listening on IP:Port and following " << IP << ":" << port << std::endl;
  if (directory.size() > 0) {
    std::cout << "Writing data to files in " << directory << std::endl;
  }

  // Compute the total number of packets to receive, where we send 342 packets per frame.
  size_t packetsPerFrame = 342;
  size_t totalPacketsPerCamera = static_cast<size_t>(fps * secondsWorth) * packetsPerFrame;

  // We receive three lines of 1024 pixels of 2 bytes each.
  size_t bytesPerPacket = 1024 * 2 * 3;

  // Create the receive sockets.
  std::vector<ReceiverUDP> receiveSockets;
  for (unsigned i = 0; i < cameras; i++) {
    receiveSockets.push_back(ReceiverUDP(IP, port + i));
    if (receiveSockets.back().GetConstructorStatus() != OKAY) {
      std::cerr << "Error creating receive socket: " << receiveSockets.back().GetConstructorStatus() << std::endl;
      return 2;
    }
  }

  // Start the specified number of threads.
  std::mutex printMutex;
  std::atomic<bool> broken(false);
  std::vector<std::thread> receivers;
  for (unsigned i = 0; i < cameras; i++) {
    std::string fileName;
    if (directory.size() > 0) {
      fileName = directory + "/" + std::to_string(i + 1) + ".asdp";
      if ((directory == "/dev/null") || (directory == "NUL:")) {
        fileName = directory;
      }
    }
    std::thread receiver(receiveDataThread, std::ref(receiveSockets[i]), bytesPerPacket,
      totalPacketsPerCamera, std::ref(printMutex), std::ref(broken), fileName);
    receivers.push_back(std::move(receiver));
  }

  // Wait for the threads to finish.
  for (unsigned i = 0; i < cameras; i++) {
    receivers[i].join();
  }

  // Check for any errors
  if (broken) {
    std::cerr << "Error: Packets were dropped" << std::endl;
    return 3;
  } else {
    std::cout << "Success" << std::endl;
  }

  // Clean up resources
  receivers.clear();
  receiveSockets.clear();

  return 0;
}
