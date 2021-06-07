#include <process.h>
#include <assert.h>
#include "IoSelectorImpl.h"

//////////////////////////////////////////////////////////////////////////
IoSelectorImpl::IoSelectorImpl()
	:_iocpHandle(NULL), _sslCtx(NULL)
{
	/*
	* 创建一个完成端口工作对象
	*/
	if( NULL == (_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0)) )
	{
		assert(0);
	}

	/*
	* 需要的时候再 创建 SSL CTX
	*/
}

IoSelectorImpl::~IoSelectorImpl()
{
	// 关闭 iocp 句柄
	if(NULL != _iocpHandle)
	{
		CloseHandle(_iocpHandle);
		_iocpHandle = NULL;
	}

	// 清空事件队列
	_actAdpList.clear();

	// 释放所有的套接字对象
	for(auto itr = _adpList.begin(); itr != _adpList.end(); ++itr)
	{
		freeSocket(*itr);
	}
	_adpList.clear();

	// 释放 SSL 相关数据结构
	if(_sslCtx)
	{
		SSL_CTX_free(_sslCtx);
	}
}

int IoSelectorImpl::setopt(int optname, const char* optval, int optlen)
{
	// 设置 SSL 参数(证书,私钥等)
	int ret = 0;
	switch (optname)
	{
		case IO_SELECTOR_OPT_SETCRT:
		{
			ret = SSL_CTX_use_certificate_file(getSSLCtx(), optval, SSL_FILETYPE_PEM);
			break;
		}
		case IO_SELECTOR_OPT_SETPRVKEY:
		{
			ret = SSL_CTX_use_PrivateKey_file(getSSLCtx(), optval, SSL_FILETYPE_PEM);
			break;
		}
		default:
		{
			break;
		}
	}
	return ret;
}

int IoSelectorImpl::getopt(int optname, char* optval, int* optlen)
{
	return 0;
}

IoSocket* IoSelectorImpl::socket(int t)
{
	return socket(NULL, t);
}

IoSocket* IoSelectorImpl::accept(IoSocket* sock)
{
	return socket(sock, 0);
}

SSL_CTX* IoSelectorImpl::getSSLCtx()
{
	if(!_sslCtx)
	{
		/* 以SSL V2 和V3 标准兼容方式产生一个SSL_CTX */
		_sslCtx = SSL_CTX_new(SSLv23_method());
		assert(_sslCtx);
	}
	return _sslCtx;
}

IoSocket* IoSelectorImpl::socket(IoSocket* acceptBy, int t)
{
	IoSocketImpl *newAdp = NULL;
	SSL* ssl = NULL;
	if(acceptBy == NULL)
	{
		if(t == IO_TYPE_SOCKET_SSL)
		{
			// 创建一个 SSL 套接字
			newAdp = new IoSocketSSLImpl(getSSLCtx(), INVALID_SOCKET);
		}
		else
		{
			// 创建一个普通套接字
			newAdp = new IoSocketImpl(INVALID_SOCKET);
		}
	}
	else
	{
		sockaddr sockname, peername;
		SOCKET s = INVALID_SOCKET;
		if(((IoSocketImpl*)acceptBy)->accept(&s, &sockname, &peername) == 0)
		{
			if(acceptBy->type() == IO_TYPE_SOCKET)
			{
				newAdp = new IoSocketImpl(s, &sockname, &peername);
			}
			else if(acceptBy->type() == IO_TYPE_SOCKET_SSL)
			{
				newAdp = new IoSocketSSLImpl(getSSLCtx(), s, &sockname, &peername);
			}
			else
			{
				assert(0);
			}
		}
		else
		{
			assert(0);
		}
	}
	assert(newAdp);

	/* 把新建的套接字关联到IOCP句柄 */
	if(newAdp && _iocpHandle == CreateIoCompletionPort((HANDLE)newAdp->getSocket(), _iocpHandle, (ULONG_PTR)newAdp, 0))
	{
		/* 初始化关联数据; 严格按照设计原则的话应该有一个 map 把 newAdp 和 一个 struct 关联起来,不过直接放入 newAdp 对象简单点 */
		iosock_info_t* sockInfo = new iosock_info_t;
		sockInfo->isInQueue = false;
		sockInfo->eventMask = IO_EVENT_NONE;
		sockInfo->curEvent = IO_EVENT_NONE;

		newAdp->setPtr2(sockInfo);

		/* 保存这个指针 */
		_adpList.push_back(newAdp);

		if(acceptBy)
		{
			/* 对于通过 accept 获得的socket,默认调用一次 recv ,此时 newAdp 内部的接收缓冲区必定是空的,所以会引发一个 recv IOCP 操作 */
			int r = newAdp->recv(NULL, 0);
			assert(r == IO_FAILED && newAdp->getLastError() == IO_EAGAIN);
		}
	}
	else
	{
		assert(0);
		freeSocket(newAdp);
		newAdp = NULL;
	}

	return newAdp;
}

void IoSelectorImpl::freeSocket(IoSocketImpl* adpImpl)
{
	// 释放关联数据
	iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
	assert(sockInfo);

	//assert(!sockInfo->isInQueue);
	delete sockInfo;
	
	// 释放套接字
	delete adpImpl;
}

int IoSelectorImpl::close(IoSocket* adp)
{
	/*
	* 要确保 IoSocketImpl 没有正在进行的 IOCP 操作才能删除指针
	*/
	IoSocketImpl* adpImpl = (IoSocketImpl*)adp;
	iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
	if (0 == adpImpl->closesocket())
	{
		// 如果已经在活跃队列,则从队列中删除
		if (sockInfo->isInQueue)
		{
			sockInfo->isInQueue = false;
			_actAdpList.remove(adpImpl);
		}

		// 从adp队列中删除
		_adpList.remove(adpImpl);

		// 释放
		freeSocket(adpImpl);
		return 1;
	}
	return 0;
}

int IoSelectorImpl::ctl(IoSocket* adp, u_int ev)
{
	IoSocketImpl *adpImpl = (IoSocketImpl*)adp;
	ev |= (IO_EVENT_ERROR | IO_EVENT_HANGUP); // 自动添加本地和远程两种异常状态.

	// 先从活跃队列中移除
	iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
	if (sockInfo->isInQueue)
	{
		_actAdpList.remove(adpImpl);
		sockInfo->isInQueue = false;
	}

	// adp 也需要处理
	adpImpl->ctl(ev);

	// 清空当前事件,设置新的事件屏蔽位
	sockInfo->curEvent = IO_EVENT_NONE;
	sockInfo->eventMask = ev;

	// 检测 adp 的状态,判断是否需要生成初始事件,如果是则进入活跃队列,由下一次 wait() 调用取出
	sockInfo->curEvent = (adpImpl->detectEvent() & sockInfo->eventMask);
	if (sockInfo->curEvent && !sockInfo->isInQueue)
	{
		sockInfo->isInQueue = true;
		_actAdpList.push_back(adpImpl);
	}
	return 0;
}

/* 使正在调用的 wait 函数以返回值 IO_WAIT_WAKEUP 返回 */
int IoSelectorImpl::wakeup()
{
	return PostQueuedCompletionStatus(_iocpHandle, 0, NULL, NULL);
}

int IoSelectorImpl::wait(IoSocket** adpOut, unsigned int* evOut, int timeo /* = INFINITE */)
{
	// 先检查活跃队列中是否已经有就绪的 adp,如果有直接返回
	if (!_actAdpList.empty())
	{
		IoSocketImpl* adpImpl = _actAdpList.front();
		iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();

		*adpOut = adpImpl;
		*evOut = sockInfo->curEvent;

		_actAdpList.pop_front();
		return IO_WAIT_SUCESS;
	}

	// IoSelector 中 <0 定义为无限等待,与 Windows INFINITE 定义不一致
	DWORD dwTimeout = INFINITE;
	if (timeo >= 0)
	{
		dwTimeout = timeo;
	}

	// 等待 iocp 操作结果
	int ret = IO_WAIT_SUCESS;
	for (;;)
	{
		// TODO: 每次循环开始时应该减去上一次等待的时间
		// ..

		DWORD transfered = 0;
		IoSocketImpl* adpImpl = NULL;
		IOCPOVERLAPPED* iocpOlpPtr = NULL;
		u_int ev = IO_EVENT_NONE;
		if (!GetQueuedCompletionStatus(_iocpHandle, &transfered, reinterpret_cast<PULONG_PTR>(&adpImpl), (LPOVERLAPPED*)&iocpOlpPtr, dwTimeout))
		{
			if (iocpOlpPtr)
			{
				/*
				* IO操作被标记为失败
				*/
				ev = adpImpl->update(false, iocpOlpPtr, transfered);
			}
			else
			{
				/*
				* IOCP本身发生了一些错误,可能是超时 GetLastError returns WAIT_TIMEOUT 或者其他系统错误
				*/
				if (GetLastError() == WAIT_TIMEOUT)
				{
					ret = IO_WAIT_TIMEOUT;
				}
				else
				{
					ret = IO_WAIT_ERROR;
				}
				break;
			}
		}
		else
		{

			if (transfered == 0 && iocpOlpPtr == NULL && adpImpl == NULL)
			{
				/*
				* 约定的唤醒标志
				*/
				ret = IO_WAIT_WAKEUP;
				break;
			}
			else
			{
				/*
				* 根据MSDN的说明GetQueuedCompletionStatus()返回TRUE[只]表示从IOCP的队列中取得一个成功完成IO操作的包.
				* 这里"成功"的语义只是指操作这个动作本身成功完成,至于完成的结果是不是程序认为的"成功",不一定.
				*
				* 1. AcceptEx 和 ConnectEx 成功的话,如果不要求一起发送/接收数据(地址的内容除外),那么 transfered == 0成立.
				* 2. Send, Recv 请求是如果传入的缓冲区长度大于0,而transfered == 0应该判断为失败.
				*
				* 实际测试发现接受客户端连接,执行一个Recv操作,然后客户端马上断开,就能运行到这里,并且 Recv transfered == 0成立.
				* 总而言之,上层应该判断如果传入的数据(不包括AcceptEx和ConnectEx接收的远程地址,而专门指数据部分)缓冲区长度大于0,
				* 而返回的结果表示 transfered = 0 说明操作失败.
				*
				* 2016.3.15 伯克利套接字接口 recv() = 0 表示对方安全关闭套接字, IOCP 应该也是这样的,这样就解释了 transfered = 0 的情况
				*
				* 网络模块本身无法根据 transfered是否等于0来判断操作是否成功,因为上层完全可能投递一个缓冲区长度为0的Recv请求.
				* 这在服务器开发中是常用的技巧,用来节约内存.
				*
				* 2014.12.30 如果本地主动把SOCKET关闭此时该 SOCKET 正有 IOCP 操作在进行,则  GetQueuedCompletionStatus 返回 FALSE.
				* 如果远程把 SOCKET 关闭,而此时本地对应的 SOCKET 有 IOCP 操作在进行,则会导致这个 IO 完成 GetQueuedCompletionStatus 返回
				* TRUE. transfered 则是已经完成的字节数,可能是0也可能不是.
				*/
				ev = adpImpl->update(true, iocpOlpPtr, transfered);
			}
		}

		/*
		* 根据事件屏蔽字判断是否需要返回,有可能自动继续
		*/
		iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
		sockInfo->curEvent = (ev & sockInfo->eventMask);
		if (sockInfo->curEvent)
		{
			*adpOut = adpImpl;
			*evOut = sockInfo->curEvent;
			break;
		}
	}
	return ret;
}
