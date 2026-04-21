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
import math
import random
import numpy as np
from scipy.spatial.transform import Rotation as R

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
    print("Make_Camera_Config_File.py version 3.1.0");

    parser = argparse.ArgumentParser(description="Generate a camera configuration file for a specified number of cameras.")
    parser.add_argument('--output', type=str, default='camConfig.json', help='Output JSON file name (default: camConfig.json)')
    parser.add_argument('--serial', type=int, default=1, help='Camera serial number (default: 1)')
    parser.add_argument('--radial', type=float, default=0.14, help='Camera radial displacement meters (default: 0.14)')
    parser.add_argument('--num_x', type=int, default=7, help='Number of cameras in X (default: 7)')
    parser.add_argument('--num_y', type=int, default=3, help='Number of cameras in Y (default: 3)')
    parser.add_argument('--pixels_x', type=int, default=1280, help='Number of pixels in X (default: 1280)')
    parser.add_argument('--pixels_y', type=int, default=1024, help='Number of pixels in Y (default: 1024)')
    parser.add_argument('--fov_h', type=float, default=40.0, help='Horizontal camera field of view deg (default: 40)')
    parser.add_argument('--fov_v', type=float, default=32.4686, help='Vertical camera field of view deg (default: 32.4686)')
    # 27 degrees horizontal rotation and 35 vertical in the construction specifications
    parser.add_argument('--overlap_x', type=float, default=5.5, help='Camera overlap in X direction deg (default: 5.5)')
    parser.add_argument('--overlap_y', type=float, default=5.0, help='Camera overlap in Y direction deg (default: 5)')
    parser.add_argument('--simulation', action='store_true', help='Generate simulation oversize and distortion')
    parser.add_argument('--identical', action='store_true', help='Generate simulation view identical to original view')
    parser.add_argument('--wide_field', action='store_true', help='Generate wide-field cameras for depth estimation')
    parser.add_argument('--crop_min_x', type=int, default=0, help='Minimum crop X (default: 0 for whole image, 1 crops 1)')
    parser.add_argument('--crop_max_x', type=int, default=-1, help='Maximum crop X (default: -1 for whole image, -2 crops 1)')
    parser.add_argument('--crop_min_y', type=int, default=0, help='Minimum crop Y (default: 0 for whole image, 1 crops 1)')
    parser.add_argument('--crop_max_y', type=int, default=-1, help='Maximum crop Y (default: -1 for whole image, -2 crops 1)')
    parser.add_argument('--tweak_rot', type=float, default=0.0, help='Rotation tweak amount deg (default: 0.0)')
    parser.add_argument('--tweak_pos', type=float, default=0.0, help='Position tweak amount mm (default: 0.0)')
    parser.add_argument('--tweak_distortion', type=float, default=0.0, help='Distortion tweak amount percent (default: 0.0)')
    parser.add_argument('--color_offset', type=float, default=0.0, help='Offset to add to color (default: 0.0)')
    parser.add_argument('--color_gain', type=float, default=1.0, help='Gain for color (default: 1.0)')
    parser.add_argument('--stills_arrangement', action='store_true', help='Generate camera arrangement for IR stills')
    parser.add_argument('--orig_arrangement', action='store_true', help='Generate camera arrangement for the original simulation layout')
    parser.add_argument('--add_rot_x', type=float, default=0.0, help='Additional rotation around X axis deg (default: 0.0)')
    parser.add_argument('--add_rot_y', type=float, default=0.0, help='Additional rotation around Y axis deg (default: 0.0)')
    parser.add_argument('--add_rot_z', type=float, default=0.0, help='Additional rotation around Z axis deg (default: 0.0)')
    parser.add_argument('--upside_down', action='store_true', help='Flip the camera upside down')
    parser.add_argument('--flip_parity', type=int, default=0, help='Parity of per-column camera rotation, set 1 for flipped')
    
    args = parser.parse_args()

    builtArrangement = not args.orig_arrangement
    
    # Generate the configuration data, serial number and then cameras.
    data = {}
    data["serialNumber"] = args.serial
    data["cameras"] = []
    camID = 1
    for y in range(args.num_y):
        for x in range(args.num_x):
            # For the as-built arrangement, the camera IDs are assigned with numbers increasing
            # along columns fastest and rows slowest for the first 21 cameras.  The rows go from
            # right to left rather than left to right.  Compute the camID
            # based on the x and y values and ignore the increment at the end of the loop.
            if builtArrangement:
                camID = 1 + ((args.num_x - 1 - x) * args.num_y) + y

            cam = {}
            cam["id"] = camID
            cam["fieldOfViewDegrees"] = [args.fov_h, args.fov_v]
            cam["resolutionPixels"] = [args.pixels_x, args.pixels_y]
            cam["cropPixels"] = { "minX": args.crop_min_x, "maxX": args.pixels_x + args.crop_max_x,
                                  "minY": args.crop_min_y, "maxY": args.pixels_y + args.crop_max_y }
            cam["color"] = { "offset": args.color_offset, "gain": args.color_gain }

            # Odd-numbered columns are rotated with X facing up, even with it facing down, unless we're
            # in stills arrangement.  Also, in stills arrangement the cameras go from left to right
            # rather than right to left.
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
            if builtArrangement:
                # The built arrangement has the vertical axis flipped compared to initial simulations
                desiredVer *= -1
            if x % 2 == args.flip_parity or args.stills_arrangement:
                rx = 90.0
                ry = -90.0 + desiredHor
                rz = 90.0 - desiredVer
            else:
                rx = 90.0
                ry = 90.0 + desiredHor
                rz = -90.0 + desiredVer
            rMag = args.tweak_rot
            rx, ry, rz = nested_rotations(rx, ry, rz,
                                          args.add_rot_x + random.uniform(-rMag, rMag),
                                          args.add_rot_y + random.uniform(-rMag, rMag),
                                          args.add_rot_z + random.uniform(-rMag, rMag))
            # If we're upside down, flip the camera upside down
            if args.upside_down:
                rx, ry, rz = nested_rotations(0.0, 180.0, 0.0, rx, ry, rz)
            cam["orientationDegrees"] = [rx, ry, rz]

            # Compute the position of the camera, which is a radial distance from the origin.
            # Start by computing the normal distance, which is in the space that has X to the
            # right, Y into the screen, and Z up (helicopter space).  This is in spherical
            # coordinates.
            pos = args.radial * rotate_y_axis(desiredHor, desiredVer)
            if args.tweak_pos != 0.0:
                pMag = args.tweak_pos * 1e-3
                pos += np.array([random.uniform(-pMag, pMag), random.uniform(-pMag, pMag), random.uniform(-pMag, pMag)])
            if args.upside_down:
                # Rotate by 180 degrees around the Y axis to flip the position
                pos = np.array([-pos[0], pos[1], -pos[2]])
            cam["positionMeters"] = [pos[0], pos[1], pos[2]]

            # Generate the distortion data, which is unity when we're not simulating and will be filled in by
            # calibration data.
            dMap = [ [0, 0], [5, 5] ]

            # Add default vignette data
            cam["vignette"] = {}
            cam["vignette"]["type"] = "evenPolynomial"
            cam["vignette"]["parameters"] = {}
            cam["vignette"]["parameters"]["COP"] = [0.0, 0.0]
            cam["vignette"]["parameters"]["coefficients"] = [1.0]

            # Modify distortion data and add fields when simulating
            if args.simulation:
                if args.identical:
                    # No distortions and the same field of view and resolution  
                    cam["oversizedResolutionPixels"] = [args.pixels_x, args.pixels_y]
                    cam["oversizedFieldOfViewDegrees"] = [args.fov_h, args.fov_v]
                else:
                    # Computed in radial_distortion.xlsx
                    dMap = [
                      [0,	0],
                      [0.05,	0.050002503],
                      [0.1,	0.100020101],
                      [0.15,	0.150068276],
                      [0.2,	0.200163328],
                      [0.25,	0.250322876],
                      [0.3,	0.300566487],
                      [0.35,	0.350916456],
                      [0.4,	0.401398784],
                      [0.45,	0.452044395],
                      [0.5,	0.502890625],
                      [0.55,	0.553983028],
                      [0.6,	0.605377536],
                      [0.65,	0.657143013],
                      [0.7,	0.709364243],
                      [0.75,	0.762145386],
                      [0.8,	0.815613952],
                      [0.85,	0.869925324],
                      [0.9,	0.925267869],
                      [0.95,	0.981868682],
                      [1,	1.04],
                      [1.05,	1.09998632],
                      [1.1,	1.162212271],
                      [1.15,	1.227131271],
                      [1.2,	1.295275008],
                      [1.25,	1.367263794],
                      [1.3,	1.443817817],
                      [1.35,	1.525769344],
                      [1.4,	1.614075904],
                      [1.45,	1.709834499],
                      [1.5,	1.814296875]
                    ]
                    cam["oversizedResolutionPixels"] = [args.pixels_x * 2, args.pixels_y * 2]
                    cam["oversizedFieldOfViewDegrees"] = [args.fov_h + 10, args.fov_v + 10]
                    cam["color"]["offset"] += int(random.uniform(-0.3*65535, -0.1*65535))
                    cam["color"]["gain"] *= random.uniform(1.45, 1.6)
                    cam["vignette"]["parameters"]["COP"] = [random.uniform(-0.3,0.3), random.uniform(-0.3,0.3)]
                    cam["vignette"]["parameters"]["coefficients"] = [1.0, random.uniform(0.1,0.3)]

            cam["distortion"] = { "type": "radial" }
            COP = [0.0, 0.0]
            if args.tweak_distortion != 0.0:
                gain = random.uniform(1, 1 + args.tweak_distortion/100)
                for i in range(len(dMap)):
                    dMap[i][1] *= gain
            parameters = { "COP": COP, "map": dMap }
            cam["distortion"]["parameters"] = parameters

            # Generate the camera data
            data["cameras"].append(cam)
            camID += 1

    if args.wide_field:
        # Start the wide-field camera IDs after the normal cameras
        camID = 22

        # Determine the total field of regard in the horizontal direction and then divide by
        # two to find the field of regard for each half of the scene.  This is the horizontal
        # field of regard that we need to cover with the wide-field cameras.
        hFOR = args.num_x * args.fov_v - (args.num_x - 1) * args.overlap_x
        hFOR /= 2

        # We rotate by this amount even if we end up needing to scale up to match the vertical field of regard.
        rotationDegrees = hFOR / 2

        # Compute the vertical field of regard that corresponds to this horizontal field of regard,
        # scaling by the aspect ratio of the camera and remembering that that half the number of pixels
        # scales as the tangent of the half field of regard.
        vFOR = 2*math.degrees(math.atan(math.tan(math.radians(hFOR/2) * args.pixels_y/args.pixels_x)))
        print(f"wFOV horizontal wFOV: {hFOR}, vertical wFOV: {vFOR}")

        # If the vertical field of regard is smaller than we need, recompute based on the vertical FOR.
        neededVFOR = (args.num_y * args.fov_h - (args.num_y - 1) * args.overlap_y)
        if vFOR < neededVFOR:
            vFOR = neededVFOR
            hFOR = 2*math.degrees(math.atan(math.tan(math.radians(vFOR/2) * args.pixels_x/args.pixels_y)))
            print(f"Adjusted horizontal field of regard: {hFOR}, vertical field of regard: {vFOR}")

        # @todo We hard-coded the field of view to be the same as the wide-field cameras, but we could
        # use the computed FOR instead.
        hFOR = 110.0
        vFOR = 88.0

        # Add four wide-field cameras, two on each side, each covering the half field of regard
        # on that side.  They have the same fields of view but are offset in space.  They point
        # towards the center of the half field of regard.  They are mounted on the bottom of the
        # camera ball and are not rotated -- they have a wider field of regard than tall.
        for angle in [ -rotationDegrees, rotationDegrees ]:
            # The position is radial distance down and radial distance forward at rotationDegrees,
            # and then slid to the left or right (in its rotated frame) by half the radial distance.
            Z = -args.radial
            forwardX = args.radial * math.sin(math.radians(angle))
            forwardY = args.radial * math.cos(math.radians(angle))
            leftX = forwardY / 2
            leftY = -forwardX / 2

            for sign in [-1, 1]:
                cam = {}
                cam["id"] = camID
                cam["fieldOfViewDegrees"] = [hFOR, vFOR]
                cam["resolutionPixels"] = [args.pixels_x, args.pixels_y]
                cam["cropPixels"] = { "minX": args.crop_min_x, "maxX": args.pixels_x + args.crop_max_x,
                                      "minY": args.crop_min_y, "maxY": args.pixels_y + args.crop_max_y }
                cam["color"] = { "offset": args.color_offset, "gain": args.color_gain }
                cam["orientationDegrees"] = [0, 0, angle]
                cam["positionMeters"] = [forwardX + sign * leftX, forwardY + sign * leftY, Z]
                if args.upside_down:
                    # Rotate by 180 degrees around the Y axis.
                    cam["orientationDegrees"] = [0, 180, -angle]
                    pos = np.array([forwardX + sign * leftX, forwardY + sign * leftY, Z])
                    pos = np.array([-pos[0], pos[1], -pos[2]])
                    cam["positionMeters"] = [pos[0], pos[1], pos[2]]


                # Generate the distortion data.  Make a very wide map to cover the field of view.
                dMap = [ [0, 0], [10, 10] ]

                # Add default vignette data
                cam["vignette"] = {}
                cam["vignette"]["type"] = "evenPolynomial"
                cam["vignette"]["parameters"] = {}
                cam["vignette"]["parameters"]["COP"] = [0.0, 0.0]
                cam["vignette"]["parameters"]["ceofficients"] = [1.0]

                # Modify distortion data and add fields when simulating
                if args.simulation:
                    if args.identical:
                        # No distortions and the same field of view and resolution
                        cam["oversizedResolutionPixels"] = [args.pixels_x, args.pixels_y]
                        cam["oversizedFieldOfViewDegrees"] = [hFOR, vFOR]
                    else:
                        dMap = [ [0, 0],
                                 [0.1, 0.11], [0.2, 0.25], [0.5, 0.71],
                                 [1.0, 1.57], [2.0, 3.43], [5.0, 9.29],
                                 [10.0, 20.0] ]
                        cam["oversizedResolutionPixels"] = [args.pixels_x * 2, args.pixels_y * 2]
                        cam["oversizedFieldOfViewDegrees"] = [hFOR + 20, vFOR + 35]
                        cam["color"]["offset"] += int(random.uniform(-0.3*65535, -0.1*65535))
                        cam["color"]["gain"] *= random.uniform(1.45, 1.6)
                        cam["vignette"]["parameters"]["COP"] = [random.uniform(-0.3,0.3), random.uniform(-0.3,0.3)]
                        cam["vignette"]["parameters"]["ceofficients"] = [1.0, random.uniform(0.1,0.3)]

                cam["distortion"] = { "type": "radial" }
                COP = [0.0, 0.0]
                parameters = { "COP": COP, "map": dMap }
                cam["distortion"]["parameters"] = parameters

                # Generate the camera data
                data["cameras"].append(cam)
                camID += 1

    with open(args.output, 'w') as json_file:
        json.dump(data, json_file, indent=2)
    
    print(f"Data has been written to {args.output} with {len(data['cameras'])} entries.")

if __name__ == "__main__":
    main()
