/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <mutex>
#include <ASDP_Core_API.h>
using namespace asdp;

static void sendDataThread(std::vector<SenderUDP> sendSockets, std::atomic<bool> &beginSending,
  size_t bytesPerPacket, size_t totalPackets, size_t packetsPerFrame, double packetPeriod,
  std::mutex &printMutex, std::vector<std::string> fileNames)
{
  // Get a vector of data to send
  std::vector< std::vector<uint8_t> > imageDatas;
  for (size_t i = 0; i < sendSockets.size(); ++i) {
    std::vector<uint8_t> imageData(bytesPerPacket);
    imageDatas.push_back(imageData);
  }

  // Determine the floating-point number of microseconds between packets
  double packetPeriodMicroseconds = packetPeriod * 1e6;

  std::vector< std::shared_ptr<asdp::ReceiverFile> > receivers;
  for (auto &fileName : fileNames) {
    if (fileName.size() > 0) {
      std::shared_ptr<asdp::ReceiverFile>receiver = std::make_shared<asdp::ReceiverFile>(fileName);
      if (receiver->GetConstructorStatus() != OKAY) {
        std::cerr << "Error creating receiver from file " << fileName
          << ": " << ErrorMessage(receiver->GetConstructorStatus()) << std::endl;
        return;
      }
      receivers.push_back(receiver);
    } else {
      receivers.push_back(nullptr);
    }
  }

  // Wait for the trigger signal and then go
  while (!beginSending) {}

  // Start time is now on the steady clock
  std::chrono::time_point<std::chrono::steady_clock> startTime = std::chrono::steady_clock::now();

  // Loop through the packets and send each when it is time
  size_t missedDurations = 0;
  size_t largestMissedDuration = 0;
  for (int packetNum = 0; packetNum < totalPackets; ++packetNum) {

    // Determine the time when the next packet should be sent by adding the
    // number of packets sent times the period between packets to the start time
    std::chrono::time_point<std::chrono::steady_clock> nextPacketTime =
      startTime + std::chrono::microseconds(static_cast<uint64_t>(packetNum * packetPeriodMicroseconds));

    // Wait until the next packet should be sent
    std::chrono::time_point<std::chrono::steady_clock> now;
    do {
      now = std::chrono::steady_clock::now();
    } while (std::chrono::steady_clock::now() < nextPacketTime);

    // Report if we missed an entire frame.
    auto interval = std::chrono::duration_cast<std::chrono::microseconds>(now - nextPacketTime);
    if (interval > std::chrono::microseconds(static_cast<uint64_t>(packetPeriodMicroseconds))) {
      size_t missed = static_cast<uint32_t>(interval.count() / packetPeriodMicroseconds);
      missedDurations += missed;
      if (missed > largestMissedDuration) {
        largestMissedDuration = missed;
      }
    }

    if (false) {
      std::lock_guard<std::mutex> lock(printMutex);
      std::cout << "Sent" << std::endl;
    }

    // Read or fill in and send the data
    for (size_t i = 0; i < sendSockets.size(); ++i) {
      auto& imageData = imageDatas[i];
      auto& receiver = receivers[i];
      if (receiver) {
        size_t size = imageData.size();
        Status status = receiver->ReceiveBuffer(imageData.data(), size);
        imageData.resize(size);
        if (status != OKAY) {
          std::cerr << "Error receiving data: " << ErrorMessage(status) << std::endl;
          return;
        }
      } else {
        // Fill the first entry in the image data with the packet number mod 256
        imageData[0] = (packetNum) % 256;
      }

      uint8_t* data = imageData.data();
      auto& sendSocket = sendSockets[i];
      Status status = sendSocket.Send(data, bytesPerPacket);
      if (status != OKAY) {
        std::cerr << "Error sending data: " << ErrorMessage(status) << std::endl;
        return;
      }
    }
  }

  // Report how many packets per second were sent and tell about any missed durations
  auto now = std::chrono::steady_clock::now();
  double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(now - startTime).count();
  std::lock_guard<std::mutex> lock(printMutex);
  std::cout << "Sent for " << seconds << " sec: "
    << totalPackets / seconds << " p/s; " << totalPackets / seconds / packetsPerFrame
    << " frames/s to each of " << sendSockets.size() << " cameras" << std::endl;
  if (missedDurations) {
    std::cout << "*** Missed " << missedDurations << " packet durations, largest single gap = "
      << largestMissedDuration << std::endl;
  }
}

int main(int argc, char* argv[])
{
  int cameras = 25;
  int threads = 5;
  float fps = 60.0;
  float secondsWorth = 10;
  std::string IP = "localhost";
  int port = 12000;
  std::string NICName;
  std::string directory;
  size_t realParams = 0;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--cameras") {
      cameras = std::stoi(argv[++i]);
    } else if (arg == "--threads") {
      threads = std::stoi(argv[++i]);
    } else if (arg == "--fps") {
      fps = std::stof(argv[++i]);
    } else if (arg == "--secondsWorth") {
      secondsWorth = std::stof(argv[++i]);
    } else if (arg == "--IP") {
      IP = argv[++i];
    } else if (arg == "--port") {
      port = std::stoi(argv[++i]);
    } else if (arg == "--NIC") {
      NICName = argv[++i];
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

  if ((cameras / threads) * threads != cameras) {
    std::cerr << "Threads must divide the number of cameras" << std::endl;
    return 2;
  }

  std::cout << "ASDP Speed Test Sender" << std::endl;
  std::cout << "Sends data to a Speed_Test_Receiver at the requested rate" << std::endl;
  std::cout << "Run this after running the receiver." << std::endl;
  std::cout << "Usage: Speed_Test_Sender [--cameras <number>] [--threads <number>] [--fps <number>] [--secondsWorth <number>] [--IP <string>] [--port <number>] [--NIC <string>] [directory]" << std::endl;
  std::cout << "       It sends to the port specified and a number above it for each camera." << std::endl;
  std::cout << "The parameters here must match those used by the receiver except for threads must evenly divide cameras." << std::endl;
  std::cout << "NICName specifies the IP address on a NIC to be used for sending, default uses the system-selected port" << std::endl;
  std::cout << "If directory is not specified, the data is all zeroes." << std::endl;
  std::cout << "If directory is specified, the data is read from files in that directory." << std::endl;
  std::cout << std::endl;
  std::cout << "Cameras: " << cameras << std::endl;
  std::cout << "Threads: " << threads << std::endl;
  std::cout << "FPS: " << fps << std::endl;
  std::cout << "Seconds worth of data: " << secondsWorth << std::endl;
  std::cout << "Sending to IP:Port and following: " << IP << ":" << port << std::endl;
  if (NICName.size() > 0) {
    std::cout << "Using NIC: " << NICName << std::endl;
  }
  if (directory.size() > 0) {
    std::cout << "Reading data from files in " << directory << std::endl;
  }

  // Compute the total number of packets to send, where we send 342 packets per frame.
  size_t packetsPerFrame = 342;
  size_t totalPacketsPerCamera = static_cast<size_t>(fps * secondsWorth) * packetsPerFrame;

  // We send three lines of 1024 pixels of 2 bytes each.
  size_t bytesPerPacket = 1024 * 2 * 3;

  // Create sockets for sending and receiving. There is a batch of them for each sending thread.
  std::vector< std::vector<SenderUDP> > sendSockets;
  std::vector< std::vector<std::string> > fileNames;
  size_t sendsPerThread = cameras / threads;
  for (unsigned i = 0; i < threads; i++) {
    std::vector<SenderUDP> mySendSockets;
    std::vector<std::string> myFileNames;
    for (unsigned j = 0; j < sendsPerThread; j++) {
      mySendSockets.push_back(SenderUDP(IP, port + j + i * sendsPerThread));
      if (mySendSockets.back().GetConstructorStatus() != OKAY) {
        std::cerr << "Error creating send socket: " << mySendSockets.back().GetConstructorStatus() << std::endl;
        return 1;
      }
      std::string fileName;
      if (directory.size() > 0) {
        fileName = directory + "/" + std::to_string(i*threads + j + 1) + ".asdp";
      }
      myFileNames.push_back(fileName);
    }
    sendSockets.push_back(mySendSockets);
    fileNames.push_back(myFileNames);
  }

  // Start the specified number of threads and wait a second for them to get ready.
  std::atomic<bool> beginSending(false);
  std::mutex printMutex;
  std::vector<std::thread> senders;
  for (int i = 0; i < threads; ++i) {
    SenderUDP sendSocket(IP, port + i, false, NICName);
    senders.push_back(std::thread(sendDataThread, sendSockets[i], std::ref(beginSending),
            bytesPerPacket, totalPacketsPerCamera, packetsPerFrame,
            1.0 / (packetsPerFrame * fps), std::ref(printMutex), fileNames[i]));
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Start the threads and wait for them to all quit.
  std::cout << std::endl << "Starting threads to send " << totalPacketsPerCamera
    << " packets per camera" << std::endl;
  beginSending = true;
  for (int i = 0; i < threads; ++i) {
    senders[i].join();
  }

  // Clean up resources
  senders.clear();
  sendSockets.clear();

  return 0;
}
