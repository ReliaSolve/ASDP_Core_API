# Copyright (C) 2024: Arizona Board of Regents on Behalf of the University of Arizona
#
# Script to generate a camera configuration file for a specified number of cameras.
# This generates a JSON file with the camera configuration information.  It defaults to a
# 21-camera standard configuration with expected camera poses and resolutions for an IR
# camera array.
#
# Options allow the generation of fields to drive simulation, including distortion correction.
# They also allow the generation of an additional 4 wide-field cameras for a total of 25 cameras.

import builtins
import json
import argparse
import numpy as np
from scipy.spatial.transform import Rotation as R

def nested_rotations(X1, Y1, Z1, X2, Y2, Z2):

    # Create first set of rotations
    rot_1 = R.from_euler('XYZ', [X1, Y1, Z1], degrees=True)

    # Apply the second set of rotations in the new coordinate system
    rot_2 = R.from_euler('XYZ', [X2, Y2, Z2], degrees=True)
    final_rotation = rot_1 * rot_2

    # Get the quaternion
    quaternion = final_rotation.as_quat()

    # Convert the quaternion to Euler angles (XYZ order)
    euler_angles = final_rotation.as_euler('XYZ', degrees=True)

    return euler_angles

def main():
    print("Flip_Camera_Config_File.py version 1.0.0");

    parser = argparse.ArgumentParser(description="Flip a camera configuration file 180 degrees around Y.")
    parser.add_argument('input', type=str, help='Input JSON file name')
    parser.add_argument('output', type=str, help='Output JSON file name')
    
    args = parser.parse_args()

    # Load the input JSON file
    with open(args.input, 'r') as json_file:
        data = json.load(json_file)

    # Flip the positions and orientations of each camera
    for camera in data['cameras']:
        # Flip position
        camera['positionMeters'][0] = -camera['positionMeters'][0]  # Negate X coordinate
        camera['positionMeters'][2] = -camera['positionMeters'][2]  # Negate Z coordinate

        # Flip orientation
        original_euler = camera['orientationDegrees']  # Assuming orientation is in Euler angles [X, Y, Z]
        flipped_euler = nested_rotations(0, 180, 0, original_euler[0], original_euler[1], original_euler[2])
        camera['orientationDegrees'] = flipped_euler.tolist()

    # Write the flipped camera configurations
    with open(args.output, 'w') as json_file:
        json.dump(data, json_file, indent=2)
    
    print(f"Data has been written to {args.output} with {len(data['cameras'])} entries.")

if __name__ == "__main__":
    main()
