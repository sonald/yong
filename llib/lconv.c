#include "ltypes.h"
#include "lconv.h"
#include "lmem.h"

#ifdef USE_SYSTEM_ICONV

#include <iconv.h>

static iconv_t gb_utf8=(iconv_t)-1;
static iconv_t gb_utf16=(iconv_t)-1;
static iconv_t utf16_gb=(iconv_t)-1;
static iconv_t utf8_gb=(iconv_t)-1;

char *l_gb_to_utf8(const char *s,char *out,int size)
{
	size_t l1=strlen(s),l2=size-1;
	char *inbuf=(char*)s,*outbuf=(char*)out;

	if(gb_utf8==(iconv_t)-1)
	{
		gb_utf8=iconv_open("UTF8","GB18030");
		if(gb_utf8==(iconv_t)-1)
		{
			gb_utf8=iconv_open("UTF8","GBK");
			if(gb_utf8==(iconv_t)-1)
				return 0;
		}
	}
	
	iconv(gb_utf8,&inbuf,&l1,&outbuf,&l2);
	outbuf[0]=0;
	
	return out;
}

void *l_gb_to_utf16(const char *s,void *out,int size)
{
	size_t l1=strlen(s),l2=size-2;
	char *inbuf=(char*)s,*outbuf=(char*)out;
	
	if(gb_utf16==(iconv_t)-1)
	{
		gb_utf16=iconv_open("UTF16","GB18030");
		if(gb_utf16==(iconv_t)-1)
		{
			gb_utf16=iconv_open("UTF16","GBK");
			if(gb_utf16==(iconv_t)-1)
				return 0;
		}
	}
	iconv(gb_utf16,&inbuf,&l1,&outbuf,&l2);
	outbuf[0]=outbuf[1]=0;
	
	return out;
}

char *l_utf8_to_gb(const char *s,char *out,int size)
{
	size_t l1=strlen(s),l2=size-1;
	char *inbuf=(char*)s,*outbuf=(char*)out;
	
	if(utf8_gb==(iconv_t)-1)
	{
		utf8_gb=iconv_open("GB18030","UTF8");
		if(utf8_gb==(iconv_t)-1)
		{
			utf8_gb=iconv_open("GBK","UTF8");
			if(utf8_gb==(iconv_t)-1)
				return 0;
		}
	}
	iconv(utf8_gb,&inbuf,&l1,&outbuf,&l2);
	outbuf[0]=0;
	return out;
}

static int utf16_size(const void *s)
{
	const uint16_t *p=s;
	int i;
	for(i=0;p[i]!=0;i++);
	return i<<1;
}

char *l_utf16_to_gb(const void *s,char *out,int size)
{
	size_t l1=utf16_size(s),l2=size-1;
	char *inbuf=(char*)s,*outbuf=(char*)out;
	if(utf16_gb==(iconv_t)-1)
	{
		utf16_gb=iconv_open("GB18030","UTF16");
		if(utf16_gb==(iconv_t)-1)
		{
			utf16_gb=iconv_open("GBK","UTF16");
			if(utf16_gb==(iconv_t)-1)
				return 0;
		}
	}
	iconv(utf16_gb,&inbuf,&l1,&outbuf,&l2);
	outbuf[0]=0;
	return out;
}


#endif
