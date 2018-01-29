#include "common.h"
#include "yong.h"
#include "xim.h"
#include "gbk.h"
#include "im.h"
#include "ui.h"
#include "llib.h"
#include <ctype.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include "translate.h"
#include "version.h"

#ifdef CFG_XIM_IBUS
#include "xim-ibus.h"
#endif
#ifdef CFG_XIM_FBTERM
#include "fbterm.h"
#endif
#include "ybus.h"

//#define memcpy(a,b,c) memmove(a,b,c)

static LKeyFile *MainConfig,*SubConfig,*MenuConfig;

extern int key_select[];
extern char key_select_n[];
static Y_XIM xim;

int y_xim_init(const char *name)
{
	memset(&xim,0,sizeof(xim));
	if(!name || !name[0])
	{
		int y_xim_init_default(Y_XIM *x);
		y_xim_init_default(&xim);
	}
#ifdef CFG_XIM_IBUS
	else if(!strcmp(name,"ibus"))
	{
		int y_xim_init_ibus(Y_XIM *x);
		y_xim_init_ibus(&xim);
	}
#endif
#ifdef CFG_XIM_FBTERM
	else if(!strcmp(name,"fbterm"))
	{
		int y_xim_init_fbterm(Y_XIM *x);
		y_xim_init_fbterm(&xim);
	}
#endif
	if(!xim.init)
		return -1;
	else
		return xim.init();
}

char *y_xim_get_name(void)
{
	return xim.name?xim.name:"";
}

void y_xim_forward_key(int key)
{
	if(xim.forward_key)
		xim.forward_key(key);
}

void y_xim_update_config(void)
{
	if(xim.update_config)
		xim.update_config();
}

void y_xim_explore_url(const char *s)
{
	if(xim.explore_url)
		xim.explore_url(s);
}

static char last_output[512];
static char temp_output[512];

void y_xim_set_last(const char *s)
{
	if(!s || !s[0])
	{
		last_output[0]=0;
		return;
	}
	if(!strcmp(s,"$LAST"))
		return;
	strcpy(last_output,s);
}

const char *y_xim_get_last(void)
{
	return last_output;
}

int y_xim_trigger_key(int key)
{
	if(xim.trigger_key)
		return xim.trigger_key(key);
	return -1;
}

static int encrypt_clipboard_cb(const char *s)
{
	char temp[128];
	if(!s)
	{
		y_ui_show_tip(YT("û��������Ҫ����"));
		return 0;
	}
	if(0!=y_im_book_encrypt(s,temp))
	{
		y_ui_show_tip(YT("��������ʧ��"));
		return 0;
	}
	y_xim_send_string2(temp,SEND_FLUSH);
	return 0;
}

static void y_im_strip_key_useless(char *gb)
{
	char *s;
	int key=0;
	s=strrchr(gb,'$');
	if(s)
	{
		key=y_im_str_to_key(s+1);
		if(key>0) *s=0;
	}
}

void y_xim_send_string2(const char *s,int flag)
{
	int key=0;
	if(s)
	{
		if(flag&SEND_RAW)
			goto COPY;
		s+=y_im_str_desc(s,0);
		if(s[0]=='$')
		{
			if(!strcmp(s,"$LAST"))
			{
				strcat(temp_output,last_output);
			}
			else if(!strncmp(s,"$GO(",4) && l_str_has_suffix(s,")"))
			{
				y_xim_send_string2("",SEND_FLUSH);
				y_im_go_url(s);
				return;
			}
			else if(!strncmp(s,"$KEY(",5) && l_str_has_suffix(s,")"))
			{
				char temp[256];
				strcpy(temp,s+5);
				temp[strlen(temp)-1]=0;
				y_im_book_key(temp);
				y_ui_show_tip(YT("������Կ���"));
				return;
			}
			else if(!strncmp(s,"$DECRYPT(",9) && l_str_has_suffix(s,")"))
			{
				char temp[256];
				int len;
				len=strlen(temp_output);
				if(len+65>sizeof(temp_output))
					return;
				strcpy(temp,s+9);
				temp[strlen(temp)-1]=0;
				if(0!=y_im_book_decrypt(temp,temp_output+len))
				{
					y_ui_show_tip(YT("��������ʧ��"));
					return;
				}
			}
			else if(!strncmp(s,"$ENCRYPT(",9) && l_str_has_suffix(s,")"))
			{
				char temp[256];
				int len;
				len=strlen(temp_output);
				if(len+128>sizeof(temp_output))
					return;
				strcpy(temp,s+9);
				temp[strlen(temp)-1]=0;
				if(temp[0]!=0)
				{
					if(0!=y_im_book_encrypt(temp,temp_output+len))
					{
						y_ui_show_tip(YT("��������ʧ��"));
						return;
					}
				}
				else
				{
					y_ui_get_select(encrypt_clipboard_cb);
				}
			}
			else if(s[1]=='B' && s[2]=='D' && s[3]=='(' && s[4] && s[5]==')' && !s[6])
			{
				const char *temp;
				int lang=LANG_CN;
				CONNECT_ID *id=y_xim_get_connect();
				if(id) lang=id->biaodian;
				temp=YongGetPunc(s[4],lang,0);
				if(temp)
				{
					strcat(temp_output,temp);
				}					
			}
			else if(y_im_forward_key(s)==0)
			{
				// ��ǰ�Ǹ�ģ�ⰴ��������Ӧ���б���������Ϣ
				// ������ϵͳ���ﰴ�������������������Ϣ����
				// �û��ò�����ȷ������˳��
				goto out;
			}
			else
			{
				goto COPY;
			}
		}
		else if(s[0])
		{
			if(!(flag&SEND_RAW))
			{
				y_im_strip_key_useless(temp_output);
			}
COPY:
			strcat(temp_output,s);
			if(!(flag&SEND_BIAODIAN))
			{
				strcpy(last_output,temp_output);
			}
		}
	}
	else
	{
		if(strlen(temp_output)+strlen(last_output)<512)
			strcat(temp_output,last_output);
	}
	if(!(flag&SEND_FLUSH))
	{
		return;
	}

	s=temp_output;
	if(!s[0])
	{
		last_output[0]=0;
		return;
	}

	if(!(flag&SEND_RAW))
	{
		s+=y_im_str_desc(s,0);
		if(!y_im_forward_key(s))
			goto out;
		key=y_im_strip_key((char*)s);
		if(!y_im_go_url(s))
			goto out;
		if(!y_im_send_file(s))
			goto out;
#ifndef CFG_XIM_ANDROID
		if(strstr(s,"$/"))
		{
			YongSendClipboard(s);
			goto out;
		}
#endif
	}

	y_im_speed_update(0,s);
	y_im_history_write(s);
	if(xim.send_string)
	{
#ifndef CFG_NO_REPLACE
		if(key>0)
			xim.send_string(s,(flag&SEND_RAW)?DONT_ESCAPE:0);
		else
			y_replace_string(s,xim.send_string,(flag&SEND_RAW)?DONT_ESCAPE:0);
#else
		xim.send_string(s,(flag&SEND_RAW)?DONT_ESCAPE:0);
#endif
	}
	if(key>0)
	{
		int i;
		for(i=0;i<key;i++)
			y_xim_forward_key(YK_LEFT);
	}
out:
	temp_output[0]=0;
}

void y_xim_send_string(const char *s)
{
	y_xim_send_string2(s,SEND_FLUSH);
}

int y_im_get_real_cand(const char *s,char *out,size_t size)
{
	if(!s || !s[0])
	{
		out[0]=0;
		return 0;
	}
	if(!strcmp(s,"$LAST"))
	{
		s=last_output;
		s+=y_im_str_desc(s,0);
		snprintf(out,size,"%s",s);
	}
	else
	{
		s+=y_im_str_desc(s,0);
		snprintf(out,size,"%s",s);
	}
	if(!out[0])
	{
		return 0;
	}

	if(s[0]=='$' || s[0]=='\0')
		return 0;

	y_im_strip_key(out);
	return strlen(out);
}

static inline int is_mask_key(int k)
{
	return k==YK_LCTRL || k==YK_RCTRL ||
		k==YK_LSHIFT || k==YK_RSHIFT ||
		k==YK_LALT || k==YK_RALT;
}

int y_im_input_key(int key)
{
	int ret;
	int bing=key&KEYM_BING;
	int mod=key&KEYM_MASK;
	key&=~KEYM_CAPS;

	ret=YongHotKey(key);
	if(ret)
	{
		if(is_mask_key(YK_CODE(key)))
			return 0;
		return ret;
	}
	ret=YongKeyInput(key,mod);
	if(ret)
	{
		y_im_speed_update(key,0);
		if(bing && im.Bing && !im.EnglishMode)
			YongKeyInput(KEYM_BING|'+',0);
		if(is_mask_key(YK_CODE(key)))
			return 0;
	}
	return ret;
}

int y_xim_input_key(int key)
{
	if(!xim.input_key)
		return y_im_input_key(key);
	return xim.input_key(key);
}

CONNECT_ID *y_xim_get_connect(void)
{
	CONNECT_ID *id=0;
	if(xim.get_connect)
		id=xim.get_connect();
	return id;
}

void y_xim_put_connect(CONNECT_ID *id)
{
	if(xim.put_connect)
		xim.put_connect(id);
}

void y_xim_preedit_clear(void)
{
	if(xim.preedit_clear)
		xim.preedit_clear();
}

int y_im_last_key(int key)
{
	static int last;
	int ret;
	ret=last;
	last=key;
	return ret;
}

void y_xim_preedit_draw(char *s,int len)
{
	if(xim.preedit_draw)
		xim.preedit_draw(s,len);
}

void y_xim_enable(int enable)
{
	if(xim.enable)
		return xim.enable(enable);
}

Y_UI y_ui;
int y_ui_init(const char *name)
{
#ifdef CFG_XIM_FBTERM
	if(name && !strcmp(name,"fbterm"))
	{
		ui_setup_fbterm(&y_ui);
	}
	else
	{
		ui_setup_default(&y_ui);
	}
#else
	ui_setup_default(&y_ui);
#endif	
	return y_ui.init();
}

int y_im_copy_file(char *src,char *dst)
{
	int ret;
	FILE *fds,*fdd;
	char temp[1024];

	fds=y_im_open_file(src,"rb");
	if(fds==NULL)
	{
		return -1;
	}
	fdd=y_im_open_file(dst,"wb");
	if(fdd==NULL)
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

int y_im_set_exec(void)
{
	int ret;
	char *tmp;
	char data[256];
	ret=readlink("/proc/self/exe",data,256);		//linux
	if(ret<0)
		ret=readlink("/proc/curproc/file",data,256);//bsd
	if(ret<0||ret>=256)
	{
		strcpy(data,".");
		printf("yong: get self fail\n");
		return -1;
	}
	data[ret]=0;
	tmp=strrchr(data,'/');
	if(!tmp)
	{
		printf("yong: bad path\n");
		return -1;
	}
	*tmp=0;
	//printf("yong: change to dir %s\n",data);
	if(chdir(data))
	{
		printf("yong: chdir fail\n");
		return -1;
	}
	return 0;
}

int y_im_mkdir(const char *dir)
{
	char temp[256];
	char *p,*pdir;

	pdir=l_strdup(dir);
	p=strrchr(pdir,'/');
	if(!p)
	{
		l_free(pdir);
		return 0;
	}
	*p=0;
	sprintf(temp,"%s/%s",y_im_get_path("HOME"),pdir);
	l_mkdir(temp,0700);
	l_free(pdir);
	return 0;
}

int y_im_config_path(void)
{
#if !defined(CFG_BUILD_LIB)
	y_im_set_exec();
#endif
	y_im_copy_config();
	return 0;
}

#if defined(CFG_XIM_ANDROID)
static void get_so_path(const char *file,char *out)
{
	FILE *fp;
	char line[1024];
	if(out!=NULL)
		strcpy(out,"/data/data/net.dgod.yong/lib");
	fp=fopen("/proc/self/maps","r");
	if(!fp)
		return;
	while(l_get_line(line,sizeof(line),fp)>=0)
	{
		char *p;
		if((p=strstr(line,file)))
		{
			if(p==line)
				break;
			p[-1]=0;
			p=strchr(line,'/');
			if(p==NULL)
				break;
			//YongLogWrite("%s\n",p);
			if(out!=NULL)
				strcpy(out,p);
			break;
		}
	}
	fclose(fp);
	
}
#endif

const char *y_im_get_path(const char *type)
{
	const char *ret;
#if defined(CFG_XIM_ANDROID)
	if(!strcmp(type,"LIB"))
	{
		static char lib_path[128];
		//ret="/data/data/net.dgod.yong/lib";
		if(!lib_path[0])
			get_so_path("libyong.so",lib_path);
		ret=lib_path;			
	}
	else if(!strcmp(type,"HOME"))
	{
		static char home_path[128];
		if(!home_path[0])
		{
			char *p=getenv("EXTERNAL_STORAGE");
			if(!p)
			{
				strcpy(home_path,"/sdcard/yong/.yong");
			}
			else
			{
				sprintf(home_path,"%s/yong/.yong",p);
			}
		}
		if(!l_file_exists(home_path))
		{
			int res=l_mkdir(home_path,0700);
		}
		ret=home_path;
	}
	else
	{
		static char data_path[128];
		if(!data_path[0])
		{
			char *p=getenv("EXTERNAL_STORAGE");
			if(!p)
			{
				strcpy(data_path,"/sdcard/yong");
			}
			else
			{
				sprintf(data_path,"%s/yong",p);
			}
		}
		ret=data_path;
	}
#elif defined(CFG_XIM_WEBIM)
	return "yong";
#elif defined(CFG_XIM_METRO)
	char sys[128];
	if(!SHGetSpecialFolderPathA(NULL,sys,CSIDL_PROGRAM_FILESX86,FALSE))
		SHGetSpecialFolderPathA(NULL,sys,CSIDL_PROGRAM_FILES,FALSE);
	if(!strcmp(type,"LIB"))
	{
		static char path[256];
#ifdef _WIN64
		sprintf(path,"%s\\yong\\w64",sys);
#else
		sprintf(path,"%s\\yong",sys);
#endif
		ret=path;
	}
	else if(!strcmp(type,"HOME"))
	{
		static char path[256];
		sprintf(path,"%s\\yong\\.yong",sys);
		ret=path;
	}
	else
	{
		static char path[256];
		sprintf(path,"%s\\yong",sys);
		ret=path;
	}
#else
	if(!strcmp(type,"HOME"))
	{
		static char path[256];
		sprintf(path,"%s/.yong",getenv("HOME"));
		ret=path;
		if(!l_file_exists(ret))
			l_mkdir(ret,0700);
	}
	else
	{
		if(!strcmp(type,"LIB"))
			ret=".";
		else
			ret="..";
	}
#endif
	return ret;
}

static const struct{
	char *name;
	int key;
}str_key_map[]={
	{"NONE",0},
	{"LCTRL",YK_LCTRL},
	{"RCTRL",YK_RCTRL},
	{"LSHIFT",YK_LSHIFT},
	{"RSHIFT",YK_RSHIFT},
	{"LALT",YK_LALT},
	{"RALT",YK_RALT},
	{"TAB",YK_TAB},
	{"ESC",YK_ESC},
	{"ENTER",YK_ENTER},
	{"BACKSPACE",YK_BACKSPACE},
	{"SPACE",YK_SPACE},
	{"DEL",YK_DELETE},
	{"HOME",YK_HOME},
	{"LEFT",YK_LEFT},
	{"UP",YK_UP},
	{"DOWN",YK_DOWN},
	{"RIGHT",YK_RIGHT},
	{"PGUP",YK_PGUP},
	{"PGDN",YK_PGDN},
	{"PAGEUP",YK_PGUP},
	{"PAGEDOWN",YK_PGDN},
	{"END",YK_END},
	{"INSERT",YK_INSERT},
	{"CAPSLOCK",YK_CAPSLOCK},
	{"COMMA",','},
	{NULL,0}
};

int y_im_str_to_key(const char *s)
{
	char tmp[16];
	int i;
	const char *p=s;
	int key=0;

	if(!s || !s[0] || s[0]=='_')
		return -1;
	while(p[0])
	{
		for(i=0;i<15;i++)
		{
			tmp[i]=*p;
			if(!tmp[i] || tmp[i]==' ')
				break;
			p++;
			if(i>0 && tmp[i]=='_')
				break;
		}
		tmp[i]=0;
		if(i==0)
		{
			return -1;
		}
		else if(i==1)
		{
			if(isgraph(tmp[0]))
			{
				key|=tmp[0];
				if(p[0])
					return -1;
				break;
			}
			else
			{
				return -1;
			}
		}
		else if(!strcmp(tmp,"CTRL"))
		{
			if(key&KEYM_CTRL)
				return -1;
			key|=KEYM_CTRL;
		}
		else if(!strcmp(tmp,"SHIFT"))
		{
			if(key&KEYM_SHIFT)
				return -1;
			key|=KEYM_SHIFT;
		}
		else if(!strcmp(tmp,"ALT"))
		{
			if(key&KEYM_ALT)
				return -1;
			key|=KEYM_ALT;
		}
		else if(!strcmp(tmp,"WIN"))
		{
			if(key&KEYM_SUPER)
				return -1;
			key|=KEYM_SUPER;
		}
		else
		{
			for(i=0;str_key_map[i].name;i++)
			{
				if(!strcmp(str_key_map[i].name,tmp))
				{
					key|=str_key_map[i].key;
					break;
				}				
			}
			if(!str_key_map[i].name)
			{
				return -1;
			}
			if(p[0]==' ')
				break;
		}
	}
	return key;
}

int y_im_get_key(const char *name,int pos,int def)
{
	int ret=-1;
	char *tmp;

	tmp=y_im_get_config_string("key",name);
	if(!tmp) return def;
	if(!strcmp(tmp,"CTRL"))
	{
		l_free(tmp);
		tmp=l_strdup("LCTRL RCTRL");
	}
	else if(!strcmp(tmp,"SHIFT"))
	{
		l_free(tmp);
		tmp=l_strdup("LSHIFT RSHIFT");
	}
	if(pos==-1)
	{
		ret=y_im_str_to_key(tmp);
	}
	else
	{
		char **list;
		int i;
		list=l_strsplit(tmp,' ');
		for(i=0;i<pos && list[i];i++);
		if(i==pos && list[i])
			ret=y_im_str_to_key(list[i]);
		l_strfreev(list);
	}
	if(ret<0) ret=def;
	l_free(tmp);
	return ret;
}

static void str_replace(char *s1,int l1,const char *s2)
{
	int l2=strlen(s2);
	int left=strlen(s1+l1);
	memmove(s1+l2,s1+l1,left+1);
	memcpy(s1,s2,l2);
}

static char *num2hz(int n,const char *fmt,int flag)
{
	const char *ch0="��һ�����������߰˾�";
	const char *ch1="��һ�����������߰˾�";
	static char hz[64];
	char t[32];

	sprintf(t,fmt,n);
	switch(flag){
	case 0:
	{
		strcpy(hz,t);
		break;
	}
	case 1:
	{
		int i;
		for(i=0;t[i];i++)
			memcpy(hz+2*i,ch0+(t[i]-'0')*2,2);
		hz[2*i]=0;
		break;
	}
	case 2:
	{
		int i;
		for(i=0;t[i];i++)
			memcpy(hz+2*i,ch1+(t[i]-'0')*2,2);
		hz[2*i]=0;
		break;
	}
	case 3:
	{
		int l=strlen(t);
		if(l>2)
		{
			hz[0]=0;
			break;
		}
		else if(l==1)
		{
			memcpy(hz,ch0+(t[0]-'0')*2,2);
			hz[2]=0;
		}
		else if(l==2)
		{
			if(t[0]=='0')
			{
				if(t[1]!=0)
				{
					memcpy(hz,ch0,2);
					memcpy(hz+2,ch0+(t[1]-'0')*2,2);
					hz[4]=0;
				}
				else
				{
					memcpy(hz,ch0,2);
					hz[2]=0;
				}
				break;
			}
			else if(t[0]=='1')
			{
				memcpy(hz,"ʮ",2);
				if(t[1]!='0')
				{
					memcpy(hz+2,ch0+(t[1]-'0')*2,2);
					hz[4]=0;
				}
				else
				{
					hz[2]=0;
				}
				break;
			}
			memcpy(hz,ch0+(t[0]-'0')*2,2);
			memcpy(hz+2,"ʮ",2);
			if(t[1]!='0')
			{
				memcpy(hz+4,ch0+(t[1]-'0')*2,2);
				hz[6]=0;
			}
			else
			{
				hz[4]=0;
			}
		}
		break;
	}
	default:
	{
		hz[0]=0;
		break;
	}}
	return hz;
}

int y_im_forward_key(const char *s)
{
	int key;
	if(s[0]!='$')
		return -1;
	key=y_im_str_to_key(s+1);
	if(key<=0 || (key&KEYM_MASK))
		return -1;
	y_xim_forward_key(key);
	return 0;
}

void y_im_expand_space(char *s)
{
	char *ps;

	ps=strchr(s,'$');
	while(ps!=NULL)
	{
		ps++;
		if(!strncmp(ps,"$",1))
		{
			str_replace(ps-1,2,"$");
		}
		else if(!strncmp(ps,"_",1))
		{
			str_replace(ps-1,2," ");
		}
		ps=strchr(ps,'$');
	}
}

void y_im_expand_env(char *s)
{
	char *ps;
	
	ps=strchr(s,'$');
	while(ps!=NULL)
	{
		ps++;
		if(ps[0]=='(')
		{
			char name[32];
			int i;
			for(i=0;i<31;i++)
			{
				int c=ps[i+1];
				if(c==0) return;
				if(c==')') break;
				name[i]=c;
			}
			
			if(i<31 && i>0)
			{
				const char *val;
				name[i]=0;
				if(!strcmp(name,"_HOME"))
					val=y_im_get_path("HOME");
				else if(!strcmp(name,"_DATA"))
					val=y_im_get_path("DATA");
				else
					val=getenv(name);
				if(val!=NULL)
					str_replace(ps-1,3+i,val);
			}
		}
		ps=strchr(ps,'$');
	}
}

int y_im_go_url(const char *s)
{
	char *tmp;
	char *ps;
	char hold[256];

	if(s[0]!='$')
		return -1;

	/* first convert $_ to space */
	snprintf(hold,256,"%s",s);
	ps=strchr(hold,'$');
	do{
		ps++;
		/* self */
		if(!strncmp(ps,"$",1))
		{
			str_replace(ps-1,2,"$");
		}
		else if(!strncmp(ps,"_",1))
		{
			str_replace(ps-1,2," ");
		}
		else if(!strncmp(ps,"(",1))
		{
			char name[32];
			int i;
			for(i=0;i<31;i++)
			{
				int c=ps[i+1];
				if(c==')') break;
				name[i]=c;
			}
			if(i<31 && i>0)
			{
				char *val;
				name[i]=0;
				val=getenv(name);
				if(val!=NULL)
					str_replace(ps-1,3+i,val);
			}
		}
		ps=strchr(ps,'$');
	}while(ps!=NULL);
	if(s[1]=='G' && s[2]=='O' && s[3]=='(')
	{
		int len=strlen(s);
		if(s[len-1]==')')
		{
			char go[256];
			strcpy(go,s+4);go[len-5]=0;
			tmp=strchr(go,',');
			if(tmp==NULL)
			{
				tmp=go;
			}
			else
			{
				tmp++;
			}
			if(!strncmp(tmp,"$DECRYPT(",9) && l_str_has_suffix(tmp,")"))
			{
				char dec[80];
				tmp[strlen(tmp)-1]=0;
				if(0!=y_im_book_decrypt(tmp+9,dec))
					return -1;
				y_xim_explore_url(dec);
				return 0;
			}
			y_im_expand_space(tmp);
			y_im_expand_env(tmp);
			y_xim_explore_url(tmp);
			return 0;
		}
	}
	return -1;
}

int y_im_send_file(const char *s)
{
	char *tmp;

	if(s[0]!='$')
		return -1;

	if(!strncmp(s+1,"FILE(",5))
	{
		int len=strlen(s);
		if(s[len-1]==')')
		{
			char go[128];
			strcpy(go,s+6);
			tmp=strchr(go,')');
			if(!tmp)
				return -1;
			*tmp=0;
			YongSendFile(go);
			return 0;
		}
	}
	return -1;
}

int y_im_str_desc(const char *s,void *out)
{
	int ret=0;
	char *end;
	if(s[0]!='$' || s[1]!='[' || (end=gb_strchr((uint8_t*)s+2,']'))==0)
		return 0;
	ret=(int)(end-s-2);
	if(out && ret)
	{
		char temp[ret+1];
		memcpy(temp,s+2,ret);
		temp[ret]=0;
		y_im_str_encode(temp,out,0);
	}
	return ret+3;
}

char *y_im_str_escape(const char *s,int commit)
{
	char *ps;
	struct tm *tm;
	time_t t;
	char *tmp;
	static char line[8192];

	/* test if escape needed */
	ps=strchr(s,'$');
	if(!ps)
	{
		/* copy, so we can always change the escaped string without change orig */
		strcpy(line,s);
		return line;
	}
	
	/* do $LAST first, so we can escape the content later */
	if(s[0]=='$' && !strcmp(s+1,"LAST"))
	{
		s=last_output;
		ps=strchr(s,'$');
		if(!ps)
		{
			strcpy(line,s);
			return line;
		}
	}
	
	/* is only a key, or url */
	if(s[0]=='$')
	{
		int key;
		key=y_im_str_to_key(s+1);
		if(key>0 && !(key&KEYM_MASK))
		{
			strcpy(line,s+1);
			return line;
		}
		while(s[1]=='G' && s[2]=='O' && s[3]=='(')
		{
			int len=strlen(s);
			if(s[len-1]==')')
			{
				char go[MAX_CAND_LEN+1];
				strcpy(go,s+4);go[len-5]=0;
				if(strchr(go,',')==NULL)
				{
					strcpy(line,"->");
					return line;
				}
				else
				{
					tmp=strtok(go,",");
					snprintf(line,32,"->%s",tmp);
					return line;
				}
			}
			break;
		}
		if(s[1]=='B' && s[2]=='D' && s[3]=='(' && s[4] && s[5]==')' && !s[6])
		{
			const char *temp;
			int lang=LANG_CN;
			CONNECT_ID *id=y_xim_get_connect();
			if(id) lang=id->biaodian;
			temp=YongGetPunc(s[4],lang,commit?0:1);
			if(!temp) return NULL;
			strcpy(line,temp);
			return line;
		}
	}
	strcpy(line,s);
	s=line;
	ps=strchr(s,'$');
	t=time(NULL);
	tm=localtime(&t);

	/* escape the time and $ self */
	do{
		ps++;
		/* self */
		if(!strncmp(ps,"$",1))
		{
			str_replace(ps-1,2,"$");
		}
		else if(!strncmp(ps,"_",1))
		{
			str_replace(ps-1,2," ");
		}
		else if(!strncmp(ps,"/",1))
		{
			str_replace(ps-1,2,"\n");
		}
		/* english */
		else if(!strncmp(ps,"ENGLISH",7))
		{
			str_replace(ps-1,8,"->EN");
		}
		/* time */
		else if(!strncmp(ps,"YYYY0",5))
		{
			tmp=num2hz(tm->tm_year+1900,"%d",2);
			str_replace(ps-1,6,tmp);
		}
		else if(!strncmp(ps,"YYYY",4))
		{
			tmp=num2hz(tm->tm_year+1900,"%d",1);
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"yyyy",4))
		{
			tmp=num2hz(tm->tm_year+1900,"%d",0);
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"MON",3))
		{
			tmp=num2hz(tm->tm_mon+1,"%d",3);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"mon0",4))
		{
			tmp=num2hz(tm->tm_mon+1,"%02d",0);
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"mon",3))
		{
			tmp=num2hz(tm->tm_mon+1,"%d",0);
			str_replace(ps-1,4,tmp);
		}		
		else if(!strncmp(ps,"DAY",3))
		{
			tmp=num2hz(tm->tm_mday,"%d",3);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"day0",4))
		{
			tmp=num2hz(tm->tm_mday,"%02d",0);
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"day",3))
		{
			tmp=num2hz(tm->tm_mday,"%d",0);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"HOUR",4))
		{
			tmp=num2hz(tm->tm_hour,"%d",3);
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"hour0",5))
		{
			tmp=num2hz(tm->tm_hour,"%02d",0);
			str_replace(ps-1,6,tmp);
		}
		else if(!strncmp(ps,"hour",4))
		{
			tmp=num2hz(tm->tm_hour,"%d",0);
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"MIN",3))
		{
			tmp=num2hz(tm->tm_min,"%02d",3);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"min",3))
		{
			tmp=num2hz(tm->tm_min,"%02d",0);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"SEC",3))
		{
			tmp=num2hz(tm->tm_sec,"%02d",3);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"sec",3))
		{
			tmp=num2hz(tm->tm_sec,"%02d",0);
			str_replace(ps-1,4,tmp);
		}
		else if(!strncmp(ps,"WEEK",4) || !strncmp(ps,"week",4))
		{
			static const char *week_name[]=
				{"��","һ","��","��","��","��","��",""};
			tmp=(char*)week_name[tm->tm_wday];
			str_replace(ps-1,5,tmp);
		}
		else if(!strncmp(ps,"RIQI",4))
		{
			char nl[128];
			y_im_nl_day(t,nl);
			str_replace(ps-1,5,nl);
		}
		else if(!strncmp(ps,"LAST",4))
		{
			size_t len=ps-line-1;
			if(len>0)
			{
				char temp[len];
				memcpy(temp,line,len);
				temp[len]=0;
				str_replace(ps-1,5,temp);
			}
			else
			{
				str_replace(ps-1,5,last_output);
			}
		}
		s=ps;ps=strchr(s,'$');
	}while(ps!=NULL);
	return line;
}

int y_im_strip_key(char *gb)
{
	char *s;
	int key=0;
	s=strrchr(gb,'$');
	if(s && s[1]=='|')
	{
		int i;
		key=0;
		memmove(s,s+2,strlen(s+2)+1);
		for(i=0;s[i];)
		{
			if(s[i] && !s[i+1])
			{
				key++;
				break;
			}
			if(s[i]&0x80)
			{
				key++;
				i+=2;
			}
			else
			{
				if(s[i]=='$' && (s[i+1]=='_' || s[i+1]=='$' || s[i+1]=='/'))
				{
					key++;
					i+=2;
				}
				else
				{
					key++;
					i++;
				}
			}
		}
	}
	else if(s)
	{
		key=y_im_str_to_key(s+1);
		if(key>0) *s=0;
		if(key==YK_LEFT)
			key=1;
		else
			key=0;
	}
	return key;
}

void y_im_disp_cand(const char *gb,char *out,int pre,int suf)
{
	int pos=0,len=0,skip,pad=0;
	char *s,*end;
	char temp[MAX_CAND_LEN+1];
	
	/* do $LAST first, so we can escape the content later */
	if(gb[0]=='$' && !strcmp(gb+1,"LAST"))
	{
		strcpy(temp,last_output);
	}
	else
	{
		strcpy(temp,gb);
	}
	/* if found desc, only display it */
	s=temp;	
	if(s[0]=='$' && s[1]=='[' && (end=gb_strchr((uint8_t*)s+2,']'))!=0)
	{
			int ret=(int)(end-s-2);
			if(ret)
			{
				memmove(s,s+2,ret);
				s[ret]=0;
				gb=s;
			}
	}

	/* escape the string */
	s=y_im_str_escape(gb,0);
	/* strip key in it */
	y_im_strip_key(s);
	/* get the length of input */
	gb=s;
	
	while(s[0])
	{
		if(!(s[0]&0x80))
		{
			len++;s++;
		}
		else if(gb_is_gbk((uint8_t*)s))
		{
			len+=2;s+=2;
		}
		else if(gb_is_gb18030_ext((uint8_t*)s))
		{
			len+=2;s+=4;
		}
		else
		{
			len++;s++;
		}
	}
	/* get string should escape */
	skip=len-2*(pre+suf);
	if(skip<=0)
	{
		/* gb is from y_im_str_escape, we should not let it both in and out of this */
		if(gb!=temp) strcpy(temp,gb);
		y_im_str_encode(temp,out,0);
		return;
	}
	/* copy the string and skip some */
	s=temp;
	while(gb[0])
	{
		if(gb_is_ascii((uint8_t*)gb))
		{
			if(pos<2*pre || skip<=0)
				*s++=*gb;
			else
				skip--;
			gb++;
			pos++;
		}
		else if(gb_is_gbk((uint8_t*)gb))
		{
			if(pos<2*pre || skip<=0)
			{
				*s++=*gb;
				*s++=*(gb+1);
			}
			else
				skip-=2;
			gb+=2;
			pos+=2;
		}
		else if(gb_is_gb18030_ext((uint8_t*)gb))
		{
			if(pos<2*pre || skip<=0)
			{
				*s++=*gb;
				*s++=*(gb+1);
				*s++=*(gb+2);
				*s++=*(gb+3);
			}
			else
				skip-=2;
			gb+=4;
			pos+=2;
		}
		else
		{
			if(pos<2*pre || skip<=0)
				*s++=*gb;
			else
				skip--;
			gb++;
			pos++;
		}
		if(!pad && skip>0 && pos>=2*pre)
		{
			*s++='.';*s++='.';*s++='.';
			pad=1;
		}
	}
	/* encode it last */
	*s=0;
	y_im_str_encode(temp,out,0);
}

int y_im_str_encode(const char *gb,void *out,int flags)
{

	int key=-1;
	char *s;
	if(!gb[0])
	{
		memset(out,0,4);
		return 0;
	}
	if(!(flags&DONT_ESCAPE))
	{
		s=y_im_str_escape(gb,1);
		y_im_strip_key(s);
	}
	else
	{
		s=l_alloca(8192);
		strcpy(s,gb);
	}

#if defined(_WIN32) || defined(CFG_XIM_ANDROID)
	l_gb_to_utf16(s,out,8192);
#else
	l_gb_to_utf8(s,out,8192);
#endif

	return key;
}

void y_im_str_encode_r(const void *in,char *gb)
{
	l_utf8_to_gb(in,gb,4096);
}

void y_im_url_encode(char *gb,char *out)
{
	int i;
	char temp[256];

	l_gb_to_utf8(gb,temp,sizeof(temp));
	for(i=0;temp[i];i++)
		sprintf(out+3*i,"%%%02x",(uint8_t)temp[i]);
}

int y_im_is_url(const char *s)
{
	const char *p1,*p2;
	if(s[0]=='"')
		return 0;
	p1=strchr(s,':');
	p2=strchr(s,' ');
	if(p1 && (!p2 || p2>p1))
		return 1;
	return 0;
}

char *y_im_auto_path(char *fn)
{
	char temp[256];

	assert(fn!=NULL);
	
	if(fn[0]=='/' || (fn[0]=='.' && fn[1]=='/'))
	{
		strcpy(temp,fn);
	}
	else
	{
		sprintf(temp,"%s/%s",y_im_get_path("HOME"),fn);
		if(!l_file_exists(temp))
			sprintf(temp,"%s/%s",y_im_get_path("DATA"),fn);
	}
	return l_strdup(temp);
}

FILE *y_im_open_file(const char *fn,const char *mode)
{
	FILE *fp;
	if(!fn)
		return NULL;
	
	if(fn[0]=='/' || (fn[0]=='.' && fn[1]=='/'))
	{
		if(strchr(mode,'w'))
			y_im_mkdir(fn);
		fp=l_file_open(fn,mode,NULL);
	}
	else
	{
		if(strchr(mode,'w'))
			y_im_mkdir(fn);
		fp=l_file_open(fn,mode,y_im_get_path("HOME"),
				y_im_get_path("DATA"),NULL);
	}
	return fp;
}

void y_im_remove_file(char *fn)
{
	char temp[256];

	if(!fn)
		return;
	
	if(fn[0]=='/')
	{
		strcpy(temp,fn);
		l_remove(temp);
	}
	else
	{
		sprintf(temp,"%s/%s",y_im_get_path("HOME"),fn);
		l_remove(temp);
	}
}

static char *y_im_urls[64]={
	"www.",
	"ftp.",
	"bbs.",
	"mail.",
	"blog.",
	"http",
};
static int urls_begin=0;

static Y_USER_URL *urls_user=0;

Y_USER_URL *y_im_user_urls(void)
{
	return urls_user;
}

void y_im_free_urls(void)
{
	Y_USER_URL *p,*n;
	int i;
	for(i=6;i<64;i++)
	{
		if(!y_im_urls[i])
			break;
		l_free(y_im_urls[i]);
		y_im_urls[i]=0;
	}
	urls_begin=0;
	for(p=urls_user;p;p=n)
	{
		n=p->next;
		l_free(p->url);
		l_free(p);
	}
	urls_user=0;
}

void y_im_load_urls(void)
{
	FILE *fp;
	int i;
	char temp[256];
	y_im_free_urls();
	fp=l_file_open("urls.txt","rb",y_im_get_path("HOME"),
				y_im_get_path("DATA"),NULL);
	if(!fp) return;	
	for(i=6;i<64;)
	{
		if(l_get_line(temp,256,fp)<0)
			break;
		if(temp[0]==0)
			continue;
		if(!strcmp(temp,"!zero"))
		{
			urls_begin=6;
			continue;
		}
		else if(!strcmp(temp,"!english"))
		{
			while(l_get_line(temp,256,fp)>0)
			{
				Y_USER_URL *item=l_new(Y_USER_URL);
				item->url=l_strdup(temp);
				urls_user=l_slist_append(urls_user,item);
			}
			break;
		}
		y_im_urls[i++]=l_strdup(temp);
	}
	fclose(fp);
}

char *y_im_find_url(char *pre)
{
	int len;
	int i;
	if(!pre)
		return NULL;
	len=strlen(pre);
	if(len<=0)
		return NULL;
	for(i=urls_begin;i<64 && y_im_urls[i];i++)
	{
		if(!strncmp(pre,y_im_urls[i],len))
			return y_im_urls[i];
	}
	return NULL;
}

char *y_im_find_url2(char *pre,int next)
{
	int len;
	int i;
	if(!pre)
		return NULL;
	len=strlen(pre);
	if(len<=0)
		return NULL;
	for(i=urls_begin;i<64 && y_im_urls[i];i++)
	{
		if(!strncmp(pre,y_im_urls[i],len) && next==y_im_urls[i][len])
			return y_im_urls[i];
	}
	return NULL;
}

void y_im_backup_file(char *path,char *suffix)
{
	char temp[256];
	sprintf(temp,"%s%s",path,suffix);
	l_remove(temp);
	y_im_copy_file(path,temp);
}

void y_im_copy_config(void)
{
	char path[256];
	LKeyFile *usr;
	LKeyFile *sys;

	sys=l_key_file_open("yong.ini",0,y_im_get_path("DATA"),NULL);
	if(!sys)
	{
		return;
	}
	sprintf(path,"%s/yong.ini",y_im_get_path("HOME"));
	if(l_file_exists(path))
		usr=l_key_file_open("yong.ini",0,y_im_get_path("HOME"),NULL);
	else
		usr=NULL;
	if(usr)
	{
		if(l_key_file_get_int(usr,"DESC","version")==1)
		{
			char *p=l_key_file_get_string(usr,"IM","default");
			if(p)
			{
				l_key_file_set_string(usr,"IM","default","0");
				l_key_file_set_string(usr,"IM","0",p);
				l_key_file_set_string(usr,"DESC","version","2");
				l_free(p);
				l_key_file_save(usr,y_im_get_path("HOME"));
			}
		}
		else
		{
			if(l_key_file_get_int(usr,"DESC","version")<
				l_key_file_get_int(sys,"DESC","version"))
			{
				char path[256];
				sprintf(path,"%s/yong.ini",y_im_get_path("HOME"));
				y_im_backup_file(path,".old");
				l_key_file_set_dirty(sys);
				l_key_file_save(sys,y_im_get_path("HOME"));
			}
		}
		l_key_file_free(usr);
	}
	else
	{
		l_key_file_set_dirty(sys);
		l_key_file_save(sys,y_im_get_path("HOME"));
	}
	l_key_file_free(sys);
}

void y_im_set_default(int index)
{
	int def=y_im_get_config_int("IM","default");
	
	if(def==index)
		return;
		
	l_key_file_set_int(MainConfig,"IM","default",index);
	l_key_file_save(MainConfig,y_im_get_path("HOME"));	
}

static struct y_im_speed speed_all;
static struct y_im_speed speed_last;
static struct y_im_speed speed_cur;
static struct y_im_speed speed_top;

void y_im_speed_reset(void)
{
	memset(&speed_all,0,sizeof(struct y_im_speed));
	memset(&speed_cur,0,sizeof(struct y_im_speed));
	memset(&speed_top,0,sizeof(struct y_im_speed));
	memset(&speed_last,0,sizeof(struct y_im_speed));
}

static void im_speed_update(time_t now,int force)
{
	int delta;
	delta=now-speed_cur.last;
	if(delta<0)
	{
		memset(&speed_cur,0,sizeof(struct y_im_speed));
	}
	if(now-speed_cur.last<5 && !force)
		return;
	if(!speed_cur.last)
	{
		return;
	}
	delta=speed_cur.last+1-speed_cur.start;
	if(delta>=5 || force)
	{
		speed_cur.speed=speed_cur.zi*60/delta;
		speed_cur.last=delta;
		speed_cur.start=0;
		if(speed_cur.speed>speed_top.speed)
			memcpy(&speed_top,&speed_cur,sizeof(struct y_im_speed));
		memcpy(&speed_last,&speed_cur,sizeof(struct y_im_speed));
		speed_all.last+=delta;
		speed_all.zi+=speed_cur.zi;
		speed_all.key+=speed_cur.key;
		speed_all.space+=speed_cur.space;
		speed_all.select2+=speed_cur.select2;
		speed_all.select3+=speed_cur.select3;
		speed_all.select+=speed_cur.select;
		speed_all.back+=speed_cur.back;
		speed_all.speed=speed_all.zi*60/speed_all.last;
	}
	memset(&speed_cur,0,sizeof(struct y_im_speed));
}

void y_im_speed_update(int key,const char *s)
{
	time_t now=time(NULL);
	
	im_speed_update(now,0);
	if(key)
	{
		speed_cur.key++;
		if(key==YK_SPACE)
			speed_cur.space++;
		else if(key==key_select[0])
			speed_cur.select2++;
		else if(key==key_select[1])
			speed_cur.select3++;
		else if(strchr(key_select_n,key))
			speed_cur.select++;
		else if(key==YK_BACKSPACE)
			speed_cur.back++;
		if(!speed_cur.start)
			speed_cur.start=now;
		speed_cur.last=now;
	}
	if(s)
	{
		speed_cur.zi+=gb_strlen((uint8_t*)s);
	}
}

void y_im_about_self(void)
{
	char temp[2048];
	int pos=0;
	
	pos=sprintf(temp,"%s%s\n",YT("���ߣ�"),"dgod");
	pos+=sprintf(temp+pos,"%s%s\n",YT("��̳��"),"http://yong.dgod.net");
	/*pos+=*/sprintf(temp+pos,"%s%s\n",YT("����ʱ�䣺"),YONG_VERSION);
	
#if 0
	pos+=sprintf(temp+pos,"%s%d",YT("�����룺"),(int)y_im_gen_mac());
#endif

	y_ui_show_message(temp);
}

static char *y_im_speed_stat(void)
{
	char *res=l_alloc(2048);
	char format[1024];
	int len=0;
	double zi;
	static const char *split="------------------------------------------------------------------------\n";
	struct y_im_speed *speed;

	im_speed_update(time(0),1);
	
	sprintf(format,"%s: %%d%s \t%s: %%d%s\n"
		"%s: %%.2f%s \t%s: %%.2f%s \t%s: %%.2f%s\n"
		"%s: %%.2f%s \t%s: %%.2f%s\n",
			YT("����"),YT("��"),YT("�ٶ�"),YT("��/��"),
			YT("����"),YT("��/��"),YT("�볤"),YT("��/��"),YT("�ո�"),YT("��/��"),
			YT("ѡ��"),YT("��/��"),YT("����"),YT("��/��")
		);
	
	speed=&speed_all;
	zi=speed->zi+0.01;
	len+=sprintf(res+len,"%s\n%s",YT("ȫ��"),split);
	len+=sprintf(res+len,format,
			speed->zi,speed->speed,
			speed->key*1.0f/(speed->last+(speed->last?0:1)),speed->key/zi,speed->space/zi,
			speed->select/zi,speed->back/zi);
	len+=sprintf(res+len,"\n");

	speed=&speed_top;
	zi=speed->zi+0.01;
	len+=sprintf(res+len,"%s\n%s",YT("���"),split);
	len+=sprintf(res+len,format,
			speed->zi,speed->speed,
			speed->key*1.0f/(speed->last+(speed->last?0:1)),speed->key/zi,speed->space/zi,
			speed->select/zi,speed->back/zi);
	len+=sprintf(res+len,"\n");
	
	speed=&speed_last;
	zi=speed->zi+0.01;
	len+=sprintf(res+len,"%s\n%s",YT("��һ��"),split);
	/*len+=*/sprintf(res+len,format,
			speed->zi,speed->speed,
			speed->key*1.0f/(speed->last+(speed->last?0:1)),speed->key/zi,speed->space/zi,
			speed->select/zi,speed->back/zi);

	return res;
}

void *y_im_module_open(char *path)
{
	void *ret=0;
	char temp[256];
	
	if(strchr(path,'/'))
		strcpy(temp,path);
	else
		sprintf(temp,"%s/%s",y_im_get_path("LIB"),path);
	ret=dlopen(temp,RTLD_LAZY);
	if(!ret)
	{
		printf("dlopen %s error %s\n",temp,dlerror());
	}
	return ret;
}

void *y_im_module_symbol(void *mod,char *name)
{
	void *ret;
	ret=dlsym(mod,name);
	return ret;
}

void y_im_module_close(void *mod)
{
	dlclose(mod);
}

int y_im_run_tool(char *func,void *arg,void **out)
{
	int (*tool)(void *,void **);
	
	if(!im.handle)
	{
		printf("yong: no active module\n");
		return -1;
	}
	
	tool=y_im_module_symbol(im.handle,func);
	if(!tool)
	{
		printf("yong: this module don't have such tool\n");
		return -1;
	}

	return tool(arg,out);
}

char *y_im_get_im_config_string(int index,const char *key)
{
	char temp[8];
	const char *entry;
	sprintf(temp,"%d",index);
	entry=l_key_file_get_data(MainConfig,"IM",temp);
	if(!entry)
		return NULL;
	return y_im_get_config_string(entry,key);
}

int y_im_has_im_config(int index,const char *key)
{
	char temp[8];
	const char *entry;
	sprintf(temp,"%d",index);
	entry=l_key_file_get_data(MainConfig,"IM",temp);
	if(!entry)
		return 0;
	return l_key_file_get_data(MainConfig,entry,"skin")?1:0;
}

int y_im_get_im_config_int(int index,const char *key)
{
	char temp[8];
	const char *entry;
	sprintf(temp,"%d",index);
	entry=l_key_file_get_data(MainConfig,"IM",temp);
	if(!entry)
		return 0;
	return y_im_get_config_int(entry,key);
}

char *y_im_get_current_engine(void)
{
	static char engine[32];
	char *entry;
	char *p;
	sprintf(engine,"%d",im.Index);
	entry=y_im_get_config_string("IM",engine);
	engine[0]=0;
	if(!entry)
		return engine;
	p=y_im_get_config_string(entry,"engine");
	snprintf(engine,32,"%s",p);
	l_free(entry);
	l_free(p);
	return engine;
}

char *y_im_get_im_name(int index)
{
	char *entry;
	char temp[32];

	sprintf(temp,"%d",index);
	entry=y_im_get_config_string("IM",temp);
	if(entry)
	{
		char *p;
		p=y_im_get_config_string(entry,"name");
		l_free(entry);
		if(p)
		{
			int size=strlen(p)*2+1;
			entry=l_alloc(size);
			l_utf8_to_gb(p,entry,size);
			l_free(p);
		}
		else
		{
			entry=0;
		}
	}
	if(!entry && index==im.Index && im.eim)
		entry=l_strdup(im.eim->Name);
	return entry;
}

void y_im_setup_config(void)
{
#if !defined(CFG_NO_HELPER)
	char config[256];
	char *args[3]={0,config,0};
	char prog[256];

	char *setup="yong-config";

	sprintf(config,"%s/yong.ini",y_im_get_path("HOME"));

	if(!args[0] && l_file_exists(setup))
	{
		args[0]=setup;
	}
	if(!args[0] && l_file_exists("/usr/bin/yong-config"))
	{
		args[0]="/usr/bin/yong-config";
	}
	if(!args[0])
	{
		static char user[256];
		char *tmp=y_im_get_config_string("main","edit");
		if(tmp && tmp[0] && tmp[0]!=' ')
		{
			strcpy(user,tmp);
			args[0]=user;
		}
		l_free(tmp);
	}
	if(!args[0]) args[0]="xdg-open";
	sprintf(prog,"%s %s",args[0],config);

	if(strstr(args[0],"yong-config"))
	{
		char temp[32];
		y_im_get_current(temp,sizeof(temp));
		sprintf(prog+strlen(prog)," --active=%s",temp);
	}

	y_im_run_helper(prog,config,YongReloadAll);
#endif
}

#if !defined(CFG_NO_HELPER)
uint32_t y_im_tick(void)
{
	struct timeval tv;
	gettimeofday(&tv,0);
	return (uint32_t)(tv.tv_sec*1000+tv.tv_usec/1000);
}

struct im_helper{
	GPid pid;
	guint timer;
	guint child;
	char *prog;
	char *watch;
	time_t mtime;
	void (*cb)(void);
};
static struct im_helper helper_list[4];

static time_t y_im_last_mtime(const char *file)
{
	struct stat st;
	if(!file) return 0;
	if(0!=stat(file,&st))
		return 0;
	return st.st_mtime;
}

static void  HelperExit(GPid pid,gint status,gpointer data)
{
	int i;
	for(i=0;i<4;i++)
	{
		struct im_helper *p=helper_list+i;
		if(p->pid!=pid) continue;
		if(p->watch)
		{
			time_t mtime;
			mtime=y_im_last_mtime(p->watch);
			if(mtime!=p->mtime)
			{
				p->mtime=mtime;
				if(p->cb)
					p->cb();
			}
		}
		g_free(p->prog);
		p->prog=0;
		g_free(p->watch);
		p->watch=0;
		g_source_remove(p->timer);
		g_source_remove(p->child);
	}
	g_spawn_close_pid(pid);
}

static gboolean HelperTimerProc(gpointer data)
{
	struct im_helper *p=data;
	if(p->watch)
	{
		time_t mtime;
		mtime=y_im_last_mtime(p->watch);
		if(mtime!=p->mtime)
		{
			p->mtime=mtime;
			if(p->cb)
				p->cb();
		}
	}
	return TRUE;
}

void y_im_run_helper(char *prog,char *watch,void (*cb)(void))
{
	int i;
	gint argc;
	gchar **argv;
	GPid pid;
	for(i=0;i<4;i++)
	{
		char *p=helper_list[i].prog;
		if(!p) continue;
		if(!strcmp(p,prog))
			return;
	}
	if(!g_shell_parse_argv(prog,&argc,&argv,NULL))
		return;
	if(!g_spawn_async(0,argv,0,
			G_SPAWN_DO_NOT_REAP_CHILD|G_SPAWN_SEARCH_PATH,
			0,0,&pid,0))
	{
		g_strfreev(argv);
		return;
	}
	g_strfreev(argv);
	for(i=0;i<4;i++)
	{
		struct im_helper *p=helper_list+i;
		if(p->prog) continue;
		p->prog=g_strdup(prog);
		p->watch=watch?g_strdup(watch):0;
		p->pid=pid;
		p->mtime=y_im_last_mtime(watch);
		p->timer=g_timeout_add(1000,HelperTimerProc,p);
		p->cb=cb;
		p->child=g_child_watch_add(pid,HelperExit,p);
		return;
	}
	g_spawn_close_pid(pid);
}
#endif

int y_strchr_pos(char *s,int c)
{
	int i;
	for(i=0;s[i];i++)
	{
		if(s[i]==c)
			return i;
	}
	return -1;
}

void y_im_str_strip(char *s)
{
	int c,l;
	while((c=*s++)!=0)
	{
		if(c!=' ') continue;
		l=strlen(s);
		memmove(s-1,s,l+1);
		s--;
	}
}

void y_im_show_help(char *wh)
{
	char *p,*ps;
	p=y_im_get_config_string(wh,"help");
	if(!p) return;
	ps=strchr(p,' ');
	if(ps)
		y_xim_explore_url(ps+1);
	l_free(p);
}

int y_im_help_desc(char *wh,char *desc,int len)
{
	char *p,*ps;
	p=y_im_get_config_string(wh,"help");
	if(!p) return -1;
	ps=strchr(p,' ');
	if(ps) *ps=0;
	snprintf(desc,len,"%s",p);
	l_free(p);
	return 0;
}

int y_im_get_current(char *item,int len)
{
	char *p;
	char wh[32];
	sprintf(wh,"%d",im.Index);
	p=l_key_file_get_string(MainConfig,"IM",wh);
	if(!p) return -1;
	snprintf(item,len,"%s",p);
	l_free(p);
	return 0;
}

int y_im_get_keymap(char *name,int len)
{
	char item[128];
	char *p,*ps;
	if(0!=y_im_get_current(item,sizeof(item)))
		return -1;
	p=y_im_get_config_string(item,"keymap");
	if(!p) return -1;
	ps=strchr(p,' ');
	if(!ps)
	{
		l_free(p);
		return -1;
	}
	*ps=0;
	if(strlen(p)+1>len)
	{
		l_free(p);
		return -1;
	}
	strcpy(name,p);
	l_free(p);
	return 0;
}

int y_im_show_keymap(void)
{
	char item[128];
	char img[128];
	int top=0;
	int tran=0;
	char *p;
	int ret;
	if(0!=y_im_get_current(item,sizeof(item)))
		return -1;
	p=y_im_get_config_string(item,"keymap");
	if(!p) return -1;
	ret=l_sscanf(p,"%s %s %d %d",item,img,&top,&tran);
	l_free(p);
	if(ret<2) return -1;
	y_ui_show_image(item,img,top,tran);
	return 0;
}



static FILE *d_fp;
void y_im_debug(char *fmt,...)
{
	va_list ap;
	if(!d_fp)
	{
		d_fp=fopen("log.txt","w");
	}
	if(!d_fp) return;
	va_start(ap,fmt);
	vfprintf(d_fp,fmt,ap);
	va_end(ap);
	fflush(d_fp);
}

int y_im_diff_hand(char c1,char c2)
{
	const char *left="qwertasdfgzxcvb";
	if(c2==' ') return 0;
	c1=strchr(left,c1)?1:0;
	c2=strchr(left,c2)?1:0;
	return c1!=c2;
}

int y_im_request(int cmd)
{
	switch(cmd){
	case 1:
		YongKeyInput(YK_VIRT_REFRESH,0);
		break;
	default:
		break;
	};
	return 0;
}

void y_im_update_main_config(void)
{	
	l_key_file_free(MainConfig);
	MainConfig=l_key_file_open("yong.ini",1,y_im_get_path("HOME"),y_im_get_path("DATA"),NULL);

#ifndef CFG_NO_MENU
	if(!MenuConfig)
	{
		const char *p;
		l_key_file_free(MenuConfig);
		p=l_key_file_get_data(MainConfig,"main","menu");
		if(p)
			MenuConfig=l_key_file_open(p,0,y_im_get_path("HOME"),y_im_get_path("DATA"),NULL);
		if(!MenuConfig)
		{
			extern const char *ui_menu_default;
			MenuConfig=l_key_file_load(ui_menu_default,-1);
		}
	}
#endif
}

LKeyFile *y_im_get_menu_config(void)
{
	return MenuConfig;
}

void y_im_update_sub_config(const char *name)
{
	l_key_file_free(SubConfig);
	if(name)
	{
		SubConfig=l_key_file_open(name,0,y_im_get_path("HOME"),
			y_im_get_path("DATA"),NULL);
	}
	else
		SubConfig=NULL;
}

void y_im_free_config(void)
{
	if(SubConfig)
	{
		l_key_file_free(SubConfig);
		SubConfig=NULL;
	}
	if(MainConfig)
	{
		l_key_file_free(MainConfig);
		MainConfig=NULL;
	}
}

char *y_im_get_config_string(const char *group,const char *key)
{
	if(SubConfig)
	{
		char *s=l_key_file_get_string(SubConfig,group,key);
		if(s) return s;
	}
	char *res= l_key_file_get_string(MainConfig,group,key);
	return res;
}

const char *y_im_get_config_data(const char *group,const char *key)
{
	if(SubConfig)
	{
		const char *s=l_key_file_get_data(SubConfig,group,key);
		if(s) return s;
	}
	return l_key_file_get_data(MainConfig,group,key);
}

int y_im_get_config_int(const char *group,const char *key)
{
	if(SubConfig)
	{
		const char *s=l_key_file_get_data(SubConfig,group,key);
		if(s) return atoi(s);
	}
	return l_key_file_get_int(MainConfig,group,key);
}

int y_im_has_config(const char *group,const char *key)
{
	if(SubConfig)
	{
		const char *s=l_key_file_get_data(SubConfig,group,key);
		if(s) return 1;
	}
	return l_key_file_get_data(MainConfig,group,key)?1:0;
}

void y_im_verbose(const char *fmt,...)
{
	va_list ap;
	va_start(ap,fmt);
	vprintf(fmt,ap);
	va_end(ap);
}

#ifndef CFG_XIM_ANDROID
int y_im_handle_menu(const char *cmd)
{
 	if(!strcmp(cmd,"$CONFIG"))
	{
		y_im_setup_config();
	}
	else if(!strcmp(cmd,"$RELOAD"))
	{
		YongReloadAll();
	}
	else if(!strcmp(cmd,"$ABOUT"))
	{
		y_im_about_self();
	}
	else if(!strcmp(cmd,"$STAT"))
	{
		char *stat=y_im_speed_stat();
		if(stat)
		{
			y_ui_show_message(stat);
			l_free(stat);
		}
	}
	else if(!strncmp(cmd,"$HELP(",6))
	{
		char temp[64];
		l_sscanf(cmd+6,"%64[^)]",temp);
		if(!strcmp(temp,"?"))
			y_im_get_current(temp,sizeof(temp));
		y_im_show_help(temp);
	}
	else if(!strncmp(cmd,"$GO(",4))
	{
		char temp[256];
		int len;
		//l_sscanf(cmd+4,"%256[^)]",temp);
		//y_xim_explore_url(temp);
		snprintf(temp,256,"%s",cmd+4);
		len=strlen(temp);
		if(len>0 && temp[len-1]==')')
			temp[len-1]=0;
		y_xim_explore_url(temp);
	}
	else if(!strcmp(cmd,"$KEYMAP"))
	{
		y_im_show_keymap();
	}
	else if(!strcmp(cmd,"$MBO"))
	{
		int ret;
		void *out=NULL;
		ret=y_im_run_tool("tool_save_user",0,0);
		if(ret!=0) return 0;
		ret=y_im_run_tool("tool_get_file","main",&out);
		if(ret!=0 || !out) return 0;
		y_im_backup_file(out,".bak");
		y_im_run_tool("tool_optimize",0,0);
		y_ui_show_message(YT("���"));
	}
	else if(!strcmp(cmd,"$MBM"))
	{
		int ret;
		void *out=NULL;
		ret=y_im_run_tool("tool_save_user",0,0);
		if(ret!=0) return 0;
		ret=y_im_run_tool("tool_get_file","main",&out);
		if(ret!=0 || !out) return 0;
		y_im_backup_file(out,".bak");
		ret=y_im_run_tool("tool_get_file","user",&out);
		if(ret!=0 || !out) return 0;
		y_im_backup_file(out,".bak");
		ret=y_im_run_tool("tool_merge_user",0,0);
		if(ret!=0) return 0;
		y_im_remove_file(out);
		YongReloadAll();
		y_ui_show_message(YT("���"));
	}
#if !defined(CFG_NO_HELPER)
	else if(!strcmp(cmd,"$MBEDIT"))
	{
		int ret;
		void *out=NULL;
		char *ed;
		char temp[256];
		ed=y_im_get_config_string("table","edit");
		if(!ed) return 0;
		ret=y_im_run_tool("tool_get_file","main",&out);
		if(ret!=0 || !out)
		{
			l_free(ed);
			return 0;
		}
		out=y_im_auto_path(out);
		sprintf(temp,"%s %s",ed,(char*)out);
		y_im_run_helper(temp,out,YongReloadAll);
		l_free(ed);
		l_free(out);
	}
#endif
	else if(!strncmp(cmd,"$MSG(",5))
	{
		char temp[512];
		int len;
		l_utf8_to_gb(cmd+5,temp,sizeof(temp));
		len=strlen(temp);
		if(len>0)
		{
			if(temp[len-1]==')')
				temp[len-1]=0;
			y_ui_show_message(temp);
		}
	}
	return 0;
}
#endif
