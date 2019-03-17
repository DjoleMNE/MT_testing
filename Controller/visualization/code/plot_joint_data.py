# Author(s): Djordje Vukcevic
# Year: 2019
import sys, os
import numpy as np
import sympy as sp
from sympy import *
import matplotlib.pyplot as plt
import pyinotify

variable_num = 7
filename = "../joint_torques.txt"

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
wdd = wm.add_watch(filename, pyinotify.IN_CLOSE_WRITE)

with open(filename, "r") as f:
    all_data = [x.split() for x in f.readlines()]
    input_data = np.array(all_data)

rows = np.shape(input_data)[0] - 1
print("Data size: ", rows)

joint_0 = []
joint_1 = []
joint_2 = []
joint_3 = []
joint_4 = []
joint_5 = []
joint_6 = []

for sample_ in range(1, rows - 1):
    joint_0.append( np.float32( input_data[sample_][0] ) )
    joint_1.append( np.float32( input_data[sample_][1] ) )
    joint_2.append( np.float32( input_data[sample_][2] ) )
    joint_3.append( np.float32( input_data[sample_][3] ) )
    joint_4.append( np.float32( input_data[sample_][4] ) )
    joint_5.append( np.float32( input_data[sample_][5] ) )
    joint_6.append( np.float32( input_data[sample_][6] ) )

samples = np.arange(0, rows, 1)
joint_0 = np.array(joint_0)
joint_1 = np.array(joint_1)
joint_2 = np.array(joint_2)
joint_3 = np.array(joint_3)
joint_4 = np.array(joint_4)
joint_5 = np.array(joint_5)
joint_6 = np.array(joint_6)

joint_0_limit = np.array(np.full((rows, ), np.float32( input_data[0][0])))
joint_1_limit = np.array(np.full((rows, ), np.float32( input_data[0][1])))
joint_2_limit = np.array(np.full((rows, ), np.float32( input_data[0][2])))
joint_3_limit = np.array(np.full((rows, ), np.float32( input_data[0][3])))
joint_4_limit = np.array(np.full((rows, ), np.float32( input_data[0][4])))
joint_5_limit = np.array(np.full((rows, ), np.float32( input_data[0][5])))
joint_6_limit = np.array(np.full((rows, ), np.float32( input_data[0][6])))


plt.ion()
plt.figure(figsize = (18,10))
plt.gca().set_axis_off()
plt.subplots_adjust(hspace = 0.02, wspace = 15)
plt.subplots_adjust(left=0.05, right=0.99, top=0.99, bottom=0.03)
plt.margins(0,0)

plt.subplot(7, 1, 1)
plt.plot(joint_0, c = 'green', label='joint_1_torque', linewidth = 0.5, zorder = 2)
plt.plot(joint_0_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_0_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.subplot(7, 1, 2)
plt.plot(joint_1, c = 'green', label='joint_2', linewidth = 0.5, zorder = 2)
plt.plot(joint_1_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_1_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.subplot(7, 1, 3)
plt.plot(joint_2, c = 'green', label='joint_3', linewidth = 0.5, zorder = 2)
plt.plot(joint_2_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_2_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.subplot(7, 1, 4)
plt.plot(joint_3, c = 'green', label='joint_4', linewidth = 0.5, zorder = 2)
plt.plot(joint_3_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_3_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.subplot(7, 1, 5)
plt.plot(joint_4, c = 'green', label='joint_5', linewidth = 0.5, zorder = 2)
plt.plot(joint_4_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_4_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.subplot(7, 1, 6)
plt.plot(joint_5, c = 'green', label='joint_6', linewidth = 0.5, zorder = 2)
plt.plot(joint_5_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_5_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.subplot(7, 1, 7)
plt.plot(joint_6, c = 'green', label='joint_7', linewidth = 0.5, zorder = 2)
plt.plot(joint_6_limit, c = 'red', label='max_torque_limit', linewidth = 2.3, zorder = 1)
plt.plot(-1 *joint_6_limit, c = 'blue', label='min_torque_limit', linewidth = 2.3, zorder = 1)
plt.legend(loc = 1, fontsize = 'x-large')
plt.grid(True)

plt.draw()
plt.pause(0.001)
plt.savefig('../joint_torques.pdf')

notifier.loop()