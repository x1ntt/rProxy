#ifndef __RPOLL_H
#define __RPOLL_H

#ifdef __linux
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#define FD_SETSIZE 1024
#include <winsock2.h>
#include <WS2tcpip.h>
#pragma comment(lib,"ws2_32.lib")
#endif

#include "../common/types.h"
#include "../common/log.h"

#include <string.h>

#include <iostream>
#include <map>
#include <vector>
#include <mutex>
#include <algorithm>

namespace GNET {
    using std::string;
    using std::cout;
    using std::endl;
    using std::map;


    enum {
        MAX_CONNECT = 1024,
        LISTEN_COUNT = 50
    };
    class Poll;
    class BaseNet {
    protected:
        string _host;
        int _port;
        int _sock_fd;
        unsigned int _flag;
        struct sockaddr_in _addr;
        char* _buffer; // 接收缓冲区    // 记得初始化
        int _packet_size;  // 实际包大小   记得初始化
        int _packet_pos;    // 已经接收的实际大小
    public:
        enum {
            NET_ERROR = 0x0001,     // 连接失败
            NEED_DELETE = 0x0002    // 有这个标志的对象会在poll函数中被删除
        };

        // 基础包结构，只有数据大小头
        typedef struct {
            unsigned short int data_len;
            char data[];
        }BasePacket;

        int& get_sock() { return _sock_fd; };
        void set_sock(int sock) { _sock_fd = sock; };
        string& get_host() { return _host; };
        void set_host(string host) { _host = host; };
        int& get_port() { return _port; };
        void set_port(int port) { _port = port; };
        struct sockaddr_in& get_sockaddr_in() { return _addr; };
        void set_sockaddr_in(struct sockaddr_in& addr) { memcpy(&_addr, &addr, sizeof(addr)); };

        BaseNet() : _flag(0), _buffer(NULL), _packet_size(0), _packet_pos(0), _sock_fd(0), _port(0) {
            memset(&_addr, 0, sizeof(struct sockaddr_in));
        };
        BaseNet(string host, int port) :_host(host), _port(port), _flag(0),
            _buffer(NULL),
            _packet_size(0),
            _packet_pos(0),
            _sock_fd(0) {
            memset(&_addr, 0, sizeof(struct sockaddr_in));
        };
        virtual ~BaseNet() {
            /*if (_buffer){
                free(_buffer);
            }*/
        };

        virtual void OnRecv() {
            LOG_D("BaseNet的OnRecv()");
        };

        // 不在析构函数中关闭套接字是因为有时delete对象保留连接的需要 所以把对象和连接分离比较好
        void OnClose() {
            // Poll::deregister_poll(this); 
            // 还是把BaseNet和Poll分离开吧 
            // 也就select需要取消注册 epoll在套接字被close的时候就自己取消监听了
            if (_buffer) {
                free(_buffer);
                _buffer = NULL;
                // 当套接字被关闭的时候，缓冲区肯定没用了
                // 但是因为可能copy，所以不能在析构函数中free 
                // 否则会导致delete副本之后所有的对象_buffer失效
            }
#ifdef __linux
            close(_sock_fd);
#else
            closesocket(_sock_fd);
#endif
        };

        // 会接收一个连接，并且返回 BaseNet 对象指针
        BaseNet* Accept() {
            BaseNet* bn = new BaseNet();
#ifdef __linux
            socklen_t lt = sizeof(_addr);
#else
            int lt = sizeof(_addr);
#endif
            int sock;
            struct sockaddr_in& addr = bn->get_sockaddr_in();
            if ((sock = accept(_sock_fd, (struct sockaddr*)&(addr), &lt)) == -1) {
                delete bn;
                return NULL;
            }
            bn->set_sock(sock);
            bn->set_port(ntohs(addr.sin_port));
            char ip[128];
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            bn->set_host(ip);
            return bn;
        }

        int Recv(char* data, size_t len) {
            int ret = 0;
#ifdef __linux
            do {
                ret = recv(_sock_fd, data, len, 0);
            } while (ret == -1 && errno == EINTR);    // windows 大概是没有errno
#else
            ret = recv(_sock_fd, data, len, 0);
#endif
            return ret;
        }

        // 保证接受完 len 长度的数据才返回
        int RecvN(char* data, size_t len) {
            int ret = 0;
            int real_recv = 0;
            do {
                ret = Recv(data + real_recv, len - real_recv);
                if (ret <= 0) { return ret; }
                real_recv += ret;
            } while (real_recv != len);
            return real_recv;
        };

        int Send(const char* data, size_t len) {
            int ret = 0;
            ret = send(_sock_fd, data, len, 0);
            //if (ret != len && ret > 0) {
            //    printf("[!!!!!!!!] Send:  ret/len: %d/%d", ret, (int)len);
            //}
            return ret;
        }

        // 不发送完len长度的数据绝不返回 除非接收错误
        int SendN(char* data, size_t len) {
            int ret = 0;
            int real_send = 0; // 总共发送的数据
            do {
                ret = Send(data + real_send, len - real_send);
                if (ret <= 0) { return ret; }
                real_send += ret;
            } while (real_send != len);
            return real_send;
        }

        // 发送包，返回发送出去的数据，不包含长度数据的长度 N == true 的时候保证数据一定会发完再返回
        // 这里不太方便使用接收的那种处理方式，因为发送一般只调用一次
        int SendPacket(char* data, size_t len, bool N = false) {
            if (!data || !len) { return 0; }
            BasePacket* tmp = (BasePacket*)malloc(len + sizeof(us16));
            if (!tmp) { return 0; }
            tmp->data_len = (us16)len;
            memcpy(tmp->data, data, len);
            int ret = 0;
            if (N) {
                ret = SendN((char*)tmp, len + sizeof(us16));
            }
            else {
                ret = Send((char*)tmp, len + sizeof(us16));
            }
            free(tmp);
            return ret;
        };

        // 返回 -1 是还没有收到完整包，应该忽略，返回0表示出错需要处理后事
        // 包真的特别大 超过了缓冲区 ... 使用的时候别这么做...
        int RecvPacket(char* data, size_t expected_len) {
            if (!data) { return 0; }
            if (_packet_size == 0) { // 需要接收头部 不用RecvN 需要标记头部是否接收完了 <<2>>
                us16 l = 0;
                _packet_pos = 0;
                memset(data, 0, expected_len);
                int ret = RecvN((char*)&l, sizeof(us16));    // 先获取包的长度 recv返回值是1的情况确实发生了你敢信
                if (ret <= 0) { return 0; }
                // assert(ret == sizeof(us16));
                _packet_size = l;   // 得到包头
                if (_packet_size > expected_len) {
                    LOG_W("异常的包头大小 %d, 偏移: %d", l, _packet_pos);
                    // assert(false && "异常包头"); // 调试阶段需要保证逻辑正确，但是逻辑没问题之后就应该注释，否则任意的不合法连接都将会使得服务端被关闭
                    _packet_size = 0;
                    return 0;   // 包头异常表示这个连接已经没有维护的必要了 应该结束
                }
                assert(_packet_size != 0);
                // printf("[Debug]: 接收到的头 %d\n", l);
                return -1;
            }
            else {
                int ret = Recv(data + _packet_pos, _packet_size - _packet_pos);
                if (ret <= 0) { return 0; }
                assert((ret <= (_packet_size - _packet_pos)) && "接收到的长度不对");
                if (ret != (_packet_size - _packet_pos)) {   // 还没有接收完
                    _packet_pos += ret;
                    return -1;
                }
                else {
                    int pp = _packet_size;
                    _packet_size = 0;
                    _packet_pos = 0;
                    return pp;
                }
            }
        };

        unsigned int get_flag() { return _flag; };
        void SetError() { _flag |= NET_ERROR; }
        bool IsError() { return _flag & NET_ERROR; };
        void SetDelete() { _flag |= NEED_DELETE; }
        bool IsDelete() { return _flag & NEED_DELETE; };

    };

    class Passive : public BaseNet {
    public:
        Passive(string host, int port) :BaseNet(host, port) {
            _sock_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (_sock_fd < 0) {
                LOG_E("创建服务套接字失败");
                SetError();
                return;
            }
            memset(&_addr, 0, sizeof(_addr));
            if (inet_pton(AF_INET, _host.c_str(), &(_addr.sin_addr)) <= 0) {
                LOG_E("主机地址有错误");
                SetError();
                return;
            }
            _addr.sin_family = AF_INET;
            _addr.sin_port = htons(_port);
            if (bind(_sock_fd, (sockaddr*)&_addr, sizeof(_addr)) < 0) {
                LOG_E("绑定错误");
                SetError();
                return;
            }
            listen(_sock_fd, LISTEN_COUNT); // 需要错误处理
            LOG_D("开始监听, %s:%d", _host.c_str(), _port);
        };
        virtual void OnRecv() {
            LOG_D("Passivs的虚方法");
        };
    };

    class Active : public BaseNet {
    public:
        Active(string host, int port) : BaseNet(host, port) {
            _sock_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (_sock_fd < 0) {
                LOG_E("创建连接套接字失败");
                SetError();
                return;
            }
            memset(&_addr, 0, sizeof(_addr));
            _addr.sin_family = AF_INET;
            _addr.sin_port = htons(_port);
            if (inet_pton(AF_INET, _host.c_str(), &_addr.sin_addr) <= 0) {
                LOG_E("主机地址有错误 ");
                SetError();
                return;
            }
            if (connect(_sock_fd, (struct sockaddr*)&_addr, sizeof(_addr)) < 0) {
                LOG_E("连接错误");
                SetError();
                return;
            }
            // printf("[Info]: 连接成功: %s:%d\n", _host.c_str(), _port);
        }
    };

    class Poll {
    public:
        enum {
            TIMEOUT = 500  // 超时时间 毫秒
        };
    private:
#ifdef __linux
        static struct epoll_event _ev, _events[MAX_CONNECT];
        static int _eph;
        static std::vector<BaseNet*> _deleted_vector;     // 保存某次poll循环被删除的对象
#else
        static timeval _select_timeout;
        static fd_set _read_fds;
        static map<int, BaseNet*> _read_fds_map;

#endif
        static std::mutex _poll_mtx;
        static bool _running;
    public:
        Poll() {};

#ifdef __linux
        static void init() {
            _eph = epoll_create(MAX_CONNECT);
            if (_eph < 0) { perror("[Error]: 创建epoll错误"); }
        }
#else
        static void init() {
            _select_timeout.tv_sec = 0;
            _select_timeout.tv_usec = TIMEOUT;
            FD_ZERO(&_read_fds);
        }
#endif

        static void register_poll(BaseNet* bn, int sid = 0) {
            std::lock_guard<std::mutex> l(_poll_mtx);
            LOG_D("开始注册poll _sock_fd: %d, sid: %d", bn->get_sock(), sid);

#ifdef __linux
            // _ev.events = EPOLLIN | EPOLLET;
            _ev.events = EPOLLIN;
            _ev.data.ptr = bn;
            epoll_ctl(_eph, EPOLL_CTL_ADD, bn->get_sock(), &_ev);
#else
            _read_fds_map[bn->get_sock()] = bn; // 虽然注册进去了 但是可能并没有生效 这个需要select的超时
#endif
        }
        static void deregister_poll(BaseNet* bn, int sid = 0) {
            std::lock_guard<std::mutex> l(_poll_mtx);
            LOG_D("取消注册poll: %d sid: %d", bn->get_sock(), sid);
#ifdef __linux
            _deleted_vector.push_back(bn);
            epoll_ctl(_eph, EPOLL_CTL_DEL, bn->get_sock(), NULL);
#else
            // FD_CLR(bn->get_sock(), &_read_fds);     // 忘记取消注册的话，会导致select检查已经被关闭的套接字 导致出现 10038 错误
            map<int, BaseNet*>::iterator iter = _read_fds_map.find(bn->get_sock());
            if (iter != _read_fds_map.end()) {
                _read_fds_map.erase(iter);
            }
#endif
        }

#ifdef __linux
        ;
#else
        static void update_fds() {
            FD_ZERO(&_read_fds);
            for (auto iter : _read_fds_map) {
                FD_SET(iter.first, &_read_fds);
            }
        }
#endif

        static void loop_poll() {
            LOG_D("进入poll循环");
            _running = true;
            for (; _running;) {
#ifdef __linux
                int n = epoll_wait(_eph, _events, MAX_CONNECT, TIMEOUT);
                BaseNet* bn;
                for (int i = 0; i < n; i++) {
                    bn = (BaseNet*)_events[i].data.ptr;  // 由多态选择具体的操作逻辑

                    std::vector<BaseNet*>::iterator it = find(_deleted_vector.begin(), _deleted_vector.end(), bn);  // 看看是不是在本次epoll_wait已经被删除了
                    if (it == _deleted_vector.end()) {
                        bn->OnRecv();
                    }
                    else {
                        printf("[Debug]: 发现已删除对象\n");
                    }
                }
                _deleted_vector.clear();   // 仅保存一个epoll_wait的删除对象
#else
                // printf("[Debug]: t1=%d, t2=%d\n", _select_timeout.tv_sec, _select_timeout.tv_usec);
                // 更新所有套接字进去
                update_fds();
                int n = select(0, &_read_fds, NULL, NULL, /*&_select_timeout*/NULL);
                if (n == SOCKET_ERROR) {
                    LOG_E("Select 错误 WSAGetLastError: %d", WSAGetLastError());
                    stop_poll();
                    WSACleanup();
                    continue;
                };
                if (n == 0) { continue; };

                for (int i = 0; i < _read_fds.fd_count; i++) {
                    map<int, BaseNet*>::iterator iter = _read_fds_map.find(_read_fds.fd_array[i]);
                    if (iter != _read_fds_map.end()) {
                        iter->second->OnRecv();
                    }
                }
#endif
            }
            // 应该删除所有注册的对象(epoll也需要记录下来所有的注册对象否则会有内存泄漏) <<4>>

        }

        static void stop_poll() {
            _running = false;
        }

    };
};

#endif
