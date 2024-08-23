#!/bin/bash

# Define the sets of values for rx-usecs and rx-frames
rx_usecs_values=(8 32 128 256)
rx_frames_values=(16 64 128)

# Network interface
interface="enp66s0f0np0"

# Iterate over the sets of values
for rx_usecs in "${rx_usecs_values[@]}"; do
  for rx_frames in "${rx_frames_values[@]}"; do
    # Apply the ethtool settings
    sudo ethtool -C "$interface" rx-usecs "$rx_usecs" rx-frames "$rx_frames"
    
    # Run the other script and capture its output
    script_output=$(./Core_Module_Stream_Test 10.10.10.31 --duration 60 --summary)
    
    # Write the values and the script output to the log file
    echo "$rx_usecs, $rx_frames, $script_output"
  done
done

