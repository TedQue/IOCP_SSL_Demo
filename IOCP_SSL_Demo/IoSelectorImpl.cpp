#include <process.h>
#include <assert.h>
#include "IoSelectorImpl.h"

//////////////////////////////////////////////////////////////////////////
IoSelectorImpl::IoSelectorImpl()
	:_iocpHandle(NULL), _sslCtx(NULL)
{
	/*
	* ����һ����ɶ˿ڹ�������
	*/
	if( NULL == (_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0)) )
	{
		assert(0);
	}

	/*
	* ��Ҫ��ʱ���� ���� SSL CTX
	*/
}

IoSelectorImpl::~IoSelectorImpl()
{
	// �ر� iocp ���
	if(NULL != _iocpHandle)
	{
		CloseHandle(_iocpHandle);
		_iocpHandle = NULL;
	}

	// ����¼�����
	_actAdpList.clear();

	// �ͷ����е��׽��ֶ���
	for(auto itr = _adpList.begin(); itr != _adpList.end(); ++itr)
	{
		freeSocket(*itr);
	}
	_adpList.clear();

	// �ͷ� SSL ������ݽṹ
	if(_sslCtx)
	{
		SSL_CTX_free(_sslCtx);
	}
}

int IoSelectorImpl::setopt(int optname, const char* optval, int optlen)
{
	// ���� SSL ����(֤��,˽Կ��)
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
		/* ��SSL V2 ��V3 ��׼���ݷ�ʽ����һ��SSL_CTX */
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
			// ����һ�� SSL �׽���
			newAdp = new IoSocketSSLImpl(getSSLCtx(), INVALID_SOCKET);
		}
		else
		{
			// ����һ����ͨ�׽���
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

	/* ���½����׽��ֹ�����IOCP��� */
	if(newAdp && _iocpHandle == CreateIoCompletionPort((HANDLE)newAdp->getSocket(), _iocpHandle, (ULONG_PTR)newAdp, 0))
	{
		/* ��ʼ����������; �ϸ������ԭ��Ļ�Ӧ����һ�� map �� newAdp �� һ�� struct ��������,����ֱ�ӷ��� newAdp ����򵥵� */
		iosock_info_t* sockInfo = new iosock_info_t;
		sockInfo->isInQueue = false;
		sockInfo->eventMask = IO_EVENT_NONE;
		sockInfo->curEvent = IO_EVENT_NONE;

		newAdp->setPtr2(sockInfo);

		/* �������ָ�� */
		_adpList.push_back(newAdp);

		if(acceptBy)
		{
			/* ����ͨ�� accept ��õ�socket,Ĭ�ϵ���һ�� recv ,��ʱ newAdp �ڲ��Ľ��ջ������ض��ǿյ�,���Ի�����һ�� recv IOCP ���� */
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
	// �ͷŹ�������
	iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
	assert(sockInfo);

	//assert(!sockInfo->isInQueue);
	delete sockInfo;
	
	// �ͷ��׽���
	delete adpImpl;
}

int IoSelectorImpl::close(IoSocket* adp)
{
	/*
	* Ҫȷ�� IoSocketImpl û�����ڽ��е� IOCP ��������ɾ��ָ��
	*/
	IoSocketImpl* adpImpl = (IoSocketImpl*)adp;
	iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
	if (0 == adpImpl->closesocket())
	{
		// ����Ѿ��ڻ�Ծ����,��Ӷ�����ɾ��
		if (sockInfo->isInQueue)
		{
			sockInfo->isInQueue = false;
			_actAdpList.remove(adpImpl);
		}

		// ��adp������ɾ��
		_adpList.remove(adpImpl);

		// �ͷ�
		freeSocket(adpImpl);
		return 1;
	}
	return 0;
}

int IoSelectorImpl::ctl(IoSocket* adp, u_int ev)
{
	IoSocketImpl *adpImpl = (IoSocketImpl*)adp;
	ev |= (IO_EVENT_ERROR | IO_EVENT_HANGUP); // �Զ���ӱ��غ�Զ�������쳣״̬.

	// �ȴӻ�Ծ�������Ƴ�
	iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();
	if (sockInfo->isInQueue)
	{
		_actAdpList.remove(adpImpl);
		sockInfo->isInQueue = false;
	}

	// adp Ҳ��Ҫ����
	adpImpl->ctl(ev);

	// ��յ�ǰ�¼�,�����µ��¼�����λ
	sockInfo->curEvent = IO_EVENT_NONE;
	sockInfo->eventMask = ev;

	// ��� adp ��״̬,�ж��Ƿ���Ҫ���ɳ�ʼ�¼�,�����������Ծ����,����һ�� wait() ����ȡ��
	sockInfo->curEvent = (adpImpl->detectEvent() & sockInfo->eventMask);
	if (sockInfo->curEvent && !sockInfo->isInQueue)
	{
		sockInfo->isInQueue = true;
		_actAdpList.push_back(adpImpl);
	}
	return 0;
}

/* ʹ���ڵ��õ� wait �����Է���ֵ IO_WAIT_WAKEUP ���� */
int IoSelectorImpl::wakeup()
{
	return PostQueuedCompletionStatus(_iocpHandle, 0, NULL, NULL);
}

int IoSelectorImpl::wait(IoSocket** adpOut, unsigned int* evOut, int timeo /* = INFINITE */)
{
	// �ȼ���Ծ�������Ƿ��Ѿ��о����� adp,�����ֱ�ӷ���
	if (!_actAdpList.empty())
	{
		IoSocketImpl* adpImpl = _actAdpList.front();
		iosock_info_t* sockInfo = (iosock_info_t*)adpImpl->getPtr2();

		*adpOut = adpImpl;
		*evOut = sockInfo->curEvent;

		_actAdpList.pop_front();
		return IO_WAIT_SUCESS;
	}

	// IoSelector �� <0 ����Ϊ���޵ȴ�,�� Windows INFINITE ���岻һ��
	DWORD dwTimeout = INFINITE;
	if (timeo >= 0)
	{
		dwTimeout = timeo;
	}

	// �ȴ� iocp �������
	int ret = IO_WAIT_SUCESS;
	for (;;)
	{
		// TODO: ÿ��ѭ����ʼʱӦ�ü�ȥ��һ�εȴ���ʱ��
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
				* IO���������Ϊʧ��
				*/
				ev = adpImpl->update(false, iocpOlpPtr, transfered);
			}
			else
			{
				/*
				* IOCP��������һЩ����,�����ǳ�ʱ GetLastError returns WAIT_TIMEOUT ��������ϵͳ����
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
				* Լ���Ļ��ѱ�־
				*/
				ret = IO_WAIT_WAKEUP;
				break;
			}
			else
			{
				/*
				* ����MSDN��˵��GetQueuedCompletionStatus()����TRUE[ֻ]��ʾ��IOCP�Ķ�����ȡ��һ���ɹ����IO�����İ�.
				* ����"�ɹ�"������ֻ��ָ���������������ɹ����,������ɵĽ���ǲ��ǳ�����Ϊ��"�ɹ�",��һ��.
				*
				* 1. AcceptEx �� ConnectEx �ɹ��Ļ�,�����Ҫ��һ����/��������(��ַ�����ݳ���),��ô transfered == 0����.
				* 2. Send, Recv �������������Ļ��������ȴ���0,��transfered == 0Ӧ���ж�Ϊʧ��.
				*
				* ʵ�ʲ��Է��ֽ��ܿͻ�������,ִ��һ��Recv����,Ȼ��ͻ������϶Ͽ�,�������е�����,���� Recv transfered == 0����.
				* �ܶ���֮,�ϲ�Ӧ���ж�������������(������AcceptEx��ConnectEx���յ�Զ�̵�ַ,��ר��ָ���ݲ���)���������ȴ���0,
				* �����صĽ����ʾ transfered = 0 ˵������ʧ��.
				*
				* 2016.3.15 �������׽��ֽӿ� recv() = 0 ��ʾ�Է���ȫ�ر��׽���, IOCP Ӧ��Ҳ��������,�����ͽ����� transfered = 0 �����
				*
				* ����ģ�鱾���޷����� transfered�Ƿ����0���жϲ����Ƿ�ɹ�,��Ϊ�ϲ���ȫ����Ͷ��һ������������Ϊ0��Recv����.
				* ���ڷ������������ǳ��õļ���,������Լ�ڴ�.
				*
				* 2014.12.30 �������������SOCKET�رմ�ʱ�� SOCKET ���� IOCP �����ڽ���,��  GetQueuedCompletionStatus ���� FALSE.
				* ���Զ�̰� SOCKET �ر�,����ʱ���ض�Ӧ�� SOCKET �� IOCP �����ڽ���,��ᵼ����� IO ��� GetQueuedCompletionStatus ����
				* TRUE. transfered �����Ѿ���ɵ��ֽ���,������0Ҳ���ܲ���.
				*/
				ev = adpImpl->update(true, iocpOlpPtr, transfered);
			}
		}

		/*
		* �����¼��������ж��Ƿ���Ҫ����,�п����Զ�����
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
