#include "main.h"

#include <types.h>

using namespace std;

// 1. 如果连接特别多，sid可能会冲突 
// 2. 取消注册 poll √
// 3. 考虑服务端直接断开会发生什么 让客户端考虑去吧

void RequestHandle::OnRecv(){
    char buff[Packet::DATA_SIZE];
    int len = Recv(buff, Packet::DATA_SIZE);
    printf("[Debug]: <-- Request %d\n", len);
    if (len <= 0){
        _client_bn->del_session(_sid);
        _client_bn->send_cmd(CMD::MAKE_cmd_dis_connect(_sid), sizeof(CMD::cmd_dis_connect));
        return ;
    }
    Packet* pk = new Packet(_sid, len, buff);
    char* send_data = pk->get_p();
    // pk->dump();
    if (send_data){
        int ret = _client_bn->SendPacket(send_data, pk->get_packet_len(), true);
        printf("[Debug]: --> Client %d\n", ret);
    }else{
        printf("[Warning]: 组装数据包失败 buff=%p, _sid=%d, len=%d\n", buff, _sid, len);
    }
    delete pk;
}

void ClientListener::OnRecv(){
    BaseNet* bn = Accept();
    if (bn){
        int sid = get_sid();
        RequestHandle* rh = new RequestHandle(*bn, _client_bn, sid);
        _client_bn->add_session(sid, rh);
        GNET::Poll::register_poll(rh);
        delete bn;  // 删除了结构 但是并没有关闭套接字
    }else{
        printf("[Error]: 与请求端建立连接失败\n");
    }
}

void ClientHandle::OnRecv(){
    char buff[Packet::PACKET_SIZE + 2];
    int len = RecvPacket(buff, Packet::PACKET_SIZE);
    printf("[Debug]: <-- Client %d\n", len);
    if (len == 0){
        OnClose();
        return ;
    }
    if (len == -1){ // 不是个完整的数据包
        return ;
    }
    Packet* pk = new Packet(buff, len);
    if (pk->get_sid() == 0){    // 来自客户端的控制指令
        switch(((CMD::cmd_dis_connect*)pk->get_p())->_type){
        case CMD::CMD_END_SESSION:
            del_session(((CMD::cmd_dis_connect*)pk->get_p())->_sid);
            break;
        default:
            break;
        }
    }else{
        GNET::BaseNet* bn = fetch_bn(pk->get_sid());
        if(bn){
            int ret = bn->SendN(pk->get_data(), pk->get_data_len());
            printf("[Debug]: --> Request %d sid=%d\n", ret, pk->get_sid());
        }else{
            printf("[Warning]: 没有找到 sid=%d 的会话, len=%d\n", pk->get_sid(), len);
        }
    }
    delete pk;
}

void ClientHandle::OnClose(){
    GNET::BaseNet::OnClose();   // 关闭与客户端的连接
    _cl->OnClose();             // 关闭端口监听
    del_session(-1);            // 断开所有的请求端
    if(_cl){delete _cl;}        // 释放监听对象
    delete this;                // 删除自己
    ServerListener::inc_client_count(); // 减少客户端计数
}

void ClientHandle::send_cmd(char* data, int len){
    if(SendPacket(data, len) <= 0){
        OnClose();  // 与客户端断开了 处理后事
    }
}

void ServerListener::OnRecv(){
	printf("[Info]: 有客户端连接到达\n");
    GNET::BaseNet* bn = Accept();
    if(!bn){
        printf("[Error]: 与客户端建立连接失败\n");
        return ;
    }
    ClientHandle *ch = new ClientHandle(*bn);
    delete bn;

    GNET::Poll::register_poll(ch); // 注册客户端

    printf("[Info]: %s:%d, sock_fd: %d\n", ch->get_host().c_str(), ch->get_port(), ch->get_sock());
    if (_client_count >= CLIENT_COUNT){  // 超过最大数量了
        printf("[Warning]: 客户端连接到上限了 %d\n", _client_count);
        // bn->Send(); // 发送拒绝原因
        ch->OnClose();
        delete ch;
        return ;
    }

    if (_client_port > MAX_PORT){   // 超过最大端口限制
        printf("[Warning]: 端口号超过最大限制 port: %d, MAX_PORT: %d\n",_client_port-1, MAX_PORT);
        _client_port = START_PORT;
    }
    
    int try_count = 0;
    do{
        if(try_count > MAX_TRY_NUM){
            printf("[Warning]: 尝试多次都失败了\n");
            // bn->Send(); // 尝试很多次都失败了
            ch->OnClose();
            delete ch;
            return ;
        }
        try_count ++;
        
        ClientListener* cl = new ClientListener(_host, _client_port, ch);
        if (cl->IsError()){
            cl->OnClose();
            delete cl;
            _client_port ++;
        }else{  // 监听成功
            GNET::Poll::register_poll(cl);
            ch->set_client_listener(cl);
            break;
        }

        if (_client_port > MAX_PORT){
            printf("[Warning]: 端口号超过最大限制 port: %d, MAX_PORT: %d\n",_client_port-1, MAX_PORT);
            _client_count = START_PORT;
        }

    }while(1);

    _client_port++;     // 出了循环表示监听成功
    dec_client_count(); // 客户端计数+1
};

int ServerListener::_client_count = 0;
int ServerListener::CLIENT_COUNT = 10;

void usege(){
    printf("使用方法: \n\tmain 端口 [客户端数量]\n\t默认是 main 7200 10\n");
}

int main(int argv, char* args[]){
    int port = 7200;
    int max_client = 10;
    if (argv == 2){
        port = atoi(args[1]);
    }else if(argv == 3){
        port = atoi(args[1]);
        max_client = atoi(args[2]);
    }else{
        usege();
    }
	
    if((new ServerListener("0.0.0.0", port, max_client))->IsError()){
        return -1;
    }
    
    GNET::Poll::loop_poll();
	/*THREAD::ThreadPool::start_pool();
	
	for (int i=0;i<100;i++){
		while(1){
			if(THREAD::ThreadPool::add_task(new print(i)) == 0){
				break;
			}
		}
		// usleep(100000);
	}*/

	getchar();
}
