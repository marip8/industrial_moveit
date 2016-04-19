/*
 * stomp_optimization_task.cpp
 *
 *  Created on: Mar 23, 2016
 *      Author: Jorge Nicho
 */

#include <stdexcept>
#include "stomp_moveit/stomp_optimization_task.h"

using PluginArray = std::vector< std::pair<std::string,XmlRpc::XmlRpcValue> >;

bool parsePlugins(XmlRpc::XmlRpcValue config,
                      std::string param_name,
                      PluginArray& plugins)
{
  if(config.hasMember(param_name) && (config[param_name].getType() == XmlRpc::XmlRpcValue::TypeArray))
  {
    XmlRpc::XmlRpcValue& plugin_list = config[param_name];
    std::string class_name;

    // look for the 'class' entry
    for(auto i = 0u ; i < plugin_list.size(); i++)
    {
      XmlRpc::XmlRpcValue& plugin_config = plugin_list[i];
      if(plugin_config.hasMember("class") && (plugin_config["class"].getType() == XmlRpc::XmlRpcValue::TypeString))
      {
        class_name = static_cast<std::string>(plugin_config["class"]);
        plugins.push_back(std::make_pair(class_name,plugin_config));
      }
      else
      {
        ROS_ERROR("Element in the '%s' array parameter did not contain a 'class' entry",param_name.c_str());
        return false;
      }
    }
  }
  else
  {
    ROS_WARN("Plugin under entry '%s' was not found in ros parameter.",param_name.c_str());
    ROS_DEBUG("Failed to find plugin under entry '%s' in ros parameter %s",param_name.c_str(),config.toXml().c_str());
    return false;
  }

  return !plugins.empty();
}

namespace stomp_moveit
{

StompOptimizationTask::StompOptimizationTask(
    moveit::core::RobotModelConstPtr robot_model_ptr,
    std::string group_name,
    const XmlRpc::XmlRpcValue& config):
        robot_model_ptr_(robot_model_ptr),
        group_name_(group_name)
{
  // initializing plugin loaders
  cost_function_loader_.reset(new CostFunctionLoader("stomp_moveit", "stomp_moveit::cost_functions::StompCostFunction"));
  filter_loader_.reset(new FilterLoader("stomp_moveit", "stomp_moveit::filters::StompFilter"));
  smoother_loader_.reset(new SmootherLoader("stomp_moveit", "stomp_moveit::smoothers::SmootherInterface"));

  // loading cost function plugins
  if(!initializeCostFunctionPlugins(config))
  {
    ROS_ERROR("StompOptimizationTask failed to load 'cost_functions' plugins from yaml");
    throw std::logic_error("plugin not found");
  }

  // loading noisy filter plugins
  if(!initializeFilterPlugins(config,"noisy_filters",noisy_filters_))
  {
    ROS_WARN("StompOptimizationTask failed to load 'noisy_filters' plugins from yaml");
  }

  // loading filter plugins
  if(!initializeFilterPlugins(config,"optimized_filters",filters_))
  {
    ROS_WARN("StompOptimizationTask failed to load 'optimized_filters' plugins from yaml");
  }

  // loading smoother plugins
  if(!initializeSmootherPlugins(config))
  {
    ROS_WARN("StompOptimizationTask failed to load 'update_smoothers' plugins from yaml");
  }
}

StompOptimizationTask::~StompOptimizationTask()
{
  // TODO Auto-generated destructor stub
}

bool StompOptimizationTask::initializeCostFunctionPlugins(const XmlRpc::XmlRpcValue& config)
{
  PluginArray plugins;
  if(parsePlugins(config,"cost_functions",plugins))
  {
    for(auto& entry: plugins)
    {
      // instantiating
      cost_functions::StompCostFunctionPtr plugin;
      try
      {
        plugin = cost_function_loader_->createInstance(entry.first);
      }
      catch(pluginlib::PluginlibException& ex)
      {
        ROS_ERROR("%s plugin could not be created",entry.first.c_str());
        return false;
      }

      // initializing
      if(plugin->initialize(robot_model_ptr_,group_name_,entry.second))
      {
        cost_functions_.push_back(plugin);
        ROS_INFO_STREAM("Stomp Optimization Task loaded "<<plugin->getName()<<" CostFunction plugin");
      }
      else
      {
        ROS_ERROR("%s plugin failed to initialize",entry.first.c_str());
        return false;
      }
    }

    std::stringstream ss;
    ss<<"[";
    auto arrayToString = [&ss](PluginArray::value_type& p)
    {
      ss<<p.first<<" ";
    };

    std::for_each(plugins.begin(),plugins.end(),arrayToString);
    ss<<"]";

    ROS_DEBUG("Loaded cost function plugins: %s",ss.str().c_str());
  }
  else
  {
    return false;
  }

  return true;
}

bool StompOptimizationTask::initializeFilterPlugins(const XmlRpc::XmlRpcValue& config,std::string param_name,
                                                    std::vector<filters::StompFilterPtr>& filters)
{
  PluginArray plugins;
  bool success = false;
  if(parsePlugins(config,param_name,plugins))
  {
    for(auto& entry: plugins)
    {
      // instantiating
      filters::StompFilterPtr plugin;
      try
      {
        plugin = filter_loader_->createInstance(entry.first);
      }
      catch(pluginlib::PluginlibException& ex)
      {
        ROS_WARN("%s plugin could not be created",entry.first.c_str());
        continue;
      }

      // initializing
      if(plugin->initialize(robot_model_ptr_,group_name_,entry.second))
      {
        filters.push_back(plugin);
        ROS_INFO_STREAM("Stomp Optimization Task loaded "<<plugin->getName()<<" Filter plugin");
        success = true;
      }
      else
      {
        ROS_WARN("%s plugin failed to initialize",entry.first.c_str());
        continue;
      }
    }
  }
  else
  {
    return false;
  }

  return success;
}

bool StompOptimizationTask::initializeSmootherPlugins(const XmlRpc::XmlRpcValue& config)
{
  PluginArray plugins;
  bool success = false;
  if(parsePlugins(config,"update_smoothers",plugins))
  {
    for(auto& entry: plugins)
    {
      // instantiating
      smoothers::SmootherInterfacePtr plugin;
      try
      {
        plugin = smoother_loader_->createInstance(entry.first);
      }
      catch(pluginlib::PluginlibException& ex)
      {
        ROS_WARN("%s plugin could not be created",entry.first.c_str());
        continue;
      }

      // initializing
      if(plugin->initialize(robot_model_ptr_,group_name_,entry.second))
      {
        smoothers_.push_back(plugin);
        ROS_INFO_STREAM("Stomp Optimization Task loaded "<<plugin->getName()<<" Smoother plugin");
        success = true;
      }
      else
      {
        ROS_WARN("%s plugin failed to initialize",entry.first.c_str());
        continue;
      }
    }
  }
  else
  {
    return false;
  }

  return success;
}

bool StompOptimizationTask::computeCosts(const Eigen::MatrixXd& parameters,
                                         std::size_t start_timestep,
                                         std::size_t num_timesteps,
                                         int iteration_number,
                                         int rollout_number,
                                         Eigen::VectorXd& costs,
                                         bool& validity) const
{
  Eigen::MatrixXd cost_matrix = Eigen::MatrixXd::Zero(num_timesteps,cost_functions_.size());
  Eigen::VectorXd state_costs = Eigen::VectorXd::Zero(num_timesteps);
  validity = true;
  for(auto i = 0u; i < cost_functions_.size(); i++ )
  {
    bool valid;
    auto cf = cost_functions_[i];

    if(!cf->computeCosts(parameters,start_timestep,num_timesteps,iteration_number,rollout_number,state_costs,valid))
    {
      return false;
    }

    validity &= valid;

    cost_matrix.col(i) = state_costs * cf->getWeight();
  }
  costs = cost_matrix.rowwise().sum();
  return true;
}

bool StompOptimizationTask::setMotionPlanRequest(const planning_scene::PlanningSceneConstPtr& planning_scene,
                                        const moveit_msgs::MotionPlanRequest &req,
                                        const stomp_core::StompConfiguration &config,
                                        moveit_msgs::MoveItErrorCodes& error_code)
{
  for(auto p : cost_functions_)
  {
    if(!p->setMotionPlanRequest(planning_scene,req,config,error_code))
    {
      ROS_ERROR("Failed to set Plan Request on cost function %s",p->getName().c_str());
      return false;
    }
  }

  std::vector<filters::StompFilterPtr> all_filters;
  all_filters.insert(all_filters.end(),noisy_filters_.begin(),noisy_filters_.end());
  all_filters.insert(all_filters.end(),filters_.begin(),filters_.end());
  for(auto p: all_filters)
  {
    if(!p->setMotionPlanRequest(planning_scene,req,config,error_code))
    {
      ROS_ERROR("Failed to set Plan Request on filter %s",p->getName().c_str());
      return false;
    }
  }

  for(auto p: smoothers_)
  {
    if(!p->setMotionPlanRequest(planning_scene,req,config,error_code))
    {
      ROS_ERROR("Failed to set Plan Request on smoother %s",p->getName().c_str());
      return false;
    }
  }

  return true;
}

bool StompOptimizationTask::smoothParameterUpdates(std::size_t start_timestep,
                                    std::size_t num_timesteps,
                                    int iteration_number,
                                    Eigen::MatrixXd& updates) const
{
  for(auto& s : smoothers_)
  {
    if(!s->smooth(start_timestep,num_timesteps,iteration_number,updates))
    {
      return false;
    }
  }

  return true;
}

bool StompOptimizationTask::filterNoisyParameters(std::size_t start_timestep,
                                                  std::size_t num_timesteps,
                                                  int iteration_number,
                                                  int rollout_number,
                                                  Eigen::MatrixXd& parameters,bool& filtered) const
{
  filtered = false;
  bool temp;
  for(auto& f: noisy_filters_)
  {
    if(f->filter(start_timestep,num_timesteps,iteration_number,rollout_number,parameters,temp))
    {
      filtered |= temp;
    }
    else
    {
      return false;
    }
  }
  return true;
}

bool StompOptimizationTask::filterParameters(std::size_t start_timestep,
                                             std::size_t num_timesteps,
                                             int iteration_number,
                                             Eigen::MatrixXd& parameters,bool& filtered) const
{
  filtered = false;
  bool temp;
  for(auto& f: filters_)
  {
    if(f->filter(start_timestep,num_timesteps,iteration_number,-1,parameters,temp))
    {
      filtered |= temp;
    }
    else
    {
      return false;
    }
  }
  return true;
}

void StompOptimizationTask::done(bool success,int total_iterations,double final_cost)
{
  for(auto p : cost_functions_)
  {
    p->done(success,total_iterations,final_cost);
  }

  std::vector<filters::StompFilterPtr> all_filters;
  all_filters.insert(all_filters.end(),noisy_filters_.begin(),noisy_filters_.end());
  all_filters.insert(all_filters.end(),filters_.begin(),filters_.end());
  for(auto p: all_filters)
  {
    p->done(success,total_iterations,final_cost);
  }

  for(auto p: smoothers_)
  {
    p->done(success,total_iterations,final_cost);
  }
}

} /* namespace stomp_moveit */