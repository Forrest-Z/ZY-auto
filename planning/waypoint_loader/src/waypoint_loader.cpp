/*
    Formula Student Driverless Project (FSD-Project).
    Copyright (c) 2019:
     - chentairan <killasipilin@gmail.com>

    FSD-Project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FSD-Project is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FSD-Project.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <ros/ros.h>
#include "waypoint_loader.hpp"
#include <sstream>

namespace ns_waypoint_loader {
// Constructor
Waypoint_loader::Waypoint_loader(ros::NodeHandle &nh) : nh_(nh) {
    line_num = -1;
};

// Getters
autoware_msgs::Lane Waypoint_loader::getGlobalPath() {return global_path;}
nav_msgs::Path Waypoint_loader::getGlobalPathVisual() {return global_path_rviz;}

// Methods
/*
void Waypoint_loader::loadWaypointFile(std::string filename){
    // Initialization
    global_path.header.frame_id = "world";
    global_path.waypoints.clear();
    global_path_rviz.header.frame_id = "world";
    global_path_rviz.poses.clear();

    // Load recorded waypoint file
    FILE *file = fopen(filename.c_str(), "rt");
    ROS_INFO("open file [%s]",filename.c_str());
    if (file != nullptr){
        double x,y;
        while (fscanf(file, "%lf,%lf",&x,&y) != EOF) 
        {    
            // Read as Point vector    
            common_msgs::Waypoint point;

            point.pose.pose.position.x = x;
            point.pose.pose.position.y = y;
            point.pose.pose.position.z = 0;
            
            point.pose.pose.orientation.x = 1;
            point.pose.pose.orientation.y = 0;
            point.pose.pose.orientation.z = 0;
            point.pose.pose.orientation.w = 0;
            
            global_path.waypoints.push_back(point);

            // visualization : convert to Path msgs
            geometry_msgs::PoseStamped pose;

            pose.header.stamp = ros::Time(0);
            pose.pose = point.pose.pose;

            global_path_rviz.poses.push_back(pose);
        }
        ROS_INFO("global path length is: %lu",global_path.waypoints.size());
        // TODO: Convert the gps data to XYZ system
        fclose(file);

        return;
    }
    ROS_ERROR("Failed to load path files!");
}
*/
void Waypoint_loader::loadWaypointFile(std::string filename){
    // Initialization
    global_path.header.frame_id = "world";
    global_path.waypoints.clear();
    global_path_rviz.header.frame_id = "world";
    global_path_rviz.poses.clear();

    waypoint_record_file.open(filename.c_str(),std::ios::in);
    ROS_INFO("open file [%s]",filename.c_str());
    char linestr[500] = {0};
    waypoint_record_file.getline(linestr, 500);
    while (waypoint_record_file.getline(linestr, 500)){
        line_num += 1;
        if (line_num == 0){
            continue;
        }
        std::stringstream ss(linestr);
        std::string csvdata[8];
        for (int i = 0; i < 8; i++) {
            char tempdata[500] = {0};
            ss.getline(tempdata, 500, ',');
            csvdata[i] = std::string(tempdata);
        }   
        double x,y,heading,v_x,v_y,yaw_rate;

        x = atof(csvdata[2].data());
        y = atof(csvdata[3].data());
        heading = atof(csvdata[4].data());
        v_x = atof(csvdata[5].data());
        v_y = atof(csvdata[6].data());
        yaw_rate = atof(csvdata[7].data());

        autoware_msgs::Waypoint point;
        point.pose.pose.position.x = x;
        point.pose.pose.position.y = y;
        point.pose.pose.position.z = 0;

        point.pose.pose.orientation = tf::createQuaternionMsgFromYaw(heading);
        
        point.twist.twist.linear.x = v_x;
        point.twist.twist.linear.y = v_y;
        point.twist.twist.angular.z = yaw_rate;

        global_path.waypoints.push_back(point);

        // visualization : convert to Path msgs
        geometry_msgs::PoseStamped pose;

        pose.header.stamp = ros::Time::now();
        pose.pose = point.pose.pose;

        global_path_rviz.poses.push_back(pose);  
    }
    waypoint_record_file.close();

}

void Waypoint_loader::runAlgorithm() {
}
}
