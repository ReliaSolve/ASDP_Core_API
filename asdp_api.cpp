/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include "asdp_api.h"
#include <string.h>   // For memcpy
#include <iostream>   // For debugging

using namespace asdp;

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

//----------------------------------------------------------------------------
/// Error handling.

std::string asdp::ErrorMessage(Status status)
{
  switch (status) {
  case OKAY:
    return "No error";

  case TIMEOUT:
    return "Timeout";

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
  case SOCKET_ERROR:
    return "Socket error";
  case READ_PAST_END:
    return "Attempt to read past end of buffer";
  case BAD_COOKIE:
    return "Bad magic cookie in packet";
  case WRITE_PAST_END:
    return "Attempt to write past end of buffer";

  default:
    return "Unrecognized error code";
  }
}

//----------------------------------------------------------------------------
// API functions

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
    // Construct a reset command packet and verify that we can read its opcode.
    CommandPacketReset resetPacket;
    if (resetPacket.GetConstructorStatus() != OKAY) {
      return "Error constructing reset packet: " + ErrorMessage(resetPacket.GetConstructorStatus());
    }
    OpCode opCode;
    Status status = resetPacket.GetOpCode(opCode);
    if (status != OKAY) {
      return "Error getting opcode from reset packet: " + ErrorMessage(status);
    }
    if (opCode != RESET) {
      return "Error getting opcode from reset packet: opcode is not RESET";
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
    SubregionDescription region1 = { 1, 2, 3, 4, 5, 6 };
    SubregionDescription region2 = { 7, 8, 9,10,11,12 };
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

StreamPacket::StreamPacket(uint32_t bufferMaxSize, uint32_t sequenceNumber, Time timeCode)
  : BasicPacket(3 * sizeof(uint32_t), bufferMaxSize - 3 * sizeof(uint32_t))
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
  {
    // Construct a message with no parameters and check its length, time and type.
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
  }

  return "";
}

MessageFrameEnd::MessageFrameEnd(StreamPacket& packet, Time timeCode)
  : Message(packet, 0, timeCode, FRAME_END)
{
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

std::string MessageFrameEnd::Test()
{
  {
    // Construct a message with no parameters and check its length, time and type.
    StreamPacket packet;
    if (packet.GetConstructorStatus() != OKAY) {
      return "Error constructing stream packet for MessageFrameEnd test: " + ErrorMessage(packet.GetConstructorStatus());
    }
    Time timeCode = { 1234, 5678 };
    MessageFrameEnd message(packet, timeCode);
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageFrameEnd: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Check the length of the packet including the message to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking message size for MessageFrameEnd test: " + ErrorMessage(status);
    }
    if (totalLength != STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE) {
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

MessageFrameData::MessageFrameData( StreamPacket& packet, Time timeCode,
                                    uint16_t left, uint16_t top, uint16_t right, uint16_t bottom,
                                    uint8_t *data)
  : Message(packet,
            // The size of the message is the size of the parameters plus the size of the data.
            // We pad the size of the data to a multiple of 4 bytes.
            sizeof(left) + sizeof(top) + sizeof(right) + sizeof(bottom) +
              PaddedSize(2*(right-left+1)*(bottom-top+1)),
            timeCode, FRAME_DATA)
{
  // See if our subobject failed. If so, we're done.
  if (m_constructorStatus != OKAY) {
    return;
  }

  // Check our parameters.
  if (data == nullptr) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }

  // Pack our parameters.
  unsigned char *bufPtr = m_buffer->data() + m_offset + MESSAGE_BASE_SIZE;
  memcpy(bufPtr, &left, sizeof(left)); bufPtr += sizeof(left);
  memcpy(bufPtr, &top, sizeof(top)); bufPtr += sizeof(top);
  memcpy(bufPtr, &right, sizeof(right)); bufPtr += sizeof(right);
  memcpy(bufPtr, &bottom, sizeof(bottom)); bufPtr += sizeof(bottom);
  // We only copy the actual data.  We skip the size including padding.
  size_t dataSize = 2*(right-left+1)*(bottom-top+1);
  memcpy(bufPtr, data, dataSize); bufPtr += PaddedSize(dataSize);
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

Status MessageFrameData::GetLeft(uint16_t& left) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE;
  if (m_buffer->size() < myOffset + sizeof(left)) {
    return READ_PAST_END;
  }
  memcpy(&left, m_buffer->data() + myOffset, sizeof(left));
  return OKAY;
}

Status MessageFrameData::GetTop(uint16_t& top) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + sizeof(uint16_t);
  if (m_buffer->size() < myOffset + sizeof(top)) {
    return READ_PAST_END;
  }
  memcpy(&top, m_buffer->data() + myOffset, sizeof(top));
  return OKAY;
}

Status MessageFrameData::GetRight(uint16_t& right) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + 2 * sizeof(uint16_t);
  if (m_buffer->size() < myOffset + sizeof(right)) {
    return READ_PAST_END;
  }
  memcpy(&right, m_buffer->data() + myOffset, sizeof(right));
  return OKAY;
}

Status MessageFrameData::GetBottom(uint16_t& bottom) const
{
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + 3 * sizeof(uint16_t);
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
  uint32_t myOffset = m_offset + MESSAGE_BASE_SIZE + 4 * sizeof(uint16_t);
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
    uint16_t left = 0, top = 0, right = 99, bottom = 99;
    std::vector<uint8_t> data(2*(right-left+1) * (bottom-top+1), 0);
    MessageFrameData badMessage(packet, timeCode, left, top, right, bottom, data.data());
    if (badMessage.GetConstructorStatus() != WRITE_PAST_END) {
      return "Unexpected success constructing too-large MessageFrameData";
    }

    // Now add a reasonable-sized message
    bottom = 0;
    MessageFrameData message(packet, timeCode, left, top, right, bottom, data.data());
    if (message.GetConstructorStatus() != OKAY) {
      return "Error constructing MessageFrameData: " + ErrorMessage(message.GetConstructorStatus());
    }

    // Check the length of the packet including the message to make sure it matches expectation.
    uint32_t totalLength;
    Status status = packet.GetTotalLength(totalLength);
    if (status != OKAY) {
      return "Error checking message size for MessageFrameData test: " + ErrorMessage(status);
    }
    uint32_t expectedLength = STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + 4 * sizeof(uint16_t) +
      PaddedSize(2*(right-left+1)*(bottom-top+1));
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
    uint16_t rLeft, rTop, rRight, rBottom;
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
    uint8_t *rData;
    status = message.GetDataPointer(&rData);
    if (status != OKAY) {
      return "Error getting data pointer from message for MessageFrameData test: " + ErrorMessage(status);
    }
    uint32_t expectedOffset = STREAM_PACKET_BASE_SIZE + MESSAGE_BASE_SIZE + 4 * sizeof(uint16_t);
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
  if (ErrorMessage(HIGHEST_WARNING) != "Unrecognized error code") {
    return "Error message for HIGHEST_WARNING is incorrect: " + ErrorMessage(HIGHEST_WARNING);
  }

  //-------------------------------------------------------------------
  // Tests for Timer and its derived classes.
  /// @todo

  //-------------------------------------------------------------------
  // Tests for SocketSender and its derived classes.
  /// @todo

  //-------------------------------------------------------------------
  // Tests for SocketReceiver and its derived classes.
  /// @todo

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
  ret = CommandPacketStreamSubregions::Test();
  if (ret.size() > 0) {
    return "Error testing CommandPacketStreamSubregions: " + ret;
  }

  //-------------------------------------------------------------------
  // Tests for Message and its derived classes.
  ret = Message::Test();
  if (ret.size() > 0) {
    return "Error testing Message: " + ret;
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
  // Tests for Core and its derived classes.
  /// @todo


  /// @todo Run tests.
  return "@todo Implement all tests.";
}
