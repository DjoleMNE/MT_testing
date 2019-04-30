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

#include <dynamics_controller.hpp>
#define SECOND 1000000
const double MIN_NORM = 1e-10;

dynamics_controller::dynamics_controller(robot_mediator *robot_driver,
                                         const int rate_hz):
    RATE_HZ_(rate_hz),
    // Time period defined in microseconds: 1s = 1 000 000us
    DT_MICRO_(SECOND / RATE_HZ_),  DT_SEC_(1.0 / static_cast<double>(RATE_HZ_)),
    loop_start_time_(), loop_end_time_(), //Not sure if required to init
    robot_chain_(robot_driver->get_robot_model()),
    NUM_OF_JOINTS_(robot_chain_.getNrOfJoints()),
    NUM_OF_SEGMENTS_(robot_chain_.getNrOfSegments()),
    NUM_OF_FRAMES_(robot_chain_.getNrOfSegments() + 1),
    NUM_OF_CONSTRAINTS_(dynamics_parameter::NUMBER_OF_CONSTRAINTS),
    END_EFF_(NUM_OF_SEGMENTS_ - 1),
    CTRL_DIM_(NUM_OF_CONSTRAINTS_, false),
    JOINT_TORQUE_LIMITS_(robot_driver->get_joint_torque_limits()),
    current_error_twist_(KDL::Twist::Zero()),
    predicted_error_twist_(Eigen::VectorXd::Zero(abag_parameter::DIMENSIONS)),
    transformed_error_(Eigen::VectorXd::Zero(abag_parameter::DIMENSIONS)),
    use_transformed_driver_(true),
    horizon_amplitude_(1.0), horizon_slope_(4.5),
    abag_command_(Eigen::VectorXd::Zero(abag_parameter::DIMENSIONS)),
    max_command_(Eigen::VectorXd::Zero(abag_parameter::DIMENSIONS)),
    motion_profile_(Eigen::VectorXd::Ones(abag_parameter::DIMENSIONS)),
    cart_force_command_(NUM_OF_SEGMENTS_),
    hd_solver_(robot_chain_, robot_driver->get_joint_inertia(),
               robot_driver->get_root_acceleration(), NUM_OF_CONSTRAINTS_),
    fk_vereshchagin_(robot_chain_),
    safety_control_(robot_driver, true), fsm_(),
    abag_(abag_parameter::DIMENSIONS, abag_parameter::USE_ERROR_SIGN),
    predictor_(robot_chain_),
    robot_state_(NUM_OF_JOINTS_, NUM_OF_SEGMENTS_, NUM_OF_FRAMES_, NUM_OF_CONSTRAINTS_),
    desired_state_(robot_state_),
    predicted_state_(robot_state_)
{
    assert(("Robot is not initialized", robot_driver->is_initialized()));
    // KDL Solver constraint  
    assert(NUM_OF_JOINTS_ == NUM_OF_SEGMENTS_);

    // Control loop frequency must be higher than or equal to 1 Hz
    assert(("Selected frequency is too low", 1 <= RATE_HZ_));
    // Control loop frequency must be lower than or equal to 1000 Hz
    assert(("Selected frequency is too high", RATE_HZ_<= 10000));
    
    // Set default command interface to stop motion mode and initialize it as not safe
    desired_control_mode_.interface = control_mode::STOP_MOTION;
    desired_control_mode_.is_safe = false;

    desired_task_inteface_ = dynamics_interface::CART_FORCE;

    KDL::SetToZero(cart_force_command_[END_EFF_]);

    // Setting parameters of the ABAG Controller
    abag_.set_error_alpha(abag_parameter::ERROR_ALPHA);    
    abag_.set_bias_threshold(abag_parameter::BIAS_THRESHOLD);
    abag_.set_bias_step(abag_parameter::BIAS_STEP);
    abag_.set_gain_threshold(abag_parameter::GAIN_THRESHOLD);
    abag_.set_gain_step(abag_parameter::GAIN_STEP);
}

//Print information about controller settings
void dynamics_controller::print_settings_info()
{   
    #ifdef NDEBUG
        printf("The program is build in RELEASE mode.\n");
    #endif
    #ifndef NDEBUG
        printf("The program is build in DEBUG mode.\n");
    #endif
    
    printf("Selected controller settings:\n");
    printf("Control Loop Frequency: %d Hz\n", RATE_HZ_);

    printf("Control Mode: ");
    switch(desired_control_mode_.interface) 
    {
        case control_mode::STOP_MOTION:
            printf("STOP MOTION \n Stopping the robot!\n");
            break;

        case control_mode::POSITION:
            printf("Joint Position Control\n");
            break;

        case control_mode::VELOCITY:
            printf("Joint Velocity Control\n");
            break;

        case control_mode::TORQUE:
            printf("Joint Torque Control\n");
            break;
    }

    printf("Dynamics Interface: ");
    switch(desired_task_inteface_) 
    {
        case dynamics_interface::CART_ACCELERATION:
            printf("Cartesian EndEffector Acceleration Interface\n");
            break;

        case dynamics_interface::CART_FORCE:
            printf("Cartesian Force Interface\n");
            break;

        case dynamics_interface::FF_JOINT_TORQUE:
            printf("FeedForward Joint Torque Interface\n");
            break;

        default:
            printf("Stopping the robot!\n");
            break;
    }

    std::cout<< "\nInitial joint state: "<< std::endl;
    std::cout<< "Joint positions: "<< robot_state_.q << std::endl;
    std::cout<< "Joint velocities:"<< robot_state_.qd << "\n" << std::endl;

    std::cout<< "Initial Cartesian state: "<< std::endl;
    std::cout<< "End-effector position: "<< robot_state_.frame_pose[END_EFF_].p << std::endl;
    std::cout<< "End-effector orientation: \n"<< robot_state_.frame_pose[END_EFF_].M << std::endl;
    std::cout<< "End-effector velocity:"<< robot_state_.frame_velocity[END_EFF_] << "\n" << std::endl;
}

/* 
    If it is working on the real robot get sensor data from the driver 
    or if simulation is on, replace current state with 
    integrated joint velocities and positions.
*/
void dynamics_controller::update_current_state()
{
    // Get joint angles and velocities
    safety_control_.get_current_state(robot_state_);

    // Get Cart poses and velocities
    int fk_solver_result = fk_vereshchagin_.JntToCart(robot_state_.q, 
                                                      robot_state_.qd, 
                                                      robot_state_.frame_pose, 
                                                      robot_state_.frame_velocity);
    if(fk_solver_result != 0) 
        printf("Warning: FK solver returned an error! %d \n", fk_solver_result);

    // Update current constraints, external forces, and feedforward torques
    // update_dynamics_interfaces();

    // Print Current robot state in Debug mode
#ifndef NDEBUG
        // std::cout << "\nCurrent Joint state:          " << std::endl;
        // std::cout << "Joint angle:    " << robot_state_.q << std::endl;
        // std::cout << "Joint velocity: " << robot_state_.qd << std::endl;
        
        // std::cout << "\nCurrent Cartesian state:                 " << std::endl;
        // std::cout << "End-effector Position:   " 
        //           << robot_state_.frame_pose[END_EFF_].p  << std::endl;
        // std::cout << "End-effector Velocity:                \n" 
        //           << robot_state_.frame_velocity[END_EFF_] << std::endl;
#endif 

#ifdef NDEBUG
        // std::cout << "End-effector Velocity:   \n" 
        //           << robot_state_.frame_velocity[END_EFF_] << std::endl;
#endif
}

// Update current dynamics intefaces using desired robot state specifications 
void dynamics_controller::update_dynamics_interfaces()
{ 
    robot_state_.ee_unit_constraint_force = desired_state_.ee_unit_constraint_force;
    robot_state_.ee_acceleration_energy   = desired_state_.ee_acceleration_energy;
    robot_state_.feedforward_torque       = desired_state_.feedforward_torque;
    robot_state_.external_force           = desired_state_.external_force;
}

// Write control data to a file
void dynamics_controller::write_to_file()
{   
    for(int i = 0; i < 3; i++) 
        log_file_cart_ << robot_state_.frame_pose[END_EFF_].p(i) << " ";
    for(int i = 3; i < 6; i++) 
        log_file_cart_ << 0.0 << " ";

    log_file_cart_ << std::endl;

    for(int i = 0; i < 3; i++) 
        log_file_cart_ << desired_state_.frame_pose[END_EFF_].p(i) << " ";
    for(int i = 3; i < 6; i++) 
        log_file_cart_ << 0.0 << " ";
        
    log_file_cart_ << std::endl;

    for(int i = 0; i < 6; i++) 
        log_file_cart_ << predicted_error_twist_(i) << " ";
    log_file_cart_ << std::endl;

    log_file_cart_ << abag_.get_error().transpose().format(dynamics_parameter::WRITE_FORMAT);
    log_file_cart_ << abag_.get_bias().transpose().format(dynamics_parameter::WRITE_FORMAT);
    log_file_cart_ << abag_.get_gain().transpose().format(dynamics_parameter::WRITE_FORMAT);
    log_file_cart_ << abag_.get_command().transpose().format(dynamics_parameter::WRITE_FORMAT);
    
    log_file_joint_ << robot_state_.control_torque.data.transpose().format(dynamics_parameter::WRITE_FORMAT);
}

// Set all values of desired state to 0 - public method
void dynamics_controller::reset_desired_state()
{
    reset_state(desired_state_);
}

// Set all values of selected state to 0 - Private method
void dynamics_controller::reset_state(state_specification &state)
{
    desired_state_.reset_values();
}

//Send 0 joints velocities to the robot driver
void dynamics_controller::stop_robot_motion()
{   
    safety_control_.stop_robot_motion();
}

void dynamics_controller::define_desired_ee_pose(
                            const std::vector<bool> &constraint_direction,
                            const std::vector<double> &cartesian_pose)
{
    assert(constraint_direction.size() == NUM_OF_CONSTRAINTS_);
    assert(cartesian_pose.size() == NUM_OF_CONSTRAINTS_ * 2);
    
    CTRL_DIM_ = constraint_direction;
    
    desired_state_.frame_pose[END_EFF_].p(0) = cartesian_pose[0];
    desired_state_.frame_pose[END_EFF_].p(1) = cartesian_pose[1];
    desired_state_.frame_pose[END_EFF_].p(2) = cartesian_pose[2];

    desired_state_.frame_pose[END_EFF_].M = \
        KDL::Rotation(cartesian_pose[3], cartesian_pose[4], cartesian_pose[5],
                      cartesian_pose[6], cartesian_pose[7], cartesian_pose[8],
                      cartesian_pose[9], cartesian_pose[10], cartesian_pose[11]);
}

// Define Cartesian Acceleration task on the end-effector - Public Method
void dynamics_controller::define_ee_acc_constraint(
                            const std::vector<bool> &constraint_direction,
                            const std::vector<double> &cartesian_acceleration)
{    
    //Call private method for this state
    set_ee_acc_constraints(desired_state_, 
                           constraint_direction, 
                           cartesian_acceleration);
}

// Define Cartesian Acceleration task on the end-effector - Private Method
void dynamics_controller::set_ee_acc_constraints(
                                state_specification &state,
                                const std::vector<bool> &constraint_direction, 
                                const std::vector<double> &cartesian_acceleration)
{    
    assert(constraint_direction.size() == NUM_OF_CONSTRAINTS_);
    assert(cartesian_acceleration.size() == NUM_OF_CONSTRAINTS_);

    // Set directions in which constraint force should work. Alpha in the solver 
    KDL::Twist unit_force_x_l(
        KDL::Vector((constraint_direction[0] ? 1.0 : 0.0), 0.0, 0.0), 
        KDL::Vector(0.0, 0.0, 0.0));
    state.ee_unit_constraint_force.setColumn(0, unit_force_x_l);

    KDL::Twist unit_force_y_l(
            KDL::Vector(0.0, (constraint_direction[1] ? 1.0 : 0.0), 0.0),
            KDL::Vector(0.0, 0.0, 0.0));
    state.ee_unit_constraint_force.setColumn(1, unit_force_y_l);

    KDL::Twist unit_force_z_l(
            KDL::Vector(0.0, 0.0, (constraint_direction[2] ? 1.0 : 0.0)),
            KDL::Vector(0.0, 0.0, 0.0));
    state.ee_unit_constraint_force.setColumn(2, unit_force_z_l);

    KDL::Twist unit_force_x_a(
            KDL::Vector(0.0, 0.0, 0.0),
            KDL::Vector((constraint_direction[3] ? 1.0 : 0.0), 0.0, 0.0));
    state.ee_unit_constraint_force.setColumn(3, unit_force_x_a);

    KDL::Twist unit_force_y_a(
            KDL::Vector(0.0, 0.0, 0.0),
            KDL::Vector(0.0, (constraint_direction[4] ? 1.0 : 0.0), 0.0));
    state.ee_unit_constraint_force.setColumn(4, unit_force_y_a);

    KDL::Twist unit_force_z_a(
            KDL::Vector(0.0, 0.0, 0.0),
            KDL::Vector(0.0, 0.0, (constraint_direction[5] ? 1.0 : 0.0)));
    state.ee_unit_constraint_force.setColumn(5, unit_force_z_a);

    // Set desired acceleration on the end-effector. Beta in the solver
    for (int i = 0; i < NUM_OF_CONSTRAINTS_; i++)
        state.ee_acceleration_energy(i) = cartesian_acceleration[i];
}

// Define External force task - Public Method
void dynamics_controller::define_ee_external_force(const std::vector<double> &external_force)
{
    //Call private method for this state
    set_external_forces(desired_state_, external_force);
}

// Define External force task - Private Method
void dynamics_controller::set_external_forces(state_specification &state, 
                                              const std::vector<double> &external_force)
{
    //For now it is only updating forces on the end-effector
    //TODO: add forces on other segments as well
    assert(external_force.size() == NUM_OF_CONSTRAINTS_);

    state.external_force[END_EFF_] = KDL::Wrench (KDL::Vector(external_force[0],
                                                              external_force[1],
                                                              external_force[2]),
                                                  KDL::Vector(external_force[3],
                                                              external_force[4],
                                                              external_force[5]));
}

// Define FeedForward joint torques task - Public Method
void dynamics_controller::define_feedforward_torque(const std::vector<double> &ff_torque)
{
    //Call private method for this state
    set_feedforward_torque(desired_state_, ff_torque);
}

// Define FeedForward joint torques task - Private Method
void dynamics_controller::set_feedforward_torque(state_specification &state, 
                                                 const std::vector<double> &ff_torque)
{
    assert(ff_torque.size() == NUM_OF_JOINTS_);

    for (int i = 0; i < NUM_OF_JOINTS_; i++) state.feedforward_torque(i) = ff_torque[i];
}

//Make sure that the control loop runs exactly with specified frequency
int dynamics_controller::enforce_loop_frequency()
{
    loop_interval_= std::chrono::duration<double, std::micro>\
                    (std::chrono::steady_clock::now() - loop_start_time_);

    if(loop_interval_ < std::chrono::microseconds(DT_MICRO_))
    {   
        //Loop is sufficiently fast
        // clock_nanosleep((DT_MICRO_ - loop_interval_.count()));
        while(loop_interval_.count() < DT_MICRO_){
            loop_interval_= std::chrono::duration<double, std::micro>\
                    (std::chrono::steady_clock::now() - loop_start_time_);
        }
        return 0;
    } else return -1; //Loop is too slow
}

/*
    Apply joint commands using safe control interface.
    If the computed commands are not safe, exit the program.
*/
int dynamics_controller::apply_joint_control_commands()
{ 
    /* 
        Safety controller checks if the commands are over the limits.
        If false: use desired control mode
        Else: stop the robot motion 
    */
    int safe_control_mode = safety_control_.set_control_commands(robot_state_, 
                                                                 DT_SEC_, 
                                                                 desired_control_mode_.interface,
                                                                 integration_method::SYMPLECTIC_EULER);
   
    // Check if the safety controller has changed the control mode
    // Save the current decision if desired control mode is safe or not.
    desired_control_mode_.is_safe =\
        (desired_control_mode_.interface == safe_control_mode)? true : false; 

    // Notify if the safety controller has changed the control mode
    switch(safe_control_mode) {
        case control_mode::TORQUE:
            assert(desired_control_mode_.is_safe);
            return 0;

        case control_mode::VELOCITY:
            if(!desired_control_mode_.is_safe) 
                printf("WARNING: Control switched to velocity mode \n");
            return 0;

        case control_mode::POSITION:
            if(!desired_control_mode_.is_safe) 
                printf("WARNING: Control switched to position mode \n");
            return 0;

        default: 
            stop_robot_motion();
            // printf("WARNING: Computed commands are not safe. Stopping the robot!\n");
            return -1;
    }
}

/*  
    Predict future robot Cartesian states given the current Cartesian state.
    I.e. Integrate Cartesian variables.
*/
void dynamics_controller::make_predictions(const double dt_sec, const int num_steps)
{
    predictor_.integrate_cartesian_space(robot_state_, 
                                         predicted_state_, 
                                         dt_sec, num_steps);
}


KDL::Twist dynamics_controller::displacement_twist(const state_specification &state_a, 
                                                   const state_specification &state_b)
{
    /** 
     * Difference between two poses: Decoupled calculation 
     * See "Modern Robotics" Book, 2017, sections 9.2.1 and 11.3.3.
    */

    // The default constructor initialises to Zero via the constructor of Vector!
    KDL::Twist twist;

    /**
     * This error part represents a linear motion necessary to go from 
     * predicted to desired position (positive direction of translation).
    */
    twist.vel = state_a.frame_pose[END_EFF_].p - state_b.frame_pose[END_EFF_].p;

    /**
     * Describes rotation required to align R_p with R_d.
     * It represents relative rotation from predicted state to 
     * desired state, expressed in the BASE frame!
     * Source: Luh et al. "Resolved-acceleration control of 
     * mechanical manipulators".
    */
    KDL::Rotation relative_rot_matrix = state_a.frame_pose[END_EFF_].M * \
                                        state_b.frame_pose[END_EFF_].M.Inverse();

    // Error calculation for angular part, i.e. logarithmic map on SO(3).
    twist.rot = geometry::log_map_so3(relative_rot_matrix);

    return twist;
}

double dynamics_controller::kinetic_energy(const KDL::Twist &twist,
                                           const int segment_index)
{
    return 0.5 * dot(twist, robot_chain_.getSegment(segment_index).getInertia() * twist);
}

/**
 * Compute the error between desired Cartesian state 
 * and predicted (integrated) Cartesian state.
*/
void dynamics_controller::compute_control_error()
{
    current_error_twist_ = displacement_twist(desired_state_, robot_state_);

    // KDL::Twist total_twist = current_error_twist_ + robot_state_.frame_velocity[END_EFF_];

    // for(int i = 0; i < 6; i++)
    //     if(!CTRL_DIM_[i]) total_twist(i) = 0.0;

    // double energy = kinetic_energy(total_twist, END_EFF_);



    // double time_horizon_sec = fsm_.tanh_decision_map(energy, 
    //                                                  horizon_amplitude_, 
    //                                                  horizon_slope_);

    // double time_horizon_sec = fsm_.tanh_inverse_decision_map(energy,
    //                                                          0.1, 2.4, 1.0);

    // double time_horizon_sec = fsm_.step_decision_map(energy, 
    //                                                  3.5, 0.2, 
    //                                                  0.03, 0.01);

    double energy = 0.0; 
    double time_horizon_sec = horizon_amplitude_;

#ifndef NDEBUG
    for(int i = 0; i < 3; i++) 
        log_file_predictions_ << robot_state_.frame_velocity[END_EFF_].vel(i) << " ";
    
    log_file_predictions_ << energy << " " << time_horizon_sec << std::endl;
#endif

    make_predictions(time_horizon_sec, 1);

    KDL::Twist error_twist = displacement_twist(desired_state_, predicted_state_);
    transformed_error_(0) = error_twist.vel.Norm();
    transformed_error_(3) = error_twist.rot.Norm();
    
    predicted_error_twist_ = conversions::kdl_twist_to_eigen(error_twist);

#ifndef NDEBUG
        // std::cout << "\nLinear Error: " << predicted_error_twist_.head(3).transpose() << "    Linear norm: " << predicted_error_twist_.head(3).norm() << std::endl;
        // std::cout << "Angular Error: " << predicted_error_twist_.tail(3).transpose() << "         Angular norm: " << predicted_error_twist_.tail(3).norm() << std::endl;
#endif
}

void dynamics_controller::transform_motion_driver()
{
    abag_command_ = abag_.update_state(transformed_error_).transpose();
    double linear_1D_command = abag_command_(0);
    double angular_1D_command = abag_command_(3);

    // Transform to Linear 3D command
    if (transformed_error_(0) >= MIN_NORM)
    {
        for(int i = 0; i < 3; i++)
            abag_command_(i) = (predicted_error_twist_(i) / transformed_error_(0)) * linear_1D_command;
    }
    else
    {

#ifndef NDEBUG
            printf("Linear error Norm too small");
#endif
        for(int i = 0; i < 3; i++) 
            abag_command_(i) = 0.0;
    }

    // Transform to Angular 3D command
    if (transformed_error_(3) >= MIN_NORM)
    {
        for(int i = 3; i < 6; i++)
            abag_command_(i) = (predicted_error_twist_(i) / transformed_error_(3)) * angular_1D_command;
    }
    else
    {

#ifndef NDEBUG
            // printf("Angular error Norm too small");
#endif
        for(int i = 3; i < 6; i++) 
            abag_command_(i) = 0.0;
    }

}

void dynamics_controller::compute_cart_control_commands()
{   
    if (use_transformed_driver_) transform_motion_driver();
    else abag_command_ = abag_.update_state(predicted_error_twist_).transpose();
    
    bool use_motion_profile = true;
    if(use_motion_profile) for(int i = 0; i < 3; i++)
        motion_profile_(i) = motion_profile::negative_step_decision_map(current_error_twist_.vel.Norm(), 
                                                                        max_command_(i), 0.25, 0.4, 0.1);
    else motion_profile_ = max_command_;

    switch (desired_task_inteface_)
    {
        case dynamics_interface::CART_FORCE:
            // Set additional (virtual) force computed by the ABAG controller
            for(int i = 0; i < NUM_OF_CONSTRAINTS_; i++)
                cart_force_command_[END_EFF_](i) = CTRL_DIM_[i]? abag_command_(i) * motion_profile_(i) : 0.0;
            break;

        case dynamics_interface::CART_ACCELERATION:
            // Overwrite the existing Cart Acc Constraints on the End-Effector
            set_ee_acc_constraints(robot_state_,
                                   std::vector<bool>{CTRL_DIM_[0], CTRL_DIM_[1], CTRL_DIM_[2], // Linear
                                                     CTRL_DIM_[3], CTRL_DIM_[4], CTRL_DIM_[5]}, // Angular
                                   std::vector<double>{abag_command_(0) * motion_profile_(0), // Linear
                                                       abag_command_(1) * motion_profile_(1), // Linear
                                                       abag_command_(2) * motion_profile_(2), // Linear
                                                       abag_command_(3) * motion_profile_(3), // Angular
                                                       abag_command_(4) * motion_profile_(4), // Angular
                                                       abag_command_(5) * motion_profile_(5)}); // Angular
            break;

        default:
            assert(("Unsupported interface!", false));
            break;
    }


#ifndef NDEBUG
    // std::cout << "ABAG Commands:         "<< abag_command_.transpose() << std::endl;
    // std::cout << "Virtual Force Command: " << cart_force_command_[END_EFF_] << std::endl;
    // printf("\n");
#endif
}

//Calculate robot dynamics - Resolve motion and forces using the Vereshchagin HD solver
int dynamics_controller::evaluate_dynamics()
{
    int hd_solver_result = hd_solver_.CartToJnt(robot_state_.q,
                                                robot_state_.qd,
                                                robot_state_.qdd,
                                                robot_state_.ee_unit_constraint_force,
                                                robot_state_.ee_acceleration_energy,
                                                robot_state_.external_force,
                                                cart_force_command_,
                                                robot_state_.feedforward_torque);

    if(hd_solver_result != 0) return hd_solver_result;

    hd_solver_.get_control_torque(robot_state_.control_torque);

    // hd_solver_.get_transformed_link_acceleration(robot_state_.frame_acceleration);
    
    // Print computed state in Debug mode
#ifndef NDEBUG
        // std::cout << "\nComputed Cartesian state:" << std::endl;

        // std::cout << "Frame ACC" << '\n';
        // for (size_t i = 0; i < NUM_OF_SEGMENTS_ + 1; i++)
        //     std::cout << robot_state_.frame_acceleration[i] << '\n';

        // std::cout << "End-effector Position:   " 
        //       << robot_state_.frame_pose[END_EFF_].p  << std::endl;

        // std::cout << "\nComputed Joint state:          " << std::endl;
        // std::cout << "Joint torque:  " << robot_state_.control_torque << std::endl;
        // std::cout << "Joint acc:     " << robot_state_.qdd << std::endl;
#endif 

#ifdef NDEBUG
        // std::cout << "Joint torque:  " << robot_state_.control_torque << std::endl;
#endif

    return hd_solver_result;
}

//Main control loop
int dynamics_controller::control(const int desired_control_mode,
                                 const bool store_control_data)
{   
    // Save current selection of desire control mode
    desired_control_mode_.interface = desired_control_mode;
    
    //Exit the program if the "Stop Motion" mode is selected
    if(desired_control_mode_.interface == control_mode::STOP_MOTION){
        std::cout << "Stop Motion mode selected. Exiting the program" << std::endl;
        return -1;
    } 
    
    // First make sure that the robot is not moving
    stop_robot_motion();

    /* 
        Get sensor data from the robot driver or if simulation is on, 
        replace current state with the integrated joint velocities and positions.
        Additionally, update dynamics intefaces.
    */
    update_current_state();  update_dynamics_interfaces();

    //Print information about controller settings
    print_settings_info();

    if (store_control_data) 
    {
        log_file_cart_.open(dynamics_parameter::LOG_FILE_CART_PATH);
        if (!log_file_cart_.is_open()) {
            printf("Unable to open the file!\n");
            return -1;
        }

        log_file_joint_.open(dynamics_parameter::LOG_FILE_JOINT_PATH);
        if (!log_file_joint_.is_open()) {
            printf("Unable to open the file!\n");
            return -1;
        }

        log_file_predictions_.open(dynamics_parameter::LOG_FILE_PREDICTIONS_PATH);
        if (!log_file_predictions_.is_open()) {
            printf("Unable to open the file!\n");
            return -1;
        }

        for(int i = 0; i < NUM_OF_JOINTS_; i++) 
            log_file_joint_ << JOINT_TORQUE_LIMITS_[i] << " ";
        log_file_joint_ << std::endl;
    }

    double loop_time = 0.0;
    int loop_count = 0;

    std::cout << "Control Loop Started"<< std::endl;
    while(1)
    {   
        loop_count++; 
        // printf("Loop Count: %d \n", loop_count);

        // Save current time point
        loop_start_time_ = std::chrono::steady_clock::now();

        //Get current robot state from the joint sensors: velocities and angles
        update_current_state();

        compute_control_error();
        
        compute_cart_control_commands();
        if (store_control_data) write_to_file();        

        // Calculate robot dynamics using the Vereshchagin HD solver
        if(evaluate_dynamics() != 0)
        {
            stop_robot_motion();
            if (store_control_data) 
            {
                log_file_cart_.close();
                log_file_joint_.close();
                log_file_predictions_.close();
            }
            printf("WARNING: Dynamics Solver returned error. Stopping the robot!");
            return -1;
        }
        // if(loop_count == 1) return 0;

        // Apply joint commands using safe control interface.
        if(apply_joint_control_commands() != 0){
            if (store_control_data) 
            {
                log_file_cart_.close();
                log_file_joint_.close();
                log_file_predictions_.close();
            }
            return -1;
        } 

        // Make sure that the loop is always running with the same frequency
        if(!enforce_loop_frequency() == 0)
            printf("WARNING: Control loop runs too slow \n");

        // loop_time += std::chrono::duration<double, std::micro>\
        //             (std::chrono::steady_clock::now() -\
        //                                          loop_start_time_).count();
        // if(loop_count == 40) {
        //     std::cout << loop_time / 40.0 <<std::endl;
        //     return 0;
        // }
    }

    if (store_control_data) 
    {
        log_file_cart_.close();
        log_file_joint_.close();
        log_file_predictions_.close();
    }
    return 0;
}

void dynamics_controller::initialize(const int desired_control_mode, 
                                     const int desired_task_inteface,
                                     const bool use_transformed_driver,
                                     const bool store_control_data)
{
    // Save current selection of desire control mode
    desired_control_mode_.interface = desired_control_mode;

    //Exit the program if the "Stop Motion" mode is selected
    assert(desired_control_mode_.interface != control_mode::STOP_MOTION); 

    desired_task_inteface_ = desired_task_inteface;

    use_transformed_driver_ = use_transformed_driver;
    
    // First make sure that the robot is not moving
    // stop_robot_motion();

    // Update current constraints, external forces, and feedforward torques
    update_dynamics_interfaces();
    store_control_data_ = store_control_data;

    if (store_control_data_) 
    {
        log_file_cart_.open(dynamics_parameter::LOG_FILE_CART_PATH);
        assert(log_file_cart_.is_open());

        log_file_joint_.open(dynamics_parameter::LOG_FILE_JOINT_PATH);
        assert(log_file_joint_.is_open());

        log_file_predictions_.open(dynamics_parameter::LOG_FILE_PREDICTIONS_PATH);
        assert(log_file_predictions_.is_open());

        for(int i = 0; i < NUM_OF_JOINTS_; i++) 
            log_file_joint_ << JOINT_TORQUE_LIMITS_[i] << " ";
        log_file_joint_ << std::endl;
    }
}

void dynamics_controller::set_parameters(const double horizon_amplitude,
                                         const double horizon_slope,
                                         const int abag_error_type,
                                         const Eigen::VectorXd &max_command,
                                         const Eigen::VectorXd &error_alpha, 
                                         const Eigen::VectorXd &bias_threshold, 
                                         const Eigen::VectorXd &bias_step, 
                                         const Eigen::VectorXd &gain_threshold, 
                                         const Eigen::VectorXd &gain_step,
                                         const Eigen::VectorXd &min_bias_sat,
                                         const Eigen::VectorXd &min_command_sat)
{
    //First check input dimensions
    assert(max_command.size()    == NUM_OF_CONSTRAINTS_); 
    assert(error_alpha.size()    == NUM_OF_CONSTRAINTS_); 
    assert(bias_threshold.size() == NUM_OF_CONSTRAINTS_); 
    assert(bias_step.size()      == NUM_OF_CONSTRAINTS_); 
    assert(gain_threshold.size() == NUM_OF_CONSTRAINTS_); 
    assert(gain_step.size()      == NUM_OF_CONSTRAINTS_); 

    this->horizon_amplitude_  = horizon_amplitude;
    this->horizon_slope_      = horizon_slope;
    this->max_command_        = max_command;
    
    // Setting parameters of the ABAG Controller
    abag_.set_error_alpha(error_alpha);    
    abag_.set_bias_threshold(bias_threshold);
    abag_.set_bias_step(bias_step);
    abag_.set_gain_threshold(gain_threshold);
    abag_.set_gain_step(gain_step);
    abag_.set_min_bias_sat_limit(min_bias_sat);
    abag_.set_min_command_sat_limit(min_command_sat);
    abag_.set_error_type(abag_error_type);
}

/**
 * Perform single step of the control loop, given current robot joint state
 * Required for RTT's updateHook method
*/
int dynamics_controller::step(const KDL::JntArray &q_input,
                              const KDL::JntArray &qd_input,
                              Eigen::VectorXd &tau_output)
{
    robot_state_.q = q_input;
    robot_state_.qd = qd_input;

    // Get Cart poses and velocities
    int fk_solver_result = fk_vereshchagin_.JntToCart(robot_state_.q, 
                                                      robot_state_.qd, 
                                                      robot_state_.frame_pose, 
                                                      robot_state_.frame_velocity);
    if(fk_solver_result != 0)
    {
        deinitialize();
        printf("Warning: FK solver returned an error! %d \n", fk_solver_result);
        return -1;
    }

    // Print Current robot state in Debug mode
    #ifndef NDEBUG
        // std::cout << "\nCurrent Joint state:          " << std::endl;
        // std::cout << "Joint angle:    " << robot_state_.q << std::endl;
        // std::cout << "Joint velocity: " << robot_state_.qd << std::endl;
        
        // std::cout << "\nCurrent Cartesian state:                 " << std::endl;
        // std::cout << "End-effector Position:   " 
        //           << robot_state_.frame_pose[END_EFF_].p  << std::endl;
        // std::cout << "End-effector Orientation:   \n" 
        //           << robot_state_.frame_pose[END_EFF_].M  << std::endl;
        // std::cout << "End-effector Velocity:                \n" 
        //           << robot_state_.frame_velocity[END_EFF_] << std::endl;
    #endif 

    #ifdef NDEBUG
        // std::cout << "End-effector Velocity:   \n" 
        //           << robot_state_.frame_velocity[END_EFF_] << std::endl;
    #endif

    compute_control_error();
    
    compute_cart_control_commands();
    if (store_control_data_) write_to_file();   

    // Calculate robot dynamics using the Vereshchagin HD solver
    if(evaluate_dynamics() != 0)
    {
        deinitialize();
        printf("WARNING: Dynamics Solver returned error. Stopping the robot!\n");
        return -1;
    }

    // apply_joint_control_commands();
    // Apply joint commands using safe control interface.
    // if(apply_joint_control_commands() != 0)
    // {
    //     deinitialize();
    //     return -1;
    // } 
    tau_output = robot_state_.control_torque.data;
    
    return 0;
}

void dynamics_controller::deinitialize()
{
    // First make sure that the robot is not moving
    stop_robot_motion();
    if (store_control_data_) 
    {
        log_file_cart_.close();
        log_file_joint_.close();
        log_file_predictions_.close();
    }
}