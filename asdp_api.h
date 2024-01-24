/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

/**
 * @file asdp_api.h
 * @brief Apache Strap-Down Pilotage Core C++ API exposed as a static library.
 *
* @author Russell Taylor.
* @date January 22, 2024.
*/

//----------------------------------------------------------------------------------------
// Include the configuration file that defines the import or export for DLLs on Windows.
// These are left undefined on other platforms.  The CMake system adds a definition that
// causes export when the library is built and import for other applications.
#include <cstdint>
#include <string>
#include <chrono>
#include <memory>
#include <vector>

namespace asdp {

//---------------------------------------------------------------------------
/// @brief Status enumeration, returned by API functions.
enum Status {
  /// @brief All is okay.
  OKAY                          = 0,

  /// @brief A timeout was exceeded (not an error).
  TIMEOUT                       = 1,

  /// @brief Can be used to see if the return was a system error.
  HIGHEST_WARNING               = 1000,

  /// @brief Error: Bad parameter passed to function.
  BAD_PARAMETER                  = 1001,
  /// @brief Error: Out of memory when trying to execute function.
  OUT_OF_MEMORY                 = 1002,
  /// @brief Error: Function not yet implemented.
  NOT_IMPLEMENTED               = 1003,
  /// @brief Error: Attempting to delete an object failed.
  DELETION_FAILED               = 1004,
  /// @brief Error: Internal failure: calling an object with a NULL object pointer.
  NULL_OBJECT_POINTER           = 1005,
  /// @brief Error: Internal failure: Exception inside the implementation.
  INTERNAL_EXCEPTION            = 1006,
  /// @brief Error: Socket error.
  SOCKET_ERROR                  = 1007,
  /// @brief Error: Attempting to read past the memory available in an object.
  READ_PAST_END                 = 1008,
  /// @brief Error: Bad magic cookie in packet.
  BAD_COOKIE                    = 1009
};

/// @brief Helper function to return a descriptive error message based on a status value.
/// @param [in] status Status value returned from an API call.
/// @return String describing the status condition.
std::string ErrorMessage(Status status);

//---------------------------------------------------------------------------
/// @brief Operation codes for command packets.
enum OpCode {
  RESET                         = 0,
  CANCEL_ALL_STREAMS            = 1,
  START_RECORDING               = 2,
  CANCEL_RECORDING              = 3,
  START_REPLAY                  = 4,
  PAUSE_REPLAY                  = 5,
  RESUME_REPLAY                 = 6,
  CANCEL_REPLAY                 = 7,
  SET_START_UP_RECORDING_STATE  = 8,
  SET_KEEPALIVE                 = 9,
  STREAM_STATE                  = 10,
  CANCEL_STATE                  = 11,
  CONFIGURE_TRIGGER             = 10000,
  SOFTWARE_TRIGGER              = 10001,
  STREAM_EVENTS                 = 10002,
  CANCEL_EVENTS                 = 10003,
  STREAM_SUBREGIONS             = 20000,
  CANCEL_SUBREGIONS             = 20001,
  ERASE_ALL_STORED_STREAMS      = 30000,
  LIST_STORED_STREAMS           = 30001,
  ERASE_STORED_STREAM           = 30002,
  STREAM_TEMPERATURES           = 40000,
  CANCEL_TEMPERATURES           = 40001,
  STREAM_POSES                  = 50000,
  CANCEL_POSES                  = 50001
};

//---------------------------------------------------------------------------
/// @brief Message IDs for stream packets.
enum Message {
  DISCOVERY                     = 0,
  STATE                         = 1,
  EVENT                         = 10000,
  FRAME_BEGIN                   = 20000,
  FRAME_DATA                    = 20001,
  FRAME_END                     = 20002,
  LIST_STORED_PARTIAL           = 30000,
  LIST_STORED_END               = 30001,
  TEMPERATURE                   = 40000,
  POSE                          = 50000
};

//---------------------------------------------------------------------------
/// @brief Event IDs.
enum Event {
  INVALID_OPERATION             = 256,
  CLOCK_SYNC                    = 768,
  START_OF_REPLAY               = 769,
  END_OF_REPLAY                 = 770
};

//---------------------------------------------------------------------------
/// @brief Class to store seconds and microseconds since the epoch, matching Linux gettimeofday().

class Time {
public:
  uint32_t seconds;         ///< Seconds portion of time since the start of the epoch.
  uint32_t microseconds;    ///< Microseconds portion of time since the start of the epoch.
};

//---------------------------------------------------------------------------
/// @brief Class to report the time on the Core based on local time.

class Timer {
public:
  virtual ~Timer();

  /// @brief Get the Core time corresponding to the specified local time.
  /// @param [out] core_time The Core time corresponding to the specified local time.
  /// @param [in] local_time The local time to convert. Defaults to the current time.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCoreTime(Time& core_time,
    const std::chrono::system_clock::time_point local_time = std::chrono::system_clock::now()) const;

protected:
  Timer();
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;

  /// @brief Set the offset between Core time and local time.
  Status SetCoreOffset(Time offset);

  friend class Core;
};

//---------------------------------------------------------------------------
/// @brief Base packet type, not used by client code, supports code common to CommandPacket and StreamPacket.
///
/// The GetConstructorStatus() function can be used to determine if the constructor was successful
/// in this class and in derived classes.

class BasicPacket {
public:

  /// @todo Think about how to do reading into a buffer pool and passing them to the
  /// packets for both command packets and stream packets. Do we use a specialized
  /// shared_ptr that returns things to a free pool; taking ownership rather than
  /// creating them?

  /// @brief Return the status of the constructor.
  Status GetConstructorStatus() const;

  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  virtual ~BasicPacket();

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  // Remove the default constructor and copy operators.
  BasicPacket() = delete;
  BasicPacket(const BasicPacket&) = delete;
  BasicPacket& operator=(const BasicPacket&) = delete;
  BasicPacket(BasicPacket&&) = delete;
  BasicPacket& operator=(BasicPacket&&) = delete;

  /// @brief Construct a basic packet with its own buffer and fill its values in.
  /// @param [in] extraHeaderSize Size of the header portion of the packet beyond the basic header size.
  /// @param [in] parameterSize Size of the parameter portion of the packet after the base size.
  /// @param [in] code Operation code for the packet.
  BasicPacket(uint32_t extraHeaderSize, uint32_t parameterSize);

  /// @brief Construct a basic packet that shares a buffer with another packet.
  ///
  /// This us used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  BasicPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer);

  std::shared_ptr<std::vector<uint8_t>> m_buffer;  ///< Buffer containing the packet.
  Status m_constructorStatus = OKAY;               ///< Status of the constructor.

  friend class CommandPacket;
  friend class StreamPacket;
};

//---------------------------------------------------------------------------
/// @brief Command packet, subclass constructed and sent by clients and received and parsed by server.
///
/// The command packet is a UDP packet sent by a client to a server.  It contains an operation code
/// and optional parameters.  The server receives the packet, parses it, and executes the operation.
/// These packets are sent using the SocketSender class and received using the SocketReceiver class.
/// They are created on a client by constructing a subclass.  They are parsed on a server from a
/// buffer by checking the operation code and then typecasting to the appropriate subclass.
///
/// Subclasses are listed below.

class CommandPacket : public BasicPacket {
public:

  /// @brief Get the operation code for this command packet.
  /// @param [out] opCode The operation code for this command packet.
  /// @return OKAY if successful, otherwise an error code.
  Status GetOpCode(OpCode& opCode) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  // Remove the default constructor and copy operators.
  CommandPacket() = delete;
  CommandPacket(const CommandPacket&) = delete;
  CommandPacket& operator=(const CommandPacket&) = delete;
  CommandPacket(CommandPacket&&) = delete;
  CommandPacket& operator=(CommandPacket&&) = delete;

  /// @brief Construct a command packet with its own buffer and fill its values in.
  /// @param [in] parameterSize Size of the parameter portion of the packet.
  /// @param [in] code Operation code for the packet.
  CommandPacket(uint32_t parameterSize, OpCode code);

  /// @brief Construct a command packet that shares a buffer with another packet.
  ///
  /// This us used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  CommandPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer);

  /// @todo See if we need this once we've refactored
  friend class CommandPacketReset;
  friend class CommandPacketCancelAllStreams;
  /// @todo Finish the rest of the subclasses, here and below, once we've finished a full example.
  friend class CommandPacketStreamSubregions;
};

/// @brief Command packet to reset the system
class CommandPacketReset : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the Reset opcode.
  CommandPacketReset();

  /// @brief Type-cast a base CommandPacket into a Reset packet, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketReset(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel all streams on a subnet.
class CommandPacketCancelAllStreams : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the CancelAllStreams opcode.
  /// @param [in] subnet Subnet to cancel all streams on.
  CommandPacketCancelAllStreams(uint32_t subnet);

  /// @brief Type-cast a base CommandPacket into a CancelAllStreams packet, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelAllStreams(CommandPacket& basePacket);

  /// @brief Get the subnet to cancel all streams on.
  /// @param [out] subnet Subnet to cancel all streams on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSubnet(uint32_t& subnet) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Structure describing a subregion.
struct SubregionDescription {
  uint32_t cameraID;    ///< Camera ID the region is from
  uint32_t skipFrames;  ///< Number of frames to skip between frames in the subregion
  uint32_t left;        ///< Left side of the subregion
  uint32_t top;         ///< Top side of the subregion
  uint32_t right;       ///< Right side of the subregion
  uint32_t bottom;      ///< Bottom side of the subregion

  /// @brief Equality operator.
  bool operator ==(const SubregionDescription& other) const {
    return cameraID == other.cameraID &&
      skipFrames == other.skipFrames &&
      left == other.left &&
      top == other.top &&
      right == other.right &&
      bottom == other.bottom;
  };
  /// @brief Inequality operator.
  bool operator !=(const SubregionDescription& other) const {
    return !(*this == other);
  };
};

/// @brief Command packet to stream subregions.
class CommandPacketStreamSubregions : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the StreamSubregions opcode.
  /// @param [in] IP IP address of the system to stream to.
  /// @param [in] port Port number of the system to stream to.
  /// @param [in] regions Vector of subregions to stream.
  CommandPacketStreamSubregions(uint32_t IP, uint16_t port, std::vector<SubregionDescription> const &regions);

  /// @brief Type-cast a base CommandPacket into a StreamSubregions packet, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStreamSubregions(CommandPacket& basePacket);

  /// @brief Get the IP to stream to.
  /// @param [out] IP IP to stream subregions on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& subnet) const;

  /// @brief Get the port to stream to.
  /// @param [out] port Port to stream.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& regionID) const;

  /// @brief Get the subregion ID to stream.
  /// @param [out] subregionID Subregion ID to stream.
  /// @return OKAY if successful, otherwise an error code.
  Status GetRegionDescriptions(std::vector<SubregionDescription> & regions) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @todo StreamPacket and CommandPacket should share a common Packet base class.
/// The StreamPacket should allocate a maximum sized buffer and then resize as needed
/// when adding messages.

/// @todo Messages should each be tied to a StreamPacket, keeping a shared pointer to its buffer
/// to ensure that the buffer is not deleted out from under us.  It will also store an
/// offset into the buffer to the start of the message, and a length of the message.
/// It should always be read-only after construction and it should only be constructable
/// with a StreamPacket passed to it.

//---------------------------------------------------------------------------
/// @brief Class used to send UDP packets on a socket. Used internally by CoreClient and CoreServer.

class SocketSender {
public:
  /// @brief Construct a SocketSender object.
  /// @param [in] timer Pointer to a Timer object to be used to determine the current time
  /// when sending a packet.
  SocketSender(std::shared_ptr<Timer> timer);
  virtual ~SocketSender();

  /// @brief Send a UDP packet.
  /// @param [in] buffer Pointer to the buffer containing the packet to send.
  /// @param [in] length Length of the packet to send.
  /// @return OKAY if successful, otherwise an error code.
  Status Send(const void* buffer, size_t length);

protected:
  std::shared_ptr<Timer> m_timer; ///< Used to determine the current time when sending a packet.
};

//---------------------------------------------------------------------------
/// @brief Class used to receive UDP packets on a socket.

class SocketReceiver {
public:
  /// @brief Construct a SocketReceiver object.
  /// @param [in] IP address to listen on.
  /// @param [in] port Port number to listen on (default of 0 means any available port).
  SocketReceiver(uint32_t IP, uint16_t port = 0);

  /// @brief Get the port associated with this receiver.
  /// @return The port associated with this receiver.
  uint16_t GetPort() const;

  virtual ~SocketReceiver();
protected:
};

//---------------------------------------------------------------------------
/// @brief Core class, which is the derived class for both a client and server.

class Core {
public:
  /// @brief Get the version of the Core API being used.
  /// @return The version of the Core API.
  static std::string GetVersion();

  /// @brief Get the maximum size of the payload that can be sent in a UDP packet using this Core.
  /// @param [out] value The maximum size of the payload that can be sent in a UDP packet using this Core.
  /// @return OKAY if successful, otherwise an error code.
  Status GetMaxPayloadSize(size_t& value) const;

  /// @brief Set the maximum size of the payload that can be sent in a UDP packet using this Core.
  /// @param [in] value The maximum size of the payload that can be sent in a UDP packet using this Core.
  /// @return OKAY if successful, otherwise an error code.
  Status SetMaxPayloadSize(size_t value);

  /// @brief Virtual destructor.
  virtual ~Core();

protected:
  Core();
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  Core(Core&&) = delete;
  Core& operator=(Core&&) = delete;
};

//---------------------------------------------------------------------------
/// @brief Test function that verifies that all classes and functions are working.
/// @return Empty string if successful, otherwise descriptive error message.
std::string Test();

} // namespace asdp