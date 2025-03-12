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
  SpinFreeQueue() : numNodes(0) {
    // This is a dummy node to avoid special cases for empty queue.
    // The fact that the head's next pointer is null means the queue is empty.
    Node* dummy = new Node;
    head.store(dummy);
    tail.store(dummy);
  }

  /// @brief Destructor.
  ~SpinFreeQueue() {
    while (Node* node = head.load()) {
      head.store(node->next.load());
      delete node;
      numNodes.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  /// @brief Enqueues a data item in a thread-safe way.
  /// @param data The data item to enqueue.
  void enqueue(T data) {
    Node* new_node = new Node(data);
    Node* old_tail = tail.load();
    while (true) {
      Node* next = old_tail->next.load();
      if (next == nullptr) {
        if (old_tail->next.compare_exchange_strong(next, new_node)) {
          tail.compare_exchange_strong(old_tail, new_node);
          numNodes.fetch_add(1, std::memory_order_relaxed);
          std::lock_guard<std::mutex> cvlk(dmut);
          dcv.notify_one();
          return;
        }
      } else {
        tail.compare_exchange_strong(old_tail, next);
      }
      old_tail = tail.load();
    }
  }

  /// @brief Dequeues a data item in a thread-safe way with a timeout.
  /// @details This method dequeues a data item from the queue in a thread-safe way
  /// without spin-waiting or blocking indefinitely.  It can thus be used in a thread
  /// that needs to dequeue data items but also needs to perform other tasks or watch
  /// for a done flag.
  /// @param result The data item that was dequeued (if any).
  /// @param timeout The maximum time to wait for a data item to be available.
  /// @return True if a data item is dequeued, false if the timeout is reached.
  bool dequeue(T& result, const std::chrono::milliseconds& timeout) {
    // Wait for data to be available, timing out if necessary.
    {
      std::unique_lock<std::mutex> cvlk(dmut);
      if (!dcv.wait_for(cvlk, timeout, [&] { return numNodes.load() > 0; })) {
        return false;
      }
    }

    Node* old_head = head.load();
    while (true) {
      Node* next = old_head->next.load();
      if (next == nullptr) {
        return false; // Queue is empty
      }
      if (head.compare_exchange_strong(old_head, next)) {
        result = next->data;
        delete old_head;
        numNodes.fetch_sub(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> cvlk(emut);
        ecv.notify_all();
        return true;
      }
      old_head = head.load();
    }
  }

  /// @brief Waits for the queue to be reduced to a specified size with a timeout.
  /// @details This method is used to wait for the queue to be reduced to a specified size
  /// without spin-waiting or blocking indefinitely.  It can thus be used in a thread
  /// that needs to wait for the queue to be reduced to a certain size but also needs to
  /// perform other tasks or watch for a done flag.
  /// @param size The size to wait for.
  /// @param timeout The maximum time to wait.
  /// @return True if the queue is reduced to the specified size, false if the timeout is reached.
  bool awaitEmpty(size_t size, const std::chrono::milliseconds& timeout) {
    std::unique_lock<std::mutex> cvlk(emut);
    if (!ecv.wait_for(cvlk, timeout, [&] { return numNodes.load() <= size; })) {
      return false;
    }
    return true;
  }

  /// @brief Returns the number of nodes in the queue.
  size_t size() const {
    return numNodes.load();
  }

private:
  /// @brief A node in the queue with a pointer to the next node.
  struct Node {
    Node(T value) : data(value), next(nullptr) {};
    Node() = default;

    T data = {};                        ///< The data in the node.
    std::atomic<Node*> next = nullptr;  ///< The next node in the queue, nullptr for the end.
  };

  std::atomic<Node*> head;              ///< The head of the queue, dummy node if empty.
  std::atomic<Node*> tail;              ///< The tail of the queue, dummy node if empty.
  std::atomic<size_t> numNodes;         ///< The number of nodes in the queue.

  /// Condition variable for dequeue to avoid spin-wait for getting data and its mutex
  std::condition_variable dcv;
  std::mutex dmut;

  /// Condition variable for enqueue to avoid spin-wait for queue depletion and its mutex
  std::condition_variable ecv;
  std::mutex emut;
};

/// @brief Test the SpinFreeQueue class (defined in ASDP_Core_API library).
/// @return An empty string if the test is successful, otherwise an error message.
std::string SpinFreeQueue_Test();

} // namespace asdp
