/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// Include the C++ class definitions, which we will use here to implement the
// C functions.
#include "asdp_api.h"
#include <string.h>   // For memcpy

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
  case BAD_COOKIE:
    return "Bad magic cookie in packet";

  default:
    return "Unrecognized error code";
  }
}

//----------------------------------------------------------------------------
// API functions

static const unsigned char MAGIC_COOKIE[4] = { 'A', 'S', 'D', 'P' };
static const unsigned char VERSION[4] = { 1, 0, 0, 0 };

static const uint32_t COMMAND_PACKET_BASE_SIZE = 4 * 5;

CommandPacket::CommandPacket(uint32_t parameterSize, OpCode code)
  : m_buffer(std::make_shared<std::vector<uint8_t>>(COMMAND_PACKET_BASE_SIZE + parameterSize))
{
  // Pack our header and operation code.
  unsigned char *bufPtr = m_buffer->data();
  memcpy(bufPtr, MAGIC_COOKIE, sizeof(MAGIC_COOKIE)); bufPtr += sizeof(MAGIC_COOKIE);
  memcpy(bufPtr, VERSION, sizeof(VERSION)); bufPtr += sizeof(VERSION);
  uint32_t totalSize = COMMAND_PACKET_BASE_SIZE + parameterSize;
  memcpy(bufPtr, &totalSize, sizeof(totalSize)); bufPtr += sizeof(totalSize);
  const uint32_t header_size = 4 * 4;
  memcpy(bufPtr, &header_size, sizeof(header_size)); bufPtr += sizeof(header_size);
  memcpy(bufPtr, &code, sizeof(code)); bufPtr += sizeof(code);
}

Status CommandPacket::GetConstructorStatus() const
{
  return m_constructorStatus;
}

CommandPacket::CommandPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer)
  : m_buffer(existingBuffer)
{
  // Make sure the buffer is of the correct size.
  if (m_buffer->size() < COMMAND_PACKET_BASE_SIZE) {
    m_constructorStatus = BAD_PARAMETER;
    return;
  }
  // Check the magic cookie.
  if (!std::equal(m_buffer->begin(), m_buffer->begin() + 4, "ASDP")) {
    m_constructorStatus = BAD_COOKIE;
    return;
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
  memcpy(&opCodeOffset, m_buffer->data() + 4 * 3, sizeof(opCodeOffset));
  if (opCodeOffset + sizeof(opCode) > m_buffer->size()) {
    return READ_PAST_END;
  }
  memcpy(&opCode, m_buffer->data() + opCodeOffset, sizeof(opCode));
  return OKAY;
}

CommandPacket::~CommandPacket()
{
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
  // Tests for CommandPacket and its derived classes.
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
  /// @todo

  //-------------------------------------------------------------------
  // Tests for Core and its derived classes.
  /// @todo


  /// @todo Run tests.
  return "@todo Implement all tests.";
}
