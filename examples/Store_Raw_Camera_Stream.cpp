/*
 * Copyright (C) 2025: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first server it encounters and stores the raw UDP
// packets read from its first camera into a file.  This program is useful for debugging whether
// the packet streams from a camera are correct by saving them and then running Stream_File_Parser on
// its output file.

#include <iostream>
#include <chrono>
#include <map>
#include <ASDP_Core_API.h>

using namespace asdp;

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
  std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  do {
    std::shared_ptr<StreamPacket> response;
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
  } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= seconds);

  return empty;
}

void usage(std::string progName)
{
  std::cerr << "Usage: " << progName << " <ip_address> <dump_file>" << std::endl;
  std::cerr << "  <ip_address>: The IP address to listen on for servers." << std::endl;
  std::cerr << "  <dump_file>: The name of the file to write the raw packets to, e.g. stream1.dat." << std::endl;
}

int main(int argc, char** argv)
{
  std::string ip_address, dump_file;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.  There is a --dumpUDP flag to cause it
  // to write the contents of its first UDP packet to UDP.dat.
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == std::string("--help")) {
      usage(argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      usage(argv[0]);
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      case 1:
        dump_file = argv[i];
        break;
      default:
        std::cerr << "Unexpected argument: " << argv[i] << std::endl;
        usage(argv[0]);
        return 2;
    }
  }
  if (realParams != 2) {
    std::cerr << "Expected two arguments, got " << realParams << std::endl;
    usage(argv[0]);
    return 2;
  }

  // Open a client, specifying the IP address to listen on.
  CoreClient client(ip_address);
  if (client.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to open client: " << ErrorMessage(client.GetConstructorStatus()) << std::endl;
    return 3;
  }
  std::cout << "Listening for servers on " << ip_address << std::endl;

  // Wait for two seconds to allow servers to send Discovery messages and then check the status of
  // the Discover thread and find the servers.
  std::this_thread::sleep_for(std::chrono::seconds(2));
  std::vector<std::string> servers;
  Status threadStatus;
  Status status = client.GetDiscoveryThreadStatus(threadStatus);
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
  if (servers.empty()) {
    std::cerr << "No servers found; be sure to run Base_Server or another first." << std::endl;
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
  std::cout << "  Connected to server version " << major << "." << minor << "." << patch << std::endl;
  uint32_t serialNumber;
  status = client.GetServerSerialNumber(serialNumber);
  if (status != OKAY) {
    std::cerr << "Failed to get server serial number: " << ErrorMessage(status) << std::endl;
    return 9;
  }
  std::cout << "  Connected to server with serial number " << serialNumber << std::endl;

  // Get the main stream receiver
  std::shared_ptr<Receiver> receiver;
  status = client.GetMainStreamReceiver(receiver);
  if (status != OKAY) {
    std::cerr << "Failed to get main stream receiver: " << ErrorMessage(status) << std::endl;
    return 10;
  }

  // Ensure that we get a clock-sync message from the server within a reasonable time.
  std::cout << "Waiting for clock sync message" << std::endl;
  std::string ret = WaitForEventType(receiver, CLOCK_SYNC, 5.0);
  if (!ret.empty()) {
    std::cerr << "Did not get clock sync: " << ret << std::endl;
    return 11;
  }

  // Ensure that we get a state message from the server within a reasonable time.
  std::cout << "Waiting for state message" << std::endl;
  std::shared_ptr<Message> msg = WaitForMessageType(receiver, STATE, 5.0);
  if (msg == nullptr) {
    std::cerr << "Did not get state message." << std::endl;
    return 12;
  }

  // Find the available features on the server so we can ensure that we can or can't
  // issue the relevant commands.  Start by filling them all in with false and then
  // adding the ones that we find in the state.
  std::map<FeatureID, bool> features;
  features[STORAGE_API_AVAILABLE] = false;
  features[TEMPERATURE_API_AVAILABLE] = false;
  features[POSE_API_ORIENTATION_AVAILABLE] = false;
  features[POSE_API_POSITION_AVAILABLE] = false;
  MessageState state(*msg);
  if (state.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
    return 13;
  }
  std::vector<FeatureID> availableFeatures;
  status = state.GetFeatures(availableFeatures);
  if (status != OKAY) {
    std::cerr << "Failed to get available features: " << ErrorMessage(status) << std::endl;
    return 14;
  }
  for (FeatureID feature : availableFeatures) {
    features[feature] = true;
  }
  std::cout << "  Storage feature = " << features[STORAGE_API_AVAILABLE] << std::endl;
  std::cout << "  Temperature feature = " << features[TEMPERATURE_API_AVAILABLE] << std::endl;
  std::cout << "  Pose orientation feature = " << features[POSE_API_ORIENTATION_AVAILABLE] << std::endl;
  std::cout << "  Pose position feature = " << features[POSE_API_POSITION_AVAILABLE] << std::endl;

  // Report information about the cameras that were found.
  std::vector<CameraInfo> cameras;
  status = state.GetCameras(cameras);
  std::cout << "Found " << cameras.size() << " cameras" << std::endl;

  // Try streaming data from the first camera at the highest rate.
  uint32_t camID = 1;
  size_t count = 0;
  if (camID <= cameras.size()) {

    // Find the minimum period for the camera and which internal trigger ID it uses, then
    // configure the trigger to run at that rate.
    TriggerInfo ti;
    ti.ID = cameras[camID - 1].trigger;
    ti.mode = 1;
    ti.period = cameras[camID - 1].minTriggerPeriod;
    ti.offset = 0;
    ti.trackingFactor = 0.5;
    ti.externalID = 0;
    status = client.SendCommandPacket(CommandPacketConfigureTrigger(ti));
    if (status != OKAY) {
      std::cerr << "Failed to configure trigger: " << ErrorMessage(status) << std::endl;
      return 29;
    }

    // Construct a UDP receiver for a stream from the camera.
    ReceiverUDP receiverUDP(ip_address);
    if (receiverUDP.GetConstructorStatus() != OKAY) {
      std::cerr << "Error constructing ReceiverUDP: " << ErrorMessage(receiverUDP.GetConstructorStatus()) << std::endl;
      return 30;
    }
    uint16_t port;
    asdp::Status status = receiverUDP.GetPort(port);
    if (status != asdp::OKAY) {
      std::cerr << "Error getting port from ReceiverUDP: " << ErrorMessage(status) << std::endl;
      return 31;
    }

    std::cout << "Streaming image messages from camera " << camID << " on port " << port << std::endl;

    // Request the camera to stream images.
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
      return 32;
    }

    // Stream for half a second, writing the packets to a file.
    std::shared_ptr<SenderFile> fileSender = std::make_shared<SenderFile>(dump_file);
    if (fileSender->GetConstructorStatus() != OKAY) {
      std::cerr << "Error constructing SenderFile: " << ErrorMessage(fileSender->GetConstructorStatus()) << std::endl;
      return 100;
    }
    auto start = std::chrono::high_resolution_clock::now();
    do {
      std::shared_ptr<asdp::StreamPacket> receiveStreamPacket;
      size_t offset = 0;
      status = receiverUDP.ReceiveStreamPacket(1.0, receiveStreamPacket, offset);
      if (status != asdp::OKAY) {
        std::cerr << "Error receiving StreamPacket: " << ErrorMessage(status) << std::endl;
        return 33;
      }
      count++;
      {
        //std::cout << "Writing UDP packet" << std::endl;
        Status status = fileSender->SendStreamPacket(*receiveStreamPacket);
        if (status != OKAY) {
          std::cerr << "Error writing packet to file: " << ErrorMessage(status) << std::endl;
          return 102;
        }
      }
    } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= 0.5);
    fileSender.reset();
    std::cout << "  Got " << count << " packets." << std::endl;

    // Turn off streaming.
    status = client.SendCommandPacket(CommandPacketCancelSubregion(camID, endpoint));
    if (status != OKAY) {
      std::cerr << "Failed to cancel stream images: " << ErrorMessage(status) << std::endl;
      return 40;
    }
  } else {
    std::cerr << "No cameras found to stream from." << std::endl;
    return 41;
  }

  std::cout << std::endl << "Success!" << std::endl;
  return 0;
}
