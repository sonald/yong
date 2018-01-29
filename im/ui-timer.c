#include <glib.h>

#define MAX_TIMER_CB		8
#define MAX_IDLE_CB			4
typedef struct{
	void (*cb)(void *);
	void *arg;
	guint id;
}UI_CALLBACK;

static UI_CALLBACK l_timers[MAX_TIMER_CB];
static UI_CALLBACK l_idles[MAX_IDLE_CB];

static gboolean _timer(gpointer arg)
{
	int i=GPOINTER_TO_INT(arg);
	void (*cb)(void*)=l_timers[i].cb;
	arg=l_timers[i].arg;
	l_timers[i].cb=NULL;
	l_timers[i].id=0;
	if(cb)
		cb(arg);
	return FALSE;
}
static gboolean _idle(gpointer arg)
{
	int i=GPOINTER_TO_INT(arg);
	void (*cb)(void*)=l_idles[i].cb;
	arg=l_idles[i].arg;
	l_idles[i].cb=NULL;
	l_idles[i].id=0;
	if(cb)
		cb(arg);
	return FALSE;
}

static void ui_timer_del(void (*cb)(void *),void *arg)
{
	int i;
	for(i=0;i<MAX_TIMER_CB;i++)
	{
		if(l_timers[i].cb==cb && l_timers[i].arg==arg)
		{
			l_timers[i].cb=NULL;
			l_timers[i].arg=NULL;
			g_source_remove(l_timers[i].id);
			l_timers[i].id=0;
			break;
		}
	}
}

static void ui_idle_del(void (*cb)(void *),void *arg)
{
	int i;
	for(i=0;i<MAX_IDLE_CB;i++)
	{
		if(l_idles[i].cb==cb && l_idles[i].arg==arg)
		{
			l_idles[i].cb=NULL;
			l_idles[i].arg=NULL;
			g_source_remove(l_idles[i].id);
			l_idles[i].id=0;
			break;
		}
	}
}

static int ui_timer_add(unsigned interval,void (*cb)(void *),void *arg)
{
	int i;
	ui_timer_del(cb,arg);
	for(i=0;i<MAX_TIMER_CB;i++)
	{
		if(l_timers[i].cb==NULL)
			break;
	}
	if(i==MAX_TIMER_CB)
		return -1;
	l_timers[i].cb=cb;
	l_timers[i].arg=arg;
	l_timers[i].id=g_timeout_add(interval,(GSourceFunc)_timer,GINT_TO_POINTER(i));
	return 0;
}

static int ui_idle_add(void (*cb)(void *),void *arg)
{
	int i;
	ui_idle_del(cb,arg);
	for(i=0;i<MAX_IDLE_CB;i++)
	{
		if(l_idles[i].cb==NULL)
			break;
	}
	if(i==MAX_IDLE_CB)
		return -1;
	l_idles[i].cb=cb;
	l_idles[i].arg=arg;
	l_idles[i].id=g_idle_add((GSourceFunc)_idle,GINT_TO_POINTER(i));
	return 0;
}
