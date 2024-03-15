/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

#include <chrono>
#include <mutex>
#include <condition_variable>

template <typename T> class SpinFreeQueue {
private:
  struct Node {
    T data;
    Node* next = nullptr;
  };

  Node* head;
  Node* tail;
  size_t nodes;

  // Condition variable for dequeue to avoid spin-wait for getting data
  std::condition_variable dcv;
  std::mutex dcv_m;

  // Condition variable for enqueue to avoid spin-wait for queue depletion
  std::condition_variable ecv;
  std::mutex ecv_m;

  // Mutes for the queue
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
      } else {
        tail->next = new_node;
        tail = new_node;
      }

      nodes++;
    }
    dcv.notify_one();
  }

  bool awaitEmpty(size_t size, const std::chrono::milliseconds& timeout) {
    std::unique_lock<std::mutex> cvlk(ecv_m);
    if (!ecv.wait_for(cvlk, timeout, [&] { return nodes <= size; })) {
      return false;
    }
    return true;
  }

  bool dequeue(T& value, const std::chrono::milliseconds& timeout) {
    if (nodes == 0) {
      std::unique_lock<std::mutex> cvlk(dcv_m);
      if (!dcv.wait_for(cvlk, timeout, [&] { return nodes != 0; })) {
        return false;
      }
    }

    {
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
    }
    ecv.notify_all();
    return true;
  }

  size_t size() const {
    return nodes;
  }
};
