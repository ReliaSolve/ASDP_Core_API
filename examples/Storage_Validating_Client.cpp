/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first server it encounters and runs a series
// of validation tests on it.
// It is intended to be used to verify that a Storage Module performs as expected.

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

std::string GetStreamList(CoreClient &client, std::shared_ptr<Receiver> receiver, std::vector<uint32_t>& IDs, float seconds)
{
  // Determine how many streams are stored.
  Status status = client.SendCommandPacket(CommandPacketListStoredStreams());
  if (status != OKAY) {
    return "Failed to send storage command: " + ErrorMessage(status);
  }
  std::shared_ptr<Message> msg = WaitForMessageType(receiver, STORED_STREAMS, 5.0);
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

void Usage(const char* name)
{
  std::cerr << "Usage: " << name << " [--destructive] <ip_address>" << std::endl;
  std::cerr << "  --destructive: Run tests that will change the state of the server (requires Module connected to camera)" << std::endl;
}

int main(int argc, char** argv)
{
  std::string ip_address;
  bool destructive = false;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.  There is a --serial flag to specify
  // the serial number of the server, which defaults to 1.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--destructive") {
      destructive = true;
    } else if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        ip_address = argv[i];
        break;
      default:
        Usage(argv[0]);
        return 2;
    }
  }
  if (realParams != 1) {
    Usage(argv[0]);
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
    std::cerr << "No servers found; be sure to run ASDP_Storage_Module or another first." << std::endl;
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

  // Find the available features on the server so we can ensure that it is a storage
  // module.  Start by filling them all in with false and then
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
  if (!features[STORAGE_API_AVAILABLE]) {
    std::cerr << "Server does not support storage API" << std::endl;
    return 15;
  }

  // Stop replaying in case it has been turned on.
  status = client.SendCommandPacket(CommandPacketStopReplay());
  if (status != OKAY) {
    std::cerr << "Failed to send stop replay command: " << ErrorMessage(status) << std::endl;
    return 15;
  }

  // Report the disk-space information.
  uint64_t totalSpace, freeSpace;
  status = state.GetTotalDiskSpace(totalSpace);
  if (status != OKAY) {
    std::cerr << "Failed to get total disk space: " << ErrorMessage(status) << std::endl;
    return 15;
  }
  std::cout << "Total disk space: " << totalSpace << " bytes" << std::endl;
  status = state.GetRemainingDiskSpace(freeSpace);
  if (status != OKAY) {
    std::cerr << "Failed to get remaining disk space: " << ErrorMessage(status) << std::endl;
    return 15;
  }
  std::cout << "Free disk space: " << freeSpace << " bytes" << std::endl;

  // Report whether the device is set to record data at start-up.  Then switch it to the opposite
  // setting and back to the original setting and make sure that both take effect.  Get two state
  // messages to ensure that the change is complete.
  uint8_t recordOnReset;
  status = state.GetRecordOnReset(recordOnReset);
  if (status != OKAY) {
    std::cerr << "Failed to get recording at startup: " << ErrorMessage(status) << std::endl;
    return 16;
  }
  std::cout << "Recording on reset is " << (recordOnReset ? "on" : "off") << std::endl;

  std::cout << "Toggling recording on reset" << std::endl;
  status = client.SendCommandPacket(CommandPacketSetStartUpRecordingState(!recordOnReset));
  msg = WaitForMessageType(receiver, STATE, 5.0);
  if (msg == nullptr) {
    std::cerr << "Did not get state message." << std::endl;
    return 17;
  }
  msg = WaitForMessageType(receiver, STATE, 5.0);
  if (msg == nullptr) {
    std::cerr << "Did not get state message." << std::endl;
    return 18;
  }
  state = MessageState(*msg);
  if (state.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
    return 19;
  }
  uint8_t newRecordOnReset;
  status = state.GetRecordOnReset(newRecordOnReset);
  if (status != OKAY) {
    std::cerr << "Failed to get recording at startup: " << ErrorMessage(status) << std::endl;
    return 20;
  }
  std::cout << "  Recording on reset is now " << (newRecordOnReset ? "on" : "off") << std::endl;
  if (newRecordOnReset == recordOnReset) {
    std::cerr << "Failed to toggle recording on reset" << std::endl;
    return 21;
  }

  status = client.SendCommandPacket(CommandPacketSetStartUpRecordingState(recordOnReset));
  msg = WaitForMessageType(receiver, STATE, 5.0);
  if (msg == nullptr) {
    std::cerr << "Did not get state message." << std::endl;
    return 22;
  }
  msg = WaitForMessageType(receiver, STATE, 5.0);
  if (msg == nullptr) {
    std::cerr << "Did not get state message." << std::endl;
    return 23;
  }
  state = MessageState(*msg);
  if (state.GetConstructorStatus() != OKAY) {
    std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
    return 24;
  }
  status = state.GetRecordOnReset(newRecordOnReset);
  if (status != OKAY) {
    std::cerr << "Failed to get recording at startup: " << ErrorMessage(status) << std::endl;
    return 25;
  }
  std::cout << "  Recording on reset is now " << (newRecordOnReset ? "on" : "off") << std::endl;
  if (newRecordOnReset != recordOnReset) {
    std::cerr << "Failed to toggle recording on reset" << std::endl;
    return 26;
  }

  // Determine how many streams are stored.
  std::vector<uint32_t> IDs;
  std::string retStreams = GetStreamList(client, receiver, IDs, 5.0);
  if (!retStreams.empty()) {
    std::cerr << retStreams << std::endl;
    return 27;
  }
  size_t numStreams = IDs.size();
  std::cout << "There are " << numStreams << " stored streams." << std::endl;

  if (destructive) {
    // Report whether the device is set to storing.  Then switch it to the opposite
    // setting and back to the original setting and make sure that both take effect.  Get two state
    // messages to ensure that the change is complete.  If we are able to toggle storing on and off,
    // we expect an additional stored stream.
    size_t numExpectedStreams = numStreams;
    uint8_t storing;
    status = state.GetStoring(storing);
    if (status != OKAY) {
      std::cerr << "Failed to get storing: " << ErrorMessage(status) << std::endl;
      return 100;
    }
    std::cout << "Storing is " << (storing ? "on" : "off") << std::endl;

    std::cout << "Toggling storing" << std::endl;
    if (storing) {
      status = client.SendCommandPacket(CommandPacketStopRecording());
    } else {
      status = client.SendCommandPacket(CommandPacketStartRecording());
    }
    if (status != OKAY) {
      std::cerr << "Failed to send start/stop recording command: " << ErrorMessage(status) << std::endl;
      return 101;
    }
    msg = WaitForMessageType(receiver, STATE, 5.0);
    if (msg == nullptr) {
      std::cerr << "Did not get state message." << std::endl;
      return 102;
    }
    msg = WaitForMessageType(receiver, STATE, 5.0);
    if (msg == nullptr) {
      std::cerr << "Did not get state message." << std::endl;
      return 103;
    }
    state = MessageState(*msg);
    if (state.GetConstructorStatus() != OKAY) {
      std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
      return 104;
    }
    uint8_t newStoring;
    status = state.GetStoring(newStoring);
    if (status != OKAY) {
      std::cerr << "Failed to get storing: " << ErrorMessage(status) << std::endl;
      return 105;
    }
    std::cout << "  Storing is now " << (newStoring ? "on" : "off") << std::endl;
    if (newStoring == storing) {
      std::cerr << "Failed to toggle storing (Storage Module may not be connected to Core Module?)" << std::endl;
      return 106;
    } else {
      numExpectedStreams++;
    }

    if (storing) {
      status = client.SendCommandPacket(CommandPacketStartRecording());
    }
    else {
      status = client.SendCommandPacket(CommandPacketStopRecording());
    } if (status != OKAY) {
      std::cerr << "Failed to send start/stop recording command: " << ErrorMessage(status) << std::endl;
      return 107;
    }
    msg = WaitForMessageType(receiver, STATE, 5.0);
    if (msg == nullptr) {
      std::cerr << "Did not get state message." << std::endl;
      return 108;
    }
    msg = WaitForMessageType(receiver, STATE, 5.0);
    if (msg == nullptr) {
      std::cerr << "Did not get state message." << std::endl;
      return 109;
    }
    state = MessageState(*msg);
    if (state.GetConstructorStatus() != OKAY) {
      std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
      return 110;
    }
    status = state.GetStoring(newStoring);
    if (status != OKAY) {
      std::cerr << "Failed to get storing: " << ErrorMessage(status) << std::endl;
      return 111;
    }
    std::cout << "  Storing is now " << (newStoring ? "on" : "off") << std::endl;
    if (newStoring != storing) {
      std::cerr << "Failed to toggle storing" << std::endl;
      return 112;
    }

    // Determine how many streams are stored and see if it matches what we expect.
    retStreams = GetStreamList(client, receiver, IDs, 5.0);
    if (!retStreams.empty()) {
      std::cerr << retStreams << std::endl;
      return 113;
    }
    numStreams = IDs.size();
    if (numStreams != numExpectedStreams) {
      std::cerr << "Expected " << numExpectedStreams << " stored streams, but got " << numStreams << std::endl;
      return 114;
    }

    // Stop recording so that the next test can delete the last stored stream.  Wait until we have a report
    // that we are no longer storing.
    std::cout << "Turning off storing" << std::endl;
    status = client.SendCommandPacket(CommandPacketStopRecording());
    if (status != OKAY) {
      std::cerr << "Failed to send start/stop recording command: " << ErrorMessage(status) << std::endl;
      return 200;
    }
    /// @todo Wait for report rather than sleeping.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Make sure we have at least one stream.
    if (numStreams < 1) {
      std::cerr << "No stored streams: toggling storage should have made a new one." << std::endl;
      return 201;
    }

    // Delete the first stored stream.
    std::cout << "Deleting first stored stream" << std::endl;
    status = client.SendCommandPacket(CommandPacketEraseStoredStream(IDs.front()));
    if (status != OKAY) {
      std::cerr << "Failed to delete stored stream: " << ErrorMessage(status) << std::endl;
      return 202;
    }
    numStreams--;

    // Determine how many streams are stored and see if it matches what we expect.
    retStreams = GetStreamList(client, receiver, IDs, 5.0);
    if (!retStreams.empty()) {
      std::cerr << retStreams << std::endl;
      return 203;
    }
    if (numStreams != IDs.size()) {
      std::cerr << "Expected " << numStreams << " stored streams, but got " << IDs.size() << std::endl;
      return 204;
    }

    // Delete all stored streams and make sure they are all gone.
    std::cout << "Deleting all stored streams" << std::endl;
    status = client.SendCommandPacket(CommandPacketEraseAllStoredStreams());
    if (status != OKAY) {
      std::cerr << "Failed to delete all stored streams: " << ErrorMessage(status) << std::endl;
      return 202;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    retStreams = GetStreamList(client, receiver, IDs, 5.0);
    if (!IDs.empty()) {
      std::cerr << "Could not delete all stored streams, " << IDs.size() << " remain" << std::endl;
      return 203;
    }

    // Trying to delete a stream this is currently being recorded should fail.
    std::cout << "Trying to delete a stream that is currently being recorded" << std::endl;
    status = client.SendCommandPacket(CommandPacketStartRecording());
    if (status != OKAY) {
      std::cerr << "Failed to send start recording command: " << ErrorMessage(status) << std::endl;
      return 205;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    status = client.SendCommandPacket(CommandPacketEraseAllStoredStreams());
    if (status != OKAY) {
      std::cerr << "Failed to delete all stored streams: " << ErrorMessage(status) << std::endl;
      return 206;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    retStreams = GetStreamList(client, receiver, IDs, 5.0);
    if (IDs.size() != 1) {
      std::cerr << "Error: Expected 1 stored stream, found " << IDs.size() << std::endl;
      return 207;
    }

    // Turn off storing after ten seconds so that we get some data but don't fill up the disk.
    std::cout << "Recording for ten seconds to get sample data set" << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration<double>(now - start).count() < 10.0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      now = std::chrono::high_resolution_clock::now();
    }
    std::cout << "Turning off storing" << std::endl;
    status = client.SendCommandPacket(CommandPacketStopRecording());
    if (status != OKAY) {
      std::cerr << "Failed to send start/stop recording command: " << ErrorMessage(status) << std::endl;
      return 209;
    }

    // If the system supports temperature and poses, request them.
    if (features[TEMPERATURE_API_AVAILABLE]) {
      std::cout << "Requesting temperature" << std::endl;
      status = client.SendCommandPacket(CommandPacketStreamTemperatures());
      if (status != OKAY) {
        std::cerr << "Failed to send request temperature command: " << ErrorMessage(status) << std::endl;
        return 210;
      }
    }
    if (features[POSE_API_ORIENTATION_AVAILABLE]) {
      std::cout << "Requesting orientation" << std::endl;
      status = client.SendCommandPacket(CommandPacketStreamPoses());
      if (status != OKAY) {
        std::cerr << "Failed to send request orientation command: " << ErrorMessage(status) << std::endl;
        return 211;
      }
    }

    status = client.SendCommandPacket(CommandPacketStopReplay());
    if (status != OKAY) {
      std::cerr << "Failed to send stop replay command: " << ErrorMessage(status) << std::endl;
      return 15;
    }

    // Find the list of stored streams and make sure that we have at least one.
    // This will also gobble up any state and other messages that stacked up while we were doing other things.
    retStreams = GetStreamList(client, receiver, IDs, 5.0);
    if (!retStreams.empty()) {
      std::cerr << retStreams << std::endl;
      return 300;
    }
    if (IDs.empty()) {
      std::cerr << "No stored streams: expected at least one." << std::endl;
      return 301;
    }

    // Compute a time offset that is 10000 seconds in the future compared to the current stream time.
    Time currentTime;
    status = state.GetTime(currentTime);
    if (status != OKAY) {
      std::cerr << "Failed to get current time: " << ErrorMessage(status) << std::endl;
      return 302;
    }

    // Request replay on the first stored stream, requesting that packets have a time offset of 10000 seconds.
    // Make sure that we get a start-of-replay event.
    // Make sure that we get a state message that says we are replaying after that event.
    // Verify that the time on that message is larger than the time we requested.
    std::cout << "Requesting replay of first stored stream" << std::endl;
    Time timeOffset(10000 + currentTime.seconds, 0);
    status = client.SendCommandPacket(CommandPacketStartReplay(IDs.front(), timeOffset));
    if (status != OKAY) {
      std::cerr << "Failed to send start replay command: " << ErrorMessage(status) << std::endl;
      return 400;
    }
    if (!WaitForEventType(receiver, START_OF_REPLAY, 5.0).empty()) {
      std::cerr << "Did not get start-of-replay event." << std::endl;
      return 401;
    }
    msg = WaitForMessageType(receiver, STATE, 5.0);
    if (msg == nullptr) {
      std::cerr << "Did not get state message." << std::endl;
      return 403;
    }
    state = MessageState(*msg);
    if (state.GetConstructorStatus() != OKAY) {
      std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
      return 404;
    }
    uint8_t replaying;
    status = state.GetReplaying(replaying);
    if (status != OKAY) {
      std::cerr << "Failed to get replaying: " << ErrorMessage(status) << std::endl;
      return 405;
    }
    if (!replaying) {
      std::cerr << "Replay did not start" << std::endl;
      return 406;
    }
    Time replayTime;
    status = state.GetTime(replayTime);
    if (status != OKAY) {
      std::cerr << "Failed to get time: " << ErrorMessage(status) << std::endl;
      return 407;
    }
    if (replayTime < timeOffset) {
      std::cerr << "Replay time is less than requested time offset: "
        << replayTime.seconds << ":" << replayTime.microseconds << " vs. "
        << timeOffset.seconds << ":" << timeOffset.microseconds << std::endl;
      return 408;
    }

    // Wait a few seconds and then stop replay.
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Stop replay.
    // Make sure that we get a start-of-replay event.
    // Make sure that we get a state message that says we are replaying after that event.
    // Make sure that the time on that message is smaller than the time we requested (time shifted back).
    std::cout << "Stopping replay" << std::endl;
    status = client.SendCommandPacket(CommandPacketStopReplay());
    if (status != OKAY) {
      std::cerr << "Failed to send stop replay command: " << ErrorMessage(status) << std::endl;
      return 500;
    }
    if (!WaitForEventType(receiver, END_OF_REPLAY, 5.0).empty()) {
      std::cerr << "Did not get end-of-replay event." << std::endl;
      return 501;
    }
    msg = WaitForMessageType(receiver, STATE, 5.0);
    if (msg == nullptr) {
      std::cerr << "Did not get state message." << std::endl;
      return 502;
    }
    state = MessageState(*msg);
    if (state.GetConstructorStatus() != OKAY) {
      std::cerr << "Failed to construct state message: " << ErrorMessage(state.GetConstructorStatus()) << std::endl;
      return 503;
    }
    status = state.GetReplaying(replaying);
    if (status != OKAY) {
      std::cerr << "Failed to get replaying: " << ErrorMessage(status) << std::endl;
      return 504;
    }
    if (replaying) {
      std::cerr << "Replay did not stop" << std::endl;
      return 505;
    }
    status = state.GetTime(replayTime);
    if (status != OKAY) {
      std::cerr << "Failed to get time: " << ErrorMessage(status) << std::endl;
      return 506;
    }
    if (replayTime >= timeOffset) {
      std::cerr << "Replay time is not less than requested time offset: "
        << replayTime.seconds << ":" << replayTime.microseconds << " vs. "
        << timeOffset.seconds << ":" << timeOffset.microseconds << std::endl;
      return 507;
    }

  } // End of destructive testing section

  /// @todo Add more tests here.


#if 0
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
    start = std::chrono::high_resolution_clock::now();
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
        std::cerr << "Error getting type from message: " << ErrorMessage(status) << std::endl;
        return 36;
      }
      if (rID == asdp::FRAME_BEGIN) {
        numStartFrames++;
      }
    } while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count() <= 0.5);
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
#endif

  std::cout << std::endl << "Success!" << std::endl;
  return 0;
}
