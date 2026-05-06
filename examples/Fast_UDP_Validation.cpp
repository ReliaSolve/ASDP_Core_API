/*
 * Copyright (C) 2026: Arizona Board of Regents on Behalf of the University of Arizona
 */

 // This is a client that connects to the first server it encounters and requests streaming
 // from the specified maximum number of cameras.  It then uses either the standard UDPReceiver
 // or a custom receiver thread to receive the packets and reports whether any packets were lost.
 // It can be told at the command line to replay a stored stream.

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

#ifdef _WIN32
// Include files needed to support RIO on Windows.
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>  // <-- ADD THIS LINE for inet_pton
#endif

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

void StreamReceiverThread(std::string ip_address, uint16_t port, bool fast, std::atomic_bool& done)
{
  size_t packets_received = 0;

#ifdef _WIN32

  // Allocate these here outside of a fast block because we need them inside multiple fast blocks below.
  SOCKET sock;
  const int SEGMENT_SIZE = 9000;
  const int SEGMENTS = 512;

  // RIO variables
  RIO_BUFFERID rioBufferId = nullptr;
  RIO_BUFFERID rioAddrId = nullptr;
  RIO_EXTENSION_FUNCTION_TABLE rio;

  struct Segment {
    RIO_BUF dataSeg;
    RIO_BUF addrSeg;  // Need this to get sender address from first packet
  };
  Segment segments[SEGMENTS];

  RIO_CQ cq = RIO_INVALID_CQ;
  RIO_RQ rq = RIO_INVALID_RQ;

  char* buffer = nullptr;
  char* addrBuf = nullptr;
  HANDLE completionEvent = nullptr;

  bool socket_connected = false;

  if (fast) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
    if (sock == INVALID_SOCKET) {
      std::cerr << "Error creating RIO socket: " << WSAGetLastError() << std::endl;
      done = true;
      return;
    }

    int recvBufferSize = 32 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&recvBufferSize, sizeof(recvBufferSize));

    StreamEndpoint localEndpoint(ip_address, port);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(localEndpoint.IP);
    addr.sin_port = htons(port);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
      std::cerr << "Error binding RIO socket: " << WSAGetLastError() << std::endl;
      closesocket(sock);
      done = true;
      return;
    }

    GUID rioGuid = WSAID_MULTIPLE_RIO;
    DWORD bytes = 0;
    WSAIoctl(sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
             &rioGuid, sizeof(rioGuid), &rio, sizeof(rio), &bytes, nullptr, nullptr);

    completionEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    RIO_NOTIFICATION_COMPLETION completionType;
    completionType.Type = RIO_EVENT_COMPLETION;
    completionType.Event.EventHandle = completionEvent;
    completionType.Event.NotifyReset = TRUE;

    cq = rio.RIOCreateCompletionQueue(SEGMENTS, &completionType);
    rq = rio.RIOCreateRequestQueue(sock, SEGMENTS, 1, 0, 1, cq, cq, nullptr);

    buffer = (char*)VirtualAlloc(nullptr, SEGMENT_SIZE * SEGMENTS, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    rioBufferId = rio.RIORegisterBuffer(buffer, SEGMENT_SIZE * SEGMENTS);

    // Allocate address buffer for getting sender info
    const int ADDR_SIZE = 128;
    addrBuf = (char*)VirtualAlloc(nullptr, SEGMENTS * ADDR_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    rioAddrId = rio.RIORegisterBuffer(addrBuf, SEGMENTS * ADDR_SIZE);

    for (int i = 0; i < SEGMENTS; i++) {
      segments[i].dataSeg.BufferId = rioBufferId;
      segments[i].dataSeg.Offset = i * SEGMENT_SIZE;
      segments[i].dataSeg.Length = SEGMENT_SIZE;

      segments[i].addrSeg.BufferId = rioAddrId;
      segments[i].addrSeg.Offset = i * ADDR_SIZE;
      segments[i].addrSeg.Length = ADDR_SIZE;
    }

    // Post initial receives with address buffers so that we can get the sender
    // address from them below.
    for (int i = 0; i < SEGMENTS; i++) {
      rio.RIOReceiveEx(rq, &segments[i].dataSeg, 1, nullptr, &segments[i].addrSeg,
                       nullptr, nullptr, 0, (PVOID)(uintptr_t)i);
    }

    rio.RIONotify(cq);
  }
#endif

  if (fast) {
#ifdef _WIN32
    const int BATCH = SEGMENTS;
    RIORESULT results[BATCH];

    while (!done) {
      // Wait for the event to be signaled (with timeout for checking done flag)
      DWORD waitResult = WaitForSingleObject(completionEvent, 100);  // 100ms timeout

      if (waitResult == WAIT_FAILED) {
        std::cerr << "WaitForSingleObject failed: " << GetLastError() << std::endl;
        done = true;
        return;
      }

      if (waitResult == WAIT_TIMEOUT) {
        // No completions ready, check done flag and loop
        continue;
      }

      // Event was signaled, dequeue all available completions
      ULONG n;
      do {
        n = rio.RIODequeueCompletion(cq, results, BATCH);

        if (n == RIO_CORRUPT_CQ) {
          std::cerr << "Corrupt completion queue detected" << std::endl;
          done = true;
          return;
        }

        if (n > 0) {
          packets_received += n;

          // Connect socket after first packet
          if (!socket_connected) {
            int idx = (int)(uintptr_t)results[0].RequestContext;
            sockaddr_in* senderAddr = (sockaddr_in*)(addrBuf + segments[idx].addrSeg.Offset);

            if (connect(sock, (sockaddr*)senderAddr, sizeof(sockaddr_in)) == 0) {
              socket_connected = true;
              std::cout << "Socket connected to sender for optimized processing to "
                << inet_ntoa(senderAddr->sin_addr) << ":" << ntohs(senderAddr->sin_port)
                << std::endl;

              // Now repost using RIOReceive (faster) instead of RIOReceiveEx
              for (ULONG i = 0; i < n; i++) {
                int idx = (int)(uintptr_t)results[i].RequestContext;
                rio.RIOReceive(rq, &segments[idx].dataSeg, 1, 0, (PVOID)(uintptr_t)idx);
              }
            } else {
              std::cerr << "Failed to connect socket: " << WSAGetLastError() << std::endl;
              // Continue with RIOReceiveEx
              for (ULONG i = 0; i < n; i++) {
                int idx = (int)(uintptr_t)results[i].RequestContext;
                rio.RIOReceiveEx(rq, &segments[idx].dataSeg, 1, nullptr, &segments[idx].addrSeg,
                                 nullptr, nullptr, 0, (PVOID)(uintptr_t)idx);
              }
            }
          } else {
            // Socket already connected, use faster RIOReceive
            for (ULONG i = 0; i < n; i++) {
              int idx = (int)(uintptr_t)results[i].RequestContext;
              rio.RIOReceive(rq, &segments[idx].dataSeg, 1, 0, (PVOID)(uintptr_t)idx);
            }
          }
        }
      } while (n > 0);

      rio.RIONotify(cq);
    }
#endif
  }

  // Cleanup code...
#ifdef _WIN32
  if (fast) {
    if (cq != RIO_INVALID_CQ) rio.RIOCloseCompletionQueue(cq);
    if (rioBufferId != RIO_INVALID_BUFFERID) rio.RIODeregisterBuffer(rioBufferId);
    if (rioAddrId != RIO_INVALID_BUFFERID) rio.RIODeregisterBuffer(rioAddrId);
    if (buffer) VirtualFree(buffer, 0, MEM_RELEASE);
    if (addrBuf) VirtualFree(addrBuf, 0, MEM_RELEASE);
    if (rq != RIO_INVALID_RQ) closesocket(sock);
    if (completionEvent) CloseHandle(completionEvent);
  }
#endif

  std::cout << "Receiver thread received " << packets_received << " packets." << std::endl;
}

//==============================================================================

void usage(const char* name)
{
  std::cerr << "Usage: " << name << " [--duration S] [--maxCameras C] [--replay R] [--fast] <ip_address>" << std::endl;
  exit(1);
}

int main(int argc, char** argv)
{
  float durationSeconds = 30;   ///< Run for this many seconds
  std::string ip_address;
  size_t realParams = 0;
  uint32_t maxCameras = 0;
  int replayID = 0;
  bool fast = false;
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
    }
    else if (argv[i] == std::string("--maxCameras")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --maxCameras" << std::endl;
        return 1;
      }
      maxCameras = std::stoul(argv[i]);
    }
    else if (argv[i] == std::string("--replay")) {
      if (++i >= argc) {
        std::cerr << "Missing argument for --replay" << std::endl;
        return 1;
      }
      replayID = std::stoi(argv[i]);
    }
    else if (argv[i] == std::string("--fast")) {
      fast = true;
    }
    else if (argv[i][0] == '-') {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    }
    else switch (realParams++) {
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
  if ((maxCameras > 0) && (maxCameras < cameras.size())) {
    std::cout << "Limiting to " << maxCameras << " cameras." << std::endl;
    cameras.resize(maxCameras);
  }

  // Start receiver threads for each camera, remembering which port each is listening on.
  // Also construct statistics vectors for each camera.
  std::vector<std::thread> receiverThreads;
  std::vector<uint16_t> ports(cameras.size());
  for (size_t i = 0; i < cameras.size(); ++i) {
    ports[i] = 10111 + (uint16_t)i;
    receiverThreads.push_back(std::thread(StreamReceiverThread, ip_address, ports[i], fast, std::ref(done)));
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

  if (fast) {
#ifdef _WIN32
    std::cout << "Using RIO for UDP reception." << std::endl;
#else
    std::cerr << "Fast mode is only supported on Windows with RIO." << std::endl;
    return 60;
#endif
  } else {
    std::cout << "Using standard ReceiverUDP approach." << std::endl;
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

  return 0;
}
