<<一种 Windows IOCP 整合 OpenSSL 实现方案>>

by 阙荣文 Que's C++ Studio
2021-06-06

1. IOCP 和 epoll 编程模型比较

本来程序员们都已经习惯了传统的 select 模型(包括 linux epoll 和 BSD kqueue),非常好理解. 但 Windows IOCP API接口另搞了一套,显得相当异类.
我个人还是更喜欢传统的 select 模型,而且我认为所谓的重叠操作没什么意义,为什么需要同时投递多个同类请求呢?比如发送一个大文件时,我们不可能放任程序一直投递写请求而使内存用量失控.
基于上述原因,也为了跨平台使用方便,最终把 IOCP 封装为 epoll 风格的接口,程序结构大致如下,是最常见的事件循环:

	for (;;)
	{
		// 等待网络事件
		selector->wait(&socket, &ev);
		
		// 分类处理活跃的网络事件
		if (TEST_BIT(ev, IO_EVENT_SEND))
		{
		}
		else if (TEST_BIT(ev, IO_EVENT_RECV))
		{
		}
		else
		{
		}
	}
	详情请参考 main() 函数.

2. 理解 OpenSSL API 的抽象结构

作为 OpenSSL 库用户,我们可以把它理解为一个黑盒,加密时写入明文读出密文,解密时写入密文读出明文.这个黑盒的操作接口被称为 "BIO",也就是说 SSL 库通过 BIO 接口与外界交互信息,
而 socket 只是众多 BIO 实现之一.利用 BIO 接口,可以把 SSL 协议应用到任意类型的数据通道中,这就是抽象的威力.
通信连接建立之后,通信双方还需要进行 SSL 握手,握手过程中也会产生数据交互,在同步套接字中,调用 SSL_accept() 或者 SSL_connect() 会阻塞直到握手完成;
在异步套接字中则需要反复调用 SSL_do_handshake() 直到返回 1 为止.
由于 SSL 协议中通信双方并非对等,而分为服务器端和客户端两个不同角色,所以在调用 SSL_do_handsake() 之前,
我们需要调用 SSL_set_accept_state() 把 SSL 设置为服务器端角色或者 SSL_set_connect_state()设置为客户端角色.
对于 SSL 握手协议而言 SSL_accept() = SSL_set_accept_state() + SSL_do_handsake(); SSL_connect() = SSL_set_connect_state() + SSL_do_handsake().

3. IOCP 与 OpenSSL 整合思路

通过对上述 BIO 接口的介绍可知, BIO 接口是整合 IOCP 与 OpenSSL 最符合设计原则的方案.
OpenSSL 库中提供了众多 BIO 实现,其中内存 bio_mem 似乎很符合我们的需要,思路是通过 SSL_write() 把密文写入 bio_mem, 然后 IOCP 工作线程从该 bio_mem 中读取密文并发送出去;
接收时, IOCP 工作线程把密文写入 bio_mem,再调用 SSL_read() 读取该 bio_mem 得到明文.这个方案毫无疑问是可行的.但是也有不足之处,数据先写入 bio_mem 再写入 IOCP 缓冲区,需要
两倍缓冲区占用和内存复制,此其一; 在通常使用异步套接字的习惯中,我们总是一直调用 SSL_write() 直到返回 -1 为止,bio_mem 会自动扩展内存用量,所以只要内存足够,我们可以一直调用 
SSL_write() 把所有数据全部写入内存,为了避免出现这种情况,只好修改调用 SSL_write() 惯用法,此其二.
另一个方案是实现自己的 BIO,使 SSL_read/write() 直接读写 IOCP 缓冲区,使 BIO 完全按照我们的期望运行.参考 bio_mem 的实现源码 crypto/bio/bss_mem.c 并不难做到, demo 中采用
的就是这个方案.在编写此 demo 的过程中还有个小插曲,这个 demo 的主要代码写于 2016 年,近日安装了最新版 OpenSSL 后发现编译不通过了,原来是新版库已经不再暴露 BIO_METHOD 结构,需要调用
一组 BIO_meth_xxx() 接口访问该结构.这当然是正确的,作为用户最好只使用接口而不要过多介入内部细节.

实现可供 SSL_read/write() 使用的 BIO 需要以下3个关键函数,分别对应 BIO_read(), BIO_write() 和 BIO_flush() 在此3个函数中我们让 SSL 库直接读写 IOCP 缓冲区.
详情请参考源码 IoSocketSSLImpl.cpp 之 bio_iocp_read(), bio_iocp_write() 和 bio_iocp_ctrl().

[后记]
早就想写点关于 IOCP 和 OpenSSL 整合的文章,但 IOCP 模型一般用在大项目中不是很好剥离,一拖数年直到现在才完成,惭愧.
这个 demo 是从本人的另一个项目中抽取出来的,为了减少干扰简化了诸如超时控制,多线程安全等相关代码,也没有经过仔细测试,主要演示上述整合方案的可行性,望见谅.

================================================================
附1: 本项目访问 https://www.baidu.com/ 的效果(证书信息输出在下方的调试器窗口中)
screenshot.png

附2: 我在本机 Windows 10 上编译安装 OpenSSL 说明(如果您无法编译 demo 请对比此安装流程)
1. 安装 Visual Studio 2019 社区版
2. 下载 OpenSSL 源码 https://github.com/openssl/openssl ,按照 NOTES-WINDOWS.md 文件说明按部就班操作.
3. 安装 Perl http://strawberryperl.com/ 并添加安装路径到 path 环境变量,运行命令 "perl --version" 可正确打印版本信息.
4. 安装 nasm https://www.nasm.us 并添加安装路径到 path 环境变量,运行命令 "nasm --version" 可正确打印版本信息.
5. 编译 32 位 OpenSSL 库
	以管理员权限从开始菜单启动 "Developer Command Prompt for VS 2019" 之后 cd 至 OpenSSL 源码目录
	运行命令 "perl Configure VS-WIN32" 配置 OpenSSL 生成选项为 VS-WIN32
	运行命令 "nmake"
	运行命令 "nmake test"
	运行命令 "nmake install" (默认安装路径在 C:\Program Files (x86)\OpenSSL)
6. 把上述 OpenSSL 安装路径添加至 path 环境变量中,运行命令 "openssl version" 可正确打印版本信息.
7. 在 VS 项目中引用 OpenSSL
	添加 C:\Program Files (x86)\OpenSSL\include 到 Visual Studio 项目的包含目录
	添加 C:\Program Files (x86)\OpenSSL\lib 到 Visual Studio 项目的库目录
	引入库 libcrypto.lib, libssl.lib 依赖