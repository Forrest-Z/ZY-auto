#include <ros/ros.h>
#include "control.hpp"
#include <sstream>

namespace ns_control
{
  // Constructor
  Control::Control(ros::NodeHandle &nh) : nh_(nh),
                                          pid_controller(1.0, 0.0, 0.0),
                                          pp_controller(3.975)
                                          {
                                            initial_stage = true;
                                          };

  // Getters
  common_msgs::ChassisControl Control::getChassisControlCommand(){
    return chassis_control_command;
  }
  geometry_msgs::PointStamped Control::getLookaheadPoint() { 
    lookahead_point.header.frame_id = "world";
    lookahead_point.header.stamp = ros::Time::now();
    return lookahead_point; 
  }

  geometry_msgs::PointStamped Control::getNearestPoint(){
    nearest_point.header.frame_id = "world";
    nearest_point.header.stamp = ros::Time::now();
    return nearest_point;
  }

  common_msgs::ControlState Control::getControlState(){
    control_state.header.frame_id = "world";
    control_state.header.stamp = ros::Time::now();
    return control_state;
  }

  common_msgs::Trigger Control::getReplayTrigger(){
    // replay_trigger.trigger = true;
    return replay_trigger;
  }

  // Setters
  void Control::setFinalWaypoints(const autoware_msgs::Lane &msg){
    final_waypoints = msg;
    current_waypoints = final_waypoints.waypoints;
  }
  void Control::setVehicleDynamicState(const common_msgs::ChassisState &msg){
    vehicle_dynamic_state = msg;
    // ROS_INFO_STREAM("[Control]current speed: " << vehicle_state.twist.linear.x);
  }
  void Control::setUtmPose(const nav_msgs::Odometry &msg){
    utm_pose = msg; 
    current_pose = utm_pose.pose.pose;
  }
  void Control::setVirtualVehicleState(const common_msgs::VirtualVehicleState &msg){
    virtual_vehicle_state = msg;
    // ROS_INFO("virtual vehicle state: distance: %f, speed: %f.",virtual_vehicle_state.distance,
    //           virtual_vehicle_state.utmpose.twist.twist.linear.x);
  }

  void Control::setPidParameters(const Pid_para &msg){
    pid_para = msg;
    pid_controller.kp = pid_para.kp;
    pid_controller.ki = pid_para.ki;
    pid_controller.kd = pid_para.kd;
  }
  void Control::setPurePursuitParameters(const Pure_pursuit_para &msg){
    pp_para = msg;
    if (pp_para.mode == "fixed"){
      lookahead_distance = pp_para.lookahead_distance;
    }
  }

  void Control::setLQRParameters(const LQR_para &msg){
    lqr_para = msg;
    lqr_controller.lqr_para_filename = lqr_para.para_filename;
    lqr_controller.readLQRParameters();
  }

  void Control::setControlParameters(const Para &msg){
    control_para = msg;
    
  }
  int Control::findNearestWaypoint(){
    int waypoints_size = current_waypoints.size();
    if (waypoints_size == 0){
      ROS_WARN("No waypoints in final_waypoints.");
      return -1;
    }

    // find nearest point
    int nearest_idx = 0;
    double nearest_distance = getPlaneDistance(current_waypoints.at(0).pose.pose.position,current_pose.position);
    for (int i = 0; i < waypoints_size; i++){
      // if search waypoint is the last
      if(i == (waypoints_size - 1)){
        ROS_INFO("search waypoint is the last");
        // break;
      }
      double dis = getPlaneDistance(current_waypoints.at(i).pose.pose.position,current_pose.position);
      if (dis < nearest_distance){
        nearest_idx = i;
        nearest_distance = dis;
      }
    }
    nearest_waypoint = final_waypoints.waypoints[nearest_idx];
    nearest_ps = nearest_waypoint.pose;
    nearest_point.point = nearest_ps.pose.position;
    return nearest_idx;
  }

  int Control::findLookAheadWaypoint(float lookAheadDistance){
    int waypoints_size = current_waypoints.size();
    int nearest_waypoint_idx = findNearestWaypoint();
    if (nearest_waypoint_idx < 0 | nearest_waypoint_idx == waypoints_size - 1){
      return -1;
    }
    
    // look for the next waypoint
    for (int j = nearest_waypoint_idx; j < waypoints_size; j++){
      // if search waypoint is the last
      if (j == (waypoints_size - 1)){
        ROS_INFO("search waypoints is the last");
      }
      // if there exists an effective waypoint
      if (getPlaneDistance(current_waypoints.at(j).pose.pose.position, current_pose.position) > lookAheadDistance){
          lookahead_waypoint = final_waypoints.waypoints[j];
          lookahead_ps = lookahead_waypoint.pose;
          lookahead_point.point = lookahead_ps.pose.position;
        return j;
      }
    }
  }

  double Control::latControlUpdate(){
    // State update
    double v_x = utm_pose.twist.twist.linear.x;
    double v_y = -utm_pose.twist.twist.linear.y;

    double yaw_rate = utm_pose.twist.twist.angular.z; 
    yaw_rate = v_y/ 3.89 / 0.55/ 180.0*M_PI;
    double curvature = 0;

    // calculate yaw 
    tf::Quaternion quat,near_quat;
    tf::quaternionMsgToTF(current_pose.orientation, quat);
    tf::quaternionMsgToTF(nearest_ps.pose.orientation, near_quat);
    double roll, pitch, near_yaw, cur_yaw;
    tf::Matrix3x3(quat).getRPY(roll, pitch, near_yaw);
    tf::Matrix3x3(quat).getRPY(roll, pitch, cur_yaw);

    control_state.lateral_error = calcRelativeCoordinate(nearest_ps.pose.position, current_pose).y;
    control_state.heading_error = near_yaw - cur_yaw;

    if( control_state.heading_error > M_PI) control_state.heading_error = control_state.heading_error-2*M_PI;
    else if( control_state.heading_error < -M_PI) control_state.heading_error = control_state.heading_error+ 2*M_PI;

    if (pp_para.mode == "variable"){
        lookahead_distance = pp_para.lookahead_distance;
    }
    int lookahead_waypoint_idx = findLookAheadWaypoint(lookahead_distance);

    ROS_INFO_STREAM("[Control] lookahead waypoint idx: " << lookahead_waypoint_idx
                    << ", x: " << lookahead_ps.pose.position.x << ", y: " << lookahead_ps.pose.position.y);
    
    // calculate front wheel angle
    double front_wheel_angle;
    ROS_INFO_STREAM("lat control id: " << control_para.lat_controller_id);
    switch (control_para.lat_controller_id){
      case 1: // pure pursuit controller
        {
          front_wheel_angle = pp_controller.outputFrontWheelAngle(lookahead_ps.pose.position,current_pose);
          ROS_INFO_STREAM("Using pure pursuit controller, output: " << front_wheel_angle);  
        }
        
        break;

      case 2: {// lqr controller
          ROS_INFO("Using lqr controller.");
          geometry_msgs::PoseStamped ref_ps;
          ref_ps = lookahead_ps;
          double lateral_error = calcRelativeCoordinate(ref_ps.pose.position, current_pose).y;
          
          tf::Quaternion ref_quat;
          tf::quaternionMsgToTF(ref_ps.pose.orientation, ref_quat);
          double ref_yaw;
          tf::Matrix3x3(ref_quat).getRPY(roll, pitch, ref_yaw);
          double heading_error = ref_yaw - cur_yaw;
          double dot_lateral_error = v_y + v_x * heading_error;
          double dot_heading_error = -v_x * curvature + yaw_rate;
          ROS_INFO_STREAM("lateral error: " << lateral_error << ", dot_lateral_error: " << dot_lateral_error
                        <<"heading error: " << heading_error << ", dot_heading_error: " << dot_heading_error);
          double tmp[4] = {lateral_error,dot_lateral_error,heading_error,dot_heading_error};
          std::vector<double> current_state(tmp,tmp+4);
          front_wheel_angle = lqr_controller.outputFrontWheelAngle(v_x,current_state);
        }
        break;
      case 3:{
          ROS_INFO("Feedforward plus feedback.");
          if (virtualFlag){

          }

      }
      default:{
        front_wheel_angle = 0;
        ROS_WARN("Illegal controller id!");
      }
        
        break;
    }
    return front_wheel_angle;
  }

  double Control::lonControlUpdate(){
    double desired_pedal = 0;
    double desired_pedal_fb = 0;
    double cur_spd = utm_pose.twist.twist.linear.x;

    switch (control_para.longitudinal_mode){
      case 1:{// follow desired speed
        break;
      }
      case 2:{// follow planned speed
        break;
      }
      case 3:{// keep desired distance virtual

          // virtual vehicle not triggered or control the 1st vehicle
          if (!virtualFlag || control_para.is_first_vehicle){
            if (initial_stage){
              // desired_pedal = 25; // accelerate to desired speed
              desired_pedal = - pid_controller.outputSignal(control_para.desired_speed,cur_spd);
              if(cur_spd >= control_para.trigger_speed){
                initial_stage = false; // if the first vehicle get desired speed
                last_point = current_pose.position;
                replay_trigger.trigger = true; // trigger log and replay when the speed arrives at desired speed
                ROS_INFO("[Lon Control] Accelerate to trigger data replay. ");
              }
            }else{
              // stage 2 for the 1st vehicle: keep desired speed 
              desired_pedal = - pid_controller.outputSignal(control_para.desired_speed,cur_spd);
              // ROS_INFO("[Lon Control] Keep desired speed: %f, current speed: %f",control_para.desired_speed,cur_spd);
            }
            return desired_pedal;
          }

          //virtual vehicle is triggered
          // error defination
          double cur_dis = (virtual_vehicle_state.distance - distance);
          double e_d = cur_dis - control_para.desired_distance;
          double pre_spd = virtual_vehicle_state.utmpose.twist.twist.linear.x;
          double e_v = pre_spd - cur_spd;
          double pre_acc = virtual_vehicle_state.chassis_state.vehicle_lon_acceleration;
          double e_a = pre_acc - vehicle_dynamic_state.vehicle_lon_acceleration;
          ROS_INFO_STREAM("[Control] distance to pre vehicle: " << cur_dis 
                          << ", speed error: " << e_v);

          // constraints variable initialization
          double s_i = 0;
          double abs_s = 0;
          double gamma = 0;

          // update distance
          distance += getPlaneDistance(current_pose.position,last_point);
          last_point = current_pose.position;

          //  select controller
          switch (control_para.lon_controller_id){
            case 1:{// PID controller
              desired_pedal_fb = control_para.k_d * e_d + control_para.k_v * e_v;
              break;
            }
            case 2:{// CFC controller 
              s_i = e_v + control_para.lmd * e_d;
              abs_s = abs(s_i);
              if (abs_s > control_para.eps){
                gamma = 1/abs_s;
              }else{
                gamma = 1/control_para.eps;
              }
              desired_pedal_fb = (control_para.k_s + control_para.k_u * gamma) * s_i; 
              break;
            }
            case 3:{// TCFC controller
              double z = 0;
              double d_z = 0;
              double tmp = control_para.c_0 * e_d + control_para.c_1;
              if (tmp > M_PI/2){
                tmp = M_PI/2 - 0.1;
              }else{
                if(tmp < -M_PI/2){
                  tmp = -M_PI/2 + 0.1;
                }
              }
              z = 10 * (tan(tmp) + control_para.c_2);
              d_z = 10 * control_para.c_0 * e_v / (pow(cos(tmp),2));
              // constraints
              s_i = d_z + control_para.lmd * z;
              abs_s = abs(s_i);
              if (abs_s > control_para.eps){
                gamma = 1/abs_s;
              }else{
                gamma = 1/control_para.eps;
              }
              desired_pedal_fb = control_para.k_s * s_i + control_para.k_u * s_i * gamma;
              break;
            }
            default:{
              break;
            }

            // add feedforward control value
            double desired_pedal_ff;
            double pre_pedal_acc = virtual_vehicle_state.chassis_state.real_acc_pedal;
            double pre_pedal_brake = virtual_vehicle_state.chassis_state.real_brake_pedal;
            
            if (pre_pedal_acc > pre_pedal_brake){
              desired_pedal_ff = pre_pedal_acc;
            }else{
              desired_pedal_ff = -pre_pedal_brake;
            }
            double desired_pedal = control_para.p_ff * desired_pedal_ff + control_para.p_fb * desired_pedal_fb;

          }
        break;
      }
      default:{
        ROS_WARN("No such longitudinal controller.");
      }
    }

    
    return desired_pedal;
  }

  void Control::runAlgorithm(){

    ROS_DEBUG("[Control]In run() ... ");

    // if (vehicleDynamicStateFlag && (finalWaypointsFlag||control_para.is_first_vehicle) && utmPoseFlag){
    if ( (finalWaypointsFlag||control_para.is_first_vehicle||!control_para.lateral_control_switch) && utmPoseFlag){
      /********************** 
        Lateral control
      ***********************/
      if (control_para.lateral_control_switch){
        // limit front wheel angle
        double front_wheel_angle = latControlUpdate();
        BOUND(front_wheel_angle,LIMIT_STEERING_ANGLE,-LIMIT_STEERING_ANGLE);
        // convert front wheel angle to steering wheel angle
        chassis_control_command.steer_angle = 
                - (24.1066 * front_wheel_angle + 4.8505);
     
        ROS_INFO_STREAM("[Contorl] chassis_control_command steer angle: " << chassis_control_command.steer_angle);
      }
      else{
        chassis_control_command.steer_angle = 0;
        // ROS_INFO_STREAM("[Contorl] Lateral control disabled");
      }
     
      /********************** 
        Longitudinal control
      ***********************/
      if (control_para.longitudinal_control_switch){
        
        double desired_pedal = lonControlUpdate();

        // set dead zone
        if (desired_pedal > -2 && desired_pedal < 2){
          desired_pedal = 0;
        }
        // set bound
        BOUND(desired_pedal,65,-40);

        ROS_INFO_STREAM("[Control] desired_pedal: " << desired_pedal
              <<", current speed: " << utm_pose.twist.twist.linear.x
              <<", distance: " << distance);
        
        if (desired_pedal > 0){
          chassis_control_command.acc_pedal_open_request = desired_pedal;
          chassis_control_command.brk_pedal_open_request = 0;
        }else{
          chassis_control_command.brk_pedal_open_request = -desired_pedal;
          chassis_control_command.acc_pedal_open_request =  0;
        }
      }
      else{
        ROS_INFO_STREAM("[Contorl] Longitudinal control disabled");
      }
    }else{
      ROS_WARN_STREAM("[Control] utmPoseFlag: " << utmPoseFlag << ", vehicleDynamicStateFlag: " << vehicleDynamicStateFlag
                      << ", finalWaypointsFlag: " << finalWaypointsFlag << ", is_first_vehicle: " << control_para.is_first_vehicle);
    }
  }
}
