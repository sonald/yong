#include <stdio.h>
#include <stdarg.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "ltypes.h"
#include "lmem.h"
#include "lconv.h"
#include "lstring.h"
#include "lfile.h"
#include "ltricky.h"

#include <unistd.h>
#include <ctype.h>

struct _ldir{
	DIR *dirp;
};

int l_zip_goto_file(FILE *fp,const char *name);
char *l_zip_file_get_contents(FILE *fp,const char *name,size_t *length);

static char *file_is_in_zip(const char *file)
{
	const char *p=file;
	while((p=strchr(p,'.'))!=NULL)
	{
		p++;
		if(!islower(p[0]) || !islower(p[1]) || !islower(p[2]) || p[3]!='/')
			continue;
		return (char*)p+4;
	}
	return NULL;
}

FILE *l_file_vopen(const char *file,const char *mode,va_list ap,size_t *size)
{
	FILE *fp;
	char *path;
	const char *zfile=NULL;
	int check=0;
	
	if(!strchr(mode,'w') && (zfile=file_is_in_zip(file))!=NULL)
	{
		char *tmp;
		size_t len;
		len=zfile-file;
		tmp=l_alloca(len);
		memcpy(tmp,file,len-1);
		tmp[len-1]=0;
		file=tmp;
		mode="rb";
	}

	do
	{
		path=va_arg(ap,char*);
		if(path)
		{
			int zero_zfile=0;
			path=l_sprintf("%s/%s",path,file);
			if(zfile && l_file_is_dir(path))
			{
				char *tmp=l_sprintf("%s/%s",path,zfile);
				l_free(path);
				path=tmp;
				zero_zfile=1;
			}
			fp=fopen(path,mode);
			l_free(path);
			if(fp && zero_zfile)
				zfile=NULL;
			check++;
		}
		else
		{
			if(check>0)
			{
				break;
			}
			int free_path=0;
			if(zfile && l_file_is_dir(file))
			{
				path=l_sprintf("%s/%s",file,zfile);
				free_path=1;
			}
			else
			{
				path=(char*)file;
			}
			fp=fopen(path,mode);
			if(free_path)
			{
				l_free(path);
				if(fp) zfile=NULL;
			}
			break;
		}
	}while(!fp && path);
	if(fp && zfile)
	{
		int ret=l_zip_goto_file(fp,zfile);
		if(ret<0)
		{
			fclose(fp);
			return NULL;
		}
		if(size) *size=ret;
	}
	else if(fp && size)
	{
		struct stat st;
		fstat(fileno(fp),&st);
		*size=st.st_size;
	}
	return fp;
}

FILE *l_file_open(const char *file,const char *mode,...)
{
	FILE *fp;
	va_list ap;
	va_start(ap,mode);
	fp=l_file_vopen(file,mode,ap,NULL);
	va_end(ap);
	return fp;
}

#if 0
char *l_file_vget_contents(const char *file,size_t *length,va_list ap)
{
	FILE *fp;
	char *contents;
	size_t size;
	const char *zfile;
	
	zfile=file_is_in_zip(file);
	if(zfile!=NULL)
	{
		char temp[256];
		size=zfile-file-1;
		memcpy(temp,file,size);
		temp[size]=0;
		fp=l_file_vopen(temp,"rb",ap,NULL);
		if(!fp) return NULL;
		contents=l_zip_file_get_contents(fp,zfile,length);
		fclose(fp);
		return contents;
	}
	else
	{
		fp=l_file_vopen(file,"rb",ap,&size);
		if(!fp) return NULL;
		contents=l_alloc(size+1);
		fread(contents,size,1,fp);
		fclose(fp);
		contents[size]=0;
		if(length) *length=size;
		return contents;
	}
}
#else
char *l_file_vget_contents(const char *file,size_t *length,va_list ap)
{
	FILE *fp;
	char *path;
	char *zfile=NULL;
	char *res;
	do
	{
		char temp[256];
		path=va_arg(ap,char*);
		if(path)
		{
			sprintf(temp,"%s/%s",path,file);
		}
		else
		{
			strcpy(temp,file);
		}
		fp=fopen(temp,"rb");
		if(fp!=NULL)
		{
			struct stat st;
			fstat(fileno(fp),&st);
			if(st.st_size>1024*1024*1024)
			{
				fclose(fp);
				return NULL;
			}
			if(length) *length=st.st_size;
			res=l_alloc((st.st_size+1+15)&~0x0f);
			fread(res,1,st.st_size,fp);
			res[st.st_size]=0;
			fclose(fp);
			break;
		}
		zfile=file_is_in_zip(temp);
		if(!zfile) continue;
		zfile[-1]=0;
		fp=fopen(temp,"rb");
		if(fp!=NULL)
		{
			res=l_zip_file_get_contents(fp,zfile,length);
			fclose(fp);
			if(res!=NULL)
				break;
		}
	}while(path);
	return res;
}

#endif
char *l_file_get_contents(const char *file,size_t *length,...)
{
	char *contents;
	va_list ap;
	va_start(ap,length);
	contents=l_file_vget_contents(file,length,ap);
	va_end(ap);
	return contents;
}
/*
char *l_file_get_contents(const char *file,size_t *length,...)
{
	FILE *fp;
	char *contents;
	va_list ap;
	size_t size;
	va_start(ap,length);
	fp=l_file_vopen(file,"rb",ap,&size);
	va_end(ap);
	if(!fp) return NULL;
	contents=l_alloc(size+1);
	fread(contents,size,1,fp);
	fclose(fp);
	contents[size]=0;
	if(length) *length=size;
	return contents;
}
*/

int l_file_set_contents(const char *file,const void *contents,size_t length,...)
{
	FILE *fp;
	va_list ap;
	va_start(ap,length);
	fp=l_file_vopen(file,"wb",ap,NULL);
	va_end(ap);
	if(!fp) return -1;
	fwrite(contents,length,1,fp);
	fclose(fp);
	return 0;
}

LDir *l_dir_open(const char *path)
{
	LDir *dir=l_new(LDir);
	dir->dirp=opendir(path);
	if(!dir->dirp)
	{
		l_free(dir);
		return NULL;
	}
	return dir;
}

void l_dir_close(LDir *dir)
{
	closedir(dir->dirp);
	l_free(dir);
}

const char *l_dir_read_name(LDir *dir)
{
	struct dirent *entry;
	do{
		entry=readdir(dir->dirp);
	}while(entry && entry->d_name[0]=='.');
	if(!entry) return NULL;
	return entry->d_name;
	return 0;
}

bool l_file_is_dir(const char *path)
{
	struct stat st;
	if(stat(path,&st))
		return false;
	return S_ISDIR(st.st_mode);
}

bool l_file_exists(const char *path)
{
	return !access(path,F_OK);
}

int l_file_copy(const char *dst,const char *src,...)
{
	int ret;
	char temp[1024];
	FILE *fds,*fdd;
	va_list ap;
	va_start(ap,src);
	fds=l_file_vopen(src,"rb",ap,NULL);
	if(!fds)
	{
		va_end(ap);
		return -1;
	}
	fdd=l_file_vopen(dst,"wb",ap,NULL);
	va_end(ap);
	if(!fdd)
	{
		fclose(fds);
		return -1;
	}
	while(1)
	{
		ret=fread(temp,1,sizeof(temp),fds);
		if(ret<=0) break;
		fwrite(temp,1,ret,fdd);
	}
	fclose(fds);
	fclose(fdd);
	return 0;
}

int l_get_line(char *line, size_t n, FILE *fp)
{
	int len;

	if(!fgets(line,n,fp))
		return -1;
	len=strcspn(line,"\r\n");
	line[len]=0;

	/* deal last line in zip file */
	if(len>=4 && ((uint8_t*)line)[2]<9)
		return -1;

	return len;
}

int l_mkdir(const char *name,int mode)
{
	return mkdir(name,mode);
}

int l_remove(const char *name)
{
	remove(name);
	return 0;
}

int l_rmdir(const char *name)
{
	rmdir(name);
	return 0;
}
