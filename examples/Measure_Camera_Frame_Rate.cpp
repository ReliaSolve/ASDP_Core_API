/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first server it encounters and measures the frame rate
// of the cameras.  It does this by configuring the cameras to stream full-frame
// images at its maximum rate.  It can be told at the command line to replay a stored stream.

#include <iostream>
#include <chrono>
#include <fstream>
#include <thread>
#include <list>
#include <mutex>
#include <memory>
#include <atomic>
#include <string.h>
#include <ASDP_Core_API.h>
#include <ASDP_BufferPool.h>
#include <ASDP_StreamPacketSortedQueue.h>

using namespace asdp;

//==============================================================================
// Utility functions

std::string WaitForEventType(std::shared_ptr<Receiver> receiver, EventID type, float seconds)
{
  std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  do {
    std::shared_ptr<StreamPacket> response;
    size_t offset = 0;
    Status status = receiver->ReceiveStreamPacket(0, response, offset);
    if ((status != OKAY) && (status != TIMEOUT)) {
      return "Failed to receive stream packet: " + ErrorMessage(status);
    }
    if (response != nullptr) {
      std::shared_ptr<Message> message;
      status = response->GetNextMessage(message);
      if (status != OKAY) {
        return "Failed to get message from stream packet: " + ErrorMessage(status);
      }
      while (message != nullptr) {
        MessageID messageType;
        status = message->GetType(messageType);
        if (status != OKAY) {
          return "Failed to get message type: " + ErrorMessage(status);
        }
        if (messageType == EVENT) {
          MessageEvent event(*message);
          if (event.GetConstructorStatus() != OKAY) {
            return "Failed to construct event message: " + ErrorMessage(event.GetConstructorStatus());
          }
          EventID eventType;
          status = event.GetType(eventType);
          if (status != OKAY) {
            return "Failed to get event type: " + ErrorMessage(status);
          }
          if (eventType == type) {
            // Worked!
            return "";
          }
        }
        status = response->GetNextMessage(message);
        if (status != OKAY) {
          return "Failed to get message from stream packet: " + ErrorMessage(status);
        }
      }
    }
  } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= seconds);

  return "No message of the requested type received in " + std::to_string(seconds) + " seconds";
}

std::shared_ptr<Message> WaitForMessageType(std::shared_ptr<Receiver> receiver, MessageID type, float seconds)
{
  std::shared_ptr<Message> empty;   ///< We return this on failure.
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::shared_ptr<StreamPacket> response;
  do {
    size_t offset = 0;
    Status status = receiver->ReceiveStreamPacket(0, response, offset);
    if ((status != OKAY) && (status != TIMEOUT)) {
      return empty;
    }
    if (response != nullptr) {
      std::shared_ptr<Message> message;
      status = response->GetNextMessage(message);
      if (status != OKAY) {
        return empty;
      }
      while (message != nullptr) {
        MessageID messageType;
        status = message->GetType(messageType);
        if (status != OKAY) {
          return empty;
        }
        if (messageType == type) {
          // Worked!
          return message;
        }
        status = response->GetNextMessage(message);
        if (status != OKAY) {
          return empty;
        }
      }
    }
  } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= seconds);

  return empty;
}

std::string GetStreamList(CoreClient& client, std::shared_ptr<Receiver> receiver, std::vector<uint32_t>& IDs, float seconds)
{
  // Determine how many streams are stored.
  Status status = client.SendCommandPacket(CommandPacketListStoredStreams());
  if (status != OKAY) {
    return "Failed to send storage command: " + ErrorMessage(status);
  }
  std::shared_ptr<Message> msg = WaitForMessageType(receiver, STORED_STREAMS, seconds);
  if (msg == nullptr) {
    return "Did not get stored streams message.";
  }
  MessageStoredStreamList storedStreams(*msg);
  if (storedStreams.GetConstructorStatus() != OKAY) {
    return "Failed to construct stored streams message: " + ErrorMessage(storedStreams.GetConstructorStatus());
  }
  status = storedStreams.GetIDs(IDs);
  if (status != OKAY) {
    return "Failed to get stored stream IDs: " + ErrorMessage(status);
  }
  return "";
}

void StreamReceiverThread(std::shared_ptr<ReceiverUDP> receiverUDP, std::atomic_bool &done,
  std::atomic<size_t> &frames,
  std::vector<std::chrono::steady_clock::time_point> &beginFrameTimes,
  std::vector<std::chrono::steady_clock::time_point> &endFrameTimes)
{
  frames = 0;

  // Pool of buffers for receiving data and writing it to disk.  We pre-allocate them here to
  // avoid the overhead of creating and destroying them at run time.  We pre-allocate a bunch,
  // but more will be created as needed.  In fact, we should only really need 1-2 buffers
  // because we recycle the buffer each time.
  asdp::BufferPool bufferPool(9000, 10);

  // Use a sorting queue to ensure that we process the messages in order even if the UDP packets
  // arrive out of order.
  StreamPacketSortedQueue sortedQueue(50);

  size_t sequenceNumber = 0;
  std::shared_ptr<asdp::StreamPacket> packet;
  while (!done) {
    // Get the next packet.
    size_t offset = 0;
    Status status = receiverUDP->ReceiveStreamPacket(10.0, packet, offset, bufferPool.GetBuffer());
    if (status != asdp::OKAY) {
      std::cerr << "Error receiving StreamPacket: " << ErrorMessage(status) << std::endl;
      done = true;
      return;
    }

    std::list< std::shared_ptr<StreamPacket> > readyPackets = sortedQueue.AddPacket(packet);
    if (readyPackets.size() > 1) {
      std::cerr << "Warning: More than one packet ready to process (re-ordered or missing packet)." << std::endl;
    }
    while (!readyPackets.empty()) {
      std::shared_ptr<asdp::StreamPacket> receiveStreamPacket = readyPackets.front();
      readyPackets.pop_front();

      // Find all of the messages in the packet. If any of them is a begin-frame message, increment
      // the frame count.
      std::shared_ptr<asdp::Message> message;
      status = receiveStreamPacket->GetNextMessage(message);
      if (status != asdp::OKAY) {
        std::cerr << "Error getting first message from StreamPacket: " << ErrorMessage(status) << std::endl;
        done = true;
        return;
      }
      while (message != nullptr) {
        asdp::MessageID messageType;
        status = message->GetType(messageType);
        if (status != asdp::OKAY) {
          std::cerr << "Error getting message type from Message: " << ErrorMessage(status) << std::endl;
          done = true;
          return;
        }
        if (messageType == asdp::CONSOLIDATED_FRAME_DATA) {
          MessageConsolidatedFrameData frameData(*message);
          if (frameData.GetConstructorStatus() != OKAY) {
            std::cerr << "Error constructing frame data message: " << ErrorMessage(frameData.GetConstructorStatus()) << std::endl;
            done = true;
            return;
          }

          bool isBeginFrame;
          status = frameData.GetBeginFrameFlag(isBeginFrame);
          if (status != OKAY) {
            std::cerr << "Error getting begin-frame flag: " << ErrorMessage(status) << std::endl;
            done = true;
            return;
          }
          if (isBeginFrame) {
            beginFrameTimes.push_back(std::chrono::steady_clock::now());
            frames++;
          }

          bool isEndFrame;
          status = frameData.GetEndFrameFlag(isEndFrame);
          if (status != OKAY) {
            std::cerr << "Error getting end-frame flag: " << ErrorMessage(status) << std::endl;
            done = true;
            return;
          }
          if (isEndFrame) {
            endFrameTimes.push_back(std::chrono::steady_clock::now());
          }
        }
        status = receiveStreamPacket->GetNextMessage(message);
        if (status != asdp::OKAY) {
          std::cerr << "Error getting next message from StreamPacket: " << ErrorMessage(status) << std::endl;
          done = true;
          return;
        }
      }
    }
  }
}

//==============================================================================

void usage(const char* name)
{
  std::cerr << "Usage: " << name << " [--duration S] [--maxCameras C] [--summary] [--replay R] [--dumpIntervals F] <ip_address>" << std::endl;
  exit(1);
}

int main(int argc, char** argv)
{
  float durationSeconds = 30;   ///< Run for this many seconds
  std::string ip_address;
  size_t realParams = 0;
  uint32_t maxCameras = 0;
  int replayID = 0;
  bool summary = false;
  std::string dumpFileName;
  std::atomic_bool done(false);

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == std::string("--duration")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --duration" << std::endl;
        return 1;
      }
      durationSeconds = std::stof(argv[i]);
      continue;
    } else if (argv[i] == std::string("--maxCameras")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --maxCameras" << std::endl;
        return 1;
      }
      maxCameras = std::stoul(argv[i]);
    } else if (argv[i] == std::string("--summary")) {
      summary = true;
    } else if (argv[i] == std::string("--replay")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --replay" << std::endl;
        return 1;
      }
      replayID = std::stoi(argv[i]);
    } else if (argv[i] == std::string("--dumpIntervals")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --dumpIntervals" << std::endl;
        return 1;
      }
      dumpFileName = argv[i];
    } else if (argv[i][0] == '-') {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        usage(argv[0]);
    }
  }
  if (realParams != 1) {
    usage(argv[0]);
  }

  // Open a client, specifying the IP address to listen on.
  CoreClient client(ip_address);
  if (client.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to open client: " << ErrorMessage(client.GetConstructorStatus()) << std::endl;
    return 3;
  }
  if (!summary) std::cout << "Listening for servers on " << ip_address << std::endl;

  // Wait for up to two seconds to allow servers to send Discovery messages.
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::vector<std::string> servers;
  Status threadStatus;
  Status status;
  do {
    status = client.GetDiscoveryThreadStatus(threadStatus);
    if (status != OKAY) {
      std::cerr << "Failed to get discovery thread status: " << ErrorMessage(status) << std::endl;
      return 4;
    }
    if (threadStatus != OKAY) {
      std::cerr << "Discovery thread status: " << ErrorMessage(threadStatus) << std::endl;
      return 5;
    }
    status = client.IdentifiedServers(servers);
    if (status != OKAY) {
      std::cerr << "Failed to get identified servers: " << ErrorMessage(status) << std::endl;
      return 6;
    }
    if (!servers.empty()) { break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= 2.0);
  if (servers.empty()) {
    std::cerr << "No servers found; be sure to run the server first." << std::endl;
    return 7;
  }
  if (!summary) {
    std::cout << "Servers found: " << servers.size() << std::endl;
    for (const std::string& server : servers) {
      std::cout << "  " << server << std::endl;
    }
  }

  // Connect to the first server found.
  if (!summary) std::cout << "Connecting to " << servers[0] << std::endl;
  uint16_t major, minor, patch;
  status = client.ConnectToServer(servers[0], major, minor, patch);
  if (status != OKAY) {
    std::cerr << "Failed to connect to server: " << ErrorMessage(status) << std::endl;
    return 8;
  }
  if (!summary) std::cout << "  Connected to server version " << major << "." << minor << "." << patch << std::endl;

  // Get the main stream receiver
  std::shared_ptr<Receiver> receiver;
  status = client.GetMainStreamReceiver(receiver);
  if (status != OKAY) {
    std::cerr << "Failed to get main stream receiver: " << ErrorMessage(status) << std::endl;
    return 10;
  }

  // Ensure that we get a state message from the server within a reasonable time.
  // Report information about the cameras that were found.
  std::shared_ptr<Message> msg = WaitForMessageType(receiver, STATE, 5.0);
  if (msg == nullptr) {
    std::cerr << "Did not get state message." << std::endl;
    return 12;
  }
  MessageState state(*msg);
  if (state.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
    return 13;
  }
  std::vector<CameraInfo> cameras;
  status = state.GetCameras(cameras);
  if (!summary) std::cout << "Found " << cameras.size() << " cameras" << std::endl;
  if ((maxCameras > 0) && (maxCameras < cameras.size())) {
    if (!summary) std::cout << "Limiting to " << maxCameras << " cameras." << std::endl;
    cameras.resize(maxCameras);
  }

  // Start receiver threads for each camera, remembering which port each is listening on.
  // Also construct statistics vectors for each camera.
  std::vector<std::thread> receiverThreads;
  std::vector<uint16_t> ports;
  std::vector< std::atomic<size_t> > frameCounts(cameras.size());
  std::vector< std::vector<std::chrono::steady_clock::time_point> > beginFrameTimes(cameras.size());
  std::vector< std::vector<std::chrono::steady_clock::time_point> > endFrameTimes(cameras.size());
  for (size_t i = 0; i < cameras.size(); ++i) {
    std::shared_ptr<ReceiverUDP> receiverUDP = std::make_shared<ReceiverUDP>(ip_address);
    if (receiverUDP->GetConstructorStatus() != OKAY) {
      std::cerr << "Error constructing ReceiverUDP: " << ErrorMessage(receiverUDP->GetConstructorStatus()) << std::endl;
      return 30;
    }
    uint16_t port;
    asdp::Status status = receiverUDP->GetPort(port);
    if (status != asdp::OKAY) {
      std::cerr << "Error getting port from ReceiverUDP: " << ErrorMessage(status) << std::endl;
      return 31;
    }
    receiverThreads.push_back(std::thread(StreamReceiverThread, receiverUDP, std::ref(done),
      std::ref(frameCounts[i]), std::ref(beginFrameTimes[i]), std::ref(endFrameTimes[i]) ));
    ports.push_back(port);
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // If we've been asked to replay a stream, do so now.
  if (replayID > 0) {
    // Get the list of stored streams.
    std::vector<uint32_t> IDs;
    std::string error = GetStreamList(client, receiver, IDs, 5.0);
    if (!error.empty()) {
      std::cerr << error << std::endl;
      return 32;
    }

    // Find the stream with the requested ID.
    bool found = false;
    for (uint32_t ID : IDs) {
      if (ID == replayID) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::cerr << "Stream ID " << replayID << " not found." << std::endl;
      return 33;
    }

    // Request the stream to be replayed.
    status = client.SendCommandPacket(CommandPacketStartReplay(replayID, { 0, 0 }));
    if (status != OKAY) {
      std::cerr << "Failed to replay stream: " << ErrorMessage(status) << std::endl;
      return 34;
    }

    // Wait for an event saying that we've started to replay.
    if (!WaitForEventType(receiver, START_OF_REPLAY, 5.0).empty()) {
      std::cerr << "Did not get start-of-replay event." << std::endl;
      return 35;
    }
  }

  // Configure the cameras to stream full-frame images at their maximum rate.
  for (size_t i = 0; i < cameras.size(); ++i) {
    uint32_t camID = i + 1;
    uint32_t port = ports[i];

    // Find the minimum period for the camera and which internal trigger ID it uses, then
    // configure the trigger to run at that rate.
    TriggerInfo ti;
    ti.ID = cameras[camID - 1].trigger;
    ti.mode = 1;
    ti.period = cameras[camID - 1].minTriggerPeriod;
    ti.offset = 0;
    ti.trackingFactor = 0.5;
    status = client.SendCommandPacket(CommandPacketConfigureTrigger(ti));
    if (status != OKAY) {
      std::cerr << "Failed to configure trigger: " << ErrorMessage(status) << std::endl;
      return 40;
    }

    // Request the camera to stream full-frame images every frame.
    StreamEndpoint endpoint(ip_address, port);
    SubregionDescription region;
    region.cameraID = camID;
    region.skipFrames = 0;
    region.startTimeSeconds = 0;
    region.startTimeMicroseconds = 0;
    region.left = 0;
    region.top = 0;
    region.right = cameras[camID - 1].width - 1;
    region.bottom = cameras[camID - 1].height - 1;
    status = client.SendCommandPacket(CommandPacketStreamSubregion(endpoint, region));
    if (status != OKAY) {
      std::cerr << "Failed to stream images: " << ErrorMessage(status) << std::endl;
      return 50;
    }
  }

  // Every 3 seconds, print out the average frame rate for each camera.
  std::vector<size_t> lastFrameCounts(cameras.size());
  for (size_t i = 0; i < cameras.size(); ++i) {
    lastFrameCounts[i] = frameCounts[i];
  }
  auto lastTime = std::chrono::steady_clock::now();

  // Wait until one of the threads has set done, sleeping a bit between checks.
  // Stop if it has been durationSeconds since we started.
  if (!summary) std::cout << "Running for " << durationSeconds
    << " seconds or until a camera thread has an error receiving data." << std::endl;
  start = std::chrono::steady_clock::now();
  std::shared_ptr<StreamPacket> response;
  while (!done) {
    // Check for incoming packets on the main stream so that we are consuming them.
    size_t offset = 0;
    Status status = receiver->ReceiveStreamPacket(0.1, response, offset);
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= durationSeconds) {
      done = true;
    }

    // Print out the frame rate every 3 seconds.
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - lastTime).count() >= 3.0) {
      lastTime = std::chrono::steady_clock::now();
      for (size_t i = 0; i < cameras.size(); ++i) {
        std::cout << "Camera " << i + 1 << ": " << (frameCounts[i] - lastFrameCounts[i]) / 3.0 << " fps" << std::endl;
        lastFrameCounts[i] = frameCounts[i];
      }
    }
  }

  // Join all of our threads.
  for (size_t i = 0; i < receiverThreads.size(); ++i) {
    receiverThreads[i].join();
  }

  // Dump the frame intervals to a file if requested.
  if (!dumpFileName.empty()) {
    if (!summary) std::cout << "Writing intervals to " << dumpFileName << std::endl;
    std::ofstream dumpFile(dumpFileName);
    if (!dumpFile) {
      std::cerr << "Failed to open dump file " << dumpFileName << std::endl;
      return 60;
    }
    // Add the header line
    for (size_t i = 0; i < cameras.size(); ++i) {
      dumpFile << "BeginIntervals" << i << ",";
      dumpFile << "EndIntervals" << i;
      if (i < cameras.size() - 1) {
        dumpFile << ",";
      }
    }
    dumpFile << std::endl;
    // Add the data lines.
    size_t maxFrames = 0;
    for (size_t i = 0; i < cameras.size(); ++i) {
      maxFrames = std::max(maxFrames, beginFrameTimes[i].size());
      maxFrames = std::max(maxFrames, endFrameTimes[i].size());
    }
    for (size_t j = 1; j < maxFrames - 1; ++j) {
      for (size_t i = 0; i < cameras.size(); ++i) {
        std::chrono::duration<double> intervalB = beginFrameTimes[i][j] - beginFrameTimes[i][j - 1];
        std::chrono::duration<double> intervalE = endFrameTimes[i][j] - endFrameTimes[i][j - 1];
        dumpFile << intervalB.count() << "," << intervalE.count();
        if (j < maxFrames - 1) {
          dumpFile << ",";
        }
      }
      dumpFile << std::endl;
    }
    dumpFile.close();
  }

  return 0;
}
