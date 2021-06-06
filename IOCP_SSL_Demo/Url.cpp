#include <string.h>
#include <algorithm>
#include <assert.h>
#include "Url.h"

#define TXT(str) str

// 这里的值应该和 typedef enum _protocol_t 一一对应
static const char* scheme_name_table[] = {
	TXT(""), // idx:0 无效
	TXT("file"), // idx:1 sch_file
	TXT("gopher"),
	TXT("http"),
	TXT("https"),
	TXT("ftp"),
	TXT("mailto"),
	TXT("mms"),
	TXT("news"),
	TXT("nntp"),
	TXT("telnet"),
	TXT("wais"),
	TXT("prospero")
};

static Url::scheme_t mapProtocolType(const std::string& protoName)
{
	if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_file]) == 0)
	{
		return Url::sch_file;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_gopher]) == 0)
	{
		return Url::sch_gopher;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_http]) == 0)
	{
		return Url::sch_http;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_https]) == 0)
	{
		return Url::sch_https;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_ftp]) == 0)
	{
		return Url::sch_ftp;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_mailto]) == 0)
	{
		return Url::sch_mailto;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_mms]) == 0)
	{
		return Url::sch_mms;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_news]) == 0)
	{
		return Url::sch_news;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_nntp]) == 0)
	{
		return Url::sch_nntp;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_telnet]) == 0)
	{
		return Url::sch_telnet;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_wais]) == 0)
	{
		return Url::sch_wais;
	}
	else if(_stricmp(protoName.c_str(), scheme_name_table[Url::sch_prospero]) == 0)
	{
		return Url::sch_prospero;
	}
	else
	{
		return Url::sch_unknown;
	}
}

static unsigned int getDefaultPort(Url::scheme_t schType)
{
	unsigned int port = 0;
	switch(schType)
	{
		case Url::sch_file: port = 0; break;
		case Url::sch_gopher: port = 70; break;
		case Url::sch_http: port = 80; break;
		case Url::sch_https: port = 443; break;
		case Url::sch_ftp: port = 21; break;
		case Url::sch_mailto: port = 0; break;
		case Url::sch_mms: port = 1755; break;
		case Url::sch_news: port = 0; break;
		case Url::sch_nntp: port = 119; break;
		case Url::sch_telnet: port = 23; break;
		case Url::sch_wais: port = 0; break;
		case Url::sch_prospero: port = 409; break;
		default: break;
	}

	return port;
}

// 从 str 的范围 [fst, lst) 中查找 sub
static std::string::size_type find(const std::string& str, std::string::size_type frt, std::string::size_type lst, const std::string::value_type* sub)
{
	for(std::string::size_type i = frt; i != lst; ++i)
	{
		for(int j = 0; ;)
		{
			if(str[i + j] != sub[j])
			{
				break;
			}
			++j;
			if(sub[j] == 0) return i;
			if(i + j >= lst - frt) return NULL;
		}
	}
	return lst;
}

static std::string::size_type find(const std::string& str, std::string::size_type frt, std::string::size_type lst, std::string::value_type c)
{
	std::string::value_type sub[2];
	sub[0] = c;
	sub[1] = 0;

	return find(str, frt, lst, sub);
}

static std::string substr(const std::string& str, std::string::size_type frt, std::string::size_type lst)
{
	return str.substr(frt, lst - frt);
}

// 把字符串[off, off + count)根据字符 d 分为两个部分
static int bisect(const std::string& str, std::string::size_type frt, std::string::size_type lst,  std::string::value_type d, std::string& part1, std::string& part2)
{
	std::string::size_type pos = find(str, frt, lst, d);
	if(lst == pos)
	{
		part1 = str.substr(frt, lst - frt);
		return 1;
	}
	else
	{
		part1 = str.substr(frt, pos - frt);
		part2 = str.substr(pos + 1, lst - pos - 1);
		return 2;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Url::Url(const char*urlStr, scheme_t dftSchType) : _dftSchemeType(dftSchType), _schType(sch_unknown), _port(0)
{
	_parse(urlStr);
}

Url::Url(scheme_t dftSchType) : _dftSchemeType(dftSchType), _schType(sch_unknown), _port(0)
{
}

Url::Url(const Url& rh) : _dftSchemeType(rh._dftSchemeType), _schType(rh._schType), _port(rh._port), _urlStr(rh._urlStr),
	_hostname(rh._hostname), _path(rh._path), _query(rh._query), _locate(rh._locate), _address(rh._address),
	_username(rh._username), _password(rh._password), _fragment(rh._fragment)
{
}

Url::~Url()
{
}

void Url::_reset()
{
	_schType = sch_unknown;
	_port = 0;
	
	_urlStr.clear();
	_hostname.clear();
	_address.clear();
	_path.clear();
	_query.clear();
	_locate.clear();
	_username.clear();
	_password.clear();
	_fragment.clear();
}

Url::operator bool() const
{
	return _schType == sch_unknown;
}

Url& Url::operator = (const Url& rh)
{
	_dftSchemeType = rh._dftSchemeType;
	_schType = rh._schType;
	_port = rh._port;
	_urlStr = rh._urlStr;
	_hostname = rh._hostname;
	_address = rh._address;
	_path = rh._path;
	_query = rh._query;
	_locate = rh._locate;
	_username = rh._username;
	_password = rh._password;
	_fragment = rh._fragment;

	return *this;
}

Url& Url::operator = (const char* urlStr)
{
	_reset();
	_parse(urlStr);
	return *this;
}

bool Url::operator == (const Url& rh) const
{
	return ( 
		this->_schType == rh._schType &&
		this->_username == rh._username && this->_password == rh._password &&
		this->_hostname == rh._hostname && this->_port == rh._port &&
		this->_path == rh._path && this->_query == rh._query && this->_fragment == rh._fragment
		);
}

// url格式 <协议名>://<用户名>:<密码>@<主机>:<端口>/<url路径>?<参数名>=<参数值>&<参数名>=<参数值>...#<片段>
bool Url::_parse(const std::string& urlStr)
{
	// 去除开头和结尾的空格
	_urlStr = urlStr;
	auto nend = std::remove_if(_urlStr.begin(), _urlStr.end(), [](char c) { return isblank(c); });
	_urlStr.erase(nend, _urlStr.end());
	if(_urlStr.size() <= 0) return false;

	// 保存url字符串,从此时开始 _urlStr 的值不再改变,所以它的 iterator都一直有效.要分析的内容总是在 [beginPos, endPos) 内.
	std::string::size_type beginPos = 0; 
	std::string::size_type endPos = _urlStr.size();

	for(int st = 0; beginPos < endPos; ++st)
	{	
		// 各个部分需要设置已经被处理过了的结束位置
		std::string::size_type curEndPos = endPos;
		switch (st)
		{
			// scheme - 查找 :// 之前的子串
			case 0:
			{
				std::string::size_type schEndPos = find(_urlStr, beginPos, curEndPos, TXT("://"));
				if(curEndPos == schEndPos)
				{
					// 没找到 :// 则尝试查找 : 看看是不是 maito
					schEndPos = find(_urlStr, beginPos, curEndPos, TXT(':'));
					if(curEndPos != schEndPos && sch_mailto == mapProtocolType(substr(_urlStr, beginPos, schEndPos)))
					{
						_schType = sch_mailto;
						curEndPos = schEndPos + 1;
					}
					else
					{
						_schType = _dftSchemeType;
						curEndPos = beginPos;
					}
				}
				else
				{
					// 识别协议名
					_schType = mapProtocolType(substr(_urlStr, beginPos, schEndPos));
					assert(_schType != sch_unknown);
					curEndPos = schEndPos + 3;
				}
			}
			break;

			// <user>:<password>@<host>:<port>
			case 1:
			{
				// search end
				curEndPos = find(_urlStr, beginPos, curEndPos, TXT('/'));

				// <user>:<pwd>
				std::string::size_type upEndPos = find(_urlStr, beginPos, curEndPos, TXT('@'));
				if(curEndPos != upEndPos)
				{
					bisect(_urlStr, beginPos, upEndPos, TXT(':'), _username, _password);
					beginPos = upEndPos + 1;
				}
		
				// <host>:<port>
				std::string port;
				if(2 == bisect(_urlStr, beginPos, curEndPos, TXT(':'), _hostname, port))
				{
					_address = _hostname + ':' + port;
					_port = atoi(port.c_str());
				}
				else
				{
					_address = _hostname;
					_port = getDefaultPort(_schType);
				}
			}
			break;

			// <locate> = <path> + <qurey> + <fragment>
			case 2:
			{
				_locate = substr(_urlStr, beginPos, curEndPos);
				curEndPos = beginPos;
			}
			break;

			// <path>
			case 3:
			{
				std::string::size_type pathEndPos = find(_urlStr, beginPos, curEndPos, TXT('?'));
				if(curEndPos == pathEndPos)
				{
					pathEndPos = find(_urlStr, beginPos, curEndPos, TXT('#'));
				}
				_path = substr(_urlStr, beginPos, pathEndPos);
				curEndPos = pathEndPos;
			}
			break;

			// <query>
			case 4:
			{
				if(_urlStr[beginPos] == TXT('?'))
				{
					++beginPos;
					curEndPos = find(_urlStr, beginPos, curEndPos, TXT('#'));
					_query = substr(_urlStr, beginPos, curEndPos);
				}
				else
				{
					curEndPos = beginPos;
				}
			}
			break;

			// <fragment>
			case 5:
			{
				if(_urlStr[beginPos] == TXT('#'))
				{
					++beginPos;
					_fragment = substr(_urlStr, beginPos, curEndPos);
				}
				else
				{
					assert(0);
				}
			}
			break;

			default:
			{
				assert(0);
			}
			break;
		}

		// 设置下个循环处理的起始位置
		beginPos = curEndPos;
		assert(endPos == _urlStr.size());
	}

	// 返回
	if(_path.empty()) _path = TXT("/");
	return true;
}

const char* Url::locate() const
{
	return _locate.c_str();
}

const char* Url::url() const
{
	return _urlStr.c_str();
}

const char* Url::host() const
{
	return _hostname.c_str();
}

const char* Url::address() const
{
	return _address.c_str();
}

const char* Url::path() const
{
	return _path.c_str();
}

const char* Url::query() const
{
	return _query.c_str();
}

const char* Url::username() const
{
	return _username.c_str();
}

const char* Url::password() const
{
	return _password.c_str();
}

Url::scheme_t Url::scheme() const
{
	return _schType;
}

unsigned int Url::port() const
{
	return _port;
}

const char* Url::fragment() const
{
	return _fragment.c_str();
}

static char _toHex(int n)
{
	if( n < 0 ) return 0;
	if( n >= 0 && n <= 9 ) return static_cast<char>('0' + n);
	if( n >= 10 && n <= 15) return static_cast<char>('a' + n - 10);
	return 0;
}

std::string Url::encode(const std::string& utf8Str)
{
	// 扫描,如果内有大于 127 的字节,则用HEX表示.
	std::string destStr("");
	for(std::string::size_type idx = 0; idx < utf8Str.size(); ++idx)
	{
		unsigned char ch = static_cast<unsigned char>(utf8Str[idx]);
		if( ch > 127 )
		{
			destStr.push_back('%');
			destStr.push_back( _toHex(ch >> 4));
			destStr.push_back( _toHex( ch & 0x0f));
		}
		else
		{
			destStr.push_back(utf8Str[idx]);
		}
	}

	return destStr;
}

std::string Url::decode(const std::string& inputStr)
{
	std::string destStr("");
	
	// 扫描,得到一个UTF8字符串
	for(std::string::size_type idx = 0; idx < inputStr.size(); ++idx)
	{
		char ch = inputStr[idx];
		if(ch == '%')
		{
			if( idx + 1 < inputStr.size() && idx + 2 < inputStr.size() )
			{
				idx += 2;

				char orgValue[3] = {0}, *stopPos = NULL;
				orgValue[0] = inputStr[idx + 1];
				orgValue[1] = inputStr[idx + 2];
				ch = static_cast<char> (strtol(orgValue, &stopPos, 16));
			}
			else
			{
				// 格式错误
				return inputStr;
				break;
			}
		}
		
		destStr.push_back(ch);
	}
	
	return destStr;
}
