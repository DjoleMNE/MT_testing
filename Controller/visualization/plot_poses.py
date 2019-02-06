# Author(s): Djordje Vukcevic
# Year: 2019
import sys, os
from mpl_toolkits import mplot3d
from mpl_toolkits.mplot3d import Axes3D
from matplotlib.patches import FancyArrowPatch
from mpl_toolkits.mplot3d import proj3d
import numpy as np
import sympy as sp
import matplotlib
import matplotlib.pyplot as plt
import pyinotify
plt.rcParams['interactive'] 
plt.rcParams['figure.dpi'] = 150 
label_size = 8
plt.rcParams['xtick.labelsize'] = label_size
# Read Pose DATA
measured_pose_path = "/home/djole/Master/Thesis/GIT/MT_testing/Controller/visualization/measured_pose.txt"
predicted_pose_path = "/home/djole/Master/Thesis/GIT/MT_testing/Controller/visualization/predicted_pose.txt"
twist_path = "/home/djole/Master/Thesis/GIT/MT_testing/Controller/visualization/current_twist.txt"

def restart_program(): #restart application
    python = sys.executable
    os.execl(python, python, * sys.argv)

class ModHandler(pyinotify.ProcessEvent):
    # evt has useful properties, including pathname
    def process_IN_CLOSE_WRITE(self, evt):
        print("Data file has changed")
        restart_program()

handler = ModHandler()
wm = pyinotify.WatchManager()
notifier = pyinotify.Notifier(wm, handler)
wdd = wm.add_watch(predicted_pose_path, pyinotify.IN_CLOSE_WRITE)
    
class Arrow3D(FancyArrowPatch):
    def __init__(self, xs, ys, zs, *args, **kwargs):
        FancyArrowPatch.__init__(self, (0,0), (0,0), *args, **kwargs)
        self._verts3d = xs, ys, zs

    def draw(self, renderer):
        xs3d, ys3d, zs3d = self._verts3d
        xs, ys, zs = proj3d.proj_transform(xs3d, ys3d, zs3d, renderer.M)
        self.set_positions((xs[0],ys[0]),(xs[1],ys[1]))
        FancyArrowPatch.draw(self, renderer)
        
def set_axes_radius(ax, origin, radius):
    ax.set_xlim3d([origin[0] - radius, origin[0] + radius])
    ax.set_ylim3d([origin[1] - radius, origin[1] + radius])
    ax.set_zlim3d([origin[2] - radius, origin[2] + radius])

def set_axes_equal(ax):
    '''Make axes of 3D plot have equal scale so that spheres appear as spheres,
    cubes as cubes, etc..  This is one possible solution to Matplotlib's
    ax.set_aspect('equal') and ax.axis('equal') not working for 3D.

    Input
      ax: a matplotlib axis, e.g., as output from plt.gca().
    '''

    limits = np.array([
        ax.get_xlim3d(),
        ax.get_ylim3d(),
        ax.get_zlim3d(),
    ])

    origin = np.mean(limits, axis=1)
    radius = 0.5 * np.max(np.abs(limits[:, 1] - limits[:, 0]))
    set_axes_radius(ax, origin, radius)

with open(measured_pose_path,"r") as f:
    measured_pose = [x.split() for x in f.readlines()]
    measured_pose = np.array(measured_pose)

with open(predicted_pose_path,"r") as f:
    predicted_pose = [x.split() for x in f.readlines()]
    predicted_pose = np.array(predicted_pose)

with open(twist_path,"r") as f:
    twist_vector = [x.split() for x in f.readlines()]
    twist_vector = np.array(np.float32(twist_vector))
    
### Create and visualize your sketch here###    
figure = plt.figure(1, figsize=(10, 10))
ax = figure.add_subplot(111, projection='3d')
axis_length = 0.20
plt.subplots_adjust(bottom=0.0, right=1.0, top=1.0, left = 0.0)

# world frame (plot line from (0,0) to (1,0)) === Base Frame
#plot X axis
ax.plot([0., axis_length], 
        [0., 0.], 
        [0., 0.],  'red', label = "Base: X axis")
#plot Y axis
ax.plot([0., 0.], 
        [0., axis_length], 
        [0., 0.], 'green', label = "Base: Y axis")
#plot Z axis
ax.plot([0., 0.], 
        [0., 0.], 
        [0., axis_length], 'blue', label = "Base: Z axis")

#Current Pose of the end-effector
position_origin_1 = np.array([np.float32(measured_pose[9][0]), np.float32(measured_pose[10][0]), np.float32(measured_pose[11][0])])

R_1 = np.array([[np.float32(measured_pose[0][0]), np.float32(measured_pose[1][0]), np.float32(measured_pose[2][0])], 
                [np.float32(measured_pose[3][0]), np.float32(measured_pose[4][0]), np.float32(measured_pose[5][0])], 
                [np.float32(measured_pose[6][0]), np.float32(measured_pose[7][0]), np.float32(measured_pose[8][0])]])

twist_vector[:3] = np.dot(R_1, twist_vector[:3])
twist_vector[3:] = np.dot(R_1, twist_vector[3:])

# Plot the vector of "commanded" twist
#Linear part
a2 = Arrow3D([position_origin_1[0], position_origin_1[0] + twist_vector[0][0]], 
             [position_origin_1[1], position_origin_1[1] + twist_vector[1][0]], 
             [position_origin_1[2], position_origin_1[2] + twist_vector[2][0]], 
             mutation_scale=10, lw=1, arrowstyle="-|>", color="purple",label='Linear Twist')
ax.add_artist(a2)

#Angular part
a3 = Arrow3D([position_origin_1[0], position_origin_1[0] + twist_vector[3][0]], 
             [position_origin_1[1], position_origin_1[1] + twist_vector[4][0]], 
             [position_origin_1[2], position_origin_1[2] + twist_vector[5][0]], 
             mutation_scale=10, lw=1, arrowstyle="-|>", color="black",label='Angular Twist')
ax.add_artist(a3)

#plot of the current pose
#X axis
ax.plot([position_origin_1[0], position_origin_1[0] +  (axis_length * R_1[0, 0]) ], 
        [position_origin_1[1], position_origin_1[1] +  (axis_length * R_1[1, 0]) ], 
        [position_origin_1[2], position_origin_1[2] +  (axis_length * R_1[2, 0]) ], '--r', label='Current: X axis')

#Y axix
ax.plot([position_origin_1[0], position_origin_1[0] +  (axis_length * R_1[0, 1]) ], 
        [position_origin_1[1], position_origin_1[1] +  (axis_length * R_1[1, 1]) ], 
        [position_origin_1[2], position_origin_1[2] +  (axis_length * R_1[2, 1]) ],  '--g', label='Current: Y axis')

#Y axix
ax.plot([position_origin_1[0], position_origin_1[0] +  (axis_length * R_1[0, 2]) ], 
        [position_origin_1[1], position_origin_1[1] +  (axis_length * R_1[1, 2]) ], 
        [position_origin_1[2], position_origin_1[2] +  (axis_length * R_1[2, 2]) ],  '--b', label='Current: Z axis')



#Predicted Pose of the end-effector
position_origin_2 = np.array([np.float32(predicted_pose[9][0]), np.float32(predicted_pose[10][0]), np.float32(predicted_pose[11][0])])

R_2 = np.array([[np.float32(predicted_pose[0][0]), np.float32(predicted_pose[1][0]), np.float32(predicted_pose[2][0])], 
                [np.float32(predicted_pose[3][0]), np.float32(predicted_pose[4][0]), np.float32(predicted_pose[5][0])], 
                [np.float32(predicted_pose[6][0]), np.float32(predicted_pose[7][0]), np.float32(predicted_pose[8][0])]])

#X axis
ax.plot([position_origin_2[0], position_origin_2[0] +  (axis_length * R_2[0, 0]) ], 
        [position_origin_2[1], position_origin_2[1] +  (axis_length * R_2[1, 0]) ], 
        [position_origin_2[2], position_origin_2[2] +  (axis_length * R_2[2, 0]) ], 'r:', label='Predicted: X axis')

#Y axix
ax.plot([position_origin_2[0], position_origin_2[0] +  (axis_length * R_2[0, 1]) ], 
        [position_origin_2[1], position_origin_2[1] +  (axis_length * R_2[1, 1]) ], 
        [position_origin_2[2], position_origin_2[2] +  (axis_length * R_2[2, 1]) ],  ':g', label='Predicted: Y axis')

#Y axix
ax.plot([position_origin_2[0], position_origin_2[0] +  (axis_length * R_2[0, 2]) ], 
        [position_origin_2[1], position_origin_2[1] +  (axis_length * R_2[1, 2]) ], 
        [position_origin_2[2], position_origin_2[2] +  (axis_length * R_2[2, 2]) ],  ':b', label='Predicted: Z axis')

#Plot the vector of "data estimated" twist
a1 = Arrow3D([position_origin_1[0], position_origin_2[0]], 
             [position_origin_1[1], position_origin_2[1]], 
             [position_origin_1[2], position_origin_2[2]], 
            mutation_scale=10, lw=1, arrowstyle="-|>", color="orange")
ax.add_artist(a1)

ax.legend(loc=3)
ax.set_aspect('equal')
plt.xlabel('x')
plt.ylabel('y')
plt.title('Twist test: {}'.format([twist_vector[0][0], 
                                   twist_vector[1][0], 
                                   twist_vector[2][0], 
                                   twist_vector[3][0], 
                                   twist_vector[4][0],
                                   twist_vector[5][0]]))
set_axes_equal(ax)             # important!

# rotate the axes and update
ax.view_init(25, 105)
plt.show()
# plt.pause(0.001)
# plt.savefig('frames.pdf')
notifier.loop()