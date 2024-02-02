/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <chrono>
#include <asdp_api.h>

using namespace asdp;

const size_t imageWidth = 1280;
const size_t imageHeight = 1024;
const size_t bitsPerPixel = 16;
const float linesPerPacket = 3.0f;  // We need 0.5f to fit in a 1500 MTU, 3.0 can fit in 9000 MTU
const size_t bytesPerLine = (imageWidth * bitsPerPixel) / 8;
const size_t bytesPerPacket = bytesPerLine * linesPerPacket;
const size_t packetsPerFrame = imageHeight / linesPerPacket;
const size_t numCameras = 25;
const size_t numReadThreads = 25;
const size_t numWriteThreads = 5;
const size_t numFrames = numCameras / numReadThreads;
const size_t packetsPerIteration = numFrames * packetsPerFrame;
const size_t totalPacketIterations = 60;

std::atomic<int> totalPacketsReceived(0);
std::atomic<bool> beginSending(false);

void sendDataThread(std::vector<SenderUDP> sendSockets) {
  std::vector<uint8_t> imageData(bytesPerPacket);

  // Wait for the trigger signal and then go
  while (!beginSending) {}
  for (int iteration = 0; iteration < totalPacketIterations; ++iteration) {
    for (int packetNum = 0; packetNum < packetsPerIteration; ++packetNum) {
      // Prepare image data (replace this with your image data source)
      // For simplicity, we use a placeholder array here
      uint8_t* data = imageData.data();
      // Fill imageData with actual image data
      imageData[0] = (packetNum + iteration * packetsPerIteration) % 128;

      // Send the data
      for (unsigned i = 0; i < sendSockets.size(); i++) {
        Status status = sendSockets[i].Send(data, bytesPerPacket);
        if (status != OKAY) {
          std::cerr << "Error sending data: " << ErrorMessage(status) << std::endl;
          return;
        }
      }
    }
  }
}

void receiveDataThread(ReceiverUDP socket) {
  std::vector<uint8_t> buffer(bytesPerPacket);
  unsigned packetsReceived = 0;
  std::vector<char> copyBuffer(bytesPerPacket);

  // Wait for the trigger signal and then go
  while (!beginSending) {}
  while (true) {
    // Receive a data packet if there is one ready
    bool dataAvailable;
    Status status = socket.IsPacketAvailable(0.5, dataAvailable);
    if (status != OKAY) {
      std::cerr << "Error checking for data: " << ErrorMessage(status) << std::endl;
      totalPacketsReceived += packetsReceived;
      return;
    }
    if (!dataAvailable) {
      continue;
    }
    status = socket.ReceiveBuffer(buffer);
    if (status != OKAY) {
      std::cerr << "Error receiving data: " << ErrorMessage(status) << std::endl;
      totalPacketsReceived += packetsReceived;
      return;
    }

    if (buffer.size() != bytesPerPacket) {
      std::cerr << "Missed some data: " << buffer.size() << " of " << bytesPerPacket << std::endl;
      totalPacketsReceived += packetsReceived;
      return;
    }

    // Process the received data (replace this with your processing logic).
    // Here, we check the data and then copy it to an external buffer on the heap, which would be a
    // pinned GPU memory buffer for the real code.
    if (buffer[0] != (packetsReceived % 128)) {
      std::cerr << "Error: Expected " << (packetsReceived % 128) << " but got " << (int)buffer[0] << std::endl;
      totalPacketsReceived += packetsReceived;
      break;
    }
    memcpy(copyBuffer.data(), buffer.data(), bytesPerPacket);

    // Increment the total frames received
    packetsReceived++;

    // Check if it's the last frame
    if (packetsReceived == packetsPerIteration * totalPacketIterations) {
      totalPacketsReceived += packetsReceived;
      break;
    }
  }
}


int main(int argc, char** argv)
{
  const int totalCreateIterations = 1000000;

  //===========================================================================
  std::cout << "Timing CommandPacketStreamSubregions construct/destroy" << std::endl;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < totalCreateIterations; ++i) {
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
  double fps = totalCreateIterations / duration.count();
  std::cout << "  Average construct/destroy per second: " << fps << std::endl;

  //===========================================================================
  std::cout << std::endl << "Timing multi-threaded packet send/receive as raw buffers" << std::endl;
  int ret = 0;

  // Check the parameters
  if (numFrames * numReadThreads != numCameras) {
    std::cerr << "numReadThreads must evenly divide numCameras" << std::endl;
    return 100;
  }
  if ((numReadThreads / numWriteThreads) * numWriteThreads != numReadThreads) {
    std::cerr << "numWriteThreads must evenly divide numReadThreads" << std::endl;
    return 101;
  }
  if (bytesPerPacket / linesPerPacket != bytesPerLine) {
    std::cerr << "linesPerPacket must evenly divide bytesPerLine" << std::endl;
    return 102;
  }

  // Create sockets for sending and receiving. There is a batch of them for each sending thread.
  uint16_t basePort = 12345;
  std::vector< std::vector<SenderUDP> > sendSockets;
  size_t sendsPerThread = numReadThreads / numWriteThreads;
  for (unsigned i = 0; i < numWriteThreads; i++) {
    std::vector<SenderUDP> mySendSockets;
    for (unsigned j = 0; j < sendsPerThread; j++) {
      mySendSockets.push_back(SenderUDP("localhost", basePort + j + i * sendsPerThread));
      if (mySendSockets.back().GetConstructorStatus() != OKAY) {
        std::cerr << "Error creating send socket: " << mySendSockets.back().GetConstructorStatus() << std::endl;
        return 1;
      }
    }
    sendSockets.push_back(mySendSockets);
  }
  std::vector<ReceiverUDP> receiveSockets;
  for (unsigned i = 0; i < numReadThreads; i++) {
    receiveSockets.push_back(ReceiverUDP("localhost", basePort + i));
    if (receiveSockets.back().GetConstructorStatus() != OKAY) {
      std::cerr << "Error creating receive socket: " << receiveSockets.back().GetConstructorStatus() << std::endl;
      return 2;
    }
  }

  std::cout << "  Running with " << sendSockets.size() << " write threads"
    << " and " << receiveSockets.size() << " read threads" << std::endl;

  std::cout << "  Expecting " << numReadThreads * packetsPerIteration * totalPacketIterations << " total packets"
    << " of size " << bytesPerPacket << " bytes" << std::endl;

  // Start the receiver threads and sender threads
  std::vector<std::thread> receiverThreads;
  for (unsigned i = 0; i < numReadThreads; i++) {
    std::thread receiverThread(receiveDataThread, receiveSockets[i]);
    receiverThreads.push_back(std::move(receiverThread));
  }
  std::vector<std::thread> senderThreads;
  for (unsigned i = 0; i < numWriteThreads; i++) {
    unsigned stride = numCameras / (numReadThreads / numWriteThreads);
    unsigned offset = i;
    std::thread senderThread(sendDataThread, sendSockets[i]);
    senderThreads.push_back(std::move(senderThread));
  }

  // Sleep to enable send and receive threads to start
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Measure the time taken for sending all iterations, starting the
  // clock just before allowing the threads to run.
  start = std::chrono::high_resolution_clock::now();
  beginSending = true;

  // Wait for the sender and receiver threads to finish
  for (unsigned i = 0; i < receiverThreads.size(); i++) {
    receiverThreads[i].join();
  }
  for (unsigned i = 0; i < senderThreads.size(); i++) {
    senderThreads[i].join();
  }

  end = std::chrono::high_resolution_clock::now();
  duration = end - start;

  std::cout << "  Total time taken: " << duration.count() << " seconds\n";
  std::cout << "  Average time per iteration: " << duration.count() / totalPacketIterations << " seconds\n";
  fps = totalPacketIterations / duration.count();
  std::cout << "  Average frames per second: " << fps << std::endl;
  std::cout << "  Total packets received: " << totalPacketsReceived << std::endl;
  if (fps < 60.0) {
    std::cerr << std::endl << "Error: Average frames per second is less than 60" << std::endl;
    ret = 10;
  }
  if (totalPacketsReceived != numReadThreads * packetsPerIteration * totalPacketIterations) {
    std::cerr << std::endl << "Error: Total packets received does not match expected value" << std::endl;
    ret = 11;
  }

  // Clean up resources
  receiverThreads.clear();
  senderThreads.clear();
  receiveSockets.clear();
  sendSockets.clear();

  return ret;
}
