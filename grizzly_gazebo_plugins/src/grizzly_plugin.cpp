/**
Software License Agreement (BSD)

\file      grizzly_plugin.cpp
\authors   Yan Ma <yanma@clearpathrobotics.com>, Ryan Gariepy <rgariepy@clearpathrobotics.com>
\copyright Copyright (c) 2013, Clearpath Robotics, Inc., All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the 
   following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Clearpath Robotics nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WAR-
RANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, IN-
DIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
 

#include <boost/thread.hpp>
#include <nav_msgs/Odometry.h>
#include <sensor_msgs/JointState.h>
#include <geometry_msgs/Twist.h>
#include <grizzly_plugin/grizzly_plugin.h>
#include <ros/time.h>

using namespace gazebo;

enum {BL= 0, BR=1, FL=2, FR=3, FA=4};

GrizzlyPlugin::GrizzlyPlugin()
{
}

GrizzlyPlugin::~GrizzlyPlugin()
{
  delete rosnode_;
  delete spinner_thread_;
}

void GrizzlyPlugin::FiniChild()
{
  rosnode_->shutdown();
  spinner_thread_->join();
}
    
void GrizzlyPlugin::Load(physics::ModelPtr _parent, sdf::ElementPtr _sdf )
{
  this->model_ = _parent;
  this->world_ = this->model_->GetWorld();

  this->node_namespace_ = "";
  if (_sdf->HasElement("robotNamespace"))
    this->node_namespace_ = _sdf->GetElement("robotNamespace")->Get<std::string>() + "/";


  bl_joint_name_ = "backLeftJoint";
  if (_sdf->HasElement("backLeftJoint"))
    bl_joint_name_ = _sdf->GetElement("backLeftJoint")->Get<std::string>();

  br_joint_name_ = "backRightJoint";
  if (_sdf->HasElement("backRightJoint"))
    br_joint_name_ = _sdf->GetElement("backRightJoint")->Get<std::string>();

  fl_joint_name_ = "frontLeftJoint";
  if (_sdf->HasElement("frontLeftJoint"))
    fl_joint_name_ = _sdf->GetElement("frontLeftJoint")->Get<std::string>();

  fr_joint_name_ = "frontRightJoint";
  if (_sdf->HasElement("frontRightJoint"))
    fr_joint_name_ = _sdf->GetElement("frontRightJoint")->Get<std::string>();

  fa_joint_name_ = "frontAxelJoint";
  if (_sdf->HasElement("frontAxelJoint"))
    fa_joint_name_ = _sdf->GetElement("frontAxelJoint")->Get<std::string>();

  wheel_sep_ = 0.55;
  if (_sdf->HasElement("wheelSeparation"))
    wheel_sep_ = _sdf->GetElement("wheelSeparation")->Get<double>();

  wheel_diam_ = 0.30;
  if (_sdf->HasElement("wheelDiameter"))
    wheel_diam_ = _sdf->GetElement("wheelDiameter")->Get<double>();

  torque_ = 15.0;
  if (_sdf->HasElement("torque"))
    torque_ = _sdf->GetElement("torque")->Get<double>();

  base_geom_name_ = "base_link";
  if (_sdf->HasElement("baseGeom"))
    base_geom_name_ = _sdf->GetElement("baseGeom")->Get<std::string>();
  base_geom_ = model_->GetChildCollision(base_geom_name_);


  //base_geom_->SetContactsEnabled(true);

  // Get the name of the parent model
  std::string modelName = _sdf->GetParent()->Get<std::string>("name");
  gzdbg << "plugin model name: " << modelName << "\n";

  js_.name.push_back( bl_joint_name_ );
  js_.position.push_back(0);
  js_.velocity.push_back(0);
  js_.effort.push_back(0);

  js_.name.push_back( br_joint_name_ );
  js_.position.push_back(0);
  js_.velocity.push_back(0);
  js_.effort.push_back(0);

  js_.name.push_back( fl_joint_name_ );
  js_.position.push_back(0);
  js_.velocity.push_back(0);
  js_.effort.push_back(0);

  js_.name.push_back( fr_joint_name_ );
  js_.position.push_back(0);
  js_.velocity.push_back(0);
  js_.effort.push_back(0);

  js_.name.push_back( fa_joint_name_ );
  js_.position.push_back(0);
  js_.velocity.push_back(0);
  js_.effort.push_back(0);

  prev_update_time_ = 0;
  last_cmd_vel_time_ = 0;

  wheel_ang_vel_.rear_left = 0.0;
  wheel_ang_vel_.rear_right= 0.0;
  wheel_ang_vel_.front_left = 0.0;
  wheel_ang_vel_.front_left = 0.0;

  set_joints_[0] = false;
  set_joints_[1] = false;
  set_joints_[2] = false;
  set_joints_[3] = false;
  set_joints_[4] = false;

  //TODO: fix this
  joints_[BL] = model_->GetJoint(bl_joint_name_);
  joints_[BR] = model_->GetJoint(br_joint_name_);
  joints_[FL] = model_->GetJoint(fl_joint_name_);
  joints_[FR] = model_->GetJoint(fr_joint_name_);
  joints_[FA] = model_->GetJoint(fa_joint_name_);

  if (joints_[BL]) set_joints_[BL] = true;
  if (joints_[BR]) set_joints_[BR] = true;
  if (joints_[FL]) set_joints_[FL] = true;
  if (joints_[FR]) set_joints_[FR] = true;
  if (joints_[FA]) set_joints_[FA] = true;

  //initialize time and odometry position
  prev_update_time_ = last_cmd_vel_time_ = this->world_->GetSimTime();
  odom_pose_[0] = 0.0;
  odom_pose_[1] = 0.0;
  odom_pose_[2] = 0.0;

  // Initialize the ROS node and subscribe to cmd_vel
  int argc = 0;
  char** argv = NULL;
  ros::init(argc, argv, "gazebo_grizzly", ros::init_options::NoSigintHandler|ros::init_options::AnonymousName);
  rosnode_ = new ros::NodeHandle( node_namespace_ );

  drive_sub_ = rosnode_->subscribe("cmd_drive", 1, &GrizzlyPlugin::OnDrive, this );
  odom_pub_  = rosnode_->advertise<nav_msgs::Odometry>("odom", 1);
  joint_state_pub_ = rosnode_->advertise<sensor_msgs::JointState>("joint_states", 1);

  // Listen to the update event. This event is broadcast every
  // simulation iteration.
  this->spinner_thread_ = new boost::thread( boost::bind( &GrizzlyPlugin::spin, this) );
  this->updateConnection = event::Events::ConnectWorldUpdateBegin(boost::bind(&GrizzlyPlugin::UpdateChild, this));
}


void GrizzlyPlugin::UpdateChild()
{
  common::Time time_now = this->world_->GetSimTime();
  common::Time step_time = time_now - prev_update_time_;
  prev_update_time_ = time_now;

  double wd, ws;
  double d_bl, d_br, d_fl, d_fr;
  double dr, da;

  wd = wheel_diam_;
  ws = wheel_sep_;

  d_bl = d_br = d_fl = d_fr = 0;
  dr = da = 0;

  // Distance travelled by front wheels
  if (set_joints_[BL]) 
    d_bl = step_time.Double() * (wd / 2) * joints_[BL]->GetVelocity(0);
  if (set_joints_[BR])
    d_br = step_time.Double() * (wd / 2) * joints_[BR]->GetVelocity(0);
  if (set_joints_[FL])
    d_fl = step_time.Double() * (wd / 2) * joints_[FL]->GetVelocity(0);
  if (set_joints_[FR])
    d_fr = step_time.Double() * (wd / 2) * joints_[FR]->GetVelocity(0);

  // Can see NaN values here, just zero them out if needed
  if (isnan(d_bl)) {
    ROS_WARN_THROTTLE(0.1, "Gazebo ROS Grizzly plugin. NaN in d_bl. Step time: %.2f. WD: %.2f. Velocity: %.2f", step_time.Double(), wd, joints_[BL]->GetVelocity(0));
    d_bl = 0;
  }
  if (isnan(d_br)) {
    ROS_WARN_THROTTLE(0.1, "Gazebo ROS Grizzly plugin. NaN in d_br. Step time: %.2f. WD: %.2f. Velocity: %.2f", step_time.Double(), wd, joints_[BR]->GetVelocity(0));
    d_br = 0;
  }
  if (isnan(d_fl)) {
    ROS_WARN_THROTTLE(0.1, "Gazebo ROS Grizzly plugin. NaN in d_fl. Step time: %.2f. WD: %.2f. Velocity: %.2f", step_time.Double(), wd, joints_[FL]->GetVelocity(0));
    d_fl = 0;
  }
  if (isnan(d_fr)) {
    ROS_WARN_THROTTLE(0.1, "Gazebo ROS Grizzly plugin. NaN in d_fr. Step time: %.2f. WD: %.2f. Velocity: %.2f", step_time.Double(), wd, joints_[FR]->GetVelocity(0));
    d_fr = 0;
  }

  dr = (d_bl + d_br + d_fl + d_fr) / 4;
  da = ((d_br+d_fr)/2 - (d_bl+d_fl)/2) / ws;

  // Compute odometric pose
  odom_pose_[0] += dr * cos( odom_pose_[2] );
  odom_pose_[1] += dr * sin( odom_pose_[2] );
  odom_pose_[2] += da;

  // Compute odometric instantaneous velocity
  odom_vel_[0] = dr / step_time.Double();
  odom_vel_[1] = 0.0;
  odom_vel_[2] = da / step_time.Double();

  if (set_joints_[BL])
  {
    joints_[BL]->SetVelocity( 0, wheel_ang_vel_.rear_left);
    joints_[BL]->SetMaxForce( 0, torque_ );
  }
  if (set_joints_[BR])
  {
    joints_[BR]->SetVelocity( 0, wheel_ang_vel_.rear_right);
    joints_[BR]->SetMaxForce( 0, torque_ );
  }
  if (set_joints_[FL])
  {
    joints_[FL]->SetVelocity( 0, wheel_ang_vel_.front_left);
    joints_[FL]->SetMaxForce( 0, torque_ );
  }
  if (set_joints_[FR])
  {
    joints_[FR]->SetVelocity( 0, wheel_ang_vel_.front_right);
    joints_[FR]->SetMaxForce( 0, torque_ );
  }

  nav_msgs::Odometry odom;
  odom.header.stamp.sec = time_now.sec;
  odom.header.stamp.nsec = time_now.nsec;
  odom.header.frame_id = "odom";
  odom.child_frame_id = "base_footprint";
  odom.pose.pose.position.x = odom_pose_[0];
  odom.pose.pose.position.y = odom_pose_[1];
  odom.pose.pose.position.z = 0;

  tf::Quaternion qt;
  qt.setRPY(0,0,odom_pose_[2]);

  odom.pose.pose.orientation.x = qt.getX();
  odom.pose.pose.orientation.y = qt.getY();
  odom.pose.pose.orientation.z = qt.getZ();
  odom.pose.pose.orientation.w = qt.getW();

  double pose_cov[36] = { 1e-3, 0, 0, 0, 0, 0,
                          0, 1e-3, 0, 0, 0, 0,
                          0, 0, 1e6, 0, 0, 0,
                          0, 0, 0, 1e6, 0, 0,
                          0, 0, 0, 0, 1e6, 0,
                          0, 0, 0, 0, 0, 1e3};

  memcpy( &odom.pose.covariance[0], pose_cov, sizeof(double)*36 );
  memcpy( &odom.twist.covariance[0], pose_cov, sizeof(double)*36 );

  odom.twist.twist.linear.x = 0;
  odom.twist.twist.linear.y = 0;
  odom.twist.twist.linear.z = 0;

  odom.twist.twist.angular.x = 0;
  odom.twist.twist.angular.y = 0;
  odom.twist.twist.angular.z = 0;

  odom_pub_.publish( odom ); 

  js_.header.stamp.sec = time_now.sec;
  js_.header.stamp.nsec = time_now.nsec;
  if (this->set_joints_[BL])
  {
    js_.position[0] = joints_[BL]->GetAngle(0).Radian();
    js_.velocity[0] = joints_[BL]->GetVelocity(0);
  }

  if (this->set_joints_[BR])
  {
    js_.position[1] = joints_[BR]->GetAngle(0).Radian();
    js_.velocity[1] = joints_[BR]->GetVelocity(0);
  }

  if (this->set_joints_[FL])
  {
    js_.position[2] = joints_[FL]->GetAngle(0).Radian();
    js_.velocity[2] = joints_[FL]->GetVelocity(0);
  }

  if (this->set_joints_[FR])
  {
    js_.position[3] = joints_[FR]->GetAngle(0).Radian();
    js_.velocity[3] = joints_[FR]->GetVelocity(0);
  }

  if (this->set_joints_[FA])
  {
    js_.position[4] = joints_[FA]->GetAngle(0).Radian();
    js_.velocity[4] = joints_[FA]->GetVelocity(0);
  }

  joint_state_pub_.publish( js_ );

  // Timeout if we haven't received a cmd in <0.1 s
  common::Time time_since_last_cmd = time_now - last_cmd_vel_time_;
  if (time_since_last_cmd.Double() > 0.1)
  {
    wheel_ang_vel_.rear_left = 0;
    wheel_ang_vel_.rear_right = 0;
    wheel_ang_vel_.front_left = 0;
    wheel_ang_vel_.front_right = 0;
  }
}


void GrizzlyPlugin::OnDrive( const grizzly_msgs::DriveConstPtr &msg)
{
  last_cmd_vel_time_ = this->world_->GetSimTime();  
  wheel_ang_vel_ = *msg;
}

void GrizzlyPlugin::spin()
{
  while(ros::ok()) ros::spinOnce();
}

GZ_REGISTER_MODEL_PLUGIN(GrizzlyPlugin);
