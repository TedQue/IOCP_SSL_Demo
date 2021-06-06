// IOCP_SSL_Demo.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "IoSelector.h"

int adp_puts(IoSocket* adp, const char* str)
{
	return adp->send(str, strlen(str));
}

int main(int argc, const char *argv[])
{
	const char* url = "https://www.baidu.com/";
	if (argc >= 2)
	{
		url = argv[1];
	}
	// std::cout << "url: " << url << std::endl;
	IoSelector_Init();
	IoSelector* sel = CreateIoSelector();

	IoSocket* adp = sel->socket(IO_TYPE_SOCKET_SSL);
	adp->bind(NULL, 0);
	adp->connect("14.215.177.38", 443);
	sel->ctl(adp, IO_EVENT_SEND);

	IoSocket* actAdp = NULL;
	unsigned int ev = IO_EVENT_NONE;
	for (; sel->wait(&actAdp, &ev) == IO_WAIT_SUCESS;)
	{
		if (TEST_BIT(ev, IO_EVENT_SEND))
		{
			// 第一次触发可写事件,连接成功,发送 http 请求
			std::cout << "connected, sending request ..." << std::endl;
			adp_puts(actAdp, "GET / HTTP/1.1\r\n");
			adp_puts(actAdp, "Accept: */*\r\n");
			adp_puts(actAdp, "Pragma: no-cache\r\n");
			adp_puts(actAdp, "Cache-Control: no-cache\r\n");
			adp_puts(actAdp, "User-Agent: IoSelector/1.0\r\n");
			adp_puts(actAdp, "Connection: close\r\n");
			adp_puts(actAdp, "\r\n");

			// 发送的请求较短,不需要再次等待可写事件,准备接收响应
			sel->ctl(actAdp, IO_EVENT_RECV);
		}
		else if (TEST_BIT(ev, IO_EVENT_RECV))
		{
			char buf[8192] = {};
			actAdp->recv(buf, 8192);

			std::cout << buf << std::flush;
		}
		else
		{
			std::cout << "wait error: " << ev << std::endl;
			break;
		}
	}

	FreeIoSelector(sel);
	IoSelector_Cleanup();
	return 0;
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
