/*
 * This file is part of the libomnidrive project.
 *
 * Copyright (C) 2016 Georg Bartels <georg.bartels@cs.uni-bremen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef OMNI_ETHERCAT_OMNILIB_HPP
#define OMNI_ETHERCAT_OMNILIB_HPP

#include <eigen3/Eigen/Dense>
#include <tf/transform_datatypes.h>
#include <geometry_msgs/Twist.h>

namespace omni_ethercat
{
  typedef Eigen::Matrix< double, 3, 4 > OmniJac;
  typedef Eigen::Matrix< double, 4, 3 > OmniJacInv;
  typedef Eigen::Vector3d Pose2d;
  typedef Eigen::Vector3d Twist2d;
  typedef Eigen::Vector4d OmniEncPos;
  typedef Eigen::Vector4d OmniEncVel;

  inline geometry_msgs::Pose toPoseMsg(const Pose2d& pose)
  {
    geometry_msgs::Pose msg;
    msg.position.x = pose(0);
    msg.position.y = pose(1);
    msg.orientation = tf::createQuaternionMsgFromYaw(pose(2));
    return msg;
  }

  inline Pose2d fromPoseMsg(const geometry_msgs::Pose& msg)
  {
    Pose2d pose;
    pose(0) = msg.position.x;
    pose(1) = msg.position.y;
    pose(2) = tf::getYaw(msg.orientation);
    return pose;
  }

  inline geometry_msgs::Twist toTwistMsg(const Twist2d& twist)
  {
    geometry_msgs::Twist msg;
    msg.linear.x = twist(0);
    msg.linear.y = twist(1);
    msg.angular.z = twist(2);
    return msg;
  }

  inline Twist2d fromTwistMsg(const geometry_msgs::Twist& msg)
  {
    Twist2d twist;
    twist(0) = msg.linear.x;
    twist(1) = msg.linear.y;
    twist(2) = msg.angular.z;
    return twist;
  }

  class JacParams
  {
    public:
      JacParams() :
        lx(0.0), ly(0.0), drive_constant(0.0) {}
      JacParams(double lx, double ly, double drive_constant) :
        lx(lx), ly(ly), drive_constant(drive_constant) {}
      JacParams(const JacParams& other) :
        lx(other.lx), ly(other.ly), drive_constant(other.drive_constant) {}
      ~JacParams() {}
      double lx, ly, drive_constant;
  };

  inline OmniJac getJacobian(double lx, double ly, double drive_constant)
  {
    using Eigen::operator<<;
    OmniJac jac;
    double a = drive_constant * 4.0;
    double b = drive_constant * (lx + ly);
    jac << 1/a,  1/a,  1/a,  1/a,
          -1/a,  1/a,  1/a,  -1/a,
          -1/b, 1/b, -1/b, 1/b;
    return jac;
  }

  inline OmniJac getJacobian(const JacParams& params)
  {
    return getJacobian(params.lx, params.ly, params.drive_constant);
  }

  inline OmniJacInv getJacobianInverse(double lx, double ly, double drive_constant)
  {
    using Eigen::operator<<;
    OmniJacInv jac;
    double a = drive_constant;
    double b = drive_constant/(lx + ly);
    jac << a, -a, -b,
           a,  a,  b,
           a,  a, -b,
           a, -a,  b;
    return jac;
  }

  inline OmniJacInv getJacobianInverse(const JacParams& params)
  {
    return getJacobianInverse(params.lx, params.ly, params.drive_constant);
  }
 
  inline Twist2d omniFK(double lx, double ly, double drive_constant, const OmniEncVel& delta_wheels)
  {
    using Eigen::operator*;
    return getJacobian(lx, ly, drive_constant) * delta_wheels;
  }

  inline Twist2d omniFK(const JacParams& params, const OmniEncVel& delta_wheels)
  {
    return omniFK(params.lx, params.ly, params.drive_constant, delta_wheels);
  }

  inline OmniEncVel omniIK(double lx, double ly, double drive_constant, const Twist2d& twist_2d)
  {
    using Eigen::operator*;
    return getJacobianInverse(lx, ly, drive_constant) * twist_2d;
  }

  inline OmniEncVel omniIK(const JacParams& params, const Twist2d& twist_2d)
  {
    return omniIK(params.lx, params.ly, params.drive_constant, twist_2d);
  }

  Pose2d calcOdometry(const Pose2d& last_odom, const OdomEncPos& old_encoder_pos,
      const OdomEncPos& current_encoder_pos, const JacParams&)
  {
    OmniEncVel delta_wheels = current_encoder_pos - old_encoder_pos;
    Twist2d twist_2d = omniFK(JacParams, delta_wheels);
    // TODO: move this into a separate function
    // TODO: figure out why we are using only half of the rotational velocity
    double angle = last_odom(2) + twist_2d(2)/2.0;
    Eigen::Matrix< double, 3, 3 > transform;
    using Eigen::operator<<;
    transform << cos(angle), -sin(angle),  0,
                 sin(angle),  cos(angle),  0,
                 0         ,  0         ,  1;

    return transform * twist_2d + last_odom;
  }

  Pose2d calcOdometry(const Pose2d& last_odom, const OdomEncPos& old_encoder_pos,
      const OdomEncPos& current_encoder_pos, double lx, double ly, double drive_constant)
  {
    return calcOdometry(last_odom, old_encoder_pos, current_encoder_pos, 
        JacParams(lx, ly, drive_constant));
  }
}

#endif // OMNI_ETHERCAT_OMNILIB_HPP
