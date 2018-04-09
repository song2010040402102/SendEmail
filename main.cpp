#pragma warning(disable:4018)

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sys/stat.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_REC 1000 //最大收件人数
#define MAX_SUB 1024 //主题最大字节数
#define MAX_TXT 500*1024 //正文最大字节数
#define MAX_ATT 100 //附件最大数量

//Email信息结构
typedef struct _email_info
{
	char server[64]; //SMTP服务器地址
	int nPort; //端口，默认为25
	char username[64]; //用户名
	char password[64]; //密码
	char sender[64]; //发件人，默认为当前登录用户
	char receiver[MAX_REC][64]; //收件人，第一个为收件人，其它为抄送人
	char subject[MAX_SUB]; //主题
	char text[MAX_TXT]; //正文
	char attachment[MAX_ATT][MAX_PATH]; //附件

	_email_info()
	{
		memset(this, 0, sizeof(_email_info));
		nPort = 25; //非加密端口，加密可采用465/994端口
	}

}EMAIL_INFO, *PEMAIL_INFO;

//发送电子邮件，成功则返回0，失败则返回对应的错误代码
int SendEmail(EMAIL_INFO* pEmailInfo);

//建立SMTP连接
bool ConnectSMTP(SOCKET &sock, char *pServIP, int nPort);

//登录邮箱
bool LoginSMTP(SOCKET &sock, char *pUsername, char *pPasswd);

//发送邮件头
bool SendSMTPHead(SOCKET &sock, char *pSender, char (*pReceiver)[64], char *pSubject);

//发送邮件正文
bool SendSMTPBody(SOCKET &sock, char *pText, char (*pAttach)[MAX_PATH] = NULL);

//结束邮件并关闭连接
bool EndSMTP(SOCKET &sock);

//接受SMTP服务器响应
int GetSMTPResponse(SOCKET &sock);

//base64编码
void EncodeBinary2String(const void *src, int lenSrc, char* &res, int &lenRes);

//对附件base64编码
char* Base64EncodeAttachment(char *pPath);

//判断文件是否存在
bool IsExistForFile(char *pPath);

//根据文件路径获取文件名
void GetFilenameByPath(char *pPath, char *pFilename);

//从标准输入控制台获取邮件参数
void GetEmailParaFromStdin(EMAIL_INFO* pEmailInfo);

//从配置文件获取邮件参数
void GetEmailParaFromCfg(EMAIL_INFO* pEmailInfo, char *pCfgFile);

//数据解析
void ParseData(char *pSrc, char *pDst, char *pSplit, int nX, int nY);

int main(int argc, char* argv[])
{	
	EMAIL_INFO emailInfo;	
	if(argc == 1)
	{
		//提示用户输入参数
		GetEmailParaFromStdin(&emailInfo);
	}
	else
	{
		//从配置文件获取参数
		GetEmailParaFromCfg(&emailInfo, argv[1]);
	}

	SendEmail(&emailInfo);

	return 0;
}

int SendEmail(EMAIL_INFO* pEmailInfo)
{
	if(!pEmailInfo)
		return -1;

	//建立连接
	SOCKET sock; 
	if(!ConnectSMTP(sock, pEmailInfo->server, pEmailInfo->nPort))
	{		
		return 1;
	}

	//登录邮箱
	if(!LoginSMTP(sock, pEmailInfo->username, pEmailInfo->password))
	{
		return 2;
	}

	//发送邮件头
	if(strlen(pEmailInfo->sender) <= 0)
	{
		//当前登录用户作为默认发送者
		strcpy(pEmailInfo->sender, pEmailInfo->username);
	}
	if(!SendSMTPHead(sock, pEmailInfo->sender, pEmailInfo->receiver, pEmailInfo->subject))
    {  
        return 3;  
    }  

	//发送邮件正文
	if(!SendSMTPBody(sock, pEmailInfo->text, pEmailInfo->attachment))
	{
		return 4;
	}

	//结束邮件并关闭连接
	if(!EndSMTP(sock))
    {  
        return false;  
    }  
	return 0;
}

bool ConnectSMTP(SOCKET &sock, char *pServIP, int nPort)  
{   
	if(!pServIP)
		return false;

	printf("Trying to connect smtp %s:%d\n", pServIP, nPort);

    WSADATA wsaData;          
    int ret = WSAStartup(MAKEWORD(2,2), &wsaData);  
    if(ret != 0)   
    {  
        return false;  
    }  
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)  
    {  
        WSACleanup();  
        return false;   
    }  

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);  
    if(sock == INVALID_SOCKET)  
    {  
        return false;  
    }  
  
    sockaddr_in servaddr;  
    memset(&servaddr, 0, sizeof(sockaddr_in));  
    servaddr.sin_family = AF_INET;  
    servaddr.sin_port = htons(nPort);
    struct hostent *hp = gethostbyname(pServIP);
	if(!hp)
	{		
		return false;
	}
    servaddr.sin_addr.s_addr = *(int*)(*hp->h_addr_list);   
  
  
    ret = connect(sock, (sockaddr*)&servaddr, sizeof(servaddr));
    if(ret == SOCKET_ERROR)  
    {  	
        return false;  
    }  	
	GetSMTPResponse(sock);	
    return true;  
}  

bool LoginSMTP(SOCKET &sock, char *pUsername, char *pPasswd)
{
	if(!pUsername || !pPasswd)
		return false;

	printf("Trying to login smtp with username: %s password: %s\n", pUsername, pPasswd);

	char sendBuf[4096];
	memset(sendBuf, 0, sizeof(sendBuf));  
    sprintf(sendBuf, "HELO []\r\n");  
    send(sock, sendBuf, strlen(sendBuf), 0);
    int code = GetSMTPResponse(sock);
    if(code != 250)  
    {  
        return false;  
    }  
  
    memset(sendBuf, 0, sizeof(sendBuf));  
    sprintf(sendBuf, "AUTH LOGIN\r\n");  
    send(sock, sendBuf, strlen(sendBuf), 0);
    code = GetSMTPResponse(sock);
    if(code != 334)  
    {  
        return false;  
    } 
  
    memset(sendBuf, 0, sizeof(sendBuf)); 
	char *pRes = NULL;
	int nLenRes = 0;
	EncodeBinary2String(pUsername, strlen(pUsername), pRes, nLenRes);
	if(pRes)
	{
		memcpy(sendBuf, pRes, nLenRes);
		delete []pRes, pRes = NULL;

		sendBuf[strlen(sendBuf)] = '\r';  
		sendBuf[strlen(sendBuf)] = '\n';  
		send(sock, sendBuf, strlen(sendBuf), 0);
		code = GetSMTPResponse(sock);
		if(code != 334)  
		{  
			return false;  
		}  

		memset(sendBuf, 0, sizeof(sendBuf));
		EncodeBinary2String(pPasswd, strlen(pPasswd), pRes, nLenRes);
		if(pRes)
		{
			memcpy(sendBuf, pRes, nLenRes);
			delete []pRes, pRes = NULL;

			sendBuf[strlen(sendBuf)] = '\r';  
			sendBuf[strlen(sendBuf)] = '\n';  
			send(sock, sendBuf, strlen(sendBuf), 0);
			code = GetSMTPResponse(sock);
			if(code != 235)  
			{  
				return false;  
			}  	
		}		
		else
		{
			return false;
		}
	}       
    else
	{
		return false;
	}	

    return true;
}

bool SendSMTPHead(SOCKET &sock, char *pSender, char (*pReceiver)[64], char *pSubject)  
{      
	if(!pSender || !pReceiver || !pSubject)
		return false;

	printf("Trying to send smtp head\n");

	char sendBuf[4096];
    memset(sendBuf, 0, sizeof(sendBuf));  
    sprintf(sendBuf, "MAIL FROM:<%s>\r\n", pSender);  
    int ret = send(sock, sendBuf, strlen(sendBuf), 0);        
    if(ret!=strlen(sendBuf))  
    {  
        return false;  
    }  
    GetSMTPResponse(sock);
  
	char recList[MAX_REC*64] = {0};
	for(int i = 0; i < MAX_REC; i++)
	{
		char *pRec = pReceiver[i];
		if(!pRec || strlen(pRec) <= 0)
			break;		

		if(i == 0)
		{			
			//收件人
			sprintf(&recList[strlen(recList)], "To: %s<%s>\r\n", pRec, pRec); 	
		}
		else
		{
			//抄送人
			sprintf(&recList[strlen(recList)], "Cc: %s<%s>\r\n", pRec, pRec); 	
		}

		memset(sendBuf, 0, sizeof(sendBuf));  
		sprintf(sendBuf, "RCPT TO:<%s>\r\n", pRec);  
		ret = send(sock, sendBuf, strlen(sendBuf), 0);  
		if(ret!=strlen(sendBuf))  
		{  
			return false;  
		}  
		GetSMTPResponse(sock);
	}    
  
    memset(sendBuf, 0, sizeof(sendBuf));  
    sprintf(sendBuf, "DATA\r\n");  
    ret = send(sock, sendBuf, strlen(sendBuf), 0);  
    if(ret!=strlen(sendBuf))  
    {  
        return false;  
    }  
    GetSMTPResponse(sock);
  
    memset(sendBuf, 0, sizeof(sendBuf));  
    sprintf(sendBuf, "From: %s<%s>\r\n", pSender, pSender);  
    sprintf(&sendBuf[strlen(sendBuf)], "%s", recList);  
    sprintf(&sendBuf[strlen(sendBuf)], "Subject: %s\r\n", pSubject);
	sprintf(&sendBuf[strlen(sendBuf)], "Mime-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=\"INVT\"\r\n\r\n");  
    ret = send(sock, sendBuf, strlen(sendBuf), 0);  
    if(ret!=strlen(sendBuf))  
    {  
        return false;  
    }   	
    return true;  
}  

bool SendSMTPBody(SOCKET &sock, char *pText, char (*pAttach)[MAX_PATH])
{       
	if(!pText || !pAttach)
		return false;

	printf("Trying to send smtp text\n");

	char sendBuf[4096] = {0};    

	//发送正文
    sprintf(sendBuf, "--INVT\r\nContent-Type: text/plain; charset=\"gb2312\"\r\n\r\n%s\r\n", pText);  
    int ret = send(sock, sendBuf, strlen(sendBuf), 0);  
    if(ret!=strlen(sendBuf))  
    {  
        return false;  
    }  
    	
	printf("Trying to send smtp attachments\n");

	//发送附件
	for(int i = 0; i < MAX_ATT; i++)
    {    
		char *pAtt = pAttach[i];
		if(!pAtt || strlen(pAtt) <= 0)
			break;

		if(!IsExistForFile(pAtt))
			continue;

		//获取附件的文件名
		char filename[MAX_PATH] = {0};
		GetFilenameByPath(pAtt, filename);

		memset(sendBuf, 0, sizeof(sendBuf));
		sprintf(&sendBuf[strlen(sendBuf)], "\r\n--INVT\r\n");
		sprintf(&sendBuf[strlen(sendBuf)], "Content-Type: application/octet-stream\r\n");
		sprintf(&sendBuf[strlen(sendBuf)], "Content-Transfer-Encoding: base64\r\n");
		sprintf(&sendBuf[strlen(sendBuf)], "Content-Disposition: attachment; filename=\"%s\"\r\n\r\n", filename);		
		ret = send(sock, sendBuf, strlen(sendBuf), 0);
		if(ret!=strlen(sendBuf))  
		{  
			break;
		}
		        
        char *pBuffer = Base64EncodeAttachment(pAtt);       
		if(pBuffer)
		{
			send(sock, pBuffer, strlen(pBuffer), 0);		
			delete []pBuffer;
		}	
    }

	return true;
}  

bool EndSMTP(SOCKET &sock)  
{  
	char sendBuf[4096];
    memset(sendBuf, 0, sizeof(sendBuf));  
    sprintf(sendBuf, "\r\n--INVT--\r\n.\r\n");  
    send(sock, sendBuf, strlen(sendBuf), 0);  
  
    sprintf(sendBuf, "QUIT\r\n");  
    send(sock, sendBuf, strlen(sendBuf), 0);  

    closesocket(sock);  
    WSACleanup();  

	printf("Send e-mail success\n");

    return true;  
}  

int GetSMTPResponse(SOCKET &sock)
{
	char response[1024] = {0};
	recv(sock, response, 1024, 0);
	if(strlen(response) <= 0)
	{
		return -1;
	}
	else
	{
		printf("smtp response: %s\n", response);

		//截取response的前三位作为响应码
		char code[4] = {0};
		memcpy(code, response, 3);
		return atoi(code);
	}
	return 0;
}

void EncodeBinary2String(const void *src, int lenSrc, char* &res, int &lenRes)
{
	const char base[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="; 

	int cp = (lenSrc%3==0? 0: 3-(lenSrc%3));
	
	lenSrc += cp;
	lenRes = lenSrc*4/3;
	
	unsigned char* newSrc = new unsigned char[lenSrc];
	
	memcpy(newSrc, src, lenSrc);
	int i;
	for(i =0; i< cp; i++)
		newSrc[lenSrc-1-i] = 0;
	
	res = new char[lenRes+1];
	
	for(i= 0; i< lenSrc; i+=3)
	{		
		res[i/3*4] = base[newSrc[i]>>2];
		res[i/3*4 + 1] = base[((newSrc[i]&3) <<4) + (newSrc[i+1]>>4)];
		res[i/3*4 + 2] = base[((newSrc[i+1]&15) <<2) + (newSrc[i+2]>>6)];
		res[i/3*4 + 3] = base[(newSrc[i+2]&63)];
	}
	
	for(i = 0; i< cp; i++)
		res[lenRes-1-i] = '=';
	res[lenRes] = '\0';
	
	delete []newSrc;
	return;
}

char* Base64EncodeAttachment(char *pPath)
{
	if(!pPath)
		return NULL;
	
	FILE *pFile = fopen(pPath, "rb");
	if(!pFile)
		return NULL;

	char *pEnBuffer = NULL;
	fseek(pFile, 0, SEEK_END);
	int nFileSize = ftell(pFile);
	if(nFileSize > 0)		
	{
		fseek(pFile, 0, SEEK_SET);
		const int nSplit = 128;
		const char *pSplit = "\r\n";
		int nLenEnBuf = nFileSize*4/3 + nFileSize/nSplit+ nFileSize*strlen(pSplit)/nSplit + 64;
		pEnBuffer = new char[nLenEnBuf];
		memset(pEnBuffer, 0, nLenEnBuf);
		while(!feof(pFile))
		{
			char buf[nSplit] = {0};			
			int nRead = fread(buf, 1, nSplit, pFile);
			if(nRead == 0)
				break;

			char *pRes = NULL;
			int nLenRes = 0;
			EncodeBinary2String(buf, nRead, pRes, nLenRes);
			if(pRes)
			{
				sprintf(&pEnBuffer[strlen(pEnBuffer)], "%s\r\n", pRes);	
				delete []pRes, pRes = NULL;
			}							
		}
	}
	fclose(pFile);
	return pEnBuffer;
}

bool IsExistForFile(char *pPath)
{
	struct _stat st;  
    int nResult = _stat(pPath, &st);
	return nResult==0;	
}

void GetFilenameByPath(char *pPath, char *pFilename)
{
	if(!pPath || !pFilename)
		return;

	char name[_MAX_FNAME] = {0}, ext[_MAX_EXT] = {0};
	_splitpath(pPath, NULL, NULL, name, ext);
	sprintf(pFilename, "%s%s", name, ext);
}

void GetEmailParaFromStdin(EMAIL_INFO* pEmailInfo)
{		
	if(!pEmailInfo)
		return;

	printf("请输入smtp服务器: ");
	gets(pEmailInfo->server);

	printf("请输入用户名: ");
	gets(pEmailInfo->username);

	printf("请输入密码: ");
	gets(pEmailInfo->password);

	printf("请输入收件人(多个用;隔开): ");
	char rec[MAX_REC*65] = {0};
	gets(rec);
	ParseData(rec, pEmailInfo->receiver[0], ";", MAX_REC, 64);

	printf("请输入主题: ");
	gets(pEmailInfo->subject);

	printf("请输入正文: ");
	gets(pEmailInfo->text);

	printf("请输入附件(多个用;隔开): ");
	char att[MAX_ATT*MAX_PATH] = {0};
	gets(att);
	ParseData(att, pEmailInfo->attachment[0], ";", MAX_ATT, MAX_PATH);

	printf("\n*******************************************************\n\n");
	printf("smtp: %s\nusername: %s\npassword: %s\n", pEmailInfo->server, pEmailInfo->username, pEmailInfo->password);
	for(int i = 0; i < MAX_REC; i++)
	{
		if(strlen(pEmailInfo->receiver[i]) == 0)
			break;
		printf("receiver[%d]: %s\n", i, pEmailInfo->receiver[i]);
	}	
	printf("subject: %s\ntext: %s\n", pEmailInfo->subject, pEmailInfo->text);
	for(i = 0; i < MAX_ATT; i++)
	{
		if(strlen(pEmailInfo->attachment[i]) == 0)
			break;
		printf("attachment[%d]: %s\n", i, pEmailInfo->attachment[i]);
	}	
	printf("\n*******************************************************\n");
	printf("是否确认发送此邮件(Y/N)?");
	char yn[16] = {0};
	gets(yn);
	if(yn[0] != 'y' && yn[0] != 'Y')
	{
		exit(0);
	}
}

void GetEmailParaFromCfg(EMAIL_INFO* pEmailInfo, char *pCfgFile)
{
	if(!pEmailInfo || !pCfgFile)
		return;

	FILE *pFile = fopen(pCfgFile, "r");
	if(!pFile)
		return;

	fgets(pEmailInfo->server, sizeof(pEmailInfo->server), pFile), pEmailInfo->server[strlen(pEmailInfo->server)-1] = '\0';
	fgets(pEmailInfo->username, sizeof(pEmailInfo->username), pFile), pEmailInfo->username[strlen(pEmailInfo->username)-1] = '\0';
	fgets(pEmailInfo->password, sizeof(pEmailInfo->password), pFile), pEmailInfo->password[strlen(pEmailInfo->password)-1] = '\0';

	char rec[MAX_REC*65] = {0};
	fgets(rec, sizeof(rec), pFile), rec[strlen(rec)-1] = '\0';
	ParseData(rec, pEmailInfo->receiver[0], ";", MAX_REC, 64);

	fgets(pEmailInfo->subject, sizeof(pEmailInfo->subject), pFile), pEmailInfo->subject[strlen(pEmailInfo->subject)-1] = '\0';
	fgets(pEmailInfo->text, sizeof(pEmailInfo->text), pFile), pEmailInfo->text[strlen(pEmailInfo->text)-1] = '\0';

	char att[MAX_ATT*MAX_PATH] = {0};
	fgets(att, sizeof(att), pFile), att[strlen(att)-1] = '\0';
	ParseData(att, pEmailInfo->attachment[0], ";", MAX_ATT, MAX_PATH);

	fclose(pFile);
}

void ParseData(char *pSrc, char *pDst, char *pSplit, int nX, int nY)
{
	if(!pSrc || !pDst || !pSplit || nX<=0 || nY<=0)
		return;
	
	int nLenSrc = strlen(pSrc);
	int nLenSplit = strlen(pSplit);
	if(nLenSrc <= 0 || nLenSplit <= 0)
		return;

	memset(pDst, 0, nX*nY);	
	char *p1 = pSrc, *p2 = NULL;
	for(int i = 0; i < nX; i++)
	{
		p2 = strstr(p1, pSplit);
		if(!p2)
		{
			strcpy(pDst+i*nY, p1);
			break;
		}
		else
		{
			memcpy(pDst+i*nY, p1, p2-p1);
			if(p2-p1+1 == nLenSrc)
				break;
		}
		p1 = p2+nLenSplit;		
	}
}