/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

#include "ASDP_ImageSource.h"
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
//#include <iostream> // @todo Remove when done debugging.

// Get htons().
#ifdef _WIN32
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <netinet/in.h>
#endif

using namespace asdp::ImageSource;

Image::Image(uint16_t width, uint16_t height, std::shared_ptr< std::vector<uint16_t> > data)
  : m_width(width), m_height(height), m_data(data)
{
  if (m_data->size() != m_width * m_height) {
    throw std::invalid_argument("Image data size does not match width and height");
  }
};

Image::Image(std::string filename)
{
  // Check the file extension to determine the file type.
  std::string extension = filename.substr(filename.find_last_of(".") + 1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
    [](unsigned char c) { return std::tolower(c); });
  if (extension == "pgm") {
    if (!readFromPGM(filename)) {
      throw std::invalid_argument("Error reading PGM file");
    }
  } else {
    throw std::invalid_argument("Unsupported file type");
  }
}

bool Image::writeToPGM(const std::string& filename) const
{
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  // Write the header
  file.write("P5\n", 3);
  file.write("# Created by Image::writeToFile()\n", 34);
  file.write(std::to_string(m_width).c_str(), std::to_string(m_width).size());
  file.write(" ", 1);
  file.write(std::to_string(m_height).c_str(), std::to_string(m_height).size());
  file.write("\n", 1);
  file.write("65535\n", 6);

  // Write the data in network byte order.
  for (size_t i = 0; i < m_data->size(); i++) {
    uint16_t value = htons(m_data->at(i));
    file.write(reinterpret_cast<const char*>(&value), sizeof(uint16_t));
  }

  // Check for errors and close the file.
  if (file.bad()) {
    file.close();
    return false;
  }

  file.close();
  return true;
}

bool Image::readFromPGM(const std::string& filename)
{
  // Open the file
  std::ifstream file;
  file.open(filename, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  // Read and verify the header.
  std::string header;
  std::getline(file, header);
  if (header != "P5") {
    file.close();
    return false;
  }
  std::string line;
  do {  // Skip comment lines.
    std::getline(file, line);
  } while (line[0] == '#');
  std::istringstream iss(line);
  uint32_t width, height;
  iss >> width >> height;
  if (width == 0 || height == 0) {
    file.close();
    return false;
  }
  uint32_t maxVal;
  file >> maxVal;
  if (maxVal != 65535) {
    file.close();
    return false;
  }

  // Gobble up the newline at the end of the line.
  file.ignore(1);

  // Read the data in network byte order.
  m_width = width;
  m_height = height;
  m_data = std::make_shared< std::vector<uint16_t> >(m_width * m_height);
  for (size_t i = 0; i < m_data->size(); i++) {
    uint16_t value;
    file.read(reinterpret_cast<char*>(&value), sizeof(uint16_t));
    m_data->at(i) = ntohs(value);
  }

  // Check for errors and close the file.
  if (file.bad()) {
    file.close();
    return false;
  }
  file.close();
  return true;
}

std::string Image::Test()
{
  try {
    // Construct a data block for a 100x50 image, filling it with
    // increasing numbers.
    uint16_t width = 100;
    uint16_t height = 50;
    std::shared_ptr< std::vector<uint16_t> > data(new std::vector<uint16_t>(width * height));
    for (size_t i = 0; i < data->size(); i++) {
      data->at(i) = i;
    }

    // Construct an image from the buffer.
    Image image(width, height, data);

    // Write the image to a file.
    std::string filename = "deleteme_test.pgm";
    if (!image.writeToPGM(filename)) {
      return "Error writing PGM file";
    }

    // Read it back from file.
    Image image2(filename);
    if (image2.getWidth() != width || image2.getHeight() != height) {
      remove(filename.c_str());
      return "Error reading PGM file";
    }
    remove(filename.c_str());

    // Test the values to make sure they all match.
    for (size_t i = 0; i < data->size(); i++) {
      if (image2.getData()->at(i) != data->at(i)) {
        return "Mismateched data from PGM file";
      }
    }

  } catch (std::exception& e) {
    return e.what();
  }

  return "";
}

MovingBarsSource::MovingBarsSource(uint16_t width, uint16_t height, size_t numImages,
  uint16_t numBars, uint16_t barWidth, uint16_t speed,
  uint16_t darkValue, uint16_t brightValue)
  : ImageSource(width, height)
  , m_currentIndex(0)
{
  // Create each image in the sequence, filling in the moving bars for each one.
  size_t barSpacing = width / numBars;
  for (size_t i = 0; i < numImages; i++) {
    std::shared_ptr< std::vector<uint16_t> > data(new std::vector<uint16_t>(width * height));
#pragma omp parallel for
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        uint16_t value = darkValue;
        for (uint16_t bar = 0; bar < numBars; bar++) {
          if ((x + i * speed) % barSpacing <= barWidth)  {
            value = brightValue;
          }
        }
        data->at(y * width + x) = value;
      }
    }
    m_images.push_back(std::make_shared<Image>(width, height, data));
  }
}

std::shared_ptr<Image> MovingBarsSource::getNextImage(bool loop)
{
  // If there are no images, return nullptr.
  if (m_images.empty()) {
    return nullptr;
  }

  // If we're not looping and we're at the end, return nullptr.
  if (!loop && (m_currentIndex >= m_images.size() - 1)) {
    return nullptr;
  }

  // If we're past the end, reset the index.
  if (m_currentIndex >= m_images.size()) {
    m_currentIndex = 0;
  }

  // Return the current image and increment the index.
  // It will be adjusted the next time around if we're past the end.
  return m_images[m_currentIndex++];
}

std::string MovingBarsSource::Test()
{
  try {
    // Create a moving-bars image source with 4 images of size 200x50,
    // 25 bars of width 4, moving at 1 pixel per frame.  Set the dark
    // value to 100 and the bright value to 50000.
    uint16_t barWidth = 4;
    uint16_t speed = 1;
    uint16_t width = 200;
    uint16_t height = 50;
    size_t numImages = 4;
    uint16_t numBars = 25;
    uint16_t darkValue = 100;
    uint16_t brightValue = 50000;
    MovingBarsSource source(width, height, numImages, numBars, barWidth, speed, darkValue, brightValue);
    size_t barSpacing = width / numBars;

    // Get the images and check them.
    for (size_t i = 0; i < numImages; i++) {
      std::shared_ptr<Image> image = source.getNextImage();
      if (image == nullptr) {
        return "Error getting image from source";
      }
      if (image->getWidth() != width || image->getHeight() != height) {
        return "Image size mismatch";
      }
      for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
          uint16_t value = darkValue;
          for (uint16_t bar = 0; bar < numBars; bar++) {
            if ((x + i * speed) % barSpacing <= barWidth) {
              value = brightValue;
            }
          }
          if (image->getData()->at(y * width + x) != value) {
            return "Mismatched pixel value at pixel " + std::to_string(x) + "," + std::to_string(y);
          }
        }
      }
    }
  } catch (std::exception& e) {
    return e.what();
  }

  return "";
}

std::string asdp::ImageSource::Test()
{
  std::string ret;
  ret = Image::Test();
  if (ret != "") {
    return "ImageSource test: Error testing Image: " + ret;
  }
  ret = asdp::ImageSource::MovingBarsSource::Test();
  if (ret.size() > 0) {
    return "ImageSource test: Error testing MovingBarsSource: " + ret;
  }

  return "";
}
