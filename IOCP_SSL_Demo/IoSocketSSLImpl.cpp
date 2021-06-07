#include <assert.h>
#include "IoSocketSSLImpl.h"

// 调试工具函数
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

//* 新版 openssl 库不再暴露 BIO_METHOD 结构,所以不能用下列语句直接声明一个 BIO_METHOD 结构
// 必须通过 BIO_meth_xxx() 接口访问
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
//* 没找到如何实现自定义 BIO 文档说明
//* 问题1: 哪些 bio_method 需要实现,哪些可选?
//* 问题2: 各自定义函数的参数,返回值等有何规范?
//* 
//* 参考 bio_mem 的实现 bio/bss_mem.c
// 
//* 实践测试,下列说明的三个方法必须实现 read/write/ctrl
//* 调试过程发现 调用了 6, 11, 11, 7 调用 ctrl, 即要保证 BIO_flush() 调用成功
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
//* 备用解决方案: 使用 bio_mem 而不自定义 bio, 再从 bio_mem 中读取后投递 iocp 请求
//*/

static BIO *BIO_new_iocp(IoSocketSSLImpl* inst)
{
	BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "bio_iocp");
	//BIO_meth_set_create(m, bio_iocp_create);/* 可选 */
	//BIO_meth_set_destroy(m, bio_iocp_destroy);/* 可选 */
	BIO_meth_set_ctrl(m, bio_iocp_ctrl); /* 自定义 BIO 必须实现 */
	BIO_meth_set_read(m, bio_iocp_read);/* 自定义 BIO 必须实现 */
	//BIO_meth_set_puts(m, bio_iocp_puts); /* 可选 */
	BIO_meth_set_write(m, bio_iocp_write);/* 自定义 BIO 必须实现 */

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

// 返回值: >0 实际读取到的数据长度; <=0 发生错误,由 bio_flags 指定原因
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
		// 通过 accept 生成的实例认为是服务端,设置为在 SSL_Read 时自动握手
		SSL_set_accept_state(_ssl);
	}
	else
	{
		SSL_set_connect_state(_ssl);
	}

	// 使用 mem_bio 方案: 这里生成的 bio 会在 SSL 对象释放时被释放.
	//_rbio = BIO_new(BIO_s_mem());
	//_wbio = BIO_new(BIO_s_mem());
	//SSL_set_bio(_ssl, _rbio, _wbio);

	// 自定义 bio 方案
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
			// 使用相同参数下次继续调用
			assert(r == -1);
			setLastError(WSA_IO_PENDING);
		}
		else if(err == SSL_ERROR_ZERO_RETURN)
		{
			// 检测到对方安全关闭 SSL 连接
			assert(r == 0);
			setStatus(IO_STATUS_PEERCLOSED);
		}
		else
		{
			// 本地错误
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
			// 检测到对方安全关闭 SSL 连接
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
	// 执行 SSL 异步关闭
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
		// SSL 成功关闭
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
	
	// 只要 SSL_read() 没有返回 -1 就认为还有数据可读, 而不是通过 SSL_pending() 来判断

	// SSL_pending() 会导致 SSL_read() 返回 -1, 并且 SSL_get_error() 返回 SSL_ERROR_SSL(1) 不知道是怎么回事
	//if(SSL_pending(_ssl) > 0)
	//{
	//	SET_BIT(ev, IO_EVENT_IN);
	//}

	// 用户应该包容这种情况: wait() 返回 IO_EVENT_IN 事件,但是调用 recv() 却返回 -1,因为SSL recv 是按照 SSL Record 语义处理的.

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

		// 握手完成
		_handshaked = true;

		// 查看对方的证书
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

		// 握手完成,标记为可写
		SET_BIT(ev, IO_EVENT_OUT);
	}
	else
	{
		int err = SSL_get_error(_ssl, hs);
		if (err == SSL_ERROR_WANT_WRITE)
		{
			// 需要等待一次写入事件以继续完成握手
		}
		else if (err == SSL_ERROR_WANT_READ) 
		{
			// 需要等待一次读取事件以继续完成握手
		}
		else 
		{
			// 握手出错
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
	* 对于 SSL 来说,虽然底层BIO读取到数据,但是也许还没构成一个完整的 SSL 记录,所以对于用户来说无数据可读.
	* 但是又必须通过调用 SSL_read 驱动 SSL 引擎调用 BIO 继续读写事件.
	* 所以对于用户来说这样的情况是是正常的也是必须处理的: 套接字可读,调用 SSL_read 返回 -1 和 SSL_ERROR_WANT_READ.
	*/
	return ev;
}
