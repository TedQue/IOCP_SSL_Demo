#include <assert.h>
#include "IoSocketImpl.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#define MIN_SOCKADDR_BUF_SIZE (sizeof(sockaddr_in) + 16)

/*
* ���ߺ���
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
	* ��������Ѿ����ӵ��׽���
	*/
	// ���ó�ʼ״̬
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
	* ������ջ������ͷ��ͻ�����
	*/
	memset(&_sendOlp, 0, sizeof(iocp_overlapped_t));
	memset(&_recvOlp, 0, sizeof(iocp_overlapped_t));
	_recvOlp.len = RECV_BUF_LEN;
	_sendOlp.len = SEND_BUF_LEN;
	_sendOlp.buf = new byte[SEND_BUF_LEN];
	_recvOlp.buf = new byte[RECV_BUF_LEN];

	// �����׽��ֵ�ַ
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
		// ������ƽ̨��صĿ������ԵĴ�����ת��Ϊƽ̨�޹صĿ����Դ�����
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
		// ���汾�ص�ַ
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
		// ����Ϊ����״̬
		setStatus(IO_STATUS_LISTENING);

		// listen() �Ѿ��ɹ�, ���� r = 0
		// listen ���óɹ����Զ�����һ�� accept ����,������������ʱ, IoSocket ״̬��Ϊ�ɶ�;�����������Ҳ������һ���û����� wait ֮�󷵻�
		int pr = postAccept();
		if(pr == 0 || pr == WSA_IO_PENDING || pr == WSAECONNRESET)
		{
			// postAccept() �ɹ�,�û�ͨ�� wait() �õ����
		}
		else
		{
			// postAccept() ��������,�׽��ֱ��Ϊ��
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
		// ֱ�ӹر��׽���ʹ�Ѿ�Ͷ�ݵ� IOCP ��������
		::closesocket(_s);
		_s = INVALID_SOCKET;

		// �����׽���״̬Ϊ�ѹر�
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
	// ����һ��shutdown���

	_shutdownFlag = how;
	setStatus(IO_STATUS_SHUTDOWN);

	return 0;
}

int IoSocketImpl::accept(SOCKET* s, sockaddr* sockname, sockaddr* peername)
{
	int ret = 0;

	if(_recvOlp.oppType == IO_OPP_NONE)
	{
		// AccpetEx�����Ѿ����,�������Ӻ͵�ַ����
		if(!_lpfnGetAcceptExAddr)
		{
			/*
			* ���GetAcceptExSockaddrs()����ָ��
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

		// ������һ��IO
		int pr = postAccept();
		if(pr == 0 || pr == WSA_IO_PENDING || pr == WSAECONNRESET)
		{
		}
		else
		{
			// ��¼����,�ѵ�ǰ�׽��ֱ��Ϊ��,���� accept() ���سɹ�(��Ϊ��ȷ�ɹ���),�û���ͨ�� wait() �ȴ���ǰ�׽���(�����׽���)�ᷢ�ִ���
			setLastError(pr);
			setStatus(IO_STATUS_BROKEN);
		}
	}
	else
	{
		// ��IO�������ڽ���,��������
		setLastError(WSA_IO_PENDING);
		ret = SOCKET_ERROR;
	}

	return ret;
}


int IoSocketImpl::postAccept()
{
	/* ��IO�������ڽ��л����Ѿ������һ�����ӵ������ӵ��׽���û�б�ȡ�� */
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
		* ���AcceptEx()����ָ��
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
	* ����һ���µ��׽���(accept ���õ�ֻ����TCP���׽���)
	*/
	_newSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	assert(_newSock != INVALID_SOCKET);

	// ����һ�� IOCP AcceptEx ����
	DWORD dwBytesReceived = 0;
	_recvOlp.oppType = IO_OPP_ACCEPT;
	if(!_lpfnAcceptEx(_s, _newSock, _acceptBuf, 0, MIN_SOCKADDR_BUF_SIZE, MIN_SOCKADDR_BUF_SIZE, &dwBytesReceived, (OVERLAPPED*)&_recvOlp))
	{
		return WSAGetLastError();
	}
	return 0;
}

// Ӧ�õ��� connect ������,��� IoSocket ״̬��Ϊ��д���ʾ���ӽ����ɹ�;״̬��Ϊ�쳣���ʾ���ӽ���ʧ��.
int IoSocketImpl::connect(const char* ip, unsigned short port)
{
	/* ����,��ֹ�û��ظ����� */
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
	* ȡ��ConnectEx����ָ��
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
	* ִ��ConnectEx
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

	// ���ƻ������ڵ�����

	/* �Ƿ��Ѿ���ֹ���� */
	if(_shutdownFlag == SD_RECEIVE || _shutdownFlag == SD_BOTH)
	{
		// �Ѿ��ر�
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

			/* ���ݿ����ɹ�,���ؿ������� */
			ret = len;
		}
		else
		{
			/* ����ʧ�� */
			ret = SOCKET_ERROR;
		}

		// �����Ѿ�����ȡ���,��Ե����,��Ͷ��һ�� recv iocp ����
		if(_recvOlp.upos >= _recvOlp.ipos)
		{
			/* ��Ե������� */
			_recvOlp.et = true;

			/* Ͷ��һ�� recv ���� */
			// ���Է���һ��RECV IO(ֻ�е���������û�����ݲŻ�����Ͷ�� recv iocp����)
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
				/* ��⵽һ�����ش��� */
				setLastError(pr);
				setStatus(IO_STATUS_BROKEN);
			}
		}
	}
	return ret;
}

/*
* recv io �ύ�Ļ��������Ǵ� [ipos -> end), ����������Ч���ݵķ�Χ�� [upos -> ipos) ��Ч����(�Ѿ���user��ȡ)��Χ�� [start, upos)
*/
int IoSocketImpl::postRecv()
{
	if(_shutdownFlag == SD_RECEIVE || _shutdownFlag == SD_BOTH)
	{
		// ����Ѿ���ֹ�˽��վͲ�Ӧ�õ��� postRecv()
		assert(0);
		return WSAESHUTDOWN;
	}

	/* ��IO�������ڽ��� */
	if(_recvOlp.oppType != IO_OPP_NONE)
	{
		assert(_recvOlp.oppType == IO_OPP_RECV);
		return WSA_IO_PENDING;
	}

	/*
	* ���û�����,�����Ѿ���ȡ�Ĳ���
	*/
	memmove(_recvOlp.buf, _recvOlp.buf + _recvOlp.upos, _recvOlp.ipos - _recvOlp.upos);
	_recvOlp.ipos -= _recvOlp.upos;
	_recvOlp.upos = 0;
	
	if(_recvOlp.ipos > 0)
	{
		// ֻҪ�������ڻ�������,�Ͳ�Ͷ����һ�� iocp ����,�� send ���߼��е㲻һ��.
	}
	else
	{
		/*
		* Ͷ��һ��IO RECV ����
		*/
		_recvOlp.oppType = IO_OPP_RECV;
		WSABUF wsaBuf = { _recvOlp.len - _recvOlp.ipos, (char*)_recvOlp.buf + _recvOlp.ipos };
		DWORD dwFlags = 0;
		DWORD dwTransfered = 0;
		assert(wsaBuf.len > 0);
		assert(wsaBuf.len == RECV_BUF_LEN); // �߼��Ͻ��ջ���Ϊ�ղ�Ͷ�� IOCP ����,���Աض�Ͷ����������������

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
		// �׽����Ѿ����ر�
		ret = SOCKET_ERROR;
		setLastError(WSAESHUTDOWN);
	}
	else
	{
		// д�뻺����
		if(len > _sendOlp.len - _sendOlp.upos) len = _sendOlp.len - _sendOlp.upos;
		if(len > 0)
		{
			memcpy(_sendOlp.buf + _sendOlp.upos, buf, len);
			_sendOlp.upos += len;
		
			// ���ؿ�������
			ret = len;

			// ����������,��Ե������������
			if(_sendOlp.upos >= _sendOlp.len)
			{
				_sendOlp.et = true;
			}
		
			// ���Է���һ�� send IO
			int pr = postSend();
			if(0 == pr || WSA_IO_PENDING == pr)
			{
			}
			else
			{
				assert(pr != WSAESHUTDOWN);
				// ��⵽һ�����ش���,�´��û����� wait() ʱ�����������
				setLastError(pr);
				setStatus(IO_STATUS_BROKEN);
			}
		}
		else
		{
			/* ���������� */
			ret = SOCKET_ERROR;
			setLastError(WSA_IO_PENDING);
		}
	}
	return ret;
}

/*
* send io �������ֲ�, �ύ���� [start -> ipos), ��дδ�ύ���� [ipos -> upos), ���ಿ�� [upos -> end)
*/
int IoSocketImpl::postSend()
{
	/* ��������˹رձ�־ҲҪ���������Ѿ��������������ڵ����� */
	/* ��IO�������ڽ��� */
	if(_sendOlp.oppType != IO_OPP_NONE)
	{
		assert(IO_OPP_SEND == _sendOlp.oppType);
		return WSA_IO_PENDING;
	}

	/* ���÷��ͻ�����,���Ѿ����ͳɹ������ݶ��� */
	assert(_sendOlp.buf);
	memmove(_sendOlp.buf, _sendOlp.buf + _sendOlp.ipos, _sendOlp.upos - _sendOlp.ipos);
	_sendOlp.upos -= _sendOlp.ipos;
	_sendOlp.ipos = _sendOlp.upos;

	if(_sendOlp.ipos <= 0)
	{
		// ���ݷ������
		/* ���������shutdown���,�����ͻ�����ȫ��������Ϻ󷵻� SHUTDOWN */
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
		* Ͷ��һ��IO SEND ����
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

	// ��¼����ģʽ
	_mode = TEST_BIT(ev, IO_EVENT_ET) ? IO_MODE_ET : IO_MODE_LT;

	// ��������� IO_EVENT_IN ��Ͷ��һ��������(����Ѿ��ж������ڽ����л����)
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
* �����׽��ֵ��ڲ�״̬�������лᱻ�������¼�
* ����׽��ַ�����**����**������ᴥ�� IO_EVENT_ERROR �¼�.����һ���Ѿ�������׽��ַ��� selector ���ܻᱻ����.
* **Զ��**������ᱻ����,ֱ��ִ�в������ٴη���Զ�̴���.
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
		// �ж��Ƿ�ɶ� 
		// (1) (���ջ�������δ������->��Ϊ���ھ���״̬. [start ->(�Ѷ�) -> upos -> (δ��) -> ipos (���л�IOCP������) -> end])
		// (2) ����״̬��,����˵���Ѿ���һ�� socket ����
		if(_shutdownFlag == SD_RECEIVE || _shutdownFlag == SD_BOTH)
		{
		}
		else
		{
			// ���뻺����������,���������׽���û����Ͷ�� IOCP ����(˵���Ѿ������һ�� ACCEPTEX) -> �ɶ�
			if((_recvOlp.upos < _recvOlp.ipos) || (_recvOlp.oppType == IO_OPP_NONE && _status == IO_STATUS_LISTENING))
			{
				SET_BIT(ev, IO_EVENT_IN);
			}
		}

		// �ж��Ƿ��д (�׽����Ѿ�����,����д�����п��пռ�)
		if(_shutdownFlag == SD_SEND || _shutdownFlag == SD_BOTH)
		{
			// �׽��ֱ��ر�,���������Ѿ��������,�򴥷� ERR ָʾ�û����԰�ȫ�ر��׽���
			if(_sendOlp.ipos == _sendOlp.upos)
			{
				SET_BIT(ev, IO_EVENT_ERROR);
			}
		}
		else
		{
			// ״̬����,������������ڻ��п��пռ� -> ��д
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
		/* �����׽��ֵ�ַ(�� WIN7 ����Ч,��Ч�ĵ�ַ�� accept() ������ͨ�� GetAcceptExSockaddrs() �����ȷ�ĵ�ַ ) */
		if(0 != setsockopt( _newSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&_s, sizeof(_s)) )
		{
			assert(0);
		}

		/* �����ɶ��¼�(����IOSelector�Ŀɶ�����) */
		ev = IO_EVENT_IN;
	}
	else
	{
		/*
		* WSAECONNRESET: Զ�����Ӻ��ֱ��ر�. �����ɶ��¼�,���ǵ��� accept �᷵��һ�� INVALID_SOCKET
		* 2016.1.8 - �����������׽��ַ�������,Ҳ������ AcceptEx ����ʧ��,�������?
		*/
		// �ر� _newSock,����ʲô���,_newSock ����Ч��.
		::closesocket(_newSock);
		_newSock = INVALID_SOCKET;

		/* ����û������ȫ�ְ� getsockopt */
		// int sockerr = 0;
		// int sockerrlen = sizeof(int);
		// if(::getsockopt(_s, SOL_SOCKET, SO_ERROR, (char*)&sockerr, &sockerrlen) == 0 && sockerr == 0)

		/* 2016.1.8 - WSAECONNRESET �������������׽��ּ���ʹ��,���Ա��ΪԶ�̴��� */
		int err = WSAGetLastError();
		if(err == WSAECONNRESET) 
		{
			/* ȷ�ϲ��Ǳ����׽��ִ����� IOCP ʧ�� */
			// assert(WSAGetLastError() == WSAECONNRESET);
			ev = IO_EVENT_IN;
		}
		else
		{
			/* �����׽��ִ��� */
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

	/* ���ӳɹ��򴥷���д�¼�,���򴥷������¼�(������Ӧ����ʶ�����ʱ�ĳ����¼�ָ����ʧ��) */
	if(oppResult)
	{
		// ����״̬Ϊ:������
		setStatus(IO_STATUS_CONNECTED);

		// �����ӵ��׽��ִ��ڿ�д״̬,������д�¼�
		ev = IO_EVENT_OUT;

		/* �����׽���״̬(����ûʲô��) */
		if(0 != ::setsockopt(_s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0))
		{
			assert(0);
		}

		/* ����Զ�̵�ַ */
		int len = sizeof(sockaddr);
		if(0 != ::getpeername(_s, &_peername, &len))
		{
			DWORD err = WSAGetLastError();
			assert(0);
		}
	}
	else
	{
		/* ���������¼�,�������׽��ִ�����,��Ϊ�������ٴε��� Connect �������� */
		ev = IO_EVENT_HANGUP;
	}

	return ev;
}

u_int IoSocketImpl::onRecv(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;

	/* ֻ�н��ջ�����Ϊ��ʱ�Ż�Ͷ�� IOCP ���� */
	assert(_recvOlp.ipos == 0 && _recvOlp.upos == 0);

	// ��Ϊ����Ͷ�ݳ���Ϊ0�� recv ����,���� bytesTransfered = 0 Ҳ��ʧ��(��Ӧ�ó���)
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

			/* ֻҪ���ܻ����ڻ������ݾͲ�����Ͷ�� recv ����, ���û����� recv() ����*/
		}
		else
		{
			/* һ����Ч��Ͷ��? ȷ��Ͷ�� recv ʱ,len > 0 �� assert, Ϊʲô�ᷢ�������ɹ����� bytesTransfered = 0 �����? */
			/* ���� berkeley socket recv() ����ֵ, ���������Ӧ���Ǳ����Է��Ѿ����׽��ֹر���. */
			ev = IO_EVENT_HANGUP;
			setStatus(IO_STATUS_PEERCLOSED);
		}
	}
	else
	{
		// IOCP ģ��û���ر�İ취֪���Է��Ƿ�ر�������,ֻ��ͨ��Ͷ�� IOCP ����,Ȼ����������
		//_sgtrace("IoSocketImpl(0x%x)::update IO_OPP_RECV :%d bytes, WSAError: %d.\r\n", this, bytesTransfered, WSAGetLastError());
					
		/* Ͷ��ʧ��, ����error�¼� */
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

			/* send ���߼���ֻҪ���ͻ������ڻ�������,�ͱ���������� */
			int dsr = postSend();
			if(0 == dsr || WSA_IO_PENDING == dsr)
			{
				// Ͷ�ݳɹ����߻�����Ϊ��
			}
			else if(WSAESHUTDOWN == dsr)
			{
				/* ��������ʱ����������ش���,����ERR�¼�, ERR�¼���EPOLLOUT���� */
				/* �Ѿ�������shutdown���,���һ������ڵ������Ѿ�ȫ������,�򴥷�ERROR�¼�ʹ�û����Եõ�֪ͨ */
				ev = IO_EVENT_ERROR;
				setLastError(dsr);
			}
			else
			{
				// ��⵽һ�����ش���
				setLastError(dsr);
				setStatus(IO_STATUS_BROKEN);
			}
		}
		else
		{
			/* �Է��Ѿ��ر������� */
			ev = IO_EVENT_HANGUP;
			setStatus(IO_STATUS_PEERCLOSED);
		}
	}
	else
	{
		/* ����error�¼�,���ұ���׽���Ϊ�� */
		ev = IO_EVENT_ERROR;
		setLastError(WSAGetLastError());
		setStatus(IO_STATUS_BROKEN);
	}
	return ev;
}

/* �����ڲ�״̬,����һ�������¼� */
u_int IoSocketImpl::update(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	u_int ev = IO_EVENT_NONE;

	/*
	* �����Ѿ���ɵĲ���:ͳ���ֽ����ȵ�.
	*/
	if(oppResult)
	{
		olp->transfered += bytesTransfered;
	}

	/* ��ձ�� */
	int oppType = olp->oppType;
	olp->oppType = IO_OPP_NONE;

	if(_s == INVALID_SOCKET)
	{
		/*
		* �׽����Ѿ����ر�,������е� IOCP �������Ѿ�����(��æµ),����ʾɾ��.
		*/
		assert(_status == IO_STATUS_CLOSED);
	}
	else
	{
		/*
		*  ���ദ���Ѿ���ɵĲ������
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
