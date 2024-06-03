#include <stdio.h>
#include <stdlib.h>
#include <string.h>  // 修正为 string.h 而不是 string
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include<libgen.h>
#include <signal.h>
#include"http_conn.h"
#include "locker.h"
#include "threadpool.h"

#define MAX_FD 65535//最大的文件描述符个数
#define MAX_EVENT_NUMBER 1000//监听的最大的事件数量
// 添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));  // 将 '0' 修正为 0
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//添加文件描述符到epoll中
//从epoll中删除文件描述符
extern void addfd(int epollfd,int fd,bool one_shot);
extern void removefd(int eppllfd,int fd);
//修改文件描述符
extern void modfd(int epollfd,int fd,int ev);
int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("按照如下格式运行: %s port_number\n",basename(argv[0]));
        exit(-1);
    }
    
    // 检查端口号是否合法
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("无效的端口号: %s\n", argv[1]);
        exit(-1);
    }
   addsig(SIGPIPE,SIG_IGN);
   //创建线程池，初始化线程池
   threadpool<http_conn>*pool=NULL;
   try{
    pool=new threadpool<http_conn>;
   }catch(...){
    exit(-1);
   }
   //创建一个数组用于保存所有的客户端信息
   http_conn * users=new http_conn[MAX_FD];

   int listenfd=socket(PF_INET,SOCK_STREAM,0);

   //设置端口复用
   int reuse=1;
   setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
   //绑定
   struct sockaddr_in address;
   //设置地址族为 AF_INET，表示使用IPv4地址。
   address.sin_family=AF_INET;
   //设置IP地址为 INADDR_ANY，表示服务器将绑定到所有可用的网络接口。这意味着服务器可以接受任何到达本地机器任意网络接口的连接。
   address.sin_addr.s_addr=INADDR_ANY;
   //htons 函数将主机字节顺序转换为网络字节顺序，因为网络传输采用大端字节序，
   //而主机可能使用小端字节序。port 是之前从命令行参数获取并转换为整数的端口号。
   address.sin_port=htons(port);
   bind(listenfd,(struct sockaddr*)&address,sizeof(address));
   //监听
   listen(listenfd,5);
   //创建epoll对象，事件数组，添加
   epoll_event events[MAX_EVENT_NUMBER];
   int epollfd=epoll_create(5);
   //将监听的文件描述符添加到epoll对象中
   addfd(epollfd,listenfd,false);
   http_conn::m_epollfd=epollfd;
   while(true){
   int num = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
    if((num<0)&&(errno!=EINTR)){
       printf("epoll failure\n");
       break;
    }  
    //循环遍历事件数组数组
    for(int i=0;i<num;i++){
        int sockfd =events[i].data.fd;
        if(sockfd==listenfd){
            //有客户端连接进来
            struct sockaddr_in client_address;
            socklen_t client_addrlen=sizeof(client_address);
            int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);
            if(http_conn::m_user_count>=MAX_FD){
                //目前连接数满了
                //给客户端写一个信息
                close(connfd);
                continue;
            }
            //要将新的客户的信息初始化，放到数组中
            users[connfd].init(connfd,client_address);
            
        }else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
            //对方异常断开或者错误事件
            users[sockfd].close_conn();

        }else if(events[i].events&EPOLLIN){
            if(users[sockfd].read()){
                //一次性把所有数据都读完
                pool->append(users+sockfd);
            }else{
                users[sockfd].close_conn();
            }
        }else if(events[i].events&EPOLLOUT){
            if(!users[sockfd].write()){
                users[sockfd].close_conn();
            }
        }
    }
   }
   close(epollfd);
   close(listenfd);
   delete[]users;
   delete pool;
    return 0;
}
