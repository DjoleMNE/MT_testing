/*
Author(s): Djordje Vukcevic, Sven Schneider
Institute: Hochschule Bonn-Rhein-Sieg

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
#include "lwr_rtt_control/lwr_rtt_control.hpp"
const long MILLISECOND = 1000;
const long SECOND = 1000000;

LwrRttControl::LwrRttControl(const std::string& name):
    RTT::TaskContext(name), RATE_HZ_(999), NUM_OF_SEGMENTS_(7), 
    NUM_OF_JOINTS_(7), NUM_OF_CONSTRAINTS_(6), 
    environment_(lwr_environment::LWR_SIMULATION), 
    robot_model_(lwr_model::LWR_URDF), krc_compensate_gravity_(false),
    desired_pose_(1), prediction_dt_sec_(1.0),
    control_dims_(NUM_OF_CONSTRAINTS_, false),
    max_cart_force_(Eigen::VectorXd::Constant(6, 0.0)),
    error_alpha_(Eigen::VectorXd::Constant(6, 0.0)),
    bias_threshold_(Eigen::VectorXd::Constant(6, 0.0)),
    bias_step_(Eigen::VectorXd::Constant(6, 0.0)),
    gain_threshold_(Eigen::VectorXd::Constant(6, 0.0)),
    gain_step_(Eigen::VectorXd::Constant(6, 0.0)), saturate_b_u_(false),
    robot_state_(NUM_OF_JOINTS_, NUM_OF_SEGMENTS_, NUM_OF_SEGMENTS_ + 1, NUM_OF_CONSTRAINTS_)
{
    // Here you can add your ports, properties and operations
    // ex : this->addOperation("my_super_function",&LwrRttControl::MyFunction,this,RTT::OwnThread);
    this->addPort("JointPosition",port_joint_position_in).doc("Current joint positions");
    this->addPort("JointVelocity",port_joint_velocity_in).doc("Current joint velocities");
    this->addPort("JointTorque",port_joint_torque_in).doc("Current joint torques");

    this->addPort("JointPositionCommand",port_joint_position_cmd_out).doc("Command joint positions");
    this->addPort("JointVelocityCommand",port_joint_velocity_cmd_out).doc("Command joint velocities");
    this->addPort("JointTorqueCommand",port_joint_torque_cmd_out).doc("Command joint torques");

//     this->addProperty("environment", environment_).doc("environment");
//     this->addProperty("robot_model", robot_model_).doc("robot_model");
    this->addProperty("krc_compensate_gravity", krc_compensate_gravity_).doc("KRC compensate gravity");
    this->addProperty("desired_pose", desired_pose_).doc("desired pose");

    this->addProperty("control_dims", control_dims_).doc("control dimensions");
    this->addProperty("prediction_dt_sec", prediction_dt_sec_).doc("prediction_dt_sec_");
    this->addProperty("max_cart_force", max_cart_force_).doc("max_cart_force");
    this->addProperty("ERROR_ALPHA", error_alpha_).doc("ABAG ERROR_ALPHA");
    this->addProperty("BIAS_THRESHOLD", bias_threshold_).doc("BIAS_THRESHOLD");
    this->addProperty("BIAS_STEP", bias_step_).doc("BIAS_STEP");
    this->addProperty("GAIN_THRESHOLD", gain_threshold_).doc("GAIN_THRESHOLD");
    this->addProperty("GAIN_STEP", gain_step_).doc("GAIN_STEP");
    this->addProperty("saturate_b_u", saturate_b_u_).doc("saturate_b_u");
}

bool LwrRttControl::configureHook()
{
    rtt_ros_kdl_tools::getAllPropertiesFromROSParam(this);

    jnt_pos_in.setZero(NUM_OF_JOINTS_);
    jnt_vel_in.setZero(NUM_OF_JOINTS_);
    jnt_trq_in.setZero(NUM_OF_JOINTS_);

    jnt_pos_cmd_out.setZero(NUM_OF_JOINTS_);
    jnt_vel_cmd_out.setZero(NUM_OF_JOINTS_);
    jnt_trq_cmd_out.setZero(NUM_OF_JOINTS_);
    jnt_gravity_trq_out.data.setZero(NUM_OF_JOINTS_);

    port_joint_position_cmd_out.setDataSample(jnt_pos_cmd_out);
    port_joint_velocity_cmd_out.setDataSample(jnt_vel_cmd_out);
    port_joint_torque_cmd_out.setDataSample(jnt_trq_cmd_out);

    // Check validity of (all) Ports:
    if ( !port_joint_position_in.connected() || 
         !port_joint_velocity_in.connected() ||
         !port_joint_torque_in.connected() ) 
    {
        RTT::log(RTT::Fatal) << "No input connection!"<< RTT::endlog();
        return false;
    }
    if ( !port_joint_position_cmd_out.connected() ||
         !port_joint_torque_cmd_out.connected()) 
    {
           RTT::log(RTT::Warning) << "No output connection!"<< RTT::endlog();  
    }

    robot_driver_.initialize(robot_model_, environment_, krc_compensate_gravity_);
    assert(NUM_OF_JOINTS_ == robot_driver_.get_robot_model().getNrOfSegments());

    this->gravity_solver_ = std::make_shared<KDL::ChainDynParam>(robot_driver_.get_robot_model(), 
                                                                 KDL::Vector(0.0, 0.0, -9.81289)); 

    this->controller_ = std::make_shared<dynamics_controller>(&robot_driver_, RATE_HZ_);

    //Create End_effector Cartesian Acceleration task 
    controller_->define_ee_acc_constraint(std::vector<bool>{false, false, false, // Linear
                                                            false, false, false}, // Angular
                                          std::vector<double>{0.0, 0.0, 0.0, // Linear
                                                              0.0, 0.0, 0.0}); // Angular
    //Create External Forces task 
    controller_->define_ee_external_force(std::vector<double>{0.0, 0.0, 0.0, // Linear
                                                              0.0, 0.0, 0.0}); // Angular
    //Create Feedforward torques task s
    controller_->define_feedforward_torque(std::vector<double>{0.0, 0.0, 
                                                               0.0, 0.0, 
                                                               0.0, 0.0, 0.0});
    switch (desired_pose_)
    {
        case desired_pose::CANDLE:
                // Candle Pose
            controller_->define_desired_ee_pose(std::vector<bool>{control_dims_[0], control_dims_[1], control_dims_[2], // Linear
                                                                  control_dims_[3], control_dims_[4], control_dims_[5]}, // Angular
                                                std::vector<double>{ 0.0,  0.0, 1.1785, // Linear: Vector
                                                                     1.0,  0.0, 0.0, // Angular: Rotation matrix
                                                                     0.0,  1.0, 0.0,
                                                                     0.0,  0.0, 1.0});
            break;

        case desired_pose::FOLDED:
            // Navigation pose
            controller_->define_desired_ee_pose(std::vector<bool>{control_dims_[0], control_dims_[1], control_dims_[2], // Linear
                                                                  control_dims_[3], control_dims_[4], control_dims_[5]}, // Angular
                                                std::vector<double>{ 0.260912, -0.014731, -0.0945801, // Linear: Vector
                                                                     0.575147,  0.789481, -0.214301, // Angular: Rotation matrix
                                                                     0.174954,  0.137195,  0.974971,
                                                                     0.799122, -0.598245, -0.059216});
            break;

        default:
            // Navigation pose
            controller_->define_desired_ee_pose(std::vector<bool>{control_dims_[0], control_dims_[1], control_dims_[2], // Linear
                                                                  control_dims_[3], control_dims_[4], control_dims_[5]}, // Angular
                                                std::vector<double>{-0.210785, -0.328278,  0.632811, // Linear: Vector
                                                                    -0.540302, -0.841471, -0.000860, // Angular: Rotation matrix
                                                                    -0.841470,  0.540302, -0.001340,
                                                                     0.001592,  0.000000, -0.999999});
            break;
    }

    controller_->set_parameters(prediction_dt_sec_, max_cart_force_, error_alpha_,
                                bias_threshold_, bias_step_, gain_threshold_,
                                gain_step_, saturate_b_u_);

    controller_->initialize(control_mode::TORQUE, true);

    sleep(2); // wait for gazebo to load completely
    return true;
}

void LwrRttControl::updateHook()
{
    // Read status from robot
    port_joint_position_in.read(jnt_pos_in);
    port_joint_velocity_in.read(jnt_vel_in);
    port_joint_torque_in.read(jnt_trq_in);

    robot_state_.q.data  = jnt_pos_in;
    robot_state_.qd.data = jnt_vel_in;
    
    int controller_result = controller_->step(robot_state_.q.data, 
                                              robot_state_.qd.data, 
                                              robot_state_.control_torque.data);

    if(!controller_result == 0) RTT::TaskContext::stop();

    if(krc_compensate_gravity_) jnt_trq_cmd_out = robot_state_.control_torque.data;
    else
    {
        gravity_solver_->JntToGravity(robot_state_.q, jnt_gravity_trq_out);
        jnt_trq_cmd_out = robot_state_.control_torque.data - jnt_gravity_trq_out.data;
    }
    // jnt_trq_cmd_out << 2.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0;
    port_joint_torque_cmd_out.write(jnt_trq_cmd_out);
}

void LwrRttControl::stopHook()
{
    controller_->deinitialize();
    RTT::log(RTT::Error) << "Robot stopped!" << RTT::endlog();
}

// Let orocos know how to create the component
ORO_CREATE_COMPONENT(LwrRttControl)
