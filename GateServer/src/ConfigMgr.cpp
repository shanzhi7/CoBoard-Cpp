#include "GateServer/ConfigMgr.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iostream>
ConfigMgr::ConfigMgr()
{
	boost::filesystem::path current_path = boost::filesystem::current_path();	//获取程序运行路径
	boost::filesystem::path config_path = current_path / "config.ini";			//拼接config.ini路径
	std::cout << "Config path: " << config_path << std::endl;

	//Boost::PropertyTree 是 Boost 库中的一个组件，用于处理层次化的配置数据
	//boost::property_tree::ptree 是一个节点书，节点为key，value也是ptree树结构

	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(config_path.string(), pt);		//读取config.ini

	for (const auto& section_pair : pt)
	{
		const std::string& section_name = section_pair.first;
		const boost::property_tree::ptree& section_tree = section_pair.second;
		std::map<std::string, std::string> section_config;
		for (const auto& key_value_pair : section_tree)
		{
			const std::string& key = key_value_pair.first;
			const std::string& value = key_value_pair.second.get_value<std::string>();
			section_config[key] = value;
		}
		SectionInfo sectionInfo;
		sectionInfo._section_datas = section_config;
		_config_map[section_name] = sectionInfo;
	}

	//输出所有的section跟key-value对
	for (const auto& section_entry : _config_map)
	{
		const std::string& section_name = section_entry.first;
		SectionInfo section_config = section_entry.second;
		std::cout << "[" << section_name << "]" << std::endl;
		for (const auto& key_value_pair : section_config._section_datas)
		{
			std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
		}
		std::cout << std::endl;
	}
	std::cout << std::endl;
}