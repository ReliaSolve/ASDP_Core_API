/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first server it encounters and asks for all cameras to
// stream at full rate. It starts as many threads as there are cameras and pulls data from each
// of them into pre-allocated buffers.  It checks to see that the packets are in order and
// then reuses the buffers for the next frame.

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

void StreamReceiverThread(std::shared_ptr<ReceiverUDP> receiverUDP, std::atomic_bool &done, size_t &missed)
{
  missed = 0;
  size_t sequenceNumber = 0;
  std::shared_ptr<asdp::StreamPacket> receiveStreamPacket;
  while (!done) {
    // Get the next packet and verify that its sequence number matches what we expect so that
    // we know that we didn't miss any data.
    size_t offset = 0;
    Status status = receiverUDP->ReceiveStreamPacket(10.0, receiveStreamPacket, offset);
    if (status != asdp::OKAY) {
      std::cerr << "Error receiving StreamPacket: " << ErrorMessage(status) << std::endl;
      done = true;
      return;
    }
    uint32_t packetSequenceNumber;
    status = receiveStreamPacket->GetSequenceNumber(packetSequenceNumber);
    if (status != asdp::OKAY) {
      std::cerr << "Error getting sequence number from StreamPacket: " << ErrorMessage(status) << std::endl;
      done = true;
      return;
    }
    if (packetSequenceNumber != sequenceNumber++) {
      missed++;
    }
  };
}

//==============================================================================

int main(int argc, char** argv)
{
  uint32_t frameStride = 30;    ///< Read one out of every this many frames. Set to 1 for every frame.
  float durationSeconds = 30;   ///< Run for this many seconds
  std::string ip_address;
  size_t realParams = 0;
  std::atomic_bool done(false);

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.  There is a --serial flag to specify
  // the serial number of the server, which defaults to 1.
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == std::string("--duration")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --duration" << std::endl;
        return 1;
      }
      durationSeconds = std::stof(argv[i]);
      continue;
    } else if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " [-duration S] <ip_address>" << std::endl;
        return 2;
    }
  }
  if (realParams != 1) {
    std::cerr << "Usage: " << argv[0] << " [-duration S] <ip_address>" << std::endl;
    return 2;
  }

  // Open a client, specifying the IP address to listen on.
  CoreClient client(ip_address);
  if (client.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to open client: " << ErrorMessage(client.GetConstructorStatus()) << std::endl;
    return 3;
  }
  std::cout << "Listening for servers on " << ip_address << std::endl;

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
  std::cout << "Servers found: " << servers.size() << std::endl;
  for (const std::string& server : servers) {
    std::cout << "  " << server << std::endl;
  }

  // Connect to the first server found.
  std::cout << "Connecting to " << servers[0] << std::endl;
  uint16_t major, minor, patch;
  status = client.ConnectToServer(servers[0], major, minor, patch);
  if (status != OKAY) {
    std::cerr << "Failed to connect to server: " << ErrorMessage(status) << std::endl;
    return 8;
  }
  uint32_t serialNumber;
  status = client.GetServerSerialNumber(serialNumber);
  if (status != OKAY) {
    std::cerr << "Failed to get server serial number: " << ErrorMessage(status) << std::endl;
    return 9;
  }
  std::cout << "  Connected to server version " << major << "." << minor << "." << patch
    << " with serial number " << serialNumber << std::endl;

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
  std::cout << "Found " << cameras.size() << " cameras" << std::endl;

  // Start receiver threads for each camera, remembering which port each is listening on.
  std::vector<std::thread> receiverThreads;
  std::vector<uint16_t> ports;
  std::vector<size_t> missedCounts;
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
    missedCounts.push_back(0);
    receiverThreads.push_back(std::thread(StreamReceiverThread, receiverUDP, std::ref(done), std::ref(missedCounts[i])));
    ports.push_back(port);
  }

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

    // Request the camera to stream full-frame images once every frameStride frames.
    StreamEndpoint endpoint(ip_address, port);
    SubregionDescription region;
    region.cameraID = camID;
    region.skipFrames = frameStride - 1;
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

  // Wait until one of the threads has set done, sleeping a bit between checks.
  // Stop if it has been durationSeconds since we started.
  std::cout << "Running for " << durationSeconds
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
  }

  // Join all of our threads.
  for (size_t i = 0; i < receiverThreads.size(); ++i) {
    receiverThreads[i].join();
  }

  // Report how many times each camera missed packets
  for (size_t i = 0; i < cameras.size(); ++i) {
    std::cout << "Camera " << i+1 << " had " << missedCounts[i] << " cases of missed packets" << std::endl;
  }

  return 0;
}
