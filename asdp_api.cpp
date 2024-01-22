/*
 * Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
 */

// Include the C++ class definitions, which we will use here to implement the
// C functions.
#include "asdp_api.h"

using namespace asdp;

//----------------------------------------------------------------------------
/// Error handling.

std::string ErrorMessage(Status status)
{
  switch (status) {
  case OKAY:
    return "No error";

  case TIMEOUT:
    return "Timeout";

  case BAD_PARAMETER:
    return "Bad parameter";
  case OUT_OF_MEMORY:
    return "Out of memory";
  case NOT_IMPLEMENTED:
    return "Feature not yet implemented";
  case DELETION_FAILED:
    return "Pointer deletion failed";
  case NULL_OBJECT_POINTER:
    return "Object method called with NULL object pointer";
  case INTERNAL_EXCEPTION:
    return "Exception thrown inside implementation";
  case SOCKET_ERROR:
    return "Socekt error";

  default:
    return "Unrecognized error code";
  }
}

//----------------------------------------------------------------------------
// API functions

std::string asdp::Test()
{
  /// @todo Run tests.
  return "@todo Implement tests.";
}
