#!/bin/bash
# Configure the network cards on a Linux system to provide sufficient throughput
# for 60fps UDP transmission of 9000-byte packets from up to 25 cameras.  This
# script is for the specific network cards installed on the ASDP servers.

# Enable jumbo packets on each interface
echo "Two of the following commands will fail, due to different names on different hosts"
sudo ifconfig enp1s0f0np0 mtu 9000
sudo ifconfig enp1s0f1np1 mtu 9000
sudo ifconfig enp66s0f0np0 mtu 9000
sudo ifconfig enp66s0f1np1 mtu 9000

# Set the global receive buffer size larger
sudo sysctl -w net.core.rmem_max=26214400

# Set the global UDP buffer pool size larger
sudo sysctl -w net.ipv4.udp_mem=4000000

