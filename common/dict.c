#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include "common.h"
#include "im.h"
#include "gbk.h"
#include "translate.h"
#include "ui.h"

struct y_dict{
	time_t mtime;		/* last file modify time */
	FILE *fp;			/* file handle */
	LHashTable * index;	/* index of the dict */
};

struct dict_item{
	void *next;
	uintptr_t key;
	uint32_t pos;
};

static unsigned dict_item_hash(void *p)
{
	unsigned ret;
	struct dict_item *item=p;
	uintptr_t key=item->key;
	char temp[4];
	if((uintptr_t)key & 0x01)
	{
		temp[0]=((uintptr_t)key >> 8) &0xff;
		temp[1]=((uintptr_t)key >> 16) &0xff;
		temp[2]=((uintptr_t)key >> 24) &0xff;
		key=(uintptr_t)temp;
	}
	ret=l_str_hash((char*)key);
	return ret;
}

static int dict_item_cmp(const void *v1,const void *v2)
{
	const struct dict_item *item1=v1,*item2=v2;
	if(item1->key==item2->key)
		return 0;
	if((item1->key&0x01) || (item2->key&0x01))
		return 1;
	return strcmp((char*)item1->key,(char*)item2->key);
}

static void dict_item_free(void *p)
{
	struct dict_item *item=p;
	if(!p) return;
	if(!(item->key&0x01))
		l_free((void*)item->key);
	l_free(item);
}

static void escape_space(char *s)
{
	int i;
	int len=strlen(s);
	for(i=0;i<len;i++)
	{
		if(s[i]=='$' && s[i+1]=='_')
		{
			s[i]=' ';
			i++;
			len--;
			memmove(s+i,s+i+1,len-i);
		}
	}
	s[i]=0;
}

void *y_dict_open(const char *file)
{
	struct y_dict *dic;
	FILE *fp;
	struct stat st;
	int i,len,next;
	long pos;
	char line[512],*p;
	char *key;

	fp=l_file_open(file,"rb",y_im_get_path("HOME"),
			y_im_get_path("DATA"),NULL);
	if(!fp)
	{
		//printf("open file %s fail\n",file);
		return 0;
	}
	dic=l_new(struct y_dict);
	dic->fp=fp;
	fstat(fileno(fp),&st);
	dic->mtime=st.st_mtime;
	dic->index=l_hash_table_new(7000,dict_item_hash,dict_item_cmp);

	pos=0; /* zero it here to avoid warning */
	for(i=0,next=1;;i++)
	{
		struct dict_item *item;
		if(next==1) pos=ftell(fp);
		len=l_get_line(line,512,fp);
		if(len<0) break;
		if(len==0)
		{
			next=1;
			continue;
		}
		if(next==0) continue;
		p=strchr(line,' ');
		if(p)
		{
			*p=0;
			len=strlen(line);
			if(len<1) continue;
		}
		if(len>2)
		{
			escape_space(line);
			key=l_strdup(line);
		}
		else
		{
			key=NULL;		// avoid an valgrind warning
			strcpy((char*)&key+1,line);
			*((char*)&key)=1;
		}
		item=l_new(struct dict_item);
		item->next=NULL;
		item->key=(uintptr_t)key;
		item->pos=pos;
		item=l_hash_table_replace(dic->index,item);
		dict_item_free(item);
		next=0;
	}
	return dic;
}

void y_dict_close(void *p)
{
	struct y_dict *dic=p;
	if(!p) return;
	fclose(dic->fp);
	l_hash_table_free(dic->index,dict_item_free);
	l_free(dic);
}

char *y_dict_query(void *p,char *s)
{
	int len;
	struct y_dict *dic=p;
	long pos;
	char data[8192];
	struct dict_item key,*item;
	if(!p) return 0;
	len=strlen(s);
	if(len<1) return 0;
	if(len<=2)
	{
		strcpy((char*)&key.key+1,s);
		*((char*)&key.key)=1;
	}
	else
	{
		strcpy(data,s);
		key.key=(uintptr_t)data;
	}
	item=l_hash_table_find(dic->index,&key);
	if(!item) return 0;
	pos=item->pos;
	fseek(dic->fp,pos,SEEK_SET);
	len=(int)fread(data,1,8191,dic->fp);
	if(len<1) return 0;
	data[len]=0;
	s=strstr(data,"\n\n");
	if(!s) s=strstr(data,"\r\n\r\n");
	if(s) *s=0;
	escape_space(data);
	return l_strdup(data);
}

int y_dict_query_network(char *s)
{
	char url[256];
	char temp[256];
	char *site;
	int eng=im.EnglishMode || (gb_strlen((uint8_t*)s)==strlen(s));
	if(eng)
		strcpy(temp,s);
	else
		y_im_url_encode(s,temp);
	site=y_im_get_config_string("IM",eng?"dict:en":"dict_cn");
	if(site)
	{
		sprintf(url,site,temp);
		l_free(site);
	}
	else
	{
		site=eng?"http://www.iciba.com/%s/":
				"http://hanyu.iciba.com/hy/%s/";
		sprintf(url,site,temp);
	}
	y_xim_explore_url(url);
	return 0;
}

#include <gtk/gtk.h>
typedef GtkWidget *DICT_WIDGET;

#define DICT_WIDTH		500
#define DICT_HEIGHT		400

static struct y_dict *dict;

static DICT_WIDGET l_dict;
static DICT_WIDGET l_entry;
static DICT_WIDGET l_local,l_network;
static DICT_WIDGET l_view;


void btn_query_cb(GtkButton *button,gpointer userdata)
{
	int w=GPOINTER_TO_INT(userdata);
	char *s=(char*)gtk_entry_get_text(GTK_ENTRY(l_entry));
	char gb[128];
	y_im_str_encode_r(s,gb);
	if(w==0)
	{
		char *res=y_dict_query(dict,gb);
		if(res)
		{
			char data[4096*3];
			y_im_str_encode(res,data,0);
			l_free(res);
			gtk_label_set_text(GTK_LABEL(l_view),data);
		}
		else
		{
			gtk_label_set_text(GTK_LABEL(l_view),"");
		}
	}
	else
	{
		y_dict_query_network(gb);
	}
}

static void dict_ui_creat(void)
{
	char temp[256];
	GtkWidget *w;
	double scale;
	
	scale=y_ui_get_scale();

	w=gtk_window_new(GTK_WINDOW_TOPLEVEL);
	y_im_str_encode(YT("Yong输入法"),temp,0);
	gtk_window_set_keep_above(GTK_WINDOW(w),TRUE);
	gtk_window_set_title(GTK_WINDOW(w),temp);
	gtk_window_set_resizable(GTK_WINDOW(w),FALSE);
	//gtk_widget_set_usize(GTK_WIDGET(w),DICT_WIDTH,DICT_HEIGHT);
	gtk_widget_set_size_request(GTK_WIDGET(w),DICT_WIDTH*scale,DICT_HEIGHT*scale);
	gtk_window_set_position(GTK_WINDOW(w),GTK_WIN_POS_CENTER);
	g_signal_connect(w,"delete-event",G_CALLBACK(gtk_widget_hide_on_delete),NULL);
	gtk_window_set_modal(GTK_WINDOW(w),FALSE);
	gtk_widget_realize(w);
#if defined(GSEAL_ENABLE) || GTK_CHECK_VERSION(3,0,0)
	gdk_window_set_functions(gtk_widget_get_window(w),GDK_FUNC_MOVE|GDK_FUNC_MINIMIZE|GDK_FUNC_CLOSE);
#else
	gdk_window_set_functions(w->window,GDK_FUNC_MOVE|GDK_FUNC_MINIMIZE|GDK_FUNC_CLOSE);
#endif
	l_dict=w;
	//gtk_widget_show(w);

	w=gtk_fixed_new();
	gtk_container_add(GTK_CONTAINER(l_dict),GTK_WIDGET(w));
	//gtk_widget_set_usize(GTK_WIDGET(w),DICT_WIDTH,DICT_HEIGHT);
	gtk_widget_set_size_request(GTK_WIDGET(w),DICT_WIDTH*scale,DICT_HEIGHT*scale);
	gtk_widget_show(w);

	l_entry=gtk_entry_new();
	//gtk_editable_set_editable(GTK_EDITABLE(l_entry),FALSE);
	gtk_entry_set_max_length(GTK_ENTRY(l_entry),64);
	//gtk_widget_set_usize(l_entry,DICT_WIDTH-100,26);
	gtk_widget_set_size_request(l_entry,(DICT_WIDTH-110)*scale,26*scale);
	gtk_fixed_put(GTK_FIXED(w),l_entry,0,2*scale);
	gtk_widget_show(l_entry);

	y_im_str_encode(YT("本地"),temp,0);
	l_local=gtk_button_new_with_label(temp);
	//gtk_widget_set_usize(l_local,50,26);
	gtk_widget_set_size_request(l_local,50*scale,26*scale);
	gtk_fixed_put(GTK_FIXED(w),l_local,(DICT_WIDTH-110)*scale,2*scale);
	gtk_widget_show(l_local);
	g_signal_connect (G_OBJECT(l_local), "clicked",
			G_CALLBACK (btn_query_cb), GINT_TO_POINTER(0));
	
	y_im_str_encode(YT("网络"),temp,0);
	l_network=gtk_button_new_with_label(temp);
	//gtk_widget_set_usize(l_network,50,26);
	gtk_widget_set_size_request(l_network,50*scale,26*scale);
	gtk_fixed_put(GTK_FIXED(w),l_network,(DICT_WIDTH-50)*scale,2*scale);
	gtk_widget_show(l_network);
	g_signal_connect (G_OBJECT(l_network), "clicked",
			G_CALLBACK (btn_query_cb), GINT_TO_POINTER(1));
	
	l_view=gtk_scrolled_window_new(0,0);
	//gtk_widget_set_usize(l_view,DICT_WIDTH-4,DICT_HEIGHT-32);
	gtk_widget_set_size_request(l_view,(DICT_WIDTH-4)*scale,(DICT_HEIGHT-36)*scale);
	gtk_fixed_put(GTK_FIXED(w),l_view,2*scale,34*scale);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(l_view),GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(l_view),GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_show(l_view);
	w=l_view;

	l_view=gtk_label_new("");
	//gtk_container_add(GTK_CONTAINER(w),l_view);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(w),l_view);
	gtk_label_set_selectable(GTK_LABEL(l_view),TRUE);
	gtk_misc_set_alignment (GTK_MISC(l_view),0,0);
	gtk_widget_show(l_view);
}

static void dict_ui_show(int b)
{
	if(b) gtk_widget_show(l_dict);
	else gtk_widget_hide(l_dict);
}

static void dict_ui_set_query_text(char *s)
{
	char temp[256];
	y_im_str_encode(s,temp,0);
	gtk_entry_set_text(GTK_ENTRY(l_entry),temp);
}

static void dict_ui_do_query(void)
{
	btn_query_cb(0,0);
}


int y_dict_query_and_show(void *p,char *s)
{
	dict=p;
	if(!p)
		return -1;
	if(!l_dict)
	{
		dict_ui_creat();
		if(!l_dict)
			return -1;
	}
	if(!s || !s[0])
		return 0;
	dict_ui_set_query_text(s);
	dict_ui_do_query();
	dict_ui_show(1);

	return 0;
}
