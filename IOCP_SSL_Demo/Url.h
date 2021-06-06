/*
 * GPLv3
 * Copyright (C) 2015 阙荣文
*/

/////////////////////////////////////////////////////////////////////////////
#pragma once
#include <string>
#include <vector>

class Url
{
public:
	// url 协议类型
	typedef enum
	{
		sch_unknown = 0,
		sch_file = 1,
		sch_gopher,
		sch_http,
		sch_https,
		sch_ftp,
		sch_mailto,
		sch_mms,
		sch_news,
		sch_nntp,
		sch_telnet,
		sch_wais,
		sch_prospero
	}scheme_t;

protected:
	scheme_t _dftSchemeType;
	std::string _urlStr;			// 原始的url字符串
	
	scheme_t _schType;
	std::string _username;
	std::string _password;
	std::string _hostname;
	unsigned int _port;
	std::string _address; // host:port
	std::string _locate;
	std::string _path;
	std::string _query;
	std::string _fragment;

	bool _parse(const std::string& urlStr);
	void _reset();

public:
	explicit Url(scheme_t defSchType);
	Url(const char* urlStr, scheme_t dftSchType = sch_http);
	Url(const Url& rh);
	virtual ~Url();

	// URL的UTF8编码解码
	static std::string encode(const std::string& inputStr);
	static std::string decode(const std::string& inputStr);

	Url& operator = (const Url& rh);
	Url& operator = (const char* urlStr);
	bool operator == (const Url& rh) const;

	// 判断url是否合法,使这样的语法可以运行 if (url) 或者 if(!url)
	operator bool() const;

	// 返回url的各个部分
	const char* url() const;
	scheme_t scheme() const;
	const char* username() const;
	const char* password() const;
	const char* host() const;
	unsigned int port() const;
	const char* address() const;
	const char* locate() const;
	const char* path() const;
	const char* query() const;
	const char* fragment() const;
};

