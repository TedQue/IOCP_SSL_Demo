#pragma once
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "IoSocketImpl.h"

/*
* ʹ�� OpenSSL ��ʵ�� IoSocket
*
*
*/

class IoSocketSSLImpl : public IoSocketImpl
{
protected:
	SSL* _ssl;
	bool _handshaked; 
	int _sslShutdownFlag;
	bool _canRecv;
	bool _canSend;

	u_int sslHandshake(u_int ev);
	u_int sslShutdown(u_int ev);

public:
	IoSocketSSLImpl(SSL_CTX* sslCtx, SOCKET s, const sockaddr* sockname = NULL, const sockaddr* peername = NULL);
	virtual ~IoSocketSSLImpl();

	// д��/��ȡԭʼ����(�� BIO ����)
	int recvRaw(void* buf, size_t len);
	int sendRaw(const void* buf, size_t len);

	virtual int type() const { return IO_TYPE_SOCKET_SSL; }

	// ���ط��ͽ��սӿ�,ʹ�� SSL_read / SSL_write
	virtual int recv(void* buf, size_t len);
	virtual int send(const void* buf, size_t len);
	virtual int shutdown(int how);

	virtual u_int detectEvent();
	virtual u_int update(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered);
};