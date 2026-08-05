/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

 /**
 * @file ASDP_SpinFreeQueue.hpp
 * @brief Apache Strap-Down Pilotage utility class to provide a thread-safe spin-free queue.
 *
 * @author ReliaSolve.
 * @date March 15, 2024.
 */

#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace asdp {

/// @brief A thread-safe spin-free queue.
template <typename T> class SpinFreeQueue {
public:
  /// @brief Constructor.
  SpinFreeQueue() : head(nullptr), tail(nullptr), nodes(0) { }

  /// @brief Destructor.
  ~SpinFreeQueue() {
    std::lock_guard<std::mutex> lk(mut);
    while (head) {
      Node* old_head = head;
      head = old_head->next;
      delete old_head;
      nodes--;
    }
    head = tail = nullptr;
  }

  /// @brief Enqueues a data item in a thread-safe way.
  void enqueue(T data) {
    Node* new_node = new Node;
    new_node->data = std::move(data);
    new_node->next = nullptr;
    { // Hold the lock for as short a time as possible
      std::lock_guard<std::mutex> lk(mut);
      if (nodes == 0) {
        head = new_node;
        tail = new_node;
      } else {
        tail->next = new_node;
        tail = new_node;
      }
      nodes++;
    }
    dcv.notify_all();
  }

  /// @brief Waits for the queue to be reduced to a specified size with a timeout.
  /// @details This method is used to wait for the queue to be reduced to a specified size
  /// without spin-waiting or blocking indefinitely.  It can thus be used in a thread
  /// that needs to wait for the queue to be reduced to a certain size but also needs to
  /// perform other tasks or watch for a done flag.
  /// @param size The size to wait for.
  /// @param timeout The maximum time to wait.
  /// @return True if the queue is reduced to the specified size, false if the timeout is reached.
  bool awaitEmpty(size_t size, const std::chrono::microseconds& timeout) {
    std::unique_lock<std::mutex> cvlk(mut);
    if (!ecv.wait_for(cvlk, timeout, [&] { return nodes <= size; })) {
      return false;
    }
    return true;
  }

  /// @brief Dequeues a data item from the head of the queue in a thread-safe way with a timeout.
  /// @details This method dequeues a data item from the queue in a thread-safe way
  /// without spin-waiting or blocking indefinitely.  It can thus be used in a thread
  /// that needs to dequeue data items but also needs to perform other tasks or watch
  /// for a done flag.
  /// @param value The data item that was dequeued (if any).
  /// @param timeout The maximum time to wait for a data item to be available.
  /// @return True if a data item is dequeued, false if the timeout is reached.
  bool dequeue(T& value, const std::chrono::microseconds& timeout) {
    Node* old_head;
    { // Hold the lock for as short a time as possible, moving other operations outside.
      std::unique_lock<std::mutex> cvlk(mut);
      if (!dcv.wait_for(cvlk, timeout, [&] { return nodes != 0; })) {
        return false;
      }

      value = head->data;
      old_head = head;
      head = old_head->next;
      if (head == nullptr) {
        tail = head;
      }
      nodes--;
    }
    ecv.notify_all();
    delete old_head;
    return true;
  }

  /// @brief Returns the number of nodes in the queue.
  size_t size() const {
    return nodes;
  }

private:
  /// @brief A node in the queue with a pointer to the next node.
  struct Node {
    T data = { };           ///< The data in the node.
    Node* next = nullptr;   ///< The next node in the queue, nullptr for the end.
  };

  Node* head;               ///< The head of the queue, nullptr if empty.
  Node* tail;               ///< The tail of the queue, nullptr if empty.
  std::atomic<size_t> nodes;///< The number of nodes in the queue.

  /// Condition variable and its mutex for dequeue to avoid spin-wait for getting data
  std::condition_variable dcv;

  /// Condition variable and its mutex for enqueue to avoid spin-wait for queue depletion
  std::condition_variable ecv;

  /// Mutex for adjusting the queue and for the condition variables
  std::mutex mut;

};

/// @brief Test the SpinFreeQueue class (defined in ASDP_Core_API library).
/// @return An empty string if the test is successful, otherwise an error message.
std::string SpinFreeQueue_Test();

} // namespace asdp
