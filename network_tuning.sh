#!/bin/bash
# Configure the network cards on a Linux system to provide sufficient throughput
# for 60fps UDP transmission of 9000-byte packets from up to 25 cameras.  This
# script is for the specific network cards installed on the ASDP servers.

# Grab the Mellanox tune utility and run it if we haven't done so already
if [ ! -d ~/mlnx-tools ]; then
  git clone https://github.com/Mellanox/mlnx-tools ~/mlnx-tools
  sudo python3 ~/mlnx-tools/python/mlnx_tune -p HIGH_THROUGHPUT
  echo "You need to reboot and then run this script again to get things working."
fi

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

