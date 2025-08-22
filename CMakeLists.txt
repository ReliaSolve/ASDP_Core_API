cmake_minimum_required(VERSION 3.18.0)
project(ASDP_Core_API LANGUAGES CXX)

#-----------------------------------------------------------------------------
# Set your favorite C++ standard for compiling the internal files.  The library
# will export things as extern "C" so this will not impact client programs.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#-----------------------------------------------------------------------------
if(WIN32)
    add_definitions(-DNOMINMAX)
endif(WIN32)

#-----------------------------------------------------------------------------
# Local CMake Modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#-----------------------------------------------------------------------------
# Enable sorting projects within the solution on Visual Studio, to group
# Test and Example files together.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#-----------------------------------------------------------------------------
# Export libraries along with our DLLs if we want to build shared
# Allow the developer to select if Dynamic or Static libraries are built

# include (GenerateExportHeader)

#-----------------------------------------------------------------------------
# Checking for optional and required packages

find_package(Threads REQUIRED)
find_package(Doxygen)
find_package(OpenMP)

#-----------------------------------------------------------------------------
# Build options.
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TESTS "Build test programs" ON)

if (DOXYGEN_FOUND)
    option(BUILD_DOXYGEN "Build DOxygen documentation" ON)
endif (DOXYGEN_FOUND)

if (BUILD_DOXYGEN)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

    # note the option ALL which allows to build the docs together with the application
    add_custom_target( doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
endif (BUILD_DOXYGEN)

#-----------------------------------------------------------------------------
# Set up build product locations

include(GNUInstallDirs)

# Win-specific: we want shared libs (dlls) in same dir as exe files.
if(WIN32)
	set(asdp_SHARED_LIBRARY_DIR "${CMAKE_INSTALL_BINDIR}")
else()
	set(asdp_SHARED_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}")
endif()
set (asdp_SOURCES
  ASDP_Core_API.cpp
  ASDP_BufferPool.cpp
  ASDP_ClockSynchronizer.cpp
  ASDP_ImageSource.cpp
  ASDP_StreamPacketSortedQueue.cpp
)

#-----------------------------------------------------------------------------
# Build the library.

set (asdp_HEADERS
  ASDP_Core_API.h
  ASDP_BufferPool.h
  ASDP_ClockSynchronizer.h
  ASDP_ImageSource.h
  ASDP_StreamPacketSortedQueue.h
  ASDP_SpinFreeQueue.hpp
  ASDP_SpinFreeAccurateTimer.hpp
)

add_library(ASDP_Core_API STATIC ${asdp_SOURCES} ${asdp_HEADERS})
# Hide all symbols that are not explicitly exported from the library
#set_target_properties(ASDP_Core_API PROPERTIES C_VISIBILITY_PRESET hidden)
#set_target_properties(ASDP_Core_API PROPERTIES CXX_VISIBILITY_PRESET hidden)
#set_target_properties(ASDP_Core_API PROPERTIES VISIBILITY_INLINES_HIDDEN 1)
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
  # Multi-config (e.g., Visual Studio, Xcode)
  target_compile_definitions(ASDP_Core_API PRIVATE BUILD_TYPE="$<CONFIG>")
else()
  # Single-config (e.g., Makefiles, Ninja)
  if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "")
    target_compile_definitions(ASDP_Core_API PRIVATE BUILD_TYPE="${CMAKE_BUILD_TYPE}")
  else()
    target_compile_definitions(ASDP_Core_API PRIVATE BUILD_TYPE="Default")
  endif()
endif()
target_include_directories(ASDP_Core_API PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(ASDP_Core_API PUBLIC
  ${CMAKE_INSTALL_PREFIX}/include
)
target_link_libraries(ASDP_Core_API PUBLIC Threads::Threads)
set_target_properties(ASDP_Core_API PROPERTIES PUBLIC_HEADER "${asdp_HEADERS}")
if(OPENMP_FOUND)
    set_source_files_properties(ASDP_ImageSource.cpp PROPERTIES COMPILE_OPTIONS ${OpenMP_CXX_FLAGS})
    #target_compile_options(ASDP_Core_API PUBLIC ${OpenMP_CXX_FLAGS})
    target_link_libraries(ASDP_Core_API PUBLIC ${OpenMP_CXX_LIB_NAMES})
endif()
# generate_export_header(ASDP_Core_API)

install(TARGETS ASDP_Core_API EXPORT ${PROJECT_NAME}
  RUNTIME DESTINATION bin COMPONENT lib
  LIBRARY DESTINATION lib${LIB_SUFFIX} COMPONENT lib
  ARCHIVE DESTINATION lib${LIB_SUFFIX} COMPONENT lib
  INCLUDES DESTINATION include
  PUBLIC_HEADER DESTINATION include
)
# Make it possible to use the library in a DLL
set_target_properties(ASDP_Core_API PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
#install(FILES ${PROJECT_BINARY_DIR}/ASDP_EXPORT.h
#  DESTINATION include COMPONENT headers
#)

#-----------------------------------------------------------------------------
# Build examples if we've been asked to.

if(BUILD_EXAMPLES)
  set (EXAMPLES
    ASDP_Core_Speed_Measurements
    Basic_Server
    Basic_Client
    Base_Validating_Client
    Storage_Validating_Client
    Base_Server
    Speed_Test_Sender
    Speed_Test_Receiver
    Image_Saving_Client
    Stream_File_Parser
    Core_Module_Stream_Test
    Measure_Camera_Frame_Rate
    Stream_File_To_Images
    Multicast_Test
    Raw_Packet_Receiver
    Store_Raw_Camera_Stream
  )
  foreach (BASE ${EXAMPLES})
    # C++ example
    set (APP ${BASE})
    add_executable (${APP} examples/${BASE}.cpp)
    target_include_directories(${APP} PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_target_properties(${APP} PROPERTIES FOLDER examples/C++)
    target_link_libraries(${APP} ASDP_Core_API)
    #install(TARGETS ${APP} EXPORT ${PROJECT_NAME}
    #  RUNTIME DESTINATION bin
    #)
  endforeach (BASE)

endif(BUILD_EXAMPLES)

#-----------------------------------------------------------------------------
# Build tests if we've been asked to.

if(BUILD_TESTS)

  enable_testing()

  set (CPP_TESTS
    ASDP_Core_Test
  )
  foreach (BASE ${CPP_TESTS})
    set (APP ${BASE})
    add_executable (${APP} tests_cpp/${BASE}.cpp)
    set_target_properties(${APP} PROPERTIES FOLDER tests/C++)
    target_include_directories(${APP} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${APP}
      ASDP_Core_API
    )
    install(TARGETS ${APP} EXPORT ${PROJECT_NAME}
      RUNTIME DESTINATION bin
    )
    add_test(${APP} ${APP})
  endforeach (BASE)

endif(BUILD_TESTS)

# Configure the project for use by client projects using CMake
# This makes the project available as a package for use with find_package()
# and provides the configuration for the build tree.
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  VERSION 1.0
  COMPATIBILITY AnyNewerVersion
)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
install(EXPORT ${PROJECT_NAME}
  FILE ${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
)
#install(FILES
#  "${CMAKE_CURRENT_BINARY_DIR}/ASDP_Core_API_EXPORT.h"
#  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
#)
