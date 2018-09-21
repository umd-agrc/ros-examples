/****************************************************************************
 *   Copyright (c) 2016 Ramakrishna Kintada. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name ATLFlight nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/
#include "SnapdragonRosNodeVislam.hpp"

#include <geometry_msgs/PointStamped.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/Vector3Stamped.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <nav_msgs/Odometry.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/buffer.h>

Snapdragon::RosNode::Vislam::Vislam( ros::NodeHandle nh ) : nh_(nh)
{
  //pub_vislam_pose_ = nh_.advertise<geometry_msgs::PoseStamped>("vislam/pose",1);
  pub_vislam_pose_cov_ = nh_.advertise<geometry_msgs::PoseWithCovarianceStamped>("vislam/pose_cov",1);
  pub_vislam_speed_twist_ = nh_.advertise<geometry_msgs::TwistStamped>("vislam/speed_twist",1);
  //pub_vislam_speed_vec_ = nh_.advertise<geometry_msgs::Vector3Stamped>("vislam/speed_vec",1);
  //pub_vislam_odometry_ = nh_.advertise<nav_msgs::Odometry>("vislam/odometry",1);
  //pub_vislam_tbc_estimate_ = nh_.advertise<geometry_msgs::Vector3>("vislam/tbc",1);
  //pub_vislam_rbc_estimate_x_ = nh_.advertise<geometry_msgs::Vector3>("vislam/rbc_x", 1);
  //pub_vislam_rbc_estimate_y_ = nh_.advertise<geometry_msgs::Vector3>("vislam/rbc_y", 1);
  //pub_vislam_rbc_estimate_z_ = nh_.advertise<geometry_msgs::Vector3>("vislam/rbc_z", 1);
  vislam_initialized_ = false;
  thread_started_ = false;
  thread_stop_ = false;
  // sleep here so tf buffer can get populated
  ros::Duration(1).sleep(); // sleep for 1 second
}

Snapdragon::RosNode::Vislam::~Vislam()
{
  Stop();
}

int32_t Snapdragon::RosNode::Vislam::Initialize()
{ 
  vislam_initialized_ = true;
  return 0;
}

int32_t Snapdragon::RosNode::Vislam::Start() {
// start vislam processing thread.
  if( !thread_started_ ) {
    thread_started_ = true;
    vislam_process_thread_ = std::thread( &Snapdragon::RosNode::Vislam::ThreadMain, this );
  }
  else {
    ROS_WARN_STREAM( "Snapdragon::RosNodeVislam::Start() VISLAM Thread already running." );
  }
  return 0;
}

int32_t Snapdragon::RosNode::Vislam::Stop() {
  if( thread_started_ ) {
    thread_stop_ = true;
    if( vislam_process_thread_.joinable() ) {
      vislam_process_thread_.join();
    }
  }
  return 0;
}

void Snapdragon::RosNode::Vislam::ThreadMain() {
 mvCameraConfiguration config;
  // Set up camera configuraiton (snapdragon down facing camera)
  memset(&config, 0, sizeof(config));
  config.pixelWidth = 640;
  config.pixelHeight = 480;
  config.memoryStride = 640;

  config.principalPoint[0] = 320;
  config.principalPoint[1] = 240;

  config.focalLength[0] = 275;
  config.focalLength[1] = 275;

  config.distortion[0] = 0.003908;
  config.distortion[1] = -0.009574;
  config.distortion[2] = 0.010173;
  config.distortion[3] = -0.003329;
  config.distortion[4] = 0;
  config.distortion[5] = 0;
  config.distortion[6] = 0;
  config.distortion[7] = 0;
  config.distortionModel = 10;

  Snapdragon::VislamManager::InitParams vislamParams;

  // Transformation between camera and ROS IMU frame. It seems that mavros 
  // does not only invert the IMU Y- and Z-Axis, but also transforms the sensor
  // readings to the board coordinate frame. I did not confirm that in the mavros
  // sources, but the online translation and rotation estimates converge nicely
  // nicely to values supporting this hypothesis.
  vislamParams.tbc[0] = 0.009;  // default: 0.005
  vislamParams.tbc[1] = 0.000;  // default 0.015
  vislamParams.tbc[2] = 0.0;    // default 0.0

  vislamParams.ombc[0] = 2.221;   //  pi / sqrt(2)
  vislamParams.ombc[1] = -2.221;  // -pi / sqrt(2)
  vislamParams.ombc[2] = 0.0;

  vislamParams.delta = -0.008;

  vislamParams.std0Tbc[0] = 0.002;
  vislamParams.std0Tbc[1] = 0.002;
  vislamParams.std0Tbc[2] = 0.001;

  vislamParams.std0Ombc[0] = 0.0174532925199433;
  vislamParams.std0Ombc[1] = 0.0174532925199433;
  vislamParams.std0Ombc[2] = 0.0174532925199433;

  vislamParams.std0Delta = 0.001;
  vislamParams.accelMeasRange = 156;
  vislamParams.gyroMeasRange = 34;

  vislamParams.stdAccelMeasNoise = 0.316227766016838; // sqrt(1e-1);
  vislamParams.stdGyroMeasNoise = 1e-2; // sqrt(1e-4);

  vislamParams.stdCamNoise = 100;
  vislamParams.minStdPixelNoise = 0.5;
  vislamParams.failHighPixelNoisePoints = false;

  vislamParams.logDepthBootstrap = 0;
  vislamParams.useLogCameraHeight = false;
  vislamParams.logCameraHeightBootstrap = -3.22;
  vislamParams.noInitWhenMoving = true;
  vislamParams.limitedIMUbWtrigger = 35.0;  

  Snapdragon::CameraParameters param;
  param.enable_cpa = 1;
  param.camera_config.fps = 30;
  param.camera_config.cam_type = Snapdragon::CameraType::OPTIC_FLOW;
  param.mv_camera_config = config;

  //set the cpa configuration.
  mvCPA_Configuration cpaConfig;
  cpaConfig.cpaType = MVCPA_MODE_COST;
  cpaConfig.legacyCost.startExposure = param.camera_config.exposure;
  cpaConfig.legacyCost.startGain = param.camera_config.gain;
  cpaConfig.legacyCost.filterSize = 1;
  cpaConfig.legacyCost.exposureCost = 1.0f;
  cpaConfig.legacyCost.gainCost = 0.3333f;

  param.mv_cpa_config = cpaConfig;   
  Snapdragon::VislamManager vislam_man(nh_);
  if( vislam_man.Initialize( param, vislamParams ) != 0  ) {
    ROS_WARN_STREAM( "Snapdragon::RosNodeVislam::VislamThreadMain: Error initializing the VISLAM Manager " );
    thread_started_ = false;
    return;
  }

// start the VISLAM processing.
  if( vislam_man.Start() != 0 ) {
    ROS_WARN_STREAM( "Snapdragon::RosNodeVislam::VislamThreadMain: Error Starting the VISLAM manager" );
    thread_started_ = false;
    return;
  }

  mvVISLAMPose vislamPose;
  int64_t vislamFrameId;
  uint64_t timestamp_ns;
  thread_stop_ = false;
  int32_t vislam_ret;
  while( !thread_stop_ ) {
    vislam_ret = vislam_man.GetPose( vislamPose, vislamFrameId, timestamp_ns );
    if( vislam_ret == 0 ) {
      //check if the pose quality is good.  If not do not publish the data.
      if( vislamPose.poseQuality != MV_TRACKING_STATE_FAILED  && 
          vislamPose.poseQuality != MV_TRACKING_STATE_INITIALIZING ) {
          // Publish Pose Data
          PublishVislamData( vislamPose, vislamFrameId, timestamp_ns );
      }

      // Log changes in tracking state
      if (previous_mv_tracking_state_ != vislamPose.poseQuality)
      {
        switch (vislamPose.poseQuality)
        {
          case MV_TRACKING_STATE_FAILED:
            ROS_INFO_THROTTLE(1, "VISLAM TRACKING FAILED");
            break;

          case MV_TRACKING_STATE_INITIALIZING:
            ROS_INFO_THROTTLE(1, "VISLAM INITIALIZING");
            break;

          case MV_TRACKING_STATE_HIGH_QUALITY:
            ROS_INFO_THROTTLE(1, "VISLAM TRACKING HIGH QUALITY");
            break;

          case MV_TRACKING_STATE_LOW_QUALITY:
            ROS_INFO_THROTTLE(1, "VISLAM TRACKING LOW QUALITY");
            break;
        }
      }
      previous_mv_tracking_state_ = vislamPose.poseQuality;
    }
    else {
      ROS_WARN_STREAM( "Snapdragon::RosNodeVislam::VislamThreadMain: Warning Getting Pose Information" );
    }
  }
  thread_started_ = false;
  // the thread is shutting down. Stop the vislam Manager.
  vislam_man.Stop();
  ROS_INFO_STREAM( "Snapdragon::RosNodeVislam::VislamThreadMain: Exising VISLAM Thread" );
  return;
}

int32_t Snapdragon::RosNode::Vislam::PublishVislamData( mvVISLAMPose& vislamPose, int64_t vislamFrameId, uint64_t timestamp_ns  ) {
  geometry_msgs::PoseStamped pose_msg;
  ros::Time frame_time;
  frame_time.sec = (int32_t)(timestamp_ns/1000000000UL);
  frame_time.nsec = (int32_t)(timestamp_ns % 1000000000UL);
  pose_msg.header.frame_id = "vislam";
  pose_msg.header.stamp = frame_time;
  pose_msg.header.seq = vislamFrameId;

  // translate vislam pose to ROS pose
  tf2::Matrix3x3 R(
    vislamPose.bodyPose.matrix[0][0],
    vislamPose.bodyPose.matrix[0][1],
    vislamPose.bodyPose.matrix[0][2],
    vislamPose.bodyPose.matrix[1][0],
    vislamPose.bodyPose.matrix[1][1],
    vislamPose.bodyPose.matrix[1][2],
    vislamPose.bodyPose.matrix[2][0],
    vislamPose.bodyPose.matrix[2][1],
    vislamPose.bodyPose.matrix[2][2]); 
  tf2::Quaternion q;
  R.getRotation(q);
  pose_msg.pose.position.x = vislamPose.bodyPose.matrix[0][3];
  pose_msg.pose.position.y = vislamPose.bodyPose.matrix[1][3];
  pose_msg.pose.position.z = vislamPose.bodyPose.matrix[2][3];
  pose_msg.pose.orientation.x = q.getX();
  pose_msg.pose.orientation.y = q.getY();
  pose_msg.pose.orientation.z = q.getZ();
  pose_msg.pose.orientation.w = q.getW();
  //pub_vislam_pose_.publish(pose_msg);
  
  /*
  // Publish translation and rotation estimates
  geometry_msgs::Vector3 tbc_msg;
  tbc_msg.x = vislamPose.tbc[0];
  tbc_msg.y = vislamPose.tbc[1];
  tbc_msg.z = vislamPose.tbc[2];
  pub_vislam_tbc_estimate_.publish(tbc_msg);
  
  geometry_msgs::Vector3 rbc_x_msg;
  rbc_x_msg.x = vislamPose.Rbc[0][0];
  rbc_x_msg.y = vislamPose.Rbc[0][1];
  rbc_x_msg.z = vislamPose.Rbc[0][2];
  pub_vislam_rbc_estimate_x_.publish(rbc_x_msg);
  
  geometry_msgs::Vector3 rbc_y_msg;
  rbc_y_msg.x = vislamPose.Rbc[1][0];
  rbc_y_msg.y = vislamPose.Rbc[1][1];
  rbc_y_msg.z = vislamPose.Rbc[1][2];
  pub_vislam_rbc_estimate_y_.publish(rbc_y_msg);
  
  geometry_msgs::Vector3 rbc_z_msg;
  rbc_z_msg.x = vislamPose.Rbc[2][0];
  rbc_z_msg.y = vislamPose.Rbc[2][1];
  rbc_z_msg.z = vislamPose.Rbc[2][2];
  pub_vislam_rbc_estimate_z_.publish(rbc_z_msg);
  */
  
  //publish the odometry message.
  nav_msgs::Odometry odom_msg;
  odom_msg.header.stamp = frame_time;
  odom_msg.header.frame_id = "vislam";
  odom_msg.pose.pose = pose_msg.pose;
  odom_msg.twist.twist.linear.x = vislamPose.velocity[0];
  odom_msg.twist.twist.linear.y = vislamPose.velocity[1];
  odom_msg.twist.twist.linear.z = vislamPose.velocity[2];
  odom_msg.twist.twist.angular.x = vislamPose.angularVelocity[0];
  odom_msg.twist.twist.angular.y = vislamPose.angularVelocity[1];
  odom_msg.twist.twist.angular.z = vislamPose.angularVelocity[2];

  //set the error covariance for the pose.
  for( int16_t i = 0; i < 6; i++ ) {
    for( int16_t j = 0; j < 6; j++ ) {
      odom_msg.pose.covariance[ i*6 + j ] = vislamPose.errCovPose[i][j];
    }
  }
  //pub_vislam_odometry_.publish(odom_msg);

  //publish the velocity message
  geometry_msgs::TwistStamped speed_twist_msg;
  speed_twist_msg.header = odom_msg.header;
  speed_twist_msg.twist = odom_msg.twist.twist;
  pub_vislam_speed_twist_.publish(speed_twist_msg);

/*
  //TEST publish velocity vector message
  geometry_msgs::Vector3Stamped speed_vec_msg;
  speed_vec_msg.header = odom_msg.header;
  speed_vec_msg.vector.x = vislamPose.velocity[0];
  speed_vec_msg.vector.y = vislamPose.velocity[1];
  speed_vec_msg.vector.z = vislamPose.velocity[2];
  pub_vislam_speed_vec_.publish(speed_vec_msg);
*/

  // Publish pose with covariance (for mavros)
  geometry_msgs::PoseWithCovarianceStamped pose_cov_msg;
  pose_cov_msg.header = odom_msg.header;
  pose_cov_msg.pose = odom_msg.pose;
  pub_vislam_pose_cov_.publish(pose_cov_msg);

  // compute transforms
  std::vector<geometry_msgs::TransformStamped> transforms;
  geometry_msgs::TransformStamped transform;
  transform.transform.translation.x = pose_msg.pose.position.x;
  transform.transform.translation.y = pose_msg.pose.position.y;
  transform.transform.translation.z = pose_msg.pose.position.z;
  transform.transform.rotation.x = pose_msg.pose.orientation.x;
  transform.transform.rotation.y = pose_msg.pose.orientation.y;
  transform.transform.rotation.z = pose_msg.pose.orientation.z;
  transform.transform.rotation.w = pose_msg.pose.orientation.w;
  transform.child_frame_id = "base_link_vislam";
  transform.header.frame_id = "vislam";
  transform.header.stamp = frame_time;

  // collect transforms
  transforms.push_back(transform);

  // broadcast transforms
  static tf2_ros::TransformBroadcaster br;
  br.sendTransform(transforms);     
}
