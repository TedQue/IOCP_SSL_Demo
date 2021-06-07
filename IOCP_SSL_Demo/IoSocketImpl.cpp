#include <assert.h>
#include "IoSocketImpl.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#define MIN_SOCKADDR_BUF_SIZE (sizeof(sockaddr_in) + 16)

/*
* 工具函数
*/
static bool is_socket_listening(SOCKET s)
{
	BOOL listening = FALSE;
	int len = sizeof(BOOL);
	getsockopt(s, SOL_SOCKET, SO_ACCEPTCONN, (char*)&listening, &len);

	return listening == TRUE;
}

static bool is_socket_connected(SOCKET s)
{
	DWORD sec = 0;
	int len = sizeof(sec);
	return SOCKET_ERROR != getsockopt(s, SOL_SOCKET, SO_CONNECT_TIME, (char*)&sec, &len);
	//sockaddr addr;
	//int len = sizeof(sockaddr);
	//return 0 == getpeername(s, &addr, &len);
}

static int get_socket_type(SOCKET s)
{
	int sockType = SOCK_STREAM;
	int len = sizeof(sockType);
	if(SOCKET_ERROR == getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&sockType, &len))
	{
		if(WSAENOTSOCK == WSAGetLastError())
		{
			sockType = 0;
		}
		else
		{
			assert(0);
		}
	}
	return sockType;
}


IoSocketImpl::IoSocketImpl(SOCKET s, const sockaddr* sockname, const sockaddr* peername)
{
	/*
	* 保存这个已经连接的套接字
	*/
	// 设置初始状态
	_status = IO_STATUS_DISCONNECT;
	if(s == INVALID_SOCKET)
	{
		_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		if( INVALID_SOCKET == _s )
		{
			assert(0);
		}
	}
	else
	{
		_s = s;

		if(is_socket_listening(s))
		{
			_status = IO_STATUS_LISTENING;
		}
		else if(is_socket_connected(s))
		{
			_status = IO_STATUS_CONNECTED;
		}
	}
	assert(_s != INVALID_SOCKET);

	/*
	* 分配接收缓冲区和发送缓冲区
	*/
	memset(&_sendOlp, 0, sizeof(iocp_overlapped_t));
	memset(&_recvOlp, 0, sizeof(iocp_overlapped_t));
	_recvOlp.len = RECV_BUF_LEN;
	_sendOlp.len = SEND_BUF_LEN;
	_sendOlp.buf = new byte[SEND_BUF_LEN];
	_recvOlp.buf = new byte[RECV_BUF_LEN];

	// 保存套接字地址
	if(sockname)
	{
		_sockname = *sockname;
	}
	else
	{
		memset(&_sockname, 0, sizeof(sockaddr));
	}

	if(peername)
	{
		_peername = *peername;
	}
	else
	{
		memset(&_peername, 0, sizeof(sockaddr));
	}

	_newSock = INVALID_SOCKET;
	_acceptBuf = NULL;
	_lpfnAcceptEx = NULL;
	_lpfnGetAcceptExAddr = NULL;

	_mode = IO_MODE_LT;
	_shutdownFlag = -1;
	_lastError = 0;

	_userPtr = NULL;
	_userPtr2 = NULL;
}

IoSocketImpl::~IoSocketImpl()
{
	if(_newSock != INVALID_SOCKET) ::closesocket(_newSock);
	if(_s != INVALID_SOCKET) ::closesocket(_s);
	if(_acceptBuf) delete []_acceptBuf;
	if(_recvOlp.buf) delete []_recvOlp.buf;
	if(_sendOlp.buf) delete []_sendOlp.buf;
}

SOCKET IoSocketImpl::getSocket()
{
	return _s;
}

int IoSocketImpl::setLastError(int err)
{
	if(err == WSA_IO_PENDING || err == WSAEWOULDBLOCK)
	{
		// 这两种平台相关的可以重试的错误码转换为平台无关的可重试错误码
		err = IO_EAGAIN;
	}

	int oldErr = _lastError;
	_lastError = err;
	return oldErr;
}

int IoSocketImpl::getLastError() const
{
	return _lastError;
}

int IoSocketImpl::setStatus(int st)
{
	int oldSt = _status;
	_status = st;
	return oldSt;
}

void* IoSocketImpl::getPtr()
{
	return _userPtr;
}

void* IoSocketImpl::setPtr(void* p)
{
	void* oldP = _userPtr;
	_userPtr = p;
	return oldP;
}

void* IoSocketImpl::getPtr2()
{
	return _userPtr2;
}

void* IoSocketImpl::setPtr2(void* p)
{
	void* oldP = _userPtr2;
	_userPtr2 = p;
	return oldP;
}

int IoSocketImpl::bind(const char* ipAddr, u_short port)
{
	assert(_s != INVALID_SOCKET);
	
	sockaddr_in addr;
	addr.sin_family	= AF_INET;
	addr.sin_port = htons(port);
	if(NULL == ipAddr || strlen(ipAddr) == 0 )
	{
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		addr.sin_addr.s_addr = inet_addr(ipAddr);
	}

	if(SOCKET_ERROR == ::bind(_s, (sockaddr *)&addr, sizeof(sockaddr_in)))
	{
		setLastError(WSAGetLastError());
		return SOCKET_ERROR;
	}
	else
	{
		// 保存本地地址
		int len = sizeof(sockaddr);
		if(0 != ::getsockname(_s, &_sockname, &len))
		{
			int err = WSAGetLastError();
			assert(0);
		}
		return 0;
	}
}

int IoSocketImpl::getsockname(char *ipAddr, u_short *port)
{
	sockaddr_in* a = (sockaddr_in*)&_sockname;
	
	if(ipAddr) strcpy_s(ipAddr, 16, inet_ntoa(a->sin_addr));
	if(port) *port = ntohs(a->sin_port);

	return 0;
}

int IoSocketImpl::getpeername(char* ipAddr, u_short *port)
{
	sockaddr_in* a = (sockaddr_in*)&_peername;
	
	if(ipAddr) strcpy_s(ipAddr, 16, inet_ntoa(a->sin_addr));
	if(port) *port = ntohs(a->sin_port);

	return 0;
}

int IoSocketImpl::getopt(int optlevel, int optname, char* optval, int* optlen)
{
	return ::getsockopt(_s, optlevel, optname, optval, optlen);
}

int IoSocketImpl::setopt(int optlevel, int optname, const char* optval, int optlen)
{
	return ::setsockopt(_s, optlevel, optname, optval, optlen);
}

int IoSocketImpl::listen(int backlog /* = SOMAXCONN */)
{
	assert(_s != INVALID_SOCKET);
	int r = ::listen(_s, backlog);
	if( 0 == r )
	{
		// 设置为侦听状态
		setStatus(IO_STATUS_LISTENING);

		// listen() 已经成功, 所以 r = 0
		// listen 调用成功则自动发起一个 accept 操作,在这个操作完成时, IoSocket 状态变为可读;如果发生错误也是在下一次用户调用 wait 之后返回
		int pr = postAccept();
		if(pr == 0 || pr == WSA_IO_PENDING || pr == WSAECONNRESET)
		{
			// postAccept() 成功,用户通过 wait() 得到结果
		}
		else
		{
			// postAccept() 发生错误,套接字标记为损坏
			setLastError(pr);
			setStatus(IO_STATUS_BROKEN);
		}
	}
	else
	{
		assert(0);
		assert(r == SOCKET_ERROR);
		setLastError(WSAGetLastError());
	}
	return r;
}

bool IoSocketImpl::busy()
{
	return _recvOlp.oppType != IO_OPP_NONE || _sendOlp.oppType != IO_OPP_NONE;
}

int IoSocketImpl::closesocket()
{
	int r = 0;

	if(_s != INVALID_SOCKET)
	{
		// 直接关闭套接字使已经投递的 IOCP 操作返回
		::closesocket(_s);
		_s = INVALID_SOCKET;

		// 设置套接字状态为已关闭
		setStatus(IO_STATUS_CLOSED);
	}

	if(busy())
	{
		setLastError(WSA_IO_PENDING);
		r = SOCKET_ERROR;
	}
	else
	{
	}
	return r;
}

int IoSocketImpl::shutdown(int how)
{
	// 设置一个shutdown标记

	_shutdownFlag = how;
	setStatus(IO_STATUS_SHUTDOWN);

	return 0;
}

int IoSocketImpl::accept(SOCKET* s, sockaddr* sockname, sockaddr* peername)
{
	int ret = 0;

	if(_recvOlp.oppType == IO_OPP_NONE)
	{
		// AccpetEx调用已经完成,把新连接和地址返回
		if(!_lpfnGetAcceptExAddr)
		{
			/*
			* 获得GetAcceptExSockaddrs()函数指针
			*/
			GUID GuidGetAcceptExAddr = WSAID_GETACCEPTEXSOCKADDRS;
			DWORD dwBytes = 0;
			if( 0 != WSAIoctl(_s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExAddr, sizeof(GuidGetAcceptExAddr), &_lpfnGetAcceptExAddr, sizeof(_lpfnGetAcceptExAddr), &dwBytes, NULL, NULL) )
			{
				assert(0);
			}
		}

		*s = _newSock;
		_newSock = INVALID_SOCKET;

		if(_lpfnGetAcceptExAddr)
		{
			sockaddr* localSockName = NULL, *peerSockName = NULL;
			int localSockNameLen = 0, peerSockNameLen = 0;
			_lpfnGetAcceptExAddr(_acceptBuf, 0, MIN_SOCKADDR_BUF_SIZE, MIN_SOCKADDR_BUF_SIZE, &localSockName, &localSockNameLen, &peerSockName, &peerSockNameLen);
			memcpy(sockname, localSockName, localSockNameLen);
			memcpy(peername, peerSockName, peerSockNameLen);
		}

		// 发起下一个IO
		int pr = postAccept();
		if(pr == 0 || pr == WSA_IO_PENDING || pr == WSAECONNRESET)
		{
		}
		else
		{
			// 记录错误,把当前套接字标记为损坏,本次 accept() 返回成功(因为的确成功了),用户再通过 wait() 等待当前套接字(侦听套接字)会发现错误
			setLastError(pr);
			setStatus(IO_STATUS_BROKEN);
		}
	}
	else
	{
		// 有IO操作正在进行,可以重试
		setLastError(WSA_IO_PENDING);
		ret = SOCKET_ERROR;
	}

	return ret;
}


int IoSocketImpl::postAccept()
{
	/* 有IO操作正在进行或者已经完成了一次连接但新连接的套接字没有被取走 */
	if(_recvOlp.oppType != IO_OPP_NONE || _newSock != INVALID_SOCKET)
	{
		assert(0);
		return WSA_IO_PENDING;
	}

	if(!_acceptBuf)
	{
		_acceptBuf = new char[MIN_SOCKADDR_BUF_SIZE * 2];
	}

	if(!_lpfnAcceptEx)
	{
		/*
		* 获得AcceptEx()函数指针
		*/
		GUID GuidAcceptEx = WSAID_ACCEPTEX;
		DWORD dwBytes = 0;
		if( 0 != WSAIoctl(_s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &_lpfnAcceptEx, sizeof(_lpfnAcceptEx), &dwBytes, NULL, NULL) )
		{
			assert(0);
			return WSAGetLastError();
		}
	}

	/*
	* 创建一个新的套接字(accept 调用的只能是TCP流套接字)
	*/
	_newSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	assert(_newSock != INVALID_SOCKET);

	// 发起一个 IOCP AcceptEx 调用
	DWORD dwBytesReceived = 0;
	_recvOlp.oppType = IO_OPP_ACCEPT;
	if(!_lpfnAcceptEx(_s, _newSock, _acceptBuf, 0, MIN_SOCKADDR_BUF_SIZE, MIN_SOCKADDR_BUF_SIZE, &dwBytesReceived, (OVERLAPPED*)&_recvOlp))
	{
		return WSAGetLastError();
	}
	return 0;
}

// 应用调用 connect 函数后,检测 IoSocket 状态变为可写则表示连接建立成功;状态变为异常则表示连接建立失败.
int IoSocketImpl::connect(const char* ip, unsigned short port)
{
	/* 加锁,防止用户重复调用 */
	int ret = 0;
	int pr = postConnect(ip, port);
	if(pr == 0 || pr == WSA_IO_PENDING)
	{
	}
	else
	{
		setLastError(pr);
		ret = SOCKET_ERROR;
	}

	return ret;
}

int IoSocketImpl::postConnect(const char *ipAddr, u_short port)
{
	/*
	* 取得ConnectEx函数指针
	*/
	
	GUID GuidConnectEx = WSAID_CONNECTEX;
	LPFN_CONNECTEX lpfnConnectEx = NULL;
	DWORD dwBytes = 0;
	if( 0 != WSAIoctl(_s, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx, sizeof(GuidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx), &dwBytes, NULL, NULL) )
	{
		assert(0);
		return WSAGetLastError();
	}

	/*
	* 执行ConnectEx
	*/
	_sendOlp.oppType = IO_OPP_CONNECT;

	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.S_un.S_addr = inet_addr(ipAddr);

	DWORD dwBytesSent = 0;
	if(!lpfnConnectEx(_s, (const sockaddr*)&addr, sizeof(sockaddr), NULL, 0, &dwBytesSent, (LPOVERLAPPED)&_sendOlp) )
	{
		return WSAGetLastError();
	}
	return 0;
}

int IoSocketImpl::recv(void* buf, size_t len)
{
	int ret = 0;

	// 复制缓冲区内的数据

	/* 是否已经禁止接收 */
	if(_shutdownFlag == SD_RECEIVE || _shutdownFlag == SD_BOTH)
	{
		// 已经关闭
		ret = SOCKET_ERROR;
		setLastError(WSAESHUTDOWN);
	}
	else
	{
		if(len > _recvOlp.ipos - _recvOlp.upos) len = _recvOlp.ipos - _recvOlp.upos;
		if(len > 0)
		{
			if(buf)
			{
				memcpy(buf, _recvOlp.buf + _recvOlp.upos, len);
			}
			_recvOlp.upos += len;

			/* 数据拷贝成功,返回拷贝长度 */
			ret = len;
		}
		else
		{
			/* 返回失败 */
			ret = SOCKET_ERROR;
		}

		// 数据已经被读取完毕,边缘触发,再投递一个 recv iocp 请求
		if(_recvOlp.upos >= _recvOlp.ipos)
		{
			/* 边缘触发标记 */
			_recvOlp.et = true;

			/* 投递一个 recv 操作 */
			// 尝试发起一个RECV IO(只有当缓冲区内没有数据才会真正投递 recv iocp操作)
			int pr = postRecv();
			if(pr == 0 || pr == WSA_IO_PENDING)
			{
				setLastError(WSA_IO_PENDING);
			}
			else if(pr == WSAESHUTDOWN)
			{
				assert(0);
				setLastError(pr);
			}
			else
			{
				/* 检测到一个本地错误 */
				setLastError(pr);
				setStatus(IO_STATUS_BROKEN);
			}
		}
	}
	return ret;
}

/*
* recv io 提交的缓冲区总是从 [ipos -> end), 缓冲区内有效数据的范围是 [upos -> ipos) 无效数据(已经被user读取)范围是 [start, upos)
*/
int IoSocketImpl::postRecv()
{
	if(_shutdownFlag == SD_RECEIVE || _shutdownFlag == SD_BOTH)
	{
		// 如果已经禁止了接收就不应该调用 postRecv()
		assert(0);
		return WSAESHUTDOWN;
	}

	/* 有IO操作正在进行 */
	if(_recvOlp.oppType != IO_OPP_NONE)
	{
		assert(_recvOlp.oppType == IO_OPP_RECV);
		return WSA_IO_PENDING;
	}

	/*
	* 设置缓冲区,丢弃已经读取的部分
	*/
	memmove(_recvOlp.buf, _recvOlp.buf + _recvOlp.upos, _recvOlp.ipos - _recvOlp.upos);
	_recvOlp.ipos -= _recvOlp.upos;
	_recvOlp.upos = 0;
	
	if(_recvOlp.ipos > 0)
	{
		// 只要缓冲区内还有数据,就不投递下一个 iocp 操作,和 send 的逻辑有点不一样.
	}
	else
	{
		/*
		* 投递一个IO RECV 操作
		*/
		_recvOlp.oppType = IO_OPP_RECV;
		WSABUF wsaBuf = { _recvOlp.len - _recvOlp.ipos, (char*)_recvOlp.buf + _recvOlp.ipos };
		DWORD dwFlags = 0;
		DWORD dwTransfered = 0;
		assert(wsaBuf.len > 0);
		assert(wsaBuf.len == RECV_BUF_LEN); // 逻辑上接收缓冲为空才投递 IOCP 操作,所以必定投递整个缓冲区长度

		if(SOCKET_ERROR == WSARecv(_s, &wsaBuf, 1, &dwTransfered, &dwFlags, (LPOVERLAPPED)&_recvOlp, NULL))
		{
			return WSAGetLastError();
		}
	}
	return 0;
}

int IoSocketImpl::send(const void* buf, size_t len)
{
	int ret = 0;
	if(_shutdownFlag == SD_BOTH || _shutdownFlag == SD_SEND)
	{
		// 套接字已经被关闭
		ret = SOCKET_ERROR;
		setLastError(WSAESHUTDOWN);
	}
	else
	{
		// 写入缓冲区
		if(len > _sendOlp.len - _sendOlp.upos) len = _sendOlp.len - _sendOlp.upos;
		if(len > 0)
		{
			memcpy(_sendOlp.buf + _sendOlp.upos, buf, len);
			_sendOlp.upos += len;
		
			// 返回拷贝长度
			ret = len;

			// 缓冲区已满,边缘触发条件满足
			if(_sendOlp.upos >= _sendOlp.len)
			{
				_sendOlp.et = true;
			}
		
			// 尝试发起一个 send IO
			int pr = postSend();
			if(0 == pr || WSA_IO_PENDING == pr)
			{
			}
			else
			{
				assert(pr != WSAESHUTDOWN);
				// 检测到一个本地错误,下次用户调用 wait() 时返回这个错误
				setLastError(pr);
				setStatus(IO_STATUS_BROKEN);
			}
		}
		else
		{
			/* 缓冲区已满 */
			ret = SOCKET_ERROR;
			setLastError(WSA_IO_PENDING);
		}
	}
	return ret;
}

/*
* send io 缓冲区分布, 提交部分 [start -> ipos), 已写未提交部分 [ipos -> upos), 空余部分 [upos -> end)
*/
int IoSocketImpl::postSend()
{
	/* 如果设置了关闭标志也要继续发送已经拷贝到缓冲区内的数据 */
	/* 有IO操作正在进行 */
	if(_sendOlp.oppType != IO_OPP_NONE)
	{
		assert(IO_OPP_SEND == _sendOlp.oppType);
		return WSA_IO_PENDING;
	}

	/* 设置发送缓冲区,把已经发送成功的数据丢弃 */
	assert(_sendOlp.buf);
	memmove(_sendOlp.buf, _sendOlp.buf + _sendOlp.ipos, _sendOlp.upos - _sendOlp.ipos);
	_sendOlp.upos -= _sendOlp.ipos;
	_sendOlp.ipos = _sendOlp.upos;

	if(_sendOlp.ipos <= 0)
	{
		// 数据发送完毕
		/* 如果设置了shutdown标记,当发送缓冲区全部发送完毕后返回 SHUTDOWN */
		if(_shutdownFlag == SD_BOTH || _shutdownFlag == SD_SEND)
		{
			return WSAESHUTDOWN;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		/*
		* 投递一个IO SEND 操作
		*/	
		_sendOlp.oppType = IO_OPP_SEND;
		DWORD dwTransfered = 0;
		DWORD dwLastError = 0;
		WSABUF wsaBuf = { _sendOlp.ipos, (char*)_sendOlp.buf };
		assert(wsaBuf.len > 0);

		if(SOCKET_ERROR == WSASend(_s, &wsaBuf, 1, &dwTransfered, 0, (LPOVERLAPPED)&_sendOlp, NULL))
		{
			return WSAGetLastError();
		}
		
		return 0;
	}
}

int IoSocketImpl::ctl(u_int ev)
{
	int oldm = _mode;

	// 记录触发模式
	_mode = TEST_BIT(ev, IO_EVENT_ET) ? IO_MODE_ET : IO_MODE_LT;

	// 如果设置了 IO_EVENT_IN 则投递一个读请求(如果已经有读请求在进行中会忽略)
	if (TEST_BIT(ev, IO_EVENT_IN))
	{
		int pr = postRecv();
		if (pr == 0 || pr == WSA_IO_PENDING)
		{
		}
		else
		{
			setLastError(pr);
			setStatus(IO_STATUS_BROKEN);
		}
	}

	return oldm;
}

/*
* 根据套接字的内部状态返回所有会被触发的事件
* 如果套接字发生过**本地**错误则会触发 IO_EVENT_ERROR 事件.所以一个已经出错的套接字放入 selector 中总会被返回.
* **远程**错误则会被忽略,直到执行操作后再次发生远程错误.
*/
u_int IoSocketImpl::detectEvent()
{
	u_int ev = IO_EVENT_NONE;

	if(_s == INVALID_SOCKET || _status < 0)
	{
		// _sgtrace("IoSocketImpl(0x%x)::detectEvent() error code: %d.\r\n", this, _lastError);
		if(_status == IO_STATUS_PEERCLOSED)
		{
			SET_BIT(ev, IO_EVENT_HANGUP);
		}
		else
		{
			SET_BIT(ev, IO_EVENT_ERROR);
		}
	}
	else
	{
		// 判断是否可读 
		// (1) (接收缓冲内有未读数据->认为处于就绪状态. [start ->(已读) -> upos -> (未读) -> ipos (空闲或IOCP操作中) -> end])
		// (2) 侦听状态下,空闲说明已经有一个 socket 就绪
		if(_shutdownFlag == SD_RECEIVE || _shutdownFlag == SD_BOTH)
		{
		}
		else
		{
			// 输入缓冲内有数据,或者侦听套接字没有在投递 IOCP 操作(说明已经完成了一次 ACCEPTEX) -> 可读
			if((_recvOlp.upos < _recvOlp.ipos) || (_recvOlp.oppType == IO_OPP_NONE && _status == IO_STATUS_LISTENING))
			{
				SET_BIT(ev, IO_EVENT_IN);
			}
		}

		// 判断是否可写 (套接字已经连接,并且写缓冲有空闲空间)
		if(_shutdownFlag == SD_SEND || _shutdownFlag == SD_BOTH)
		{
			// 套接字被关闭,并且数据已经发送完毕,则触发 ERR 指示用户可以安全关闭套接字
			if(_sendOlp.ipos == _sendOlp.upos)
			{
				SET_BIT(ev, IO_EVENT_ERROR);
			}
		}
		else
		{
			// 状态正常,并且输出缓冲内还有空闲空间 -> 可写
			if(_status > 0 && (_sendOlp.upos < _sendOlp.len))
			{
				SET_BIT(ev, IO_EVENT_OUT);
			}
		}
	}
	return ev;
}

u_int IoSocketImpl::onAccept(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;
	if(oppResult)
	{
		/* 更新套接字地址(在 WIN7 下无效,有效的地址在 accept() 函数中通过 GetAcceptExSockaddrs() 获得正确的地址 ) */
		if(0 != setsockopt( _newSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&_s, sizeof(_s)) )
		{
			assert(0);
		}

		/* 触发可读事件(进入IOSelector的可读队列) */
		ev = IO_EVENT_IN;
	}
	else
	{
		/*
		* WSAECONNRESET: 远端连接后又被关闭. 触发可读事件,但是调用 accept 会返回一个 INVALID_SOCKET
		* 2016.1.8 - 可能是侦听套接字发生错误,也可能是 AcceptEx 调用失败,如何区别?
		*/
		// 关闭 _newSock,不管什么情况,_newSock 都无效了.
		::closesocket(_newSock);
		_newSock = INVALID_SOCKET;

		/* 调用没加锁的全局版 getsockopt */
		// int sockerr = 0;
		// int sockerrlen = sizeof(int);
		// if(::getsockopt(_s, SOL_SOCKET, SO_ERROR, (char*)&sockerr, &sockerrlen) == 0 && sockerr == 0)

		/* 2016.1.8 - WSAECONNRESET 错误允许侦听套接字继续使用,所以标记为远程错误 */
		int err = WSAGetLastError();
		if(err == WSAECONNRESET) 
		{
			/* 确认不是本地套接字错误导致 IOCP 失败 */
			// assert(WSAGetLastError() == WSAECONNRESET);
			ev = IO_EVENT_IN;
		}
		else
		{
			/* 本地套接字错误 */
			assert(0);
			ev = IO_EVENT_ERROR;
			setLastError(err);
			setStatus(IO_STATUS_BROKEN);
		}
	}
	return ev;
}

u_int IoSocketImpl::onConnect(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;

	/* 连接成功则触发可写事件,否则触发出错事件(调用者应该能识别出此时的出错事件指连接失败) */
	if(oppResult)
	{
		// 设置状态为:已连接
		setStatus(IO_STATUS_CONNECTED);

		// 新连接的套接字处于可写状态,触发可写事件
		ev = IO_EVENT_OUT;

		/* 更新套接字状态(好像没什么用) */
		if(0 != ::setsockopt(_s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0))
		{
			assert(0);
		}

		/* 保存远程地址 */
		int len = sizeof(sockaddr);
		if(0 != ::getpeername(_s, &_peername, &len))
		{
			DWORD err = WSAGetLastError();
			assert(0);
		}
	}
	else
	{
		/* 仅仅触发事件,不设置套接字错误码,因为还可以再次调用 Connect 尝试连接 */
		ev = IO_EVENT_HANGUP;
	}

	return ev;
}

u_int IoSocketImpl::onRecv(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;

	/* 只有接收缓冲区为空时才会投递 IOCP 操作 */
	assert(_recvOlp.ipos == 0 && _recvOlp.upos == 0);

	// 因为不会投递长度为0的 recv 请求,所以 bytesTransfered = 0 也是失败(不应该出现)
	if(oppResult)
	{
		//_sgtrace("IoSocketImpl(0x%x)::update IO_OPP_RECV :%d bytes, unread pos: %d.\r\n", this, bytesTransfered, _recvOlp.ipos);
		if(bytesTransfered > 0)
		{
			_recvOlp.ipos += bytesTransfered;
			if(_mode == IO_MODE_ET)
			{
				if(_recvOlp.et)
				{
					ev = IO_EVENT_IN;
					_recvOlp.et = false;
				}
				else
				{
					ev = IO_EVENT_NONE;
				}
			}
			else
			{
				ev = IO_EVENT_IN;
			}

			/* 只要接受缓冲内还有数据就不继续投递 recv 操作, 由用户调用 recv() 触发*/
		}
		else
		{
			/* 一次无效的投递? 确定投递 recv 时,len > 0 有 assert, 为什么会发生操作成功但是 bytesTransfered = 0 的情况? */
			/* 根据 berkeley socket recv() 返回值, 这种情况下应该是表明对方已经把套接字关闭了. */
			ev = IO_EVENT_HANGUP;
			setStatus(IO_STATUS_PEERCLOSED);
		}
	}
	else
	{
		// IOCP 模型没有特别的办法知道对方是否关闭了连接,只能通过投递 IOCP 请求,然后检查操作结果
		//_sgtrace("IoSocketImpl(0x%x)::update IO_OPP_RECV :%d bytes, WSAError: %d.\r\n", this, bytesTransfered, WSAGetLastError());
					
		/* 投递失败, 触发error事件 */
		ev = IO_EVENT_ERROR;
		setLastError(WSAGetLastError());
		setStatus(IO_STATUS_BROKEN);
	}
	return ev;
}

u_int IoSocketImpl::onSend(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;
	if(oppResult)
	{
		if(bytesTransfered > 0)
		{
			_sendOlp.ipos = bytesTransfered;

			if(_mode == IO_MODE_ET)
			{
				if(_sendOlp.et)
				{
					ev = IO_EVENT_OUT;
					_sendOlp.et = false;
				}
				else
				{
					ev = IO_EVENT_NONE;
				}
			}
			else
			{
				ev = IO_EVENT_OUT;
			}

			/* send 的逻辑是只要发送缓冲区内还有数据,就必须继续发送 */
			int dsr = postSend();
			if(0 == dsr || WSA_IO_PENDING == dsr)
			{
				// 投递成功或者缓冲区为空
			}
			else if(WSAESHUTDOWN == dsr)
			{
				/* 继续发送时如果发生本地错误,触发ERR事件, ERR事件和EPOLLOUT互斥 */
				/* 已经设置了shutdown标记,并且缓冲区内的数据已经全部发完,则触发ERROR事件使用户可以得到通知 */
				ev = IO_EVENT_ERROR;
				setLastError(dsr);
			}
			else
			{
				// 检测到一个本地错误
				setLastError(dsr);
				setStatus(IO_STATUS_BROKEN);
			}
		}
		else
		{
			/* 对方已经关闭了连接 */
			ev = IO_EVENT_HANGUP;
			setStatus(IO_STATUS_PEERCLOSED);
		}
	}
	else
	{
		/* 触发error事件,并且标记套接字为损坏 */
		ev = IO_EVENT_ERROR;
		setLastError(WSAGetLastError());
		setStatus(IO_STATUS_BROKEN);
	}
	return ev;
}

/* 更新内部状态,返回一个触发事件 */
u_int IoSocketImpl::update(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;

	/*
	* 清理已经完成的操作:统计字节数等等.
	*/
	if(oppResult)
	{
		olp->transfered += bytesTransfered;
	}

	/* 清空标记 */
	int oppType = olp->oppType;
	olp->oppType = IO_OPP_NONE;

	if(_s == INVALID_SOCKET)
	{
		/*
		* 套接字已经被关闭,如果所有的 IOCP 操作都已经返回(不忙碌),则提示删除.
		*/
		assert(_status == IO_STATUS_CLOSED);
	}
	else
	{
		/*
		*  分类处理已经完成的操作结果
		*/
		switch(oppType)
		{
		case IO_OPP_ACCEPT:
			{
				ev = onAccept(oppResult, olp, bytesTransfered);
			}
			break;
		case IO_OPP_CONNECT:
			{
				ev = onConnect(oppResult, olp, bytesTransfered);
			}
			break;
		case IO_OPP_RECV:
			{
				ev = onRecv(oppResult, olp, bytesTransfered);
			}
			break;
		case IO_OPP_SEND:
			{
				ev = onSend(oppResult, olp, bytesTransfered);
			}
			break;
		default: break;
		}
	}

	return ev;
}
