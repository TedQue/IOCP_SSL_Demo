// IOCP_SSL_Demo.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include "Url.h"
#include "IoSelector.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <openssl/crypto.h>

#pragma warning(disable : 4996)

int adp_puts(IoSocket* adp, const char* str)
{
	return adp->send(str, strlen(str));
}

int resolve(const char* host, char* addr)
{
    struct addrinfo hints, * res = NULL, * ptr = NULL;
    memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_CANONNAME;

    DWORD dwRetval = getaddrinfo(host, NULL, &hints, &res);
    if (dwRetval)
    {
        std::cout << "getaddrinfo failed with error: " << dwRetval << std::endl;
        return 0;
    }
    else
    {
		// 简单返回第一个 ipv4 地址
		for (ptr = res; ptr != NULL; ptr = ptr->ai_next)
		{
			in_addr inAddr = ((sockaddr_in*)(res->ai_addr))->sin_addr;
			unsigned long ip = inAddr.S_un.S_addr;
			snprintf(addr, 64, "%d.%d.%d.%d", ip & 0x000000FF, (ip >> 8) & 0x000000FF, (ip >> 16) & 0x000000FF, (ip >> 24));
			break;
		}
		freeaddrinfo(res);
		return 1;
    }
}

int main(int argc, const char *argv[])
{
	std::cout << "Welcome to the world of IOCP and OpenSSL" << std::endl;
	std::cout << "demo v0.2.5 by Que's C++ Studio\r\n" << std::endl;

	if (argc < 2)
	{
		std::cout << "usage: IOCP_SSL_Demo <url> [<save file>]" << std::endl;
		return 1;
	}

	// 初始化 winsock, openssl 库
	IoSelector_Init();
	std::cout << "OpenSSL version: " << OpenSSL_version(OPENSSL_FULL_VERSION_STRING) << "\r\n" << std::endl;

	// 域名解析
	Url url(argv[1]);
    char ip[64] = {};
	if (!resolve(url.host(), ip))
	{
		std::cout << "failed to resolve: " << url.url() << std::endl;
		return 2;
	}
	std::cout << "resolve " << url.url() << " -> " << ip << ":" << url.port() << std::endl;

	// 如果指定了输出文件名,则把 http respone 报文(包括报头)写入文件
	FILE* pf = NULL;
	if (argc >= 3) pf = fopen(argv[2], "w");

	// 读取网页内容
	IoSelector* sel = CreateIoSelector();
	IoSocket* adp = sel->socket(url.scheme() == Url::scheme_t::sch_https ? IO_TYPE_SOCKET_SSL : IO_TYPE_SOCKET);
	adp->bind(NULL, 0);
	adp->connect(ip, url.port());

	// 连接完成时将触发可写事件
	sel->ctl(adp, IO_EVENT_OUT);

	IoSocket* actAdp = NULL;
	unsigned int ev = IO_EVENT_NONE;
	for (;;)
	{
		// 等待网络事件,模仿 epoll_wait
		int waitResult = sel->wait(&actAdp, &ev);
		if (IO_WAIT_SUCESS != waitResult)
		{
			std::cout << "wait failed with error: " << waitResult << std::endl;
			break;
		}

		// 分类处理活跃的网络事件
		if (TEST_BIT(ev, IO_EVENT_OUT))
		{
			// 第一次触发可写事件,连接成功,发送 http 请求
			std::cout << "connected, sending http request ..." << std::endl;
			adp_puts(actAdp, "GET ");
			adp_puts(actAdp, url.locate());
			adp_puts(actAdp, " HTTP/1.1\r\n");
			adp_puts(actAdp, "Host: ");
			adp_puts(actAdp, url.host());
			adp_puts(actAdp, "\r\n");
			adp_puts(actAdp, "Accept: */*\r\n");
			adp_puts(actAdp, "Pragma: no-cache\r\n");
			adp_puts(actAdp, "Cache-Control: no-cache\r\n");
			adp_puts(actAdp, "User-Agent: IoSelector/1.0\r\n");
			adp_puts(actAdp, "Connection: close\r\n");
			adp_puts(actAdp, "\r\n");

			// 发送的请求较短,不需要再次等待可写事件,准备接收响应
			sel->ctl(actAdp, IO_EVENT_IN);
		}
		else if (TEST_BIT(ev, IO_EVENT_IN))
		{
			// 一直读取直到返回 0 或 -1 表示缓冲区已空
			char buf[8192] = {};
			int r = 0;
			while ((r = actAdp->recv(buf, 8191)) > 0)
			{
				buf[r] = 0;
				if (pf)
				{
					fwrite(buf, 1, r, pf);
					std::cout << "recv: " << r << " bytes" << std::endl;
				}
				else
				{
					std::cout << buf << std::flush;
				}
			}
		}
		else
		{
			std::cout << "sock error: " << ev << std::endl;
			sel->close(actAdp);
			break;
		}
	}

	if(pf) fclose(pf);

	FreeIoSelector(sel);
	IoSelector_Cleanup();
	return 0;
}