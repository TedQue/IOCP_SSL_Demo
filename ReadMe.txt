<<һ�� Windows IOCP ���� OpenSSL ʵ�ַ���>>

by ������ Que's C++ Studio
2021-06-06

1. IOCP �� epoll ���ģ�ͱȽ�

��������Ա�Ƕ��Ѿ�ϰ���˴�ͳ�� select ģ��(���� linux epoll �� BSD kqueue),�ǳ������. �� Windows IOCP API�ӿ������һ��,�Ե��൱����.
�Ҹ��˻��Ǹ�ϲ����ͳ�� select ģ��,��������Ϊ��ν���ص�����ûʲô����,Ϊʲô��ҪͬʱͶ�ݶ��ͬ��������?���緢��һ�����ļ�ʱ,���ǲ����ܷ��γ���һֱͶ��д�����ʹ�ڴ�����ʧ��.
��������ԭ��,ҲΪ�˿�ƽ̨ʹ�÷���,���հ� IOCP ��װΪ epoll ���Ľӿ�,����ṹ��������,��������¼�ѭ��:

	for (;;)
	{
		// �ȴ������¼�
		selector->wait(&socket, &ev);
		
		// ���ദ���Ծ�������¼�
		if (TEST_BIT(ev, IO_EVENT_SEND))
		{
		}
		else if (TEST_BIT(ev, IO_EVENT_RECV))
		{
		}
		else
		{
		}
	}
	������ο� main() ����.

2. ��� OpenSSL API �ĳ���ṹ

��Ϊ OpenSSL ���û�,���ǿ��԰������Ϊһ���ں�,����ʱд�����Ķ�������,����ʱд�����Ķ�������.����ںеĲ����ӿڱ���Ϊ "BIO",Ҳ����˵ SSL ��ͨ�� BIO �ӿ�����罻����Ϣ,
�� socket ֻ���ڶ� BIO ʵ��֮һ.���� BIO �ӿ�,���԰� SSL Э��Ӧ�õ��������͵�����ͨ����,����ǳ��������.
ͨ�����ӽ���֮��,ͨ��˫������Ҫ���� SSL ����,���ֹ�����Ҳ��������ݽ���,��ͬ���׽�����,���� SSL_accept() ���� SSL_connect() ������ֱ���������;
���첽�׽���������Ҫ�������� SSL_do_handshake() ֱ������ 1 Ϊֹ.
���� SSL Э����ͨ��˫�����ǶԵ�,����Ϊ�������˺Ϳͻ���������ͬ��ɫ,�����ڵ��� SSL_do_handsake() ֮ǰ,
������Ҫ���� SSL_set_accept_state() �� SSL ����Ϊ�������˽�ɫ���� SSL_set_connect_state()����Ϊ�ͻ��˽�ɫ.
���� SSL ����Э����� SSL_accept() = SSL_set_accept_state() + SSL_do_handsake(); SSL_connect() = SSL_set_connect_state() + SSL_do_handsake().

3. IOCP �� OpenSSL ����˼·

ͨ�������� BIO �ӿڵĽ��ܿ�֪, BIO �ӿ������� IOCP �� OpenSSL ��������ԭ��ķ���.
OpenSSL �����ṩ���ڶ� BIO ʵ��,�����ڴ� bio_mem �ƺ��ܷ������ǵ���Ҫ,˼·��ͨ�� SSL_write() ������д�� bio_mem, Ȼ�� IOCP �����̴߳Ӹ� bio_mem �ж�ȡ���Ĳ����ͳ�ȥ;
����ʱ, IOCP �����̰߳�����д�� bio_mem,�ٵ��� SSL_read() ��ȡ�� bio_mem �õ�����.����������������ǿ��е�.����Ҳ�в���֮��,������д�� bio_mem ��д�� IOCP ������,��Ҫ
����������ռ�ú��ڴ渴��,����һ; ��ͨ��ʹ���첽�׽��ֵ�ϰ����,��������һֱ���� SSL_write() ֱ������ -1 Ϊֹ,bio_mem ���Զ���չ�ڴ�����,����ֻҪ�ڴ��㹻,���ǿ���һֱ���� 
SSL_write() ����������ȫ��д���ڴ�,Ϊ�˱�������������,ֻ���޸ĵ��� SSL_write() ���÷�,�����.
��һ��������ʵ���Լ��� BIO,ʹ SSL_read/write() ֱ�Ӷ�д IOCP ������,ʹ BIO ��ȫ�������ǵ���������.�ο� bio_mem ��ʵ��Դ�� crypto/bio/bss_mem.c ����������, demo �в���
�ľ����������.�ڱ�д�� demo �Ĺ����л��и�С����,��� demo ����Ҫ����д�� 2016 ��,���հ�װ�����°� OpenSSL ���ֱ��벻ͨ����,ԭ�����°���Ѿ����ٱ�¶ BIO_METHOD �ṹ,��Ҫ����
һ�� BIO_meth_xxx() �ӿڷ��ʸýṹ.�⵱Ȼ����ȷ��,��Ϊ�û����ֻʹ�ýӿڶ���Ҫ��������ڲ�ϸ��.

ʵ�ֿɹ� SSL_read/write() ʹ�õ� BIO ��Ҫ����3���ؼ�����,�ֱ��Ӧ BIO_read(), BIO_write() �� BIO_flush() �ڴ�3�������������� SSL ��ֱ�Ӷ�д IOCP ������.
������ο�Դ�� IoSocketSSLImpl.cpp ֮ bio_iocp_read(), bio_iocp_write() �� bio_iocp_ctrl().

[���]
�����д����� IOCP �� OpenSSL ���ϵ�����,�� IOCP ģ��һ�����ڴ���Ŀ�в��Ǻܺð���,һ������ֱ�����ڲ����,����.
��� demo �Ǵӱ��˵���һ����Ŀ�г�ȡ������,Ϊ�˼��ٸ��ż������糬ʱ����,���̰߳�ȫ����ش���,Ҳû�о�����ϸ����,��Ҫ��ʾ�������Ϸ����Ŀ�����,������.

================================================================
��1: ����Ŀ���� https://www.baidu.com/ ��Ч��(֤����Ϣ������·��ĵ�����������)
screenshot.png

��2: ���ڱ��� Windows 10 �ϱ��밲װ OpenSSL ˵��(������޷����� demo ��Աȴ˰�װ����)
1. ��װ Visual Studio 2019 ������
2. ���� OpenSSL Դ�� https://github.com/openssl/openssl ,���� NOTES-WINDOWS.md �ļ�˵�������Ͱ����.
3. ��װ Perl http://strawberryperl.com/ ����Ӱ�װ·���� path ��������,�������� "perl --version" ����ȷ��ӡ�汾��Ϣ.
4. ��װ nasm https://www.nasm.us ����Ӱ�װ·���� path ��������,�������� "nasm --version" ����ȷ��ӡ�汾��Ϣ.
5. ���� 32 λ OpenSSL ��
	�Թ���ԱȨ�޴ӿ�ʼ�˵����� "Developer Command Prompt for VS 2019" ֮�� cd �� OpenSSL Դ��Ŀ¼
	�������� "perl Configure VS-WIN32" ���� OpenSSL ����ѡ��Ϊ VS-WIN32
	�������� "nmake"
	�������� "nmake test"
	�������� "nmake install" (Ĭ�ϰ�װ·���� C:\Program Files (x86)\OpenSSL)
6. ������ OpenSSL ��װ·������� path ����������,�������� "openssl version" ����ȷ��ӡ�汾��Ϣ.
7. �� VS ��Ŀ������ OpenSSL
	��� C:\Program Files (x86)\OpenSSL\include �� Visual Studio ��Ŀ�İ���Ŀ¼
	��� C:\Program Files (x86)\OpenSSL\lib �� Visual Studio ��Ŀ�Ŀ�Ŀ¼
	����� libcrypto.lib, libssl.lib ����