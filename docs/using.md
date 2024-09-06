\page Using Using the Core API

This page describes the Core API and provides pointers to example code and also
code snippets to perform some desired actions.

# Error checking

Most functions return a asdp_Status value indicating whether a warning or error occurred during
their call.

The asdp::Status values are defined in \ref ASDP_Core_API.h, with OKAY indicating
no warnings or errors.  Values above it and less than or equal to HIGHEST_WARNING
are warnings, and values above it are errors.  The helper function asdp::ErrorMessage()
takes in an asdp::Status and provides a string description of the status.

# Example programs

See the Examples menu for a list of example programs showing how to use and test the system.

\example ASDP_Core_Speed_Measurements.cpp
\example Base_Server.cpp
\example Base_Validating_Client.cpp
\example Basic_Client.cpp
\example Basic_Server.cpp
\example Image_Saving_Client.cpp
\example Speed_Test_Receiver.cpp
\example Speed_Test_Sender.cpp
\example Storage_Validating_Client.cpp
\example Stream_File_Parser.cpp
\example Core_Module_Stream_Test.cpp
\example Measure_Camera_Frame_Rate.cpp

\example ASDP_Core_Test.cpp
