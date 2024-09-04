/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is parser for a stored stream file.  It prints the times and types of any Messages
// from the server along with the sequence numbers of the packets.  It can be useful for
// debugging and understanding the format of the file.

#include <iostream>
#include <chrono>
#include <ASDP_Core_API.h>

using namespace asdp;

/// @brief Helper function to translate from message ID to a descriptive string.
/// @param type The type to translate.
/// @return A descriptive string for the message type.
static std::string MessageName(MessageID type)
{
  switch (type) {
  case DISCOVERY: return "DISCOVERY";
  case STATE: return "STATE";
  case EVENT: return "EVENT";
  case FRAME_BEGIN: return "FRAME_BEGIN";
  case FRAME_DATA: return "FRAME_DATA";
  case FRAME_END: return "FRAME_END";
  case STORED_STREAMS: return "STORED_STREAMS";
  case TEMPERATURE: return "TEMPERATURE";
  case POSE: return "POSE";
  default: return "UNKNOWN (" + std::to_string(type) + ")";
  }
}

int main(int argc, char** argv)
{
  std::string fileName;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the file to parse.
  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-' ) {
      std::cerr << "Unknown flag: " << argv[i] << std::endl;
      return 1;
    } else switch (realParams++) {
      case 0:
        fileName = argv[i];
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
        return 1;
    }
  }
  if (realParams != 1) {
    std::cerr << "Usage: " << argv[0] << " <filename>" << std::endl;
    return 2;
  }

  // Open a file receiver to read the file.
  ReceiverFile receiver(fileName);
  if (receiver.GetConstructorStatus() != OKAY) {
    std::cerr << "Error opening file " << fileName << ": " << ErrorMessage(receiver.GetConstructorStatus()) << std::endl;
    return 3;
  }

  // Read and parse stream packets from the file, including extracted messages.
  bool available = false;
  receiver.IsPacketAvailable(0.0, available);
  size_t badPackets = 0;
  uint32_t lastSequenceNumber = 0;
  while (available) {
    // Get the next packet.
    std::shared_ptr<StreamPacket> packet;
    size_t offset = 0;
    Status status = receiver.ReceiveStreamPacket(0.0, packet, offset);
    if (status == TIMEOUT) {
      break;
    }
    if (status != OKAY) {
      std::cerr << "Error reading packet: " << ErrorMessage(status) << std::endl;
      return 4;
    }

    // Read and print the packet sequence number and length.
    uint32_t sequenceNumber, length;
    status = packet->GetSequenceNumber(sequenceNumber);
    if (status != OKAY) {
      std::cerr << "Error reading sequence number: " << ErrorMessage(status) << std::endl;
      return 5;
    }
    status = packet->GetTotalLength(length);
    if (status != OKAY) {
      std::cerr << "Error reading packet length: " << ErrorMessage(status) << std::endl;
      return 6;
    }
    std::cout << "Packet " << sequenceNumber << " (size ";
    std::cout << length << "):";

    if ( (sequenceNumber != 0) && (sequenceNumber != lastSequenceNumber + 1) ) {
      std::cout << " (skipped)";
    }
    lastSequenceNumber = sequenceNumber;

    // Read and report the type and time of each message in the packet.
    std::shared_ptr<Message> msg;
    status = packet->GetNextMessage(msg);
    if (status != OKAY) {
      std::cerr << "Error reading message: " << ErrorMessage(status) << std::endl;
      uint32_t size;
      status = packet->GetTotalLength(size);
      if (status == OKAY) {
        std::cerr << "  Packet size: " << size << std::endl;
      } else {
        std::cerr << "  Error reading packet size: " << ErrorMessage(status) << std::endl;
      }
      badPackets++;
    }
    while (msg) {
      MessageID type;
      status = msg->GetType(type);
      if (status != OKAY) {
        std::cerr << "Error reading message type: " << ErrorMessage(status) << std::endl;
        return 7;
      }
      Time time;
      status = msg->GetTime(time);
      if (status != OKAY) {
        std::cerr << "Error reading message time: " << ErrorMessage(status) << std::endl;
        return 8;
      }
      std::cout << " " << MessageName(type) << " @" << time.seconds << ":" << time.microseconds;

      status = packet->GetNextMessage(msg);
      if (status != OKAY) {
        std::cerr << "Error reading message: " << ErrorMessage(status) << std::endl;
        uint32_t size;
        status = packet->GetTotalLength(size);
        if (status == OKAY) {
          std::cerr << "  Packet size: " << size << std::endl;
        } else {
          std::cerr << "  Error reading packet size: " << ErrorMessage(status) << std::endl;
        }
        badPackets++;
      }
    }

    // End of information for this packet.
    std::cout << std::endl;

    available = false;
    receiver.IsPacketAvailable(0.0, available);
  }

  if (badPackets) {
    std::cerr << "Error: " << badPackets << " packets had errors." << std::endl;
    return 1000;
  }

  return 0;
}
