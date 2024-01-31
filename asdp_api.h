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
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>

namespace asdp {

//---------------------------------------------------------------------------
/// @brief Status enumeration, returned by API functions.
enum Status {
  /// @brief All is okay.
  OKAY                          = 0,

  /// @brief A timeout was exceeded (not an error).
  TIMEOUT                       = 1,

  /// @brief A thread has completed its work (not an error).
  THREAD_COMPLETED              = 2,

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
  SOCKET_FAILURE                = 1007,
  /// @brief Error: Attempting to read past the memory available in an object.
  READ_PAST_END                 = 1008,
  /// @brief Error: Bad magic cookie in packet.
  BAD_COOKIE                    = 1009,
  /// @brief Error: Attempting to write past the memory available in an object.
  WRITE_PAST_END                = 1010,
  /// @brief Error: Buffer too small to receive packet or other issue.
  SOCKET_READ_FAILURE           = 1011,
  /// @brief File error
  FILE_FAILURE                  = 1012,
  /// @brief Error: Unexpected internal state.
  UNEXPECTED_INTERNAL_STATE     = 1013,
  /// @brief The endianness on this architecture is incorrect
  INCORRECT_ENDIANNESS          = 1014,
  /// @brief Error: The object is not connected to a counterpart objects.
  NOT_CONNECTED                 = 1015
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
  SET_KEEPALIVE_INTERVAL        = 9,
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
enum MessageID {
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
enum EventID {
  INVALID_OPERATION             = 256,
  CLOCK_SYNC                    = 768,
  START_OF_REPLAY               = 769,
  END_OF_REPLAY                 = 770
};

//---------------------------------------------------------------------------
/// @brief Class to store seconds and microseconds since the epoch, matching Linux gettimeofday().

struct Time {
public:
  uint32_t seconds;         ///< Seconds portion of time since the start of the epoch.
  uint32_t microseconds;    ///< Microseconds portion of time since the start of the epoch.

  /// @brief Equality operator.
  bool operator ==(const Time& other) const {
    return seconds == other.seconds && microseconds == other.microseconds;
  };

  /// @brief Inequality operator.
  bool operator !=(const Time& other) const {
    return !(*this == other);
  };

  /// @brief Less-than operator.
  bool operator <(const Time& other) const {
    return seconds < other.seconds || (seconds == other.seconds && microseconds < other.microseconds);
  };

  /// @brief Less-than-or-equal operator.
  bool operator <=(const Time& other) const {
    return seconds < other.seconds || (seconds == other.seconds && microseconds <= other.microseconds);
  };

  /// @brief Greater-than operator.
  bool operator >(const Time& other) const {
    return seconds > other.seconds || (seconds == other.seconds && microseconds > other.microseconds);
  };

  /// @brief Greater-than-or-equal operator.
  bool operator >=(const Time& other) const {
    return seconds > other.seconds || (seconds == other.seconds && microseconds >= other.microseconds);
  };

  /// @brief Add operator.
  Time operator +(const Time& other) const {
    Time result = { seconds + other.seconds, microseconds + other.microseconds };
    if (result.microseconds >= 1000000) {
      result.seconds++;
      result.microseconds -= 1000000;
    }
    return result;
  };

  /// @brief Subtract operator.
  Time operator -(const Time& other) const {
    Time result = { seconds - other.seconds, microseconds };
    if (result.microseconds < other.microseconds) {
      result.seconds--;
      result.microseconds += 1000000;
    }
    result.microseconds -= other.microseconds;
    return result;
  };

  /// @brief Add-assign operator.
  Time& operator +=(const Time& other) {
    seconds += other.seconds;
    microseconds += other.microseconds;
    if (microseconds >= 1000000) {
      seconds++;
      microseconds -= 1000000;
    }
    return *this;
  };

  /// @brief Subtract-assign operator.
  Time& operator -=(const Time& other) {
    seconds -= other.seconds;
    if (microseconds < other.microseconds) {
      seconds--;
      microseconds += 1000000;
    }
    microseconds -= other.microseconds;
    return *this;
  };
};

//---------------------------------------------------------------------------
/// @brief Class to report the time on the Core based on local time.  Must be constructed by Core.

class Timer {
public:
  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  virtual ~Timer();

  /// @brief Get the Core time corresponding to the specified local steady_clock time.
  /// @param [out] core_time The Core time corresponding to the specified local time.
  /// @param [in] local_time The local time to convert. Defaults to the current time.
  /// Note that the steady_clock might bear no relationship to wall-clock time. It is
  /// guaranteed to have uniform ticks, but the client is responsible for converting
  /// to system_clock if that is desired (note that system_clock may vary in rate and
  /// may have discontinuous jumps forwards and backwards).
  /// @return OKAY if successful, otherwise an error code.
  Status GetCoreTime(Time& core_time,
    const std::chrono::steady_clock::time_point local_time = std::chrono::steady_clock::now()) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  Timer();
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;

  /// @brief Set the offset between Core time and local time.
  /// @param [in] offset Offset between Core time and local time.
  /// @return OKAY if successful, otherwise an error code.
  /// Returns BAD_PARAMETER if the offset is too large.
  Status SetCoreOffset(Time offset);

  Time m_coreOffset;  ///< Offset between Core time and local time.

  friend class StreamWriter;  // So it can implement Test().
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

  /// @brief Return the total length of the packet.
  Status GetTotalLength(uint32_t &totalLength) const;

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
  BasicPacket(uint32_t extraHeaderSize, uint32_t parameterSize);

  /// @brief Construct a basic packet that shares a buffer with another packet.
  ///
  /// This is used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  BasicPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer);

  /// @brief Increase total length of the packet (used by Messages when inserting themselves).
  /// @param [in] addedSize Additional size.
  /// @return OKAY if successful, otherwise an error code.
  Status IncreaseTotalLength(uint32_t addedSize);

  std::shared_ptr<std::vector<uint8_t>> m_buffer;  ///< Buffer containing the packet.
  Status m_constructorStatus;                      ///< Status of the constructor.

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
  /// This is used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  CommandPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer);

  friend class SenderUDP;
  friend class SenderFile;
  friend class ReceiverUDP;
  friend class ReceiverFile;

  friend class CommandPacketReset;
  friend class CommandPacketCancelAllStreams;
  friend class CommandPacketStartRecording;
  friend class CommandPacketCancelRecording;
  friend class CommandPacketStartReplay;
  friend class CommandPacketPauseReplay;
  friend class CommandPacketResumeReplay;
  friend class CommandPacketCancelReplay;
  friend class CommandPacketSetStartUpRecordingState;
  friend class CommandPacketKeepaliveInterval;
  friend class CommandPacketStreamState;
  friend class CommandPacketCancelState;
  /// @todo Finish the rest of the subclasses, here and below, once we've finished a full example.
  friend class CommandPacketStreamSubregions;
};

/// @brief Command packet to reset the system
class CommandPacketReset : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the Reset opcode.
  CommandPacketReset();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
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

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
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

/// @brief Command packet to start recording
class CommandPacketStartRecording : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the start recording opcode.
  CommandPacketStartRecording();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStartRecording(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel recording
class CommandPacketCancelRecording : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the start recording opcode.
  CommandPacketCancelRecording();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelRecording(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to start replay.
class CommandPacketStartReplay : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the StartReplay opcode.
  /// @param [in] ID ID of the stream to replay.
  /// @param [in] initialTime Time code of the initial packet to replay.
  /// The first packet replayed will have its time code set to this value. Others will be relative to it.
  CommandPacketStartReplay(uint32_t ID, Time initialTime);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStartReplay(CommandPacket& basePacket);

  /// @brief Get the ID of the stream to replay.
  /// @param [out] ID ID of the stream to replay.
  /// @return OKAY if successful, otherwise an error code.
  Status GetID(uint32_t& ID) const;

  /// @brief Get the time code of the initial packet to replay.
  /// @param [out] initialTime Time code of the initial packet to replay.
  /// @return OKAY if successful, otherwise an error code.
  Status GetInitialTime(Time& initialTime) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to pause replay
class CommandPacketPauseReplay : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the pause replay opcode.
  CommandPacketPauseReplay();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketPauseReplay(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to resume replay
class CommandPacketResumeReplay : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the resume replay opcode.
  CommandPacketResumeReplay();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketResumeReplay(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel replay
class CommandPacketCancelReplay : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the cancel replay opcode.
  CommandPacketCancelReplay();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelReplay(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to set the start-up recording state
class CommandPacketSetStartUpRecordingState : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the set the start-up recording opcode.
  /// @param [in] state State to set the start-up recording to (0 = not recording, 1 = recording).
  CommandPacketSetStartUpRecordingState(uint32_t state);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketSetStartUpRecordingState(CommandPacket& basePacket);

  /// @brief Get the state to set the start-up recording to.
  /// @param [out] state State to set the start-up recording to (0 = not recording, 1 = recording).
  /// @return OKAY if successful, otherwise an error code.
  Status GetState(uint32_t& state) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to set the keepalive interval
class CommandPacketKeepaliveInterval : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the keepalive interval opcode.
  /// @param [in] interval Interval to set the keepalive to in seconds.
  CommandPacketKeepaliveInterval(float interval);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketKeepaliveInterval(CommandPacket& basePacket);

  /// @brief Get the keepalive interval.
  /// @param [out] interval Interval to set the keepalive to in seconds.
  /// @return OKAY if successful, otherwise an error code.
  Status GetInterval(float& interval) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to start streaming state.
class CommandPacketStreamState: public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the StreamState opcode.
  /// @param [in] IP IP address of the system to stream to.
  /// @param [in] port Port number of the system to stream to.
  /// @param [in] interval Interval to stream at in seconds.
  CommandPacketStreamState(uint32_t IP, uint16_t port, float interval);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStreamState(CommandPacket& basePacket);

  /// @brief Get the IP to stream to.
  /// @param [out] IP IP to stream state on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& IP) const;

  /// @brief Get the port to stream to.
  /// @param [out] port Port to stream.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

  /// @brief Get the interval to stream at.
  /// @param [out] interval Interval to stream at in seconds.
  /// @return OKAY if successful, otherwise an error code.
  Status GetInterval(float& interval) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel streaming state.
class CommandPacketCancelState : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the CancelState opcode.
  /// @param [in] IP IP address of the system to stop streaming to.
  /// @param [in] port Port number of the system to stop streaming to.
  CommandPacketCancelState(uint32_t IP, uint16_t port);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelState(CommandPacket& basePacket);

  /// @brief Get the IP to stop streaming to.
  /// @param [out] IP IP to stop streaming state on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& IP) const;

  /// @brief Get the port to stop streaming to.
  /// @param [out] port Port to stop streaming to .
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Structure describing a subregion.
struct SubregionDescription {
  uint32_t cameraID;    ///< Camera ID the region is from
  uint32_t skipFrames;  ///< Number of frames to skip between frames in the subregion
  uint32_t skipModulo;  ///< Which frame to send when skipping (0 = first, 1 = second, etc.)
  uint32_t left;        ///< Left side of the subregion
  uint32_t top;         ///< Top side of the subregion
  uint32_t right;       ///< Right side of the subregion
  uint32_t bottom;      ///< Bottom side of the subregion

  /// @brief Equality operator.
  bool operator ==(const SubregionDescription& other) const {
    return cameraID == other.cameraID &&
      skipFrames == other.skipFrames &&
      skipModulo == other.skipModulo &&
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

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStreamSubregions(CommandPacket& basePacket);

  /// @brief Get the IP to stream to.
  /// @param [out] IP IP to stream subregions on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& IP) const;

  /// @brief Get the port to stream to.
  /// @param [out] port Port to stream.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

  /// @brief Get the subregion description.
  /// @param [out] regions Regsion description.
  /// @return OKAY if successful, otherwise an error code.
  Status GetRegionDescriptions(std::vector<SubregionDescription> & regions) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

//---------------------------------------------------------------------------
/// @brief Stream packet, subclass constructed and sent by servers and received and parsed by clients.
///
/// The stream packet is a UDP packet sent by a server to a client.  It contains zero or more
/// Messages.  The client receives the packet, parses it, and handles the messages.
/// These packets are sent using the SocketSender class and received using the SocketReceiver class.
/// They are created on a server by constructing a subclass.  They are parsed on a client from a
/// buffer by getting each message from the buffer.
///
/// Subclasses are listed below.

class Message;   // Foreward declaration
class StreamPacket : public BasicPacket {
public:

  /// @brief Get the sequence number.
  /// @param [out] sequenceNumber The sequence number.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSequenceNumber(uint32_t& sequenceNumber) const;

  /// @brief Set the sequence number.
  /// @param [in] sequenceNumber The sequence number.
  /// @return OKAY if successful, otherwise an error code.
  Status SetSequenceNumber(uint32_t sequenceNumber);

  /// @brief Get the time code.
  /// @param [out] timeCode The time code.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTimeCode(Time& timeCode) const;

  /// @brief Set the time code.
  /// @param [in] timeCode The time code.
  /// @return OKAY if successful, otherwise an error code.
  Status SetTimeCode(Time timeCode);

  /// @brief Get the next message from the buffer
  /// @param [inout] message Pointer to the next message in the buffer. Client
  /// initially sets this to nullptr, which asks for the first message in
  /// the buffer. It then passes the previous message each time to get the
  /// next. A nullptr is returned after the last message.
  /// @return OKAY if successful, otherwise an error code.
  Status GetNextMessage(std::shared_ptr<Message>& message) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  // Remove the default constructor and copy operators.
  StreamPacket(const StreamPacket&) = delete;
  StreamPacket& operator=(const StreamPacket&) = delete;
  StreamPacket(StreamPacket&&) = delete;
  StreamPacket& operator=(StreamPacket&&) = delete;

  /// @brief Construct a StreamPacket with no messages, reserving space for them.
  /// @param [in] bufferMaxSize Maximum size of the buffer for the packet, based on
  /// the payload size of the network, subtracting the header from the MTU size.
  /// The default is the standard Ethernet MTU minus the standard IP header size.
  /// For jumbo frames, this should be set to 9000 - 28 = 8972.
  /// @param [in] sequenceNumber Sequence number for the packet.
  /// @param [in] timeCode Time code for the packet.
  StreamPacket(uint32_t bufferMaxSize = 9000 - 28, uint32_t sequenceNumber = 0, Time timeCode = { 0, 0 });

  /// @brief Construct a StreamPacket that shares a buffer with another packet.
  ///
  /// This is used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  StreamPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer);

  friend class SenderUDP;
  friend class SenderFile;
  friend class ReceiverUDP;
  friend class ReceiverFile;

  friend class StreamWriter;

  friend class Message;
  friend class MessageDiscovery;
  friend class MessageFrameBegin;
  friend class MessageFrameData;
  friend class MessageFrameEnd;
  /// @todo Finish the rest of the subclasses, here and below, once we've finished a full example.
};

//---------------------------------------------------------------------------
/// @brief Message base class. Construct using a derived class on the server
/// and parse using a derived class on the client.

class Message {
public:

  /// @brief Get the time of the message.
  /// @param [out] time The time of the message.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTime(Time &time) const;

  /// @brief Get the message type (ID).
  /// @param [out] messageID The message type (ID).
  /// @return OKAY if successful, otherwise an error code.
  Status GetType(MessageID& messageID) const;

  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  virtual ~Message();

  /// @brief Return the status of the constructor.
  /// @return OKAY if successful, otherwise an error code.
  Status GetConstructorStatus() const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  /// @brief Construct a message and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] parameterSize Size of the parameters for the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] type Type of the message.
  Message(StreamPacket &packet, uint32_t parameterSize, Time timeCode, MessageID type);

  /// @brief Construct a message that points at an existing buffer in a StreamPacket.
  /// @param [in] existingBuffer Pointer to the buffer containing the message.
  /// @param [in] offset Offset into the buffer to the start of the message.
  Message(std::shared_ptr<std::vector<uint8_t>> existingBuffer, uint32_t offset);

  Status m_constructorStatus;                       ///< Status of the constructor.

  std::shared_ptr<std::vector<uint8_t>> m_buffer;   ///< Buffer containing the message.
  uint32_t m_offset;                                ///< Offset into the buffer to the start of the message.

  friend class StreamPacket;
};

/// @brief Discovery message.
class MessageDiscovery : public Message {
public:
  /// @brief Construct a MessageDiscovery and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] IP IP address of the system.
  /// @param [in] port Port number of the system.
  /// @param [in] serial Serial number of the system.
  MessageDiscovery::MessageDiscovery(StreamPacket& packet, Time timeCode,
    uint32_t IP, uint16_t port, uint32_t serial);

  /// @brief Type-cast a base Message into a MessageDiscovery packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageDiscovery(Message& baseMessage);

  /// @brief Get the IP address of the system.
  /// @param [out] IP IP address of the system.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& IP) const;

  /// @brief Get the port number of the system.
  /// @param [out] port Port number of the system.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

  /// @brief Get the serial number of the system.
  /// @param [out] serial Serial number of the system.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSerial(uint32_t& serial) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Frame begin message.
class MessageFrameBegin : public Message {
public:
  /// @brief Construct a MessageFrameBegin and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] cameraID Camera ID for the frame.
  /// @param [in] cameraType Camera type that can be used to determine the lens and sensor by
  /// looking up the information in a table.  This also indicates whether the camera is an IR
  /// camera or a visible-light camera.
  /// @param [in] sensorWidth the total number of pixels in a full frame.
  /// @param [in] sensorHeight the total number of pixels in a full frame.
  /// @param [in] exposure Exposure in seconds for the frame (0 for none reported).
  /// @param [in] gain Gain for the frame (0 for none reported).
  MessageFrameBegin(StreamPacket& packet, Time timeCode,
    uint32_t cameraID, uint32_t cameraType, uint16_t sensorWidth, uint16_t sensorHeight,
    float exposure = 0, float gain = 0);

  /// @brief Type-cast a base Message into a MessageFrameBegin packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageFrameBegin(Message& baseMessage);

  /// @brief Get the camera ID for the frame.
  /// @param [out] cameraID Camera ID for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCameraID(uint32_t& cameraID) const;

  /// @brief Get the camera type for the frame.
  /// @param [out] cameraType Camera type for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCameraType(uint32_t& cameraType) const;

  /// @brief Get the sensor width for the frame.
  /// @param [out] sensorWidth Sensor width for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSensorWidth(uint16_t& sensorWidth) const;

  /// @brief Get the sensor height for the frame.
  /// @param [out] sensorHeight Sensor height for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSensorHeight(uint16_t& sensorHeight) const;

  /// @brief Get the exposure for the frame.
  /// @param [out] exposure Exposure for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetExposure(float& exposure) const;

  /// @brief Get the gain for the frame.
  /// @param [out] gain Gain for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetGain(float& gain) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Frame data message.
class MessageFrameData : public Message {
public:
  /// @brief Construct a MessageFrameData and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] cameraID Camera ID for the frame.
  /// @param [in] left Left side of the frame.
  /// @param [in] top Top side of the frame.
  /// @param [in] right Right side of the frame.
  /// @param [in] bottom Bottom side of the frame.
  /// @param [in] data Start of data for the frame. The number of bytes is two per pixel.
  /// @param [in] stride Stride of the image that the data is being read from.  This is
  /// required so that the message knows how many pixels to skip between rows in the image
  /// it is reading from.  This is the number of pixels to skip in memory from one row to
  /// the next, which must be >= the number of pixels in a row.  It can be larger because
  /// the image may be padded to a larger size.
  MessageFrameData(StreamPacket& packet, Time timeCode,
    uint32_t cameraID, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom,
    uint8_t *data, uint16_t stride);

  /// @brief Type-cast a base Message into a MessageFrameData packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageFrameData(Message& baseMessage);

  /// @brief Get the camera ID for the frame.
  /// @param [out] cameraID Camera ID for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCameraID(uint32_t& cameraID) const;

  /// @brief Get the index of the leftmost column of pixels.
  /// @param [out] left Index of the leftmost column of pixels.
  /// @return OKAY if successful, otherwise an error code.
  Status GetLeft(uint16_t& left) const;

  /// @brief Get the index of the rightmost column of pixels.
  /// @param [out] right Index of the rightmost column of pixels.
  /// @return OKAY if successful, otherwise an error code.
  Status GetRight(uint16_t& right) const;

  /// @brief Get the index of the topmost column of pixels.
  /// @param [out] top Index of the topmost column of pixels.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTop(uint16_t& top) const;

  /// @brief Get the index of the bottommost column of pixels.
  /// @param [out] bottom Index of the bottommost column of pixels.
  /// @return OKAY if successful, otherwise an error code.
  Status GetBottom(uint16_t& bottom) const;

  /// @brief Get a pointer to the data for the frame.
  /// @param [out] data Pointer to the data for the frame.
  /// This pointer is valid only as long as the MessageFrameData is valid.
  /// @return OKAY if successful, otherwise an error code.
  Status GetDataPointer(uint8_t** data) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Frame End message.
class MessageFrameEnd : public Message {
public:
  /// @brief Construct a MessageFrameEnd and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] cameraID Camera ID for the frame.
  MessageFrameEnd(StreamPacket& packet, Time timeCode, uint32_t cameraID);

  /// @brief Type-cast a base Message into a MessageFrameEnd packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageFrameEnd(Message& baseMessage);

  /// @brief Get the camera ID for the frame.
  /// @param [out] cameraID Camera ID for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCameraID(uint32_t& cameraID) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

//---------------------------------------------------------------------------
/// @brief Base Socket class, with implementation hidden.
class Socket;

//---------------------------------------------------------------------------
/// @brief Base interfaces class for both UDP and file-based packet stream sending.

class Sender {
public:
  /// @brief Construct a Sender object.
  Sender() : m_constructorStatus(OKAY) {};

  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  virtual ~Sender() {};

  /// @brief Send a packet from a buffer in memory.
  /// @param [in] buffer Pointer to the buffer containing the packet to send.
  /// @param [in] length Length of the packet to send.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status Send(const void* buffer, uint32_t length) = 0;

  /// @brief Send a CommandPacket.
  /// @param [in] packet CommandPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status SendCommandPacket(const CommandPacket& packet) = 0;

  /// @brief Send a StreamPacket.
  /// @param [in] packet StreamPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status SendStreamPacket(const StreamPacket& packet) = 0;

  /// @brief Return the status of the constructor.
  virtual Status GetConstructorStatus() const { return m_constructorStatus; }

protected:
  Status m_constructorStatus;       ///< Reports any errors during construction
};

//---------------------------------------------------------------------------
/// @brief Class used to send UDP packets on a socket. Used internally by CoreClient and CoreServer.

class SenderUDP : public Sender {
public:
  /// @brief Construct a SocketSender object that will send to a specific endpoint.
  /// @param [in] host Name of the host to send to.
  /// @param [in] port Port number to send to.
  SenderUDP(std::string host, uint16_t port);

  /// @brief Destructor.
  virtual ~SenderUDP();

  /// @brief Send a UDP packet.
  /// @param [in] buffer Pointer to the buffer containing the packet to send.
  /// @param [in] length Length of the packet to send.
  /// @return OKAY if successful, otherwise an error code.
  Status Send(const void* buffer, uint32_t length) override;

  /// @brief Send a CommandPacket.
  /// @param [in] packet CommandPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  Status SendCommandPacket(const CommandPacket& packet) override;

  /// @brief Send a StreamPacket.
  /// @param [in] packet StreamPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  Status SendStreamPacket(const StreamPacket& packet) override;

  /// @brief Get the IP address associated with this sender.
  /// @param [out] IP IP address associated with this sender.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& IP) const;

  /// @brief Get the port associated with this sender.
  /// @param [out] port Port associated with this sender.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

protected:
  std::shared_ptr<Socket> m_socket; ///< Pointer to the socket object to use to do our work.
  uint32_t m_IP;                    ///< IP address to send to.
  uint16_t m_port;                  ///< Port number to send to.
};

//---------------------------------------------------------------------------
/// @brief Class used to send packets to a file. Used internally by CoreClient and CoreServer.

class SenderFile : public Sender {
public:
  /// @brief Construct a SocketSender object that will send to a specific endpoint.
  /// @param [in] fileName Name of the file to write to.
  SenderFile(std::string fileName);

  /// @brief Destructor.
  virtual ~SenderFile();

  /// @brief Send a UDP packet.
  /// @param [in] buffer Pointer to the buffer containing the packet to send.
  /// @param [in] length Length of the packet to send.
  /// @return OKAY if successful, otherwise an error code.
  Status Send(const void* buffer, uint32_t length) override;

  /// @brief Send a CommandPacket.
  /// @param [in] packet CommandPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  Status SendCommandPacket(const CommandPacket& packet) override;

  /// @brief Send a StreamPacket.
  /// @param [in] packet StreamPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  Status SendStreamPacket(const StreamPacket& packet) override;

protected:
  std::shared_ptr<std::ofstream> m_file;    ///< Pointer to the file object to write to.
};

//---------------------------------------------------------------------------
/// @brief Base interfaces class for both UDP and file-based packet stream receipt.

class Receiver {
public:
  /// @brief Construct a Receiver object.
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  Receiver(uint32_t maxLen = 9000 - 28) : m_constructorStatus(OKAY), m_maxLen(maxLen) {};

  /// @brief See if a packet is available to receive.
  /// 
  /// This is not usually called by client code, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status IsPacketAvailable(double timeout_seconds, bool& available) = 0;

  /// @brief Receive a packet, hanging until one is available.
  /// 
  /// This is not usually called by client code, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [inout] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the entire packet.  If it is too small, the packet will be truncated and BUFFER_TOO_SMALL
  /// will be returned.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status ReceiveBuffer(std::vector<uint8_t>& buffer) = 0;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  virtual Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) = 0;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  virtual Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet) = 0;

  /// @brief Return the status of the constructor.
  virtual Status GetConstructorStatus() const { return m_constructorStatus; }

protected:
  Status m_constructorStatus;       ///< Reports any errors during construction
  uint32_t m_maxLen;                ///< Maximum length of a packet we can receive.
};

//---------------------------------------------------------------------------
/// @brief Class used to receive UDP packets on a socket.

class ReceiverUDP : public Receiver {
public:
  /// @brief Construct a SocketReceiver object.
  /// @param [in] interfaceName Name of the interface to listen on.
  /// @param [in] port Port number to listen on (default of 0 means any available port).
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  ReceiverUDP(std::string interfaceName = "localhost", uint16_t port = 0, uint32_t maxLen = 9000 - 28);

  /// @brief Get the port associated with this receiver.
  /// @return The port associated with this receiver, or 0 for failure.
  uint16_t GetPort() const { return m_port; }

  /// @brief See if a packet is available to receive.
  /// 
  /// This is not usually called by client code, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  Status IsPacketAvailable(double timeout_seconds, bool& available) override;

  /// @brief Receive a packet, hanging until one is available.
  /// 
  /// This is not usually called by client code, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [inout] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the entire packet.  If it is too small, the packet will be truncated and BUFFER_TOO_SMALL
  /// will be returned.
  /// @return OKAY if successful, otherwise an error code.
  Status ReceiveBuffer(std::vector<uint8_t>& buffer) override;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) override;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet) override;

  /// @brief Destructor.
  virtual ~ReceiverUDP();

  /// @brief Test function for both this class and the SenderUDP class.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  std::shared_ptr<Socket> m_socket; ///< Pointer to the socket object to use to do our work.
  uint16_t m_port;                  ///< Port number we are listening on.
};

//---------------------------------------------------------------------------
/// @brief Class used to read packets from a file that have been streamed there.

class ReceiverFile : public Receiver {
public:
  /// @brief Construct a SocketReceiver object.
  /// @param [in] fileName Name of the file to write to.
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  ReceiverFile(std::string fileName, uint32_t maxLen = 9000 - 28);

  /// @brief See if a packet is available to receive.
  /// 
  /// This is not usually called by client code, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  Status IsPacketAvailable(double timeout_seconds, bool& available) override;

  /// @brief Receive a packet, hanging until one is available.
  /// 
  /// This is not usually called by client code, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [inout] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the entire packet.  If it is too small, the packet will be truncated and BUFFER_TOO_SMALL
  /// will be returned.
  /// @return OKAY if successful, otherwise an error code.
  Status ReceiveBuffer(std::vector<uint8_t>& buffer) override;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) override;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet) override;

  /// @brief Destructor.
  virtual ~ReceiverFile();

  /// @brief Test function for both this class and the SenderFile class.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  std::shared_ptr<std::ifstream> m_file;    ///< Pointer to the file object to read from.
};

//---------------------------------------------------------------------------
/// @brief Class used to support writing messages to streams.
///
/// This class is used to support writing messages to streams.  It is used
/// by a server to write messages to a stream, keeping track of the sequence
/// number and time code for each message.  It provides methods to handle
/// keeping track of the current packet and sending it when it is full, but
/// the caller is responsible for ensuring that a message fits in the current
/// packet or else flushing the current packet and starting a new one.

class StreamWriter {
public:
  /// @brief Construct a StreamWriter object.
  /// @param [in] sender Pointer to the sender object to use to send the packets.
  /// @param [in] timer Pointer to the timer object to use to get the time code.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  StreamWriter(std::shared_ptr<asdp::Sender> sender,
    std::shared_ptr<asdp::Timer> timer,
    uint32_t maxPayloadSize = 9000 - 28);

  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  ///
  /// Flushes the current packet before destroying.
  virtual ~StreamWriter();

  /// @brief Return the status of the constructor.
  /// @return Constructor status.
  Status GetConstructorStatus() const;

  /// @brief Get the current packet being used.
  /// @param [out] packet The current packet being used.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCurrentPacket(std::shared_ptr<asdp::StreamPacket>& packet) const;

  /// @brief Flush the current packet, sending it and getting a new one.
  /// @return OKAY if successful, otherwise an error code.
  Status Flush();

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  Status m_constructorStatus;       ///< Reports any errors during construction
  std::shared_ptr<asdp::Sender> m_sender;  ///< Pointer to the sender object to use to send the packets.
  std::shared_ptr<asdp::Timer> m_timer;    ///< Pointer to the timer object to use to get the time code.
  uint32_t m_maxPayloadSize;        ///< Maximum size of the packet payload to send.
  uint32_t m_sequenceNumber;        ///< Sequence number for the next packet to send.
  std::shared_ptr<asdp::StreamPacket> m_currentPacket; ///< Current packet being built.
};

//---------------------------------------------------------------------------
/// @brief Core class, which is the derived class for both a client and server.

class Core {
public:
  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  virtual ~Core();

  /// @brief Get the version of the Core API that was linked against (not the network-connected one).
  /// @return The version of the Core API.
  static std::string GetVersion();

  /// @brief Get the maximum size of the payload that can be sent in a UDP packet using this Core.
  /// @param [out] value The maximum size of the payload that can be sent in a UDP packet using this Core.
  /// @return OKAY if successful, otherwise an error code.
  Status GetMaxPayloadSize(size_t& value) const;

  /// @brief Return the status of the constructor.
  virtual Status GetConstructorStatus() const;

protected:
  Core(uint32_t maxPayloadSize);
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  Core(Core&&) = delete;
  Core& operator=(Core&&) = delete;

  Status m_constructorStatus;       ///< Reports any errors during construction
  uint32_t m_maxPayloadSize;        ///< Maximum size of the packet to send.
  std::shared_ptr<Timer> m_timer;   ///< Pointer to the timer object to use to get the time code.
};

//---------------------------------------------------------------------------
/// @brief Core Server class, which provides functions needed to implement a server.
///
/// Starts sending periodic discovery packets to clients, and creates a listening
/// socket to receive commands from clients. This class is not used when streaming
/// from disk files, only when using network connections.

class CoreServer : public Core {
public:
  /// @brief Construct a CoreServer object.
  /// @param [in] serial Serial number of the server.
  /// @param [in] NICName Name of the network interface to use.
  /// @param [in] sendPort Port number to send packets to.
  /// @param [in] listenPort Port number to listen for packets on.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  CoreServer(uint32_t serial, std::string NICName, uint16_t sendPort = 10102, uint16_t listenPort = 10101,
    uint32_t maxPayloadSize = 9000 - 28);

  /// @brief Get the receiver to use to receive command packets.
  /// @param [out] receiver The receiver to use to receive command packets.
  /// @return OKAY if successful, otherwise an error code.
  Status GetReceiver(std::shared_ptr<Receiver>& receiver) const;

  /// @brief Get the status of the discovery thread.
  /// @param [out] threadStatus Stores the thread status.
  /// @return OKAY if successful, otherwise an error code.
  Status GetDiscoveryThreadStatus(Status& threadStatus) const;

  /// @brief Destructor.
  ~CoreServer();

protected:
  CoreServer() = delete;
  CoreServer(const CoreServer&) = delete;
  CoreServer& operator=(const CoreServer&) = delete;
  CoreServer(CoreServer&&) = delete;
  CoreServer& operator=(CoreServer&&) = delete;

  std::shared_ptr<std::thread> m_discoveryThread; ///< Thread that sends discovery packets.
  void DiscoveryThread();           ///< Thread that sends discovery packets.
  std::atomic_bool m_stopThread;    ///< Flag to tell the thread to stop.
  Status m_threadStatus;            ///< Status of the thread.

  Status m_constructorStatus;                     ///< Reports any errors during construction
  std::shared_ptr<SenderUDP> m_sender;            ///< Sender object to use to send packets.
  std::shared_ptr<ReceiverUDP> m_receiver;        ///< Receiver object to use to receive packets.

  uint32_t m_IP;                                  ///< IP address for the client to send commands to.
  uint16_t m_port;                                ///< Port number for a client to send commands to.
  uint32_t m_serial;                              ///< Serial number of the server.
};

//---------------------------------------------------------------------------
/// @brief Core Client class, which provides functions needed to implement a client.
///
/// Handles polling for connections on an interface and listing available servers.
/// Also provides function to send command packets to a connected server.

class CoreClient : public Core {
public:
  /// @brief Construct a CoreServer object.
  /// @param [in] NICName Name of the network interface to use.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  CoreClient(std::string NICName, uint32_t maxPayloadSize = 9000 - 28);

  /// @brief Get the status of the discovery thread.
  /// @param [out] threadStatus Stores the thread status.
  /// @return OKAY if successful, otherwise an error code.
  Status GetDiscoveryThreadStatus(Status& threadStatus) const;

  /// @brief Get the list of servers found.
  /// @param [out] servers List of servers found.
  /// @return OKAY if successful, otherwise an error code.
  Status IdentifiedServers(std::vector<std::string>& servers) const;

  /// @brief Connect to a server.
  /// @param [in] serverURL Server to connect to.
  /// @return OKAY if successful, otherwise an error code.
  Status ConnectToServer(std::string serverURL);

  /// @brief Get the IP address of the NIC we're using to talk with the server.
  /// @param [out] IP IP address of the NIC we're using to talk with the server.
  /// @return OKAY if successful, otherwise an error code.
  Status GetMyIP(uint32_t& IP) const;

  /// @brief Get the serial number of the server we're connected to.
  /// @param [out] serial Serial number of the server we're connected to.
  /// @return OKAY if successful, otherwise an error code.
  Status GetServerSerialNumber(uint32_t& serial) const;

  /// @brief Send a CommandPacket to the connected server.
  /// @param [in] packet CommandPacket to send.
  /// @return OKAY if successful, otherwise an error code.
  Status SendCommandPacket(const CommandPacket& packet);

  /// @brief Destructor.
  ~CoreClient();

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  CoreClient() = delete;
  CoreClient(const CoreClient&) = delete;
  CoreClient& operator=(const CoreClient&) = delete;
  CoreClient(CoreServer&&) = delete;
  CoreClient& operator=(CoreClient&&) = delete;

  Status m_constructorStatus;                     ///< Reports any errors during construction

  std::shared_ptr<std::thread> m_discoveryThread; ///< Thread that listens for discovery packets.
  void DiscoveryThread();                         ///< Thread that listens for discovery packets.
  std::atomic_bool m_stopThread;                  ///< Flag to tell the thread to stop.
  Status m_threadStatus;                          ///< Status of the thread.

  /// @brief Struct to describe the information about a server we've found.
  struct ServerInfo {
    uint32_t IP;                                  ///< IP address of the server.
    uint16_t port;                                ///< Port number of the server.
    uint32_t serial;                              ///< Serial number of the server.

    ServerInfo(uint32_t pIP, uint32_t pPort, uint32_t pSerial) : IP(pIP), port(pPort), serial(pSerial) {}
    bool operator==(const ServerInfo& rhs) const {
      return IP == rhs.IP && port == rhs.port && serial == rhs.serial;
    }
    bool operator!=(const ServerInfo& rhs) const {
      return !(*this == rhs);
    }
  };

  /// @brief Get the URL for a server.
  /// @param [in] serverInfo Information about the server.
  /// @return URL for the server.
  static std::string URLFromServerInfo(ServerInfo serverInfo);

  /// @brief Get the server connection information from a URL.
  /// @param [in] URL URL for the server.
  /// @param [out] IP IP address of the server.
  /// @param [out] port Port number of the server.
  static Status ServerInfoFromURL(std::string URL, std::string &IP, uint16_t& port);

  /// Mutex to protect the list of servers. Marked mutable so it can be used in const member functions.
  mutable std::mutex m_mutex;
  std::vector<ServerInfo> m_servers;              ///< List of servers found.
  std::shared_ptr<SenderUDP> m_sender;            ///< Sender object to use to send packets.
  std::shared_ptr<ReceiverUDP> m_receiver;        ///< Receiver object to use to receive Discovery packets.
  uint32_t m_IP;                                  ///< Our IP address where we're listening for packets.
  uint32_t m_serial;                              ///< Serial number of the server we're connected to.
};

//---------------------------------------------------------------------------
/// @brief Test function that verifies that all classes and functions are working.
/// @return Empty string if successful, otherwise descriptive error message.
std::string Test();

} // namespace asdp