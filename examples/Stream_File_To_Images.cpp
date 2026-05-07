/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// This is parser for a stored stream file.  It saves a series of images named based on the
// time of the image begin frame.

#include <string.h>
#include <iostream>
#include <cstdio>
#include <chrono>
#include <cstring>
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

void usage(const std::string& programName)
{
  std::cerr << "Usage: " << programName << " [--sequential] [--autoScale] [--scaleStd <low> <high>] <filename> <imageBaseFileName>" << std::endl;
  std::cerr << "  <filename> - The name of the file to parse." << std::endl;
  std::cerr << "  <imageBaseFileName> - The base name for the images to be saved." << std::endl;
  std::cerr << "  --sequential - Use sequential numbering for the image file names instead of time-based names." << std::endl;
  std::cerr << "  --autoScale - Automatically scale the pixel values in the image so that the specified number of standard deviations below the mean is 0 and the specified number of standard deviations above the mean is 65535." << std::endl;
  std::cerr << "  --scaleStd <low> <high> - When auto-scaling, the number of standard deviations below the mean to map to 0 and above the mean to map to 65535.  The default is 1.5 and 1.5." << std::endl;
}

int main(int argc, char** argv)
{
  std::string fileName, imageBaseFileName;
  bool sequential = false;
  bool autoScale = false;
  double scaleStdLow = 1.5;
  double scaleStdHigh = 1.5;
  int frameNum = 0;
  size_t realParams = 0;

  // Parse the command line arguments, with the first non-flag argument being the
  // name of the file to parse.
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--sequential") == 0) {
      sequential = true;
    } else if (strcmp(argv[i], "--autoScale") == 0) {
      autoScale = true;
    } else if (strcmp(argv[i], "--scaleStd") == 0) {
      if (i + 2 >= argc) {
        usage(argv[0]);
        return 1;
      }
      scaleStdLow = std::stod(argv[++i]);
      scaleStdHigh = std::stod(argv[++i]);
    } else if (argv[i][0] == '-' ) {
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
        usage(argv[0]);
        return 1;
    }
  }
  if (realParams != 2) {
    usage(argv[0]);
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
        case CONSOLIDATED_FRAME_DATA:
          {
            uint16_t imageWidth, imageHeight;

            // Read the time and resolution of the image.  If the begin-frame time is nonzero use it, otherwise
            // use the message time.
            MessageConsolidatedFrameData frameData(*msg);
            Time time;
            status = frameData.GetFirstPixelTime(time);
            if (status != OKAY) {
              std::cerr << "Error reading first pixel time: " << ErrorMessage(status) << std::endl;
              return 9;
            }
            if (time == Time()) {
              status = frameData.GetTime(time);
              if (status != OKAY) {
                std::cerr << "Error reading frame time: " << ErrorMessage(status) << std::endl;
                return 9;
              }
            }
            status = frameData.GetSensorWidth(imageWidth);
            if (status != OKAY) {
              std::cerr << "Error reading frame width: " << ErrorMessage(status) << std::endl;
              return 10;
            }
            status = frameData.GetSensorHeight(imageHeight);
            if (status != OKAY) {
              std::cerr << "Error reading frame height: " << ErrorMessage(status) << std::endl;
              return 11;
            }

            // If this is the beginning of the frame, we open the file and prepare to write the image.
            bool isBeginFrame;
            status = frameData.GetBeginFrameFlag(isBeginFrame);
            if (status != OKAY) {
              std::cerr << "Error reading begin frame: " << ErrorMessage(status) << std::endl;
              return 12;
            }

            // Generate the image file name and construct the storage for the image.
            if (isBeginFrame) {
              if (sequential) {
                // Pad with zeroes to make the frame number fixed-width.  This makes the file names sort in time order.
                std::string numberStr = std::to_string(frameNum);
                numberStr = std::string(6 - numberStr.length(), '0') + numberStr;
                imageFileName = imageBaseFileName + "_" + numberStr + ".pgm";
                frameNum++;
              } else {
                // Construct strings for the seconds and microseconds, padding with zeros to make them fixed-width.
                // This makes the file names sort in time order.
                std::string secondsStr = std::to_string(time.seconds);
                secondsStr = std::string(10 - secondsStr.length(), '0') + secondsStr;
                std::string microsecondsStr = std::to_string(time.microseconds);
                microsecondsStr = std::string(6 - microsecondsStr.length(), '0') + microsecondsStr;
                imageFileName = imageBaseFileName + "_" + secondsStr + "_" + microsecondsStr + ".pgm";
              }
              imageBuffer.resize(int(imageWidth)* imageHeight);
              std::memset(imageBuffer.data(), 0, imageBuffer.size() * sizeof(uint16_t));
              std::cout << "Reading image " << imageFileName << " (" << imageWidth << "x" << imageHeight << ")" << std::endl;
            }

            // Only do this if we have an image name.
            if (!imageFileName.empty()) {
              // Find the region and data for the image and copy it to the appropriate location in the image buffer.
              uint16_t left, right, top, bottom;
              status = frameData.GetLeft(left);
              if (status != OKAY) {
                std::cerr << "Error reading frame data left: " << ErrorMessage(status) << std::endl;
                return 13;
              }
              status = frameData.GetRight(right);
              if (status != OKAY) {
                std::cerr << "Error reading frame data right: " << ErrorMessage(status) << std::endl;
                return 14;
              }
              status = frameData.GetTop(top);
              if (status != OKAY) {
                std::cerr << "Error reading frame data top: " << ErrorMessage(status) << std::endl;
                return 15;
              }
              status = frameData.GetBottom(bottom);
              if (status != OKAY) {
                std::cerr << "Error reading frame data bottom: " << ErrorMessage(status) << std::endl;
                return 16;
              }
              uint16_t width = right - left + 1;
              for (uint16_t row = top; row <= bottom; ++row) {
                uint8_t* dataPtr;
                status = frameData.GetDataPointer(dataPtr, row - top);
                if (status != OKAY) {
                  std::cerr << "Error reading frame data pointer: " << ErrorMessage(status) << std::endl;
                  return 17;
                }
                memcpy(imageBuffer.data() + (row * width + left), dataPtr, width * sizeof(uint16_t));
              }
            }

            bool isEndFrame;
            status = frameData.GetEndFrameFlag(isEndFrame);
            if (status != OKAY) {
              std::cerr << "Error reading end frame: " << ErrorMessage(status) << std::endl;
              return 18;
            }

            // If we have an image name and this is the end of the frame, write the image to the file.
            if (isEndFrame && !imageFileName.empty()) {
              // If we've been asked to auto-gain the image, find the mean and standard deviation of the
              // pixels values and scale so that the specified number of standard deviations below the mean is 0 and
              // the specified number of standard deviations above the mean is 65535.
              if (autoScale) {
                double sum = 0;
                double sumSquares = 0;
                size_t count = 0;
                for (uint16_t value : imageBuffer) {
                  sum += value;
                  sumSquares += value * value;
                  count++;
                }
                double mean = sum / count;
                double stddev = sqrt(sumSquares / count - mean * mean);
                double scale = 65535 / ((scaleStdHigh + scaleStdLow) * stddev);
#pragma omp parallel for
                for (uint16_t& value : imageBuffer) {
                  int scaledValue = int((value - mean + scaleStdLow * stddev) * scale);
                  if (scaledValue < 0) scaledValue = 0;
                  if (scaledValue > 65535) scaledValue = 65535;
                  value = uint16_t(scaledValue);
                }
              }

              // Write the image.
              std::cout << "  Writing image to " << imageFileName << std::endl;
              fixEndian(imageBuffer);
              FILE *f = fopen(imageFileName.c_str(), "wb");
              if (f == NULL) {
                std::cerr << "Error opening image file " << imageFileName << std::endl;
                return 19;
              }
              fprintf(f, "P5\n%d %d\n%d\n", imageWidth, imageHeight, 65535);
              fwrite(imageBuffer.data(), sizeof(uint16_t), imageBuffer.size(), f);
              fclose(f);
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
