/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include "asdp_api.h"
#include <string.h>   // For memcpy
#include <iostream>

using namespace asdp;

//----------------------------------------------------------------------------
/// Error handling.

std::string asdp::ErrorMessage(Status status)
{
  switch (status) {
  case OKAY:
    return "No error";

  case TIMEOUT:
    return "Timeout";

  case THREAD_COMPLETED:
    return "Thread completed";

  case BAD_PARAMETER:
    return "Bad parameter";
  case OUT_OF_MEMORY:
    return "Out of memory";
  case NOT_IMPLEMENTED:
    return "Feature not yet implemented";
  case DELETION_FAILED:
    return "Pointer deletion failed";
  case NULL_OBJECT_POINTER:
    return "Object method called with NULL object pointer";
  case INTERNAL_EXCEPTION:
    return "Exception thrown inside implementation";
  case SOCKET_FAILURE:
    return "Socket error";
  case READ_PAST_END:
    return "Attempt to read past end of buffer";
  case BAD_COOKIE:
    return "Bad magic cookie in packet";
  case WRITE_PAST_END:
    return "Attempt to write past end of buffer";
  case SOCKET_READ_FAILURE:
    return "Read error on socket; perhaps client buffer too small";
  case FILE_FAILURE:
    return "File error";
  case UNEXPECTED_INTERNAL_STATE:
    return "Unexpected internal state";
  case INCORRECT_ENDIANNESS:
    return "This architecture is not little-endian";
  case NOT_CONNECTED:
    return "Not connected";
  case INCORRECT_FLOAT_SIZE:
    return "This architecture does not have 32-bit floats";

  default:
    return "Unrecognized error code: " + std::to_string(status);
  }
}

//----------------------------------------------------------------------------
// Definitions of static constants used below.

static const unsigned char MAGIC_COOKIE[4] = { 'A', 'S', 'D', 'P' };
static const unsigned char VERSION[4] = { 1, 0, 0, 0 };

static const uint32_t PACKET_BASIC_HEADER_SIZE = 4 * sizeof(uint32_t);
static const uint32_t PACKET_HEADER_MAGIC_COOKIE_OFFSET = 0;
static const uint32_t PACKET_HEADER_VERSION_OFFSET = 4;
static const uint32_t PACKET_HEADER_TOTAL_SIZE_OFFSET = 8;
static const uint32_t PACKET_HEADER_HEADER_SIZE_OFFSET = 12;
static const uint32_t COMMAND_PACKET_BASE_SIZE = PACKET_BASIC_HEADER_SIZE + sizeof(uint32_t);
static const uint32_t STREAM_PACKET_BASE_SIZE = PACKET_BASIC_HEADER_SIZE + 3 * sizeof(uint32_t);

static const uint32_t MESSAGE_BASE_SIZE = 6 * sizeof(uint32_t);
static const uint32_t MESSAGE_HEADER_VERSION_OFFSET = 0;
static const uint32_t MESSAGE_HEADER_MESSAGE_TOTAL_SIZE_OFFSET = 4;
static const uint32_t MESSAGE_HEADER_MESSAGE_HEADER_SIZE_OFFSET = 8;
static const uint32_t MESSAGE_HEADER_MESSAGE_TIME_SECONDS_OFFSET = 12;
static const uint32_t MESSAGE_HEADER_MESSAGE_TIME_MICROSECONDS_SIZE_OFFSET = 16;
static const uint32_t MESSAGE_HEADER_MESSAGE_TYPE_OFFSET = 20;

//----------------------------------------------------------------------------
// Figure out whether we're using Windows sockets or not.

// let's start with a clean slate
#undef ASDP_USE_WINSOCK_SOCKETS

// Does cygwin use winsock sockets or unix sockets?  Define this before
// compiling the library if you want it to use WINSOCK sockets.
//#define CYGWIN_USES_WINSOCK_SOCKETS

#if defined(_WIN32) && (!defined(__CYGWIN__) || defined(CYGWIN_USES_WINSOCK_SOCKETS))
#define ASDP_USE_WINSOCK_SOCKETS
#endif

//--------------------------------------
// Architecture-dependent include files.

#ifndef ASDP_USE_WINSOCK_SOCKETS
#include <sys/time.h>   // for timeval, timezone, gettimeofday
#include <sys/select.h> // for fd_set
#include <netinet/in.h> // for htonl, htons
#include <poll.h>       // for poll()
#include <netdb.h>      // for addrinfo and related functions
#include <unistd.h>     // for close()
#endif

#ifdef ASDP_USE_WINSOCK_SOCKETS
  // These are a pair of horrible hacks that instruct Windows include
  // files to (1) not define min() and max() in a way that messes up
  // standard-library calls to them, and (2) avoids pulling in a large
  // number of Windows header files.  They are not used directly within
  // the Sockets library, but rather within the Windows include files to
  // change the way they behave.

#ifndef NOMINMAX
#define ASDP_CORESOCKET_REPLACE_NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define ASDP_CORESOCKET_REPLACE_WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h> // struct timeval is defined here
#include "Ws2Tcpip.h"
#pragma comment(lib,"WS2_32")
#ifdef ASDP_CORESOCKET_REPLACE_NOMINMAX
#undef NOMINMAX
#endif
#ifdef ASDP_CORESOCKET_REPLACE_WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif

#endif

//----------------------------------------------------------------------------
// Architecture-dependent definitions.

#ifndef ASDP_USE_WINSOCK_SOCKETS

  // On Winsock, we have to use SOCKET, so we're going to have to use it
  // everywhere.
typedef int SOCKET;
// On Winsock, INVALID_SOCKET is #defined as ~0 (sockets are unsigned ints)
// We can't redefine it locally, so we have to switch to another name
static const int BAD_SOCKET = -1;
static const int SOCKET_ERROR = -1;
#define closesocket close
#else // winsock sockets

  // Bring the SOCKET type into our namespace, basing it on the root namespace one.
typedef SOCKET SOCKET;

// Make a namespaced INVALID_SOCKET definition, which cannot be just
// INVALID_SOCKET because Windows #defines it, so we pick another name.
static const SOCKET BAD_SOCKET = INVALID_SOCKET;
#endif

//--------------------------------------------------------------
// Ensures that someone calls WSAStartup on Windows before using
// any socket code.
#if defined(ASDP_USE_WINSOCK_SOCKETS)
class WSAStart {
public:
  WSAStart() {
    WSADATA wsaData;
    int winStatus;

    winStatus = WSAStartup(MAKEWORD(1, 1), &wsaData);
    if (winStatus) {
      std::cerr << "ASDP_Core: Failed to set up sockets: WSAStartup failed with error code " << winStatus << std::endl;
    }
  }
  // We do not call WSACleanup() because we don't know that we're the only
  // user of the sockets library.
};
static WSAStart startUp;
#endif

//----------------------------------------------------------------------------
// Helper functions.

/// @brief Helper function to determine the size of the buffer needed to hold a message,
/// which can include padding to align the message on a 4-byte boundary.
static size_t PaddedSize(size_t size)
{
  size_t padding = 4 - (size % 4);
  if (padding == 4) {
    padding = 0;
  }
  return size + padding;
}

/// @brief Helper function to unpack the version sections.
static void UnpackVersion(const uint8_t *version, uint16_t& major, uint16_t& minor, uint16_t& patch)
{
  major = version[0];
  minor = version[1] + (version[2] * static_cast<uint16_t>(256));
  patch = version[3];
}

//----------------------------------------------------------------------------
// API functions

Timer::Timer()
  : m_coreOffset({0, 0})
{
}

Status Timer::GetCoreTime(Time& core_time, const std::chrono::steady_clock::time_point local_time) const
{
  // Get the local time into a Time.
  Time localTime = {
    (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(local_time.time_since_epoch()).count() / 1000000,
    (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(local_time.time_since_epoch()).count() % 1000000
  };

  // Verify that the local time is not before the core offset.
  if (localTime < m_coreOffset) {
    return BAD_PARAMETER;
  }

  // Subtract the core offset.
  core_time = localTime - m_coreOffset;
  return OKAY;
}

Status Timer::SetCoreOffset(Time offset)
{
  // Ensure that the offset is not too large.
  std::chrono::steady_clock::time_point local_time = std::chrono::steady_clock::now();
  Time localTime = {
    (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(local_time.time_since_epoch()).count() / 1000000,
    (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(local_time.time_since_epoch()).count() % 1000000
  };
  if (offset > localTime) {
    return BAD_PARAMETER;
  }

  m_coreOffset = offset;
  return OKAY;
}

Timer::~Timer()
{
}

std::string Timer::Test()
{
  // Test the + and += operators on the Time class.
  {
    Time time1 = { 1, 2 };
    Time time2 = { 3, 4 };
    Time time3 = time1 + time2;
    if (time3.seconds != 4 || time3.microseconds != 6) {
      return "Error adding times: " + std::to_string(time3.seconds) + "." + std::to_string(time3.microseconds);
    }
    time1 += time2;
    if (time1.seconds != 4 || time1.microseconds != 6) {
      return "Error adding times: " + std::to_string(time1.seconds) + "." + std::to_string(time1.microseconds);
    }
  }

  // Test the - and -= operators on the Time class, including cases where the microseconds must borrow.
  {
    Time time1 = { 1, 2 };
    Time time2 = { 3, 4 };
    Time time3 = time2 - time1;
    if (time3.seconds != 2 || time3.microseconds != 2) {
      return "Error subtracting times: " + std::to_string(time3.seconds) + "." + std::to_string(time3.microseconds);
    }
    time2 -= time1;
    if (time2.seconds != 2 || time2.microseconds != 2) {
      return "Error subtracting times: " + std::to_string(time2.seconds) + "." + std::to_string(time2.microseconds);
    }
    time1 = { 1, 2 };
    time2 = { 0, 4 };
    time3 = time1 - time2;
    if (time3.seconds != 0 || time3.microseconds != 999998) {
      return "Error subtracting times: " + std::to_string(time3.seconds) + "." + std::to_string(time3.microseconds);
    }
    time1 -= time2;
    if (time1.seconds != 0 || time1.microseconds != 999998) {
      return "Error subtracting times: " + std::to_string(time1.seconds) + "." + std::to_string(time1.microseconds);
    }
  }

  // Test the Timer class methods for cases that should work and cases that should fail.
{
    Timer timer;
    Time coreTime;
    std::chrono::steady_clock::time_point localTime = std::chrono::steady_clock::now();
    Time localTimeStruct = {
      (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(localTime.time_since_epoch()).count() / 1000000,
      (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(localTime.time_since_epoch()).count() % 1000000
    };

    // Test the GetCoreTime method.
    Status status = timer.GetCoreTime(coreTime, localTime);
    if (status != OKAY) {
      return "Error getting core time: " + ErrorMessage(status);
    }
    if (coreTime != localTimeStruct) {
      return "Error getting core time: " + std::to_string(coreTime.seconds) + "." + std::to_string(coreTime.microseconds);
    }

    // Test the SetCoreOffset method.
    status = timer.SetCoreOffset(localTimeStruct);
    if (status != OKAY) {
      return "Error setting core offset: " + ErrorMessage(status);
    }

    // Test the GetCoreTime method again.
    status = timer.GetCoreTime(coreTime, localTime);
    if (status != OKAY) {
      return "Error getting core time: " + ErrorMessage(status);
    }
    if (coreTime != Time({ 0, 0 })) {
      return "Error getting core time: " + std::to_string(coreTime.seconds) + "." + std::to_string(coreTime.microseconds);
    }

    // Test the SetCoreOffset method with a bad parameter.
    Time badTimeStruct = localTimeStruct;
    badTimeStruct.seconds += 1;
    status = timer.SetCoreOffset(badTimeStruct);
    if (status != BAD_PARAMETER) {
      return "Error: Permitted to set core offset when should not have been: " + ErrorMessage(status);
    }
  }

  return "";
}

BasicPacket::BasicPacket(uint32_t extraHeaderSize, uint32_t parameterSize)
  : m_buffer(std::make_shared<std::vector<uint8_t>>(COMMAND_PACKET_BASE_SIZE + extraHeaderSize + parameterSize))
  , m_constructorStatus(OKAY)
{
  // Pack our header.
  unsigned char* bufPtr = m_buffer->data();
  memcpy(bufPtr, MAGIC_COOKIE, sizeof(MAGIC_COOKIE)); bufPtr += sizeof(MAGIC_COOKIE);
  memcpy(bufPtr, VERSION, sizeof(VERSION)); bufPtr += sizeof(VERSION);
  uint32_t totalSize = PACKET_BASIC_HEADER_SIZE + extraHeaderSize + parameterSize;
  memcpy(bufPtr, &totalSize, sizeof(totalSize)); bufPtr += sizeof(totalSize);
  const uint32_t header_size = PACKET_BASIC_HEADER_SIZE + extraHeaderSize;
  memcpy(bufPtr, &header_size, sizeof(header_size)); bufPtr += sizeof(header_size);
}

BasicPacket::BasicPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer)
  : m_buffer(existingBuffer)
  , m_constructorStatus(OKAY)
{
  // Make sure the buffer is large enough.
  if (m_buffer->size() < PACKET_BASIC_HEADER_SIZE) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
  // Check the magic cookie.
  if (!std::equal(m_buffer->begin(), m_buffer->begin() + 4, "ASDP")) {
    m_constructorStatus = BAD_COOKIE;
    return;
  }
}

Status BasicPacket::GetConstructorStatus() const
{
  return m_constructorStatus;
}

Status BasicPacket::GetTotalLength(uint32_t& totalLength) const
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < PACKET_BASIC_HEADER_SIZE) {
    return READ_PAST_END;
  }

  // Read the total packet length.
  memcpy(&totalLength, m_buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET, sizeof(totalLength));
  return OKAY;
}

Status BasicPacket::IncreaseTotalLength(uint32_t increase)
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < PACKET_BASIC_HEADER_SIZE) {
    return READ_PAST_END;
  }

  // Read the total packet length, verify that it is not too long and then increase it.
  uint32_t totalLength;
  memcpy(&totalLength, m_buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET, sizeof(totalLength));
  totalLength += increase;
  if (totalLength > m_buffer->size()) {
    return WRITE_PAST_END;
  }
  memcpy(m_buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET, &totalLength, sizeof(totalLength));
  return OKAY;
}

BasicPacket::~BasicPacket()
{
}

std::string BasicPacket::Test()
{
  {
    // Construct a basic packet, giving it some extra parameter size.
    uint32_t parameterSize = 200;
    BasicPacket packet(0, parameterSize);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing base packet: " + ErrorMessage(packet.GetConstructorStatus());
    }

    // Construct a new packet from the original packet's buffer.
    BasicPacket packet2(packet.m_buffer);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing base packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }

    // Check the length of the packet to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet2.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking base packet size: " + ErrorMessage(status);
    }
    if (totalLength != PACKET_BASIC_HEADER_SIZE + parameterSize) {
      return "Error constructing base packet from buffer: packet length is not " +
        std::to_string(PACKET_BASIC_HEADER_SIZE + parameterSize) + " but " + std::to_string(totalLength);
    }
  }

  {
    // Try to construct a basic packet from a buffer that is too small and make
    // sure that it fails.
    std::shared_ptr<std::vector<uint8_t>> empty = std::make_shared<std::vector<uint8_t>>();
    BasicPacket packet(empty);
    Status status = packet.GetConstructorStatus();
    if (status != BAD_PARAMETER) {
      return "Unexpected return code from empty packet construction: " + ErrorMessage(status);
    }
  }

  {
    // Try to construct a basic packet from a buffer that has a bad cookie and make
    // sure that it fails.
    std::shared_ptr<std::vector<uint8_t>> noCookie = std::make_shared<std::vector<uint8_t>>(COMMAND_PACKET_BASE_SIZE);
    BasicPacket packet(noCookie);
    Status status = packet.GetConstructorStatus();
    if (status != BAD_COOKIE) {
      return "Unexpected return code from no-cookie packet construction: " + ErrorMessage(status);
    }
  }

  // Everything worked.
  return "";
}

CommandPacket::CommandPacket(uint32_t parameterSize, OpCode code)
  : BasicPacket(0, sizeof(uint32_t) + parameterSize)
{
  // Pack our operation code.
  unsigned char *bufPtr = m_buffer->data() + PACKET_BASIC_HEADER_SIZE;
  uint32_t myOpCode = code;
  memcpy(bufPtr, &myOpCode, sizeof(myOpCode)); bufPtr += sizeof(myOpCode);
}

CommandPacket::CommandPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer)
  : BasicPacket(existingBuffer)
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacket::GetOpCode(OpCode& opCode) const
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE) {
    return READ_PAST_END;
  }

  // Read the offset from the beginning of the buffer to the opcode from the header.
  // This is done to allow future versions to have more information in the header.
  uint32_t opCodeOffset;
  memcpy(&opCodeOffset, m_buffer->data() + PACKET_HEADER_HEADER_SIZE_OFFSET, sizeof(opCodeOffset));
  if (opCodeOffset + sizeof(opCode) > m_buffer->size()) {
    return READ_PAST_END;
  }
  memcpy(&opCode, m_buffer->data() + opCodeOffset, sizeof(opCode));
  return OKAY;
}

std::string CommandPacket::Test()
{
  {
    // Construct a command packet with the RESET opcode and verify that we can read its opcode.
    CommandPacket resetPacket(0, RESET);
    if (resetPacket.GetConstructorStatus() != OKAY) {
      return "Error constructing base packet: " + ErrorMessage(resetPacket.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = resetPacket.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from base packet: " + ErrorMessage(status);
    }
    if (opCode != RESET) {
      return "Error getting opcode from base packet: opcode is not RESET";
    }

    // Construct a new packet from the reset packet's buffer and verify that it has the same opcode.
    CommandPacket resetPacket2(resetPacket.m_buffer);
    if (resetPacket2.GetConstructorStatus() != OKAY) {
      return "Error constructing base packet from buffer: " + ErrorMessage(resetPacket2.GetConstructorStatus());
    }
    OpCode opCode2;
    status = resetPacket2.GetOpCode(opCode2);
    if (status != OKAY) {
      return "Error getting opcode from base packet constructed from buffer: " + ErrorMessage(status);
    }
  }

  {
    // Try to construct a CommandPacket from a buffer that is too small and make
    // sure that it fails.
    std::shared_ptr<std::vector<uint8_t>> empty = std::make_shared<std::vector<uint8_t>>();
    CommandPacket packet(empty);
    Status status = packet.GetConstructorStatus();
    if (status != BAD_PARAMETER) {
      return "Unexpected return code from empty packet construction: " + ErrorMessage(status);
    }

    // Also make sure that trying to read its opcode fails.
    OpCode opCode;
    status = packet.GetOpCode(opCode);
    if (status != READ_PAST_END) {
      return "Unexpected return code from empty packet opcode read: " + ErrorMessage(status);
    }
  }

  {
    // Try to construct a CommandPacket from a buffer that has a bad cookie and make
    // sure that it fails.
    std::shared_ptr<std::vector<uint8_t>> noCookie = std::make_shared<std::vector<uint8_t>>(COMMAND_PACKET_BASE_SIZE);
    CommandPacket packet(noCookie);
    Status status = packet.GetConstructorStatus();
    if (status != BAD_COOKIE) {
      return "Unexpected return code from no-cookie packet construction: " + ErrorMessage(status);
    }
  }

  // Everything worked.
  return "";
}

CommandPacketReset::CommandPacketReset()
  : CommandPacket(0, RESET)
{
}

CommandPacketReset::CommandPacketReset(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != RESET) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketReset::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketReset packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != RESET) {
      return "Error getting opcode from packet: opcode is not RESET";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket &originalPacket = packet;
    CommandPacketReset packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketCancelAllStreams::CommandPacketCancelAllStreams(uint32_t subnet)
  : CommandPacket(sizeof(subnet), CANCEL_ALL_STREAMS)
{
  memcpy(m_buffer->data() + COMMAND_PACKET_BASE_SIZE, &subnet, sizeof(subnet));
}

CommandPacketCancelAllStreams::CommandPacketCancelAllStreams(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CANCEL_ALL_STREAMS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketCancelAllStreams::GetSubnet(uint32_t& subnet) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(subnet)) {
    return READ_PAST_END;
  }
  memcpy(&subnet, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(subnet));
  return OKAY;
}

std::string CommandPacketCancelAllStreams::Test()
{
  {
    // Construct a CancelAllStreams command packet and verify that we can read its opcode.
    CommandPacketCancelAllStreams packet(10);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing CommandPacketCancelAllStreams packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from CommandPacketCancelAllStreams packet: " + ErrorMessage(status);
    }
    if (opCode != CANCEL_ALL_STREAMS) {
      return "Error getting opcode from CommandPacketCancelAllStreams packet: opcode is not CANCEL_ALL_STREAMS";
    }

    // Also be sure we can read the subnet.
    uint32_t subnet;
    status = packet.GetSubnet(subnet);
    if (status != OKAY) {
      return "Error getting subnet from CommandPacketCancelAllStreams packet: " + ErrorMessage(status);
    }
    if (subnet != 10) {
      return "Error getting subnet from CommandPacketCancelAllStreams packet: subnet is not 10";
    }
  }

  return "";
}

CommandPacketStartRecording::CommandPacketStartRecording()
  : CommandPacket(0, START_RECORDING)
{
}

CommandPacketStartRecording::CommandPacketStartRecording(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != START_RECORDING) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketStartRecording::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStartRecording packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != START_RECORDING) {
      return "Error getting opcode from packet: opcode is not START_RECORDING";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket& originalPacket = packet;
    CommandPacketStartRecording packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketStopRecording::CommandPacketStopRecording()
  : CommandPacket(0, STOP_RECORDING)
{
}

CommandPacketStopRecording::CommandPacketStopRecording(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STOP_RECORDING) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketStopRecording::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStopRecording packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != STOP_RECORDING) {
      return "Error getting opcode from packet: opcode is not STOP_RECORDING";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket& originalPacket = packet;
    CommandPacketStopRecording packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketPauseReplay::CommandPacketPauseReplay()
  : CommandPacket(0, PAUSE_REPLAY)
{
}

CommandPacketPauseReplay::CommandPacketPauseReplay(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != PAUSE_REPLAY) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketPauseReplay::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketPauseReplay packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != PAUSE_REPLAY) {
      return "Error getting opcode from packet: opcode is not PAUSE_REPLAY";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket& originalPacket = packet;
    CommandPacketPauseReplay packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketStartReplay::CommandPacketStartReplay(uint32_t ID, Time initialTime)
  : CommandPacket(sizeof(ID) + sizeof(initialTime), START_REPLAY)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &ID, sizeof(ID)); bufPtr += sizeof(ID);
  memcpy(bufPtr, &initialTime.seconds, sizeof(initialTime.seconds)); bufPtr += sizeof(initialTime.seconds);
  memcpy(bufPtr, &initialTime.microseconds, sizeof(initialTime.microseconds)); bufPtr += sizeof(initialTime.microseconds);
}

CommandPacketStartReplay::CommandPacketStartReplay(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != START_REPLAY) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStartReplay::GetID(uint32_t& ID) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(ID)) {
    return READ_PAST_END;
  }
  memcpy(&ID, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(ID));
  return OKAY;
}

Status CommandPacketStartReplay::GetInitialTime(Time& initialTime) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 3 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  memcpy(&initialTime.seconds, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(uint32_t));
  memcpy(&initialTime.microseconds, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(uint32_t));
  return OKAY;
}

std::string CommandPacketStartReplay::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStartReplay packet(10, { 1, 2 });
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != START_REPLAY) {
      return "Error getting opcode from packet: opcode is not START_REPLAY";
    }

    // Also be sure we can read the ID.
    uint32_t ID;
    status = packet.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet: " + ErrorMessage(status);
    }
    if (ID != 10) {
      return "Error getting ID from packet: ID is not 10";
    }

    // Also be sure we can read the initial time.
    Time initialTime;
    status = packet.GetInitialTime(initialTime);
    if (status != OKAY) {
      return "Error getting initial time from packet: " + ErrorMessage(status);
    }
    if (initialTime.seconds != 1 || initialTime.microseconds != 2) {
      return "Error getting initial time from packet: time is not 1.2";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketStartReplay packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (ID != 10) {
      return "Error getting ID from packet constructed from buffer: ID is not 10";
    }
    status = packet2.GetInitialTime(initialTime);
    if (status != OKAY) {
      return "Error getting initial time from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (initialTime.seconds != 1 || initialTime.microseconds != 2) {
      return "Error getting initial time from packet constructed from buffer: time is not 1.2";
    }
  }

  return "";
}

CommandPacketResumeReplay::CommandPacketResumeReplay()
  : CommandPacket(0, RESUME_REPLAY)
{
}

CommandPacketResumeReplay::CommandPacketResumeReplay(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != RESUME_REPLAY) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketResumeReplay::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketResumeReplay packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != RESUME_REPLAY) {
      return "Error getting opcode from packet: opcode is not RESUME_REPLAY";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket& originalPacket = packet;
    CommandPacketResumeReplay packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketStopReplay::CommandPacketStopReplay()
  : CommandPacket(0, STOP_REPLAY)
{
}

CommandPacketStopReplay::CommandPacketStopReplay(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STOP_REPLAY) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketStopReplay::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStopReplay packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != STOP_REPLAY) {
      return "Error getting opcode from packet: opcode is not STOP_REPLAY";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket& originalPacket = packet;
    CommandPacketStopReplay packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketSetStartUpRecordingState::CommandPacketSetStartUpRecordingState(uint32_t state)
  : CommandPacket(sizeof(state), SET_START_UP_RECORDING_STATE)
{
  memcpy(m_buffer->data() + COMMAND_PACKET_BASE_SIZE, &state, sizeof(state));
}

CommandPacketSetStartUpRecordingState::CommandPacketSetStartUpRecordingState(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != SET_START_UP_RECORDING_STATE) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketSetStartUpRecordingState::GetState(uint32_t& state) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(state)) {
    return READ_PAST_END;
  }
  memcpy(&state, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(state));
  return OKAY;
}

std::string CommandPacketSetStartUpRecordingState::Test()
{
  {
    // Construct a CommandPacketSetStartUpRecordingState command packet and verify that we can read its opcode.
    CommandPacketSetStartUpRecordingState packet(1);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != SET_START_UP_RECORDING_STATE) {
      return "Error getting opcode from packet: opcode is not SET_START_UP_RECORDING_STATE";
    }

    // Also be sure we can read the state.
    uint32_t state;
    status = packet.GetState(state);
    if (status != OKAY) {
      return "Error getting state from packet: " + ErrorMessage(status);
    }
    if (state != 1) {
      return "Error getting state from packet: state is not 1";
    }
  }

  return "";
}

CommandPacketKeepaliveInterval::CommandPacketKeepaliveInterval(float interval)
  : CommandPacket(sizeof(interval), SET_KEEPALIVE_INTERVAL)
{
  memcpy(m_buffer->data() + COMMAND_PACKET_BASE_SIZE, &interval, sizeof(interval));
}

CommandPacketKeepaliveInterval::CommandPacketKeepaliveInterval(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != SET_KEEPALIVE_INTERVAL) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketKeepaliveInterval::GetInterval(float& interval) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(interval)) {
    return READ_PAST_END;
  }
  memcpy(&interval, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(interval));
  return OKAY;
}

std::string CommandPacketKeepaliveInterval::Test()
{
  {
    // Construct a CommandPacketKeepaliveInterval command packet and verify that we can read its opcode.
    CommandPacketKeepaliveInterval packet(1);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != SET_KEEPALIVE_INTERVAL) {
      return "Error getting opcode from packet: opcode is not SET_KEEPALIVE_INTERVAL";
    }

    // Also be sure we can read the interval.
    float interval;
    status = packet.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting state from packet: " + ErrorMessage(status);
    }
    if (interval != 1) {
      return "Error getting interval from packet: interval is not 1";
    }
  }

  return "";
}

CommandPacketStreamState::CommandPacketStreamState(uint32_t IP, uint16_t port, float interval)
  : CommandPacket(2 * sizeof(uint32_t) + sizeof(float), STREAM_STATE)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
  memcpy(bufPtr, &interval, sizeof(interval)); bufPtr += sizeof(interval);
}

CommandPacketStreamState::CommandPacketStreamState(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STREAM_STATE) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStreamState::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketStreamState::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

Status CommandPacketStreamState::GetInterval(float& interval) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t) + sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&interval, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(interval));
  return OKAY;
}

std::string CommandPacketStreamState::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStreamState packet(1, 2, 3);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != STREAM_STATE) {
      return "Error getting opcode from packet: opcode is not STREAM_STATE";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Also be sure we can read the interval.
    float interval;
    status = packet.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting interval from packet: " + ErrorMessage(status);
    }
    if (interval != 3) {
      return "Error getting interval from packet: interval is not 3";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketStreamState packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
    status = packet2.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting interval from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (interval != 3) {
      return "Error getting interval from packet constructed from buffer: interval is not 3";
    }
  }

  return "";
}

CommandPacketCancelState::CommandPacketCancelState(uint32_t IP, uint16_t port)
  : CommandPacket(2 * sizeof(uint32_t), CANCEL_STATE)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
}

CommandPacketCancelState::CommandPacketCancelState(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CANCEL_STATE) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketCancelState::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketCancelState::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

std::string CommandPacketCancelState::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketCancelState packet(1, 2);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != CANCEL_STATE) {
      return "Error getting opcode from packet: opcode is not CANCEL_STATE";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketCancelState packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
  }

  return "";
}

CommandPacketConfigureTrigger::CommandPacketConfigureTrigger(uint16_t ID, uint8_t mode, uint8_t externalID,
  float period, float offset, float trackingFactor)
  : CommandPacket(sizeof(uint16_t) + 2 * sizeof(uint8_t) + 3 * sizeof(float), CONFIGURE_TRIGGER)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &ID, sizeof(ID)); bufPtr += sizeof(ID);
  memcpy(bufPtr, &mode, sizeof(mode)); bufPtr += sizeof(mode);
  memcpy(bufPtr, &externalID, sizeof(externalID)); bufPtr += sizeof(externalID);
  memcpy(bufPtr, &period, sizeof(period)); bufPtr += sizeof(period);
  memcpy(bufPtr, &offset, sizeof(offset)); bufPtr += sizeof(offset);
  memcpy(bufPtr, &trackingFactor, sizeof(trackingFactor)); bufPtr += sizeof(trackingFactor);
}

CommandPacketConfigureTrigger::CommandPacketConfigureTrigger(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CONFIGURE_TRIGGER) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketConfigureTrigger::GetID(uint16_t& ID) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(ID)) {
    return READ_PAST_END;
  }
  memcpy(&ID, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(ID));
  return OKAY;
}

Status CommandPacketConfigureTrigger::GetMode(uint8_t& mode) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + sizeof(mode)) {
    return READ_PAST_END;
  }
  memcpy(&mode, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t), sizeof(mode));
  return OKAY;
}

Status CommandPacketConfigureTrigger::GetExternalID(uint8_t& externalID) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t)) {
    return READ_PAST_END;
  }
  memcpy(&externalID, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + sizeof(uint8_t), sizeof(externalID));
  return OKAY;
}

Status CommandPacketConfigureTrigger::GetPeriod(float& period) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t) + sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&period, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t), sizeof(period));
  return OKAY;
}

Status CommandPacketConfigureTrigger::GetOffset(float& offset) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t) + 2 * sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&offset, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t) + sizeof(float), sizeof(offset));
  return OKAY;
}

Status CommandPacketConfigureTrigger::GetTrackingFactor(float& trackingFactor) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t) + 3 * sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&trackingFactor, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint16_t) + 2 * sizeof(uint8_t) + 2 * sizeof(float), sizeof(trackingFactor));
  return OKAY;
}

std::string CommandPacketConfigureTrigger::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketConfigureTrigger packet(1, 2, 3, 4, 5, 6);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != CONFIGURE_TRIGGER) {
      return "Error getting opcode from packet: opcode is not CONFIGURE_TRIGGER";
    }

    // Also be sure we can read the ID.
    uint16_t ID;
    status = packet.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet: " + ErrorMessage(status);
    }
    if (ID != 1) {
      return "Error getting ID from packet: ID is not 1";
    }

    // Also be sure we can read the mode.
    uint8_t mode;
    status = packet.GetMode(mode);
    if (status != OKAY) {
      return "Error getting mode from packet: " + ErrorMessage(status);
    }
    if (mode != 2) {
      return "Error getting mode from packet: mode is not 2";
    }

    // Also be sure we can read the external ID.
    uint8_t externalID;
    status = packet.GetExternalID(externalID);
    if (status != OKAY) {
      return "Error getting external ID from packet: " + ErrorMessage(status);
    }
    if (externalID != 3) {
      return "Error getting external ID from packet: external ID is not 3";
    }

    // Also be sure we can read the period.
    float period;
    status = packet.GetPeriod(period);
    if (status != OKAY) {
      return "Error getting period from packet: " + ErrorMessage(status);
    }
    if (period != 4) {
      return "Error getting period from packet: period is not 4";
    }

    // Also be sure we can read the offset.
    float offset;
    status = packet.GetOffset(offset);
    if (status != OKAY) {
      return "Error getting offset from packet: " + ErrorMessage(status);
    }
    if (offset != 5) {
      return "Error getting offset from packet: offset is not 5";
    }

    // Also be sure we can read the tracking factor.
    float trackingFactor;
    status = packet.GetTrackingFactor(trackingFactor);
    if (status != OKAY) {
      return "Error getting tracking factor from packet: " + ErrorMessage(status);
    }
    if (trackingFactor != 6) {
      return "Error getting tracking factor from packet: tracking factor is not 6";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketConfigureTrigger packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (ID != 1) {
      return "Error getting ID from packet constructed from buffer: ID is not 1";
    }
    status = packet2.GetMode(mode);
    if (status != OKAY) {
      return "Error getting mode from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (mode != 2) {
      return "Error getting mode from packet constructed from buffer: mode is not 2";
    }
    status = packet2.GetExternalID(externalID);
    if (status != OKAY) {
      return "Error getting external ID from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (externalID != 3) {
      return "Error getting external ID from packet constructed from buffer: external ID is not 3";
    }
    status = packet2.GetPeriod(period);
    if (status != OKAY) {
      return "Error getting period from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (period != 4) {
      return "Error getting period from packet constructed from buffer: period is not 4";
    }
    status = packet2.GetOffset(offset);
    if (status != OKAY) {
      return "Error getting offset from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (offset != 5) {
      return "Error getting offset from packet constructed from buffer: offset is not 5";
    }
    status = packet2.GetTrackingFactor(trackingFactor);
    if (status != OKAY) {
      return "Error getting tracking factor from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (trackingFactor != 6) {
      return "Error getting tracking factor from packet constructed from buffer: tracking factor is not 6";
    }
  }

  return "";
}

CommandPacketSoftwareTrigger::CommandPacketSoftwareTrigger(uint8_t ID, Time initialTime)
  : CommandPacket(sizeof(ID) + 3 + sizeof(initialTime), SOFTWARE_TRIGGER)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  uint32_t IDField = ID;
  memcpy(bufPtr, &IDField, sizeof(IDField)); bufPtr += sizeof(IDField);
  memcpy(bufPtr, &initialTime.seconds, sizeof(initialTime.seconds)); bufPtr += sizeof(initialTime.seconds);
  memcpy(bufPtr, &initialTime.microseconds, sizeof(initialTime.microseconds)); bufPtr += sizeof(initialTime.microseconds);
}

CommandPacketSoftwareTrigger::CommandPacketSoftwareTrigger(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != SOFTWARE_TRIGGER) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketSoftwareTrigger::GetID(uint8_t& ID) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t IDField;
  memcpy(&IDField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IDField));
  ID = IDField;
  return OKAY;
}

Status CommandPacketSoftwareTrigger::GetInitialTime(Time& initialTime) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  memcpy(&initialTime.seconds, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(uint32_t));
  memcpy(&initialTime.microseconds, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(uint32_t));
  return OKAY;
}

std::string CommandPacketSoftwareTrigger::Test()
{
  {
    // Construct a CommandPacketSoftwareTrigger command packet and verify that we can read its opcode.
    CommandPacketSoftwareTrigger packet(1, { 2, 3 });
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != SOFTWARE_TRIGGER) {
      return "Error getting opcode from packet: opcode is not SOFTWARE_TRIGGER";
    }

    // Also be sure we can read the ID.
    uint8_t ID;
    status = packet.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet: " + ErrorMessage(status);
    }
    if (ID != 1) {
      return "Error getting ID from packet: ID is not 1";
    }

    // Also be sure we can read the initial time.
    Time initialTime;
    status = packet.GetInitialTime(initialTime);
    if (status != OKAY) {
      return "Error getting initial time from packet: " + ErrorMessage(status);
    }
    if (initialTime.seconds != 2 || initialTime.microseconds != 3) {
      return "Error getting initial time from packet: time is not 2.3";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketSoftwareTrigger packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (ID != 1) {
      return "Error getting ID from packet constructed from buffer: ID is not 1";
    }
    status = packet2.GetInitialTime(initialTime);
    if (status != OKAY) {
      return "Error getting initial time from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (initialTime.seconds != 2 || initialTime.microseconds != 3) {
      return "Error getting initial time from packet constructed from buffer: time is not 2.3";
    }
  }
  return "";
}

CommandPacketStreamEvents::CommandPacketStreamEvents(uint32_t IP, uint16_t port, uint32_t verbosity)
  : CommandPacket(2 * sizeof(uint32_t) + sizeof(verbosity), STREAM_EVENTS)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
  memcpy(bufPtr, &verbosity, sizeof(verbosity)); bufPtr += sizeof(verbosity);
}

CommandPacketStreamEvents::CommandPacketStreamEvents(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STREAM_EVENTS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStreamEvents::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketStreamEvents::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

Status CommandPacketStreamEvents::GetVerbosity(uint32_t& verbosity) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t) + sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&verbosity, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(verbosity));
  return OKAY;
}

std::string CommandPacketStreamEvents::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStreamEvents packet(1, 2, 3);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != STREAM_EVENTS) {
      return "Error getting opcode from packet: opcode is not STREAM_EVENTS";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Also be sure we can read the verbosity.
    uint32_t verbosity;
    status = packet.GetVerbosity(verbosity);
    if (status != OKAY) {
      return "Error getting verbosity from packet: " + ErrorMessage(status);
    }
    if (verbosity != 3) {
      return "Error getting verbosity from packet: verbosity is not 3";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketStreamEvents packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
    status = packet2.GetVerbosity(verbosity);
    if (status != OKAY) {
      return "Error getting verbosity from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (verbosity != 3) {
      return "Error getting verbosity from packet constructed from buffer: verbosity is not 3";
    }
  }

  return "";
}

CommandPacketCancelEvents::CommandPacketCancelEvents(uint32_t IP, uint16_t port)
  : CommandPacket(2 * sizeof(uint32_t), CANCEL_EVENTS)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
}

CommandPacketCancelEvents::CommandPacketCancelEvents(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CANCEL_EVENTS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketCancelEvents::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketCancelEvents::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

std::string CommandPacketCancelEvents::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketCancelEvents packet(1, 2);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != CANCEL_EVENTS) {
      return "Error getting opcode from packet: opcode is not CANCEL_EVENTS";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketCancelEvents packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
  }

  return "";
}

CommandPacketStreamSubregions::CommandPacketStreamSubregions(uint32_t IP, uint16_t port,
  std::vector<SubregionDescription> const &regions)
  : CommandPacket(sizeof(IP) + sizeof(uint32_t) + sizeof(uint32_t)
      + regions.size() * sizeof(SubregionDescription), STREAM_SUBREGIONS)
{
  unsigned char *bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
  uint32_t numRegions = regions.size();
  memcpy(bufPtr, &numRegions, sizeof(numRegions)); bufPtr += sizeof(numRegions);
  for (size_t i = 0; i < regions.size(); ++i) {
    memcpy(bufPtr, &regions[i].cameraID, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(bufPtr, &regions[i].skipFrames, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(bufPtr, &regions[i].skipModulo, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(bufPtr, &regions[i].left, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(bufPtr, &regions[i].top, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(bufPtr, &regions[i].right, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(bufPtr, &regions[i].bottom, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
  }
}

CommandPacketStreamSubregions::CommandPacketStreamSubregions(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STREAM_SUBREGIONS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStreamSubregions::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketStreamSubregions::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

Status CommandPacketStreamSubregions::GetRegionDescriptions(std::vector<SubregionDescription>& regions) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 3 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t numRegions;
  memcpy(&numRegions, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(numRegions));
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 3 * sizeof(uint32_t) + numRegions * sizeof(SubregionDescription)) {
    return READ_PAST_END;
  }
  regions.resize(numRegions);
  unsigned char *bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 3 * sizeof(uint32_t);
  for (size_t i = 0; i < numRegions; ++i) {
    memcpy(&regions[i].cameraID, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(&regions[i].skipFrames, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(&regions[i].skipModulo, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(&regions[i].left, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(&regions[i].top, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(&regions[i].right, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
    memcpy(&regions[i].bottom, bufPtr, sizeof(uint32_t)); bufPtr += sizeof(uint32_t);
  }
  return OKAY;
}

std::string CommandPacketStreamSubregions::Test()
{
  {
    // Construct a CommandPacketStreamSubregions command packet and verify that we can read its opcode.
    uint32_t IP = 0x01020304;
    uint16_t port = 1234;
    SubregionDescription region1 = { 1, 2, 3, 4, 5, 6, 7 };
    SubregionDescription region2 = { 8, 9,10,11,12,13, 14 };
    std::vector<SubregionDescription> regions = { region1, region2 };
    CommandPacketStreamSubregions packet(IP, port, regions);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing CommandPacketStreamSubregions packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from CommandPacketStreamSubregions packet: " + ErrorMessage(status);
    }
    if (opCode != STREAM_SUBREGIONS) {
      return "Error getting opcode from CommandPacketStreamSubregions packet: opcode is not STREAM_SUBREGIONS";
    }

    // Also be sure we can read the values.
    uint32_t rIP;
    status = packet.GetIP(rIP);
    if (status != OKAY) {
      return "Error getting IP from CommandPacketStreamSubregions packet: " + ErrorMessage(status);
    }
    if (rIP != IP) {
      return "Error getting IP from CommandPacketStreamSubregions packet: IP is not " +
        std::to_string(IP);
    }

    uint16_t rPort;
    status = packet.GetPort(rPort);
    if (status != OKAY) {
      return "Error getting port from CommandPacketStreamSubregions packet: " + ErrorMessage(status);
    }
    if (rPort != port) {
      return "Error getting port from CommandPacketStreamSubregions packet: port is not " +
        std::to_string(port);
    }

    std::vector<SubregionDescription> rRegions;
    status = packet.GetRegionDescriptions(rRegions);
    if (status != OKAY) {
      return "Error getting regions from CommandPacketStreamSubregions packet: " + ErrorMessage(status);
    }
    if (rRegions != regions) {
      return "Error getting regions from CommandPacketStreamSubregions packet: regions don't match";
    }
  }

  return "";
}

CommandPacketCancelSubregions::CommandPacketCancelSubregions(uint32_t IP, uint16_t port)
  : CommandPacket(2 * sizeof(uint32_t), CANCEL_SUBREGIONS)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
}

CommandPacketCancelSubregions::CommandPacketCancelSubregions(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CANCEL_SUBREGIONS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketCancelSubregions::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketCancelSubregions::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

std::string CommandPacketCancelSubregions::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketCancelSubregions packet(1, 2);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != CANCEL_SUBREGIONS) {
      return "Error getting opcode from packet: opcode is not CANCEL_SUBREGIONS";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketCancelSubregions packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
  }

  return "";
}

CommandPacketEraseAllStoredStreams::CommandPacketEraseAllStoredStreams()
  : CommandPacket(0, ERASE_ALL_STORED_STREAMS)
{
}

CommandPacketEraseAllStoredStreams::CommandPacketEraseAllStoredStreams(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != ERASE_ALL_STORED_STREAMS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

std::string CommandPacketEraseAllStoredStreams::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketEraseAllStoredStreams packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != ERASE_ALL_STORED_STREAMS) {
      return "Error getting opcode from packet: opcode is not ERASE_ALL_STORED_STREAMS";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same opcode.
    CommandPacket& originalPacket = packet;
    CommandPacketEraseAllStoredStreams packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
  }

  return "";
}

CommandPacketStreamStoredList::CommandPacketStreamStoredList(uint32_t IP, uint16_t port)
  : CommandPacket(2 * sizeof(uint32_t), LIST_STORED_STREAMS)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
}

CommandPacketStreamStoredList::CommandPacketStreamStoredList(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != LIST_STORED_STREAMS) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStreamStoredList::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketStreamStoredList::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

std::string CommandPacketStreamStoredList::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStreamStoredList packet(1, 2);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != LIST_STORED_STREAMS) {
      return "Error getting opcode from packet: opcode is not LIST_STORED_STREAMS";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketStreamStoredList packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
  }

  return "";
}

CommandPacketEraseStoredStream::CommandPacketEraseStoredStream(uint32_t ID)
  : CommandPacket(sizeof(ID), ERASE_STORED_STREAM)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &ID, sizeof(ID)); bufPtr += sizeof(ID);
}

CommandPacketEraseStoredStream::CommandPacketEraseStoredStream(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != ERASE_STORED_STREAM) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketEraseStoredStream::GetID(uint32_t& ID) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(ID)) {
    return READ_PAST_END;
  }
  memcpy(&ID, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(ID));
  return OKAY;
}

std::string CommandPacketEraseStoredStream::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketEraseStoredStream packet(10);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != ERASE_STORED_STREAM) {
      return "Error getting opcode from packet: opcode is not ERASE_STORED_STREAM";
    }

    // Also be sure we can read the ID.
    uint32_t ID;
    status = packet.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet: " + ErrorMessage(status);
    }
    if (ID != 10) {
      return "Error getting ID from packet: ID is not 10";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketEraseStoredStream packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetID(ID);
    if (status != OKAY) {
      return "Error getting ID from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (ID != 10) {
      return "Error getting ID from packet constructed from buffer: ID is not 10";
    }
  }

  return "";
}

CommandPacketStreamTemperatures::CommandPacketStreamTemperatures(uint32_t IP, uint16_t port, float interval)
  : CommandPacket(2 * sizeof(uint32_t) + sizeof(float), STREAM_TEMPERATURES)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
  memcpy(bufPtr, &interval, sizeof(interval)); bufPtr += sizeof(interval);
}

CommandPacketStreamTemperatures::CommandPacketStreamTemperatures(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STREAM_TEMPERATURES) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStreamTemperatures::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketStreamTemperatures::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

Status CommandPacketStreamTemperatures::GetInterval(float& interval) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t) + sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&interval, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(interval));
  return OKAY;
}

std::string CommandPacketStreamTemperatures::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStreamTemperatures packet(1, 2, 3);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != STREAM_TEMPERATURES) {
      return "Error getting opcode from packet: opcode is not STREAM_TEMPERATURES";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Also be sure we can read the interval.
    float interval;
    status = packet.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting interval from packet: " + ErrorMessage(status);
    }
    if (interval != 3) {
      return "Error getting interval from packet: interval is not 3";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketStreamTemperatures packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
    status = packet2.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting interval from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (interval != 3) {
      return "Error getting interval from packet constructed from buffer: interval is not 3";
    }
  }

  return "";
}

CommandPacketCancelTemperatures::CommandPacketCancelTemperatures(uint32_t IP, uint16_t port)
  : CommandPacket(2 * sizeof(uint32_t), CANCEL_TEMPERATURES)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
}

CommandPacketCancelTemperatures::CommandPacketCancelTemperatures(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CANCEL_TEMPERATURES) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketCancelTemperatures::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketCancelTemperatures::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

std::string CommandPacketCancelTemperatures::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketCancelTemperatures packet(1, 2);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != CANCEL_TEMPERATURES) {
      return "Error getting opcode from packet: opcode is not CANCEL_TEMPERATURES";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketCancelTemperatures packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
  }

  return "";
}

CommandPacketStreamPoses::CommandPacketStreamPoses(uint32_t IP, uint16_t port, float interval)
  : CommandPacket(2 * sizeof(uint32_t) + sizeof(float), STREAM_POSES)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
  memcpy(bufPtr, &interval, sizeof(interval)); bufPtr += sizeof(interval);
}

CommandPacketStreamPoses::CommandPacketStreamPoses(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != STREAM_POSES) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketStreamPoses::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketStreamPoses::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

Status CommandPacketStreamPoses::GetInterval(float& interval) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t) + sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&interval, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(interval));
  return OKAY;
}

std::string CommandPacketStreamPoses::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketStreamPoses packet(1, 2, 3);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != STREAM_POSES) {
      return "Error getting opcode from packet: opcode is not STREAM_POSES";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Also be sure we can read the interval.
    float interval;
    status = packet.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting interval from packet: " + ErrorMessage(status);
    }
    if (interval != 3) {
      return "Error getting interval from packet: interval is not 3";
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketStreamPoses packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
    status = packet2.GetInterval(interval);
    if (status != OKAY) {
      return "Error getting interval from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (interval != 3) {
      return "Error getting interval from packet constructed from buffer: interval is not 3";
    }
  }

  return "";
}

CommandPacketCancelPoses::CommandPacketCancelPoses(uint32_t IP, uint16_t port)
  : CommandPacket(2 * sizeof(uint32_t), CANCEL_POSES)
{
  unsigned char* bufPtr = m_buffer->data() + COMMAND_PACKET_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
}

CommandPacketCancelPoses::CommandPacketCancelPoses(CommandPacket& basePacket)
  : CommandPacket(basePacket.m_buffer)
{
  OpCode opCode;
  basePacket.GetOpCode(opCode);
  if (opCode != CANCEL_POSES) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status CommandPacketCancelPoses::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + COMMAND_PACKET_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status CommandPacketCancelPoses::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + COMMAND_PACKET_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

std::string CommandPacketCancelPoses::Test()
{
  {
    // Construct a command packet and verify that we can read its opcode.
    CommandPacketCancelPoses packet(1, 2);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing packet: " + ErrorMessage(packet.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = packet.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from packet: " + ErrorMessage(status);
    }
    if (opCode != CANCEL_POSES) {
      return "Error getting opcode from packet: opcode is not CANCEL_POSES";
    }

    // Also be sure we can read the ID.
    uint32_t IP;
    status = packet.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting IP from packet: IP is not 1";
    }

    // Also be sure we can read the port.
    uint16_t port;
    status = packet.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }

    // Construct a new packet from the packet's buffer and verify that it has the same parameters.
    CommandPacket& originalPacket = packet;
    CommandPacketCancelPoses packet2(originalPacket);
    if (packet2.GetConstructorStatus() != OKAY) {
      return "Error constructing packet from buffer: " + ErrorMessage(packet2.GetConstructorStatus());
    }
    status = packet2.GetIP(IP);
    if (status != OKAY) {
      return "Error getting IP from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (IP != 1) {
      return "Error getting ID from packet constructed from buffer: IP is not 1";
    }
    status = packet2.GetPort(port);
    if (status != OKAY) {
      return "Error getting port from packet constructed from buffer: " + ErrorMessage(status);
    }
    if (port != 2) {
      return "Incorrect port returned: " + std::to_string(port);
    }
  }

  return "";
}

StreamPacket::StreamPacket(uint32_t bufferMaxSize, uint32_t sequenceNumber, Time timeCode)
  : BasicPacket(3 * sizeof(uint32_t), bufferMaxSize - COMMAND_PACKET_BASE_SIZE - 3 * sizeof(uint32_t))
{
  // Overwrite the stored total size with the actually filled-in size, leaving room in the buffer.
  uint32_t usedSize = STREAM_PACKET_BASE_SIZE;
  unsigned char* bufPtr = m_buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET;
  memcpy(bufPtr, &usedSize, sizeof(usedSize)); bufPtr += sizeof(usedSize);

  // Set the sequence number and time sent.
  bufPtr = m_buffer->data() + PACKET_BASIC_HEADER_SIZE;
  memcpy(bufPtr, &sequenceNumber, sizeof(sequenceNumber)); bufPtr += sizeof(sequenceNumber);
  memcpy(bufPtr, &timeCode.seconds, sizeof(timeCode.seconds)); bufPtr += sizeof(timeCode.seconds);
  memcpy(bufPtr, &timeCode.microseconds, sizeof(timeCode.microseconds)); bufPtr += sizeof(timeCode.microseconds);
}

StreamPacket::StreamPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer)
  : BasicPacket(existingBuffer)
{
  if (m_buffer->size() < STREAM_PACKET_BASE_SIZE) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status StreamPacket::GetSequenceNumber(uint32_t& sequenceNumber) const
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < STREAM_PACKET_BASE_SIZE) {
    return READ_PAST_END;
  }

  // Read the offset from the beginning of the buffer to the first message from the header.
  // This is done to allow future versions to have more information in the header.
  uint32_t firstMessageOffset;
  memcpy(&firstMessageOffset, m_buffer->data() + PACKET_HEADER_HEADER_SIZE_OFFSET, sizeof(firstMessageOffset));
  uint32_t sequenceNumberOffset = firstMessageOffset - 3 * sizeof(uint32_t);
  if (sequenceNumberOffset + sizeof(sequenceNumber) > m_buffer->size()) {
    return READ_PAST_END;
  }
  memcpy(&sequenceNumber, m_buffer->data() + sequenceNumberOffset, sizeof(sequenceNumber));
  return OKAY;
}

Status StreamPacket::SetSequenceNumber(uint32_t sequenceNumber)
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < STREAM_PACKET_BASE_SIZE) {
    return WRITE_PAST_END;
  }

  // Read the offset from the beginning of the buffer to the first message from the header.
  // This is done to allow future versions to have more information in the header.
  uint32_t firstMessageOffset;
  memcpy(&firstMessageOffset, m_buffer->data() + PACKET_HEADER_HEADER_SIZE_OFFSET, sizeof(firstMessageOffset));
  uint32_t sequenceNumberOffset = firstMessageOffset - 3 * sizeof(uint32_t);
  if (sequenceNumberOffset + sizeof(sequenceNumber) > m_buffer->size()) {
    return WRITE_PAST_END;
  }
  memcpy(m_buffer->data() + sequenceNumberOffset, &sequenceNumber, sizeof(sequenceNumber));
  return OKAY;
}

Status StreamPacket::GetTimeCode(Time& timeCode) const
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < STREAM_PACKET_BASE_SIZE) {
    return READ_PAST_END;
  }

  // Read the offset from the beginning of the buffer to the first message from the header.
  // This is done to allow future versions to have more information in the header.
  uint32_t firstMessageOffset;
  memcpy(&firstMessageOffset, m_buffer->data() + PACKET_HEADER_HEADER_SIZE_OFFSET, sizeof(firstMessageOffset));
  uint32_t timeCodeOffset = firstMessageOffset - 2 * sizeof(uint32_t);
  if (timeCodeOffset + 2 * sizeof(uint32_t) > m_buffer->size()) {
    return READ_PAST_END;
  }
  memcpy(&timeCode.seconds, m_buffer->data() + timeCodeOffset, sizeof(timeCode.seconds));
  memcpy(&timeCode.microseconds, m_buffer->data() + timeCodeOffset + sizeof(timeCode.seconds), sizeof(timeCode.microseconds));
  return OKAY;
}

Status StreamPacket::SetTimeCode(Time timeCode)
{
  // Make sure we have enough data to hold the header.
  if (m_buffer->size() < STREAM_PACKET_BASE_SIZE) {
    return WRITE_PAST_END;
  }

  // Read the offset from the beginning of the buffer to the first message from the header.
  // This is done to allow future versions to have more information in the header.
  uint32_t firstMessageOffset;
  memcpy(&firstMessageOffset, m_buffer->data() + PACKET_HEADER_HEADER_SIZE_OFFSET, sizeof(firstMessageOffset));
  uint32_t timeCodeOffset = firstMessageOffset - 2 * sizeof(uint32_t);
  if (timeCodeOffset + 2 * sizeof(uint32_t) > m_buffer->size()) {
    return WRITE_PAST_END;
  }
  memcpy(m_buffer->data() + timeCodeOffset, &timeCode.seconds, sizeof(timeCode.seconds));
  memcpy(m_buffer->data() + timeCodeOffset + sizeof(timeCode.seconds), &timeCode.microseconds, sizeof(timeCode.microseconds));
  return OKAY;
}

Status StreamPacket::GetNextMessage(std::shared_ptr<Message>& message) const
{
  // Make sure we have enough data to hold the header. Then get the total length
  // of the packet.
  if (m_buffer->size() < STREAM_PACKET_BASE_SIZE) {
    message.reset();
    return READ_PAST_END;
  }
  uint32_t totalLength;
  memcpy(&totalLength, m_buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET, sizeof(totalLength));

  // If the message is a nullptr, then find the offset to the first message
  // in the buffer.  Otherwise, use the offset from the message
  uint32_t offset;
  if (message == nullptr) {
    // Read the offset from the beginning of the buffer to the first message from the header.
    // This is done to allow future versions to have more information in the header.
    memcpy(&offset, m_buffer->data() + PACKET_HEADER_HEADER_SIZE_OFFSET, sizeof(offset));
  } else {
    // Make sure we're using the same buffer.
    if (message->m_buffer != m_buffer) {
      message.reset();
      return BAD_PARAMETER;
    }

    // Make sure we're not trying to read past the end of the buffer.
    offset = message->m_offset;
    if (offset + MESSAGE_HEADER_MESSAGE_TOTAL_SIZE_OFFSET + sizeof(uint32_t) > totalLength) {
      message.reset();
      return READ_PAST_END;
    }

    // Add the length of the message to the offset to find the offset of the next message.
    uint32_t msgSize;
    memcpy(&msgSize, m_buffer->data() + offset + MESSAGE_HEADER_MESSAGE_TOTAL_SIZE_OFFSET, sizeof(msgSize));
    offset += msgSize;
  }

  // If our offset is past the end of the packet, we're done and return a nullptr.
  if (offset >= totalLength) {
    message.reset();
    return OKAY;
  }

  // Read the size of the first message and make sure it fits entirely within the buffer.
  uint32_t messageSize;
  if (totalLength < offset + MESSAGE_HEADER_MESSAGE_TOTAL_SIZE_OFFSET + sizeof(messageSize)) {
    message.reset();
    return READ_PAST_END;
  }
  memcpy(&messageSize, m_buffer->data() + offset + MESSAGE_HEADER_MESSAGE_TOTAL_SIZE_OFFSET, sizeof(messageSize));
  if (offset + messageSize > totalLength) {
    message.reset();
    return READ_PAST_END;
  }

  // Construct a message from the buffer.
  Message *messagePtr = new Message(m_buffer, offset);
  message.reset(messagePtr);
  if (message->GetConstructorStatus() != OKAY) {
    message.reset();
    return message->GetConstructorStatus();
  }
  return OKAY;
}

std::string StreamPacket::Test()
{
  {
    // Construct a StreamPacket with no messages and check its length.
    StreamPacket packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet: " + ErrorMessage(packet.GetConstructorStatus());
    }

    // Check the length of the packet to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking stream packet size: " + ErrorMessage(status);
    }
    if (totalLength != STREAM_PACKET_BASE_SIZE) {
      return "Error constructing stream packet from buffer: packet length is not " +
        std::to_string(STREAM_PACKET_BASE_SIZE) + " but " + std::to_string(totalLength);
    }

    // Increase its length and ensure that it still does.
    status = packet.IncreaseTotalLength(100);
    if (status != OKAY) {
      return "Error increasing stream packet size: " + ErrorMessage(status);
    }
    if (totalLength != STREAM_PACKET_BASE_SIZE + 100) {
      return "Error increasing stream packet size: packet length is not " +
        std::to_string(STREAM_PACKET_BASE_SIZE) + " but " + std::to_string(totalLength);
    }

    // Try to increase its length too much and ensure that it fails.
    status = packet.IncreaseTotalLength(packet.m_buffer->size() * 2);
    if (status != WRITE_PAST_END) {
      return "Unexpected return code from increasing stream packet size: " + ErrorMessage(status);
    }
  }

  {
    // Construct a StreamPacket with a specified sequence number and time.
    uint32_t sequenceNumber = 1234;
    Time timeCode = { 5678, 9012 };
    StreamPacket packet(1200, sequenceNumber, timeCode);
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet: " + ErrorMessage(packet.GetConstructorStatus());
    }

    // Check the values to make sure they match expectation.
    uint32_t rSequenceNumber;
    Status status = packet.GetSequenceNumber(rSequenceNumber);
    if (status != OKAY) {
      return "Error getting sequence number from stream packet: " + ErrorMessage(status);
    }
    if (rSequenceNumber != sequenceNumber) {
      return "Error getting sequence number from stream packet: sequence number is not " +
        std::to_string(sequenceNumber);
    }
    Time rTimeCode;
    status = packet.GetTimeCode(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from stream packet: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from stream packet: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }

    // Set a new sequence number and time code and check that they are set correctly.
    sequenceNumber = 4321;
    timeCode = { 8765, 4321 };
    status = packet.SetSequenceNumber(sequenceNumber);
    if (status != OKAY) {
      return "Error setting sequence number in stream packet: " + ErrorMessage(status);
    }
    status = packet.SetTimeCode(timeCode);
    if (status != OKAY) {
      return "Error setting time code in stream packet: " + ErrorMessage(status);
    }
    status = packet.GetSequenceNumber(rSequenceNumber);
    if (status != OKAY) {
      return "Error getting set sequence number from stream packet: " + ErrorMessage(status);
    }
    if (rSequenceNumber != sequenceNumber) {
      return "Error getting set sequence number from stream packet: sequence number is not " +
        std::to_string(sequenceNumber);
    }
    status = packet.GetTimeCode(rTimeCode);
    if (status != OKAY) {
      return "Error getting set time code from stream packet: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting set time code from stream packet: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
  }
  // Everything worked.
  return "";
}

Message::Message(StreamPacket& packet, uint32_t parameterSize, Time timeCode, MessageID type)
  : m_buffer(packet.m_buffer), m_offset(0), m_constructorStatus(OKAY)
{
  uint32_t totalSize = MESSAGE_BASE_SIZE + parameterSize;

  // Make sure that we have enough room in the buffer to add the message to the packet.
  uint32_t originalSize;
  Status status = packet.GetTotalLength(originalSize);
  if (status != OKAY) {
    m_constructorStatus = status;
    return;
  }
  m_offset = originalSize;
  status = packet.IncreaseTotalLength(totalSize);
  if (status != OKAY) {
    m_constructorStatus = status;
    return;
  }

  // Pack our header.
  uint8_t *bufPtr = m_buffer->data() + m_offset;
  memcpy(bufPtr, VERSION, sizeof(VERSION)); bufPtr += sizeof(VERSION);
  memcpy(bufPtr, &totalSize, sizeof(totalSize)); bufPtr += sizeof(totalSize);
  const uint32_t header_size = MESSAGE_BASE_SIZE;
  memcpy(bufPtr, &header_size, sizeof(header_size)); bufPtr += sizeof(header_size);
  memcpy(bufPtr, &timeCode.seconds, sizeof(timeCode.seconds)); bufPtr += sizeof(timeCode.seconds);
  memcpy(bufPtr, &timeCode.microseconds, sizeof(timeCode.microseconds)); bufPtr += sizeof(timeCode.microseconds);
  uint32_t myType = type;
  memcpy(bufPtr, &myType, sizeof(myType)); bufPtr += sizeof(myType);
}

Message::Message(std::shared_ptr<std::vector<uint8_t>> existingBuffer, uint32_t offset)
  : m_buffer(existingBuffer), m_offset(offset), m_constructorStatus(OKAY)
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status Message::GetConstructorStatus() const
{
  return m_constructorStatus;
}

Status Message::GetTime(Time& time) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE) {
    return READ_PAST_END;
  }
  memcpy(&time.seconds, m_buffer->data() + m_offset + MESSAGE_HEADER_MESSAGE_TIME_SECONDS_OFFSET,
    sizeof(time.seconds));
  memcpy(&time.microseconds, m_buffer->data() + m_offset + MESSAGE_HEADER_MESSAGE_TIME_MICROSECONDS_SIZE_OFFSET,
    sizeof(time.microseconds));
  return OKAY;
}

Status Message::GetType(MessageID& messageID) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE) {
    return READ_PAST_END;
  }
  memcpy(&messageID, m_buffer->data() + m_offset + MESSAGE_HEADER_MESSAGE_TYPE_OFFSET, sizeof(messageID));
  return OKAY;
}

Message::~Message()
{
}

std::string Message::Test()
{
  // Construct a message with no parameters and check its length, size, time and type.
  StreamPacket packet;
  if (packet.GetConstructorStatus() != OKAY) {
    return "Error constructing stream packet for message test: " + ErrorMessage(packet.GetConstructorStatus());
  }
  Time timeCode = { 1234, 5678 };
  Message message(packet, 0, timeCode, FRAME_END);
  if (message.GetConstructorStatus() != OKAY) {
    return "Error constructing message: " + ErrorMessage(message.GetConstructorStatus());
  }

  // Check the length of the packet including the message to make sure it matches expectation.
  uint32_t totalLength;
  Status status = packet.GetTotalLength(totalLength);
  if (status != OKAY) {
    return "Error checking message size for message test: " + ErrorMessage(status);
  }
  if (totalLength != STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE) {
    return "Error constructing message from buffer for message test: packet length is not " +
      std::to_string(STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE) + " but " + std::to_string(totalLength);
  }
  if (packet.m_buffer->size() != 9000 - 28) {
    return "Error constructing message from buffer for message test: packet size is not " +
      std::to_string(9000 - 28) + " but " + std::to_string(packet.m_buffer->size());
  }

  // Check the time and type of the message.
  Time rTimeCode;
  status = message.GetTime(rTimeCode);
  if (status != OKAY) {
    return "Error getting time code from message for message test: " + ErrorMessage(status);
  }
  if (rTimeCode != timeCode) {
    return "Error getting time code from message for message test: time code is not " +
      std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
  }
  MessageID type;
  status = message.GetType(type);
  if (status != OKAY) {
    return "Error getting type from message for message test: " + ErrorMessage(status);
  }
  if (type != FRAME_END) {
    return "Error getting type from message for message test: type is not FRAME_END";
  }

  // Construct a Message based on the existing one in the StreamPacket and make sure we
  // can read data from it as well.
  Message message2(packet.m_buffer, STREAM_PACKET_BASE_SIZE);
  if (message2.GetConstructorStatus() != OKAY) {
    return "Error constructing second message: " + ErrorMessage(message2.GetConstructorStatus());
  }
  status = message2.GetTime(rTimeCode);
  if (status != OKAY) {
    return "Error getting time code from second message for message test: " + ErrorMessage(status);
  }
  if (rTimeCode != timeCode) {
    return "Error getting time code from second message for message test: time code is not " +
      std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
  }
  status = message2.GetType(type);
  if (status != OKAY) {
    return "Error getting type from second message for message test: " + ErrorMessage(status);
  }
  if (type != FRAME_END) {
    return "Error getting type from second message for message test: type is not FRAME_END";
  }

  // Find the messages in the packet and make sure we can read them.
  std::shared_ptr<Message> message3;
  status = packet.GetNextMessage(message3);
  if (status != OKAY) {
    return "Error getting first message from packet for message test: " + ErrorMessage(status);
  }
  if (message3 == nullptr) {
    return "Error getting first message from packet for message test: message is null";
  }
  status = message3->GetTime(rTimeCode);
  if (status != OKAY) {
    return "Error getting time code from packet for message test: " + ErrorMessage(status);
  }
  if (rTimeCode != timeCode) {
    return "Error getting time code from packet for message test: time code is not " +
      std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
  }
  status = message3->GetType(type);
  if (status != OKAY) {
    return "Error getting type from packet for message test: " + ErrorMessage(status);
  }
  if (type != FRAME_END) {
    return "Error getting type from packet for message test: type is not FRAME_END";
  }
  status = packet.GetNextMessage(message3);
  if (status != OKAY) {
    return "Error getting second message from packet for message test: " + ErrorMessage(status);
  }
  if (message3 != nullptr) {
    return "Error getting second message from packet for message test: message is not null";
  }

  return "";
}

MessageDiscovery::MessageDiscovery(StreamPacket& packet, Time timeCode,
  uint32_t IP, uint16_t port, uint32_t serial)
  : Message(packet, 3 * sizeof(uint32_t), timeCode, DISCOVERY)
{
  // See if our subobject failed. If so, we're done.
  if (m_constructorStatus != OKAY) {
    return;
  }

  // Pack our parameters.
  unsigned char* bufPtr = m_buffer->data() + m_offset + MESSAGE_BASE_SIZE;
  memcpy(bufPtr, &IP, sizeof(IP)); bufPtr += sizeof(IP);
  uint32_t portField = port;
  memcpy(bufPtr, &portField, sizeof(portField)); bufPtr += sizeof(portField);
  memcpy(bufPtr, &serial, sizeof(serial)); bufPtr += sizeof(serial);
}

MessageDiscovery::MessageDiscovery(Message& baseMessage)
  : Message(baseMessage)
{
  MessageID type;
  baseMessage.GetType(type);
  if (type != DISCOVERY) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status MessageDiscovery::GetIP(uint32_t& IP) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + sizeof(IP)) {
    return READ_PAST_END;
  }
  memcpy(&IP, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE, sizeof(IP));
  return OKAY;
}

Status MessageDiscovery::GetPort(uint16_t& port) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  uint32_t portField;
  memcpy(&portField, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t), sizeof(portField));
  port = portField;
  return OKAY;
}

Status MessageDiscovery::GetSerial(uint32_t& serial) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 3 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  memcpy(&serial, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(serial));
  return OKAY;
}

std::string MessageDiscovery::Test()
{
  {
    // Construct a message and check its length, time and type.
    StreamPacket packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet for MessageDiscovery test: " + ErrorMessage(packet.GetConstructorStatus());
    }

    // Add a message.
    Time timeCode = { 1234, 5678 };
    uint32_t IP = 0x01020304, serial = 0x05060708;
    uint16_t port = 1234;
    MessageDiscovery message(packet, timeCode, IP, port, serial);
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageDiscovery: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Check the length of the packet including the message to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking message size for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (totalLength != STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + 3 * sizeof(uint32_t)) {
      return "Error constructing MessageDiscovery from buffer: packet length is not " +
        std::to_string(STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + 3 * sizeof(uint32_t)) + " but " +
        std::to_string(totalLength);
    }

    // Check the time and type of the message.
    Time rTimeCode;
    status = message.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from MessageDiscovery for MessageDiscovery test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    MessageID type;
    status = message.GetType(type);
    if (status != OKAY) {
      return "Error getting type from MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (type != DISCOVERY) {
      return "Error getting type from MessageDiscovery for MessageDiscovery test: type is not DISCOVERY";
    }

    // Check the values of the message
    uint32_t rIP, rSerial;
    uint16_t rPort;
    status = message.GetIP(rIP);
    if (status != OKAY) {
      return "Error getting IP from MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rIP != IP) {
      return "Error getting IP from MessageDiscovery for MessageDiscovery test: IP is not " +
        std::to_string(IP);
    }
    status = message.GetPort(rPort);
    if (status != OKAY) {
      return "Error getting port from MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rPort != port) {
      return "Error getting port from MessageDiscovery for MessageDiscovery test: port is not " +
        std::to_string(port);
    }
    status = message.GetSerial(rSerial);
    if (status != OKAY) {
      return "Error getting serial from MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rSerial != serial) {
      return "Error getting serial from MessageDiscovery for MessageDiscovery test: serial is not " +
        std::to_string(serial);
    }

    // Construct a MessageDiscovery based on the existing one in the StreamPacket and make sure we
    // can read data from it as well.
    MessageDiscovery message2(message);
    if (message2.GetConstructorStatus() != OKAY) {
      return "Error constructing second MessageDiscovery: " + ErrorMessage(message2.GetConstructorStatus());
    }
    status = message2.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from second MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from second MessageDiscovery for MessageDiscovery test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    status = message2.GetType(type);
    if (status != OKAY) {
      return "Error getting type from second MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (type != DISCOVERY) {
      return "Error getting type from second MessageDiscovery for MessageDiscovery test: type is not DISCOVERY";
    }
    status = message2.GetIP(rIP);
    if (status != OKAY) {
      return "Error getting IP from second MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rIP != IP) {
      return "Error getting IP from second MessageDiscovery for MessageDiscovery test: IP is not " +
        std::to_string(IP);
    }
    status = message2.GetPort(rPort);
    if (status != OKAY) {
      return "Error getting port from second MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rPort != port) {
      return "Error getting port from second MessageDiscovery for MessageDiscovery test: port is not " +
        std::to_string(port);
    }
    status = message2.GetSerial(rSerial);
    if (status != OKAY) {
      return "Error getting serial from second MessageDiscovery for MessageDiscovery test: " + ErrorMessage(status);
    }
    if (rSerial != serial) {
      return "Error getting serial from second MessageDiscovery for MessageDiscovery test: serial is not " +
        std::to_string(serial);
    }
  }

  return "";
}

MessageFrameBegin::MessageFrameBegin(StreamPacket& packet, Time timeCode,
  uint32_t cameraID, uint32_t cameraType, uint16_t sensorWidth, uint16_t sensorHeight,
  float exposure, float gain)
  : Message(packet,
            2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) + 2 * sizeof(float) + 128,
            timeCode, FRAME_BEGIN)
{
  // See if our subobject failed. If so, we're done.
  if (m_constructorStatus != OKAY) {
    return;
  }

  // Pack our parameters.
  unsigned char* bufPtr = m_buffer->data() + m_offset + MESSAGE_BASE_SIZE;
  memcpy(bufPtr, &cameraID, sizeof(cameraID)); bufPtr += sizeof(cameraID);
  memcpy(bufPtr, &cameraType, sizeof(cameraType)); bufPtr += sizeof(cameraType);
  memcpy(bufPtr, &sensorWidth, sizeof(sensorWidth)); bufPtr += sizeof(sensorWidth);
  memcpy(bufPtr, &sensorHeight, sizeof(sensorHeight)); bufPtr += sizeof(sensorHeight);
  memcpy(bufPtr, &exposure, sizeof(exposure)); bufPtr += sizeof(exposure);
  memcpy(bufPtr, &gain, sizeof(gain)); bufPtr += sizeof(gain);
}

MessageFrameBegin::MessageFrameBegin(Message& baseMessage)
  : Message(baseMessage)
{
  MessageID type;
  baseMessage.GetType(type);
  if (type != FRAME_BEGIN) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status MessageFrameBegin::GetCameraID(uint32_t& cameraID) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  memcpy(&cameraID, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE, sizeof(cameraID));
  return OKAY;
}

Status MessageFrameBegin::GetCameraType(uint32_t& cameraType) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  memcpy(&cameraType, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t), sizeof(cameraType));
  return OKAY;
}

Status MessageFrameBegin::GetSensorWidth(uint16_t& sensorWidth) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + sizeof(uint16_t)) {
    return READ_PAST_END;
  }
  memcpy(&sensorWidth, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t), sizeof(sensorWidth));
  return OKAY;
}

Status MessageFrameBegin::GetSensorHeight(uint16_t& sensorHeight) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t)) {
    return READ_PAST_END;
  }
  memcpy(&sensorHeight, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + sizeof(uint16_t),
    sizeof(sensorHeight));
  return OKAY;
}

Status MessageFrameBegin::GetExposure(float& exposure) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) + sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&exposure, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t),
    sizeof(exposure));
  return OKAY;
}

Status MessageFrameBegin::GetGain(float& gain) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) + 2 * sizeof(float)) {
    return READ_PAST_END;
  }
  memcpy(&gain, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) + sizeof(float),
    sizeof(gain));
  return OKAY;
}

std::string MessageFrameBegin::Test()
{
  {
    // Construct a message and check its length, time and type.
    StreamPacket packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet for MessageFrameBegin test: " + ErrorMessage(packet.GetConstructorStatus());
    }

    // Add a message.
    Time timeCode = { 1234, 5678 };
    uint32_t cameraID = 0, cameraType = 1;
    uint16_t sensorWidth = 100, sensorHeight = 100;
    float exposure = 1.0f, gain = 2.0f;
    MessageFrameBegin message(packet, timeCode, cameraID, cameraType, sensorWidth, sensorHeight, exposure, gain);
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageFrameBegin: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Check the length of the packet including the message to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking message size for MessageFrameBegin test: " + ErrorMessage(status);
    }
    uint32_t expectedLength = STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + 2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) + 2 * sizeof(float) + 128;
    if (totalLength != expectedLength) {
      return "Error constructing message from buffer for MessageFrameBegin test: packet length is not " +
        std::to_string(expectedLength) + " but " + std::to_string(totalLength);
    }

    // Check the time and type of the message.
    Time rTimeCode;
    status = message.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from message for MessageFrameBegin test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    MessageID type;
    status = message.GetType(type);
    if (status != OKAY) {
      return "Error getting type from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (type != FRAME_BEGIN) {
      return "Error getting type from message for MessageFrameBegin test: type is not FRAME_BEGIN";
    }

    // Check the parameters of the message.
    uint32_t rCameraID;
    status = message.GetCameraID(rCameraID);
    if (status != OKAY) {
      return "Error getting camera ID from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rCameraID != cameraID) {
      return "Error getting camera ID from message for MessageFrameBegin test: camera ID is not " +
        std::to_string(cameraID);
    }
    uint32_t rCameraType;
    status = message.GetCameraType(rCameraType);
    if (status != OKAY) {
      return "Error getting camera type from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rCameraType != cameraType) {
      return "Error getting camera type from message for MessageFrameBegin test: camera type is not " +
        std::to_string(cameraType);
    }
    uint16_t rSensorWidth;
    status = message.GetSensorWidth(rSensorWidth);
    if (status != OKAY) {
      return "Error getting sensor width from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rSensorWidth != sensorWidth) {
      return "Error getting sensor width from message for MessageFrameBegin test: sensor width is not " +
        std::to_string(sensorWidth);
    }
    uint16_t rSensorHeight;
    status = message.GetSensorHeight(rSensorHeight);
    if (status != OKAY) {
      return "Error getting sensor height from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rSensorHeight != sensorHeight) {
      return "Error getting sensor height from message for MessageFrameBegin test: sensor height is not " +
        std::to_string(sensorHeight);
    }
    float rExposure;
    status = message.GetExposure(rExposure);
    if (status != OKAY) {
      return "Error getting exposure from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rExposure != exposure) {
      return "Error getting exposure from message for MessageFrameBegin test: exposure is not " +
        std::to_string(exposure);
    }
    float rGain;
    status = message.GetGain(rGain);
    if (status != OKAY) {
      return "Error getting gain from message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rGain != gain) {
      return "Error getting gain from message for MessageFrameBegin test: gain is not " +
        std::to_string(gain);
    }

    // Construct a Message based on the existing one in the StreamPacket and make sure we
    // Can read its parameters (just doing a spot check on the final parameter).
    MessageFrameBegin message2(message);
    if (message2.GetConstructorStatus() != OKAY) {
      return "Error constructing second MessageFrameBegin: " + ErrorMessage(message2.GetConstructorStatus());
    }
    status = message2.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from second message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from second message for MessageFrameBegin test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    status = message2.GetType(type);
    if (status != OKAY) {
      return "Error getting type from second message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (type != FRAME_BEGIN) {
      return "Error getting type from second message for MessageFrameBegin test: type is not FRAME_BEGIN";
    }
    status = message2.GetGain(rGain);
    if (status != OKAY) {
      return "Error getting gain from second message for MessageFrameBegin test: " + ErrorMessage(status);
    }
    if (rGain != gain) {
      return "Error getting gain from second message for MessageFrameBegin test: gain is not " +
        std::to_string(gain);
    }
  }

  return "";
}

MessageFrameData::MessageFrameData(StreamPacket& packet, Time timeCode,
  uint32_t cameraID, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom,
  uint8_t* data, uint16_t stride)
  : Message(packet,
    // The size of the message is the size of the parameters plus the size of the data.
    // We pad the size of the data to a multiple of 4 bytes.
    sizeof(cameraID) + sizeof(left) + sizeof(top) + sizeof(right) + sizeof(bottom) +
    PaddedSize(2 * (right - left + 1) * (bottom - top + 1)),
    timeCode, FRAME_DATA)
{
  // See if our subobject failed. If so, we're done.
  if (m_constructorStatus != OKAY) {
    return;
  }

  // Check our parameters.
  if ((data == nullptr) || (right < left) || (bottom < top) || (stride < (right-left+1))) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }

  // Pack our parameters.
  unsigned char* bufPtr = m_buffer->data() + m_offset + MESSAGE_BASE_SIZE;
  memcpy(bufPtr, &cameraID, sizeof(cameraID)); bufPtr += sizeof(cameraID);
  memcpy(bufPtr, &left, sizeof(left)); bufPtr += sizeof(left);
  memcpy(bufPtr, &top, sizeof(top)); bufPtr += sizeof(top);
  memcpy(bufPtr, &right, sizeof(right)); bufPtr += sizeof(right);
  memcpy(bufPtr, &bottom, sizeof(bottom)); bufPtr += sizeof(bottom);

  // Copy the data a row at a time.
  size_t rowStride = stride * sizeof(uint16_t);
  size_t rowSize = sizeof(uint16_t) * (right - left + 1);
  for (uint16_t row = top; row <= bottom; row++) {
    memcpy(bufPtr, data + row * rowStride + left * sizeof(uint16_t), rowSize);
    bufPtr += rowSize;
  }
}

MessageFrameData::MessageFrameData(Message& baseMessage)
  : Message(baseMessage)
{
  MessageID type;
  baseMessage.GetType(type);
  if (type != FRAME_DATA) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status MessageFrameData::GetCameraID(uint32_t& cameraID) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE;
  if (m_buffer->size() < myOffset + sizeof(cameraID)) {
    return READ_PAST_END;
  }
  memcpy(&cameraID, m_buffer->data() + myOffset, sizeof(cameraID));
  return OKAY;
}

Status MessageFrameData::GetLeft(uint16_t& left) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t);
  if (m_buffer->size() < myOffset + sizeof(left)) {
    return READ_PAST_END;
  }
  memcpy(&left, m_buffer->data() + myOffset, sizeof(left));
  return OKAY;
}

Status MessageFrameData::GetTop(uint16_t& top) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t) + sizeof(uint16_t);
  if (m_buffer->size() < myOffset + sizeof(top)) {
    return READ_PAST_END;
  }
  memcpy(&top, m_buffer->data() + myOffset, sizeof(top));
  return OKAY;
}

Status MessageFrameData::GetRight(uint16_t& right) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t) + 2 * sizeof(uint16_t);
  if (m_buffer->size() < myOffset + sizeof(right)) {
    return READ_PAST_END;
  }
  memcpy(&right, m_buffer->data() + myOffset, sizeof(right));
  return OKAY;
}

Status MessageFrameData::GetBottom(uint16_t& bottom) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t) + 3 * sizeof(uint16_t);
  if (m_buffer->size() < myOffset + sizeof(bottom)) {
    return READ_PAST_END;
  }
  memcpy(&bottom, m_buffer->data() + myOffset, sizeof(bottom));
  return OKAY;
}

Status MessageFrameData::GetDataPointer(uint8_t** data) const
{
  if (data == nullptr) {
    return BAD_PARAMETER;
  }
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t) + 4 * sizeof(uint16_t);
  if (m_buffer->size() < myOffset) {
    return READ_PAST_END;
  }
  *data = m_buffer->data() + myOffset;
  return OKAY;
}

std::string MessageFrameData::Test()
{
  {
    // Construct a message and check its length, time and type.
    StreamPacket packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet for MessageFrameData test: " + ErrorMessage(packet.GetConstructorStatus());
    }

    // Try to add a message that is too long for the packet. It should fail.
    Time timeCode = { 1234, 5678 };
    uint32_t cameraID = 0;
    uint16_t left = 0, top = 0, right = 99, bottom = 99;
    std::vector<uint8_t> data(sizeof(uint16_t) * (right - left + 1) * (bottom - top + 1), 0);
    MessageFrameData badMessage(packet, timeCode, cameraID, left, top, right, bottom, data.data(), 100);
    if (badMessage.GetConstructorStatus() != WRITE_PAST_END) {
      return "Unexpected success constructing too-large MessageFrameData";
    }

    // Now add a reasonable-sized message
    bottom = 0;
    MessageFrameData message(packet, timeCode, cameraID, left, top, right, bottom, data.data(), 100);
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageFrameData: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Check the length of the packet including the message to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking message size for MessageFrameData test: " + ErrorMessage(status);
    }
    uint32_t expectedLength = STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + sizeof(uint32_t)
      + 4 * sizeof(uint16_t) + PaddedSize(sizeof(uint16_t) * (right - left + 1) * (bottom - top + 1));
    if (totalLength != expectedLength) {
      return "Error constructing message from buffer for MessageFrameData test: packet length is not " +
        std::to_string(expectedLength) + " but " + std::to_string(totalLength);
    }

    // Check the time and type of the message.
    Time rTimeCode;
    status = message.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from message for MessageFrameData test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    MessageID type;
    status = message.GetType(type);
    if (status != OKAY) {
      return "Error getting type from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (type != FRAME_DATA) {
      return "Error getting type from message for MessageFrameData test: type is not FRAME_DATA";
    }

    // Check the other messaege parameters.
    uint32_t rCameraID;
    uint16_t rLeft, rTop, rRight, rBottom;
    status = message.GetCameraID(rCameraID);
    if (status != OKAY) {
      return "Error getting camera ID from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rCameraID != cameraID) {
      return "Error getting camera ID from message for MessageFrameData test: Camera ID is not " +
        std::to_string(cameraID);
    }
    status = message.GetLeft(rLeft);
    if (status != OKAY) {
      return "Error getting left from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rLeft != left) {
      return "Error getting left from message for MessageFrameData test: left is not " +
        std::to_string(left);
    }
    status = message.GetTop(rTop);
    if (status != OKAY) {
      return "Error getting top from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rTop != top) {
      return "Error getting top from message for MessageFrameData test: top is not " +
        std::to_string(top);
    }
    status = message.GetRight(rRight);
    if (status != OKAY) {
      return "Error getting right from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rRight != right) {
      return "Error getting right from message for MessageFrameData test: right is not " +
        std::to_string(right);
    }
    status = message.GetBottom(rBottom);
    if (status != OKAY) {
      return "Error getting bottom from message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rBottom != bottom) {
      return "Error getting bottom from message for MessageFrameData test: bottom is not " +
        std::to_string(bottom);
    }
    uint8_t* rData;
    status = message.GetDataPointer(&rData);
    if (status != OKAY) {
      return "Error getting data pointer from message for MessageFrameData test: " + ErrorMessage(status);
    }
    uint32_t expectedOffset = STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + sizeof(uint32_t) +
      4 * sizeof(uint16_t);
    if (rData != packet.m_buffer->data() + expectedOffset) {
      return "Error getting data pointer from message for MessageFrameData test: unexpected data pointer.";
    }

    // Construct a Message based on the existing one and make sure we
    // can read data from it as well.
    MessageFrameData message2(message);
    if (message2.GetConstructorStatus() != OKAY) {
      return "Error constructing second MessageFrameData: " + ErrorMessage(message2.GetConstructorStatus());
    }
    status = message2.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from second message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from second message for MessageFrameData test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    status = message2.GetType(type);
    if (status != OKAY) {
      return "Error getting type from second message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (type != FRAME_DATA) {
      return "Error getting type from second message for MessageFrameData test: type is not FRAME_DATA";
    }

    // Check the bottom and data pointer for the second message to make sure they are the same.
    status = message2.GetBottom(rBottom);
    if (status != OKAY) {
      return "Error getting bottom from second message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rBottom != bottom) {
      return "Error getting bottom from second message for MessageFrameData test: bottom is not " +
        std::to_string(bottom);
    }
    status = message2.GetDataPointer(&rData);
    if (status != OKAY) {
      return "Error getting data pointer from second message for MessageFrameData test: " + ErrorMessage(status);
    }
    if (rData != packet.m_buffer->data() + expectedOffset) {
      return "Error getting data pointer from second message for MessageFrameData test: unexpected data pointer.";
    }
  }

  return "";
}

MessageFrameEnd::MessageFrameEnd(StreamPacket& packet, Time timeCode, uint32_t cameraID)
  : Message(packet, sizeof(cameraID), timeCode, FRAME_END)
{
  // See if our subobject failed. If so, we're done.
  if (m_constructorStatus != OKAY) {
    return;
  }

  // Pack our parameters.
  unsigned char* bufPtr = m_buffer->data() + m_offset + MESSAGE_BASE_SIZE;
  memcpy(bufPtr, &cameraID, sizeof(cameraID)); bufPtr += sizeof(cameraID);
}

MessageFrameEnd::MessageFrameEnd(Message& baseMessage)
  : Message(baseMessage)
{
  MessageID type;
  baseMessage.GetType(type);
  if (type != FRAME_END) {
    m_constructorStatus = BAD_PARAMETER;
  }
}

Status MessageFrameEnd::GetCameraID(uint32_t& cameraID) const
{
  if (m_buffer->size() < m_offset + MESSAGE_BASE_SIZE + sizeof(uint32_t)) {
    return READ_PAST_END;
  }
  memcpy(&cameraID, m_buffer->data() + m_offset + MESSAGE_BASE_SIZE, sizeof(cameraID));
  return OKAY;
}

std::string MessageFrameEnd::Test()
{
  {
    // Construct a message with no parameters and check its length, time and type.
    StreamPacket packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet for MessageFrameEnd test: " + ErrorMessage(packet.GetConstructorStatus());
    }
    uint32_t cameraID = 100;
    Time timeCode = { 1234, 5678 };
    MessageFrameEnd message(packet, timeCode, cameraID);
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageFrameEnd: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Check the length of the packet including the message to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking message size for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (totalLength != STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + sizeof(uint32_t)) {
      return "Error constructing message from buffer for MessageFrameEnd test: packet length is not " +
        std::to_string(STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE) + " but " + std::to_string(totalLength);
    }

    // Check the time and type of the message.
    Time rTimeCode;
    status = message.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from message for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from message for MessageFrameEnd test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    MessageID type;
    status = message.GetType(type);
    if (status != OKAY) {
      return "Error getting type from message for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (type != FRAME_END) {
      return "Error getting type from message for MessageFrameEnd test: type is not FRAME_END";
    }

    // Check the other messaege parameters.
    uint32_t rCameraID;
    status = message.GetCameraID(rCameraID);
    if (status != OKAY) {
      return "Error getting camera ID from message for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (rCameraID != cameraID) {
      return "Error getting camera ID from message for MessageFrameEnd test: Camera ID is not " +
        std::to_string(cameraID);
    }

    // Construct a Message based on the existing one and make sure we
    // can read data from it as well.
    MessageFrameEnd message2(message);
    if (message2.GetConstructorStatus() != OKAY) {
      return "Error constructing second MessageFrameEnd: " + ErrorMessage(message2.GetConstructorStatus());
    }
    status = message2.GetTime(rTimeCode);
    if (status != OKAY) {
      return "Error getting time code from second message for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (rTimeCode != timeCode) {
      return "Error getting time code from second message for MessageFrameEnd test: time code is not " +
        std::to_string(timeCode.seconds) + "." + std::to_string(timeCode.microseconds);
    }
    status = message2.GetType(type);
    if (status != OKAY) {
      return "Error getting type from second message for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (type != FRAME_END) {
      return "Error getting type from second message for MessageFrameEnd test: type is not FRAME_END";
    }
  }

  return "";
}

class asdp::Socket {
public:
  SOCKET socket;    ///< The socket to use
};

SenderUDP::SenderUDP(std::string host, uint16_t port)
  : m_socket(std::make_shared<Socket>())
  , m_IP(0)
  , m_port(0)
{
  // Map the host name to an IP address and set the port.
  struct addrinfo hints, * res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags |= AI_CANONNAME;
  if (0 != getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res)) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }

  // Open the socket to use for sending UDP packets.
  m_socket->socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (m_socket->socket == BAD_SOCKET) {
    m_constructorStatus = BAD_PARAMETER;
    freeaddrinfo(res);
    return;
  }

  // Record our IP address and port.
  struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
  m_IP = ntohl(addr->sin_addr.s_addr);
  m_port = ntohs(addr->sin_port);

  // Connect the socket to the specified host and port.
  if (0 != connect(m_socket->socket, res->ai_addr, res->ai_addrlen)) {
    m_constructorStatus = SOCKET_FAILURE;
    freeaddrinfo(res);
    closesocket(m_socket->socket);
    return;
  }

  // Free our resources
  freeaddrinfo(res);
}

SenderUDP::~SenderUDP()
{
  if ((m_socket != nullptr) && (m_socket->socket != BAD_SOCKET)) {
    closesocket(m_socket->socket);
  }
}

Status SenderUDP::Send(const void* buffer, uint32_t length)
{
  // Check our parameters
  if (buffer == nullptr) {
    return BAD_PARAMETER;
  }

  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Send the data.
  int result = send(m_socket->socket, (const char*)buffer, length, 0);
  if (result == SOCKET_ERROR) {
    return SOCKET_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

Status SenderUDP::SendCommandPacket(const CommandPacket& packet)
{
  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Send the data.
  int result = send(m_socket->socket, (const char*)packet.m_buffer->data(), packet.m_buffer->size(), 0);
  if (result == SOCKET_ERROR) {
    return SOCKET_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

Status SenderUDP::SendStreamPacket(const StreamPacket& packet)
{
  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Find out how large the data in the packet is.
  uint32_t length;
  Status status = packet.GetTotalLength(length);
  if (status != OKAY) {
    return status;
  }

  // Send the data.
  int result = send(m_socket->socket, (const char*)packet.m_buffer->data(), length, 0);
  if (result == SOCKET_ERROR) {
    return SOCKET_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

Status SenderUDP::GetIP(uint32_t &IP) const
{
  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Return the IP address.
  IP = m_IP;
  return OKAY;
}

Status SenderUDP::GetPort(uint16_t& port) const
{
  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Return the port.
  port = m_port;
  return OKAY;
}

SenderFile::SenderFile(std::string fileName)
{
  // Open the file.
  m_file = std::make_shared<std::ofstream>(fileName.c_str(), std::ios::binary);
  if (m_file == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
}

SenderFile::~SenderFile()
{
  if (m_file != nullptr) {
    m_file->close();
  }
}

Status SenderFile::Send(const void* buffer, uint32_t length)
{
  // Check our parameters
  if (buffer == nullptr) {
    return BAD_PARAMETER;
  }

  // Make sure we have a valid file.
  if (m_file == nullptr) {
    return m_constructorStatus;
  }
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Send the data.
  m_file->write(reinterpret_cast<const char*>(buffer), length);
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

Status SenderFile::SendCommandPacket(const CommandPacket& packet)
{
  // Make sure we have a valid file.
  if (m_file == nullptr) {
    return m_constructorStatus;
  }
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Send the data.
  m_file->write(reinterpret_cast<const char*>(packet.m_buffer->data()), packet.m_buffer->size());
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

Status SenderFile::SendStreamPacket(const StreamPacket& packet)
{
  // Make sure we have a valid file.
  if (m_file == nullptr) {
    return m_constructorStatus;
  }
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Find out how large the data in the packet is.
  uint32_t length;
  Status status = packet.GetTotalLength(length);
  if (status != OKAY) {
    return status;
  }

  // Send the data.
  // Send the data.
  m_file->write(reinterpret_cast<const char*>(packet.m_buffer->data()), length);
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

ReceiverUDP::ReceiverUDP(std::string host, uint16_t port, uint32_t maxLen)
  : Receiver(maxLen)
  , m_socket(std::make_shared<Socket>())
  , m_port(port)
{
  // Map the host name to an IP address and set the port.
  struct addrinfo hints, * res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags |= AI_CANONNAME;
  const char *hostName = host.c_str();
  if (host == "") {
    hostName = nullptr;
    hints.ai_flags |= AI_PASSIVE;         ///< Make the socket available for binding across multiple NICs.
  }
  std::string portString = std::to_string(m_port);
  const char *portName = portString.c_str();
  if (0 != getaddrinfo(hostName, portName, &hints, &res)) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }

  // Open the socket to use for receiving UDP packets.
  m_socket->socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (m_socket->socket == BAD_SOCKET) {
    m_constructorStatus = BAD_PARAMETER;
    freeaddrinfo(res);
    return;
  }

  // Bind the socket to the specified NIC and port.
  if (0 != bind(m_socket->socket, res->ai_addr, res->ai_addrlen)) {
    m_constructorStatus = SOCKET_FAILURE;
    freeaddrinfo(res);
    closesocket(m_socket->socket);
    return;
  }

  // If we didn't specify a port, get the port we were assigned by the bind.
  if (m_port == 0) {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(m_socket->socket, (struct sockaddr *)&sin, &len) == -1) {
      m_constructorStatus = SOCKET_FAILURE;
      freeaddrinfo(res);
      closesocket(m_socket->socket);
      return;
    }
    m_port = ntohs(sin.sin_port);
  }

  // Free our resources
  freeaddrinfo(res);
}

ReceiverUDP::~ReceiverUDP()
{
  if ((m_socket != nullptr) && (m_socket->socket != BAD_SOCKET)) {
    closesocket(m_socket->socket);
  }
}

Status ReceiverUDP::IsPacketAvailable(double timeout_seconds, bool& available)
{
  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Set up the timeout.
  struct timeval timeout;
  timeout.tv_sec = (long)timeout_seconds;
  timeout.tv_usec = (long)((timeout_seconds - (long)timeout_seconds) * 1000000);

  // Set up the file descriptor set.
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(m_socket->socket, &fds);

  // Wait for the socket to be ready.
  int result = select(m_socket->socket + 1, &fds, nullptr, nullptr, &timeout);
  if (result == SOCKET_ERROR) {
    return SOCKET_FAILURE;
  }

  // Check if the socket is ready.
  available = (result > 0);
  return OKAY;
}

Status ReceiverUDP::ReceiveBuffer(std::vector<uint8_t>& buffer)
{
  // Make sure we have a valid socket.
  if ((m_socket == nullptr) || (m_socket->socket == BAD_SOCKET)) {
    return m_constructorStatus;
  }

  // Receive the data. On Linux, we need to ask it to inform us if the buffer is too small.
  // On Windows, it returns an error if the buffer is too small.
#ifdef ASDP_USE_WINSOCK_SOCKETS
  #define FLAGS 0
#else
  #define FLAGS MSG_TRUNC
#endif
  int length = recv(m_socket->socket, reinterpret_cast<char*>(buffer.data()), buffer.size(), FLAGS);
  if (length == SOCKET_ERROR) {
    // Windows will fall through to here if the buffer is too small.
    // There is no way to tell on Windows whether the buffer was too small or if there was some other error.
    return SOCKET_READ_FAILURE;
  }
  if (length > buffer.size()) {
    // Return the same error on Windows and Linux when the buffer is too small.
    return SOCKET_READ_FAILURE;
  }

  // Record how many bytes we received by resizing the buffer to this size.
  buffer.resize(length);

  // Everything worked.
  return OKAY;
}

Status ReceiverUDP::ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet)
{
  // See if we have a packet available.
  bool available;
  Status status = IsPacketAvailable(timeout_seconds, available);
  if (status != OKAY) {
    return status;
  }
  if (!available) {
    return TIMEOUT;
  }

  // Get the packet.
  std::shared_ptr<std::vector<uint8_t>> buffer = std::make_shared<std::vector<uint8_t>>(m_maxLen);
  status = ReceiveBuffer(*buffer);
  if (status != OKAY) {
    return status;
  }

  // Construct the packet using the buffer.
  CommandPacket *commandPacketPtr = new CommandPacket(buffer);
  packet.reset(commandPacketPtr);
  if (packet->GetConstructorStatus() != OKAY) {
    return packet->GetConstructorStatus();
  }

  // Everything worked.
  return OKAY;
}

Status ReceiverUDP::ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet)
{
  // See if we have a packet available.
  bool available;
  Status status = IsPacketAvailable(timeout_seconds, available);
  if (status != OKAY) {
    return status;
  }
  if (!available) {
    return TIMEOUT;
  }

  // Get the packet.
  std::shared_ptr<std::vector<uint8_t>> buffer = std::make_shared<std::vector<uint8_t>>(m_maxLen);
  status = ReceiveBuffer(*buffer);
  if (status != OKAY) {
    return status;
  }

  // Construct the packet using the buffer.
  StreamPacket *streamPacketPtr = new StreamPacket(buffer);
  packet.reset(streamPacketPtr);
  if (packet->GetConstructorStatus() != OKAY) {
    return packet->GetConstructorStatus();
  }

  // Everything worked.
  return OKAY;
}

std::string ReceiverUDP::Test()
{
  Status status;

  // Create a sender and receiver.
  ReceiverUDP receiver;
  if (receiver.GetConstructorStatus() != OKAY) {
    return "Error constructing ReceiverUDP: " + ErrorMessage(receiver.GetConstructorStatus());
  }
  uint16_t port;
  port = receiver.GetPort();
  SenderUDP sender("localhost", port);
  if (sender.GetConstructorStatus() != OKAY) {
    return "Error constructing SenderUDP: " + ErrorMessage(sender.GetConstructorStatus());
  }

  // Verify that we can get the IP address and port from the sender.
  uint32_t IP;
  status = sender.GetIP(IP);
  if (status != OKAY) {
    return "Error getting IP address from SenderUDP: " + ErrorMessage(status);
  }
  if (IP != 0x7f000001) {
    return "Error getting IP address from SenderUDP: IP address is not Localhost";
  }
  status = sender.GetPort(port);
  if (status != OKAY) {
    return "Error getting port from SenderUDP: " + ErrorMessage(status);
  }
  if (port != receiver.GetPort()) {
    return "Error getting port from SenderUDP: port is not " + std::to_string(receiver.GetPort());
  }

  // Send a packet.
  std::vector<uint8_t> sendBuffer(1000, 0);
  status = sender.Send(sendBuffer.data(), sendBuffer.size());
  if (status != OKAY) {
    return "Error sending packet: " + ErrorMessage(status);
  }

  // Receive the packet into a buffer that it larger than the packet.
  bool available;
  status = receiver.IsPacketAvailable(0.5, available);
  if (status != OKAY) {
    return "Error checking for packet: " + ErrorMessage(status);
  }
  if (!available) {
    return "Error checking for packet: no packet available";
  }
  std::vector<uint8_t> receiveBuffer(2000, 0);
  status = receiver.ReceiveBuffer(receiveBuffer);
  if (status != OKAY) {
    return "Error receiving packet: " + ErrorMessage(status);
  }
  if (receiveBuffer.size() != sendBuffer.size()) {
    return "Error receiving packet: buffer was not resized";
  }

  // Try sending and receiving into a buffer that is too small, which should fail.
  status = sender.Send(sendBuffer.data(), sendBuffer.size());
  if (status != OKAY) {
    return "Error sending second packet: " + ErrorMessage(status);
  }
  status = receiver.IsPacketAvailable(0.5, available);
  if (status != OKAY) {
    return "Error checking for second packet: " + ErrorMessage(status);
  }
  if (!available) {
    return "Error checking for second packet: no packet available";
  }
  receiveBuffer.resize(100, 0);
  status = receiver.ReceiveBuffer(receiveBuffer);
  if (status != SOCKET_READ_FAILURE) {
    return "Unexpected return value when receiving into a too-small buffer";
  }

  // Try sending and receiving a CommandPacket.
  CommandPacketReset sendCommandPacket;
  status = sendCommandPacket.GetConstructorStatus();
  if (status != OKAY) {
    return "Error constructing CommandPacketReset: " + ErrorMessage(status);
  }
  status = sender.SendCommandPacket(sendCommandPacket);
  if (status != OKAY) {
    return "Error sending CommandPacketReset: " + ErrorMessage(status);
  }
  std::shared_ptr<CommandPacket> receiveCommandPacket;
  status = receiver.ReceiveCommandPacket(0.5, receiveCommandPacket);
  if (status != OKAY) {
    return "Error receiving CommandPacketReset: " + ErrorMessage(status);
  }
  if (receiveCommandPacket == nullptr) {
    return "Empty CommandPacketReset packet";
  }
  OpCode opCode;
  status = receiveCommandPacket->GetOpCode(opCode);
  if (status != OKAY) {
    return "Error checking CommandPacketReset opcode: " + ErrorMessage(status);
  }
  if (opCode != RESET) {
    return "Error receiving CommandPacketReset: wrong type";
  }

  // Try sending and receiving a StreamPacket.
  StreamPacket sendStreamPacket;
  status = sendStreamPacket.GetConstructorStatus();
  if (status != OKAY) {
    return "Error constructing StreamPacket: " + ErrorMessage(status);
  }
  status = sender.SendStreamPacket(sendStreamPacket);
  if (status != OKAY) {
    return "Error sending StreamPacket: " + ErrorMessage(status);
  }
  std::shared_ptr<StreamPacket> receiveStreamPacket;
  status = receiver.ReceiveStreamPacket(0.5, receiveStreamPacket);
  if (status != OKAY) {
    return "Error receiving StreamPacket: " + ErrorMessage(status);
  }
  if (receiveStreamPacket == nullptr) {
    return "Empty StreamPacket packet";
  }

  return "";
}

ReceiverFile::ReceiverFile(std::string fileName, uint32_t maxLen)
  : Receiver(maxLen)
{
  // Open the file.
  m_file = std::make_shared<std::ifstream>(fileName.c_str(), std::ios::binary);
  if (m_file == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
}

ReceiverFile::~ReceiverFile()
{
  if (m_file != nullptr) {
    m_file->close();
  }
}

Status ReceiverFile::IsPacketAvailable(double timeout_seconds, bool& available)
{
  // Make sure we have a valid file.
  if (m_file == nullptr) {
    return m_constructorStatus;
  }
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  /// @todo Implement timeout.

  // Check if there is more data available in the file.
  available = !m_file->eof();
  return OKAY;
}

Status ReceiverFile::ReceiveBuffer(std::vector<uint8_t>& buffer)
{
  // Make sure we have a valid file.
  if (m_file == nullptr) {
    return m_constructorStatus;
  }
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Read the data.  If there is not enough data to fill the buffer,
  // the buffer will be resized to the amount of data read.
  m_file->read(reinterpret_cast<char*>(buffer.data()), buffer.size());
  std::streamsize bytesActuallyRead = m_file->gcount();
  if (bytesActuallyRead < buffer.size()) {
    buffer.resize(bytesActuallyRead);
  }
  if (bytesActuallyRead == 0) {
    return FILE_FAILURE;
  }

  // Everything worked.
  return OKAY;
}

Status ReceiverFile::ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet)
{
  // See if we have a packet available.
  bool available;
  Status status = IsPacketAvailable(timeout_seconds, available);
  if (status != OKAY) {
    return status;
  }
  if (!available) {
    return TIMEOUT;
  }

  // Get the packet header up through the field that records the length.
  if (m_maxLen < 3 * sizeof(uint32_t)) {
    return WRITE_PAST_END;
  }
  std::shared_ptr<std::vector<uint8_t>> buffer = std::make_shared<std::vector<uint8_t>>(m_maxLen);
  m_file->read(reinterpret_cast<char*>(buffer->data()), PACKET_HEADER_TOTAL_SIZE_OFFSET + sizeof(uint32_t));
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Find the length of the packet.
  uint32_t length;
  memcpy(&length, buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET, sizeof(length));
  if (length > m_maxLen) {
    return WRITE_PAST_END;
  }

  // Read the rest of the packet.
  m_file->read(reinterpret_cast<char*>(buffer->data()) + PACKET_HEADER_TOTAL_SIZE_OFFSET + sizeof(uint32_t),
    length - PACKET_HEADER_TOTAL_SIZE_OFFSET - sizeof(uint32_t));
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Construct the packet using the buffer.
  CommandPacket* commandPacketPtr = new CommandPacket(buffer);
  packet.reset(commandPacketPtr);
  if (packet->GetConstructorStatus() != OKAY) {
    return packet->GetConstructorStatus();
  }

  // Everything worked.
  return OKAY;
}

Status ReceiverFile::ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet)
{
  // See if we have a packet available.
  bool available;
  Status status = IsPacketAvailable(timeout_seconds, available);
  if (status != OKAY) {
    return status;
  }
  if (!available) {
    return TIMEOUT;
  }

  // Get the packet header up through the field that records the length.
  if (m_maxLen < 3 * sizeof(uint32_t)) {
    return WRITE_PAST_END;
  }
  std::shared_ptr<std::vector<uint8_t>> buffer = std::make_shared<std::vector<uint8_t>>(m_maxLen);
  m_file->read(reinterpret_cast<char*>(buffer->data()), PACKET_HEADER_TOTAL_SIZE_OFFSET + sizeof(uint32_t));
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Find the length of the packet.
  uint32_t length;
  memcpy(&length, buffer->data() + PACKET_HEADER_TOTAL_SIZE_OFFSET, sizeof(length));
  if (length > m_maxLen) {
    return WRITE_PAST_END;
  }

  // Read the rest of the packet.
  m_file->read(reinterpret_cast<char*>(buffer->data()) + PACKET_HEADER_TOTAL_SIZE_OFFSET + sizeof(uint32_t),
    length - PACKET_HEADER_TOTAL_SIZE_OFFSET - sizeof(uint32_t));
  if (!(*m_file)) {
    return FILE_FAILURE;
  }

  // Construct the packet using the buffer.
  StreamPacket* streamPacketPtr = new StreamPacket(buffer);
  packet.reset(streamPacketPtr);
  if (packet->GetConstructorStatus() != OKAY) {
    return packet->GetConstructorStatus();
  }

  // Everything worked.
  return OKAY;
}

std::string ReceiverFile::Test()
{
  Status status;

  // Send a packet.
  std::vector<uint8_t> sendBuffer(1000, 0);
  {
    SenderFile sender("deleteme.bin");
    if (sender.GetConstructorStatus() != OKAY) {
      return "Error constructing SenderFile: " + ErrorMessage(sender.GetConstructorStatus());
    }
    status = sender.Send(sendBuffer.data(), sendBuffer.size());
    if (status != OKAY) {
      return "Error sending packet: " + ErrorMessage(status);
    }
  }

  // Receive the packet into a buffer that it larger than the packet.
  {
    ReceiverFile receiver("deleteme.bin");
    if (receiver.GetConstructorStatus() != OKAY) {
      return "Error constructing ReceiverFile: " + ErrorMessage(receiver.GetConstructorStatus());
    }
    bool available;
    status = receiver.IsPacketAvailable(0.5, available);
    if (status != OKAY) {
      return "Error checking for packet: " + ErrorMessage(status);
    }
    if (!available) {
      return "Error checking for packet: no packet available";
    }
    std::vector<uint8_t> receiveBuffer(2000, 0);
    status = receiver.ReceiveBuffer(receiveBuffer);
    if (status != OKAY) {
      return "Error receiving packet: " + ErrorMessage(status);
    }
    if (receiveBuffer.size() != sendBuffer.size()) {
      return "Error receiving packet: buffer was not resized";
    }
  }

  // Delete the file.
  remove("deleteme.bin");

  // Try sending and receiving a CommandPacket.
  {
    SenderFile sender("deleteme.bin");
    if (sender.GetConstructorStatus() != OKAY) {
      return "Error constructing SenderFile for CommandPacket: " + ErrorMessage(sender.GetConstructorStatus());
    }

    CommandPacketReset sendCommandPacket;
    status = sendCommandPacket.GetConstructorStatus();
    if (status != OKAY) {
      return "Error constructing CommandPacketReset: " + ErrorMessage(status);
    }
    status = sender.SendCommandPacket(sendCommandPacket);
    if (status != OKAY) {
      return "Error sending CommandPacketReset: " + ErrorMessage(status);
    }
  }

  {
    ReceiverFile receiver("deleteme.bin");
    if (receiver.GetConstructorStatus() != OKAY) {
      return "Error constructing ReceiverFile: " + ErrorMessage(receiver.GetConstructorStatus());
    }
    std::shared_ptr<CommandPacket> receiveCommandPacket;

    status = receiver.ReceiveCommandPacket(0.5, receiveCommandPacket);
    if (status != OKAY) {
      return "Error receiving CommandPacketReset: " + ErrorMessage(status);
    }
    if (receiveCommandPacket == nullptr) {
      return "Empty CommandPacketReset packet";
    }
    OpCode opCode;
    status = receiveCommandPacket->GetOpCode(opCode);
    if (status != OKAY) {
      return "Error checking CommandPacketReset opcode: " + ErrorMessage(status);
    }
    if (opCode != RESET) {
      return "Error receiving CommandPacketReset: wrong type";
    }
  }
  remove("deleteme.bin");

  // Try sending and receiving a StreamPacket.
  {
    SenderFile sender("deleteme.bin");
    if (sender.GetConstructorStatus() != OKAY) {
      return "Error constructing SenderFile: " + ErrorMessage(sender.GetConstructorStatus());
    }

    StreamPacket sendStreamPacket;
    status = sendStreamPacket.GetConstructorStatus();
    if (status != OKAY) {
      return "Error constructing StreamPacket: " + ErrorMessage(status);
    }
    status = sender.SendStreamPacket(sendStreamPacket);
    if (status != OKAY) {
      return "Error sending StreamPacket: " + ErrorMessage(status);
    }
  }

  {
    ReceiverFile receiver("deleteme.bin");
    if (receiver.GetConstructorStatus() != OKAY) {
      return "Error constructing ReceiverFile: " + ErrorMessage(receiver.GetConstructorStatus());
    }

    std::shared_ptr<StreamPacket> receiveStreamPacket;
    status = receiver.ReceiveStreamPacket(0.5, receiveStreamPacket);
    if (status != OKAY) {
      return "Error receiving StreamPacket: " + ErrorMessage(status);
    }
    if (receiveStreamPacket == nullptr) {
      return "Empty StreamPacket packet";
    }
  }
  remove("deleteme.bin");

  return "";
}

StreamWriter::StreamWriter(std::shared_ptr<asdp::Sender> sender,
    std::shared_ptr<asdp::Timer> timer,
    uint32_t maxPayloadSize)
  : m_constructorStatus(OKAY)
  , m_sender(sender)
  , m_timer(timer)
  , m_maxPayloadSize(maxPayloadSize)
  , m_sequenceNumber(0)
{
// Make sure we have a valid sender.
  if (m_sender == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
  if (m_sender->GetConstructorStatus() != OKAY) {
    m_constructorStatus = m_sender->GetConstructorStatus();
    return;
  }

  // Make sure we have a valid timer.
  if (m_timer == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }

  // Make sure we have a valid maximum packet size.
  if (m_maxPayloadSize == 0) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }

  // Construct the current packet.
  StreamPacket *packetPtr = new StreamPacket(m_maxPayloadSize, m_sequenceNumber);
  m_currentPacket.reset(packetPtr);
  if (m_currentPacket->GetConstructorStatus() != OKAY) {
    m_constructorStatus = m_currentPacket->GetConstructorStatus();
    return;
  }
}

StreamWriter::~StreamWriter()
{
  Flush();
}

Status StreamWriter::GetCurrentPacket(std::shared_ptr<asdp::StreamPacket>& packet) const
{
  if (m_currentPacket == nullptr) {
    if (m_constructorStatus != OKAY) {
      return m_constructorStatus;
    }
    return UNEXPECTED_INTERNAL_STATE;
  }
  packet = m_currentPacket;
  return OKAY;
}

Status StreamWriter::Flush()
{
  // Make sure we have a valid sender.
  if (m_sender == nullptr) {
    return UNEXPECTED_INTERNAL_STATE;
  }
  if (m_sender->GetConstructorStatus() != OKAY) {
    return m_sender->GetConstructorStatus();
  }

  // Make sure we have a valid timer.
  if (m_timer == nullptr) {
    return UNEXPECTED_INTERNAL_STATE;
  }

  // Make sure we have a valid maximum packet size.
  if (m_maxPayloadSize == 0) {
    return UNEXPECTED_INTERNAL_STATE;
  }

  // Make sure we have a valid current packet.
  if (m_currentPacket == nullptr) {
    if (m_constructorStatus != OKAY) {
      return m_constructorStatus;
    }
    return UNEXPECTED_INTERNAL_STATE;
  }

  // Set the time code in the current packet.
  Time timeCode;
  Status status = m_timer->GetCoreTime(timeCode);
  if (status != OKAY) {
    return status;
  }
  status = m_currentPacket->SetTimeCode(timeCode);
  if (status != OKAY) {
    return status;
  }

  // Send the packet.
  status = m_sender->SendStreamPacket(*m_currentPacket);
  if (status != OKAY) {
    return status;
  }

  // Construct a new packet with an incremented sequence number.
  m_sequenceNumber++;
  StreamPacket *packetPtr = new StreamPacket(m_maxPayloadSize, m_sequenceNumber);
  if (m_currentPacket->GetConstructorStatus() != OKAY) {
    return m_currentPacket->GetConstructorStatus();
  }
  m_currentPacket.reset(packetPtr);

  // Everything worked.
  return OKAY;
}

Status StreamWriter::GetConstructorStatus() const
{
  return m_constructorStatus;
}

std::string StreamWriter::Test()
{
  Status status;

  // Create a sender and receiver.
  ReceiverUDP receiver;
  if (receiver.GetConstructorStatus() != OKAY) {
    return "Error constructing ReceiverUDP: " + ErrorMessage(receiver.GetConstructorStatus());
  }
  uint16_t port;
  port = receiver.GetPort();
  std::shared_ptr<SenderUDP> sender = std::make_shared<SenderUDP>("localhost", port);
  if (sender->GetConstructorStatus() != OKAY) {
    return "Error constructing SenderUDP: " + ErrorMessage(sender->GetConstructorStatus());
  }

  // Create a Timer
  Timer *timerPtr = new Timer();
  std::shared_ptr<Timer> timer;
  timer.reset(timerPtr);

  // Create a StreamWriter.
  std::shared_ptr<StreamWriter> streamWriter = std::make_shared<StreamWriter>(sender, timer);
  if (streamWriter->GetConstructorStatus() != OKAY) {
    return "Error constructing StreamWriter: " + ErrorMessage(streamWriter->GetConstructorStatus());
  }

  // Try sending ten packets to make sure this works repeatedly
  Time lastTimeCode;
  status = timer->GetCoreTime(lastTimeCode);
  if (status != OKAY) {
    return "Error getting timecode: " + ErrorMessage(status);
  }
  status = timer->SetCoreOffset(lastTimeCode);
  if (status != OKAY) {
    return "Error setting timecode: " + ErrorMessage(status);
  }
  status = timer->GetCoreTime(lastTimeCode);
  if (status != OKAY) {
    return "Error getting timecode after setting it: " + ErrorMessage(status);
  }
  for (size_t i = 0; i < 10; i++) {
    // Make sure the time advances, so the new time will be larger than lastTimeCode.
    std::this_thread::sleep_for(std::chrono::microseconds(20));

      // Pack a message into the StreamWriter.
    std::shared_ptr<asdp::StreamPacket> packet;
    status = streamWriter->GetCurrentPacket(packet);
    if (status != OKAY) {
      return "Error getting current packet: " + ErrorMessage(status);
    }
    Time timeCode;
    timer->GetCoreTime(timeCode);
    uint32_t cameraID = 1;
    uint32_t cameraType = 2;
    uint32_t sensorWidth = 1920;
    uint32_t sensorHeight = 1080;
    float exposure = 0.001f;
    float gain = 1.0f;
    MessageFrameBegin message(*packet, timeCode, cameraID, cameraType, sensorWidth, sensorHeight, exposure, gain);
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageFrameBegin: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Send the packet.
    status = streamWriter->Flush();
    if (status != OKAY) {
      return "Error flushing StreamWriter: " + ErrorMessage(status);
    }

    // Make sure we received the packet.
    std::shared_ptr<StreamPacket> receiveStreamPacket;
    status = receiver.ReceiveStreamPacket(0.5, receiveStreamPacket);
    if (status != OKAY) {
      return "Error receiving StreamPacket: " + ErrorMessage(status);
    }
    if (receiveStreamPacket == nullptr) {
      return "Empty StreamPacket packet";
    }

    // Make sure the sequence number is correct.
    uint32_t sequenceNumber;
    status = receiveStreamPacket->GetSequenceNumber(sequenceNumber);
    if (status != OKAY) {
      return "Error getting sequence number: " + ErrorMessage(status);
    }
    if (sequenceNumber != i) {
      return "Unexpected sequence number: " + std::to_string(sequenceNumber);
    }

    // Make sure that the timecode is updating.
    Time receiveTimeCode;
    status = receiveStreamPacket->GetTimeCode(receiveTimeCode);
    if (status != OKAY) {
      return "Error getting timecode: " + ErrorMessage(status);
    }
    if (receiveTimeCode <= lastTimeCode) {
      return "Unexpected timecode: "
        + std::to_string(receiveTimeCode.seconds) + ":" + std::to_string(receiveTimeCode.microseconds)
        + " vs. " + std::to_string(lastTimeCode.seconds) + ":" + std::to_string(lastTimeCode.microseconds);
    }
    lastTimeCode = receiveTimeCode;
  }

  // Make sure we cannot receive another packet.
  std::shared_ptr<StreamPacket> receiveStreamPacket;
  status = receiver.ReceiveStreamPacket(0.05, receiveStreamPacket);
  if (status != TIMEOUT) {
    return "Received unexpected packet";
  }

  // Make sure we can set the maximum packet size in the constructor.
  {
    uint32_t maxPayloadSize = 1500 - 28;
    StreamWriter streamWriter2(sender, timer, maxPayloadSize);
    if (streamWriter2.GetConstructorStatus() != OKAY) {
      return "Error constructing large StreamWriter: " + ErrorMessage(streamWriter2.GetConstructorStatus());
    }
    std::shared_ptr<asdp::StreamPacket> packet;
    Status status = streamWriter2.GetCurrentPacket(packet);
    if (status != OKAY) {
      return "Error getting current packet for large StreamWriter: " + ErrorMessage(status);
    }
    if (packet->m_buffer->size() != maxPayloadSize) {
      return "Error constructing large StreamWriter: wrong maximum packet size";
    }
  }

  return "";
}

Core::Core(uint32_t maxPayloadSize)
  : m_constructorStatus(OKAY)
  , m_maxPayloadSize(maxPayloadSize)
{
  // Verify that the endianness on this architecture is little endian.
  std::vector<uint8_t> vals = { 1, 2, 3, 4 };
  uint32_t val = vals[0] + (vals[1] << 8) + (vals[2] << 16) + (vals[3] << 24);
  const uint8_t *ptr = reinterpret_cast<const uint8_t*>(&val);
  for (size_t i = 0; i < vals.size(); i++) {
    if (vals[i] != ptr[i]) {
      m_constructorStatus = INCORRECT_ENDIANNESS;
      return;
    }
  }

  // Verify that the size of a float is 4 bytes.
  if (sizeof(float) != 4) {
    m_constructorStatus = INCORRECT_FLOAT_SIZE;
    return;
  }

  // Construct our timer.
  Timer *timerPtr = new Timer();
  m_timer.reset(timerPtr);
}

Status Core::GetConstructorStatus() const
{
  return m_constructorStatus;
}

Status Core::GetMaxPayloadSize(size_t& value) const
{
  value = m_maxPayloadSize;
  return OKAY;
}

std::string Core::GetVersion()
{
  uint16_t major, minor, patch;
  UnpackVersion(VERSION, major, minor, patch);
  return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

Core::~Core() {}

CoreServer::CoreServer(uint32_t serial, std::string NICName, uint16_t sendPort, uint16_t listenPort, uint32_t maxPayloadSize)
  : Core(maxPayloadSize)
  , m_constructorStatus(OKAY)
  , m_sender(std::make_shared<SenderUDP>(NICName, sendPort))
  , m_receiver(std::make_shared<ReceiverUDP>(NICName, listenPort))
  , m_stopThread(false)
  , m_threadStatus(OKAY)
  , m_IP(0)
  , m_port(listenPort)
  , m_serial(serial)
{
  // Make sure we have a valid sender.
  if (m_sender == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
  if (m_sender->GetConstructorStatus() != OKAY) {
    m_constructorStatus = m_sender->GetConstructorStatus();
    return;
  }
  Status status = m_sender->GetIP(m_IP);
  if (status != OKAY) {
    m_constructorStatus = status;
    return;
  }

  // Make sure we have a valid receiver.
  if (m_receiver == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
  if (m_receiver->GetConstructorStatus() != OKAY) {
    m_constructorStatus = m_receiver->GetConstructorStatus();
    return;
  }

  // Make a thread to send discovery packets on, sending them to the broadcast address.
  m_discoveryThread = std::make_shared<std::thread>(&CoreServer::DiscoveryThread, this);
}

void CoreServer::DiscoveryThread()
{
  /// Store a status to notify the caller if this thread fails.
  m_threadStatus = OKAY;

  // Make a StreamWriter to send the discovery packets.
  if (m_sender->GetConstructorStatus() != OKAY) {
    m_threadStatus = UNEXPECTED_INTERNAL_STATE;
    return;
  }
  StreamWriter streamWriter(m_sender, m_timer, m_maxPayloadSize);

  // Twice a second, send Discovery packets to the broadcast address.
  while (!m_stopThread) {
    // Pack a message into the StreamWriter.
    std::shared_ptr<asdp::StreamPacket> packet;
    Status status = streamWriter.GetCurrentPacket(packet);
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    Time timeCode;
    status = m_timer->GetCoreTime(timeCode);
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    MessageDiscovery message(*packet, timeCode, m_IP, m_port, m_serial);
    status = message.GetConstructorStatus();
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    status = streamWriter.Flush();
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  m_threadStatus = THREAD_COMPLETED;
}

Status CoreServer::GetReceiver(std::shared_ptr<Receiver>& receiver) const
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }

  receiver = m_receiver;
  return OKAY;
}

Status CoreServer::GetDiscoveryThreadStatus(Status& threadStatus) const
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }

  threadStatus = m_threadStatus;
  return OKAY;
}

CoreServer::~CoreServer()
{
  // Stop our discovery thread and wait for it to join.
  m_stopThread = true;
  if ((m_discoveryThread != nullptr) && m_discoveryThread->joinable()) {
    m_discoveryThread->join();
  }
}

CoreClient::CoreClient(std::string NICName, uint32_t maxPayloadSize)
  : Core(maxPayloadSize)
  , m_constructorStatus(OKAY)
  , m_threadStatus(OKAY)
  , m_IP(0)
  , m_serial(0)
{
  // Open a socket on our NICName to receive Discovery packets.
  m_receiver = std::make_shared<ReceiverUDP>(NICName, 10102);
  if (m_receiver->GetConstructorStatus() != OKAY) {
    m_constructorStatus = m_receiver->GetConstructorStatus();
    return;
  }

  // Start the discovery thread to listen for servers.
  m_discoveryThread = std::make_shared<std::thread>(&CoreClient::DiscoveryThread, this);
}

void CoreClient::DiscoveryThread()
{
  /// Store a status to notify the caller if this thread fails.
  m_threadStatus = OKAY;

  // Listen for Discovery packets. When they arrive, make a description of
  // the server as a URL and add it and its other information to our list of servers
  // if it isn't already there.
  while (!m_stopThread) {
    std::shared_ptr<StreamPacket> packet;
    Status status = m_receiver->ReceiveStreamPacket(0.5, packet);

    // If we timed out, just try again.
    if (status == TIMEOUT) {
      continue;
    }

    // If we got an error, stop the thread and record why.
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }

    std::shared_ptr<Message> message = nullptr;
    status = packet->GetNextMessage(message);
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    MessageDiscovery discovery(*message);

    // Get the IP address, port, and serial number from the message.
    uint32_t IP;
    status = discovery.GetIP(IP);
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    uint16_t port;
    status = discovery.GetPort(port);
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    uint32_t serial;
    status = discovery.GetSerial(serial);
    if (status != OKAY) {
      m_threadStatus = status;
      return;
    }
    ServerInfo SI(IP, port, serial);

    // Lock the mutex so we can modify our list of servers.
    {
      std::lock_guard<std::mutex> lock(m_mutex);

      // See if we already have this server in our list.
      bool found = false;
      for (size_t i = 0; i < m_servers.size(); i++) {
        if (m_servers[i] == SI) {
          found = true;
          break;
        }
      }

      // If we don't have this server in our list, add it.
      if (!found) {
        m_servers.push_back(SI);
      }
    }
  }

  m_threadStatus = THREAD_COMPLETED;
}

Status CoreClient::GetDiscoveryThreadStatus(Status& threadStatus) const
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }

  threadStatus = m_threadStatus;
  return OKAY;
}

Status CoreClient::IdentifiedServers(std::vector<std::string>& servers) const
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::string> ret;
  for (size_t i = 0; i < m_servers.size(); i++) {
    ret.push_back(URLFromServerInfo(m_servers[i]));
  }
  servers = ret;
  return OKAY;
}

Status CoreClient::GetMyIP(uint32_t& IP) const
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }

  IP = m_IP;
  return OKAY;
}

Status CoreClient::GetServerSerialNumber(uint32_t& serial) const
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }
  if (m_sender == nullptr) {
    return NOT_CONNECTED;
  }

  serial = m_serial;
  return OKAY;
}

std::string CoreClient::URLFromServerInfo(ServerInfo serverInfo)
{
  uint32_t IP = serverInfo.IP;
  uint16_t port = serverInfo.port;
  std::string IPString = std::to_string((IP >> 24) & 0xff) + "." + std::to_string((IP >> 16) & 0xff) + "."
    + std::to_string((IP >> 8) & 0xff) + "." + std::to_string(IP & 0xff);
  return "socket://" + IPString + ":" + std::to_string(port);
}

Status CoreClient::ServerInfoFromURL(std::string URL, std::string& IP, uint16_t& port)
{
  // Make sure the URL starts with "socket://".
  if (URL.substr(0, 9) != "socket://") {
    return BAD_PARAMETER;
  }

  // Get the IP address and port.
  std::string IPString = URL.substr(9);
  size_t colonPos = IPString.find(':');
  if (colonPos == std::string::npos) {
    return BAD_PARAMETER;
  }
  IP = IPString.substr(0, colonPos);
  std::string portString = IPString.substr(colonPos + 1);
  port = 0;
  port = std::stoi(portString);

  return OKAY;
}

Status CoreClient::ConnectToServer(std::string serverURL)
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }

  // Parse the URL.
  std::string IP;
  uint16_t port;
  Status status = ServerInfoFromURL(serverURL, IP, port);
  if (status != OKAY) {
    return status;
  }

  // Make a sender to send packets to the server.
  m_sender = std::make_shared<SenderUDP>(IP, port);
  if (m_sender->GetConstructorStatus() != OKAY) {
    return m_sender->GetConstructorStatus();
  }

  // Record our IP address.
  status = m_sender->GetIP(m_IP);

  // Look up the server's serial number from our table based on the
  // IP address and port being connected to.
  bool found = false;
  for (size_t i = 0; i < m_servers.size(); i++) {
    if ((m_servers[i].IP == m_IP) && (m_servers[i].port == port)) {
      m_serial = m_servers[i].serial;
      found = true;
      break;
    }
  }
  if (!found) {
    return BAD_PARAMETER;
  }

  return OKAY;
}

Status CoreClient::SendCommandPacket(const CommandPacket& packet)
{
  if (m_constructorStatus != OKAY) {
    return m_constructorStatus;
  }
  if (m_sender == nullptr) {
    return NOT_CONNECTED;
  }

  return m_sender->SendCommandPacket(packet);
}

CoreClient::~CoreClient()
{
  // Stop our discovery thread and wait for it to join.
  m_stopThread = true;
  if ((m_discoveryThread != nullptr) && m_discoveryThread->joinable()) {
    m_discoveryThread->join();
  }
}

std::string CoreClient::Test()
{
  /// Test URLFromServerInfo on an IP gotten from localhost
  SenderUDP sender("localhost", 10102);
  if (sender.GetConstructorStatus() != OKAY) {
    return "Error constructing SenderUDP: " + ErrorMessage(sender.GetConstructorStatus());
  }
  uint32_t IP;
  Status status = sender.GetIP(IP);
  if (status != OKAY) {
    return "Error getting IP: " + ErrorMessage(status);
  }
  uint16_t port;
  status = sender.GetPort(port);
  if (status != OKAY) {
    return "Error getting port: " + ErrorMessage(status);
  }
  uint32_t serial = 123456789;
  ServerInfo serverInfo(IP, port, serial);
  std::string URL = URLFromServerInfo(serverInfo);
  if (URL != "socket://127.0.0.1:10102") {
    return "Error creating URL: " + URL;
  }

  // Test ServerInfoFromURL on the URL we just created.
  std::string IPString;
  uint16_t port2;
  status = ServerInfoFromURL(URL, IPString, port2);
  if (status != OKAY) {
    return "Error parsing URL: " + ErrorMessage(status);
  }
  if (IPString != "127.0.0.1") {
    return "Error parsing URL: " + IPString;
  }
  if (port2 != port) {
    return "Error parsing URL: " + std::to_string(port2);
  }

  // Start a CoreServer on Localhost.
  CoreServer coreServer(serial, "localhost");
  if (coreServer.GetConstructorStatus() != OKAY) {
    return "Error constructing CoreServer: " + ErrorMessage(coreServer.GetConstructorStatus());
  }
  Status threadStatus;
  status = coreServer.GetDiscoveryThreadStatus(threadStatus);
  if (status != OKAY) {
    return "Error getting server discovery thread status: " + ErrorMessage(status);
  }

  // Start a CoreClient on Localhost.
  CoreClient coreClient("localhost");
  if (coreClient.GetConstructorStatus() != OKAY) {
    return "Error constructing CoreClient: " + ErrorMessage(coreClient.GetConstructorStatus());
  }
  status = coreClient.GetDiscoveryThreadStatus(threadStatus);
  if (status != OKAY) {
    return "Error getting client discovery thread status: " + ErrorMessage(status);
  }

  // Listen for a server on the CoreClient, waiting 1 second to find one.
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  std::vector<std::string> servers;
  status = coreClient.IdentifiedServers(servers);
  if (status != OKAY) {
    return "Error getting identified servers: " + ErrorMessage(status);
  }
  if (servers.size() != 1) {
    return "Error getting identified servers: " + std::to_string(servers.size());
  }

  // Try sending a CommandPacketReset from the CoreClient to the CoreServer, which
  // should fail because it is not yet connected.
  CommandPacketReset commandPacketReset;
  status = coreClient.SendCommandPacket(commandPacketReset);
  if (status != NOT_CONNECTED) {
    return "Able to send CommandPacketReset when not connected.";
  }

  // Connect the CoreClient to the server.
  status = coreClient.ConnectToServer(servers[0]);
  if (status != OKAY) {
    return "Error connecting to server: " + ErrorMessage(status);
  }

  // Get the Server's receiver so we can receive packets from it.
  std::shared_ptr<Receiver> receiver;
  status = coreServer.GetReceiver(receiver);
  if (status != OKAY) {
    return "Error getting receiver: " + ErrorMessage(status);
  }

  // Send a CommandPacketReset from the CoreClient to the CoreServer.
  status = coreClient.SendCommandPacket(commandPacketReset);
  if (status != OKAY) {
    return "Error sending CommandPacketReset: " + ErrorMessage(status);
  }

  // Receive the CommandPacketReset on the CoreServer.
  std::shared_ptr<CommandPacket> receiveCommandPacket;
  status = receiver->ReceiveCommandPacket(0.5, receiveCommandPacket);
  if (status != OKAY) {
    return "Error receiving CommandPacketReset: " + ErrorMessage(status);
  }
  if (receiveCommandPacket == nullptr) {
    return "Empty CommandPacketReset packet";
  }
  OpCode opCode;
  status = receiveCommandPacket->GetOpCode(opCode);
  if (status != OKAY) {
    return "Error checking CommandPacketReset opcode: " + ErrorMessage(status);
  }
  if (opCode != RESET) {
    return "Error receiving CommandPacketReset: wrong type";
  }

  // Get the IP address and server serial number from the CoreClient.
  uint32_t IP2;
  status = coreClient.GetMyIP(IP2);
  if (status != OKAY) {
    return "Error getting IP: " + ErrorMessage(status);
  }
  if (IP2 != IP) {
    return "Error getting IP: " + std::to_string(IP2);
  }
  uint32_t serial2;
  status = coreClient.GetServerSerialNumber(serial2);
  if (status != OKAY) {
    return "Error getting serial number: " + ErrorMessage(status);
  }
  if (serial2 != serial) {
    return "Error getting serial number: " + std::to_string(serial2);
  }

  return "";
}

std::string asdp::Test()
{
  std::string ret;

  //-------------------------------------------------------------------
  // Test the error message function by checking some of its values and then
  // a value that is never set by the code to be sure it is unrecognized.
  if (ErrorMessage(OKAY) != "No error") {
    return "Error message for OKAY is incorrect: " + ErrorMessage(OKAY);
  }
  if (ErrorMessage(TIMEOUT) != "Timeout") {
    return "Error message for TIMEOUT is incorrect: " + ErrorMessage(TIMEOUT);
  }
  if (ErrorMessage(BAD_PARAMETER) != "Bad parameter") {
    return "Error message for BAD_PARAMETER is incorrect: " + ErrorMessage(BAD_PARAMETER);
  }
  if (ErrorMessage(HIGHEST_WARNING) != "Unrecognized error code: 1000") {
    return "Error message for HIGHEST_WARNING is incorrect: " + ErrorMessage(HIGHEST_WARNING);
  }

  //-------------------------------------------------------------------
  // Tests for Timer.
  ret = Timer::Test();
  if (ret.size() > 0) {
    return "Error testing Timer: " + ret;
  }

  //-------------------------------------------------------------------
  // Tests for Sender and Receiver subclasses.
  ret = ReceiverUDP::Test();
  if (ret.size() > 0) {
    return "Error testing Socket send/receive: " + ret;
  }
  ret = ReceiverFile::Test();
  if (ret.size() > 0) {
    return "Error testing File send/receive: " + ret;
  }

  //-------------------------------------------------------------------
  // Tests for BasicPacket and its derived classes.
  ret = BasicPacket::Test();
  if (ret.size() > 0) {
    return "Error testing BasicPacket: " + ret;
  }
  ret = CommandPacket::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacket: " + ret;
  }
  ret = CommandPacketReset::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketReset: " + ret;
  }
  ret = CommandPacketCancelAllStreams::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketCancelAllStreams: " + ret;
  }
  ret = CommandPacketStartRecording::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStartRecording: " + ret;
  }
  ret = CommandPacketStopRecording::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStopRecording: " + ret;
  }
  ret = CommandPacketStartReplay::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStartReplay: " + ret;
  }
  ret = CommandPacketPauseReplay::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketPauseReplay: " + ret;
  }
  ret = CommandPacketResumeReplay::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketResumeReplay: " + ret;
  }
  ret = CommandPacketStopReplay::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStopReplay: " + ret;
  }
  ret = CommandPacketSetStartUpRecordingState::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketSetStartUpRecordingState: " + ret;
  }
  ret = CommandPacketKeepaliveInterval::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketKeepaliveInterval: " + ret;
  }
  ret = CommandPacketStreamState::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStreamState: " + ret;
  }
  ret = CommandPacketCancelState::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketCancelState: " + ret;
  }
  ret = CommandPacketConfigureTrigger::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketConfigureTrigger: " + ret;
  }
  ret = CommandPacketSoftwareTrigger::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketSoftwareTrigger: " + ret;
  }
  ret = CommandPacketStreamEvents::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStreamEvents: " + ret;
  }
  ret = CommandPacketCancelEvents::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketCancelEvents: " + ret;
  }
  ret = CommandPacketStreamSubregions::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStreamSubregions: " + ret;
  }
  ret = CommandPacketCancelSubregions::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketCancelSubregions: " + ret;
  }
  ret = CommandPacketEraseAllStoredStreams::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketEraseAllStoredStreams: " + ret;
  }
  ret = CommandPacketStreamStoredList::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketEraseAllStoredStreams: " + ret;
  }
  ret = CommandPacketEraseStoredStream::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketEraseStoredStream: " + ret;
  }
  ret = CommandPacketStreamTemperatures::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStreamTemperatures: " + ret;
  }
  ret = CommandPacketCancelTemperatures::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketCancelTemperatures: " + ret;
  }
  ret = CommandPacketStreamPoses::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStreamPoses: " + ret;
  }
  ret = CommandPacketCancelPoses::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketCancelPoses: " + ret;
  }

  //-------------------------------------------------------------------
  // Tests for Message and its derived classes.
  ret = Message::Test();
  if (ret.size() > 0) {
    return "Error testing Message: " + ret;
  }
  ret = MessageDiscovery::Test();
  if (ret.size() > 0) {
    return "Error testing MessageDiscovery: " + ret;
  }
  ret = MessageFrameBegin::Test();
  if (ret.size() > 0) {
    return "Error testing MessageFrameBegin: " + ret;
  }
  ret = MessageFrameData::Test();
  if (ret.size() > 0) {
    return "Error testing MessageFrameData: " + ret;
  }
  ret = MessageFrameEnd::Test();
  if (ret.size() > 0) {
    return "Error testing MessageFrameEnd: " + ret;
  }

  //-------------------------------------------------------------------
  // Tests for StreamWriter and its derived classes.
  ret = StreamWriter::Test();
  if (ret.size() > 0) {
    return "Error testing StreamWriter: " + ret;
  }

  //-------------------------------------------------------------------
  // Tests for Core and its derived classes.
  ret = CoreClient::Test();
  if (ret.size() > 0) {
    return "Error testing Core classes: " + ret;
  }

  /// @todo Run tests.
  return "@todo Implement all tests.";
}
