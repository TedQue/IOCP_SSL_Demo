#pragma once
#include "IoSocketImpl.h"
#include "IoSocketSSLImpl.h"

class IoSelectorImpl : public IoSelector
{
protected:
	HANDLE _iocpHandle;
	SSL_CTX* _sslCtx;
	std::list<IoSocketImpl*> _adpList;
	std::list<IoSocketImpl*> _actAdpList;

	SSL_CTX* getSSLCtx();

public:
	IoSelectorImpl();
	virtual ~IoSelectorImpl();

	IoSocket* socket(int t);
	IoSocket* accept(IoSocket* acceptBy);
	IoSocket* socket(IoSocket* acceptBy, int t);
	int close(IoSocket* adp);

	int setopt(int optname, const char* optval, int optlen);
	int getopt(int optname, char* optval, int* optlen);

	int ctl(IoSocket* adp, u_int ev);
	int wait(IoSocket** adp, unsigned int* ev, int timeo = -1);
	int wakeup();
};