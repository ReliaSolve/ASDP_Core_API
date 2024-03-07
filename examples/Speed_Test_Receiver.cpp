/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
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
using namespace asdp;

template <typename T> class SpinFreeQueue {
private:
  struct Node {
    T data;
    Node* next;
  };

  Node* head;
  Node* tail;
  size_t nodes;
  std::condition_variable cv;
  std::mutex cv_m;
  std::mutex mut;

public:
  SpinFreeQueue() {
    head = nullptr;
    tail = nullptr;
    nodes = 0;
  }

  ~SpinFreeQueue() {
    std::lock_guard<std::mutex> lk(mut);
    while (head) {
      Node* old_head = head;
      head = old_head->next;
      delete old_head;
      nodes--;
    }
  }

  void enqueue(T data) {
    {
      std::lock_guard<std::mutex> lk(mut);
      Node* new_node = new Node;
      new_node->data = data;
      new_node->next = nullptr;

      if (nodes == 0) {
        head = new_node;
        tail = new_node;
      }
      else {
        tail->next = new_node;
        tail = new_node;
      }

      nodes++;
    }
    cv.notify_one();
  }

  bool dequeue(T& value, const std::chrono::milliseconds& timeout) {
    if (nodes == 0) {
      std::unique_lock<std::mutex> cvlk(cv_m);
      if (!cv.wait_for(cvlk, timeout, [&] { return nodes != 0; })) {
        return false;
      }
    }

    std::lock_guard<std::mutex> lk(mut);
    if (nodes == 0) {
      return false;
    }
    value = head->data;
    Node* old_head = head;
    head = old_head->next;
    delete old_head;
    nodes--;
    if (head == nullptr) {
      tail = head;
    }
    return true;
  }

  size_t size() const {
    return nodes;
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
  SpinFreeQueue< std::shared_ptr< std::vector<uint8_t> > >& queue)
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
  SpinFreeQueue< std::shared_ptr< std::vector<uint8_t> > > queue;

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
      break;
    }

    // Verify that the data is correct and we haven't missed any packets
    if (buffer[0] != (packetsReceived % 256)) {
      std::lock_guard<std::mutex> lock(printMutex);
      std::cerr << "Error: Expected " << (packetsReceived % 256) << " but got " << (int)buffer[0] << std::endl;
      broken = true;
      break;
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
