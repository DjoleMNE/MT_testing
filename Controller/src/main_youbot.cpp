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
#include <state_specification.hpp>
#include <dynamics_controller.hpp>
#include <fk_vereshchagin.hpp>
#include <geometry_utils.hpp>
#include <model_prediction.hpp>
#include <safety_controller.hpp>
#include <finite_state_machine.hpp>
#include <motion_profile.hpp>
#include <iostream>
#include <utility> 
#include <sstream>
#include <fstream>
#include <chrono>
#include <thread> // std::this_thread::sleep_for
#include <time.h>
#include <cmath>
#include <boost/assign/list_of.hpp>
#include <stdlib.h> /* abs */
#include <unistd.h>
// Eigen
#include <Eigen/Dense>

enum desired_pose
{
    CANDLE       = 0,
    NAVIGATION   = 1,
    NAVIGATION_2 = 2,
    FOLDED       = 3,
    TABLE        = 4,
    CANDLE2      = 5,
    FOLDED2      = 6,
    LOOK_AT      = 7
};

enum path_types
{
    SINE_PATH = 0,
    STEP_PATH = 1,
    INF_SIGN_PATH = 2
};

const long SECOND                    = 1000000;
const int MILLISECOND                = 1000;
const int JOINTS                     = 5;
const int NUMBER_OF_CONSTRAINTS      = 6;
const int desired_dynamics_interface = dynamics_interface::CART_ACCELERATION;
const int motion_profile_id          = m_profile::CONSTANT;
const int abag_error_type            = error_type::SIGN;
const int path_type                  = path_types::STEP_PATH;
int desired_pose_id                  = desired_pose::NAVIGATION;
int environment                      = youbot_environment::SIMULATION;
int robot_model_id                   = youbot_model::URDF;
int desired_task_model               = task_model::full_pose;
int desired_control_mode             = control_mode::VELOCITY;
const double task_time_limit_sec     = 600.0;
double tube_speed                    = 0.01;
const double damper_amplitude        = 2.5;
const bool compansate_gravity        = false;
const bool log_data                  = true;

const std::vector<bool> control_dims      = {true, true, true, // Linear
                                             false, false, false}; // Angular
// Last parameter: Numer of points
const std::vector<double> path_parameters = {0.5, 4.5, 0.05, 0.008, 70};

std::vector<double> tube_start_position   = {0.262105, 0.004157, 0.300};
const std::vector<double> tube_tolerances = {0.001, 0.02, 0.02, 
                                             0.17, 0.17, 0.17, 
                                             0.0, 0.1};

const Eigen::VectorXd max_command         = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 10.0, 10.0, 10.0, 
                                               10.0, 10.0, 10.0).finished();

// Full Pose ABAG parameters
const Eigen::VectorXd error_alpha         = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.900000, 0.900000, 0.900000, 
                                               0.850000, 0.850000, 0.850000).finished();
const Eigen::VectorXd bias_threshold      = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.000407, 0.000407, 0.000407, 
                                               0.001007, 0.001007, 0.001007).finished();
const Eigen::VectorXd bias_step           = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.000495, 0.000495, 0.000495, 
                                               0.003495, 0.003495, 0.003495).finished();
const Eigen::VectorXd gain_threshold      = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.552492, 0.552492, 0.552492, 
                                               0.252492, 0.252492, 0.252492).finished();
const Eigen::VectorXd gain_step           = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.003152, 0.003152, 0.003152, 
                                               0.015152, 0.015152, 0.015152).finished();

// moveTo-torque ABAG parameters
const Eigen::VectorXd error_alpha_2         = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.800000, 0.900000, 0.900000, 
                                               0.850000, 0.850000, 0.850000).finished();
const Eigen::VectorXd bias_threshold_2      = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.000507, 0.000407, 0.000407, 
                                               0.001007, 0.001007, 0.001007).finished();
const Eigen::VectorXd bias_step_2           = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.000495, 0.000495, 0.000495, 
                                               0.003495, 0.003495, 0.003495).finished();
const Eigen::VectorXd gain_threshold_2      = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.452492, 0.552492, 0.552492, 
                                               0.252492, 0.252492, 0.252492).finished();
const Eigen::VectorXd gain_step_2           = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.002052, 0.003152, 0.003152, 
                                               0.015152, 0.015152, 0.015152).finished();

// moveTo-velocity ABAG parameters
const Eigen::VectorXd error_alpha_2_1         = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.800000, 0.900000, 0.900000, 
                                               0.850000, 0.850000, 0.850000).finished();
const Eigen::VectorXd bias_threshold_2_1      = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.000507, 0.000407, 0.000407, 
                                               0.001007, 0.001007, 0.001007).finished();
const Eigen::VectorXd bias_step_2_1           = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.000495, 0.000495, 0.000495, 
                                               0.003495, 0.003495, 0.003495).finished();
const Eigen::VectorXd gain_threshold_2_1      = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.452492, 0.552492, 0.552492, 
                                               0.252492, 0.252492, 0.252492).finished();
const Eigen::VectorXd gain_step_2_1           = (Eigen::VectorXd(NUMBER_OF_CONSTRAINTS) \
                                            << 0.002052, 0.003152, 0.003152, 
                                               0.015152, 0.015152, 0.015152).finished();

const Eigen::VectorXd min_bias_sat                = Eigen::VectorXd::Constant(6, -1.0);
const Eigen::VectorXd min_command_sat             = Eigen::VectorXd::Constant(6, -1.0);
const Eigen::VectorXd null_space_abag_parameters  = Eigen::VectorXd::Constant(5, 0.01);

//  Parameters for weight compensation: K proportional, error-tube, bias-offset,
//                                      bias-variance, gain-variance, bias slope, 
//                                      control-period 
const Eigen::VectorXd compensation_parameters = (Eigen::VectorXd(7) \
                                                << 1.7, 0.025, 0.0,
                                                   0.00016, 0.0025, 0.00002,
                                                   60).finished();

void define_task(dynamics_controller *dyn_controller, const int model_of_task)
{
    desired_task_model = model_of_task;
    std::vector<double> desired_ee_pose(12, 0.0);

    //Create End_effector Cartesian Acceleration task
    dyn_controller->define_ee_acc_constraint(std::vector<bool>{false, false, false, // Linear
                                                               false, false, false}, // Angular
                                             std::vector<double>{0.0, 0.0, 0.0, // Linear
                                                                 0.0, 0.0, 0.0}); // Angular
    //Create External Forces task
    dyn_controller->define_ee_external_force(std::vector<double>{0.0, 0.0, 0.0, // Linear
                                                                 0.0, 0.0, 0.0}); // Angular
    //Create Feedforward torques task
    dyn_controller->define_feedforward_torque(std::vector<double>{0.0, 0.0, 
                                                                  0.0, 0.0, 0.0});

    switch (desired_pose_id)
    {
        case desired_pose::CANDLE:
            tube_start_position = std::vector<double>{0.045522, 0.0222869, 0.535};
            desired_ee_pose     = { 0.045522, 0.0222869, 0.435, // Linear: Vector
                                    1.0, 0.0, 0.0, // Angular: Rotation matrix
                                    0.0, 1.0, 0.0,
                                    0.0, 0.0, 1.0};
            break;

        case desired_pose::LOOK_AT:
            tube_start_position = std::vector<double>{0.0195779, 0.366672, 0.252514};
            desired_ee_pose     = { 0.0192443, 0.235581, 0.240953, // Linear: Vector
                                    1.0, 0.0, 0.0, // Angular: Rotation matrix
                                    0.0, 1.0, 0.0,
                                    0.0, 0.0, 1.0};
            break;

        default:
            tube_start_position = std::vector<double>{0.262105, 0.004157, 0.308879};
            desired_ee_pose     = { 0.262105,  0.004157,  0.27000, // Linear: Vector
                                    0.338541,  0.137563,  0.930842, // Angular: Rotation Matrix
                                    0.337720, -0.941106,  0.016253,
                                    0.878257,  0.308861, -0.365061};
            break;
    }

    switch (desired_task_model)
    {
        case task_model::moveTo:
            dyn_controller->define_moveTo_task(std::vector<bool>{control_dims[0], control_dims[1], control_dims[2], // Linear
                                                                 control_dims[3], control_dims[4], control_dims[5]},// Angular
                                               tube_start_position,
                                               tube_tolerances,
                                               tube_speed,
                                               1.0, 0.1, //contact_threshold linear and angular
                                               task_time_limit_sec,// time_limit
                                               desired_ee_pose); // TF pose
            break;

        case task_model::moveTo_weight_compensation:
            dyn_controller->define_moveTo_weight_compensation_task(std::vector<bool>{control_dims[0], control_dims[1], control_dims[2], // Linear
                                                                                     control_dims[3], control_dims[4], control_dims[5]},// Angular
                                                                   tube_start_position,
                                                                   tube_tolerances,
                                                                   tube_speed,
                                                                   1.0, 0.1, // contact_threshold linear and angular
                                                                   task_time_limit_sec,// time_limit
                                                                   desired_ee_pose); // TF pose
            break;

        case task_model::full_pose:
            dyn_controller->define_desired_ee_pose(std::vector<bool>{control_dims[0], control_dims[1], control_dims[2], // Linear
                                                                     control_dims[3], control_dims[4], control_dims[5]}, // Angular
                                                   desired_ee_pose,
                                                   1.0, 0.2, //contact_threshold linear and angular
                                                   task_time_limit_sec);
            break;

        default:
            assert(("Unsupported task model", false));
            break;
    }
}

// Go to Candle 1 configuration  
void go_candle_1(youbot_mediator &arm){
    KDL::JntArray candle_pose(JOINTS);
    double candle[] = {2.1642, 1.13446, -2.54818, 1.78896, 0.12};
    for (int i = 0; i < JOINTS; i++) 
        candle_pose(i) = candle[i];  
    arm.set_joint_positions(candle_pose);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

// Go to Candle 2 configuration  
void go_candle_2(youbot_mediator &arm){
    KDL::JntArray candle_pose(JOINTS);
    double candle[] = {2.9496, 1.1344, -2.6354, 1.7890, 2.9234};
    for (int i = 0; i < JOINTS; i++) 
        candle_pose(i) = candle[i];  
    arm.set_joint_positions(candle_pose);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

// Go to Candle 3 configuration  
void go_candle_3(youbot_mediator &arm){
    KDL::JntArray candle_pose(JOINTS);
    double candle[] = {2.9496, 1.1344, -2.54818, 1.78896, 2.9234};
    for (int i = 0; i < JOINTS; i++) 
        candle_pose(i) = candle[i];  
    arm.set_joint_positions(candle_pose);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

// Go to Folded configuration  
void go_folded(youbot_mediator &arm){
    KDL::JntArray folded_pose(JOINTS);
    double folded[] = {0.02, 0.02, -0.02, 0.023, 0.12};
    for (int i = 0; i < JOINTS; i++) 
        folded_pose(i) = folded[i];  
    arm.set_joint_positions(folded_pose);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

void go_folded_2(youbot_mediator &arm){
    KDL::JntArray folded_pose(JOINTS);
    double folded[] = {0.02, 0.22, -0.02, 0.223, 0.12};
    for (int i = 0; i < JOINTS; i++) 
        folded_pose(i) = folded[i];  
    arm.set_joint_positions(folded_pose);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

// Go to Navigation 1 configuration  
void go_navigation_1(youbot_mediator &arm){
    KDL::JntArray desired_config(JOINTS);
    double navigation[] = {2.9496, 0.075952, -1.53240, 3.35214, 2.93816};
    for (int i = 0; i < JOINTS; i++) 
        desired_config(i) = navigation[i];  
    arm.set_joint_positions(desired_config);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

// Go to Navigation 2 configuration  
void go_navigation_2(youbot_mediator &arm){
    KDL::JntArray desired_config(JOINTS);
    double navigation[] = {2.9496, 1.0, -1.53240, 2.85214, 2.93816};
    for (int i = 0; i < JOINTS; i++) 
        desired_config(i) = navigation[i];  
    arm.set_joint_positions(desired_config);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

// Go to Navigation 3 configuration  
void go_navigation_3(youbot_mediator &arm){
    KDL::JntArray desired_config(JOINTS);
    double navigation[] = {1.3796, 1.0, -1.53240, 2.85214, 2.93816};
    for (int i = 0; i < JOINTS; i++) 
        desired_config(i) = navigation[i];  
    arm.set_joint_positions(desired_config);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

void go_look_at(youbot_mediator &arm){
    KDL::JntArray desired_config(JOINTS);
    double navigation[] = {1.3842, 1.59705, -1.49501, 1.92562, 2.95774};
    // double navigation[] = {1.3796, 1.29471, -1.53241, 2.85201, 2.93825};
    for (int i = 0; i < JOINTS; i++) 
        desired_config(i) = navigation[i];  
    arm.set_joint_positions(desired_config);
    if (environment != youbot_environment::SIMULATION) usleep(5000 * MILLISECOND);
}

//Set velocities of arm's joints to 0 value
void stop_robot_motion(youbot_mediator &arm){
    KDL::JntArray stop_motion(JOINTS);
    for (int i = 0; i < JOINTS; i++) 
        stop_motion(i) = 0.0;  
    arm.set_joint_velocities(stop_motion);
}

// Test case for last joint
void rotate_joint(youbot_mediator &arm, const int joint, const double rate){
    KDL::JntArray rotate_joint(JOINTS);
    for (int i = 0; i < JOINTS; i++) 
        rotate_joint(i) = 0.0;
    rotate_joint(joint) = rate; 
    arm.set_joint_velocities(rotate_joint);
    usleep(3000 * MILLISECOND);
}

int main(int argc, char **argv)
{
    printf("youBot MAIN Started \n");
    youbot_mediator robot_driver;

    environment          = youbot_environment::REAL;
    robot_model_id       = youbot_model::URDF;
    desired_pose_id      = desired_pose::LOOK_AT;
    desired_control_mode = control_mode::TORQUE;
    tube_speed           = 0.05;

    // Extract robot model and if not simulation, establish connection with motor drivers
    robot_driver.initialize(robot_model_id, environment, compansate_gravity);
    assert(("Robot is not initialized", robot_driver.is_initialized()));
    
    int number_of_segments = robot_driver.get_robot_model().getNrOfSegments();
    int number_of_joints   = robot_driver.get_robot_model().getNrOfJoints();
    assert(JOINTS == number_of_segments);

    stop_robot_motion(robot_driver);
    if (desired_pose_id == desired_pose::LOOK_AT) go_look_at(robot_driver);
    else if (desired_pose_id == desired_pose::CANDLE) go_candle_1(robot_driver);
    else go_navigation_2(robot_driver);
    // rotate_joint(robot_driver, 5, 0.1);

    // state_specification motion(number_of_joints, number_of_segments,
    //                            number_of_segments + 1, NUMBER_OF_CONSTRAINTS);
    // robot_driver.get_joint_positions(motion.q);
    // robot_driver.get_joint_velocities(motion.qd);
    // return 0;

    // Extract robot model and if not simulation, establish connection with motor drivers
    // robot_model_id = youbot_model::URDF;
    // robot_driver.initialize(robot_model_id, environment, compansate_gravity);

    //loop rate in Hz
    int rate_hz = 650;
    dynamics_controller controller(&robot_driver, rate_hz);

    define_task(&controller, task_model::moveTo);
    if (desired_task_model == task_model::full_pose) 
    {
        controller.set_parameters(damper_amplitude, abag_error_type, 
                                  max_command, error_alpha,
                                  bias_threshold, bias_step, gain_threshold,
                                  gain_step, min_bias_sat, min_command_sat,
                                  null_space_abag_parameters,
                                  compensation_parameters);
    }
    else if ((desired_task_model == task_model::moveTo) && (desired_control_mode == control_mode::VELOCITY)) 
    {
        controller.set_parameters(damper_amplitude, abag_error_type, 
                                  max_command, error_alpha_2_1,
                                  bias_threshold_2_1, bias_step_2_1, gain_threshold_2_1,
                                  gain_step_2_1, min_bias_sat, min_command_sat,
                                  null_space_abag_parameters,
                                  compensation_parameters);
    }

    else
    {
     controller.set_parameters(damper_amplitude, abag_error_type, 
                               max_command, error_alpha_2,
                               bias_threshold_2, bias_step_2, gain_threshold_2,
                               gain_step_2, min_bias_sat, min_command_sat,
                               null_space_abag_parameters,
                               compensation_parameters);
    }

    int initial_result = controller.initialize(desired_control_mode, 
                                               desired_dynamics_interface,
                                               log_data,
                                               motion_profile_id);
    if (initial_result != 0) return -1;
    controller.control();

    return 0;
}