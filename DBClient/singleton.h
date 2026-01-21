/***********************************************************************************
*
* @file         singleton.h
* @brief        单例模板类
*
* @author       shanzhi
* @date         2026/01/20
* @history
***********************************************************************************/
#ifndef SINGLETON_H
#define SINGLETON_H

#include "global.h"
#include <iostream>
#include <mutex>
template<typename T>
class Singleton{
protected:
    Singleton() = default;
    Singleton(const Singleton<T>&) = delete;
    Singleton& operator=(const Singleton<T>& st) = delete;

    static std::shared_ptr<T> instance;

public:
    static std::shared_ptr<T>& getInstance()
    {
        static std::once_flag s_flag;
        std::call_once(s_flag,[&](){
            instance = std::shared_ptr<T>(new T);
        });

        return instance;
    }
    void printAddress()
    {
        std::cout<<instance.get()<<std::endl;
    }
    ~Singleton()
    {
        std::cout<<"this is singleton destroyed"<<std::endl;
    }
};
template<typename T>
std::shared_ptr<T> Singleton<T>::instance = nullptr;
#endif // SINGLETON_H
