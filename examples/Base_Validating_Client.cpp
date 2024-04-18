/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first server it encounters and runs a series
// of validation tests on it. It only tests basic functions of the server, and is not
// a comprehensive test suite. In particular, it does not test any additional features
// and it does not test streaming image data from the server. It is intended to be used
// to test the CoreServerBase class.

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

int main(int argc, char** argv)
{
  std::string ip_address;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.  There is a --serial flag to specify
  // the serial number of the server, which defaults to 1.
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " <ip_address>" << std::endl;
        return 2;
    }
  }
  if (realParams != 1) {
    std::cerr << "Usage: " << argv[0] << " <ip_address>" << std::endl;
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
  std::cout << "Verifying that the server opcode responses match the reported features." << std::endl;

  // Report information about the cameras that were found.
  std::vector<CameraInfo> cameras;
  status = state.GetCameras(cameras);
  std::cout << "Found " << cameras.size() << " cameras" << std::endl;

  // Test the storage API and see if our getting an invalid opcode matches what we
  // expect.
  status = client.SendCommandPacket(CommandPacketListStoredStreams());
  if (status != OKAY) {
    std::cerr << "Failed to send storage command: " << ErrorMessage(status) << std::endl;
    return 15;
  }
  ret = WaitForEventType(receiver, INVALID_OPERATION, 5.0);
  bool available = (!ret.empty());
  if (features[STORAGE_API_AVAILABLE] != available) {
    std::cerr << "Storage API availability mismatch" << std::endl;
    return 16;
  }

  // Test the temperature API and see if our getting an invalid opcode matches what we
  // expect.
  status = client.SendCommandPacket(CommandPacketCancelTemperatures());
  if (status != OKAY) {
    std::cerr << "Failed to send temperature command: " << ErrorMessage(status) << std::endl;
    return 17;
  }
  ret = WaitForEventType(receiver, INVALID_OPERATION, 5.0);
  available = (!ret.empty());
  if (features[TEMPERATURE_API_AVAILABLE] != available) {
    std::cerr << "Temperature API availability mismatch" << std::endl;
    return 18;
  }

  // Test the pose API and see if our getting an invalid opcode matches what we
  // expect.
  status = client.SendCommandPacket(CommandPacketCancelPoses());
  if (status != OKAY) {
    std::cerr << "Failed to send pose command: " << ErrorMessage(status) << std::endl;
    return 19;
  }
  ret = WaitForEventType(receiver, INVALID_OPERATION, 5.0);
  available = (!ret.empty());
  bool expected = features[POSE_API_ORIENTATION_AVAILABLE] || features[POSE_API_POSITION_AVAILABLE];
  if (expected != available) {
    std::cerr << "Pose API availability mismatch" << std::endl;
    return 20;
  }

  // Set the rate of state messages to 10Hz and make sure we get at least 15 in 2 seconds.
  std::cout << "Checking fast state messages" << std::endl;
  status = client.SendCommandPacket(CommandPacketSetStreamStatePeriod(0.1));
  if (status != OKAY) {
    std::cerr << "Failed to set state rate: " << ErrorMessage(status) << std::endl;
    return 21;
  }
  std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
  int count = 0;
  do {
    std::shared_ptr<Message> msg = WaitForMessageType(receiver, STATE, 0.1);
    if (msg != nullptr) {
      ++count;
    }
  } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= 2.0);
  if (count < 15) {
    std::cerr << "Did not get enough state messages: " << count << std::endl;
    return 22;
  }

  // Set the rate of state messages to 1 Hz and make sure we don't get too many in 2 seconds.
  std::cout << "Checking slow state messages" << std::endl;
  status = client.SendCommandPacket(CommandPacketSetStreamStatePeriod(1.0));
  if (status != OKAY) {
    std::cerr << "Failed to set state rate: " << ErrorMessage(status) << std::endl;
    return 23;
  }
  start = std::chrono::high_resolution_clock::now();
  count = 0;
  do {
    std::shared_ptr<Message> msg = WaitForMessageType(receiver, STATE, 0.1);
    if (msg != nullptr) {
      ++count;
    }
  } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= 2.0);
  if (count > 3) {
    std::cerr << "Got too many state messages: " << count << std::endl;
    return 24;
  }

  // If it can send temperatures, stream and make sure we get at least 1 in 2 seconds.
  if (features[TEMPERATURE_API_AVAILABLE]) {
    std::cout << "Checking for temperature messages" << std::endl;
    status = client.SendCommandPacket(CommandPacketStreamTemperatures());
    if (status != OKAY) {
      std::cerr << "Failed to stream temperatures: " << ErrorMessage(status) << std::endl;
      return 25;
    }
    start = std::chrono::high_resolution_clock::now();
    count = 0;
    do {
      std::shared_ptr<Message> msg = WaitForMessageType(receiver, TEMPERATURE, 0.1);
      if (msg != nullptr) {
        ++count;
      }
    } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= 2.0);
    if (count < 1) {
      std::cerr << "Did not get enough temperature messages: " << count << std::endl;
      return 26;
    }
  }

  // If it can send poses, stream and make sure we get at least 1 in 2 seconds.
  if (features[POSE_API_ORIENTATION_AVAILABLE] || features[POSE_API_POSITION_AVAILABLE]) {
    std::cout << "Checking for pose messages" << std::endl;
    status = client.SendCommandPacket(CommandPacketStreamPoses());
    if (status != OKAY) {
      std::cerr << "Failed to stream poses: " << ErrorMessage(status) << std::endl;
      return 27;
    }
    start = std::chrono::high_resolution_clock::now();
    count = 0;
    do {
      std::shared_ptr<Message> msg = WaitForMessageType(receiver, POSE, 0.1);
      if (msg != nullptr) {
        ++count;
      }
    } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= 2.0);
    if (count < 1) {
      std::cerr << "Did not get enough pose messages: " << count << std::endl;
      return 28;
    }
  }

  // If we have at least one camera, try streaming data from it at its highest rate.
  for (uint32_t camID = 1; camID <= cameras.size(); camID++) {

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
      return 29;
    }
      
    // Construct a UDP receiver for a stream from the camera.
    ReceiverUDP receiverUDP;
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

    std::cout << "Checking for image messages from camera " << camID << " on port " << port << std::endl;

    // Request the camera to stream images and make sure we get at least one begin-frame message.
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
    start = std::chrono::steady_clock::now();
    size_t sequenceNumber = 0;
    size_t numStartFrames = 0;
    do {
      std::shared_ptr<asdp::StreamPacket> receiveStreamPacket;
      size_t offset = 0;
      status = receiverUDP.ReceiveStreamPacket(1.0, receiveStreamPacket, offset);
      if (status != asdp::OKAY) {
        std::cerr << "Error receiving StreamPacket: " << ErrorMessage(status) << std::endl;
        return 33;
      }
      uint32_t packetSequenceNumber;
      status = receiveStreamPacket->GetSequenceNumber(packetSequenceNumber);
      if (status != asdp::OKAY) {
        std::cerr << "Error getting sequence number from StreamPacket: " << ErrorMessage(status) << std::endl;
        return 34;
      }
      if (packetSequenceNumber != sequenceNumber++) {
        std::cerr << " Bad sequence number: " << packetSequenceNumber << ", expected " << sequenceNumber - 1 << std::endl;
        std::cerr << "  (Presumably dropped network packets, consider re-running)" << std::endl;
      }
      std::shared_ptr<asdp::Message> message;
      status = receiveStreamPacket->GetNextMessage(message);
      if (status != asdp::OKAY) {
        std::cerr << "Error getting first message from packet: " << ErrorMessage(status) << std::endl;
        return 35;
      }
      asdp::MessageID rID;
      status = message->GetType(rID);
      if (status != asdp::OKAY) {
        std::cerr << "Error getting type from packet: " << ErrorMessage(status) << std::endl;
        return 36;
      }
      if (rID == asdp::FRAME_BEGIN) {
        numStartFrames++;
      }
    } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= 0.5);
    if (count < 1) {
      std::cerr << "Did not get enough image messages: " << count << std::endl;
      return 37;
    }
    std::cout << "  Got " << numStartFrames << " begin-frame messages." << std::endl;

    // Turn off streaming.
    status = client.SendCommandPacket(CommandPacketCancelSubregion(camID, endpoint));
    if (status != OKAY) {
      std::cerr << "Failed to cancel stream images: " << ErrorMessage(status) << std::endl;
      return 38;
    }

  }

  std::cout << std::endl << "Success!" << std::endl;
  return 0;
}