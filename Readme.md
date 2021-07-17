# 还是这个名字 :)
# 目录介绍
## common
里面是客户端和服务端会共同需要的代码，这样就不用同时维护两份代码了。当然，编写的时候也需要考虑两个平台兼容的问题了。

## poll
简单实现的一个网络库，只有几百行代码，也不知道能不能称之为`库`，通过宏定义区别客户端和服务端的部分。因为服务端使用`epoll`模型，但是`windows`没有这个模型。那么客户端目前选择`select`，因为对于客户端来说似乎请求数量并不会很大。再者`epoll`和`select`结构上比较相似，比较容易使用宏区分。

## thread
是一个简单的线程池，不过这次好像用不到线程池。另外`fetch`任务的地方其实有个`bug`，不过不影响其正常工作。

## client
是客户端了，使用`vs`开发，建立工程。将来也许会考虑同时支持`linux`，也就是`client`文件夹下有`sln`通过`vs`直接能够编译，同时也有`Makefile`通过`Linux`直接`make`就能出二进制文件。不过服务端目前没有考虑兼容`windows`，因为我也没有`windows`服务器。不过如果只是通过简单的修改就能兼容`windows`的话，也未尝不可。最后才会去做这个事情。

## server
服务端，直接`make`就能得到二进制文件。未来会介绍一下其工作原理，但是现在还没有开发完成。

# Todo
+ 完成目录调整
+ 跑通客户端
+ bug,,, bug,,, and bug....

# Log
+ 当客户端主动断开的时候，服务端会崩溃
    > 原因是对`Recv`的返回值预计错误，以为recv返回0就表示对端断开。如果对端close()这里返回0，如果直接关闭进程，返回-1
+ 如果给select函数第三个参数设置超时时间会有`10022`错误
+ 客户端Packet类中取包长多了2
    > `packet.h`里面传入的`data_len`的结果就是包含`sid`的包长度，所以不需要加上`HEADER_SIZE`
