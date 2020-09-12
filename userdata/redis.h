#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>    // a redis client API
#include <iostream>
#include <string.h>
#include "../lock/lock.h"

using namespace std;
class redis_clt
{
private:
    locker m_redis_lock;
    static redis_clt *m_redis_instance;
    struct timeval timeout;
    redisContext *m_redisContext;
    redisReply *m_redisReply;       
    /*
        存储Redis操作返回结果的结构体
        //This is the reply object returned by redisCommand()
        typedef struct redisReply 
        {
            命令执行结果的返回类型
            int type; REDIS_REPLY
            存储执行结果返回为整数
            long long integer;        
            //字符串值的长度
            size_t len;               
            //存储命令执行结果返回是字符串
            char *str;                  
            //返回结果是数组的大小
            size_t elements;           
            //存储执行结果返回是数组
            struct redisReply **element; 
        } redisReply;
    */

private:
    string getReply(string m_command);
    redis_clt();

public:
    string setUserpasswd(string username, string passwd)
    /*
        设置用户密码
    */
    {
        return getReply("set " + username + " " + passwd);
    }

    string getUserpasswd(string username)
    /*
        获取用户密码
    */
    {
        return getReply("get " + username);
    }
    void vote(string votename)
    {
        //cout << "vote for : " << votename << endl;
        if (votename.length() > 0)
        {
            string temp = getReply("ZINCRBY GOT 1 " + votename);
            //cout << temp << endl;
        }
        else
        {
            //cout << "votename error didnot vote!" << endl;
        }

        //zrange company 0 -1 withscores
    }
    string getvoteboard();
    void board_exist();

    static redis_clt *getinstance();
};

#endif
