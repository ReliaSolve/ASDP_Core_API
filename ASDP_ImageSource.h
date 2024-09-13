/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

 /**
  * @file ASDP_ImageSource.h
  * @brief Provides utility classes that produce sequences of images, serving as artificial cameras
  * for test purposes.
  *
 * @author ReliaSolve.
 * @date April 9th, 2024.
 */

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace asdp {
namespace ImageSource {

/// @brief Class that stores an image and its metadata and can read and write PGM files.
class Image {
public:
  /// @brief Constructor for an image.
  /// @param width The width of the image.
  /// @param height The height of the image.
  /// @param data The image data.
  /// @throws std::invalid_argument If the width or height does not match the size of the data.
  Image(uint16_t width, uint16_t height, std::shared_ptr< std::vector<uint16_t> > data);

  /// @brief Constructor for an image from a file in PGM format.
  /// @param filename The name of the file to read from.
  /// @throws std::invalid_argument If the file cannot be read or is not in PGM format.
  Image(std::string filename);

  /// @brief Get the width of the image.
  /// @return The width of the image.
  uint16_t getWidth() const { return m_width; }

  /// @brief Get the height of the image.
  /// @return The height of the image.
  uint16_t getHeight() const { return m_height; }

  /// @brief Get the image data.
  /// @return The image data.
  std::shared_ptr< std::vector<uint16_t> > getData() const { return m_data; }

  /// @brief Write the image to a file in PGM format.
  /// @param filename The name of the file to write to.
  /// @return True if the image was written successfully, false otherwise.
  bool writeToPGM(const std::string& filename) const;

  /// @brief Test this class.
  /// @return An empty string on success, an error message on failure.
  static std::string Test();

protected:
  uint16_t m_width;                                 ///< The width of the image.
  uint16_t m_height;                                ///< The height of the image.
  std::shared_ptr< std::vector<uint16_t> > m_data;  ///< The image data.

  /// @brief Read the image from a file in PGM format.
  /// @param filename The name of the file to read from.
  /// @return True if the image was read successfully, false otherwise.
  bool readFromPGM(const std::string& filename);
};

/// @brief Base class for image sources that defines the required methods.
class ImageSource {
public:
  /// @brief Get the next image from the source.
  /// @param loop If true, the source will loop back to the beginning when it reaches the end.
  /// @return The next image from the source, or nullptr if at the end.
  virtual std::shared_ptr<Image> getNextImage(bool loop = true) = 0;

  /// @brief Get the width of the images.
  /// @return The width of the images.
  virtual uint16_t getWidth() const { return m_width; }

  /// @brief Get the height of the images.
  /// @return The height of the images.
  virtual uint16_t getHeight() const { return m_height; }

protected:
  /// @brief Constructor for an image source.
  /// @param width The width of the images.
  /// @param height The height of the images.
  ImageSource(uint16_t width, uint16_t height)
    : m_width(width), m_height(height) {};

  ImageSource() = delete;  ///< No default constructor.

  uint16_t m_width;             ///< The width of the images.
  uint16_t m_height;            ///< The height of the images.
};

/// @brief Class that produces a sequence of images that are a moving set of bars.
class MovingBarsSource : public ImageSource {
public:
  /// @brief Constructor for a moving bars image source.
  /// @param width The width of the images.
  /// @param height The height of the images.
  /// @param numImages The number of images in the sequence.
  /// @param numBars The number of bars in the images.
  /// @param barWidth The width of the bars in the images.
  /// @param speed The speed of the bars in pixels per frame.
  /// @param darkValue The value for the dark parts of the bars.
  /// @param brightValue The value for the bright parts of the bars.
  MovingBarsSource(uint16_t width, uint16_t height, size_t numImages,
    uint16_t numBars, uint16_t barWidth, uint16_t speed,
    uint16_t darkValue = 0, uint16_t brightValue = UINT16_MAX);

  /// @brief Get the next image from the source.
  /// @param loop If true, the source will loop back to the beginning when it reaches the end.
  /// @return The next image from the source.
  std::shared_ptr<Image> getNextImage(bool loop = true) override;

  /// @brief Test this class.
  /// @return An empty string on success, an error message on failure.
  static std::string Test();

protected:
  std::vector< std::shared_ptr<Image> > m_images;  ///< The images in the sequence.
  size_t m_currentIndex;        ///< The index of the current image.
};

/// @brief Tests all classes in the file.
std::string Test();

} // namespace IS
} // namespace asdp
