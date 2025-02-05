/*
Author(s): Djordje Vukcevic, Sven Schneider
Institute: Hochschule Bonn-Rhein-Sieg
Description: Mediator component for enabling conversion of data types.
Acknowledgment: This sofware component is based on Jeyaprakash Rajagopal's 
master thesis code.

Copyright (c) [2019]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef YOUBOT_MEDIATOR_HPP
#define YOUBOT_MEDIATOR_HPP
#include <robot_mediator.hpp>
#include <youbot_driver/youbot/YouBotManipulator.hpp>
#include <kdl_parser/kdl_parser.hpp>
#include <urdf/model.h>
#include <youbot_custom_model.hpp>
#include <constants.hpp>
#include <memory>

enum youbot_model 
{
    URDF = 0,
    YB_STORE = 1   
};

enum youbot_environment 
{
    REAL = 0,
    SIMULATION = 1   
};

class youbot_mediator: public robot_mediator
{
	public:
		youbot_mediator();
		~youbot_mediator(){}

		// Initializes variables and calibrates the manipulator
		virtual void initialize(const int robot_model,
								const int robot_environment,
								const int id,
                                const double DT_SEC);
		
		virtual bool is_initialized();

		// Update joint space state: measured positions, velocities and torques
		virtual void get_joint_state(KDL::JntArray &joint_positions,
									 KDL::JntArray &joint_velocities,
									 KDL::JntArray &joint_torques);

		// Update robot state: measured positions, velocities, torques and measured / estimated external forces on end-effector
		virtual void get_robot_state(KDL::JntArray &joint_positions,
                                     KDL::JntArray &joint_velocities,
                                     KDL::JntArray &joint_torques,
                                     KDL::Wrench &end_effector_wrench);

		// Set desired joint commands to move robot and save them for sake of simulation
		virtual int set_joint_command(const KDL::JntArray &joint_positions,
							   const KDL::JntArray &joint_velocities,
							   const KDL::JntArray &joint_torques,
							   const int desired_control_mode);

		// Get current joint positions
		virtual void get_joint_positions(KDL::JntArray &joint_positions);
		// Get current joint velocities 
		virtual void get_joint_velocities(KDL::JntArray &joint_velocities);
		// Get current joint torques
		virtual void get_joint_torques(KDL::JntArray &joint_torques);
		// Get measured / estimated external forces acting on the end-effector
		virtual void get_end_effector_wrench(KDL::Wrench &end_effector_wrench);

		// Set joint position command
		virtual int set_joint_positions(const KDL::JntArray &joint_positions);
		// Set joint velocity command
		virtual int set_joint_velocities(const KDL::JntArray &joint_velocities);
		// Set joint torque command
		virtual int set_joint_torques(const KDL::JntArray &joint_torques); 
		// Set Zero Joint Velocities and wait until robot has stopped completely
		virtual int stop_robot_motion();

		virtual std::vector<double> get_maximum_joint_pos_limits();
		virtual std::vector<double> get_minimum_joint_pos_limits();
		virtual std::vector<double> get_joint_position_thresholds();
		virtual std::vector<double> get_joint_velocity_limits();
		virtual std::vector<double> get_joint_acceleration_limits();
		virtual std::vector<double> get_joint_torque_limits();
		virtual std::vector<double> get_joint_stopping_torque_limits();
		virtual std::vector<double> get_joint_inertia();
		virtual std::vector<double> get_joint_offsets();
		virtual int get_robot_ID();
		
		virtual KDL::Twist get_root_acceleration();
		virtual KDL::Chain get_robot_model();
		virtual KDL::Chain get_full_robot_model();

	private:
		bool is_initialized_;
		const int ROBOT_ID_;
		int youbot_model_;
		int youbot_environment_;

		/*
        	If interface is used with the solver and the custom(youBot store) model: add offsets
        	Else: set the original values
    	*/ // Custom (youBot Store) model's home state is not folded - it is candle
		bool add_offsets_;
		bool connection_established_;

		// Handles for the youbot manipulator and kdl urdf parsel
	    std::shared_ptr<youbot::YouBotManipulator> youbot_arm_;
		KDL::Chain yb_chain_;		
		KDL::Tree yb_tree_;
    	urdf::Model yb_urdf_model_;

		//Arm's root acceleration
		const KDL::Vector linear_root_acc_;
		const KDL::Vector angular_root_acc_;
		const KDL::Twist root_acc_;
        
        // Joint Measured State Variables
        std::vector<youbot::JointSensedAngle> q_measured_;
        std::vector<youbot::JointSensedVelocity> qd_measured_;
        std::vector<youbot::JointSensedTorque> tau_measured_;
        // std::vector<youbot::JointSensedCurrent> current_measured;
        
        // Joint Setpoint Variables
        std::vector<youbot::JointAngleSetpoint> q_setpoint_;
        std::vector<youbot::JointVelocitySetpoint> qd_setpoint_;
        std::vector<youbot::JointTorqueSetpoint> tau_setpoint_;

        //Extract youBot model from urdf file
        int get_model_from_urdf();

		bool get_bit(unsigned int flag, const int position);
		bool robot_stopped();
};
#endif /* YOUBOT_MEDIATOR_HPP */
