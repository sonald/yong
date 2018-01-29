#include "llib.h"
#include "ui.h"
#include "common.h"

#include <assert.h>
#include <stdarg.h>

typedef struct{
	int event,x,y,which;
}UI_EVENT;

typedef struct{
	uint8_t r,g,b,a;
}UI_COLOR;

static char skin_path[64];
static double ui_scale=1.0;


#include <gtk/gtk.h>
typedef GdkPixbuf *UI_IMAGE;
typedef GdkPixbuf *UI_ICON;
typedef PangoLayout *UI_FONT;
typedef GtkWidget *UI_WINDOW;
typedef cairo_t *UI_DC;
typedef char UI_CHAR;
#if GTK_CHECK_VERSION(3,0,0)
typedef cairo_region_t *UI_REGION;
#else
typedef GdkRegion *UI_REGION;
#endif

UI_WINDOW MainWin;

#define ui_icon_load(x) ui_image_load(x)
#define ui_icon_free(x) ui_icon_free(x)

static void image_load_cb(GdkPixbufLoader *loader,GdkPixbuf **pixbuf)
{
	if(*pixbuf)
		return;
	*pixbuf=gdk_pixbuf_loader_get_pixbuf(loader);
	g_object_ref(*pixbuf);
}

static UI_IMAGE ui_image_load(const char *file)
{
	char path[256];
	GdkPixbuf *pixbuf;
	if(!file)
	{
		return 0;
	}
	if(file[0]=='~' && file[1]=='/')
	{
		sprintf(path,"%s/%s",getenv("HOME"),file+2);
	}
	else
	{
		strcpy(path,file);
	}
	if(file[0]=='/')
	{
        	pixbuf = gdk_pixbuf_new_from_file(path, NULL);
	}
	else
	{
		char home_path[256],home_path2[256];
		char data_path[256],data_path2[256];
		char *contents;
		size_t length;
		GdkPixbufLoader *load;
		sprintf(home_path,"%s/%s",y_im_get_path("HOME"),skin_path);
		sprintf(home_path2,"%s/skin",y_im_get_path("HOME"));
		sprintf(data_path,"%s/%s",y_im_get_path("DATA"),skin_path);
		sprintf(data_path2,"%s/skin",y_im_get_path("DATA"));
		contents=l_file_get_contents(file,&length,
				home_path,home_path2,y_im_get_path("HOME"),
				data_path,data_path2,y_im_get_path("DATA"),
				NULL);
		if(!contents)
		{
			//fprintf(stderr,"load %s contents fail\n",file);
			return NULL;
		}
		load=gdk_pixbuf_loader_new();
		pixbuf=NULL;
		g_signal_connect(load,"area-prepared",G_CALLBACK(image_load_cb),&pixbuf);
		if(!gdk_pixbuf_loader_write(load,(const guchar*)contents,length,NULL))
		{
			l_free(contents);
			//fprintf(stderr,"load image %s fail\n",file);
			return NULL;
		}
		l_free(contents);
		pixbuf=gdk_pixbuf_loader_get_pixbuf(load);
		if(pixbuf) g_object_ref(pixbuf);
		gdk_pixbuf_loader_close(load,NULL);
	}
	return pixbuf;
}

UI_IMAGE ui_image_part(UI_IMAGE img,int x,int y,int w,int h)
{
	return gdk_pixbuf_new_subpixbuf(img,x,y,w,h);
}

static void ui_image_free(UI_IMAGE img)
{
	if(!img) return;
	g_object_unref(img);
}

static int ui_image_size(UI_IMAGE img,int *w,int *h)
{
	*w=gdk_pixbuf_get_width(img);
	*h=gdk_pixbuf_get_height(img);
	return 0;
}

static void ui_image_draw(UI_DC dc,UI_IMAGE img,int x,int y)
{
	int w,h;
	if(!img)
		return;
	ui_image_size(img,&w,&h);
	cairo_rectangle(dc,x,y,w,h);
	cairo_clip(dc);
	gdk_cairo_set_source_pixbuf(dc,img,x,y);
	cairo_paint(dc);
	cairo_reset_clip(dc);
}

static void ui_image_stretch(UI_DC dc,UI_IMAGE img,int x,int y,int w,int h)
{
	int w0,h0;
	double sx=1,sy=1;
	double ox=0,oy=0;

	if(!img)
		return;
	
	ui_image_size(img,&w0,&h0);
	if(w!=w0)
	{
		sx=(double)(w)/(double)(w0-1);
		ox=-0.5;
	}
	if(h!=h0)
	{
		sy=(double)h/(double)(h0-1);
		oy=-0.5;
	}
	
	cairo_save(dc);
	
	cairo_translate(dc,x,y);
	cairo_rectangle(dc,0,0,w,h);
	cairo_clip(dc);
	cairo_scale(dc,sx,sy);
	gdk_cairo_set_source_pixbuf(dc,img,ox,oy);
	cairo_paint(dc);
	
	cairo_restore(dc);
}

static UI_FONT ui_font_parse(UI_WINDOW w,const char *s)
{
	PangoFontDescription *desc;
	GdkWindow *window;
	PangoLayout *res;
	cairo_t *cr;

#if defined(GSEAL_ENABLE) || GTK_CHECK_VERSION(3,0,0)
	window=gtk_widget_get_window(w);
#else
	window=w->window;
#endif

	desc=pango_font_description_from_string(s);
	assert(desc!=NULL);
	// NOTE: GDK_DPI_SCALE not affect this, so scale the size by ourself
	if(ui_scale!=1)
	{
		gint size=pango_font_description_get_size(desc);
		pango_font_description_set_size(desc,(int)size*ui_scale);
	}
	cr=gdk_cairo_create(window);
	res=pango_cairo_create_layout(cr);
	cairo_destroy(cr);
	assert(res!=NULL);
	pango_layout_set_font_description(res,desc);
	pango_font_description_free(desc);
		
	return res;
}

static void ui_font_free(UI_FONT font)
{
	if(!font)
		return;
	g_object_unref(font);
}

static void ui_set_source_color(UI_DC dc,UI_COLOR c)
{
	double r,g,b,a;
	r=c.r/255.0;
	g=c.g/255.0;
	b=c.b/255.0;
	a=c.a/255.0;
	cairo_set_source_rgba(dc,r,g,b,a);
}

static void ui_draw_text(UI_DC dc,UI_FONT font,int x,int y,const void *text,UI_COLOR color)
{
	pango_layout_set_text (font, text, -1);
	cairo_move_to(dc,x,y);
	cairo_set_source_rgba(dc,color.r/255.0,color.g/255.0,color.b/255.0,color.a/255.0);
	pango_cairo_show_layout(dc,font);
}

static void ui_text_size(UI_DC dc,UI_FONT font,const char *text,int *w,int *h)
{
	if(!text[0])
	{
		pango_layout_set_text(font," ",-1);
		pango_layout_get_pixel_size(font,w,h);
		*w=0;
	}
	else
	{
		pango_layout_set_text(font,text,-1);
		pango_layout_get_pixel_size(font,w,h);
	}	
}

static void ui_draw_line(UI_DC dc,int x0,int y0,int x1,int y1,UI_COLOR color,double line_width)
{
	if(line_width>2) line_width=2;
	ui_set_source_color(dc,color);
	if(line_width==1 || line_width==2)
	{
		cairo_set_line_width(dc,line_width);
		cairo_move_to(dc,x0,y0);
		cairo_line_to(dc,x1,y1);
	}
	else
	{
		cairo_set_line_width(dc,1);
		cairo_move_to(dc,x0,y0);
		cairo_line_to(dc,x1,y1);
		cairo_stroke(dc);
		
		color.a*=line_width-1;
		ui_set_source_color(dc,color);
		if(y0==y1)
		{
			cairo_move_to(dc,x0,y0+1);
			cairo_line_to(dc,x1,y1+1);
		}
		else
		{
			cairo_move_to(dc,x0+1,y0);
			cairo_line_to(dc,x1+1,y1);
		}
	}
	cairo_stroke(dc);
}

static void ui_draw_rect(UI_DC dc,int x,int y,int w,int h,UI_COLOR color,double line_width)
{
	if(line_width>2) line_width=2;
	cairo_set_antialias(dc,CAIRO_ANTIALIAS_DEFAULT);
	ui_set_source_color(dc,color);
	cairo_set_line_width(dc,line_width);
	cairo_rectangle(dc,x+line_width/2,y+line_width/2,w-line_width,h-line_width);
	cairo_stroke(dc);
	cairo_set_antialias(dc,CAIRO_ANTIALIAS_NONE);
}

static void ui_fill_rect(UI_DC dc,int x,int y,int w,int h,UI_COLOR color)
{
	ui_set_source_color(dc,color);
	cairo_rectangle(dc,x,y,w,h);
	cairo_fill(dc);
}

static void UpdateMainWindow(void)
{
	gtk_widget_queue_draw(MainWin);
}

static void ui_region_destroy(UI_REGION r)
{
#if GTK_CHECK_VERSION(3,0,0)
	cairo_region_destroy(r);
#else
	gdk_region_destroy(r);
#endif
}


/*
static void ui_image_tile(UI_DC dc,UI_IMAGE img,int x,int y,int w,int h)
{
	int w0,h0,cx,cy,i,j;
	ui_image_size(img,&w0,&h0);
	cx=w/w0,cy=h/h0;
	for(j=0;j<cy;j++)
	for(i=0;i<cx;i++)
		ui_image_draw(dc,img,x+w0*i,y+h0*j);
}
*/

enum{
	UI_STATE_NORMAL=0,
	UI_STATE_DOWN,
	UI_STATE_OVER,
	UI_STATE_DISABLE,
};

enum{
	UI_EVENT_DOWN=0,
	UI_EVENT_UP,
	UI_EVENT_MOVE,
	UI_EVENT_LEAVE,
};

enum{
	UI_BUTTON_LEFT=0,
	UI_BUTTON_MIDDLE,
	UI_BUTTON_RIGHT,
};

typedef struct{
	bool visible;
	int state;
	UI_RECT rc;
	UI_IMAGE bmp[3];
	void (*click)(void *);
	void *arg;

	UI_CHAR text[32];
	UI_FONT font;
	UI_COLOR color;
}UI_BTN_REAL;

static UI_BTN_REAL btns[UI_BTN_COUNT];

static UI_IMAGE MainWin_bg;
static UI_COLOR MainWin_bgc;
static UI_COLOR MainWin_border;
static UI_RECT MainWin_move;
static int MainWin_X,MainWin_Y,MainWin_W,MainWin_H;
static int MainWin_Drag;
static int MainWin_Drag_X,MainWin_Drag_Y;
static int MainWin_tran;
static int MainWin_auto_tran;

UI_WINDOW InputWin;
static int InputWin_X,InputWin_Y;
static int InputWin_Drag;
static int InputWin_Drag_X,InputWin_Drag_Y;

struct{
	int scale;
	int CodeX,CodeY;
	int CandX,CandY;
	int OffX,OffY;
	int Height,Width;
	int mWidth,mHeight;
	int RealHeight,RealWidth;
	int Left,Right;
	int Top,Bottom;
	int WorkLeft,WorkRight;
	int MaxHeight;
	UI_IMAGE bg[3];
	UI_REGION rgn[3];
	GdkRectangle clip;
	UI_COLOR bg_color;
	UI_COLOR border;
	UI_COLOR sep;
	UI_COLOR text[7];
	UI_FONT layout;
	int space;
	int root;
	int noshow;
	int line;
	int page;
	int caret;
	int no;
	int strip;
	int x,y;
	int tran;
	double line_width;
	int onspot;
}InputTheme;

struct{
	int scale;
	double line_width;
	int move_style;
}MainTheme;

static bool ui_pt_in_rect(const UI_RECT *rc,int x,int y)
{
	return x>=rc->x && y>=rc->y &&
		x<rc->x+rc->w && y<rc->y+rc->h;
}

static UI_COLOR ui_color_parse(const char *s)
{
	UI_COLOR clr;
	int len=strlen(s);
	int a=255,r,g,b;
	if(len==9)
		l_sscanf(s,"#%02x%02x%02x%02x",&a,&r,&g,&b);
	else
		l_sscanf(s,"#%02x%02x%02x",&r,&g,&b);
	clr.a=a;clr.g=g;clr.b=b;clr.r=r;
	return clr;
}

static void ui_popup_menu(void);
static int ui_button_event(UI_WINDOW win,UI_EVENT *event,UI_BTN_REAL **under)
{
	double scale=MainTheme.scale!=1?ui_scale:1;
	UI_BTN_REAL *cur=NULL;
	UI_BTN_REAL *last=NULL;
	int dirty=0;
	int i;

	if(!MainWin_bg)
	{
		event->x=(int)(event->x/scale);
		event->y=(int)(event->y/scale);
	}
	if(event->event!=UI_EVENT_LEAVE)
	for(i=0;i<UI_BTN_COUNT;i++)
	{
		if(btns[i].visible==false)
		{
			continue;
		}
		if(ui_pt_in_rect(&btns[i].rc,event->x,event->y))
			cur=btns+i;
		if(btns[i].state!=UI_STATE_NORMAL)
			last=btns+i;
	}
	if(last!=cur)
		dirty++;
	switch(event->event){
	case UI_EVENT_DOWN:
		if(last && last!=cur && last->state!=UI_STATE_NORMAL)
		{			
			last->state=UI_STATE_NORMAL;
			dirty++;
		}
		if(cur && cur->state!=UI_STATE_DOWN)
		{
			cur->state=UI_STATE_DOWN;
			dirty++;
		}
		break;
	case UI_EVENT_UP:
		if(last && last!=cur && last->state!=UI_STATE_NORMAL)
		{
			last->state=UI_STATE_NORMAL;
			dirty++;
		}
		if(cur && cur->state!=UI_STATE_OVER && event->which==UI_BUTTON_LEFT)
		{
			cur->state=UI_STATE_OVER;
			dirty++;
			if(last==cur)
			{
				if(cur->click)
				{
					cur->click(cur->arg);
				}
				else if(cur==btns+UI_BTN_MENU)
				{
					y_im_setup_config();
				}
			}
		}
		if(cur && event->which==UI_BUTTON_RIGHT)
		{
			if(cur==btns+UI_BTN_MENU)
				ui_popup_menu();
			else if(cur==btns+UI_BTN_KEYBOARD)
				y_kbd_popup_menu();
		}
		break;
	case UI_EVENT_MOVE:
		if(last) last->state=UI_STATE_NORMAL;
		if(cur) cur->state=UI_STATE_OVER;
		break;
	case UI_EVENT_LEAVE:
		if(last)
		{
			last->state=UI_STATE_NORMAL;
			dirty++;
		}
		for(i=0;i<UI_BTN_COUNT;i++)
		{
			if(btns[i].visible==false)
			{
				continue;
			}
			if(btns[i].state!=UI_STATE_NORMAL)
			{
				btns[i].state=UI_STATE_NORMAL;
				dirty++;
			}
		}
		break;
	}
	if(dirty)
	{
		UpdateMainWindow();
	}
	if(under)
		*under=cur;
	return dirty;
}

int ui_button_update(int id,UI_BUTTON *param)
{
	UI_BTN_REAL *btn=btns+id;
	int i;

	for(i=0;i<3;i++)
	{
		if(btn->bmp[i])
		{
			ui_image_free(btn->bmp[i]);
			btn->bmp[i]=0;
		}
	}
	if(btn->font)
	{
		ui_font_free(btn->font);
		btn->font=0;
	}
	if(!param)
	{
		btn->visible=false;
		btn->rc.w=0;
		return 0;
	}
	btn->bmp[0]=ui_image_load(param->normal);
	if(param->down)
		btn->bmp[1]=ui_image_load(param->down);
	if(param->over)
		btn->bmp[2]=ui_image_load(param->over);
	if(btn->bmp[0])
	{
		int w,h;
		ui_image_size(btn->bmp[0],&w,&h);
		btn->rc.w=w;btn->rc.h=h;
	}
	else
	{
		btn->rc.w=0;
		btn->rc.h=0;
	}
	btn->visible=false;
	btn->rc.x=param->x;
	btn->rc.y=param->y;
	btn->font=param->font?ui_font_parse(MainWin,param->font):0;
	btn->color=param->color?ui_color_parse(param->color):(UI_COLOR){0,0,9,0};
	btn->click=param->click;
	btn->arg=param->arg;
	if(id==UI_BTN_MENU || id==UI_BTN_NAME || id==UI_BTN_KEYBOARD)
		ui_button_show(id,1);
	return 0;
}

static int ui_button_show(int id,int show)
{
	btns[id].visible=show;
	if(!show)
		btns[id].state=UI_STATE_NORMAL;
	UpdateMainWindow();
	return 0;
}

static int ui_button_label(int id,const char *text)
{
	if(!btns[id].rc.w)
		return 0;
	y_im_str_encode(text,btns[id].text,0);
	UpdateMainWindow();
	return 0;
}

static void ui_skin_path(const char *p)
{
	strncpy(skin_path,p,63);
	skin_path[63]=0;
}

static void ui_draw_main_win(UI_DC cr)
{
	double scale=MainTheme.scale!=1?ui_scale:1;
	int i;

	//if(MainWin_bg || MainTheme.line_width==1 || MainTheme.line_width==2)
		cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);
	
	if(MainWin_bg)
	{
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		ui_image_draw(cr,MainWin_bg,0,0);
		cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
	}
	else
	{
		double w=MainWin_W;
		double h=MainWin_H;
		
		ui_fill_rect(cr,0,0,w,h,MainWin_bgc);
		ui_draw_rect(cr,0,0,w,h,MainWin_border,MainTheme.line_width*scale);
		if(MainWin_move.w>=3 && MainWin_move.h>=3)
		{
			if(MainTheme.move_style==0)
			{
				double x=MainWin_move.x*scale+MainWin_move.w*scale/2+0.5;
				double y=MainWin_move.y*scale+0.5;
			
				ui_draw_line(cr,x,y,x,MainWin_move.h*scale+y,MainWin_border,MainTheme.line_width*scale);
				ui_draw_line(cr,x+2,y,x+2,MainWin_move.h*scale+y,MainWin_border,MainTheme.line_width*scale);
			}
			else
			{
				double x=MainWin_move.x*scale+MainWin_move.w*scale/3+0.5;
				double y=MainWin_move.y*scale+0.5;
				double d=MainWin_move.w*scale/3;

				ui_draw_line(cr,x,y,x,MainWin_move.h*scale+y,MainWin_border,MainTheme.line_width*scale);
				ui_draw_line(cr,x+d,y,x+d,MainWin_move.h*scale+y,MainWin_border,MainTheme.line_width*scale);
			}
				
		}
	}
	for(i=0;i<UI_BTN_COUNT;i++)
	{
		UI_BTN_REAL *btn=btns+i;
		UI_RECT *r=&btn->rc;
		if(!btn->visible || !r->w) continue;
		if(MainWin_bg)
		{
			if((btn->state==UI_STATE_DOWN) && btn->bmp[1])
				ui_image_draw(cr,btn->bmp[1],r->x,r->y);
			else if(btn->state==UI_STATE_OVER && btn->bmp[2])
				ui_image_draw(cr,btn->bmp[2],r->x,r->y);
			else
				ui_image_draw(cr,btn->bmp[0],r->x,r->y);
		}
		else
		{
			int x=(int)(r->x*scale);
			int y=(int)(r->y*scale);
			int w=(int)(r->w*scale);
			int h=(int)(r->h*scale);

			if((btn->state==UI_STATE_DOWN) && btn->bmp[1])
				ui_image_stretch(cr,btn->bmp[1],x,y,w,h);
			else if(btn->state==UI_STATE_OVER && btn->bmp[2])
				ui_image_stretch(cr,btn->bmp[2],x,y,w,h);
			else
				ui_image_stretch(cr,btn->bmp[0],x,y,w,h);
		}
		if(btn->text[0])
		{
			int x,y,w,h;
			UI_RECT *rc=&btn->rc;
			ui_text_size(cr,btn->font,btn->text,&w,&h);
			x=rc->x+(rc->w-w)/2;y=rc->y+(rc->h-h)/2;			
			if(!MainWin_bg)
			{
				x=(int)(x*scale);y=(int)(y*scale);
			}
			ui_draw_text(cr,btn->font,x,y,btn->text,btn->color);
		}
	}
}

static void ui_draw_input_win(UI_DC cr)
{
	double scale=InputTheme.scale!=1?ui_scale:1;
	UI_COLOR color;
	int count=0,i;
	EXTRA_IM *eim=CURRENT_EIM();
		
	cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);

	if(eim)
		count=eim->CandWordCount;

	if(!InputTheme.bg[1])
	{
		double w=InputTheme.RealWidth;
		double h=InputTheme.RealHeight;
		//int line=InputTheme.line;
		ui_fill_rect(cr,0,0,w,h,InputTheme.bg_color);
		ui_draw_rect(cr,0,0,w,h,InputTheme.border,InputTheme.line_width*scale);
	}
	else/* if(InputTheme.line!=2)*/
	{
		cairo_set_operator(cr,CAIRO_OPERATOR_SOURCE);
		if(InputTheme.bg[0])
		{
			ui_image_draw(cr,InputTheme.bg[0],0,0);
		}
		if(InputTheme.bg[2])
		{
			ui_image_draw(cr,InputTheme.bg[2],InputTheme.RealWidth-InputTheme.Right,0);
		}
		ui_image_stretch(cr,InputTheme.bg[1],
				InputTheme.Left,0,
				InputTheme.RealWidth-InputTheme.Left-InputTheme.Right,
				InputTheme.RealHeight);
		cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
	}
	
	if(InputTheme.line!=1 && InputTheme.sep.a!=0)
	{
		double w=InputTheme.RealWidth;
		double h=InputTheme.Height;
		ui_draw_line(cr,4,h/2-1,w-7,h/2-1,InputTheme.sep,InputTheme.line_width*scale);
	}

	if(eim && eim->StringGet[0] && !(InputTheme.onspot && InputTheme.line==1 && im.Preedit==1))
	{
		ui_draw_text(cr,InputTheme.layout,im.CodePos[0],InputTheme.CodeY,
			im.StringGet,InputTheme.text[6]);

	}
	if(im.CodeInput[0] && !(InputTheme.onspot && InputTheme.line==1 && im.Preedit==1))
	{
		ui_draw_text(cr,InputTheme.layout,im.CodePos[1],InputTheme.CodeY,
			im.CodeInput,InputTheme.text[5]);
	}
	if(/*!im.EnglishMode && */eim && eim->CandPageCount>1 && InputTheme.page)
	{
		double pos_x,pos_y;
		pos_x=im.PagePosX;
		pos_y=im.PagePosY;
		ui_draw_text(cr,InputTheme.layout,pos_x,pos_y,im.Page,InputTheme.text[4]);
	}
	if(InputTheme.caret)
	{
		ui_draw_line(cr,im.CodePos[2],InputTheme.CodeY+2,
					im.CodePos[2],InputTheme.CodeY+im.cursor_h,
					InputTheme.text[3],InputTheme.line_width);
	}

	for(i=0;i<count;i++)
	{
		double *posx=im.CandPosX+3*i;
		double *posy=im.CandPosY+3*i;

		if(InputTheme.no==0)
		{
			color=InputTheme.text[0];
			ui_draw_text(cr,InputTheme.layout,posx[0],posy[0],YongGetSelectNumber(i),color);
		}
		else
		{
			color=InputTheme.text[0];
		}

		if(i==eim->SelectIndex) color=InputTheme.text[1];
		ui_draw_text(cr,InputTheme.layout,posx[1],posy[1],im.CandTable[i],color);

		if(im.Hint && eim->CodeTips[i] && *eim->CodeTips[i])
		{
			color=InputTheme.text[2];
			ui_draw_text(cr,InputTheme.layout,posx[2],posy[2],im.CodeTips[i],color);
		}
	}
}
