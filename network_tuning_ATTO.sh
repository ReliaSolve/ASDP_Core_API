#!/bin/bash
# Configure the network cards on a Linux system to provide sufficient throughput
# for 60fps UDP transmission of 9000-byte packets from up to 25 cameras.  This
# script is for the ATTO ThunderLink 5102 device that converts 100-GigE to a
# laptop Thunderbolt connection.

# Enable jumbo packets on each interface
sudo ifconfig enp62s0f0np0 mtu 9000
sudo ifconfig enp62s0f1np1 mtu 9000

# Set the link speed to 50Gbits full duplex
echo "One of the following commands will fail, only one is connected"
sudo ethtool -s enp62s0f0np0 speed 50000 duplex full autoneg off
sudo ethtool -s enp62s0f1np1 speed 50000 duplex full autoneg off

# Set the global receive buffer size larger
sudo sysctl -w net.core.rmem_max=26214400

# Set the global UDP buffer pool size larger
sudo sysctl -w net.ipv4.udp_mem=4000000

