#include <assert.h>
#include "IoSelectorImpl.h"
//////////////////////////////////////////////////////////////////////////
//																		//
//																		//
//////////////////////////////////////////////////////////////////////////

int IoSelector_Init()
{
	/*
	* 初始化 WinSock
	*/
	WORD ver = MAKEWORD(2,2);
	WSADATA wd;
	if(WSAStartup(ver, &wd) != 0)
	{
		return 1;
	}

	/*
	* 初始化 OpenSSL 库
	*/
    SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	return 0;
}

int IoSelector_Cleanup()
{
	/*
	* 清除 OpenSSL 库
	*/
	ERR_free_strings();
	EVP_cleanup();
	//CRYPTO_cleanup_all_ex_data();

	/*
	* 清除 WinSock 库
	*/
	WSACleanup();
	return 0;
}

IoSelector* CreateIoSelector()
{
	return new IoSelectorImpl();
}

void FreeIoSelector(IoSelector* selector)
{
	delete (IoSelectorImpl*)selector;
}