#pragma once

// Winsock selector ʵ��
#include <SDKDDKVer.h>

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN             //  �� Windows ͷ�ļ����ų�����ʹ�õ���Ϣ

#include <Windows.h>
#include <WinSock2.h>
#include <Mswsock.h>

#include <list>
#include "IoSelector.h"

#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "libcrypto.lib")
#pragma comment(lib, "libssl.lib")

typedef unsigned char byte;

/* ����ģʽ: LTģʽ(Ĭ��);ETģʽ */
#define IO_MODE_ET 0
#define IO_MODE_LT 1

/* IOCP �ص��ṹ���� */
typedef struct iocp_overlapped_t
{
	OVERLAPPED olp;
	int oppType;
	byte* buf;
	size_t len;
	size_t ipos;  /* iocp operation pos */
	size_t upos;  /* user operation pos */
	bool et;	/* edge trriger flag */
	long long transfered; /* �ܼƴ��͵��ֽ��� */
}IOCPOVERLAPPED;

/* Ͷ�ݵ�IOCP�������� */
#define IO_OPP_NONE 0
#define IO_OPP_ACCEPT 0x01
#define IO_OPP_CONNECT 0x02
#define IO_OPP_RECV 0x04
#define IO_OPP_SEND 0x08

/* �׽���״̬ */
// 0 δ����״̬,ֻ�ܵ��� connect
#define IO_STATUS_DISCONNECT 0

// > 0 ��ֵ��ʾ����״̬,�׽��ֿ���
#define IO_STATUS_CONNECTED 1
#define IO_STATUS_LISTENING 2
#define IO_STATUS_SHUTDOWN 3

// < 0 ��ֵ��ʾ�׽����Ѳ�����,ֻ�ܹر�
#define IO_STATUS_BROKEN -1	/* ���� */
#define IO_STATUS_CLOSED -2 /* �ѹر� */
#define IO_STATUS_PEERCLOSED -3 /* �Ѿ����Է��ر� */

class IoSocketImpl : public IoSocket
{
protected:
	SOCKET _s;
	sockaddr _sockname;
	sockaddr _peername;
	int _mode;
	int _shutdownFlag;

	int _lastError;
	int _status;

	void* _userPtr;
	void* _userPtr2;

	SOCKET _newSock;
	char* _acceptBuf;
	LPFN_ACCEPTEX _lpfnAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS _lpfnGetAcceptExAddr;

	iocp_overlapped_t _sendOlp;
	iocp_overlapped_t _recvOlp;

	bool busy();
	int setLastError(int err);
	int setStatus(int st);

	// Ͷ��IOCP����
	virtual int postAccept();
	virtual int postConnect(const char *ipAddr, u_short port);
	virtual int postRecv();
	virtual int postSend();

	// ��Ӧ��������
	virtual u_int onAccept(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered);
	virtual u_int onConnect(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered);
	virtual u_int onRecv(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered);
	virtual u_int onSend(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered);

public:
	IoSocketImpl(SOCKET s, const sockaddr* sockname = NULL, const sockaddr* peername = NULL);
	virtual ~IoSocketImpl();

	/*
	* IoSocket �ӿ�
	*/
	virtual int type() const {return IO_TYPE_SOCKET;}
	virtual int getsockname(char *ipAddr, u_short *port);
	virtual int getpeername(char* ipAddr, u_short *port);
	virtual int setopt(int optlevel, int optname, const char* optval, int optlen);
	virtual int getopt(int optlevel, int optname, char* optval, int* optlen);
	virtual int bind(const char* ipAddr, u_short port);
	virtual int listen(int backlog = SOMAXCONN);
	virtual int connect(const char* ip, u_short port);
	virtual int shutdown(int how);
	virtual int closesocket();
	virtual int recv(void* buf, size_t len);
	virtual int send(const void* buf, size_t len);
	virtual int accept(SOCKET* s, sockaddr* sockname, sockaddr* peername);
	virtual int getLastError() const;
	virtual void* getPtr();
	virtual void* setPtr(void* p);

	/*
	* IoSelectorImpl ���õĺ���������
	*/
	virtual u_int update(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered);
	virtual int ctl(u_int ev);
	virtual u_int detectEvent();

	SOCKET getSocket();
	void* getPtr2();
	void* setPtr2(void* p);
};
