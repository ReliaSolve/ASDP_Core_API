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
#include <condition_variable>
#include <atomic>
#include <list>
#include <thread>

namespace asdp {

/// @brief A thread-safe spin-free accurate timer (has only one spinning thread for all users).
class SpinFreeAccurateTimer {
private:
  /// @brief An entry describing what to do at a specific time.
  struct Entry {
    std::chrono::steady_clock::time_point time;   ///< The time to fire.
    std::shared_ptr<std::condition_variable> cv;  ///< Condition variable for the time to fire.
  };

  /// @brief The list of entries describing what timer actions to take.
  std::list<Entry> entries;

  /// @brief The mutex to protect the entries list.
  std::mutex mut;

  /// @brief The done flag to signal the timer thread to exit.
  std::atomic_bool done = false;

  /// @brief The timer thread.
  std::thread t;

  ///@brief The timer thread that spins until the next timer action is due.
  void timerThread() {
    while (!done) {
      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      // Notify all of the condition variables whose time has come. They are stored in
      // increasing-time order on the list, so we can stop when we reach the first one
      // that has not yet expired.
      std::unique_lock<std::mutex> lk(mut);
      while (!entries.empty() && (entries.front().time <= now)) {
        entries.front().cv->notify_all();
        entries.pop_front();
      }
    }
  }

public:
  /// @brief Constructor starts the timer thread.
  SpinFreeAccurateTimer();

  /// @brief Destructor stop the timer thread.
  ~SpinFreeAccurateTimer();

  /// @brief Adds a timer action to the list of actions.
  /// @param [in] time The time at which to perform the action.
  /// @param [in] cv The condition variable to notify when the time comes.
  void AddEntry(std::chrono::steady_clock::time_point time, std::shared_ptr<std::condition_variable> cv);

  /// @brief Test the SpinFreeAccurateTimer class.
  /// @return An empty string if the test is successful, otherwise an error message.
  static std::string Test();
};

} // namespace asdp
