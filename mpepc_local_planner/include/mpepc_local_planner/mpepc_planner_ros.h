/*********************************************************************
 *
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, Austin Hendrix
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Austin Hendrix
 *********************************************************************/
#ifndef MPEPC_LOCAL_PLANNER_MPEPC_PLANNER_ROS_H_
#define MPEPC_LOCAL_PLANNER_MPEPC_PLANNER_ROS_H_

#include <navfn/navfn.h>
#include <nav_msgs/Odometry.h>
#include <costmap_2d/costmap_2d_ros.h>
#include <nav_core/base_local_planner.h>
#include <base_local_planner/odometry_helper_ros.h>
#include <base_local_planner/latched_stop_rotate_controller.h>
#include <mpepc_local_planner/control_law.h>
#include <mpepc_local_planner/EgoGoal.h>
#include <mpepc_local_planner/MPEPCPlannerConfig.h>
#include <math.h>
#include <string>
#include <vector>

#include <visualization_msgs/Marker.h>
#include <visualization_msgs/MarkerArray.h>

#include <std_msgs/Int32.h>
#include <std_msgs/Float32MultiArray.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/PoseArray.h>
#include <geometry_msgs/Vector3.h>
#include <geometry_msgs/Quaternion.h>
#include <nav_msgs/GridCells.h>

#include "tf2_ros/transform_listener.h"
#include "tf2_ros/message_filter.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.h"

#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>

#include <nlopt.hpp>
#include "flann/flann.hpp"

namespace mpepc_local_planner
{
#define RATE_FACTOR 0.2
#define DEFAULT_LOOP_RATE 10

#define RESULT_BEGIN 1
#define RESULT_SUCCESS 2
#define RESULT_CANCEL 3

	static const double PI = 3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348;
	static const double TWO_PI = 6.2831853071795864769252867665590057683943387987502116419498891846156328125724179972560696;
	static const double minusPI = -3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348;
	struct Point
	{
		float a;
		float b;
		int member;
		int p_idx;
		Point(float x, float y) : a(x), b(y), member(-1), p_idx(0) {}
		Point() : a(0), b(0), member(-1), p_idx(0) {}
		inline bool operator==(Point p)
		{
			if (p.a == a && p.b == b)
				return true;
			else
				return false;
		}
	};

	struct MinDistResult
	{
		Point p;
		double dist;
	};

	double mod(double x, double y)
	{
		double m = x - y * floor(x / y);
		// handle boundary cases resulted from floating-point cut off:
		if (y > 0) // modulo range: [0..y)
		{
			if (m >= y) // Mod(-1e-16             , 360.    ): m= 360.
				return 0;

			if (m < 0)
			{
				if (y + m == y)
					return 0; // just in case...
				else
					return y + m; // Mod(106.81415022205296 , _TWO_PI ): m= -1.421e-14
			}
		}
		else // modulo range: (y..0]
		{
			if (m <= y) // Mod(1e-16              , -360.   ): m= -360.
				return 0;

			if (m > 0)
			{
				if (y + m == y)
					return 0; // just in case...
				else
					return y + m; // Mod(-106.81415022205296, -_TWO_PI): m= 1.421e-14
			}
		}

		return m;
	}

	double distance(double pose_x, double pose_y, double obx, double oby)
	{
		double diffx = obx - pose_x;
		double diffy = oby - pose_y;
		double dist = sqrt(diffx * diffx + diffy * diffy);
		return dist;
	}

	double score_trajectory(const std::vector<double> &x, std::vector<double> &grad, void *f_data);
	/**
	 * @class MpepcPlannerROS
	 * @brief ROS Wrapper for the AckermannPlanner that adheres to the
	 * BaseLocalPlanner interface and can be used as a plugin for move_base.
	 */
	class MpepcPlannerROS : public nav_core::BaseLocalPlanner
	{
	public:
		/**
		 * @brief  Constructor for MpepcPlannerROS
		 */
		MpepcPlannerROS();

		/**
		 * @brief  Constructs the planner
		 * @param name The name to give this instance of the trajectory planner
		 * @param tf A pointer to a transform listener
		 * @param costmap The cost map to use for assigning costs to trajectories
		 */
		//   virtual void initialize(std::string name, tf::TransformListener* tf,
		//       costmap_2d::Costmap2DROS* costmap_ros);
		virtual void initialize(std::string name, tf2_ros::Buffer *tf,
								costmap_2d::Costmap2DROS *costmap_ros);
		/**
		 * @brief  Destructor for the planner
		 */
		virtual ~MpepcPlannerROS();

		/**
		 * @brief  Given the current position, orientation, and velocity of the robot,
		 * compute velocity commands to send to the base
		 * @param cmd_vel Will be filled with the velocity command to be passed to the robot base
		 * @return True if a valid trajectory was found, false otherwise
		 */
		virtual bool computeVelocityCommands(geometry_msgs::Twist &cmd_vel);

		/**
		 * @brief  Set the plan that the controller is following
		 * @param orig_global_plan The plan to pass to the controller
		 * @return True if the plan was updated successfully, false otherwise
		 */
		virtual bool setPlan(const std::vector<geometry_msgs::PoseStamped> &orig_global_plan);

		/**
		 * @brief  Check if the goal pose has been achieved
		 * @return True if achieved, false otherwise
		 */
		virtual bool isGoalReached();

		// Use NLOPT to find the next subgoal for the trajectory generator
		void find_intermediate_goal_params(EgoGoal *next_step);
		// This function is used by the optimizer to score different trajectories
		double sim_trajectory(double r, double delta, double theta, double vMax);

	private:
		bool isInitialized()
		{
			return initialized_;
		}
		bool initialized_;
		bool goal_reached_;
		//   tf::TransformListener* tf_;
		tf2_ros::Buffer *tf_;
		tf2_ros::TransformListener *tf2_;
		std::vector<geometry_msgs::PoseStamped> global_plan_;
		base_local_planner::OdometryHelperRos odom_helper_;

		// for visualization, publishers of global and local plan
		ros::Publisher l_plan_pub_;

		costmap_2d::Costmap2DROS *costmap_ros_;

		ros::Subscriber navfn_cost_sub_;

		std::vector<int8_t> global_potarr_;
		unsigned int global_width_, global_height_;
		double origin_x_, origin_y_, resolution_;

		// Properties for mpepc optimization
		boost::mutex pose_mutex_, cost_map_mutex_;
		nav_msgs::GridCells cost_map;
		ControlLawSettings settings_;
		ControlLaw *cl;

		dynamic_reconfigure::Server<MPEPCPlannerConfig> *dsrv_;

		// Trajectory Model Params
		double K_1;		 // 1.2           // 2
		double K_2;		 // 3             // 8
		double BETA;	 // 0.4          // 0.5
		double LAMBDA;	 // 2          // 3
		double R_THRESH; // 0.05
		double V_MAX;	 // 0.5         // 0.3
		double V_MIN;	 // 0.0
		double W_TURN;	 // 0.2

		// Trajectory Optimization Params
		double TIME_HORIZON;	// 5.0
		double DELTA_SIM_TIME;	// 0.2
		double SAFETY_ZONE;		// 0.225
		double WAYPOINT_THRESH; // 1.75

		// Cost function params
		double C1;							   // = 0.05;
		double C2;							   // = 1.00;		 //2.5
		double C3;							   // = 0.05;       // 0.15
		double C4;							   // = 0.05;       // 0.2 //turn
		double SIGMA;						   // 0.10
		static constexpr double PHI_COL = 1.0; // 0.4

		double GOAL_DIST_UPDATE_THRESH;
		double GOAL_ANGLE_UPDATE_THRESH;
		double GOAL_DIST_ID_THRESH;
		double GOAL_ANGLE_ID_THRESH;

		flann::Index<flann::L2<float>> *obs_tree;
		flann::Matrix<float> *data;
		int trajectory_count;

		double map_resolution;
		double interp_rotation_factor;

		static char *cost_translation_table_;

		geometry_msgs::Pose sim_current_pose_;
		bool isLocalOptimize_;
		double maxTrajCost;
		visualization_msgs::MarkerArray trajectArray;
		visualization_msgs::Marker nnArray;
		int last_local_traj_size_;
		ros::Publisher pub_traj_sample_, pub_nn;

		boost::mutex planner_mutex_, inter_goal_mutex_;
		boost::condition_variable planner_cond_;
		EgoPolar inter_goal_coords_;
		double inter_goal_vMax_, inter_goal_k1_, inter_goal_k2_;
		bool isPlanThreadStart_;
		bool run_planner_;
		boost::thread *planner_thread_;
		geometry_msgs::PoseStamped global_goal_pose_stamped_;
		geometry_msgs::Pose local_goal_pose_;

		/**
		 * @brief Callback to update the local planner's parameters based on dynamic reconfigure
		 */
		void reconfigureCB(MPEPCPlannerConfig &config, uint32_t level);

		geometry_msgs::Pose getCurrentRobotPose();

		// Function for mpepc optimization
		void nav_cost_cb(const nav_msgs::OccupancyGrid::ConstPtr &nav_cost);
		void planThread();
		bool same_global_goal(geometry_msgs::PoseStamped new_goal);
		geometry_msgs::Point transformOdomToMap(geometry_msgs::Pose local_pose);
		double getGlobalPointPotential(geometry_msgs::Pose local_pose);

		void updateObstacleTree(costmap_2d::Costmap2D *costmap);
		vector<MinDistResult> find_points_within_threshold(Point newPoint, double threshold);
		MinDistResult find_nearest_neighbor(Point queryPoint);
		double min_distance_to_obstacle(geometry_msgs::Pose local_current_pose, double *heading);

		geometry_msgs::PoseArray get_trajectory_viz(EgoGoal new_coords);
	};
};
#endif
