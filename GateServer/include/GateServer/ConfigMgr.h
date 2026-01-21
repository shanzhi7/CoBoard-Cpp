#pragma once
#include "const.h"
#include <map>
#include <string>

struct SectionInfo {
	SectionInfo() {}
	~SectionInfo() { _section_datas.clear(); }			//析构清除map里的内容

	SectionInfo(const SectionInfo& src)					//拷贝构造
	{
		_section_datas = src._section_datas;
	}

	SectionInfo& operator = (const SectionInfo& src)	//重载=
	{
		if (&src == this)
		{
			return *this;
		}
		this->_section_datas = src._section_datas;
		return *this;
	}

	std::map<std::string, std::string> _section_datas;	//存储key_value(端口)
	std::string operator[](const std::string& key)		//重载[]
	{
		if (_section_datas.find(key) == _section_datas.end())
		{
			return "";
		}
		return _section_datas[key];
	}
};
class ConfigMgr
{
public:
	~ConfigMgr()
	{
		_config_map.clear();									//析构清除map里的内容
	}
	SectionInfo operator[](const std::string& section)			//重载[],value 为 SectionInfo
	{
		if (_config_map.find(section) == _config_map.end())
		{
			return SectionInfo();
		}
		return _config_map[section];
	}

	static ConfigMgr& Inst()
	{
		static ConfigMgr cfg_mgr;
		return cfg_mgr;
	}

	ConfigMgr(const ConfigMgr& src)								//实现拷贝构造
	{
		this->_config_map = src._config_map;
	}

	ConfigMgr& operator = (const ConfigMgr& src)				//重载=
	{
		if (&src == this)
		{
			return *this;
		}
		_config_map = src._config_map;
	}
private:
	ConfigMgr();												//定义构造函数

	std::map<std::string, SectionInfo> _config_map;		//存储SectionInfo结构体
};

