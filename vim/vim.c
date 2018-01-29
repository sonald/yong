
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct{
	uint16_t magic;
	uint16_t seq;
	uint16_t len;
	uint16_t flag;
	char method[8];
	uint32_t data[2];
}LCallMsg;

static void l_call_build_path(char *path)
{
	char *p;
	p=getenv("DISPLAY");
	sprintf(path,"/tmp/yong-%s",p);
	p=strchr(path,'.');
	if(p) *p=0;
}

int l_call_connect(void)
{
	struct sockaddr_un sa;
	int s;
	struct timeval timeo;
	
	timeo.tv_sec=0;
	timeo.tv_usec=500*1000;

	s=socket(AF_UNIX,SOCK_STREAM,0);
	memset(&sa,0,sizeof(sa));
	sa.sun_family=AF_UNIX;
	l_call_build_path(sa.sun_path);
	
	if(0!=connect(s,(struct sockaddr*)&sa,sizeof(sa)))
	{
		perror("connect");
		close(s);
		return -1;
	}
	setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&timeo,sizeof(timeo));
	setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&timeo,sizeof(timeo));
	return s;
}

static LCallMsg tool_buf={
	0x4321,0,sizeof(LCallMsg),0,"tool",{1,0}
};

int main(int arc,char *arg[])
{
	int s;
	int ret;
	int i;
	int res=0;
	
	for(i=1;i<arc;i++)
	{
		if(!strcmp(arg[i],"-w"))
			tool_buf.flag=1;
		else if(!strcmp(arg[i],"-t") && i<arc-1)
			tool_buf.data[0]=atoi(arg[++i]);
		else
			tool_buf.data[1]=atoi(arg[i]);
	}
	s=l_call_connect();
	ret=write(s,&tool_buf,sizeof(tool_buf));
	if(ret<=0)
	{
		close(s);
		return 0;
	}
	if(tool_buf.flag!=0)
	{
		ret=read(s,&tool_buf,sizeof(tool_buf));
		if(ret>=sizeof(tool_buf)-4)
		{
			res=tool_buf.data[0];
		}
		else
		{
		}
	}
	printf("%d\n",res);
	return 0;
}
