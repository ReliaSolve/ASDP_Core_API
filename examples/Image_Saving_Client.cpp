/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is a client that connects to the first server it encounters and stores images from
// the data stream coming from one of the cameras.  It gets a subset of the frames from the camera
// over time so that it can keep up.

#include <iostream>
#include <chrono>
#include <cstdio>
#include <thread>
#include <list>
#include <mutex>
#include <memory>
#include <atomic>
#include <string.h>
#include <ASDP_Core_API.h>
#include <ASDP_StreamPacketSortedQueue.h>

using namespace asdp;

//==============================================================================
// Utility functions

std::shared_ptr<Message> WaitForMessageType(std::shared_ptr<Receiver> receiver, MessageID type, float seconds)
{
  std::shared_ptr<Message> empty;   ///< We return this on failure.
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
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
  } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= seconds);

  return empty;
}

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

static bool isBigEndian() {
  union {
    uint32_t i;
    char c[4];
  } testUnion = { 0x01020304 };

  return testUnion.c[0] == 1;
}

static void fixEndian(uint8_t *data, size_t size) {
  if (!isBigEndian()) {
    uint16_t *data16 = reinterpret_cast<uint16_t *>(data);
    for (size_t i = 0; i < size / 2; i++) {
      uint16_t& value = data16[i];
      value = (value >> 8) | (value << 8);
    }
  }
}

//==============================================================================
// Helper classes and thread function to save images to disk without blocking the
// main thread.

struct FileData {
  std::string fileName;   ///< The name of the file to write to.
  uint8_t *data;          ///< We use a raw pointer here because allocating a vector is too slow.
  size_t size;            ///< The size of the data in bytes.
};

std::list< std::shared_ptr<FileData> > fileDataList;
std::mutex fileDataMutex;
std::atomic_bool done{false};
std::thread saveFileThread;

void SaveFileThread()
{
  while (!done) {
    std::shared_ptr<FileData> fileData;
    {
      if (fileDataList.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      std::lock_guard<std::mutex> lock(fileDataMutex);
      fileData = fileDataList.front();
      fileDataList.pop_front();
    }
    fixEndian(fileData->data, fileData->size);
    FILE* f = fopen(fileData->fileName.c_str(), "wb");
    if (f == NULL) {
      std::cerr << "Error opening image file " << fileData->fileName << std::endl;
      return;
    }
    fprintf(f, "P5\n%d %d\n%d\n", 1280, 1024, 65535);
    fwrite(fileData->data, sizeof(uint8_t), fileData->size, f);
    fclose(f);
    delete[] fileData->data;
    std::cout << "Wrote " << fileData->fileName << std::endl;
  }
  std::cout << "SaveFileThread done" << std::endl;
}

//==============================================================================
// Helper classes and thread function to save raw packet data to disk without blocking the
// main thread.

std::list< std::shared_ptr<StreamPacket> > rawDataList;
std::mutex rawDataMutex;
std::thread saveRawDataThread;

void SaveRawDataThread(std::shared_ptr<SenderFile> sender)
{
  while (!done && (sender != nullptr)) {
    std::shared_ptr<StreamPacket> packet;
    {
      if (rawDataList.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      std::lock_guard<std::mutex> lock(rawDataMutex);
      packet = rawDataList.front();
      rawDataList.pop_front();
      Status ret = sender->SendStreamPacket(*packet);
      if (ret != OKAY) {
        std::cerr << "SaveRawDataThread(): Error sending raw data packet: " << ErrorMessage(ret) << std::endl;
        done = true;
      }
    }
  }
  std::cout << "SaveRawDataThread done" << std::endl;
}

//==============================================================================

void usage(const char* name)
{
  std::cerr << "Usage: " << name << " [--replay R] [--rawSave F] [--frameStride S] [--durationSeconds D] <ip_address>" << std::endl;
  exit(1);
}

int main(int argc, char** argv)
{
  uint32_t frameStride = 30;    ///< Read one out of every this many frames. Set to 1 for every frame.
  float durationSeconds = 10;   ///< Run for this many seconds
  std::string ip_address;
  int replayID = 0;
  std::string rawSaveFileName; ///< If set, save raw packets to this file.
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the IP address to listen on.
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == std::string("--replay")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --replay" << std::endl;
        return 1;
      }
      replayID = std::stoi(argv[i]);
    } else if (argv[i] == std::string("--rawSave")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --rawSave" << std::endl;
        return 1;
      }
      rawSaveFileName = argv[i];
    } else if (argv[i] == std::string("--frameStride")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --frameStride" << std::endl;
        return 1;
      }
      frameStride = std::stoi(argv[i]);
    } else if (argv[i] == std::string("--durationSeconds")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --durationSeconds" << std::endl;
        return 1;
      }
      durationSeconds = std::stof(argv[i]);
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
  std::cout << "  Connected to server version " << major << "." << minor << "." << patch << std::endl;

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

  // If we have at least one camera, try streaming data from it at its highest rate.
  if (cameras.empty()) {
    std::cerr << "No cameras found." << std::endl;
    return 36;
  }
  uint32_t camID = 1;
  {
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

    std::cout << "Saving every " << frameStride << " images from camera " << camID << " on port " << port
      << " for " << durationSeconds << " seconds" << std::endl;

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
      return 32;
    }

    // Start a thread to save the images to disk.
    saveFileThread = std::thread(SaveFileThread);

    // If we have a rawSaveFileName, we will also save the raw packets to disk.
    if (!rawSaveFileName.empty()) {
      std::shared_ptr<SenderFile> sender = std::make_shared<SenderFile>(rawSaveFileName);
      if (sender == nullptr) {
        std::cerr << "Failed to construct SenderFile " << rawSaveFileName << " for raw data" << std::endl;
        return 33;
      }
      saveRawDataThread = std::thread(SaveRawDataThread, sender);
    }

    // Use a sorting queue to ensure that we process the messages in order even if the UDP packets
    // arrive out of order.
    StreamPacketSortedQueue sortedQueue(50);

    // Process frames until we've run for the specified duration.
    // We initialize the data every FRAME_BEGIN, accumulate over all FRAME_DATA, and complete at FRAME_END
    start = std::chrono::steady_clock::now();
    size_t sequenceNumber = 0;
    size_t numFrames = 0;
    std::string fileName;
    std::shared_ptr<FileData> fileData;
    do {
      // Service the main receiver, gobbling up any packets.
      std::shared_ptr<asdp::StreamPacket> mainPacket;
      size_t offset = 0;
      status = receiver->ReceiveStreamPacket(0, mainPacket, offset);
      if (status != asdp::OKAY && status != asdp::TIMEOUT) {
        std::cerr << "Error receiving main StreamPacket: " << ErrorMessage(status) << std::endl;
        return 100;
      }

      // Get the next UDP packet.
      // Add to the sorted queue and then handle any messages that are ready to be processed.
      std::shared_ptr<asdp::StreamPacket> packet;
      offset = 0;
      status = receiverUDP.ReceiveStreamPacket(0.0, packet, offset);
      if (status == asdp::TIMEOUT) {
        continue;
      }
      if (status != asdp::OKAY) {
        std::cerr << "Error receiving StreamPacket: " << ErrorMessage(status) << std::endl;
        return 33;
      }
      std::list< std::shared_ptr<StreamPacket> > readyPackets = sortedQueue.AddPacket(packet);
      if (readyPackets.size() > 1) {
        std::cerr << "Warning: More than one packet ready to process (re-ordered or missing packet)." << std::endl;
      }
      while (!readyPackets.empty()) {

        std::shared_ptr<asdp::StreamPacket> receiveStreamPacket = readyPackets.front();
        readyPackets.pop_front();

        // Save the raw packet to disk if requested.
        if (!rawSaveFileName.empty()) {
          std::lock_guard<std::mutex> lock(rawDataMutex);
          rawDataList.push_back(receiveStreamPacket);
        }

        // Get and handle all messages from the packet.
        std::shared_ptr<asdp::Message> message;
        status = receiveStreamPacket->GetNextMessage(message);
        if (status != asdp::OKAY) {
          std::cerr << "Error getting message from packet: " << ErrorMessage(status) << std::endl;
          return 36;
        }
        while (message != nullptr) {
          asdp::MessageID rID;
          status = message->GetType(rID);
          if (status != asdp::OKAY) {
            std::cerr << "Error getting type from message: " << ErrorMessage(status) << std::endl;
            return 37;
          }
          switch (rID) {
          case asdp::CONSOLIDATED_FRAME_DATA:
          {
            MessageConsolidatedFrameData frameData(*message);
            if (frameData.GetConstructorStatus() != asdp::OKAY) {
              std::cerr << "Error constructing FrameData message: " << ErrorMessage(frameData.GetConstructorStatus()) << std::endl;
              return 38;
            }
            bool isBeginFrame;
            status = frameData.GetBeginFrameFlag(isBeginFrame);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting begin frame flag from FrameData message: " << ErrorMessage(status) << std::endl;
              return 39;
            }
            if (isBeginFrame) {
              fileName = "saved_image_" + std::to_string(numFrames) + ".pgm";
              fileData.reset(new FileData);
              fileData->fileName = fileName;
              size_t size = (cameras[camID - 1].width * cameras[camID - 1].height) * sizeof(uint16_t);
              fileData->data = new uint8_t[size];
              fileData->size = size;
            }

            // Don't do anything if we haven't created fileData yet.
            if (fileData == nullptr) { break; }

            // Find out how many pixels are in the frame and sum their values.
            uint16_t stride = cameras[camID - 1].width;
            uint16_t left, right, top, bottom;
            status = frameData.GetLeft(left);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting left from FrameData message: " << ErrorMessage(status) << std::endl;
              return 40;
            }
            status = frameData.GetRight(right);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting right from FrameData message: " << ErrorMessage(status) << std::endl;
              return 41;
            }
            status = frameData.GetTop(top);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting top from FrameData message: " << ErrorMessage(status) << std::endl;
              return 42;
            }
            status = frameData.GetBottom(bottom);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting bottom from FrameData message: " << ErrorMessage(status) << std::endl;
              return 43;
            }
            uint8_t* rawData;
            status = frameData.GetDataPointer(rawData);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting data pointer from FrameData message: " << ErrorMessage(status) << std::endl;
              return 44;
            }
            // NOTE: This makes use of the fact that we're asking for the full frame, and that the server sends
            // full lines at once when this is the case.  Otherwise, we'd need to copy the data line by line and
            // adjust for the full-image stride when doing offsets.
            size_t size = (right - left + 1) * (bottom - top + 1) * sizeof(uint16_t);
            memcpy(fileData->data + (top * stride + left) * sizeof(uint16_t), rawData, size);

            bool isEndFrame;
            status = frameData.GetEndFrameFlag(isEndFrame);
            if (status != asdp::OKAY) {
              std::cerr << "Error getting end frame flag from FrameData message: " << ErrorMessage(status) << std::endl;
              return 45;
            }
            if (isEndFrame) {
              // Don't do anything if we haven't created fileData yet.
              if (fileData == nullptr) { break; }
              numFrames++;
              std::cout << "Writing " << fileName << std::endl;
              std::lock_guard<std::mutex> lock(fileDataMutex);
              fileDataList.push_back(fileData);
            }
          }
          break;
          default:
            break;
          }

          status = receiveStreamPacket->GetNextMessage(message);
          if (status != asdp::OKAY) {
            std::cerr << "Error getting first message from packet: " << ErrorMessage(status) << std::endl;
            return 45;
          }
        }
      }

    } while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <= durationSeconds);

    // Stop our saving threads.
    done = true;
    saveFileThread.join();
    if (saveRawDataThread.joinable()) { saveRawDataThread.join(); }
    std::cout << "Wrote " << numFrames << " images." << std::endl;

    // Turn off streaming.
    status = client.SendCommandPacket(CommandPacketCancelSubregion(camID, endpoint));
    if (status != OKAY) {
      std::cerr << "Failed to cancel stream images: " << ErrorMessage(status) << std::endl;
      return 46;
    }
  }

  std::cout << std::endl << "Success!" << std::endl;
  return 0;
}
