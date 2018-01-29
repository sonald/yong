#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "ltypes.h"
#include "lmem.h"
#include "lstring.h"
#include "ltricky.h"

char *l_sprintf(const char *fmt,...)
{
	va_list ap;
	int len;
	char *ret;
	va_start(ap,fmt);
#if 1
	len=vasprintf(&ret,fmt,ap);
	if(len<0) ret=NULL;
#else
	len=vsnprintf(NULL,0,fmt,ap);
	va_end(ap);
	ret=l_alloc(len+1);
	va_start(ap,fmt);
	vsprintf(ret,fmt,ap);
	
#endif
	va_end(ap);
	return ret;
}

char **l_strsplit(const char *str,int delimiter)
{
	char *list[512];
	int count=0;
	int i;
	for(i=0;i<511;i++)
	{
		char *p=strchr(str,delimiter);
		if(!p)
		{
			list[count++]=l_strdup(str);
			break;
		}
		size_t len=p-str;
		list[count++]=l_strndup(str,len);
		str=p+1;
	}
	list[count++]=0;
	return l_memdup(list,sizeof(char*)*count);
}

void l_strfreev(char **list)
{
	int i;
	if(!list) return;
	for(i=0;list[i]!=NULL;i++)
	{
		l_free(list[i]);
	}
	l_free(list);
}

#ifndef EMSCRIPTEN
#if defined(_WIN32) || (!defined(__GLIBC__)/* && !defined(__BIONIC__)*/)
/*static */char *stpcpy(char *dest,const char *src)
{
	do *dest++=*src;while(*src++!='\0');
	return dest-1;
}
#endif
#endif

char *l_strjoinv(const char *sep,char **list)
{
	int i,len;
	char *string,*ptr;
	if(!list || !list[0])
		return l_strdup("");
	if(!sep) sep="";
	for(i=len=0;list[i]!=NULL;i++)
		len+=strlen(list[i]);
	if(i>0) len+=(i-1)*strlen(sep);
	len++;
	string=l_alloc(len);
	ptr=stpcpy(string,*list);
	for(i=1;list[i]!=NULL;i++)
	{
		ptr=stpcpy(ptr,sep);
		ptr=stpcpy(ptr,list[i]);
	}
	return string;
}

int l_strv_length(char **list)
{
	int i;
	if(!list) return 0;
	for(i=0;list[i]!=NULL;i++);
	return i;
}

LString *l_string_new(int size)
{
	LString *string;
	string=l_new0(LString);
	if(size>0)
	{
		string->str=l_alloc(size);
		string->size=size;
	}
	return string;
}

void l_string_free(LString *string)
{
	if(!string) return;
	l_free(string->str);
	l_free(string);
}

static void l_string_expand(LString *string,int len)
{
	int v1,v2;
	if(string->size-string->len>=len+1)
		return;
	v1=string->len+len+1;
	v2=string->size*5/4;
	string->size=v1>v2?v1:v2;
	string->str=l_realloc(string->str,string->size);
}

void l_string_append(LString *string,const char *val,int len)
{
	if(len<0) len=strlen(val);
	l_string_expand(string,len);
	if(val)
	{
		memcpy(string->str+string->len,val,len);
		string->str[string->len+len]=0;
	}
	string->len+=len;
}

void l_string_append_c(LString *string,int c)
{
	l_string_expand(string,1);
	string->str[string->len++]=c;
	string->str[string->len]=0;
}

unsigned l_str_hash (const void *v)
{
	const signed char *p = v;
	unsigned h = *p;

	if (h) for (p += 1; *p != '\0'; p++)
		h = (h << 5) - h + *p;

	return h;
}


void l_strup(char *s)
{
	register int c;
	while((c=*s)!='\0')
	{
		if(c>='a' && c<='z')
			*s='A'+c-'a';
		s++;
	}
}

#include <dlfcn.h>
static int (*p_vsscanf)(const char *buf,const char *fmt,va_list ap);
int l_sscanf(const char * buf, const char * fmt, ...)
{
	va_list args;
	int i;
	
	va_start(args,fmt);
	if(!p_vsscanf)
	{
		p_vsscanf=dlsym(0,"vsscanf");
#ifndef __GLIBC__
		if(!p_vsscanf) p_vsscanf=vsscanf;
#endif
	}
	i=p_vsscanf(buf,fmt,args);
	va_end(args);
	return i;
}

bool l_str_has_prefix(const char *str,const char *prefix)
{
	int len1=strlen(str),len2=strlen(prefix);
	if(len1<len2) return false;
	return memcmp(str,prefix,len2)?false:true;
}

bool l_str_has_suffix(const char *str,const char *suffix)
{
	int len1=strlen(str),len2=strlen(suffix);
	if(len1<len2) return false;
	return memcmp(str+len1-len2,suffix,len2)?false:true;
}

char *l_str_trim_left(char *str)
{
	int i;
	for(i=0;str[i]!=0;i++)
	{
		char c=str[i];
		if((c&0x80) || !isspace(c))
			break;
	}
	if(i!=0)
		memmove(str,str+i,strlen(str+i)+1);
	return str;
}

char *l_str_trim_right(char *str)
{
	int len=strlen(str);
	for(len--;len>=0;len--)
	{
		char c=str[len];
		if((c&0x80) || !isspace(c))
			break;
		str[len]=0;
	}
	return str;
}

char *l_str_trim(char *str)
{
	return l_str_trim_left(l_str_trim_right(str));
}
