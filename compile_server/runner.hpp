#pragma once

#include<iostream>
#include<string>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/time.h>
#include<sys/resource.h>

#include"../comm/log.hpp"
#include"../comm/util.hpp"

namespace ns_runner
{
    using namespace ns_util;
    using namespace ns_log;

    class Runner
    {
    public:
    Runner(){}
    ~Runner(){}
    public:
    //提供设置进程占用资源大小的接口
        static void SetProcLimit(int _cpu_limit,int _mem_limit)
        {
            //设置cpu时长
            struct rlimit cpu_limit;
            cpu_limit.rlim_max = RLIM_INFINITY;
            cpu_limit.rlim_cur = _cpu_limit;

            setrlimit(RLIMIT_CPU,&cpu_limit);

            //设置内存大小
            struct rlimit mem_limit;
            mem_limit.rlim_max = RLIM_INFINITY;
            mem_limit.rlim_cur = _mem_limit * 1024;//kb大小
            setrlimit(RLIMIT_AS,&mem_limit);

        }
    //指明文件名即可，不需要代理路径，不要带后缀


        /************************************************************
        返回值 > 0,程序异常，退出时受到信号，返回值就是对应的信息编号
        返回值 == 0,正常运行完毕，结果保存到了对应的临时文件
        返回值 < 0,内部错误
        ************************************************************/

        //cpu_limit:该程序运行的时候，可以使用的最大cpu资源
        //mem_limit:该程序运行的时候，可以使用的内存大小（kb）
        static int Run(const std::string &file_name,int cpu_limit,int mem_limt)
        {
            std::string _execute = PathUtil::Exe(file_name);
            std::string _stdin   = PathUtil::Stdin(file_name);
            std::string _stdout  = PathUtil::Stdout(file_name);
            std::string _stderr  = PathUtil::Stderr(file_name);

            umask(0);
            int _stdin_fd  = open(_stdin.c_str(),O_CREAT | O_RDONLY,0644);
            int _stdout_fd = open(_stdout.c_str(),O_CREAT | O_WRONLY,0644);
            int _stderr_fd = open(_stderr.c_str(),O_CREAT | O_WRONLY,0644);

            if(_stdin_fd < 0 || _stdout_fd < 0 || _stderr_fd < 0)
            {
                LOG(ERROR) <<"运行时打开标准文件失败" << "\n";
                return -1;//打开文件失败
            }

            pid_t pid= fork();
            if(pid < 0)
            {
                LOG(ERROR) << "运行时创建子进程失败" << "\n";
                close(_stdin_fd);
                close(_stdout_fd);
                close(_stderr_fd);
                return -2;//创建子进程失败

            }else if(pid == 0)
            {
                dup2(_stdin_fd,0);
                dup2(_stdout_fd,1);
                dup2(_stderr_fd,2);

                SetProcLimit(cpu_limit,mem_limt);

                execl(_execute.c_str()/*我要执行什么*/,_execute.c_str()/*我要在命令行执行什么*/,nullptr);
                exit(1);
            }else
            {
                close(_stdin_fd);
                close(_stdout_fd);
                close(_stderr_fd);
                int status = 0;
                waitpid(pid,&status,0);
                //程序运行异常，收到了信号
                LOG(INFO) << "运行完毕，info：" << (status & 0x7F) << "\n" ;
                return status & 0x7F;
            }
        }
    };
}