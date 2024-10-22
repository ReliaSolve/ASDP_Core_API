/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is parser for a stored stream file.  It saves a series of images named based on the
// time of the image begin frame.

#include <string.h>
#include <iostream>
#include <chrono>
#include <ASDP_Core_API.h>

using namespace asdp;

static bool isBigEndian() {
  union {
    uint32_t i;
    char c[4];
  } testUnion = { 0x01020304 };

  return testUnion.c[0] == 1;
}

static void fixEndian(std::vector<uint16_t>& data) {
  if (!isBigEndian()) {
    for (uint16_t& value : data) {
      value = (value >> 8) | (value << 8);
    }
  }
}

int main(int argc, char** argv)
{
  std::string fileName, imageBaseFileName;
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
      case 1:
        imageBaseFileName = argv[i];
        break;
      default:
        std::cerr << "Usage: " << argv[0] << " <filename> <imageBaseFileName>" << std::endl;
        return 1;
    }
  }
  if (realParams != 2) {
    std::cerr << "Usage: " << argv[0] << " <filename> <imageBaseFileName>" << std::endl;
    return 2;
  }

  // Open a file receiver to read the file.
  ReceiverFile receiver(fileName);
  if (receiver.GetConstructorStatus() != OKAY) {
    std::cerr << "Error opening file " << fileName << ": " << ErrorMessage(receiver.GetConstructorStatus()) << std::endl;
    return 3;
  }

  // Read and parse stream packets from the file, extracting messages and acting on them.
  bool available = false;
  receiver.IsPacketAvailable(0.0, available);
  size_t badPackets = 0;
  uint32_t lastSequenceNumber = 0;
  std::string imageFileName;
  std::vector<uint16_t> imageBuffer;
  uint16_t imageWidth, imageHeight;
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
    if ( (sequenceNumber != 0) && (sequenceNumber != lastSequenceNumber + 1) ) {
      std::cout << "Skipped packets" << std::endl;
    }
    lastSequenceNumber = sequenceNumber;

    // Read and handle each frame message in the packet.
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
      switch (type) {
        case FRAME_BEGIN:
          {
            // Read the time and resolution of the image.
            MessageFrameBegin frameBegin(*msg);
            Time time;
            status = frameBegin.GetTime(time);
            if (status != OKAY) {
              std::cerr << "Error reading frame begin time: " << ErrorMessage(status) << std::endl;
              return 9;
            }
            status = frameBegin.GetSensorWidth(imageWidth);
            if (status != OKAY) {
              std::cerr << "Error reading frame begin width: " << ErrorMessage(status) << std::endl;
              return 10;
            }
            status = frameBegin.GetSensorHeight(imageHeight);
            if (status != OKAY) {
              std::cerr << "Error reading frame begin height: " << ErrorMessage(status) << std::endl;
              return 11;
            }

            // Generate the image file name and construct the storage for the image.
            imageFileName = imageBaseFileName + "_" + std::to_string(time.seconds) + "_" + std::to_string(time.microseconds) + ".pgm";
            imageBuffer.resize(int(imageWidth) * imageHeight);
            std::cout << "Reading image " << imageFileName << " (" << imageWidth << "x" << imageHeight << ")" << std::endl;
          }
          break;

        case FRAME_DATA:
          {
            // Only do this if we have an image name.
            if (!imageFileName.empty()) {
              // Find the region and data for the image and copy it to the appropriate location in the image buffer.
              MessageFrameData frameData(*msg);
              uint16_t left, right, top, bottom;
              uint8_t* dataPtr;
              status = frameData.GetLeft(left);
              if (status != OKAY) {
                std::cerr << "Error reading frame data left: " << ErrorMessage(status) << std::endl;
                return 12;
              }
              status = frameData.GetRight(right);
              if (status != OKAY) {
                std::cerr << "Error reading frame data right: " << ErrorMessage(status) << std::endl;
                return 13;
              }
              status = frameData.GetTop(top);
              if (status != OKAY) {
                std::cerr << "Error reading frame data top: " << ErrorMessage(status) << std::endl;
                return 14;
              }
              status = frameData.GetBottom(bottom);
              if (status != OKAY) {
                std::cerr << "Error reading frame data bottom: " << ErrorMessage(status) << std::endl;
                return 15;
              }
              status = frameData.GetDataPointer(dataPtr);
              if (status != OKAY) {
                std::cerr << "Error reading frame data pointer: " << ErrorMessage(status) << std::endl;
                return 16;
              }
              uint16_t width = right - left + 1;
              for (uint16_t row = top; row <= bottom; ++row) {
                memcpy(imageBuffer.data() + (row * width + left),
                  dataPtr + (row - top) * width * sizeof(uint16_t),
                  width * sizeof(uint16_t));
              }
            }
          }
          break;

        case FRAME_END:
          {
            // If we have an image name, write the image to the file.
            if (!imageFileName.empty()) {
              std::cout << "  Writing image to " << imageFileName << std::endl;
              std::ofstream imageFile(imageFileName, std::ios::binary);
              if (!imageFile) {
                std::cerr << "Error opening image file " << imageFileName << std::endl;
                return 12;
              }
              imageFile << "P5\n" << imageWidth << " " << imageHeight << " " << 1 << "\n" << 65535 << "\n";
              fixEndian(imageBuffer);
              imageFile.write(reinterpret_cast<const char*>(imageBuffer.data()), imageBuffer.size() * sizeof(uint16_t));
              if (!imageFile) {
                std::cerr << "Error writing image file " << imageFileName << std::endl;
                return 13;
              }
              imageFileName.clear();
            }
          }
          break;

        default:
          // Other messages are ignored.
          break;
      }

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

    available = false;
    receiver.IsPacketAvailable(0.0, available);
  }

  if (badPackets) {
    std::cerr << "Error: " << badPackets << " packets had errors." << std::endl;
    return 1000;
  }

  return 0;
}
