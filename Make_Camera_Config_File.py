# Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
#
# Script to generate a camera configuration file for a specified number of cameras.
# This generates a JSON file with the camera configuration information.  It defaults to a
# 21-camera standard configuration with expected camera poses and resolutions for an IR
# camera array.
#
# Options allow the generation of fields to drive simulation, including distortion correction.
# They also allow the generation of an additional 4 wide-field cameras for a total of 25 cameras.

import json
import argparse
import math
import numpy as np

def rotate_y_axis(hor, ver):
    # Convert degrees to radians
    hor_rad = np.radians(hor)
    ver_rad = np.radians(ver)

    # Define the rotation matrix around the Z axis
    Rz = np.array([
        [np.cos(hor_rad), -np.sin(hor_rad), 0],
        [np.sin(hor_rad),  np.cos(hor_rad), 0],
        [0,               0,               1]
    ])

    # Define the rotation matrix around the X axis
    Rx = np.array([
        [1, 0,               0              ],
        [0, np.cos(ver_rad), -np.sin(ver_rad)],
        [0, np.sin(ver_rad),  np.cos(ver_rad)]
    ])

    # Initial unit Y axis vector
    y_axis = np.array([0, 1, 0])

    # Apply the rotations
    y_axis_rotated = Rx @ y_axis  # Rotate point around X axis
    y_axis_rotated = Rz @ y_axis_rotated  # Rotate point around original Z axis

    return y_axis_rotated

def main():
    parser = argparse.ArgumentParser(description="Generate a camera configuration file for a specified number of cameras.")
    parser.add_argument('--output', type=str, default='camConfig.json', help='Output JSON file name (default: camConfig.json)')
    parser.add_argument('--serial', type=int, default=1, help='Camera serial number (default: 1)')
    parser.add_argument('--radial', type=float, default=0.05, help='Camera radial displacement meters (default: 0.05)')
    parser.add_argument('--num_x', type=int, default=7, help='Number of cameras in X (default: 7)')
    parser.add_argument('--num_y', type=int, default=3, help='Number of cameras in Y (default: 3)')
    parser.add_argument('--pixels_x', type=int, default=1280, help='Number of pixels in X (default: 1280)')
    parser.add_argument('--pixels_y', type=int, default=1024, help='Number of pixels in Y (default: 1024)')
    parser.add_argument('--fov_h', type=float, default=40.0, help='Horizontal camera field of view deg (default: 40)')
    parser.add_argument('--fov_v', type=float, default=32.5, help='Vertical camera field of view deg (default: 32.5)')
    parser.add_argument('--overlap_x', type=float, default=5.0, help='Camera overlap in X direction deg (default: 5)')
    parser.add_argument('--overlap_y', type=float, default=5.0, help='Camera overlap in Y direction deg (default: 5)')
    parser.add_argument('--simulation', action='store_true', help='Generate simulation oversize and distortion')
    parser.add_argument('--wide_field', action='store_true', help='Generate wide-field cameras')
    
    args = parser.parse_args()
    
    # Generate the configuration data, serial number and then cameras.
    data = {}
    data["serialNumber"] = args.serial
    data["cameras"] = []
    camID = 1
    for y in range(args.num_y):
        for x in range(args.num_x):
            cam = {}
            cam["id"] = camID
            cam["fieldOfViewDegrees"] = [args.fov_h, args.fov_v]
            cam["resolutionPixels"] = [args.pixels_x, args.pixels_y]

            # Odd-numbered columns are rotated with X facing up, even with it facing down.
            # The transformations are complicated by the fact that our Euler order of operations
            # is XYZ.  We need to rotate around X by 90 or -90 degrees to point straight up or down.
            # We then need to rotate around the the new Y axis by -90 plus the desired Y rotation
            # so that the original X axis will be pointing down.  Finally, we need to rotate around
            # the new Z axis by 90 + the desired vertical rotation.
            # Remember that the cameras are rotated into portrait mode, so FOVs and their offsets are swapped.
            hRatio = (args.fov_v - args.overlap_x) / args.fov_v
            desiredHor = hRatio * (x - (args.num_x - 1)/2.0) * args.fov_v
            vRatio = (args.fov_h - args.overlap_y) / args.fov_h
            desiredVer = vRatio * (y - (args.num_y - 1)/2.0) * args.fov_h
            if x % 2 == 0:
                cam["orientationDegrees"] = [90.0, -90.0 + desiredHor, 90.0 - desiredVer]
            else:
                cam["orientationDegrees"] = [90.0, 90.0 + desiredHor, -90.0 + desiredVer]

            # mpute the position of the camera, which is a radial distance from the origin.
            # Start by computing the normal distance, which is in the space that has X to the
            # right, Y into the screen, and Z up (helicopter space).  This is in spherical
            # coordinates.
            pos = args.radial * rotate_y_axis(desiredHor, desiredVer)   
            cam["positionMeters"] = [pos[0], pos[1], pos[2]]

            # Generate the distortion data, which is unity when we're not simulating and will be filled in by
            # calibration data.
            dMap = [ [0, 0], [5, 5] ]

            # Modify distortion data and add fields when simulating
            if args.simulation:
                dMap = [ [0, 0], [0.1, 0.1], [0.2, 0.21], [0.4, 0.45], [1.0, 1.3], [1.5, 2.2] ]
                cam["oversizedResolutionPixels"] = [args.pixels_x * 2, args.pixels_y * 2]
                cam["oversizedFieldOfViewDegrees"] = [args.fov_h + 10, args.fov_v + 10]

            cam["distortion"] = { "type": "radial" }
            COP = [0.0, 0.0]
            parameters = { "COP": COP, "map": dMap }
            cam["distortion"]["parameters"] = parameters

            # Generate the camera data
            data["cameras"].append(cam)
            camID += 1
    
    with open(args.output, 'w') as json_file:
        json.dump(data, json_file, indent=2)
    
    print(f"Data has been written to {args.output} with {args.num_x * args.num_y} entries.")

if __name__ == "__main__":
    main()
