#pragma once

/* Copyright (C) 2015 阙荣文
 *
 * GPLv3
 *
 * 联系原作者: querw@sina.com 
*/

/*
* Socket IO 封装,对 Windows 平台的 IOCP 模型和 Linux/Unix 平台的 epoll 提供一个统一的接口.
* 主要是把 IOCP 模型的回调方式改为应用主动获取方式向 epoll 的编程风格靠拢.
*
*/

/*
* 预设应用场景
* 单线程使用 selector, 同时最多只有一个发送请求和一个接收请求
* 
*/

/*
* 位操作工具宏函数
*/
#define TEST_BIT(val, bit) ((val) & (bit))
#define SET_BIT(val, bit) ((val) |= (bit))
#define UNSET_BIT(val, bit) ((val) &= ~(bit))

/* SOCKET 固定分配的内部缓冲区的长度 */
#define RECV_BUF_LEN 2048
#define SEND_BUF_LEN 2048

/*
* IoSelector 封装隐藏了 socket 的平台区别,所以在处理返回值的时候必须定义平台不相关的值
* berkeley 套接字接口返回值的一般原则:
* 0: recv / send 表示对方正常关闭 gracefully closed, 其他表示成功
* >0: recv / send 表示成功传输的字符数
* -1: SOCKET_ERROR 
* 一般说来,用户判断以下3个返回值就足够了. 其他大于0的错误码是平台相关的.
*/

#define IO_SUCESS	0		/* 成功 */
#define IO_FAILED	-1		/* 失败 */
#define IO_EAGAIN	1		/* 稍后重试 */

// 套接字类型
#define IO_TYPE_SOCKET		0
#define IO_TYPE_SOCKET_SSL	1

/* 
* 套接字接口,流接口同 Berkeley 套接字
*/
class IoSocket
{
public:
	virtual int type() const = 0;
	virtual int bind(const char* ipAddr, unsigned short port) = 0;
	virtual int listen(int backlog = 0x7fffffff) = 0;
	virtual int connect(const char* ip, unsigned short port) = 0;
	virtual int getsockname(char *ipAddr, unsigned short *port) = 0;
	virtual int getpeername(char* ipAddr, unsigned short *port) = 0;
	virtual int setopt(int optlevel, int optname, const char* optval, int optlen) = 0;
	virtual int getopt(int optlevel, int optname, char* optval, int* optlen) = 0;
	virtual int recv(void* buf, size_t len) = 0;
	virtual int send(const void* buf, size_t len) = 0;
	virtual int shutdown(int how) = 0;
	virtual int closesocket() = 0;
	virtual int getLastError() const = 0;
	virtual void* getPtr() = 0;
	virtual void* setPtr(void* p) = 0;
};

/*
* ctl 函数的EVENT定义
*/
#define IO_EVENT_NONE			0x00
#define IO_EVENT_IN				0x01 /* recv or accept available */
#define IO_EVENT_OUT			0x02 /* send available or connect done */
#define IO_EVENT_ERROR			0x10 /* local error occur */
#define IO_EVENT_HANGUP			0x20 /* peer error occur, hanged up usually | auto set */
#define IO_EVENT_TIMEOUT		0x40 /* IoSelector doesn't set timeout flag, it's reserved for IoSelector user */

#define IO_EVENT_LT				0x0100
#define IO_EVENT_ET				0x0200
#define IO_EVENT_ONESHOT		0x0400

/*
* wait() 返回值
*/
#define IO_WAIT_SUCESS	0 /* 取得了一个活跃IOAdpater */
#define IO_WAIT_TIMEOUT 1 /* 超时 */
#define IO_WAIT_ERROR	2 /* 出错 */
#define IO_WAIT_WAKEUP	3 /* 唤醒 */

/*
* IoSelector 选项
*/
#define IO_SELECTOR_OPT_SETCRT		0x0002	/* 设置证书, 参数 C字符串表示证书的文件路径 */
#define IO_SELECTOR_OPT_SETPRVKEY	0x0004	/* 设置私钥, 参数 C字符串表示密钥的文件路径 */

class IoSelector
{
public:
	/* 创建或者销毁 IoSocket 对象 */
	virtual IoSocket* socket(int t) = 0;
	virtual IoSocket* accept(IoSocket* sock) = 0;
	virtual int close(IoSocket* adp) = 0;

	/* 选项控制(设置 SSL 参数等) */
	virtual int setopt(int optname, const char* optval, int optlen) = 0;
	virtual int getopt(int optname, char* optval, int* optlen) = 0;

	/* 由于 IoSocket 的创建和销毁由 IoSelector 负责,所以 ctl 函数对于 EPOLL_CTL_ADD 和 EPOLL_CTL_DEL 不再有意义,只执行 EPOLL_CTL_MOD 操作 */
	virtual int ctl(IoSocket* adp, unsigned int ev) = 0;

	/* 每次只返回一个"活跃"的 IoSocket 对象,而不像 epoll_wait 一样返回一个数组 */
	virtual int wait(IoSocket** adp, unsigned int* ev, int timeo = -1) = 0;

	/* 使正在调用的 wait 函数以返回值 IO_WAIT_WAKEUP 返回 */
	virtual	int wakeup() = 0;
};

extern "C"
{
	int IoSelector_Init();
	int IoSelector_Cleanup();

	IoSelector* CreateIoSelector();
	void FreeIoSelector(IoSelector* selector);
}