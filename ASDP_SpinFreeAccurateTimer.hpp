/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

 /**
 * @file ASDP_SpinFreeAccurateTimer.hpp
 * @brief Apache Strap-Down Pilotage utility class to provide a thread-safe spin-free queue.
 *
 * @author ReliaSolve.
 * @date April 3, 2024.
 */

#include <chrono>
#include <mutex>
#include <atomic>
#include <list>
#include <thread>
#include "ASDP_SpinFreeQueue.hpp"

namespace asdp {

/// @brief A thread-safe spin-free accurate timer (has only one spinning thread for all users).
template <typename T> class SpinFreeAccurateTimer {
public:
  /// @brief Constructor starts the timer thread.
  SpinFreeAccurateTimer() {
    // Start the timer thread.
    t = std::thread([this] { TimerThread(); });
  };

  /// @brief Destructor stop the timer thread.
  ~SpinFreeAccurateTimer() {
    // Signal the timer thread to stop.
    done = true;
    t.join();
  };

  /// @brief Adds a timer action to the list of actions.
  /// @param [in] time The time at which to perform the action.
  /// @param [in] element Element to push onto the queue when the time comes.
  /// @param [in] queue The queue to push an element onto when the time comes.
  void AddEntry(std::chrono::steady_clock::time_point time,
                T element, std::shared_ptr< asdp::SpinFreeQueue<T> > queue) {
    std::lock_guard<std::recursive_mutex> lk(mut);
    // Insert the new entry in increasing-time order.
    auto it = entries.begin();
    while ((it != entries.end()) && (it->time < time)) {
      ++it;
    }
    Entry entry;
    entry.time = time;
    entry.element = element;
    entry.queue = queue;
    entries.insert(it, entry);
  }

private:
  /// @brief An entry describing what to do at a specific time.
  struct Entry {
    std::chrono::steady_clock::time_point time;   ///< The time to fire.
    T element;                                    ///< The entry to push onto the queue.
    std::shared_ptr< asdp::SpinFreeQueue<T> > queue;    ///< Queue to push the entry onto.
  };

  /// @brief The list of entries describing what timer actions to take.
  std::list<Entry> entries;

  /// @brief The mutex to protect the entries list.
  std::recursive_mutex mut;

  /// @brief The done flag to signal the timer thread to exit.
  std::atomic_bool done = false;

  /// @brief The timer thread.
  std::thread t;

  ///@brief The timer thread that spins until the next timer action is due.
  void TimerThread() {
    while (!done) {
      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      // Push all of the elements whose time has come. They are stored in
      // increasing-time order on the list, so we can stop when we reach the first one
      // that has not yet expired.
      std::unique_lock<std::recursive_mutex> lk(mut);
      while (!entries.empty() && (entries.front().time <= now)) {
        entries.front().queue->enqueue(entries.front().element);
        entries.pop_front();
      }
    }
  };
};

/// @brief Test the SpinFreeAccurateTimer class (defined in ASDP_Core_API library).
/// @return An empty string if the test is successful, otherwise an error message.
std::string SpinFreeAccurateTimer_Test();

} // namespace asdp
