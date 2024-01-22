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
  SOCKET_ERROR                  = 1007
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

//---------------------------------------------------------------------------
/// @brief Test function that verifies that all classes and functions are working.
/// @return Empty string if successful, otherwise descriptive error message.
std::string Test();

} // namespace asdp