/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#pragma once

/**
 * @file ASDP_Core_API.h
 * @brief Apache Strap-Down Pilotage Core C++ API exposed as a static library.
 *
* @author ReliaSolve.
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
#include <array>
#include <list>
#include <map>
#include <ostream>

namespace asdp {

//---------------------------------------------------------------------------
/// @brief Status enumeration, returned by API functions.
enum Status : uint32_t {
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
  NOT_CONNECTED                 = 1015,
  /// @brief Error: The size of a floating-point number is not what was expected.
  INCORRECT_FLOAT_SIZE          = 1016,
  /// @brief Error: Incompatible version of the API.
  INCOMPATIBLE_API_VERSION      = 1017
};

/// @brief Helper function to return a descriptive error message based on a status value.
/// @param [in] status Status value returned from an API call.
/// @return String describing the status condition.
std::string ErrorMessage(Status status);

//---------------------------------------------------------------------------
/// @brief Operation codes for command packets.
enum OpCode : uint32_t {
  RESET                         = 0,
  START_RECORDING               = 2,
  STOP_RECORDING                = 3,
  START_REPLAY                  = 4,
  PAUSE_REPLAY                  = 5,
  RESUME_REPLAY                 = 6,
  STOP_REPLAY                   = 7,
  SET_START_UP_RECORDING_STATE  = 8,
  SET_STREAM_STATE_PERIOD       = 10,
  SET_NUC_FLAG_STATE            = 11,
  START_ON_CAMERA_NUC           = 12,
  CONFIGURE_TRIGGER             = 10000,
  SOFTWARE_TRIGGER              = 10001,
  SET_EVENT_VERBOSITY           = 10002,
  STREAM_SUBREGION              = 20000,
  CANCEL_SUBREGION              = 20001,
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
enum MessageID : uint32_t {
  DISCOVERY                     = 0,
  STATE                         = 1,
  EVENT                         = 10000,
  //FRAME_BEGIN                   = 20000,
  //FRAME_DATA                    = 20001,
  //FRAME_END                     = 20002,
  CONSOLIDATED_FRAME_DATA       = 20003,
  STORED_STREAMS                = 30000,
  TEMPERATURE                   = 40000,
  POSE                          = 50000
};

//---------------------------------------------------------------------------
/// @brief Event IDs.  Most of these are sent on the main command stream.
enum EventID : uint32_t {
  INVALID_OPERATION             = 256,
  INTERNAL_ERROR                = 257,
  UNRECOGNIZED_OPCODE           = 512,
  CLOCK_SYNC                    = 768,
  START_OF_REPLAY               = 769,
  END_OF_REPLAY                 = 770,
  NUC_FLAG_STATE                = 771,    ///< Sent on the camera UDP stream.
  ON_CAMERA_NUC_STATE           = 772,    ///< Sent on the camera UDP stream.
  REPLAY_PAUSED                 = 773,
  REPLAY_RESUMED                = 774,
};

//---------------------------------------------------------------------------
/// @brief feature IDs.
enum FeatureID : uint16_t {
  STORAGE_API_AVAILABLE = 1,
  TEMPERATURE_API_AVAILABLE = 3,
  POSE_API_ORIENTATION_AVAILABLE = 4,
  POSE_API_POSITION_AVAILABLE = 5,
  NUC_FLAG_AVAILABLE = 6,
  ON_CAMERA_NUC_AVAILABLE = 7
};

//---------------------------------------------------------------------------
/// @brief Class to store information about a camera.

class CameraInfo {
public:
  double minTriggerPeriod;    ///< Minimum period between triggers in seconds.
  double maxTriggerPeriod;    ///< Maximum period between triggers in seconds.
  uint32_t trigger;           ///< Internal hardware trigger ID camera is tied to (0 = none).
  uint32_t type;              ///< Type of camera (used to look up lens, sensors, etc. in a table)
  uint16_t width;             ///< Width of the camera image in pixels.
  uint16_t height;            ///< Height of the camera image in pixels.

  /// @brief Equality operator.
  bool operator ==(const CameraInfo& other) const {
    return type == other.type &&
      width == other.width &&
      height == other.height &&
      minTriggerPeriod == other.minTriggerPeriod &&
      maxTriggerPeriod == other.maxTriggerPeriod &&
      trigger == other.trigger;
  };

  /// @brief Inequality operator.
  bool operator !=(const CameraInfo& other) const {
    return !(*this == other);
  };
};

//---------------------------------------------------------------------------
/// @brief Class to store information about a trigger configuration.

class TriggerInfo {
public:
  double period;              ///< Period of the trigger in seconds.
  float offset;               ///< Offset of the trigger in seconds. A positive offset will cause the local hardware trigger to fire later than the incoming trigger command.  Negative values are clamped to 0.
  float trackingFactor;       ///< Tracking factor of the trigger. Value in range [0-1] telling fraction of discrepancy to correct when synchronizing with a new incoming trigger.  A value of 1 completely synchronizes with each trigger. A value of 0.1 shifts by 1 / 10th of the distance, smoothing the adjustment to handle jitter in the incoming hardware trigger while following long-time-scale drift.
  uint16_t ID;                ///< ID of the trigger.
  uint8_t mode;               ///< Mode of the trigger (0 = disabled, 1 = unsynchronized, 2 = one-shot software, 3 = periodic software, 4 = one-shot hardware, 5 = periodic hardware).
  uint8_t externalID;         ///< ID of the external trigger to use (0 = none).

  /// @brief Equality operator.
  bool operator ==(const TriggerInfo& other) const {
    return ID == other.ID &&
      mode == other.mode &&
      externalID == other.externalID &&
      period == other.period &&
      offset == other.offset &&
      trackingFactor == other.trackingFactor;
  }

  /// @brief Inequality operator.
  bool operator !=(const TriggerInfo& other) const {
    return !(*this == other);
  }
};

//---------------------------------------------------------------------------
/// @brief Class to store information about a stream endpoint.
/// @brief Class to store information about a stream endpoint.

class StreamEndpoint {
public:
  /// @brief Construct a stream endpoint from an IP address and port.
  /// @param [in] IP IP address being streamed to in host byte order.
  /// @param [in] port Port being streamed to in host byte order.
  StreamEndpoint(uint32_t IP, uint16_t port) : IP(IP), port(port) { };

  /// @brief Construct a stream endpoint from a host name/IP and port.
  /// @param [in] host Host name or IP address being streamed to. Sets
  /// the IP to 0 if the host name is not a valid name or IP address.
  /// Sets it to INADDR_ANY (which is also 0) if the host name is "".
  /// @param [in] port Port being streamed to in host byte order.
  StreamEndpoint(const std::string &host, uint16_t port);

  /// @brief default constructor
  StreamEndpoint() : IP(0), port(0) { };

  uint32_t IP;    ///< IP being streamed to in host byte order
  uint16_t port;  ///< Port being streamed to in host byte order

  /// @brief Equality operator.
  bool operator ==(const StreamEndpoint& other) const {
    return IP == other.IP && port == other.port;
  }

  /// @brief Inequality operator.
  bool operator !=(const StreamEndpoint& other) const {
    return !(*this == other);
  }

  /// @brief less-than operator.
  bool operator <(const StreamEndpoint& other) const {
    return IP < other.IP || (IP == other.IP && port < other.port);
  }

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
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

  /// @brief Set to a floating-point value in seconds.
  Time& operator =(float value) {
    if (value < 0) {
      seconds = 0;
      microseconds = 0;
    } else {
      seconds = static_cast<uint32_t>(value);
      microseconds = static_cast<uint32_t>((value - seconds) * 1000000);
    }
    return *this;
  }

  /// @brief Construct with a floating-point value in seconds.
  Time(float value) {
    *this = value;
  }

  /// @brief Construct with two values
  Time(uint32_t sec, uint32_t usec) : seconds(sec), microseconds(usec) { }

  /// @brief Default constructor
  Time() : seconds(0), microseconds(0) {};
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

  /// @brief Set the offset between Core time and local time, subtracted from local time.
  /// @details Note that both negative and positive offsets can be provided.  The Core time
  /// is localtime + positive offset - negative offset.  This helps with clock synchronization.
  /// @param [in] offset Offset between Core time and local time.
  /// @return OKAY if successful, otherwise an error code.
  /// Returns BAD_PARAMETER if the offset is too large.
  Status SetCoreNegativeOffset(Time offset);

  /// @brief Set the offset between Core time and local time, added to local time.
  /// @details Note that both negative and positive offsets can be provided.  The Core time
  /// is localtime + positive offset - negative offset.  This helps with clock synchronization.
  /// @param [in] offset Offset between Core time and local time.
  /// @return OKAY if successful, otherwise an error code.
  /// Returns BAD_PARAMETER if the offset is too large.
  Status SetCorePositiveOffset(Time offset);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  Timer();
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;

  Time m_coreNegativeOffset;  ///< Offset subtracted from local time to get Core time.
  Time m_corePositiveOffset;  ///< Offset added to Core time to get local time.

  friend class StreamWriter;  // So it can implement Test().
  friend class Core;
  friend class ClockSynchronizer; // So it can implement Test().
};

//---------------------------------------------------------------------------
/// @brief Base packet type, not used by calling program, supports code common to CommandPacket and StreamPacket.
///
/// The GetConstructorStatus() function can be used to determine if the constructor was successful
/// in this class and in derived classes.

class BasicPacket {
public:

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
  /// @param [in] extraSize Size of the packet beyond the basic packet size.
  /// This includes all extra header information and the parameter portion of the packet.
  BasicPacket(uint32_t extraSize);

  /// @brief Construct a basic packet that shares a buffer with another packet.
  ///
  /// This is used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  /// @param [in] offset Offset into the buffer to start at.  This supports having multiple
  /// packets in the same buffer
  BasicPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer, size_t offset = 0);

  /// @brief Increase total length of the packet (used by Messages when inserting themselves).
  /// @param [in] addedSize Additional size.
  /// @return OKAY if successful, otherwise an error code.
  Status IncreaseTotalLength(uint32_t addedSize);

  /// @brief Get the remaining free space in our buffer.
  size_t MyRemainingSize() const { return m_buffer->size() - m_offset; }

  /// @brief Get the remaining allocated space in our buffer.
  size_t MyRemainingCapacity() const { return m_buffer->capacity() - m_offset; }

  /// @brief Get the buffer containing the packet data at the appropriate offset.
  std::uint8_t *MyData() const { return m_buffer->data() + m_offset; }

  /// @brief Adjust an offset in the buffer to align with the start of the packet.
  size_t AddOffset(size_t offset) const { return offset + m_offset; }

  /// @brief Adjust an offset in the buffer to align with the start of the packet.
  size_t RemoveOffset(size_t offset) const { return offset - m_offset; }

  std::shared_ptr<std::vector<uint8_t>> m_buffer;  ///< Buffer containing the packet.
  size_t m_offset;                                 ///< Offset into the buffer to start at.
  Status m_constructorStatus;                      ///< Status of the constructor.

  friend class CommandPacket;
  friend class StreamPacket;
};

//---------------------------------------------------------------------------
/// @brief Command packet, subclass constructed and sent by clients and received and parsed by server.
///
/// The command packet is a command sent by a client to a server.  It contains an operation code
/// and optional parameters.  The server receives the packet, parses it, and executes the operation.
/// These packets are sent using the Sender class and received using the Receiver class.
/// They are created on a client by constructing a subclass.  They are parsed on a server from a
/// buffer by checking the operation code and then typecasting to the appropriate subclass.
///
/// There can only be one CommandPacket per buffer, so the offset in the BasicPacket is always 0.
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
  /// It is used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  CommandPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer);

  /// @brief Construct a command packet that shares a buffer with another packet.
  ///
  /// This is used when type-casting from an existing buffer to a subclass.
  /// It verifies that the buffer contains the correct type of packet.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// @param [in] code Operation code for the packet. Used to verify that the buffer contains the correct type of packet.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  CommandPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer, OpCode code);

  friend class SenderUDP;
  friend class SenderFile;
  friend class ReceiverUDP;
  friend class ReceiverFile;
  friend class SenderReceiverTCP;

  friend class CommandPacketReset;
  friend class CommandPacketStartRecording;
  friend class CommandPacketStopRecording;
  friend class CommandPacketStartReplay;
  friend class CommandPacketPauseReplay;
  friend class CommandPacketResumeReplay;
  friend class CommandPacketStopReplay;
  friend class CommandPacketSetStartUpRecordingState;
  friend class CommandPacketSetStreamStatePeriod;
  friend class CommandPacketSetNUCFlagState;
  friend class CommandPacketStartOnCameraNUC;
  friend class CommandPacketConfigureTrigger;
  friend class CommandPacketSoftwareTrigger;
  friend class CommandPacketSetEventVerbosity;
  friend class CommandPacketStreamSubregion;
  friend class CommandPacketCancelSubregion;
  friend class CommandPacketEraseAllStoredStreams;
  friend class CommandPacketListStoredStreams;
  friend class CommandPacketEraseStoredStream;
  friend class CommandPacketStreamTemperatures;
  friend class CommandPacketCancelTemperatures;
  friend class CommandPacketStreamPoses;
  friend class CommandPacketCancelPoses;
};

/// @brief Command packet to reset the system
class CommandPacketReset : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the RESET opcode.
  CommandPacketReset();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketReset(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to start recording
class CommandPacketStartRecording : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the START_RECORDING opcode.
  CommandPacketStartRecording();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStartRecording(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel recording
class CommandPacketStopRecording : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the STOP_RECORDING opcode.
  CommandPacketStopRecording();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStopRecording(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to set the start-up recording state
class CommandPacketSetStartUpRecordingState : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the SET_START_UP_RECORDING_STATE opcode.
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

/// @brief Command packet to start replay.
class CommandPacketStartReplay : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the START_REPLAY opcode.
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
  /// @brief Construct a brand-new command buffer with the PAUSE_REPLAY opcode.
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
  /// @brief Construct a brand-new command buffer with the RESUME_REPLAY opcode.
  CommandPacketResumeReplay();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketResumeReplay(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel replay
class CommandPacketStopReplay : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the STOP_REPLAY opcode.
  CommandPacketStopReplay();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStopReplay(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to set the streaming state period.
class CommandPacketSetStreamStatePeriod: public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the SET_STREAM_STATE_PERIOD opcode.
  /// @param [in] interval Interval to stream at in seconds.
  CommandPacketSetStreamStatePeriod(float interval);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketSetStreamStatePeriod(CommandPacket& basePacket);

  /// @brief Get the interval to stream at.
  /// @param [out] interval Interval to stream at in seconds.
  /// @return OKAY if successful, otherwise an error code.
  Status GetInterval(float& interval) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to set the NUC flag state.
class CommandPacketSetNUCFlagState : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the SET_NUC_FLAG_STATE opcode.
  /// @param [in] ID ID of the camera to set the NUC flag for.
  /// @param [in] state State to set the NUC flag to (0 = off, 1 = on).
  CommandPacketSetNUCFlagState(uint32_t ID, uint32_t state);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketSetNUCFlagState(CommandPacket& basePacket);

  /// @brief Get the ID of the camera to set the NUC flag on.
  /// @param [out] ID ID of the camera to set the NUC flag on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetID(uint32_t& ID) const;

  /// @brief Get the state to set the NUC flag to.
  /// @param [out] state State to set the NUC flag to (0 = off, 1 = on).
  /// @return OKAY if successful, otherwise an error code.
  Status GetState(uint32_t& state) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to start on-camera NUC.
class CommandPacketStartOnCameraNUC : public CommandPacket {
  public:
  /// @brief Construct a brand-new command buffer with the START_ON_CAMERA_NUC opcode.
  /// @param [in] ID ID of the camera to start the NUC on.
  CommandPacketStartOnCameraNUC(uint32_t ID);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStartOnCameraNUC(CommandPacket& basePacket);

  /// @brief Get the ID of the camera to start the NUC on.
  /// @param [out] ID ID of the camera to start the NUC on.
  /// @return OKAY if successful, otherwise an error code.
  Status GetID(uint32_t& ID) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to configure a trigger.
class CommandPacketConfigureTrigger : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the CONFIGURE_TRIGGER opcode.
  /// @param [in] config Information about the trigger.
  CommandPacketConfigureTrigger(TriggerInfo config);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketConfigureTrigger(CommandPacket& basePacket);

  /// @brief Get the configuration for the trigger.
  /// @param [out] config Configuration for the trigger.
  /// @return OKAY if successful, otherwise an error code.
  Status GetConfiguration(TriggerInfo &config) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to send a software trigger.
class CommandPacketSoftwareTrigger : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the SOFTWARE_TRIGGER opcode.
  /// @param [in] ID ID of the trigger.
  /// @param [in] initialTime Time code of the trigger.
  /// The first packet replayed will have its time code set to this value. Others will be relative to it.
  CommandPacketSoftwareTrigger(uint8_t ID, Time initialTime);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketSoftwareTrigger(CommandPacket& basePacket);

  /// @brief Get the ID of the trigger.
  /// @param [out] ID ID of the trigger.
  /// @return OKAY if successful, otherwise an error code.
  Status GetID(uint8_t& ID) const;

  /// @brief Get the time code of the trigger.
  /// @param [out] initialTime Time code of the trigger.
  /// @return OKAY if successful, otherwise an error code.
  Status GetInitialTime(Time& initialTime) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to set the verbosity of event streaming.
class CommandPacketSetEventVerbosity : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the SET_EVENT_VERBOSITY opcode.
  /// @param [in] verbosity Verbosity of the events to stream.
  CommandPacketSetEventVerbosity(uint8_t verbosity);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketSetEventVerbosity(CommandPacket& basePacket);

  /// @brief Get the verbosity of the events to stream.
  /// @param [out] verbosity Verbosity of the events to stream.
  /// @return OKAY if successful, otherwise an error code.
  Status GetVerbosity(uint8_t& verbosity) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Structure describing a subregion.
struct SubregionDescription {
  uint32_t cameraID;    ///< Camera ID the region is from
  uint32_t skipFrames;  ///< Number of frames to skip between frames in the subregion
  uint32_t startTimeSeconds;  ///< Time of the first frame to be streamed
  uint32_t startTimeMicroseconds;  ///< Time of the first frame to be streamed
  uint16_t left;        ///< Left side of the subregion
  uint16_t top;         ///< Top side of the subregion
  uint16_t right;       ///< Right side of the subregion
  uint16_t bottom;      ///< Bottom side of the subregion

  /// @brief Equality operator.
  bool operator ==(const SubregionDescription& other) const {
    return cameraID == other.cameraID &&
      skipFrames == other.skipFrames &&
      startTimeSeconds == other.startTimeSeconds &&
      startTimeMicroseconds == other.startTimeMicroseconds &&
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

/// @brief Command packet to stream subregion.
class CommandPacketStreamSubregion : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the STREAM_SUBREGION opcode.
  /// @param [in] endpoint Endpoint to stream to.
  /// @param [in] region Subregion to stream.
  CommandPacketStreamSubregion(StreamEndpoint endpoint, SubregionDescription const &region);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStreamSubregion(CommandPacket& basePacket);

  /// @brief Get the endpoint.
  /// @param [out] endpoint Endpoint to stream to.
  /// @return OKAY if successful, otherwise an error code.
  Status GetEndpoint(StreamEndpoint& endpoint) const;

  /// @brief Get the subregion description.
  /// @param [out] region Region description.
  /// @return OKAY if successful, otherwise an error code.
  Status GetRegionDescription(SubregionDescription &region) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel streaming subregion.
class CommandPacketCancelSubregion : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the CANCEL_SUBREGION opcode.
  /// @param [in] camera Camera to stop streaming to.
  /// @param [in] endpoint Endpoint to stop streaming to.
  CommandPacketCancelSubregion(uint32_t camera, StreamEndpoint endpoint);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelSubregion(CommandPacket& basePacket);

  /// @brief Get the camera to stop streaming to.
  /// @param [out] camera Camera to stop streaming to.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCamera(uint32_t& camera) const;

  /// @brief Get the endpoint.
  /// @param [out] endpoint Endpoint to stop streaming to.
  /// @return OKAY if successful, otherwise an error code.
  Status GetEndpoint(StreamEndpoint& endpoint) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to erase all stored streams.
class CommandPacketEraseAllStoredStreams : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the ERASE_ALL_STORED_STREAMS opcode.
  CommandPacketEraseAllStoredStreams();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketEraseAllStoredStreams(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to send the list of stored streams (there is no corresponding cancel).
class CommandPacketListStoredStreams : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the LIST_STORED_STREAMS opcode.
  CommandPacketListStoredStreams();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketListStoredStreams(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to start replay.
class CommandPacketEraseStoredStream : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the ERASE_STORED_STREAM opcode.
  /// @param [in] ID ID of the stream to erase.
  CommandPacketEraseStoredStream(uint32_t ID);

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketEraseStoredStream(CommandPacket& basePacket);

  /// @brief Get the ID of the stream to eraase.
  /// @param [out] ID ID of the stream to erase.
  /// @return OKAY if successful, otherwise an error code.
  Status GetID(uint32_t& ID) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to start streaming temperatures.
class CommandPacketStreamTemperatures : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the STREAM_TEMPERATURES opcode.
  CommandPacketStreamTemperatures();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStreamTemperatures(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel streaming temperatures.
class CommandPacketCancelTemperatures : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the CANCEL_TEMPERATURES opcode.
  CommandPacketCancelTemperatures() : CommandPacket(0, CANCEL_TEMPERATURES) {};

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelTemperatures(CommandPacket& basePacket) : CommandPacket(basePacket.m_buffer, CANCEL_TEMPERATURES) {};

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test() { return CommandPacket::Test(); }
};

/// @brief Command packet to start streaming poses.
class CommandPacketStreamPoses : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the STREAM_POSES opcode.
  CommandPacketStreamPoses();

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketStreamPoses(CommandPacket& basePacket);

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Command packet to cancel streaming state.
class CommandPacketCancelPoses : public CommandPacket {
public:
  /// @brief Construct a brand-new command buffer with the CANCEL_POSES opcode.
  CommandPacketCancelPoses() : CommandPacket(0, CANCEL_POSES) {};

  /// @brief Type-cast a base CommandPacket, re-using its buffer.
  /// @param [in] basePacket The base packet to convert from.
  CommandPacketCancelPoses(CommandPacket& basePacket) : CommandPacket(basePacket.m_buffer, CANCEL_POSES) {};

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test() { return CommandPacket::Test(); }
};

class Message;   // Forward declaration

//---------------------------------------------------------------------------
/// @brief Stream packet, subclass constructed and sent by servers and received and parsed by clients.
///
/// The stream packet is a packet full of messages sent by a server to a client.  It contains zero or more
/// Messages.  The client receives the packet, parses it, and handles the messages.
/// These packets are sent using a Sender class and received using a Receiver class.
/// They are created on a server by constructing a subclass.  They are parsed on a client from a
/// buffer by getting each message from the buffer.
///
/// Subclasses are listed below.

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

  /// @brief Offset all of the times for the messages in the packet.
  /// @details This is useful when adjusting the times for a packet that is being replayed.
  /// It replaces the times on all messages in place.
  /// @param [in] offset Signed fractional time in seconds to add, clamping to 0 if negative.
  /// @return OKAY if successful, otherwise an error code.
  Status OffsetMessageTimes(double offset);

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
  StreamPacket(uint32_t bufferMaxSize = 9000 - 28, uint32_t sequenceNumber = 0);

  /// @brief Construct a StreamPacket that shares a buffer with another packet.
  ///
  /// This is used when type-casting from an existing buffer to a subclass.
  /// It is also used when constructing a new packet from an existing buffer
  /// that was received from the network.
  /// 
  /// @param [in] existingBuffer Pointer to the buffer containing the packet information.
  /// This adds a reference count to the buffer to ensure that it is not deleted out from
  /// under us.
  /// @param [in] offset Offset into the buffer to the start of the packet.  This supports
  /// having more than one packet in a buffer.
  StreamPacket(std::shared_ptr<std::vector<uint8_t>> existingBuffer, size_t offset = 0);

  friend class SenderUDP;
  friend class SenderFile;
  friend class ReceiverUDP;
  friend class ReceiverFile;
  friend class SenderReceiverTCP;

  friend class StreamWriter;

  friend class StreamPacketSortedQueue;

  friend class Message;
  friend class MessageDiscovery;
  friend class MessageState;
  friend class MessageEvent;
  friend class MessageConsolidatedFrameData;
  friend class MessageStoredStreamList;
  friend class MessageTemperature;
  friend class MessagePose;
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

  /// @brief Get the total size of the message as packed into the message itself.
  /// @param [out] size Total size of the message as packed into the message itself.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTotalSize(uint32_t& size) const;

  /// @brief Copy the message into a StreamPacket, possibly overriding the time.
  /// @param [in] packet StreamPacket to copy the message into.
  /// @param [in] timeCode Time code to override the message's time code with (default Time re-uses existing time).
  /// @return OKAY if successful, otherwise an error code.
  Status CopyToStreamPacket(StreamPacket& packet, Time timeCode = Time()) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  /// @brief Construct a message and store it into a buffer from a StreamPacket.
  /// @param [in] packet The StreamPacket containing the message.
  /// @param [in] parameterSize Size of the parameters for the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] type Type of the message.
  Message(StreamPacket &packet, uint32_t parameterSize, Time timeCode, MessageID type);

  /// @brief Construct a message that points at an existing buffer in a StreamPacket.
  /// @param [in] existingBuffer Pointer to the buffer containing the message.
  /// @param [in] offset Offset into the buffer to the start of the message.  This is the
  /// total offset, so if the message is in a StreamPacket, it will be the offset into
  /// the StreamPacket plus the offset into the message.
  Message(std::shared_ptr<std::vector<uint8_t>> existingBuffer, uint32_t offset);

  /// @brief Set the message time.
  /// @param [in] time Time code for the message.
  /// @return OKAY if successful, otherwise an error code.
  Status SetTime(Time time);

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
  /// @param [in] endpoint Endpoint for the client to connect to on the server.
  /// @param [in] serial Serial number of the system.
  MessageDiscovery(StreamPacket& packet, Time timeCode,
    StreamEndpoint endpoint, uint32_t serial);

  /// @brief Type-cast a base Message into a MessageDiscovery packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageDiscovery(Message& baseMessage);

  /// @brief Get the version of the system.
  /// @param [out] major Major version number.
  /// @param [out] minor Minor version number.
  /// @param [out] patch Patch version number.
  /// @return OKAY if successful, otherwise an error code.
  Status GetVersion(uint8_t &major, uint16_t &minor, uint8_t &patch) const;

  /// @brief Get the endpoint to send commands to.
  /// @param [out] endpoint Endpoint to send commands to.
  /// @return OKAY if successful, otherwise an error code.
  Status GetEndpoint(StreamEndpoint& endpoint) const;

  /// @brief Get the serial number of the system.
  /// @param [out] serial Serial number of the system.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSerial(uint32_t& serial) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief State message.
class MessageState : public Message {
public:
  /// @brief Construct a MessageState and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] features Vector of features supported by the system.
  /// @param [in] cameras Vector of cameras installed on the system.
  /// @param [in] numTempSensorsPerCamera Number of temperature sensors per camera.
  /// @param [in] numExternalTempSensors Number of external temperature sensors.
  /// @param [in] storing Whether the system is storing data to disk.
  /// @param [in] camerasStreaming Whether the system is streaming data from its cameras.
  /// @param [in] replaying Whether the system is replaying data from disk.
  /// @param [in] replayAtEnd Whether replaying data is at the end.
  /// @param [in] recordOnReset Whether the system will record data on reset.
  /// @param [in] triggerConfigs Vector of trigger configurations.
  /// @param [in] totalDiskSpace Total disk space in bytes.
  /// @param [in] remainingDiskSpace Remaining disk space in bytes.
  /// @param [in] streamReplayTime Time code of the stream replay.
  MessageState(StreamPacket& packet, Time timeCode,
    std::vector<FeatureID> features, std::vector<CameraInfo> cameras,
    uint32_t numTempSensorsPerCamera, uint32_t numExternalTempSensors,
    uint8_t storing, uint8_t camerasStreaming, uint8_t replaying, uint8_t replayAtEnd,
    uint8_t recordOnReset,
    std::vector<TriggerInfo> triggerConfigs,
    uint64_t totalDiskSpace, uint64_t remainingDiskSpace,
    Time streamReplayTime);

  /// @brief Type-cast a base Message into a MessageState packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageState(Message& baseMessage);

  /// @brief Get the features supported by the system.
  /// @param [out] features Vector of features supported by the system.
  /// @return OKAY if successful, otherwise an error code.
  Status GetFeatures(std::vector<FeatureID>& features) const;

  /// @brief Get the cameras installed on the system.
  /// @param [out] cameras Vector of cameras installed on the system.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCameras(std::vector<CameraInfo>& cameras) const;

  /// @brief Get the number of temperature sensors per camera.
  /// @param [out] numTempSensorsPerCamera Number of temperature sensors per camera.
  /// @return OKAY if successful, otherwise an error code.
  Status GetNumTempSensorsPerCamera(uint32_t& numTempSensorsPerCamera) const;

  /// @brief Get the number of external temperature sensors.
  /// @param [out] numExternalTempSensors Number of external temperature sensors.
  /// @return OKAY if successful, otherwise an error code.
  Status GetNumExternalTempSensors(uint32_t& numExternalTempSensors) const;

  /// @brief Get whether the system is storing data to disk.
  /// @param [out] storing Whether the system is storing data to disk.
  /// @return OKAY if successful, otherwise an error code.
  Status GetStoring(uint8_t& storing) const;

  /// @brief Get whether the system is streaming data from its cameras.
  /// @param [out] camerasStreaming Whether the system is streaming data from its cameras.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCamerasStreaming(uint8_t& camerasStreaming) const;

  /// @brief Get whether the system is replaying data from disk.
  /// @param [out] replaying Whether the system is replaying data from disk.
  /// @return OKAY if successful, otherwise an error code.
  Status GetReplaying(uint8_t& replaying) const;

  /// @brief Get whether replaying data is at the end.
  /// @param [out] replayAtEnd Whether replaying data is at the end.
  /// @return OKAY if successful, otherwise an error code.
  Status GetReplayAtEnd(uint8_t& replayAtEnd) const;

  /// @brief Get whether the system will record data on reset.
  /// @param [out] recordOnReset Whether the system will record data on reset.
  /// @return OKAY if successful, otherwise an error code.
  Status GetRecordOnReset(uint8_t& recordOnReset) const;

  /// @brief Get the trigger configurations.
  /// @param [out] triggerConfigs Vector of trigger configurations.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTriggerConfigs(std::vector<TriggerInfo>& triggerConfigs) const;

  /// @brief Get the total disk space.
  /// @param [out] totalDiskSpace Total disk space in bytes.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTotalDiskSpace(uint64_t& totalDiskSpace) const;

  /// @brief Get the remaining disk space.
  /// @param [out] remainingDiskSpace Remaining disk space in bytes.
  /// @return OKAY if successful, otherwise an error code.
  Status GetRemainingDiskSpace(uint64_t& remainingDiskSpace) const;

  /// @brief Get the time code of the stream replay.
  /// @param [out] streamReplayTime Time code of the stream replay.
  /// @return OKAY if successful, otherwise an error code.
  Status GetStreamReplayTime(Time& streamReplayTime) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  /// @brief Get a pointer to the first parameter after the (padded) features.
  /// @param [out] offset Offset to the first parameter after the (padded) features.
  /// @return OKAY if successful, otherwise an error code.
  Status GetAfterFeaturesOffset(uint32_t& offset) const;

  /// @brief Get a pointer to the first parameter after the cameras.
  /// @param [out] offset Offset to the first parameter after the cameras.
  /// @return OKAY if successful, otherwise an error code.
  Status GetAfterCamerasOffset(uint32_t& offset) const;

  /// @brief Get a pointer to the first parameter after the trigger configurations.
  /// @param [out] offset Offset to the first parameter after the trigger configurations.
  /// @return OKAY if successful, otherwise an error code.
  Status GetAfterTriggerConfigsOffset(uint32_t& offset) const;
};

/// @brief Event message.
class MessageEvent : public Message {
public:
  /// @brief Construct a MessageEvent and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] priority Priority of the event.
  /// @param [in] type Type of the event.
  /// @param [in] param String parameter for the event.
  MessageEvent(StreamPacket& packet, Time timeCode, uint8_t priority, EventID type,
    std::string param);

  /// @brief Type-cast a base Message into a MessageEvent packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageEvent(Message& baseMessage);

  /// @brief Get the priority of the event.
  /// @param [out] priority Priority of the event.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPriority(uint8_t& priority) const;

  /// @brief Get the type of the event.
  /// @param [out] type Type of the event.
  /// @return OKAY if successful, otherwise an error code.
  Status GetType(EventID& type) const;

  /// @brief Get the string parameter for the event.
  /// @param [out] param String parameter for the event.
  /// @return OKAY if successful, otherwise an error code.
  Status GetParam(std::string& param) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Frame begin message.
class MessageConsolidatedFrameData : public Message {
public:
  /// @brief Construct a MessageConsolidateFrameData and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message. If neither beginFrame nor endFrame is set, this
  /// is the time of the last pixel in this packet.  If only beginFrame is set, this is the time of the
  /// first pixel in the entire frame, whether not it is in the subregion being sent.  If only endFrame is set,
  /// this is the time of the last pixel in the entire frame, whether or not it is in the subregion being sent.
  /// If both bits are set, this is the average of the first and last pixel times in the entire frame, whether or
  /// not they are in the subregion being sent.
  /// @param [in] cameraID Camera ID for the frame.
  /// @param [in] cameraType Camera type that can be used to determine the lens and sensor by
  /// looking up the information in a table.  This also indicates whether the camera is an IR
  /// camera or a visible-light camera.
  /// @param [in] sensorWidth the total number of pixels in a full frame.
  /// @param [in] sensorHeight the total number of pixels in a full frame.
  /// @param [in] left Index of the leftmost column of pixels.
  /// @param [in] top Index of the topmost column of pixels.
  /// @param [in] right Index of the rightmost column of pixels.
  /// @param [in] bottom Index of the bottommost column of pixels.
  /// @param [in] beginFrame Whether this is the beginning of a subregion frame.
  /// @param [in] endFrame Whether this is the end of a subregion frame.
  /// @param [in] data Pointer to the data for the frame.  This points to the first pixel in the entire frame
  /// whether or not it is in the subregion being sent or in this packet.
  /// @param [in] stride Stride of the image that the data is being read from.  This is
  /// required so that the message knows how many pixels to skip between rows in the image
  /// it is reading from.  This is the number of pixels to skip in memory from one row to
  /// the next, which must be >= the number of pixels in a row.  It can be larger because
  /// the image may be padded to a larger size.
  /// @param [in] exposure Exposure in seconds for the frame (0 for none reported).
  /// @param [in] gain Gain for the frame (0 for none reported).
  MessageConsolidatedFrameData(StreamPacket& packet, Time timeCode,
    uint32_t cameraID, uint32_t cameraType, uint16_t sensorWidth, uint16_t sensorHeight,
    uint16_t left, uint16_t top, uint16_t right, uint16_t bottom,
    bool beginFrame, bool endFrame,
    uint8_t *data, uint16_t stride,
    float exposure = 0, float gain = 0);

  /// @brief Type-cast a base Message into a MessageConsolidateFrameData packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageConsolidatedFrameData(Message& baseMessage);

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

  /// @brief Get whether this is the beginning of a frame.
  /// @param [out] beginFrame Whether this is the beginning of a frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetBeginFrameFlag(bool& beginFrame) const;

  /// @brief Get whether this is the end of a frame.
  /// @param [out] endFrame Whether this is the end of a frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetEndFrameFlag(bool& endFrame) const;

  /// @brief Get the exposure for the frame.
  /// @param [out] exposure Exposure for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetExposure(float& exposure) const;

  /// @brief Get the gain for the frame.
  /// @param [out] gain Gain for the frame.
  /// @return OKAY if successful, otherwise an error code.
  Status GetGain(float& gain) const;

  /// @brief Get a pointer to the data for the frame.
  /// @param [out] data Pointer to the data for the frame.
  /// This pointer is valid only as long as the MessageFrameData is valid.
  /// @param [in] row Row of the data to get (0 for the first row in the message, which
  /// is at the top).
  /// @return OKAY if successful, otherwise an error code.
  Status GetDataPointer(uint8_t*& data, uint16_t row = 0) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief List of stored message.
class MessageStoredStreamList : public Message {
public:
  /// @brief Construct a MessageStoredStreamList and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] IDs Vector of IDs of stored streams.
  MessageStoredStreamList(StreamPacket& packet, Time timeCode, std::vector<uint32_t> IDs);

  /// @brief Type-cast a base Message into a MessageStoredStreamList packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageStoredStreamList(Message& baseMessage);

  /// @brief Get the IDs of stored streams.
  /// @param [out] IDs Vector of IDs of stored streams.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIDs(std::vector<uint32_t>& IDs) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Temperature message.
class MessageTemperature : public Message {
public:
  /// @brief Construct a MessageTemperature and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] cameraID Camera ID for the temperature (0 for system sensor).
  /// @param [in] sensorID Sensor ID for the temperature.
  /// @param [in] temperatureCelcius Temperature in degrees Celcius.
  MessageTemperature(StreamPacket& packet, Time timeCode, uint16_t cameraID, uint16_t sensorID, float temperatureCelcius);

  /// @brief Type-cast a base Message into a MessageTemperature packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessageTemperature(Message& baseMessage);

  /// @brief Get the camera ID for the temperature.
  /// @param [out] cameraID Camera ID for the temperature.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCameraID(uint16_t& cameraID) const;

  /// @brief Get the sensor ID for the temperature.
  /// @param [out] sensorID Sensor ID for the temperature.
  /// @return OKAY if successful, otherwise an error code.
  Status GetSensorID(uint16_t& sensorID) const;

  /// @brief Get the temperature in degrees Celcius.
  /// @param [out] temperatureCelcius Temperature in degrees Celcius.
  /// @return OKAY if successful, otherwise an error code.
  Status GetTemperatureCelcius(float& temperatureCelcius) const;

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();
};

/// @brief Pose message.
class MessagePose : public Message {
public:
  /// @brief Construct a MessagePose and store it into a buffer from a StreamPacket.
  /// @param [in] packet Pointer to the StreamPacket containing the message.
  /// @param [in] timeCode Time code for the message.
  /// @param [in] latitude Latitude in degrees. (-1e6 if not available)
  /// @param [in] longitude Longitude in degrees. (-1e6 if not available)
  /// @param [in] altitude Altitude in meters. (-1e6 if not available)
  /// @param [in] rot Rotation in degrees around the X, Y, and Z axes in that order.
  /// The canonical orientation has X pointing East, Y pointing North, and Z pointing up
  /// (undefined at the poles).  The helicopter orientation is defined as
  /// pilot right is X, pilot forward is Y, and up is Z. (-1e6 if not available)
  /// The rotation tells how to re-orient an object with canonical orientation to the
  /// helicopter orientation. (-1e6 if not available)
  /// @param [in] vel Velocity in meters per second in rotated helicopter coordinates.
  /// A positive Y value means that the helicopter is moving forwards. (-1e6 if not available)
  /// @param [in] rotVel Rotational velocity about the rotated helicopter axes in degrees per second.
  /// A positive X rotation means that the helicopter is tilting back. (-1e6 if not available)

  MessagePose(StreamPacket& packet, Time timeCode,
    double latitude, double longitude, double altitude,
    std::array<float, 3> rot, std::array<float, 3> vel, std::array<float, 3> rotVel);

  /// @brief Type-cast a base Message into a MessagePose packet, re-using its buffer.
  /// @param [in] baseMessage The base Message to convert from.
  MessagePose(Message& baseMessage);

  /// @brief Get the longitude in degrees.
  /// @param [out] longitude Longitude in degrees. (-1e6 if not available)
  /// @return OKAY if successful, otherwise an error code.
  Status GetLongitude(double& longitude) const;

  /// @brief Get the latitude in degrees.
  /// @param [out] latitude Latitude in degrees. (-1e6 if not available)
  /// @return OKAY if successful, otherwise an error code.
  Status GetLatitude(double& latitude) const;

  /// @brief Get the altitude in meters.
  /// @param [out] altitude Altitude in meters. (-1e6 if not available)
  /// @return OKAY if successful, otherwise an error code.
  Status GetAltitude(double& altitude) const;

  /// @brief Get the rotation about the X, Y, and Z axes in degrees.
  /// @param [out] rot Rotation about the X, Y, and Z axes in degrees.
  /// The rotation tells how to re-orient an object with canonical orientation to the
  /// helicopter orientation. (-1e6 if not available)
  /// @return OKAY if successful, otherwise an error code.
  Status GetRot(std::array<float, 3>& rot) const;

  /// @brief Get the velocity in the X, Y, and Z directions in meters per second in helicopter coordinates.
  /// @param [out] vel Velocity in the X, Y, and Z directions in meters per second in helicopter coordinates.
  /// (-1e6 if not available)
  /// @return OKAY if successful, otherwise an error code.
  Status GetVel(std::array<float, 3>& vel) const;

  /// @brief Get the rotational velocity about the X, Y, and Z axes in degrees per second in helicopter coordinates.
  /// @param [out] rotVel Rotational velocity about the X, Y, and Z axes in degrees per second in helicopter coordinates.
  /// (-1e6 if not available)
  /// @return OKAY if successful, otherwise an error code.
  Status GetRotVel(std::array<float, 3>& rotVel) const;

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
  /// @brief Construct a SenderUDP object that will send to a specific endpoint.
  /// @param [in] host Name of the host to send to.
  /// @param [in] port Port number to send to.
  /// @param [in] broadcast Whether to use the broadcast address from the specified address.
  /// @param [in] NICName Name of the network interface to use for sending, "" for system chooses.
  /// @param [in] multicastName Name of the multicast group to join (default none).
  SenderUDP(std::string host, uint16_t port, bool broadcast = false, std::string const& NICName = "",
    std::string multicastName = "");

  /// @brief Construct a SenderUDP object that will send to a specific endpoint.
  /// @param [in] endpoint Endpoint to send to.
  /// @param [in] broadcast Whether to use the broadcast address from the specified address.
  /// @param [in] NICName Name of the network interface to use for sending, "" for system chooses.
  /// @param [in] multicastName Name of the multicast group to join (default none).
  SenderUDP(const StreamEndpoint& endpoint, bool broadcast = false, std::string const& NICName = "",
    std::string multicastName = "");

  /// @brief Send a buffer full of data.
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
  std::shared_ptr<Socket> m_socket; ///< Pointer to the socket object to use to do our work.
};

//---------------------------------------------------------------------------
/// @brief Class used to send packets to a file. Used internally by CoreClient and CoreServer.

class SenderFile : public Sender {
public:
  /// @brief Construct a SenderFile object that will send to a specific endpoint.
  /// @param [in] fileName Name of the file to write to.
  /// @param [in] directWrite Whether to write directly to the file or buffer the writes.
  /// This does not have an effect on Windows, but on Linux, direct writes are faster
  /// to SSDs.  However, direct writes require that all wriaes be in multiples of the
  /// block size, so the client must ensure this if they set directWrite to true.
  SenderFile(std::string fileName, bool directWrite = false);

  /// @brief Destructor.
  ~SenderFile() override;

  /// @brief Send a buffer full of data.
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
  int m_file;    ///< File object to write to.
};

//---------------------------------------------------------------------------
/// @brief Base interfaces class for both UDP and file-based packet stream receipt.

class Receiver {
public:
  /// @brief Construct a Receiver object.
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  Receiver(uint32_t maxLen = 9000 - 28) : m_constructorStatus(OKAY), m_maxLen(maxLen) {};

  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  virtual ~Receiver() {};

  /// @brief See if a packet is available to receive.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status IsPacketAvailable(double timeout_seconds, bool& available) = 0;

  /// @brief Receive a packet, hanging until one is available.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [out] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the size.
  /// @param [inout] size The size of the buffer to receive.  If the packet is larger than the buffer,
  /// the packet will be truncated and BUFFER_TOO_SMALL will be returned.  If the size of the packet
  /// is smaller than the buffer, the size will be set to the size of the packet.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status ReceiveBuffer(uint8_t* buffer, size_t &size) = 0;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  virtual Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) = 0;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @param [inout] offset Offset into the buffer to start writing the packet, if a non-null
  /// buffer was passed in.  Its initial value is ignored if bufptr is Null.  On successful
  /// return, it will be filled in with the new offset within the buffer that points one past
  /// the end of the StreamPacket.
  /// @param [in] bufptr A buffer to fill in with the incoming packet.  It must be large enough
  /// (after the offset) to receive the largest-sized packet.  If this is nullptr,
  /// a new buffer will be allocated and will be resized to fit exactly the one received packet.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  virtual Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet,
    size_t& offset, std::shared_ptr< std::vector<uint8_t> > bufptr = nullptr) = 0;

  /// @brief Return the status of the constructor.+
  virtual Status GetConstructorStatus() const { return m_constructorStatus; }

protected:
  Status m_constructorStatus;       ///< Reports any errors during construction
  uint32_t m_maxLen;                ///< Maximum length of a packet we can receive.
};

//---------------------------------------------------------------------------
/// @brief Class used to receive UDP packets on a socket.

class ReceiverUDP : public Receiver {
public:
  /// @brief Construct a ReceiverUDP object given a name and port.
  /// @param [in] interfaceName Name of the interface to listen on.
  /// @param [in] port Port number to listen on (default of 0 means any available port).
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  /// @param [in] broadcast Whether to use the broadcast address from the specified address.
  /// @param [in] multicastName Name of the multicast group to join (default none).
  ReceiverUDP(std::string interfaceName = "localhost", uint16_t port = 0,
    uint32_t maxLen = 9000 - 28, bool broadcast = false, std::string multicastName = "");

  /// @brief Construct a ReceiverUDP object given and endpoint.
  /// @param [in] endpoint Endpoint to listen on.
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  /// @param [in] broadcast Whether to use the broadcast address from the specified address.
  /// @param [in] multicastName Name of the multicast group to join (default none).
  ReceiverUDP(const StreamEndpoint& endpoint, uint32_t maxLen = 9000 - 28, bool broadcast = false,
    std::string multicastName = "");

  /// @brief Get the port associated with this receiver in host byte order.
  /// @return The port associated with this receiver, or 0 for failure.
  Status GetPort(uint16_t& port) const { port = m_port;  return OKAY; }

  /// @brief See if a packet is available to receive.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  Status IsPacketAvailable(double timeout_seconds, bool& available) override;

  /// @brief Receive a packet, hanging until one is available.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [out] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the size.
  /// @param [inout] size The size of the buffer to receive.  If the packet is larger than the buffer,
  /// the packet will be truncated and BUFFER_TOO_SMALL will be returned.  If the size of the packet
  /// is smaller than the buffer, the size will be set to the size of the packet.
  /// @return OKAY if successful, otherwise an error code.
  /// @return OKAY if successful, otherwise an error code.
  Status ReceiveBuffer(uint8_t* buffer, size_t& size) override;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) override;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @param [inout] offset Offset into the buffer to start writing the packet, if a non-null
  /// buffer was passed in.  Its initial value is ignored if bufptr is Null.  On successful
  /// return, it will be filled in with the new offset within the buffer that points one past
  /// the end of the StreamPacket.
  /// @param [in] bufptr A buffer to fill in with the incoming packet.  It must be large enough
  /// (after the offset) to receive the largest-sized packet.  If this is nullptr,
  /// a new buffer will be allocated and will be resized to fit exactly the one received packet.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet,
    size_t& offset, std::shared_ptr< std::vector<uint8_t> > bufptr = nullptr) override;

  /// @brief Test function for both this class and the SenderUDP class.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  std::shared_ptr<Socket> m_socket; ///< Pointer to the socket object to use to do our work.
  uint16_t m_port;                  ///< Port number we are listening on in host byte order.
};

//---------------------------------------------------------------------------
/// @brief Class used to read packets from a file that have been streamed there.

class ReceiverFile : public Receiver {
public:
  /// @brief Construct a ReceiverFile object.
  /// @param [in] fileName Name of the file to write to.
  /// @param [in] maxLen Maximum length of a packet to receive (default of 1472 is the maximum for Ethernet).
  ReceiverFile(std::string fileName, uint32_t maxLen = 9000 - 28);

  /// @brief See if a packet is available to receive.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  Status IsPacketAvailable(double timeout_seconds, bool& available) override;

  /// @brief Receive a packet, hanging until one is available.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [out] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the size.
  /// @param [inout] size The size of the buffer to receive.  If the packet is larger than the buffer,
  /// the packet will be truncated and BUFFER_TOO_SMALL will be returned.  If the size of the packet
  /// is smaller than the buffer, the size will be set to the size of the packet.
  /// @return OKAY if successful, otherwise an error code.
  Status ReceiveBuffer(uint8_t* buffer, size_t& size) override;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) override;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @param [inout] offset Offset into the buffer to start writing the packet, if a non-null
  /// buffer was passed in.  Its initial value is ignored if bufptr is Null.  On successful
  /// return, it will be filled in with the new offset within the buffer that points one past
  /// the end of the StreamPacket.
  /// @param [in] bufptr A buffer to fill in with the incoming packet.  It must be large enough
  /// (after the offset) to receive the largest-sized packet.  If this is nullptr,
  /// a new buffer will be allocated and will be resized to fit exactly the one received packet.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet,
    size_t& offset, std::shared_ptr< std::vector<uint8_t> > bufptr = nullptr) override;

  /// @brief Destructor.
  ~ReceiverFile() override;

  /// @brief Test function for both this class and the SenderFile class.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  std::shared_ptr<std::ifstream> m_file;    ///< Pointer to the file object to read from.
};

//---------------------------------------------------------------------------
/// @brief Class used to send and receive via TCP. Used internally by CoreClient and CoreServer.
///
/// The class constructors are used to create a connection to a specified endpoint.  To
/// listen on a port for incoming TCP connections, use the TCPListener factory class to
/// construct one or more of these objects.

class SenderReceiverTCP : public Sender, public Receiver {
public:
  //=================================================================================
  // Sender overrides

  /// @brief Construct a SenderReceiverTCP object that will connect to a specific endpoint.
  /// @param [in] host Name of the host to connect to.
  /// @param [in] port Port number to connect to.
  SenderReceiverTCP(std::string host, uint16_t port);

  /// @brief Construct a SenderReceiverTCP object that will connect to a specific endpoint.
  /// @param [in] endpoint Endpoint to connect to.
  SenderReceiverTCP(const StreamEndpoint& endpoint);

  Status GetConstructorStatus() const override { return Receiver::m_constructorStatus; }

  /// @brief Send a buffer full of data.
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

  /// @brief Get the local IP address associated with this sender.
  /// @param [out] IP local IP address associated with this sender in host byte order.
  /// @return OKAY if successful, otherwise an error code.
  Status GetIP(uint32_t& IP) const;

  /// @brief Get the local port associated with this sender, which will be determined by the OS.
  /// @param [out] port Local port associated with this sender in host byte order.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

  //=================================================================================
  // Receiver overrides not already covered above (GetPort overrides both)

  /// @brief See if a packet is available to receive.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] available True if a packet is available, false if not.
  /// @return OKAY if successful, otherwise an error code.
  Status IsPacketAvailable(double timeout_seconds, bool& available) override;

  /// @brief Receive a packet, hanging until one is available.
  ///
  /// This is not usually called by the calling program, which will call one of the ReceivePacket() functions
  /// for CommandPacket or StreamPacket instead.
  /// Use IsPacketAvailable() to see if a packet is available before calling this function.
  /// @param [out] buffer A buffer to fill in with the incoming packet.  It must be large enough
  /// to receive the size.
  /// @param [inout] size The size of the buffer to receive.  If the packet is larger than the buffer,
  /// the packet will be truncated and BUFFER_TOO_SMALL will be returned.  If the size of the packet
  /// is smaller than the buffer, the size will be set to the size of the packet.
  /// @return OKAY if successful, otherwise an error code.
  Status ReceiveBuffer(uint8_t* buffer, size_t& size) override;

  /// @brief Allocates a new CommandPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received CommandPacket, nullptr if timeout or error.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveCommandPacket(double timeout_seconds, std::shared_ptr<CommandPacket>& packet) override;

  /// @brief Allocates a new StreamPacket and fills it in with the received data.
  /// @param [in] timeout_seconds Timeout in seconds to wait for a packet.
  /// @param [out] packet The received StreamPacket, nullptr if timeout or error.
  /// @param [inout] offset Offset into the buffer to start writing the packet, if a non-null
  /// buffer was passed in.  Its initial value is ignored if bufptr is Null.  On successful
  /// return, it will be filled in with the new offset within the buffer that points one past
  /// the end of the StreamPacket.
  /// @param [in] bufptr A buffer to fill in with the incoming packet.  It must be large enough
  /// (after the offset) to receive the largest-sized packet.  If this is nullptr,
  /// a new buffer will be allocated and will be resized to fit exactly the one received packet.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status ReceiveStreamPacket(double timeout_seconds, std::shared_ptr<StreamPacket>& packet,
    size_t& offset, std::shared_ptr< std::vector<uint8_t> > bufptr = nullptr) override;

protected:
  std::shared_ptr<Socket> m_socket; ///< Pointer to the socket object to use to do our work.
  uint32_t m_IP;                    ///< IP address to send to.
  uint16_t m_port;                  ///< Port number to send to.

  /// @brief Constuctor used by TCPListener to create a new connection.
  SenderReceiverTCP(std::shared_ptr<Socket> socket, uint32_t IP, uint16_t port);

  /// @brief Set the socket behavior to be low latency and anyting else desired.
  /// @return OKAY if successful, otherwise an error code.
  Status SetSocketOptions();

  friend class TCPListener;
};

//---------------------------------------------------------------------------
/// @brief Class used to listen for incoming TCP connections.

class TCPListener {
public:
  /// @brief Construct a TCPListener object that will listen on a specific endpoint.
  /// @param [in] endpoint Endpoint to listen on.
  /// @param [in] numListeners Number of incoming simulataneous connections to allow.
  TCPListener(const StreamEndpoint& endpoint, uint32_t numListeners = 2);

  /// @brief Return the status of the constructor.
  /// @return Constructor status.
  Status GetConstructorStatus() const { return m_constructorStatus; }

  /// @brief Get the local port associated with this sender, which will be determined by the OS.
  /// @param [out] port Local port associated with this sender in host byte order.
  /// @return OKAY if successful, otherwise an error code.
  Status GetPort(uint16_t& port) const;

  /// @brief Accept an incoming connection and return a new connection.
  /// @param [out] newConnection Pointer to the new connection object.  Is nullptr on timeout or error.
  /// @param [in] timeoutSeconds Timeout in seconds to wait for a connection.
  /// @return OKAY if successful, TIMEOUT on timeout, otherwise an error code.
  Status AcceptConnection(std::shared_ptr<SenderReceiverTCP> &newConnection, float timeoutSeconds);

  /// @brief Test function for both this class and the SenderReceiverTCP class.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  Status m_constructorStatus;       ///< Reports any errors during construction
  std::shared_ptr<Socket> m_socket; ///< Pointer to the socket object to use to do our work.
  uint32_t m_IP;                    ///< IP address to send to.
  uint16_t m_port;                  ///< Port number to send to.
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
/// It is not thread-safe.

class StreamWriter {
public:
  /// @brief Construct a StreamWriter object.
  /// @details This constructor creates a StreamWriter object that will generate
  /// a sequence of packets to send to a client.  It allocates the first packet
  /// of the specified size and sets the sequence number to 0.
  /// @param [in] sender Pointer to the sender object to use to send the packets.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  StreamWriter(std::shared_ptr<Sender> sender, uint32_t maxPayloadSize = 9000 - 28);

  /// @brief Virtual destructor so all derived class pointers will destroy properly.
  ///
  /// Flushes the current packet before destroying.
  virtual ~StreamWriter();

  /// @brief Return the status of the constructor.
  /// @return Constructor status.
  Status GetConstructorStatus() const;

  /// @brief Get the current packet being filled in.
  /// @param [out] packet The current packet being used.
  /// @return OKAY if successful, otherwise an error code.
  Status GetCurrentPacket(std::shared_ptr<StreamPacket>& packet) const;

  /// @brief Inserts an externally-defined packet into the stream of packets.
  /// @details Any partially-filled packet is flushed before the new packet is inserted.
  /// The sequence number on the inserted packet is changed to the next sequence number
  /// in the stream and then the inserted packet is sent immediately.  The sequence number
  /// for the next current packet is incremented.
  /// @param [in] packet The packet to insert into the stream.
  /// @return OKAY if successful, otherwise an error code.
  Status InsertPacket(StreamPacket &packet);

  /// @brief Flush the current packet, sending it and getting a new one.
  /// @details The current packet is sent and a new one is created.  The sequence number
  /// is incremented for the new packet.  If there are no messages in the current packet,
  /// nothing is done.
  /// @return OKAY if successful, otherwise an error code.
  Status Flush();

  /// @brief Test function.
  /// @return Empty string if successful, otherwise descriptive error message.
  static std::string Test();

protected:
  Status m_constructorStatus;         ///< Reports any errors during construction
  std::shared_ptr<Sender> m_sender;   ///< Pointer to the sender object to use to send the packets.
  uint32_t m_maxPayloadSize;          ///< Maximum size of the packet payload to send.
  uint32_t m_sequenceNumber;          ///< Sequence number for the next packet to send.
  std::shared_ptr<StreamPacket> m_currentPacket; ///< Current packet being built.
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

  /// @brief Get the timer that can be used to convert local time into Core time.
  /// @param [out] timer Pointer to the timer object to use to get the time code.
  /// @return OKAY if successful, otherwise an error code.
  virtual Status GetTimer(std::shared_ptr<Timer>& timer) const;

protected:
  /// @brief Construct a Core object.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
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
/// Starts sending periodic discovery packets to clients, and creates a Receiver
/// to get commands from clients and a Sender for sending Messages other than
/// Frame Image messages to the client. This class is not used when streaming
/// from disk files, only when using network connections.

class CoreServer : public Core {
public:
  /// @brief Construct a CoreServer object.
  /// 
  /// This constructor creates a CoreServer object that will send discovery packets
  /// to clients and listen for command packets from clients.  The calling program
  /// must construct and use its own StreamWriter objects to send messages to clients
  /// and handle their creation and shutdown in response commands from the client.
  /// @param [in] serial Serial number of the server.
  /// @param [in] NICName Name of the network interface to use.
  /// @param [in] sendPort Port number to send Discovery packets to.
  /// @param [in] listenPort Port number to listen for Command packets on.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  CoreServer(uint32_t serial, const std::string &NICName, uint16_t sendPort = 10102, uint16_t listenPort = 10101,
    uint32_t maxPayloadSize = 9000 - 28);

  /// @brief Class to hold the information about a client that has connected.
  struct ClientInfo {
    std::shared_ptr<SenderReceiverTCP> stream; ///< Pointer to the SenderReceiverTCP object for the client.
    uint16_t major = 0;                        ///< Major version number of the client.
    uint16_t minor = 0;                        ///< Minor version number of the client.
    uint16_t patch = 0;                        ///< Patch version number of the client.
  };

  /// @brief Get TCP links to new clients that have been established since the last time this function was called.
  /// @param [out] newLinks Vector of new SenderReceiverTCP objects that have been established.
  /// @return OKAY if successful, otherwise an error code.
  Status GetNewTCPLinks(std::vector< std::shared_ptr<ClientInfo> >& newLinks);

  /// @brief Get the status of the discovery thread.
  /// @param [out] threadStatus Stores the thread status.
  /// @return OKAY if successful, otherwise an error code.
  Status GetDiscoveryThreadStatus(Status& threadStatus) const;

  /// @brief Virtual destructor to enable subclases to be detroyed properly.
  virtual ~CoreServer();

protected:
  CoreServer() = delete;
  CoreServer(const CoreServer&) = delete;
  CoreServer& operator=(const CoreServer&) = delete;
  CoreServer(CoreServer&&) = delete;
  CoreServer& operator=(CoreServer&&) = delete;

  std::shared_ptr<std::thread> m_discoveryThread; ///< Thread that sends discovery packets.
  void DiscoveryThread();           ///< Thread that sends discovery packets.
  std::atomic_bool m_threadStarted; ///< Thread uses to let us know that has started running.
  std::atomic_bool m_stopThread;    ///< Flag to tell the thread to stop.
  std::atomic<Status> m_threadStatus;            ///< Status of the thread.
  std::recursive_mutex m_mutex;     ///< Mutex to protect the new links vector.

  std::shared_ptr<SenderUDP> m_discoverySender;     ///< Sender object to use to send Discovery packets.
  std::shared_ptr<TCPListener> m_listener;          ///< Listener object to use to listen for incoming connections.
  std::vector< std::shared_ptr<ClientInfo> > m_newStreams;  ///< Client connections established since the last request.

  uint32_t m_IP;                                    ///< IP address for the client to connect to.
  uint16_t m_port;                                  ///< Port number for a client to connect to.
  uint32_t m_serial;                                ///< Serial number of the server.
};

//---------------------------------------------------------------------------
/// @brief Core Client class, which provides functions needed to implement a client.
///
/// Handles polling for connections on an interface and listing available servers.
/// Also provides function to send command packets to a connected server.
/// Its methods are thread safe, so can be called from different threads (interleaving
/// commands, for example).

class CoreClient : public Core {
public:
  /// @brief Construct a CoreClient object.
  /// @param [in] NICName Name of the network interface to use.
  /// @param [in] listenPort Port number to listen for Discovery packets on.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  CoreClient(std::string NICName, uint16_t listenPort = 10102, uint32_t maxPayloadSize = 9000 - 28);

  /// @brief Get the status of the discovery thread.
  /// @param [out] threadStatus Stores the thread status.
  /// @return OKAY if successful, otherwise an error code.
  Status GetDiscoveryThreadStatus(Status& threadStatus) const;

  /// @brief Get the list of servers found, returning just their connection strings.
  /// @param [out] servers List of servers found.
  /// @return OKAY if successful, otherwise an error code.
  Status IdentifiedServers(std::vector<std::string>& servers) const;

  /// @brief Get the list of servers found as a map from serial number to connection string.
  /// @param [out] servers Map of servers found, with serial number as key and connection string as value.
  /// @return OKAY if successful, otherwise an error code.
  Status IdentifiedServers(std::map<uint32_t, std::string>& servers) const;

  /// @brief Connect to a server.
  /// @param [in] serverURL Server to connect to.
  /// @param [out] major Major version number of the server.
  /// @param [out] minor Minor version number of the server.
  /// @param [out] patch Patch version number of the server.
  /// @return OKAY if successful, otherwise an error code.
  Status ConnectToServer(std::string serverURL, uint16_t &major, uint16_t &minor, uint16_t &patch);

  /// @brief Disconnect from any connected server.  It is not an error to call this if not connected.
  /// @return OKAY if successful, otherwise an error code.
  Status DisconnectFromServer();

  /// @brief Get the IP address of the NIC we're using to talk with the server.
  /// @param [out] IP IP address of the NIC we're using to talk with the server.
  /// @return OKAY if successful, otherwise an error code.
  Status GetMyIP(uint32_t& IP) const;

  /// @brief Get the serial number of the server we're connected to.
  /// @param [out] serial Serial number of the server we're connected to.
  /// @return OKAY if successful, otherwise an error code.
  Status GetServerSerialNumber(uint32_t& serial) const;

  /// @brief Get the Receiver that will get all but Frame Info messages from the server.
  /// @param [out] receiver Receiver that will get all but Frame Info messages from the server.
  /// @return OKAY if successful, otherwise an error code.
  Status GetMainStreamReceiver(std::shared_ptr<Receiver>& receiver) const;

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

  std::shared_ptr<std::thread> m_discoveryThread; ///< Thread that listens for discovery packets.
  void DiscoveryThread();                         ///< Thread that listens for discovery packets.
  std::atomic_bool m_threadStarted; ///< Thread uses to let us know that has started running.
  std::atomic_bool m_stopThread;                  ///< Flag to tell the thread to stop.
  Status m_threadStatus;                          ///< Status of the thread.

  /// @brief Struct to describe the information about a server we've found.
  struct ServerInfo {
    uint32_t IP;                                  ///< IP address of the server in host byte order.
    uint16_t port;                                ///< Port number of the server in host byte order.
    uint32_t serial;                              ///< Serial number of the server.

    /// @brief Constructor.
    /// @param [in] pIP IP address of the server in host byte order.
    /// @param [in] pPort Port number of the server in host byte order.
    /// @param [in] pSerial Serial number of the server.
    ServerInfo(uint32_t pIP, uint32_t pPort, uint32_t pSerial) : IP(pIP), port(pPort), serial(pSerial) {}

    /// @brief Test for equality.
    bool operator==(const ServerInfo& rhs) const {
      return IP == rhs.IP && port == rhs.port && serial == rhs.serial;
    }

    /// @brief Test for inequality.
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

  /// Mutex to protect the list of servers and other internal state.
  /// Marked mutable so it can be used in const member functions.
  mutable std::mutex m_mutex;
  std::vector<ServerInfo> m_servers;                ///< List of servers found.
  std::shared_ptr<SenderReceiverTCP> m_stream;      ///< Stream to use to send and receive packets.
  std::shared_ptr<ReceiverUDP> m_discoveryReceiver; ///< Receiver object to use to receive Discovery packets.
  uint32_t m_IP;                                    ///< Our IP address where we're listening for packets.
  uint32_t m_serial;                                ///< Serial number of the server we're connected to.
};

//---------------------------------------------------------------------------
/// @brief Base class for servers, adding command parsing and helper function to CoreServer.
///
/// This is a virtual base class that can be used to implement a server.
/// It handles the bookkeeping for the command packets and provides a
/// number of helper functions to handle the commands.
/// 
/// It should be tested by running the Base_Server example program and then running
/// the Base_Validating_Client example program.

class CoreServerBase : public CoreServer {
public:
  /// @brief Constructor
  /// @param serialNumber The serial number of the server
  /// @param [in] NICName Name of the network interface to use.
  /// @param [in] sendPort Port number to send Discovery packets to.
  /// @param [in] listenPort Port number to listen for Command packets on.
  /// @param [in] maxPayloadSize Maximum size of a packet payload to send.
  /// @param verbosity The verbosity level of the server, 0 for no verbosity, higher for more verbosity.
  /// Verbosity above 0 will print messages to std::cout.
  CoreServerBase(uint32_t serialNumber, const std::string &NICName,
    uint16_t sendPort = 10102, uint16_t listenPort = 10101,
    uint32_t maxPayloadSize = 9000 - 28, int verbosity = 0);

  ~CoreServerBase() override {};

  /// @brief Run the server, not returning unless an error occurs.  Not expected to be overridden.
  ///
  /// This function runs the server, handling all incoming commands and
  /// returning only when an error occurs.  The error message is returned.
  /// A derived class can override this function, but it will then need
  /// to poll for and parse incoming commands itself.  It is better to
  /// override the command functions below to handle the commands.
  /// @return Error message when the server stops running.
  virtual std::string run();

protected:
  /// @brief Hold per-client state information.
  class ClientState {
  public:
    std::shared_ptr<ClientInfo> m_client;   ///< The client information from connection
    std::shared_ptr<StreamWriter> m_writer; ///< The StreamWriter for the client
    Time m_clockPeriod;                     ///< The between clock events
    Time m_lastClockSent;                   ///< The time the last clock was sent
    Time m_statePeriod;                     ///< The period for state streaming
    Time m_lastStateSent;                   ///< The time the last state was sent
    uint8_t m_eventVerbosity;               ///< The minimum numbered priority of events to send
    bool m_streamingTemperatures;           ///< Are we streaming temperature data?
    bool m_streamingPoses;                  ///< Are we streaming poses?

    /// @brief Constructor
    /// @param [in] client Client to talk with.
    /// @param [in] writer StreamWriter to use to talk with the client.
    ClientState(std::shared_ptr<ClientInfo> client, std::shared_ptr<StreamWriter> writer)
      : m_client(client), m_writer(writer)
      , m_clockPeriod(0.1), m_lastClockSent({ 0, 0 })
      , m_statePeriod(0.5), m_lastStateSent({ 0, 0 }), m_eventVerbosity(0)
      , m_streamingTemperatures(false), m_streamingPoses(false) {};

    /// @brief Equality operator just judges by the client pointer
    bool operator==(const ClientState& rhs) const {
      return m_client.get() == rhs.m_client.get();
    }

    /// @brief Inequality operator
    bool operator!=(const ClientState& rhs) const {
      return !(*this == rhs);
    }

    /// @brief Less than operator just judges by the client pointer (enables map insertion)
    bool operator<(const ClientState& rhs) const {
      return m_client.get() < rhs.m_client.get();
    }

  protected:
    ClientState() = delete;
  };

  /// @brief Function to be called every time through the run loop, override in derived class.
  ///
  /// This function is called every time through the run loop.  It can be used
  /// to service devices, implement periodic tasks that are not handled by
  /// the base class (streaming subregions, temperatures or poses), or to do other things
  /// that need to be done every time through the run loop.
  /// The default implementation does nothing.
  /// 
  /// The function should set m_error to non empty to indicate an error, which
  /// will cause the run() function to exit.
  virtual void doEveryLoop() {};

  /// @brief Function called after a new client is added, override in derived class.
  ///
  /// This can be used to set up the client state, or to do other things that
  /// need to be done when a new client is added.
  /// 
  /// The function should set m_error to non empty to indicate an error, which
  /// will cause the run() function to exit.
  virtual void clientAdded(ClientState& client) {};

  /// @brief Function called before a client is removed, override in derived class.
  ///
  /// This can be used to clean up per-camera UDP senders related to the client or
  /// perform other cleanup before the client is removed.
  /// 
  /// The function should set m_error to non empty to indicate an error, which
  /// will cause the run() function to exit.
  virtual void clientBeingRemoved(ClientState& client) {};

  /// @brief Can be used to indicate an error state by internal methods.
  ///
  /// The run() method can watch this and return an error message if it is set.
  /// Subclasses can set this to indicate an error state and their run() method
  /// can handle it, or this can be ignored.  Note that some of the helper classes
  /// and methods below will set this if they encounter an error.
  std::string m_error;

  int m_verbosity; ///< The verbosity level of the server, 0 for no verbosity, higher for more verbosity.

  // State variables that are per server.  These should be overridden in the constructor if needed
  // and maintained by the derived class.  These are used to fill in the state packet.
  std::vector<FeatureID> m_features;        ///< The features of the server (filled in by derived class)
  std::vector<CameraInfo> m_cameras;        ///< Cameras available on the server (filled in by derived class)
  uint32_t m_numTemperaturesPerCamera;      ///< Number of temperature sensors per camera (filled in by derived class)
  uint32_t m_numSystemTemperatures;         ///< Number of system temperature sensors (filled in by derived class)
  std::atomic<uint8_t> m_storing;           ///< The state of storing (filled in by derived class)
  std::atomic<uint8_t> m_camerasStreaming;  ///< The state of cameras streaming (filled in by derived class)
  std::atomic<uint8_t> m_replaying;         ///< The state of replaying (filled in by derived class)
  uint8_t m_replayAtEnd;                    ///< The state of replay at end (filled in by derived class)
  uint8_t m_recordOnReset;                  ///< The state of start-up recording (filled in by derived class)
  std::vector<TriggerInfo> m_triggers;      ///< Trigger information (filled in by derived class)
  uint64_t m_totalDiskSpace;                ///< Total disk space (filled in by derived class)
  uint64_t m_remainingDiskSpace;            ///< Free disk space (filled in by derived class)
  Time m_streamReplayTime;                  ///< The current replay time (filled in by derived class)

  /// A list of current clients that we will receive commands from and send responses to.
  std::vector<ClientState> m_clients;

  //=============================================================================
  // There are a number of sets of StreamWriters that are created and managed by the server.
  // These are the StreamWriters for the various types of streams.  There can be more
  // than one writer for each type of stream.  This set of structures and associated
  // methods manage these streams.  They can be used as is by a derived class, or
  // ignored (and virtual functions overridden) to provide a different implementation.

  std::recursive_mutex m_mutex;     ///< Mutex to protect operations on internal structures.

  /// @brief Helper method to send an invalid-command event to the event stream.
  /// 
  /// This base-class implementation uses the m_eventWriters list to send the event.
  /// A derived class that does not use the above list will need to override this
  /// method to send the event message in a different way.
  /// @param opCode The invalid command opcode.
  /// @param client The client that the command is coming from.
  virtual void sendInvalidCommandMessage(OpCode opCode, ClientState& client);

  /// @brief Helper method to send an unrecognized-opcode event to the event stream.
  /// 
  /// This base-class implementation uses the m_eventWriters list to send the event.
  /// A derived class that does not use the above list will need to override this
  /// method to send the event message in a different way.
  /// @param opCode The invalid command opcode.
  /// @param client The client that the command is coming from.
  virtual void sendUnrecognizedOpcodeMessage(OpCode opCode, ClientState& client);

  /// @brief Helper method to send state message to the event stream.
  /// @param client The client that the command is coming from.
  virtual Status SendStateMessage(ClientState& client);

  /// @brief Helper method to send clock-sync message to the event stream.
  /// @param client The client that the command is coming from.
  virtual Status SendClockSyncMessage(ClientState& client);

  //=============================================================================
  // Methods to implement the commands. NOTE: These are each implemented to send
  // an invalid-command event message.  A derived class will need to override these
  // methods to implement the actual command.  It can leave the ones as is for
  // commands supporting features that it does not have.
  // These include the client that they are coming from, in case there is the need
  // to perform per-client actions.

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doReset(const CommandPacketReset& command, ClientState& client) {
    sendInvalidCommandMessage(RESET, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStartRecording(const CommandPacketStartRecording& command, ClientState& client) {
    sendInvalidCommandMessage(START_RECORDING, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStopRecording(const CommandPacketStopRecording& command, ClientState& client) {
    sendInvalidCommandMessage(STOP_RECORDING, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doSetStartUpRecordingState(const CommandPacketSetStartUpRecordingState& command, ClientState& client) {
    sendInvalidCommandMessage(SET_START_UP_RECORDING_STATE, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStartReplay(const CommandPacketStartReplay& command, ClientState& client) {
    sendInvalidCommandMessage(START_REPLAY, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doPauseReplay(const CommandPacketPauseReplay& command, ClientState& client) {
    sendInvalidCommandMessage(PAUSE_REPLAY, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doResumeReplay(const CommandPacketResumeReplay& command, ClientState& client) {
    sendInvalidCommandMessage(RESUME_REPLAY, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStopReplay(const CommandPacketStopReplay& command, ClientState& client) {
    sendInvalidCommandMessage(STOP_REPLAY, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doSetStreamStatePeriod(const CommandPacketSetStreamStatePeriod& command, ClientState& client);

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doSetNUCFlagState(const CommandPacketSetNUCFlagState& command, ClientState& client) {
    sendInvalidCommandMessage(SET_NUC_FLAG_STATE, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStartOnCameraNUC(const CommandPacketStartOnCameraNUC& command, ClientState& client) {
    sendInvalidCommandMessage(START_ON_CAMERA_NUC, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doConfigureTrigger(const CommandPacketConfigureTrigger& command, ClientState& client) {
    sendInvalidCommandMessage(CONFIGURE_TRIGGER, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doSoftwareTrigger(const CommandPacketSoftwareTrigger& command, ClientState& client) {
    sendInvalidCommandMessage(SOFTWARE_TRIGGER, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doSetEventVerbosity(const CommandPacketSetEventVerbosity& command, ClientState& client);

  /// @brief Implement the specified command.
  virtual void doStreamSubregion(const CommandPacketStreamSubregion& command, ClientState& client) {
    sendInvalidCommandMessage(STREAM_SUBREGION, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doCancelSubregion(const CommandPacketCancelSubregion& command, ClientState& client) {
    sendInvalidCommandMessage(CANCEL_SUBREGION, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doEraseAllStoredStreams(const CommandPacketEraseAllStoredStreams& command, ClientState& client) {
    sendInvalidCommandMessage(ERASE_ALL_STORED_STREAMS, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doListStoredStreams(const CommandPacketListStoredStreams& command, ClientState& client) {
    sendInvalidCommandMessage(LIST_STORED_STREAMS, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doEraseStoredStream(const CommandPacketEraseStoredStream& command, ClientState& client) {
    sendInvalidCommandMessage(ERASE_STORED_STREAM, client);
  }

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStreamTemperatures(const CommandPacketStreamTemperatures& command, ClientState& client);

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doCancelTemperatures(const CommandPacketCancelTemperatures& command, ClientState& client);

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doStreamPoses(const CommandPacketStreamPoses& command, ClientState& client);

  /// @brief Implement the specified command.
  /// @param command The command packet to implement.
  /// @param client The client that the command is coming from.
  virtual void doCancelPoses(const CommandPacketCancelPoses& command, ClientState& client);
};

//---------------------------------------------------------------------------
/// @brief Test function that verifies that all classes and functions are working.
/// @return Empty string if successful, otherwise descriptive error message.
std::string Test();

} // namespace asdp

/// @brief Operator to send a StreamEndpoint to an ostream.
std::ostream& operator<<(std::ostream& os, const asdp::StreamEndpoint& endpoint);
