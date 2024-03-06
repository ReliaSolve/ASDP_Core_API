/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <mutex>
#include <list>
#include <string.h>
#include <ASDP_Core_API.h>
using namespace asdp;

#include <atomic>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <mutex>

template<typename T>
class LockFreeQueue {
private:
  struct Node {
    T data;
    std::atomic<Node*> next;

    Node(T value) : data(value), next(nullptr) {}
  };

  std::atomic<Node*> head;
  std::atomic<Node*> tail;
  std::condition_variable cv;
  std::mutex cv_m;
  std::atomic<size_t> count;

public:
  LockFreeQueue() {
    Node* newNode = new Node(T());
    head.store(newNode, std::memory_order_relaxed);
    tail.store(newNode, std::memory_order_relaxed);
    count.store(0, std::memory_order_relaxed);
  }

  ~LockFreeQueue() {
    while (Node* node = head.load(std::memory_order_relaxed)) {
      head.store(node->next, std::memory_order_relaxed);
      delete node;
    }
  }

  void enqueue(T value) {
    Node* newNode = new Node(value);
    Node* prevTail = tail.exchange(newNode, std::memory_order_acq_rel);

    prevTail->next.store(newNode, std::memory_order_release);

    cv.notify_one();

    count.fetch_add(1, std::memory_order_relaxed);
  }

  bool dequeue(T& value, const std::chrono::milliseconds& timeout) {
    Node* node = head.load(std::memory_order_relaxed);
    auto start = std::chrono::high_resolution_clock::now();
    while (node != tail.load(std::memory_order_acquire)) {
      if (head.compare_exchange_weak(node, node->next, std::memory_order_relaxed)) {
        value = node->next.load(std::memory_order_relaxed)->data;
        delete node;
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
      }
      node = head.load(std::memory_order_relaxed);

      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::milli> elapsed = end - start;
      if (elapsed > timeout) {
        return false;
      }
    }

    std::unique_lock<std::mutex> lk(cv_m);
    if (cv.wait_for(lk, timeout, [&] { return node != tail.load(std::memory_order_acquire); })) {
      if (head.compare_exchange_weak(node, node->next, std::memory_order_relaxed)) {
        value = node->next.load(std::memory_order_relaxed)->data;
        delete node;
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
      }
    }
    return false;
  }

  size_t size() const {
    return count.load(std::memory_order_relaxed);
  }
};


/// @brief Separate thread per receive-data thread to write data to file.
/// @details This is used to enable the receive thread to continue receiving data while the
/// file is being written to disk, enabling the system to keep up with the incoming data.
/// @param done Atomic boolean to signal the thread to stop.
/// @param sender The sender object to use to write data to file.
/// @param queue The queue of data to write to file.
/// @return None
static void saveDataThread(std::atomic<bool>& done,
  std::shared_ptr<asdp::SenderFile> sender,
  LockFreeQueue< std::shared_ptr< std::vector<uint8_t> > >& queue)
{
  std::shared_ptr< std::vector<uint8_t> > data;
  while (!done) {
    if (queue.dequeue(data, std::chrono::milliseconds(100))) {
      sender->Send(data->data(), data->size());
    }
  }
}

static void receiveDataThread(ReceiverUDP& receiveSocket, size_t bytesPerPacket, size_t totalPackets,
  std::mutex& printMutex, std::atomic<bool> &broken, std::string fileName)
{
  std::vector<uint8_t> buffer(bytesPerPacket);
  unsigned packetsReceived = 0;
  std::vector<char> copyBuffer(bytesPerPacket);

  // Thread to handle saving data to file and associated resources
  std::thread saveThread;
  std::atomic<bool> done(false);
  LockFreeQueue< std::shared_ptr< std::vector<uint8_t> > > queue;

  std::shared_ptr<asdp::SenderFile> sender;
  if (fileName.size() > 0) {
    sender = std::make_shared<asdp::SenderFile>(fileName);
    if (sender->GetConstructorStatus() != OKAY) {
      std::cerr << "Error creating sender to file " << fileName
        << ": " << ErrorMessage(sender->GetConstructorStatus()) << std::endl;
      broken = true;
      return;
    }
    saveThread = std::thread(saveDataThread, std::ref(done), sender, std::ref(queue));
  }

  // Loop through and receive packets until we've gotten them all or an error occurs
  while (packetsReceived < totalPackets) {
    Status status = receiveSocket.ReceiveBuffer(buffer);
    if (status != OKAY) {
      std::cerr << "Error receiving data: " << ErrorMessage(status) << std::endl;
      return;
    }

    // Verify that the data is correct and we haven't missed any packets
    if (buffer[0] != (packetsReceived % 256)) {
      std::lock_guard<std::mutex> lock(printMutex);
      std::cerr << "Error: Expected " << (packetsReceived % 256) << " but got " << (int)buffer[0] << std::endl;
      broken = true;
      return;
    }

    if (sender) {
      // Copy the data to file.
      std::shared_ptr< std::vector<uint8_t> > data = std::make_shared< std::vector<uint8_t> >(buffer);
      queue.enqueue(data);
    } else {
      // Here, we check the data and then copy it to an external buffer on the heap, which would be a
      // pinned GPU memory buffer for the real code.
      memcpy(copyBuffer.data(), buffer.data(), bytesPerPacket);
    }

    // Increment the number of packets received
    packetsReceived++;
  }

  // If we have a thread, time how long it takes it to finish
  if (saveThread.joinable()) {
    size_t queueSize = queue.size();
    std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
    done = true;
    saveThread.join();
    std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::lock_guard<std::mutex> lock(printMutex);
    std::cout << "Save thread had " << queueSize << " items in the queue" << std::endl;
    std::cout << "  Time to save data: " << elapsed.count() << " seconds" << std::endl;
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
