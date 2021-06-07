#include <assert.h>
#include "IoSocketSSLImpl.h"

// ���Թ��ߺ���
#ifdef NDEBUG
#define sgtrace(...)
#else
void sgtrace(const char* fmt, ...)
{
	char dest[1024] = {};
	va_list args;
	va_start(args, fmt);
	vsprintf_s(dest, 1023, fmt, args);
	va_end(args);

	OutputDebugStringA(dest);
}
#endif

static int bio_iocp_write(BIO *h, const char *buf, int num);
static int bio_iocp_read(BIO *h, char *buf, int size);
static int bio_iocp_puts(BIO *h, const char *str);
static long bio_iocp_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int bio_iocp_create(BIO *h);
static int bio_iocp_destroy(BIO *data);

//* �°� openssl �ⲻ�ٱ�¶ BIO_METHOD �ṹ,���Բ������������ֱ������һ�� BIO_METHOD �ṹ
// ����ͨ�� BIO_meth_xxx() �ӿڷ���
// 
//static BIO_METHOD methods_iosockp = 
//{
//	BIO_TYPE_SOCKET,
//	"iocp_socket",
//	bio_iocp_write,
//	bio_iocp_read,
//	bio_iocp_puts,
//	NULL,                       /* sock_gets, */
//	bio_iocp_ctrl,
//	bio_iocp_create,
//	bio_iocp_destroy,
//	NULL,
//	0
//};

int inst_index = -1;

///*
//* û�ҵ����ʵ���Զ��� BIO �ĵ�˵��
//* ����1: ��Щ bio_method ��Ҫʵ��,��Щ��ѡ?
//* ����2: ���Զ��庯���Ĳ���,����ֵ���кι淶?
//* 
//* �ο� bio_mem ��ʵ�� bio/bss_mem.c
// 
//* ʵ������,����˵����������������ʵ�� read/write/ctrl
//* ���Թ��̷��� ������ 6, 11, 11, 7 ���� ctrl, ��Ҫ��֤ BIO_flush() ���óɹ�
//# define BIO_CTRL_RESET          1/* opt - rewind/zero etc */
//# define BIO_CTRL_EOF            2/* opt - are we at the eof */
//# define BIO_CTRL_INFO           3/* opt - extra tit-bits */
//# define BIO_CTRL_SET            4/* man - set the 'IO' type */
//# define BIO_CTRL_GET            5/* man - get the 'IO' type */
//# define BIO_CTRL_PUSH           6/* opt - internal, used to signify change */
//# define BIO_CTRL_POP            7/* opt - internal, used to signify change */
//# define BIO_CTRL_GET_CLOSE      8/* man - set the 'close' on free */
//# define BIO_CTRL_SET_CLOSE      9/* man - set the 'close' on free */
//# define BIO_CTRL_PENDING        10/* opt - is their more data buffered */
//# define BIO_CTRL_FLUSH          11/* opt - 'flush' buffered output */
//# define BIO_CTRL_DUP            12/* man - extra stuff for 'duped' BIO */
//# define BIO_CTRL_WPENDING       13/* opt - number of bytes still to write */
//# define BIO_CTRL_SET_CALLBACK   14/* opt - set callback function */
//# define BIO_CTRL_GET_CALLBACK   15/* opt - set callback function */
// 
// 
//* ���ý������: ʹ�� bio_mem �����Զ��� bio, �ٴ� bio_mem �ж�ȡ��Ͷ�� iocp ����
//*/

static BIO *BIO_new_iocp(IoSocketSSLImpl* inst)
{
	BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "bio_iocp");
	//BIO_meth_set_create(m, bio_iocp_create);/* ��ѡ */
	//BIO_meth_set_destroy(m, bio_iocp_destroy);/* ��ѡ */
	BIO_meth_set_ctrl(m, bio_iocp_ctrl); /* �Զ��� BIO ����ʵ�� */
	BIO_meth_set_read(m, bio_iocp_read);/* �Զ��� BIO ����ʵ�� */
	//BIO_meth_set_puts(m, bio_iocp_puts); /* ��ѡ */
	BIO_meth_set_write(m, bio_iocp_write);/* �Զ��� BIO ����ʵ�� */

	BIO* b = BIO_new(m);
	BIO_set_data(b, inst);
	BIO_set_init(b, 1);
	BIO_set_shutdown(b, 0);
	return b;
}

static int bio_iocp_create(BIO *bi)
{
    return 1;
}

static int bio_iocp_destroy(BIO *a)
{
    return 1;
}

static long bio_iocp_ctrl(BIO* b, int cmd, long num, void* ptr)
{
	sgtrace("bio_iocp_ctrl(%d, %ld, %p)\n", cmd, num, ptr);

	int ret = 0;
	switch (cmd)
	{
	case BIO_CTRL_FLUSH:
	{
		ret = 1;
		break;
	}
	default:
		ret = 0;
	}
	return ret;
}

// ����ֵ: >0 ʵ�ʶ�ȡ�������ݳ���; <=0 ��������,�� bio_flags ָ��ԭ��
static int bio_iocp_read(BIO *b, char *out, int outl)
{
	IoSocketSSLImpl* inst = (IoSocketSSLImpl*)BIO_get_data(b);
	assert(inst);
    int ret = 0;

    if (out != NULL) 
	{
		ret = inst->recvRaw((void*)out, (size_t)outl);
        BIO_clear_retry_flags(b);
		if(ret <= 0)
		{
			if(inst->getLastError() == IO_EAGAIN)
			{
				BIO_set_retry_read(b);
			}
		}
    }
	return ret;
}

static int bio_iocp_write(BIO *b, const char *in, int inl)
{
	IoSocketSSLImpl* inst = (IoSocketSSLImpl*)BIO_get_data(b);
	assert(inst);

	int ret = inst->sendRaw((const void*)in, (size_t)inl);
    BIO_clear_retry_flags(b);
	if(ret <= 0)
	{
		if(inst->getLastError() == IO_EAGAIN)
		{
			BIO_set_retry_write(b);
		}
	}
	return ret;
}

static int bio_iocp_puts(BIO *b, const char *str)
{
    return bio_iocp_write(b, str, strlen(str));
}

IoSocketSSLImpl::IoSocketSSLImpl(SSL_CTX* sslCtx, SOCKET s, const sockaddr* sockname, const sockaddr* peername)
	: IoSocketImpl(s, sockname, peername), _ssl(NULL), _handshaked(false), _sslShutdownFlag(-1), _canRecv(false), _canSend(false)
{
	_ssl = SSL_new(sslCtx);
	if(s != INVALID_SOCKET)
	{
		// ͨ�� accept ���ɵ�ʵ����Ϊ�Ƿ����,����Ϊ�� SSL_Read ʱ�Զ�����
		SSL_set_accept_state(_ssl);
	}
	else
	{
		SSL_set_connect_state(_ssl);
	}

	// ʹ�� mem_bio ����: �������ɵ� bio ���� SSL �����ͷ�ʱ���ͷ�.
	//_rbio = BIO_new(BIO_s_mem());
	//_wbio = BIO_new(BIO_s_mem());
	//SSL_set_bio(_ssl, _rbio, _wbio);

	// �Զ��� bio ����
	BIO* b = BIO_new_iocp(this);
	SSL_set_bio(_ssl, b, b);
}

IoSocketSSLImpl::~IoSocketSSLImpl()
{
	SSL_free(_ssl);
}

int IoSocketSSLImpl::recvRaw(void* buf, size_t len)
{
	return IoSocketImpl::recv(buf, len);
}

int IoSocketSSLImpl::recv(void* buf, size_t len)
{
	int r = SSL_read(_ssl, buf, len);
	if(r <= 0)
	{
		_canRecv = false;

		int err = SSL_get_error(_ssl, r);
		if(err == SSL_ERROR_WANT_READ)
		{
			// ʹ����ͬ�����´μ�������
			assert(r == -1);
			setLastError(WSA_IO_PENDING);
		}
		else if(err == SSL_ERROR_ZERO_RETURN)
		{
			// ��⵽�Է���ȫ�ر� SSL ����
			assert(r == 0);
			setStatus(IO_STATUS_PEERCLOSED);
		}
		else
		{
			// ���ش���
			assert(r == -1);
			setLastError(err);
			setStatus(IO_STATUS_BROKEN);
		}
	}
	else
	{
		_canRecv = true;
	}
	return r;
}

int IoSocketSSLImpl::sendRaw(const void* buf, size_t len)
{
	return IoSocketImpl::send(buf, len);
}

int IoSocketSSLImpl::send(const void* buf, size_t len)
{
	int w = SSL_write(_ssl, buf, len);
	if(w <= 0)
	{
		_canSend = false;

		int err = SSL_get_error(_ssl, w);
		if(err == SSL_ERROR_WANT_WRITE)
		{
			assert(w == -1);
			setLastError(WSA_IO_PENDING);
		}
		else if(err == SSL_ERROR_ZERO_RETURN)
		{
			// ��⵽�Է���ȫ�ر� SSL ����
			assert(w == 0);
			setStatus(IO_STATUS_PEERCLOSED);
		}
		else
		{
			assert(w == -1);
			setLastError(err);
			setStatus(IO_STATUS_BROKEN);
		}
	}
	else
	{
		_canSend = true;
	}
	return w;
}

int IoSocketSSLImpl::shutdown(int how)
{
	// ִ�� SSL �첽�ر�
	_sslShutdownFlag = how;

	SSL_set_shutdown(_ssl, SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN);
	sslShutdown(IO_EVENT_NONE);
	return 0;
}

u_int IoSocketSSLImpl::sslShutdown(u_int ev)
{
	int shr = SSL_shutdown(_ssl);
	if (1 == shr)
	{
		// SSL �ɹ��ر�
		IoSocketImpl::shutdown(_sslShutdownFlag);
	}
	else
	{
		ev = IO_EVENT_NONE;
		int err = SSL_get_error(_ssl, shr);
		if(SSL_ERROR_WANT_READ == err)
		{
		}
		else if(SSL_ERROR_WANT_WRITE == err)
		{
		}
		else
		{
			ev = IO_EVENT_ERROR;
		}
	}
	return ev;
}

u_int IoSocketSSLImpl::detectEvent()
{
	u_int ev = IoSocketImpl::detectEvent();
	
	// ֻҪ SSL_read() û�з��� -1 ����Ϊ�������ݿɶ�, ������ͨ�� SSL_pending() ���ж�

	// SSL_pending() �ᵼ�� SSL_read() ���� -1, ���� SSL_get_error() ���� SSL_ERROR_SSL(1) ��֪������ô����
	//if(SSL_pending(_ssl) > 0)
	//{
	//	SET_BIT(ev, IO_EVENT_IN);
	//}

	// �û�Ӧ�ð����������: wait() ���� IO_EVENT_IN �¼�,���ǵ��� recv() ȴ���� -1,��ΪSSL recv �ǰ��� SSL Record ���崦���.

	if(_canRecv)
	{
		SET_BIT(ev, IO_EVENT_IN);
	}

	if(_canSend)
	{
		SET_BIT(ev, IO_EVENT_OUT);
	}
	return ev;
}

u_int IoSocketSSLImpl::sslHandshake(u_int ev)
{
	assert(!_handshaked);
	assert(!SSL_is_init_finished(_ssl));
	ev = IO_EVENT_NONE;
	int hs = SSL_do_handshake(_ssl);
	if(1 == hs)
	{
		sgtrace("SSL_do_handshake() return 1\n");

		// �������
		_handshaked = true;

		// �鿴�Է���֤��
		X509* cert = NULL;
		char* line = NULL;
		cert = SSL_get_peer_certificate(_ssl);
		if (cert)
		{
			sgtrace("certificate:\n");

			line = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
			sgtrace("subject name: %s\n", line);

			line = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
			sgtrace("issuer name: %s\n", line);

			X509_free(cert);
		}
		else
		{
			sgtrace("no certificate received\n");
		}

		// �������,���Ϊ��д
		SET_BIT(ev, IO_EVENT_OUT);
	}
	else
	{
		int err = SSL_get_error(_ssl, hs);
		if (err == SSL_ERROR_WANT_WRITE)
		{
			// ��Ҫ�ȴ�һ��д���¼��Լ����������
		}
		else if (err == SSL_ERROR_WANT_READ) 
		{
			// ��Ҫ�ȴ�һ�ζ�ȡ�¼��Լ����������
		}
		else 
		{
			// ���ֳ���
			ev = IO_EVENT_ERROR;
		}
	}
	return ev;
}

u_int IoSocketSSLImpl::update(bool oppResult, IOCPOVERLAPPED* olp, size_t bytesTransfered)
{
	int opp = olp->oppType;
	assert(opp != IO_OPP_NONE);

	u_int ev = IoSocketImpl::update(oppResult, olp, bytesTransfered);

	if(oppResult)
	{
		if(_sslShutdownFlag != -1)
		{
			ev = sslShutdown(ev);
		}
		else if(!_handshaked)
		{
			ev = sslHandshake(ev);
		}
	}

	/*
	* ���� SSL ��˵,��Ȼ�ײ�BIO��ȡ������,����Ҳ��û����һ�������� SSL ��¼,���Զ����û���˵�����ݿɶ�.
	* �����ֱ���ͨ������ SSL_read ���� SSL ������� BIO ������д�¼�.
	* ���Զ����û���˵�������������������Ҳ�Ǳ��봦���: �׽��ֿɶ�,���� SSL_read ���� -1 �� SSL_ERROR_WANT_READ.
	*/
	return ev;
}
