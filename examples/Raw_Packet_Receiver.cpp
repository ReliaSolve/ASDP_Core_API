/*
 * Copyright (C) 2025: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <list>
#include <string.h>
#include <ASDP_Core_API.h>
#include <ASDP_BufferPool.h>
#include <ASDP_SpinFreeQueue.hpp>
using namespace asdp;

/// @brief Separate thread per receive-data thread to write data to file.
/// @details This is used to enable the receive thread to continue receiving data while the
/// file is being written to disk, enabling the system to keep up with the incoming data.
/// @param done Atomic boolean to signal the thread to stop.
/// @param sender The sender object to use to write data to file.
/// @param queue The queue of data to write to file.
/// @return None
static void saveDataThread(std::atomic<bool>& done,
  std::shared_ptr<asdp::SenderFile> sender,
  SpinFreeQueue< std::shared_ptr< std::vector<uint8_t> > >& queue)
{
  std::shared_ptr< std::vector<uint8_t> > data;
  while (!done) {
    if (queue.dequeue(data, std::chrono::milliseconds(100))) {
      sender->Send(data->data(), data->size());
    }
  }
}

static void receiveDataThread(ReceiverUDP& receiveSocket, size_t maxBytesPerPacket, size_t totalPackets,
  std::mutex& printMutex, std::atomic<bool> &broken, std::string fileName)
{
  // Generate a buffer pool to use to get pre-allocated buffers for reading the data from
  // the network and then sending it to the disk-write thread without copying it.  Initially
  // fill it with 1024 buffers.  It will automatically expand if needed.
  BufferPool bufferPool(maxBytesPerPacket, 1024);

  // Accumulate multiple packets into a buffer and then write it in blocks
  // We have a "copy buffer" that we hand off to when we're not saving to disk.
  std::shared_ptr< std::vector<uint8_t> > buffer = bufferPool.GetBuffer();
  std::shared_ptr< std::vector<uint8_t> > copyBuffer;
  unsigned packetsReceived = 0;

  // Thread to handle saving data to file and associated resources
  std::thread saveThread;
  std::atomic<bool> done(false);
  SpinFreeQueue< std::shared_ptr< std::vector<uint8_t> > > queue;

  std::shared_ptr<asdp::SenderFile> sender;
  if (fileName.size() > 0) {
    // Open the sender file not in direct-write mode, so we can write packets of any size.
    sender = std::make_shared<asdp::SenderFile>(fileName, false);
    if (sender->GetConstructorStatus() != OKAY) {
      std::cerr << "Error creating sender to file " << fileName
        << ": " << ErrorMessage(sender->GetConstructorStatus()) << std::endl;
      broken = true;
      return;
    }
    saveThread = std::thread(saveDataThread, std::ref(done), sender, std::ref(queue));
  }

  // Loop through and receive packets until we've gotten them all or an error occurs.
  // These packets are all multiples of the sector size, so we don't need to worry about
  // partial writes to disk.
  while (packetsReceived < totalPackets) {

    // Get a new buffer to use to receive the data into and write to file.
    buffer = bufferPool.GetBuffer();
    buffer->reserve(maxBytesPerPacket);

    // Copy into the buffer.
    size_t size = maxBytesPerPacket;
    Status status = receiveSocket.ReceiveBuffer(buffer->data(), size);
    if (size == 0) {
      std::lock_guard<std::mutex> lock(printMutex);
      std::cerr << "Error: Received " << size << " bytes out of max " << buffer->size() << std::endl;
      broken = true;
      break;
    }
    if (status != OKAY) {
      std::cerr << "Error receiving data: " << ErrorMessage(status) << std::endl;
      break;
    }
    buffer->resize(size);

    if (sender) {
      // Copy the data to file.
      queue.enqueue(buffer);
    } else {
      // Here, we check the data and then copy it to an external buffer on the heap, which would be a
      // pinned GPU memory buffer for the real code.
      copyBuffer = buffer;
    }

    // Increment the number of packets received
    packetsReceived++;
  }

  // If we have a thread, time how long it takes it to finish writing everything to disk.
  if (saveThread.joinable()) {
    size_t queueSize = queue.size();
    std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
    // Wait for the queue to empty
    while (queue.size() > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    done = true;
    saveThread.join();
    std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::lock_guard<std::mutex> lock(printMutex);
    std::cout << "Save thread had " << queueSize << " items in the queue.  "
      << "Time to save data: " << elapsed.count() << " seconds" << std::endl;
  }
}

int main(int argc, char* argv[])
{
  int cameras = 1;
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
    }
    else if (arg == "--port") {
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

  // We receive three lines of width pixels of 2 bytes each.
  size_t maxBytesPerPacket = 9000;

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
    std::thread receiver(receiveDataThread, std::ref(receiveSockets[i]), maxBytesPerPacket,
      totalPacketsPerCamera, std::ref(printMutex), std::ref(broken), fileName);
    receivers.push_back(std::move(receiver));
  }

  // Wait for the threads to finish.
  for (unsigned i = 0; i < cameras; i++) {
    receivers[i].join();
  }

  // Check for any errors
  int ret = 0;
  if (broken) {
    std::cerr << "Error: Packets were dropped" << std::endl;
   ret = 3;
  } else {
    std::cout << "Success" << std::endl;
  }

  // Clean up resources
  receivers.clear();
  receiveSockets.clear();

  return ret;
}
