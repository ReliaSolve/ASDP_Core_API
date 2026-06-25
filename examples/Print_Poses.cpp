/*
 * Copyright (C) 2026: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to a server (defaults to the first one found unless a specific
// serial number is specified) and prints any pose reports received.

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

//==============================================================================

void usage(const char* name)
{
  std::cerr << "Usage: " << name << " [--duration S] [--serial s] [--replay R] <ip_address>" << std::endl;
  exit(1);
}

int main(int argc, char** argv)
{
  float durationSeconds = 30;   ///< Run for this many seconds
  std::string ip_address;
  int serial = -1;
  size_t realParams = 0;
  int replayID = 0;
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
    } else if (argv[i] == std::string("--replay")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --replay" << std::endl;
        return 1;
      }
      replayID = std::stoi(argv[i]);
    } else if (argv[i] == std::string("--serial")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --serial" << std::endl;
        return 1;
      }
      serial = atoi(argv[i]);
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

  // Wait for up to two seconds to allow servers to send Discovery messages.
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::map<uint32_t, std::string> servers;
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
    // If we have been asked for a specific serial number, remove all others.
    if (serial >= 0) {
      std::map<uint32_t, std::string> filteredServers;
      for (const auto& server : servers) {
        uint32_t serverSerialNumber = server.first;
        if (serverSerialNumber == static_cast<uint32_t>(serial)) {
          filteredServers[server.first] = server.second;
        }
      }
      servers = filteredServers;
    }
    if (!servers.empty()) { break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= 2.0);
  if (servers.empty()) {
    std::cerr << "No servers found; be sure to run the server first." << std::endl;
    return 7;
  }

  // Connect to the server found.
  uint16_t major, minor, patch;
  status = client.ConnectToServer(servers.begin()->second, major, minor, patch);
  if (status != OKAY) {
    std::cerr << "Failed to connect to server: " << ErrorMessage(status) << std::endl;
    return 8;
  }

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

  std::cout << "Requesting pose data." << std::endl; 
  status = client.SendCommandPacket(CommandPacketStreamPoses()); 
  if (status != OKAY) { 
    std::cerr << "Failed to request pose data: " << ErrorMessage(status) << std::endl; 
    return 36; 
  } 

  // Stop if it has been durationSeconds since we started.
  start = std::chrono::steady_clock::now();
  std::shared_ptr<StreamPacket> response;
  while (!done) {
    // Print any pose reports received.
    std::shared_ptr<Message> msg = WaitForMessageType(receiver, POSE, 0.1);
    if (msg != nullptr) {
      MessagePose poseMsg(*msg);
      if (poseMsg.GetConstructorStatus() != OKAY) {
        std::cerr << "Error parsing POSE message" << std::endl;
        return 9;
      }
      std::array<float, 3> rotation, rotVel;
      if (OKAY != poseMsg.GetRot(rotation)) {
        std::cerr << "Error reading rotation from POSE message" << std::endl;
        return 10;
      }
      if (OKAY != poseMsg.GetRotVel(rotVel)) {
        std::cerr << "Error reading reticle velocity from POSE message" << std::endl;
        return 11;
      }
      std::cout << " (rotation: [" << rotation[0] << ", " << rotation[1] << ", " << rotation[2]
        << "], rotVel: [" << rotVel[0] << ", " << rotVel[1] << ", " << rotVel[2] << "])" << std::endl;
    }
  }

  return 0;
}
