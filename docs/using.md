\page Using Using the Core API

This page describes the Core API and provides pointers to example code and also
code snippets to perform some desired actions.

# Error checking

Most functions return a asdp_Status value indicating whether a warning or error occurred during
their call.

The asdp::Status values are defined in \ref asdp_api.h, with OKAY indicating
no warnings or errors.  Values above it and less than or equal to HIGHEST_WARNING
are warnings, and values above it are errors.  The helper function asdp::ErrorMessage()
takes in an asdp::Status and provides a string description of the status.

\example ASDL_Core_Test.cpp

// List the test programs
\example ASDL_Core_Test.cpp
