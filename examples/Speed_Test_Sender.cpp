/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <mutex>
#include <functional>
#include <ASDP_Core_API.h>
#include <ASDP_BufferPool.h>
#include <ASDP_SpinFreeQueue.hpp>
using namespace asdp;

typedef SpinFreeQueue< std::shared_ptr< std::vector<uint8_t> > > Queue;

/// @brief Thread to read data from a file and put it in a queue, keeping the queue at least 3 packets full.
/// @details This thread enables overlapping disk reads with sending data.  There is one per file
/// to be read.  It reads data from the file and puts it in the queue, keeping at least 3 packets
/// in the queue until we get to the end of the file or are marked done.
/// @param receiver The receiver to read from
/// @param bytesPerPacket The number of bytes in each packet
/// @param pool The buffer pool to use to get shared pointers to buffers
/// @param queue The queue to put the data in
/// @param done A flag to indicate when we are done reading from the file
static void readFromFileThread(std::shared_ptr<asdp::ReceiverFile> receiver, size_t bytesPerPacket,
  BufferPool &pool,
  std::shared_ptr<Queue> queue,
  std::atomic<bool> &done)
{
  // Read the data from the file and put it in the queue, keeping at least 3 packets in the
  // queue until we get to the end of the file or are marked done.
  while (!done) {
    // Wait until we have less than 3 packets in the queue, timing out after 1 millisecond
    // so that we can check if we are done.
    if (queue->awaitEmpty(3, std::chrono::milliseconds(1))) {
      std::shared_ptr< std::vector<uint8_t> > data = pool.GetBuffer();
      size_t size = data->size();
      Status status = receiver->ReceiveBuffer(data->data(), size);
      data->resize(size);
      if ((status != OKAY) || (size != bytesPerPacket)) {
        // Done reading from the file.
        return;
      }
      queue->enqueue(data);
    }
  }
}

/// @brief A class to call a function when it goes out of scope.
class Finally {
public:
  explicit Finally(std::function<void()> f) : f_(std::move(f)) {}
  ~Finally() { f_(); }

private:
  std::function<void()> f_;
};

/// @brief Thread to send data to a vector of receivers at a specified rate.
/// @param sendSockets The sockets to send data to
/// @param beginSending A flag to indicate when to start sending
/// @param bytesPerPacket The number of bytes in each packet
/// @param totalPackets The total number of packets to send
/// @param packetsPerFrame The number of packets per frame
/// @param packetPeriod The period between packets in seconds
/// @param printMutex A mutex to protect printing
/// @param fileNames A vector of file names to read data from, one per receiver, or empty strings
/// for no files.
static void sendDataThread(std::vector<SenderUDP> sendSockets, std::atomic<bool> &beginSending,
  size_t bytesPerPacket, size_t totalPackets, size_t packetsPerFrame, double packetPeriod,
  std::mutex &printMutex, std::vector<std::string> fileNames)
{
  // Get a vector of data to send in case we're not reading from files
  std::vector< std::vector<uint8_t> > imageDatas;
  for (size_t i = 0; i < sendSockets.size(); ++i) {
    std::vector<uint8_t> imageData(bytesPerPacket);
    imageDatas.push_back(imageData);
  }

  // Get a buffer pool to use for reading from files with 3 buffers per file.
  BufferPool pool(bytesPerPacket, 3 * fileNames.size());

  // Determine the floating-point number of microseconds between packets
  double packetPeriodMicroseconds = packetPeriod * 1e6;

  // If there are files to read, create receivers for them and also a spin-free queue for each
  // along with a thread to read the data and put it in the queue.
  std::atomic_bool done(false);
  std::vector< std::shared_ptr<Queue> > queues;
  std::vector<std::thread> readThreads;
  Finally finally([&]() {
      // Quit and join our file reading threads
      done = true;
      for (auto& thread : readThreads) {
        thread.join();
      }
    });
  for (auto &fileName : fileNames) {
    if (fileName.size() > 0) {
      std::shared_ptr<asdp::ReceiverFile>receiver = std::make_shared<asdp::ReceiverFile>(fileName);
      if (receiver->GetConstructorStatus() != OKAY) {
        std::cerr << "Error creating receiver from file " << fileName
          << ": " << ErrorMessage(receiver->GetConstructorStatus()) << std::endl;
        return;
      }
      std::shared_ptr<Queue> queue = std::make_shared<Queue>();
      queues.push_back(queue);
      readThreads.push_back(std::thread(readFromFileThread, receiver, bytesPerPacket, std::ref(pool),
        queue, std::ref(done)));
    } else {
      queues.push_back(nullptr);
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
      auto& queue = queues[i];
      // Keep the reference valid for the duration of the loop when we read from the queue
      std::shared_ptr< std::vector<uint8_t> > ptr;
      if (queue) {
        size_t size = imageData.size();
        if (!queue->dequeue(ptr, std::chrono::milliseconds(1000))) {
          std::cerr << "Error receiving data from queue" << std::endl;
          return;
        }
        imageData = *ptr;
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
  int width = 1024;
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
    }
    else if (arg == "--port") {
      port = std::stoi(argv[++i]);
    } else if (arg == "--width") {
      width = std::stoi(argv[++i]);
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

  // We send three lines of width pixels of 2 bytes each.
  size_t bytesPerPacket = width * 2 * 3;

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
        fileName = directory + "/" + std::to_string(i*sendsPerThread + j + 1) + ".asdp";
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
