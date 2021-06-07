#pragma once

/* Copyright (C) 2015 ������
 *
 * GPLv3
 *
 * ��ϵԭ����: querw@sina.com 
*/

/*
* Socket IO ��װ,�� Windows ƽ̨�� IOCP ģ�ͺ� Linux/Unix ƽ̨�� epoll �ṩһ��ͳһ�Ľӿ�.
* ��Ҫ�ǰ� IOCP ģ�͵Ļص���ʽ��ΪӦ��������ȡ��ʽ�� epoll �ı�̷��£.
*
*/

/*
* Ԥ��Ӧ�ó���
* ���߳�ʹ�� selector, ͬʱ���ֻ��һ�����������һ����������
* 
*/

/*
* λ�������ߺ꺯��
*/
#define TEST_BIT(val, bit) ((val) & (bit))
#define SET_BIT(val, bit) ((val) |= (bit))
#define UNSET_BIT(val, bit) ((val) &= ~(bit))

/* SOCKET �̶�������ڲ��������ĳ��� */
#define RECV_BUF_LEN 2048
#define SEND_BUF_LEN 2048

/*
* IoSelector ��װ������ socket ��ƽ̨����,�����ڴ�����ֵ��ʱ����붨��ƽ̨����ص�ֵ
* berkeley �׽��ֽӿڷ���ֵ��һ��ԭ��:
* 0: recv / send ��ʾ�Է������ر� gracefully closed, ������ʾ�ɹ�
* >0: recv / send ��ʾ�ɹ�������ַ���
* -1: SOCKET_ERROR 
* һ��˵��,�û��ж�����3������ֵ���㹻��. ��������0�Ĵ�������ƽ̨��ص�.
*/

#define IO_SUCESS	0		/* �ɹ� */
#define IO_FAILED	-1		/* ʧ�� */
#define IO_EAGAIN	1		/* �Ժ����� */

// �׽�������
#define IO_TYPE_SOCKET		0
#define IO_TYPE_SOCKET_SSL	1

/* 
* �׽��ֽӿ�,���ӿ�ͬ Berkeley �׽���
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
* ctl ������EVENT����
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
* wait() ����ֵ
*/
#define IO_WAIT_SUCESS	0 /* ȡ����һ����ԾIOAdpater */
#define IO_WAIT_TIMEOUT 1 /* ��ʱ */
#define IO_WAIT_ERROR	2 /* ���� */
#define IO_WAIT_WAKEUP	3 /* ���� */

/*
* IoSelector ѡ��
*/
#define IO_SELECTOR_OPT_SETCRT		0x0002	/* ����֤��, ���� C�ַ�����ʾ֤����ļ�·�� */
#define IO_SELECTOR_OPT_SETPRVKEY	0x0004	/* ����˽Կ, ���� C�ַ�����ʾ��Կ���ļ�·�� */

class IoSelector
{
public:
	/* ������������ IoSocket ���� */
	virtual IoSocket* socket(int t) = 0;
	virtual IoSocket* accept(IoSocket* sock) = 0;
	virtual int close(IoSocket* adp) = 0;

	/* ѡ�����(���� SSL ������) */
	virtual int setopt(int optname, const char* optval, int optlen) = 0;
	virtual int getopt(int optname, char* optval, int* optlen) = 0;

	/* ���� IoSocket �Ĵ����������� IoSelector ����,���� ctl �������� EPOLL_CTL_ADD �� EPOLL_CTL_DEL ����������,ִֻ�� EPOLL_CTL_MOD ���� */
	virtual int ctl(IoSocket* adp, unsigned int ev) = 0;

	/* ÿ��ֻ����һ��"��Ծ"�� IoSocket ����,������ epoll_wait һ������һ������ */
	virtual int wait(IoSocket** adp, unsigned int* ev, int timeo = -1) = 0;

	/* ʹ���ڵ��õ� wait �����Է���ֵ IO_WAIT_WAKEUP ���� */
	virtual	int wakeup() = 0;
};

extern "C"
{
	int IoSelector_Init();
	int IoSelector_Cleanup();

	IoSelector* CreateIoSelector();
	void FreeIoSelector(IoSelector* selector);
}