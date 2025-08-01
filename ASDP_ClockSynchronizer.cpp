/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include "ASDP_ClockSynchronizer.h"
#include <utility>
#include <vector>
#include <iostream>

using namespace asdp;

ClockSynchronizer::ClockSynchronizer(std::shared_ptr<Timer> timer, Time transmissionDelay)
  : m_timer(timer)
  , m_transmissionDelay(transmissionDelay.seconds * int64_t(1000000) + transmissionDelay.microseconds)
{
}

void ClockSynchronizer::ClearHistory()
{
  m_offsetsInBlock.clear();
  m_BlockOffsets.clear();
}

int64_t ClockSynchronizer::ComputeOffsetMicroseconds(Time serverTime, const std::chrono::steady_clock::time_point localTime) const
{
  int64_t localTimeMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(localTime.time_since_epoch()).count();
  int64_t serverTimeMicroseconds = serverTime.seconds * int64_t(1000000) + serverTime.microseconds;
  // The transmission delay caused an increase in our local time, which was subtracted from the server time.
  // We add back in the expected transmission delay to get the actual offset.
  return serverTimeMicroseconds - localTimeMicroseconds + m_transmissionDelay;
}

// Function to perform least squares fit
static std::pair<double, double> leastSquaresFit(const std::vector<std::pair<double, double>>& points)
{
  double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
  int n = points.size();

  for (const auto& point : points) {
    double x = point.first;
    double y = point.second;
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }

  double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
  double intercept = (sumY - slope * sumX) / n;

  return { slope, intercept };
}

bool ClockSynchronizer::AddDataPoint(Time serverTime, const std::chrono::steady_clock::time_point localTime)
{
  // Compute the offset and push it into the current block.
  int64_t offsetMicroseconds = ComputeOffsetMicroseconds(serverTime, localTime);
  m_offsetsInBlock.push_back(offsetMicroseconds);

  // Find the maximum offset in the current block.
  int64_t maxBlockOffsetMicroseconds = offsetMicroseconds;
  for (int64_t o : m_offsetsInBlock) {
    if (o > maxBlockOffsetMicroseconds) {
      maxBlockOffsetMicroseconds = o;
    }
  }

  // If we are the maximum in the block, record our time as the maximum offset.
  // This will be true for the first entry in the block, and may be true for subsequent entries.
  if (maxBlockOffsetMicroseconds == offsetMicroseconds) {
    m_blockMaxOffsetTime = localTime;
  }

  // If the current block is full, add it to the history and clear it.
  if (m_offsetsInBlock.size() >= 100) {
    m_BlockOffsets.push_back({ maxBlockOffsetMicroseconds, m_blockMaxOffsetTime });
    m_offsetsInBlock.clear();
  }

  // Find the maximum of all the current blocks; if there are fewer than 10, we'll use this to set the offset.
  int64_t maxOffset = maxBlockOffsetMicroseconds;
  for (auto const &block : m_BlockOffsets) {
    if (block.maxOffset > maxOffset) {
      maxOffset = block.maxOffset;
    }
  }

  // If we have at least 10 blocks, compute the least-squares line fit to the whole set of blocks,
  // indexed by time before now -- the intercept is the offset.
  if (m_BlockOffsets.size() >= 10) {
    // Make a vector of (X,Y) pairs, where X is the time in microseconds between the max time in the block
    // and the current time and Y is the difference between maxOffset and the current offset (measured now).
    // This gives us the best baseline for both of these values, keeping them near zero.
    std::vector<std::pair<double, double>> points;
    for (auto const &block : m_BlockOffsets) {
      int64_t time = std::chrono::duration_cast<std::chrono::microseconds>(localTime - block.maxOffsetTime).count();
      points.push_back({static_cast<double>(time), static_cast<double>(block.maxOffset - offsetMicroseconds)});
    }

    // Compute the least-squares line fit.  Add its intercept to the current offset.  This is the expected
    // difference between the server and local times at differential time zero (which is now).
    std::pair<double, double> fit = leastSquaresFit(points);
    maxOffset = offsetMicroseconds + fit.second;
  }

  // If we have more than 100 blocks, drop the oldest one.
  while (m_BlockOffsets.size() > 100) {
    m_BlockOffsets.pop_front();
  }

  // Set the timer offset based on what we determined it to be above.
  Status status = OKAY;
  if (maxOffset < 0) {
    status = m_timer->SetCoreNegativeOffset(Time( (-maxOffset) / 1000000, (-maxOffset) % 1000000));
    if (status != OKAY) { return false; }
    status = m_timer->SetCorePositiveOffset(Time(0, 0));
  } else {
    status = m_timer->SetCorePositiveOffset(Time(maxOffset / 1000000, maxOffset % 1000000));
    if (status != OKAY) { return false; }
    status = m_timer->SetCoreNegativeOffset(Time(0, 0));
  }
  return status == OKAY;
}

std::string ClockSynchronizer::Test()
{
  // Test leastSquaresFit
  {
    std::vector<std::pair<double, double>> points = {
      {1.0, 2.0},
      {2.0, 3.0},
      {3.0, 4.0},
      {4.0, 5.0},
      {5.0, 6.0}
    };
    auto fit = leastSquaresFit(points);
    if (fit.first != 1.0 || fit.second != 1.0) {
      return "leastSquaresFit failed: slope = " + std::to_string(fit.first) + ", intercept = " + std::to_string(fit.second);
    }
  }

  // Test the initial offset computation for a single positive and single negative offset.
  {
    Timer* tPtr = new Timer;
    std::shared_ptr<Timer> timer(tPtr);
    ClockSynchronizer synchronizer(timer, Time(0,0));
    auto now = std::chrono::steady_clock::now();
    double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    Time positive(nowSeconds + 1, 0);
    if (!synchronizer.AddDataPoint(positive, now)) {
      return "Initial positive offset AddDataPoint() failed";
    }
    Time coreTime;
    Status status = timer->GetCoreTime(coreTime, now);
    if (status != OKAY) {
      return "Initial positive offset GetCoreTime() failed";
    }
    // We should have truncated the microseoconds to 0 and increased the seconds by 1.
    if ((coreTime.seconds != nowSeconds + 1) || (coreTime.microseconds != 0)) {
      return "Initial positive offset failed";
    }
  }
  {
    Timer* tPtr = new Timer;
    std::shared_ptr<Timer> timer(tPtr);
    ClockSynchronizer synchronizer(timer, Time(0, 0));
    auto now = std::chrono::steady_clock::now();
    double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    Time negative(nowSeconds - 1, 0);
    if (!synchronizer.AddDataPoint(negative, now)) {
      return "Initial negative offset AddDataPoint() failed";
    }
    Time coreTime;
    Status status = timer->GetCoreTime(coreTime, now);
    if (status != OKAY) {
      return "Initial negative offset GetCoreTime() failed";
    }
    // We should have truncated the microseoconds to 0 and increased the seconds by 1.
    if ((coreTime.seconds != nowSeconds - 1) || (coreTime.microseconds != 0)) {
      return "Initial negative offset failed";
    }
  }

  // Test the use of a transmission delay.
  {
    Timer* tPtr = new Timer;
    std::shared_ptr<Timer> timer(tPtr);
    ClockSynchronizer synchronizer(timer, Time(3, 0));
    auto now = std::chrono::steady_clock::now();
    double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    Time positive(nowSeconds + 1, 0);
    if (!synchronizer.AddDataPoint(positive, now)) {
      return "Transmission delay AddDataPoint() failed";
    }
    Time coreTime;
    Status status = timer->GetCoreTime(coreTime, now);
    if (status != OKAY) {
      return "Transmission delay GetCoreTime() failed";
    }
    // We should have increased the seconds by a net of 4 (+1 then +3 for transmission delay).
    if (coreTime.seconds != nowSeconds + 4) {
      return "Transmission delay failed";
    }
  }

  // Test adding ten points and ensure that we're using the most-positive offset.
  {
    Timer* tPtr = new Timer;
    std::shared_ptr<Timer> timer(tPtr);
    ClockSynchronizer synchronizer(timer, Time(0, 0));
    auto now = std::chrono::steady_clock::now();
    double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    Time positive(nowSeconds + 1, 0);
    Time negative(nowSeconds - 1, 0);
    // Fill in the negative offset on the first point, then positive on the rest.
    if (!synchronizer.AddDataPoint(negative, now)) {
      return "Ten points AddDataPoint() failed";
    }
    for (int i = 0; i < 9; i++) {
      if (!synchronizer.AddDataPoint(positive, now)) {
        return "Ten points AddDataPoint() failed";
      }
    }
    Time coreTime;
    Status status = timer->GetCoreTime(coreTime, now);
    if (status != OKAY) {
      return "Ten points GetCoreTime() failed";
    }
    // We should have increased the seconds by 1.
    if (coreTime.seconds != nowSeconds + 1) {
      return "Ten points failed";
    }
  }

  // Test adding 900 points (9 blocks) and ensure that we're using the most-positive offset.
  {
    Timer* tPtr = new Timer;
    std::shared_ptr<Timer> timer(tPtr);
    ClockSynchronizer synchronizer(timer, Time(0, 0));
    auto now = std::chrono::steady_clock::now();
    double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    Time positive(nowSeconds + 1, 0);
    Time negative(nowSeconds - 1, 0);
    // Fill in the negative offset on the first point, then positive on the rest.
    if (!synchronizer.AddDataPoint(negative, now)) {
      return "900 points AddDataPoint() failed";
    }
    for (int i = 0; i < 899; i++) {
      if (!synchronizer.AddDataPoint(positive, now)) {
        return "900 points AddDataPoint() failed";
      }
    }
    Time coreTime;
    Status status = timer->GetCoreTime(coreTime, now);
    if (status != OKAY) {
      return "900 points GetCoreTime() failed";
    }
    // We should have increased the seconds by 1.
    if (coreTime.seconds != nowSeconds + 1) {
      return "900 points failed";
    }
  }

  // Test adding 50000 points in groups of 100 blocks, with the insertion time and server time
  // increasing by 1 second each block.  Then add a final point that is not on that line and make
  // sure that the result is on the line.
  {
    Timer* tPtr = new Timer;
    std::shared_ptr<Timer> timer(tPtr);
    ClockSynchronizer synchronizer(timer, Time(0, 0));
    auto now = std::chrono::steady_clock::now();
    double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    Time serverTime(nowSeconds, 0);
    for (int i = 0; i < 50; i++) {
      for (int j = 0; j < 100; j++) {
        if (!synchronizer.AddDataPoint(serverTime, now)) {
          return "50000 points AddDataPoint() failed";
        }
      }
      serverTime.seconds++;
      now += std::chrono::seconds(1);
    }

    // Add a point that is below the line.
    if (serverTime.seconds < 25000) {
      serverTime.seconds = 0;
    } else {
      serverTime.seconds -= 25000;
    }
    if (!synchronizer.AddDataPoint(serverTime, now)) {
      return "50000 points AddDataPoint() failed";
    }
    Time coreTime;
    auto testTime = now;
    double testSeconds = std::chrono::duration_cast<std::chrono::seconds>(testTime.time_since_epoch()).count();
    Status status = timer->GetCoreTime(coreTime, testTime);
    if (status != OKAY) {
      return "50000 points GetCoreTime() failed";
    }
    if (synchronizer.m_BlockOffsets.size() != 50) {
      return "50000 points had unexpected number of blocks";
    }
    // The time should match.
    if (coreTime.seconds != testSeconds) {
      return "50000 points failed: got "+std::to_string(coreTime.seconds)+", expected "+std::to_string(testSeconds);
    }

    // Add a point that is above the line.
    if (serverTime.seconds < 25000) {
      serverTime.seconds = 0;
    } else {
      serverTime.seconds -= 25000;
    }
    if (!synchronizer.AddDataPoint(serverTime, now)) {
      return "50000 points AddDataPoint() failed";
    }
    testTime = now;
    testSeconds = std::chrono::duration_cast<std::chrono::seconds>(testTime.time_since_epoch()).count();
    status = timer->GetCoreTime(coreTime, testTime);
    if (status != OKAY) {
      return "50000 points GetCoreTime() failed";
    }
    if (synchronizer.m_BlockOffsets.size() != 50) {
      return "50000 points had unexpected number of blocks";
    }
    // The time should match.
    if (coreTime.seconds != testSeconds) {
      return "50000 points failed: got " + std::to_string(coreTime.seconds) + ", expected " + std::to_string(testSeconds);
    }
  }

  // Add 200*100 points, which should overflow the 100 block limit and make sure we have 100 blocks.
  {
    {
      Timer* tPtr = new Timer;
      std::shared_ptr<Timer> timer(tPtr);
      ClockSynchronizer synchronizer(timer, Time(0, 0));
      auto now = std::chrono::steady_clock::now();
      double nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
      Time serverTime(nowSeconds, 0);
      for (int i = 0; i < 200; i++) {
        for (int j = 0; j < 100; j++) {
          if (!synchronizer.AddDataPoint(serverTime, now)) {
            return "200000 points AddDataPoint() failed";
          }
        }
        serverTime.seconds++;
        now += std::chrono::seconds(1);
      }
      if (synchronizer.m_BlockOffsets.size() != 100) {
        return "200000 points had unexpected number of blocks";
      }
    }
  }
  
  // Everything worked.
  return "";
}
