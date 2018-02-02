#include "yong.h"
#include "mb.h"
#include "bihua.h"
#include "gbk.h"
#include "pinyin.h"
#include "learn.h"
#include "llib.h"
#include "legend.h"

static int TableInit(const char *arg);
static void TableReset(void);
static char *TableGetCandWord(int index);
static int TableGetCandWords(int mode);
static int TableDestroy(void);
static int TableDoInput(int key);
static int TableCall(int index,...);

static void PinyinReset(void);
static char *PinyinGetCandWord(int index);
static int PinyinGetCandwords(int mode);
static int PinyinDoInput(int key);

static Y_BIHUA_INFO YongBihua[32]={
	{"ɽ","252"},		//a
	{"��","34"},			//b
	{"ܳ","122"},		//c
	{"ؼ","4"},			//d
	{"��","52"},			//e
	{"��","3534"},		//f
	{"��","121"},		//g
	{"һ","1"},			//h
	{"��","3154"},		//i
	{"��","31115"},		//j
	{"��","251"},		//k
	{"��","53"},			//l
	{"ľ","1234"},		//m
	{"Ů","531"},		//n
	{"��","3511"},		//o
	{"د","3"},			//p
	{"��","353"},		//q
	{"��","2511"},		//r
	{"��","551"},		//s
	{"��","25121"},		//t
	{"ح","2"},			//u
	{"��","5"},			//v
	{"��","354"},		//w
	{"��","442"},		//x
	{"��","14524444"},	//y
	{"��","454"},		//z
	{"��","441"},		//;
	{0,0},				//'
	{0,0},				//,
	{0,0},				//.
	{"د","3"},			//
};

L_EXPORT(EXTRA_IM EIM)={
	.Name			=	"",
	.Reset			=	TableReset,
	.DoInput		=	TableDoInput,
	.GetCandWords	=	TableGetCandWords,
	.GetCandWord	=	TableGetCandWord,
	.Init			=	TableInit,
	.Destroy		=	TableDestroy,
	.Call			=	TableCall,
};

static struct y_mb *mb;

static char SimplePhrase[4][5];
static char SuperPhrase[MAX_CAND_LEN+1];
static char AutoPhrase[MAX_CAND_LEN+1];
static char CalcPhrase[Y_MB_DATA_CALC][MAX_CAND_LEN+1];
static char CodeTips[10][MAX_TIPS_LEN+1];
static char PredictPhrase[MAX_CAND_LEN+1];

static int InsertMode;

static int key_cnen;
static int key_select2;
static int key_select3;

static int key_move_up;
static int key_move_down;

static int key_zi_switch;

static int key_backspace;
static int key_replace[8];

static int key_last;

static int py_switch;
static int py_switch_save;
static int py_assist_series;

static int hz_trad;
static int hz_filter;
static int hz_filter_strict;
static int hz_filter_show=0;
static int hz_filter_temp;
static int key_filter;

static short zi_mode;

static short auto_clear;
static short auto_move;
static short auto_move_len;
static short auto_english;
static short auto_add;

static int mb_flag;

static short assoc_mode;
static short assoc_count;
static short assoc_begin;
static short assoc_move;
static short assoc_hungry;
static void *assoc_handle;

static short tip_exist;
static short tip_simple;

static short zi_output;

//static int PhraseListCount;
#define PhraseListCount EIM.CandWordTotal
static int PhraseCalcCount;
static int PredictCount;

static int SP;

static int cand_a;

typedef struct{
	uint8_t begin;
	uint8_t end;
	uint8_t user;
}AUTO_PHRASE_CONFIG;
static AUTO_PHRASE_CONFIG ap_conf;

static void ap_config_load(AUTO_PHRASE_CONFIG *c)
{
	char *tmp;
	int ret;
	tmp=EIM.GetConfig(NULL,"auto_phrase");
	if(!tmp) return;
	ret=l_sscanf(tmp,"%hhd,%hhd,%hhd",&c->begin,&c->end,&c->user);
	if(ret<2 || c->begin<2 || c->begin>c->end || c->end>9)
	{
		memset(c,0,sizeof(*c));
	}
}

static int get_key(char *name,int pos)
{
	int ret=0;
	char *tmp=name;

	if(!tmp)
		return YK_NONE;
	if(!strcmp(tmp,"CTRL"))
		tmp="LCTRL RCTRL";
	else if(!strcmp(tmp,"SHIFT"))
		tmp="LSHIFT RSHIFT";
	if(pos==-1)
	{
		ret=EIM.GetKey(tmp);
	}
	else
	{
		char *p=strtok(tmp," ");
		if(p)
		{
			if(pos==1)
				p=strtok(0," ");
			if(p)
				ret=EIM.GetKey(p);
		}
		
	}
	if(ret<0) ret=YK_NONE;
	return ret;
}

static int TableGetConfigInt(const char *section,const char *key,int def)
{
	char *res=EIM.GetConfig(section,key);
	if(!res) return def;
	return atoi(res);
}

static int TableGetConfigKey(const char *key,int pos,int def)
{
	char *tmp=EIM.GetConfig("key",key);
	int res=get_key(tmp,pos);
	if(res<=0) res=def;
	return res;
}

static void TableInitReplaceKey(void)
{
	char *replace,**list;
	int i;
	memset(key_replace,0,sizeof(key_replace));
	replace=EIM.GetConfig("key","replace");
	if(!replace) return;
	list=l_strsplit(replace,' ');
	for(i=0;i<L_ARRAY_SIZE(key_replace) && list[i]!=NULL;i++)
	{
		key_replace[i]=EIM.GetKey(list[i]);
		if(!key_replace[i])
			break;
	}
	l_strfreev(list);
}

static inline int TableReplaceKey(int key)
{
	int i,t;
	for(i=0;i<L_ARRAY_SIZE(key_replace) && (t=key_replace[i])!=YK_NONE;i+=2)
	{
		if(key==t)
			return key_replace[i+1];
	}
	return key;
}

void y_mb_init_pinyin(struct y_mb *mb)
{
	extern int l_predict_simple;
	char *name;
	if(!mb->pinyin)
		return;

	if(!EIM.GetConfig)
	{
		py_init(mb->split,NULL);
		if(mb->split=='\'')
			mb->trie=trie_tree_new();
		return;
	}
	EIM.GetCandWord=PinyinGetCandWord;
	EIM.GetCandWords=PinyinGetCandwords;
	EIM.DoInput=PinyinDoInput;
	EIM.Reset=PinyinReset;
	
	py_switch=TableGetConfigKey("py_switch",-1,'\\');
	py_switch_save=TableGetConfigInt(NULL,"py_switch_save",0);
	py_assist_series=TableGetConfigInt(NULL,"assist_series",0);
	
	name=EIM.GetConfig(0,"sp");
	if(name && name[0])
	{
		char schema[128];
		if(strcmp(name,"zrm"))
		{
			sprintf(schema,"%s/%s.sp",EIM.GetPath("HOME"),name);
			if(!l_file_exists(schema))
			{
				sprintf(schema,"%s/%s.sp",EIM.GetPath("DATA"),name);
				if(!l_file_exists(schema))
					schema[0]=0;
			}
		}
		else
		{
			strcpy(schema,"zrm");
		}
		py_init(mb->split,schema);
		if(mb->split=='\'' && schema[0])
		{
			extern int l_predict_sp;
			SP=1;
			l_predict_sp=1;
			mb->ctx.sp=1;
		}
	}
	else
	{
		py_init(mb->split,0);
	}
	name=EIM.GetConfig("pinyin","predict");
	y_mb_learn_load(mb,name);
	name=EIM.GetConfig("pinyin","simple");
	if(name)
	{
		l_predict_simple=atoi(name);
	}
	if(mb->split=='\'')
		mb->trie=trie_tree_new();
}

static Y_BIHUA_INFO *TableGetBihuaConfig(void)
{
	if(mb->yong)
		return YongBihua;
	if(strspn(mb->bihua,Y_BIHUA_KEY)==5)
	{
		int i,p;
		char c,*bihua_key=Y_BIHUA_KEY;
		memset(YongBihua,0,sizeof(YongBihua));
		for(i=0;i<5;i++)
		{
			static char *name[5]={"һ","ح","��","ؼ","��"};
			static char *code[5]={"1","2","3","4","5"};
			c=mb->bihua[i];
			p=strchr(bihua_key,c)-bihua_key;
			YongBihua[p].code=code[i];
			YongBihua[p].name=name[i];
		}
		return YongBihua;
	}
	return NULL;
}

static int TableInitReal(const char *arg)
{
	char *name;
	struct y_mb_arg mb_arg;

	if(!arg)
		return -1;

	if(TableGetConfigInt("table","adict",0))
		mb_flag|=MB_FLAG_ADICT;

	zi_mode=TableGetConfigInt("table","zi_mode",0);

	y_mb_init();
	memset(&mb_arg,0,sizeof(mb_arg));
	name=EIM.GetConfig("table","wildcard");
	if(name && !(name[0]&0x80))
	{
		mb_arg.wildcard=name[0];
	}
	mb_arg.dicts=EIM.GetConfig(NULL,"dicts");
	if(!mb_arg.dicts || !mb_arg.dicts[0])
		mb_arg.dicts=EIM.GetConfig("table","dicts");
	mb=y_mb_new();
	if(0!=y_mb_load_to(mb,arg,mb_flag,&mb_arg))
	{
		y_mb_free(mb);
		mb=NULL;
		return -1;
	}
	if(mb->cancel)
	{
		return -1;
	}
	
	strcpy(EIM.Name,mb->name);
		
	y_mb_load_fuzzy(mb,EIM.GetConfig(0,"fuzzy"));
	y_mb_load_quick(mb,EIM.GetConfig(0,"quick"));
	y_mb_load_pin(mb,EIM.GetConfig(0,"pin"));
	
	key_cnen=TableGetConfigKey("CNen",-1,YK_LCTRL);
	key_select2=TableGetConfigKey("select",0,YK_LSHIFT);
	key_select3=TableGetConfigKey("select",1,YK_RSHIFT);
	key_move_up=TableGetConfigKey("move",0,YK_NONE);
	key_move_down=TableGetConfigKey("move",1,YK_NONE);
	
	hz_trad=TableGetConfigInt(0,"trad",0);

	name=EIM.GetConfig("IM","filter");
	if(name && name[0]=='1')
	{
		hz_filter=1;
		if(name[1]==',' && name[2]=='1')
			hz_filter_strict=1;

	}
	hz_filter_temp=hz_filter;
	hz_filter_show=TableGetConfigInt("IM","filter_show",0);
	key_filter=TableGetConfigKey("filter",-1,'\\');
	auto_clear=TableGetConfigInt(0,"auto_clear",mb->auto_clear);
	auto_english=TableGetConfigInt(0,"auto_english",-1);
	if(auto_english<0)
		auto_english=TableGetConfigInt("table","auto_english",0);
	auto_move=TableGetConfigInt("IM","auto_move",mb->auto_move);
	auto_move_len=TableGetConfigInt("IM","auto_move_len",0);
	auto_add=TableGetConfigInt("IM","auto_add",mb->pinyin);
	cand_a=TableGetConfigInt("IM","cand_a",0);
	key_zi_switch=TableGetConfigKey("zi_switch",-1,YK_NONE);
	key_backspace=TableGetConfigKey("backspace",-1,YK_NONE);
	TableInitReplaceKey();
	assoc_count=TableGetConfigInt(0,"assoc_len",0);
	if(assoc_count>0)
	{
		int assoc_save;
		assoc_begin=TableGetConfigInt(0,"assoc_begin",0);
		assoc_hungry=TableGetConfigInt(0,"assoc_hungry",0);
		assoc_move=(short)TableGetConfigInt(0,"assoc_move",0);
		assoc_save=TableGetConfigInt(0,"assoc_save",0);
		name=EIM.GetConfig(0,"assoc_dict");
		if(name && name[0])
		{
			assoc_handle=y_legend_new(name,assoc_save);
		}
	}
	
	tip_exist=TableGetConfigInt(0,"tip_exist",0);
	tip_simple=TableGetConfigInt(0,"tip_simple",0);

	EIM.Bihua=TableGetBihuaConfig();
	if(!mb->english)
	{
		if(SP || (mb->pinyin && mb->split==2) || mb->capital)
		{
			EIM.Flag|=IM_FLAG_CAPITAL;
		}
	}
	if(!mb->english && !mb->pinyin && mb->rule)
	{
		ap_config_load(&ap_conf);
	}
	
	return 0;
}

static uint8_t TableReady=0;
#ifdef __linux__
#include <pthread.h>
static void *init_thread(void *arg)
{
	TableInitReal(arg);
	l_free(arg);
	TableReady=1;
	return NULL;
}
#else
#include <windows.h>
typedef HANDLE pthread_t;
#define pthread_create(a,b,c,d) \
	(*a)=CreateThread(NULL,0,(c),(d),0,0)
#define pthread_detach(a) CloseHandle(a)
#define usleep(a) Sleep(a/1000)
static DWORD WINAPI init_thread(void *arg)
{
	//DWORD start=GetTickCount();
	TableInitReal(arg);
	l_free(arg);
	TableReady=1;
	//printf("%lu\n",GetTickCount()-start);
	return 0;
}
#endif

static int TableInit(const char *arg)
{
	int thread=TableGetConfigInt(NULL,"thread",0);
	if(!thread)
	{
		TableReady=1;
		return TableInitReal(arg);
	}
	else
	{
		pthread_t th;
		pthread_create(&th,NULL,init_thread,l_strdup(arg));
		pthread_detach(th);
		return 0;
	}
}

static int TableDestroy(void)
{
	if(!TableReady)
	{
		if(mb)
		{
			mb->cancel=1;
			if(mb->ass)
				mb->ass->cancel=1;
			printf("cancel\n");
		}
		do{
			usleep(10*1000);
		}while(!TableReady);
	}
	y_mb_learn_free(NULL);
	y_mb_free(mb);
	mb=NULL;
	y_legend_free(assoc_handle);
	assoc_handle=NULL;
	y_mb_cleanup();
	return 0;
}

static void TableReset(void)
{
	EIM.CodeInput[0]=0;
	EIM.CodeLen=0;
	EIM.CurCandPage=EIM.CandPageCount=EIM.CandWordCount=0;
	EIM.SelectIndex=0;
	PhraseListCount=0;
	PhraseCalcCount=0;
	InsertMode=0;
	SuperPhrase[0]=0;
	PredictPhrase[0]=0;
	hz_filter_temp=hz_filter;
	if(TableReady)
	{
		/* clean left info at mb */
		if(!zi_mode)
		{
			y_mb_set_zi(mb,0);
		}
		mb->ctx.input[0]=0;
		mb->ctx.result_filter_ext=0;
		mb->ctx.result_match=0;
	}
	key_last=0;
	assoc_mode=0;
}

static void DoTipWhenCommit(void)
{
	if(ap_conf.begin!=0 || (tip_exist && EIM.ShowTip!=NULL))
	{
		int len=gb_strlen((uint8_t*)EIM.StringGet);
		if(len==1)
		{
			zi_output++;
			if(zi_output>9) zi_output=9;
		}
		else
		{
			zi_output=0;
			
			if(ap_conf.user && len>=ap_conf.begin && len<ap_conf.end)
			{
				struct y_mb_ci *c;
				c=y_mb_ci_exist(mb,EIM.StringGet,Y_MB_DIC_TEMP);
				if(c!=NULL)
				{
					c->dic=Y_MB_DIC_USER;
					mb->dirty++;
					y_mb_save_user(mb);
					if(EIM.ShowTip!=NULL)
						EIM.ShowTip("��ʱ��תΪ�û��ʣ�%s",EIM.StringGet);
				}
			}
		}
	}
	/* ���������������ʾ�Ѿ����ڵĴ��� */
	if(tip_exist && EIM.ShowTip!=NULL && zi_output>1)
	{
		int i;
		const char *s;
		for(i=zi_output-1;i>=1;i--)
		{
			char temp[64];
			s=EIM.GetLast(i);
			if(!s) continue;
			sprintf(temp,"%s%s",s,EIM.StringGet);
			if(y_mb_get_exist_code(mb,temp,NULL)>0)
			{
				EIM.ShowTip("������ڣ�%s",temp);
				break;
			}
		}
	}
	if(ap_conf.begin!=0)
	{
		int i;
		const char *s;
		//char tip[256];
		//tip[0]=0;
		for(i=zi_output-1;i>=ap_conf.begin-1;i--)
		{
			char temp[64];
			if(i>ap_conf.end-1)
				i=ap_conf.end-1;
			s=EIM.GetLast(i);
			if(!s) continue;
			sprintf(temp,"%s%s",s,EIM.StringGet);
			if(y_mb_get_exist_code(mb,temp,NULL)<=0)
			{
				char code[Y_MB_KEY_SIZE+1];
				if(0==y_mb_code_by_rule(mb,temp,strlen(temp),code,NULL) &&
					0==y_mb_add_phrase(mb,code,temp,Y_MB_APPEND,Y_MB_DIC_TEMP))
				{
					/*if(tip[0]==0)
					{
						strcpy(tip,EIM.Translate("�����ʱ�ʣ�"));
						strcat(tip,temp);
					}
					else
					{
						strcat(tip,"��");
						strcat(tip,temp);
					}*/
				}
			}
		}
		//if(tip[0]!=0 && EIM.ShowTip!=NULL)
		//	EIM.ShowTip(tip);
	}
	if(tip_simple && EIM.ShowTip!=NULL)
	{
		if(gb_strlen((uint8_t*)EIM.StringGet)==1)
		{
			// �������Ǽ���CalcPhrase��������Ѿ���Ч��
			int ret;
			ret=y_mb_find_simple_code(mb,EIM.StringGet,EIM.CodeInput,CalcPhrase[0],hz_filter,tip_simple);
			if(ret==1)
			{
				EIM.ShowTip("������ڣ�%s",CalcPhrase[0]);
			}
		}
	}
}

static char *TableGetCandWord(int index)
{
	char *ret;

	if(index>=EIM.CandWordCount)
		return 0;
	if(index==-1)
		index=EIM.SelectIndex;
	ret=&EIM.CandTable[index][0];
	strcpy(EIM.StringGet,ret);

	if(assoc_mode && assoc_handle && assoc_move)
	{
		y_legend_move(assoc_handle,ret);
	}

	if(auto_move && auto_move_len<=EIM.CodeLen && 
			(EIM.CurCandPage!=0 || index!=0) && 
			EIM.CodeInput[0]!=mb->lead)
	{
		char code[Y_MB_KEY_SIZE+1];
		if(y_mb_has_wildcard(mb,EIM.CodeInput))
		{
			strcpy(code,CodeTips[index]);
		}
		else
		{
			strcpy(code,EIM.CodeInput);
			strcat(code,CodeTips[index]);
		}
		y_mb_auto_move(mb,code,EIM.StringGet,auto_move);
	}
	
	DoTipWhenCommit();

	return ret;
}

static int TableGetCandWords(int mode)
{
	struct y_mb *active;
	int i,start,max;
	
	if(mode==PAGE_LEGEND)
		active=mb;
	else
		active=Y_MB_ACTIVE(mb);

	if(active==mb->ass)
		EIM.WorkMode=EIM_WM_ASSIST;
	max=EIM.CandWordMax;
	if(mb->ass==active && cand_a)
		max=cand_a;
	if(mode==PAGE_FIRST)
	{
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
	}	
	if(mode==PAGE_LEGEND)
	{
		const char *from;
		int from_len;
		from=EIM.StringGet;
		if(from[0]=='$' && from[1]=='[')
		{
			from=gb_strchr((const uint8_t*)from,']');
			if(!from)
				return IMR_BLOCK;
			from++;
		}
		from_len=gb_strlen((uint8_t*)from);
		if(from_len<assoc_begin)
		{
			if(assoc_hungry>=assoc_begin)
				from_len=assoc_begin;
			else
				return IMR_BLOCK;
		}
		i=MAX(assoc_hungry,from_len);
		PhraseCalcCount=0;
		for(;i>=from_len && !PhraseCalcCount;i--)
		{
			from=EIM.GetLast(i);
			if(!from) continue;
			if(assoc_handle)
			{
				PhraseCalcCount=y_legend_get(
					assoc_handle,from,strlen(from),
					assoc_count,CalcPhrase,Y_MB_DATA_CALC);
			}
			else
			{
				PhraseCalcCount=y_mb_get_legend(
					mb,from,strlen(from),
					assoc_count,CalcPhrase,Y_MB_DATA_CALC);
			}
		}
		if(!assoc_hungry)
			PhraseCalcCount+=EIM.QueryHistory(EIM.StringGet,CalcPhrase+PhraseCalcCount,Y_MB_DATA_CALC-PhraseCalcCount);
		if(!PhraseCalcCount)
			return IMR_BLOCK;
		EIM.CodeLen=0;
		EIM.CodeInput[0]=0;
		strcpy(EIM.StringGet,EIM.Translate(hz_trad?"���P���~��":"���룺"));
		PhraseListCount=PhraseCalcCount;
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
		mode=PAGE_FIRST;
		assoc_mode=1;
	}

	if(mode==PAGE_FIRST) EIM.CurCandPage=0;
	else if(mode==PAGE_NEXT && EIM.CurCandPage+1<EIM.CandPageCount)
		EIM.CurCandPage++;
	else if(mode==PAGE_PREV && EIM.CurCandPage>0)
		EIM.CurCandPage--;

	if(EIM.CandPageCount==0)
		EIM.CandWordCount=0;
	else if(EIM.CurCandPage<EIM.CandPageCount-1)
		EIM.CandWordCount=max;
	else EIM.CandWordCount=PhraseListCount-max*(EIM.CandPageCount-1);

	start=EIM.CurCandPage*max;

	for(i=0;i<max;i++)
	{
		CodeTips[i][0]=0;
		EIM.CodeTips[i][0]=0;
	}

	if(PhraseCalcCount)
	{
		for(i=0;i<EIM.CandWordCount;i++)
			strcpy(EIM.CandTable[i],CalcPhrase[start+i]);
	}
	else if(mb->pinyin && EIM.CandWordCount && PredictPhrase[0])
	{
		int pos=start,count=EIM.CandWordCount,plus=0;
		if(start==0)
		{
			strcpy(EIM.CandTable[0],PredictPhrase);
			count--;
			plus++;
		}
		else
		{
			pos--;
		}
		y_mb_get(mb,pos,count,EIM.CandTable+plus,CodeTips+plus);
	}
	else if(EIM.CandWordCount)
	{
		y_mb_get(mb,start,EIM.CandWordCount,EIM.CandTable,CodeTips);
	}
	if(EIM.SelectIndex>=EIM.CandWordCount)
		EIM.SelectIndex=0;
	
	if(active->hint!=0)
	for(i=0;i<EIM.CandWordCount;i++)
		strcpy(EIM.CodeTips[i],CodeTips[i]);
	if(active==mb->ass) /* it's assist mb */
	{
		/* hint the main mb's code */
		for(i=0;i<EIM.CandWordCount;i++)
		{
			int len;
			char *data=EIM.CandTable[i];
			EIM.CodeTips[i][0]=0;
			len=strlen(data);
			if((len==2 && gb_is_gbk((uint8_t*)data)) ||
					(len==4 && gb_is_gb18030_ext((uint8_t*)data)))
			{
				y_mb_get_full_code(mb,data,EIM.CodeTips[i]);
			}
			else
			{
				y_mb_get_exist_code(mb,data,EIM.CodeTips[i]);
			}
		}
	}
	else if(active==mb->quick_mb)
	{
		for(i=0;i<EIM.CandWordCount;i++)
		{
			EIM.CodeTips[i][0]=0;
		}
	}

	if(active==mb && mb->sloop)
	{
		int prev_tip_len=-1;
		int sloop=EIM.CandWordCount;
		if(mb->sloop>1)
			sloop=MIN(EIM.CandWordCount,mb->sloop);
		SimplePhrase[EIM.CodeLen][0]=0;
		for(i=0;i<sloop;i++)
		{
			int len;
			char *data=EIM.CandTable[i];
			char *tip=EIM.CodeTips[i];
			len=strlen(data);
			if(!(len==2 && gb_is_gbk((uint8_t*)data)))
				break;
			len=strlen(tip);
			if(!prev_tip_len && len)
				break;
		}
		if(i==1 || (i>1 && EIM.CodeLen==1))
		{
			strcpy(SimplePhrase[EIM.CodeLen-1],EIM.CandTable[0]);
		}
		else if(i>0)
		{
			int count=i,j;
			for(i=0;i<count;i++)
			{
				for(j=0;j<EIM.CodeLen-1;j++)
				{
					if(!strcmp(SimplePhrase[j],EIM.CandTable[i]))
					{
						break;
					}
				}
				if(j==EIM.CodeLen-1) /* if not prev out */
				{
					/* not self and  c[0] is not full or c[j] is full here */
					//printf("%s %s\n",EIM.CodeTips[0],EIM.CodeTips[i]);
					if(i!=0 && (EIM.CodeTips[0][0] || !EIM.CodeTips[i][0]))
					{
						char temp[MAX_CAND_LEN+1];
						strcpy(temp,EIM.CandTable[i]);
						strcpy(EIM.CandTable[i],EIM.CandTable[0]);
						strcpy(EIM.CandTable[0],temp);
						strcpy(temp,EIM.CodeTips[i]);
						strcpy(EIM.CodeTips[i],EIM.CodeTips[0]);
						strcpy(EIM.CodeTips[0],temp);
					}
					break;
				}
			}
			strcpy(SimplePhrase[EIM.CodeLen-1],EIM.CandTable[0]);
		}
	}
	if(mb->yong && (EIM.CodeLen==2 || EIM.CodeLen==3))
	{
		for(i=!EIM.CurCandPage;i<EIM.CandWordCount;i++)
		{
			if(CodeTips[i][0]) continue;
			y_mb_calc_yong_tip(mb,EIM.CodeInput,EIM.CandTable[i],EIM.CodeTips[i]);
		}
	}
	if(InsertMode)
	{
		int pos=EIM.CurCandPage*EIM.CandWordMax+EIM.SelectIndex;
		if(0==y_mb_code_by_rule(mb,CalcPhrase[pos],strlen(CalcPhrase[pos]),EIM.CodeInput,NULL))
			EIM.CodeLen=strlen(EIM.CodeInput);
	}

	return IMR_DISPLAY;
}

static int TableOnVirtQuery(const char *ph)
{
	int ret,count;
	char tmp[Y_MB_DATA_SIZE+1];
	
	if(!ph)
		return IMR_BLOCK;
	
	count=gb_strlen((uint8_t*)ph);
	strcpy(tmp,ph);
	if(count==1)
		ret=y_mb_find_code(mb,tmp,CalcPhrase,Y_MB_DATA_CALC);
	else if(count>1)
		ret=y_mb_get_exist_code(mb,tmp,CalcPhrase[0]);
	else
		ret=0;
	if(ret)
	{
		EIM.WorkMode=EIM_WM_QUERY;
		strcpy(EIM.StringGet,EIM.Translate("���飺"));
		strcpy(EIM.CodeInput,tmp);
		EIM.CaretPos=-1;
		PhraseCalcCount=PhraseListCount=ret;
		TableGetCandWords(PAGE_FIRST);
		return IMR_DISPLAY;
	}
	else
	{
		return IMR_BLOCK;
	}
}

static int TableOnVirtAdd(const char *ph)
{
	PhraseCalcCount=0;
	if(ph)
	{
		strcpy(CalcPhrase[PhraseCalcCount++],ph);
	}
	if(EIM.DoInput==TableDoInput)
	{
		int i;
		for(i=2;i<10;i++)
		{
			const char *p=EIM.GetLast(i);
			if(!p) break;
			strcpy(CalcPhrase[PhraseCalcCount++],p);
		}
	}
	if(PhraseCalcCount)
	{
		//int max;
		strcpy(EIM.StringGet,EIM.Translate(hz_trad?"���a��":"���룺"));
		PhraseListCount=PhraseCalcCount;
		TableGetCandWords(PAGE_FIRST);
		InsertMode=1;
		if(0==y_mb_code_by_rule(mb,CalcPhrase[0],strlen(CalcPhrase[0]),EIM.CodeInput,NULL))
			EIM.CodeLen=strlen(EIM.CodeInput);
		return IMR_DISPLAY;
	}
	else
	{
		return IMR_NEXT;
	}
}

static int TableDoInput(int key)
{
	int ret=IMR_DISPLAY;
	struct y_mb *active_mb;
	int bing,space=0;
	
	if(!TableReady)
		return IMR_NEXT;

	bing=key&KEYM_BING;
	key&=~KEYM_BING;
	if(bing && key_last==key)
		space=1;
	key_last=key;
	if(key==key_backspace)
		key='\b';
	key=TableReplaceKey(key);

	if(key=='\b')
	{
		if(assoc_mode || EIM.WorkMode==EIM_WM_QUERY)
			return IMR_CLEAN;
		if(EIM.CodeLen==0)
			return IMR_NEXT;
		EIM.CodeLen--;
		if(EIM.CodeInput[EIM.CodeLen]&0x80 && EIM.CodeLen>0)
			EIM.CodeLen--;
		EIM.CodeInput[EIM.CodeLen]=0;
		if(EIM.CodeLen==0 && EIM.StringGet[0]==0)
			return IMR_CLEAN;
		if(hz_filter_show) hz_filter_temp=hz_filter;
		goto LIST;
	}
	else if(key==YK_ENTER && InsertMode)
	{
		y_mb_add_phrase(mb,EIM.CodeInput,EIM.CandTable[EIM.SelectIndex],0,Y_MB_DIC_USER);
		TableReset();
		return IMR_CLEAN;
	}
	else if(InsertMode && (key==YK_UP || key==YK_DOWN))
	{
		int pos;
		if(key==YK_UP)
		{
			if(EIM.SelectIndex>0)
			{
				EIM.SelectIndex=EIM.SelectIndex-1;
			}
		}
		else if(key==YK_DOWN)
		{
			if(EIM.SelectIndex<EIM.CandWordCount-1)
			{
				EIM.SelectIndex=EIM.SelectIndex+1;
			}
		}
		pos=EIM.CurCandPage*EIM.CandWordMax+EIM.SelectIndex;
		if(0==y_mb_code_by_rule(mb,CalcPhrase[pos],strlen(CalcPhrase[pos]),EIM.CodeInput,NULL))
				EIM.CodeLen=strlen(EIM.CodeInput);
	}
	else if(key==YK_VIRT_REFRESH)
	{
		if(y_mb_is_keys(mb,EIM.CodeInput))
			goto LIST;
		return IMR_NEXT;
	}
	else if(key==YK_VIRT_ADD)
	{
		char *ph=NULL;
		if(InsertMode || EIM.CodeLen)
			return IMR_BLOCK;
		if(EIM.GetSelect!=NULL)
		{
			ph=EIM.GetSelect(TableOnVirtAdd);
			if(ph==NULL)
				return IMR_BLOCK;
		}
		return TableOnVirtAdd(ph);
	}
	else if(key==YK_VIRT_DEL)
	{
		char code[Y_MB_KEY_SIZE+1];
		int ret;
		if(InsertMode)
			return IMR_BLOCK;
		if(EIM.CodeLen==0)
			return IMR_NEXT;
		if(y_mb_has_wildcard(mb,EIM.CodeInput))
		{
			strcpy(code,CodeTips[EIM.SelectIndex]);
		}
		else
		{
			strcpy(code,EIM.CodeInput);
			strcat(code,CodeTips[EIM.SelectIndex]);
		}
		ret=y_mb_del_phrase(mb,code,EIM.CandTable[EIM.SelectIndex]);
		if(ret==-1)
			return IMR_BLOCK;
		TableReset();
		return IMR_CLEAN;
	}
	else if(key==YK_VIRT_QUERY)
	{
		char *ph=NULL;
		if(EIM.CandWordCount)
			ph=EIM.CandTable[EIM.SelectIndex];
		else if(!EIM.CodeLen && EIM.GetSelect)
		{
			ph=EIM.GetSelect(TableOnVirtQuery);
			if(ph==NULL)
				return IMR_BLOCK;
		}
		return TableOnVirtQuery(ph);
	}
	else if(key==YK_VIRT_TIP)
	{
		int i=0;
		for(i=0;i<EIM.CandWordCount;i++)
		{
			if(gb_strlen((uint8_t*)EIM.CandTable[i])!=1)
				continue;
			y_mb_get_full_code(mb,EIM.CandTable[i],EIM.CodeTips[i]);
		}
		return IMR_DISPLAY;
	}
	else if(key==key_move_up || key==key_move_down)
	{
		char code[Y_MB_KEY_SIZE+1];
		int ret;
		if(InsertMode)
			return IMR_BLOCK;
		if(EIM.CandWordCount==0)
			return IMR_NEXT;
		if(y_mb_has_wildcard(mb,EIM.CodeInput))
		{
			strcpy(code,CodeTips[EIM.SelectIndex]);
		}
		else
		{
			strcpy(code,EIM.CodeInput);
			strcat(code,CodeTips[EIM.SelectIndex]);
		}
		ret=y_mb_move_phrase(mb,code,EIM.CandTable[EIM.SelectIndex],(key==key_move_up)?-1:1);
		if(ret==-1)
			return IMR_BLOCK;
		if(CodeTips[EIM.SelectIndex][0]=='\0')
		{
			char phrase[Y_MB_DATA_SIZE+1];
			int pos=EIM.CandWordMax*EIM.CurCandPage+EIM.SelectIndex;
			y_mb_get(mb,pos,1,&phrase,&code);
			if(strcmp(EIM.CandTable[EIM.SelectIndex],phrase))
			{
				if(key==key_move_up)
				{
					if(EIM.SelectIndex>0)
					{
						EIM.SelectIndex--;
					}
					else
					{
						EIM.CurCandPage--;
						EIM.SelectIndex=EIM.CandWordMax-1;
					}
				}
				else
				{
					if(EIM.SelectIndex==EIM.CandWordMax-1)
					{
						EIM.SelectIndex=0;
						EIM.CurCandPage++;
					}
					else
					{
						EIM.SelectIndex++;
					}
				}
			}
		}
		TableGetCandWords(PAGE_REFRESH);
		return IMR_DISPLAY;
	}
	else if(hz_filter && key==key_filter)
	{
		if(InsertMode || EIM.CodeLen==0)
			return IMR_NEXT;
		if(!hz_filter_temp)
			hz_filter_temp=hz_filter;
		else
			hz_filter_temp=0;
		goto LIST;
	}
	else if(mb->yong && bing && space && EIM.CodeLen==3)
	{
		char code[2]={EIM.CodeInput[EIM.CodeLen-1],0};
		EIM.CodeInput[--EIM.CodeLen]=0;
		ret=y_mb_get_simple(mb,EIM.CodeInput,EIM.StringGet,EIM.CodeLen);
		if(ret<0) return IMR_CLEAN;
		ret=y_mb_get_simple(mb,code,EIM.StringGet+strlen(EIM.StringGet),1);
		if(ret<0) return IMR_CLEAN;
		return IMR_COMMIT;
	}
	else if(mb->yong && key==YK_TAB && mb->commit_which && mb->commit_which<EIM.CodeLen)
	{
		active_mb=Y_MB_ACTIVE(mb);
		goto commit_simple;
	}
	else if(y_mb_is_key(mb,key) || (key==mb->lead && EIM.CodeLen==0) ||
			(key==mb->quick_lead && EIM.CodeLen==0))
	{
		int count;

		if(EIM.CodeLen>=MAX_CODE_LEN)
			return IMR_BLOCK;
		if(!EIM.CodeLen && !InsertMode)
		{
			PhraseCalcCount=0;
			EIM.StringGet[0]=0;
			EIM.SelectIndex=0;
			mb->ctx.input[0]=0;
			assoc_mode=0;
		}
		/* make sure ? will first to be the biaodian */
		if(key==mb->wildcard && key=='?' && EIM.CodeLen==0)
			return IMR_NEXT;

		active_mb=Y_MB_ACTIVE(mb);
		
		if(mb==active_mb && mb->yong && !InsertMode)
		{
			if(EIM.CodeLen==4 && key=='\'')
			{
				//int max;
				count=mb->ctx.result_count_zi;
				if(!count)
					return IMR_BLOCK;
				if(count>=2 && (mb->ctx.result_filter_zi || mb->ctx.result_count==count))
				{
					strcpy(EIM.StringGet,EIM.CandTable[1]);
					return IMR_COMMIT;
				}
				y_mb_set_zi(mb,1);
				PhraseListCount=count;
				TableGetCandWords(PAGE_FIRST);
				return IMR_DISPLAY;
			}
			else if(EIM.CodeLen==4)
			{
				strcpy(SuperPhrase,EIM.CandTable[EIM.SelectIndex]);
				PhraseCalcCount=y_mb_super_get(mb,CalcPhrase,Y_MB_DATA_CALC,key);
				if(PhraseCalcCount)
				{
					EIM.CodeInput[EIM.CodeLen++]=key;
					EIM.CodeInput[EIM.CodeLen]=0;
					PhraseListCount=PhraseCalcCount;
					TableGetCandWords(PAGE_FIRST);
					return IMR_DISPLAY;
				}
			}
			else if(EIM.CodeLen==5)
			{
				char temp_code[3];
				int temp_count;
				temp_code[0]=EIM.CodeInput[4];temp_code[1]=key;temp_code[2]=0;
				temp_count=y_mb_set(mb,temp_code,2,hz_filter_temp);
				if(temp_count)
				{
					strcpy(EIM.StringGet,SuperPhrase);
					EIM.CodeInput[0]=EIM.CodeInput[4];
					EIM.CodeInput[1]=0;
					EIM.CodeLen=1;
				}
				else
				{
					strcpy(EIM.StringGet,EIM.CandTable[EIM.SelectIndex]);
					EIM.CodeLen=0;
				}
				PhraseCalcCount=0;
				ret=IMR_COMMIT_DISPLAY;
			}
			else
			{
				y_mb_set_zi(mb,0);
			}
		}
		if(InsertMode)
		{
			if(key==mb->wildcard)
				return IMR_BLOCK;
		}

		EIM.CodeInput[EIM.CodeLen++]=key;
		EIM.CodeInput[EIM.CodeLen]=0;

LIST:
		if(InsertMode)
			return IMR_DISPLAY;
		active_mb=Y_MB_ACTIVE(mb);
		y_mb_set_zi(mb,zi_mode & 0x01);
		y_mb_set_ci_ext(mb,zi_mode & 0x01);
		
		mb->ctx.result_filter_ext=0;
		if(hz_filter_show && hz_filter && !hz_filter_temp)
		{
			/* ֻ��ʾ�ǳ��ú��֣�����ʹ��result_match�Ի���ȷ��ƥ�� */
			int c1,c2;
			if(mb->match) mb->ctx.result_match=1;
			c1=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,1);
			c2=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,0);
			mb->ctx.result_match=0;
			if(c2>c1)
			{
				count=c2-c1;
				mb->ctx.result_filter_ext=1;
			}
			else if(c2==0)
			{
				/* ����������룬������ͨmatch=1�²�ѯ���Ա�����ǳ������ܴ�� */
				c1=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,1);
				c2=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,0);
				if(c2>c1)
				{
					count=c2-c1;
					mb->ctx.result_filter_ext=1;
				}
				else
				{
					count=c2;
					mb->ctx.result_filter_ext=0;
				}
			}
			else
			{
				count=c2;
				mb->ctx.result_filter_ext=0;
			}
		}
		else /* normal mode */
		{
			count=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,hz_filter_temp);
		}
		if(hz_filter_temp && !count && !hz_filter_strict)
		{
			hz_filter_temp=0;
			count=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,hz_filter_temp);
			if(!count && hz_filter_show)
				hz_filter_temp=hz_filter;
		}
		if(count<=0 && key==YK_VIRT_REFRESH)
		{
			return IMR_ENGLISH;
		}
		//todo: if ass mode, don't commit auto
		while(count<=0)
		{
			int full=y_mb_is_full(mb,EIM.CodeLen);
			if(key==key_select2 || key==key_select3 || strchr(mb->key0,key))
			{
				EIM.CodeInput[--EIM.CodeLen]=0;
				return IMR_NEXT;
			}
			if(y_mb_has_wildcard(mb,EIM.CodeInput) && EIM.CodeLen<=mb->len)
			{
				PhraseListCount=0;
				TableGetCandWords(PAGE_FIRST);
				ret=IMR_COMMIT_DISPLAY;
				break;
			}
			if(auto_english && (auto_english==2 || EIM.CodeLen<=mb->len))
			{
				if(EIM.Beep) EIM.Beep(YONG_BEEP_EMPTY);
				//ret=IMR_ENGLISH;
				ret=IMR_CHINGLISH;
				break;
			}
			if(mb->english && key!=YK_SPACE)
			{
				ret=IMR_CHINGLISH;
				break;
			}
			if(auto_clear>1)
			{
				 if(EIM.CodeLen<auto_clear)
				 {
					if(EIM.CodeLen==1)
						return IMR_CLEAN_PASS;
					if(EIM.Beep) EIM.Beep(YONG_BEEP_EMPTY); 
					PhraseListCount=0;
					TableGetCandWords(PAGE_FIRST);
					return IMR_DISPLAY;
				}
				else if(EIM.CodeLen==auto_clear && auto_clear==mb->len)
				{
					if(EIM.Beep) EIM.Beep(YONG_BEEP_EMPTY);
					if(mb->commit_mode!=1)
					{
						return IMR_CLEAN;
					}
					else
					{
						PhraseListCount=0;
						TableGetCandWords(PAGE_FIRST);
						return IMR_DISPLAY;
					}
				}
				else if (EIM.CodeLen==auto_clear && auto_clear==mb->len+1)
				{
					TableReset();
					EIM.CodeInput[EIM.CodeLen++]=key;
					EIM.CodeInput[EIM.CodeLen]=0;
					goto LIST;
				}
			}
			if(full>=3 && mb->commit_which<EIM.CodeLen)
			{
				int which=mb->commit_which?:mb->len;
				int len=EIM.CodeLen-which;
				char temp_code[len+1];
				int temp_count;
				strcpy(temp_code,EIM.CodeInput+which);
				temp_count=y_mb_set(mb,temp_code,len,hz_filter_temp);
				if(temp_count)
				{
					strcpy(EIM.CodeInput,temp_code);
					EIM.CodeLen=len;
					count=temp_count;
					strcpy(EIM.StringGet,SuperPhrase);
					SuperPhrase[0]=0;
					PhraseCalcCount=0;
					ret=IMR_COMMIT_DISPLAY;
					break;
				}
			}
			/*if(full==3)
			{
				char temp_code[3];
				int temp_count;
				strcpy(temp_code,EIM.CodeInput+EIM.CodeLen-2);
				temp_count=y_mb_set(mb,temp_code,2,hz_filter_temp);
				if(temp_count)
				{
					strcpy(EIM.CodeInput,temp_code);
					EIM.CodeLen=2;
					count=temp_count;
					strcpy(EIM.StringGet,SuperPhrase);
					SuperPhrase[0]=0;
					PhraseCalcCount=0;
					ret=IMR_COMMIT_DISPLAY;
					break;
				}
			}*/
			EIM.CodeInput[--EIM.CodeLen]=0;
			if(EIM.CodeLen==0)
			{
				ret=IMR_NEXT;
			}
			/*else if(key==mb->wildcard)
			{
				ret=IMR_BLOCK;
			}*/
			else if((mb->commit_len && EIM.CodeLen<mb->commit_len) || 
					(auto_clear && !mb->commit_len && EIM.CodeLen<mb->len))
			{
				if(EIM.Beep) EIM.Beep(YONG_BEEP_EMPTY);
				if(auto_clear)
				{
					EIM.CodeInput[0]=0;
					EIM.CodeLen=0;
					ret=IMR_CLEAN;
				}
				else
				{
					ret=IMR_BLOCK;
				}
			}
			else if(full<=1 && mb->commit_which && mb->commit_which<EIM.CodeLen)
			{
				char temp[MAX_CODE_LEN+1];
				int temp_count;
				int temp_len;
				struct y_mb_context ctx;
commit_simple:
				strcpy(temp,EIM.CodeInput+mb->commit_which);
				temp_len=EIM.CodeLen-mb->commit_which;
				if(key!=YK_TAB)
				{
					temp[temp_len++]=key;
					temp[temp_len]=0;
				}
				temp_count=y_mb_set(mb,temp,temp_len,hz_filter_temp);
				if(!temp_count)
					return IMR_BLOCK;
				PhraseCalcCount=0;
				ret=IMR_COMMIT_DISPLAY;
				if(mb->commit_which<=2)
				{
					y_mb_get_simple(mb,EIM.CodeInput,EIM.StringGet,mb->commit_which);
				}
				else
				{
					strcpy(EIM.StringGet,AutoPhrase);
				}
				y_mb_push_context(mb,&ctx);
				TableReset();
				y_mb_pop_context(mb,&ctx);
				strcpy(EIM.CodeInput,temp);
				EIM.CodeLen=temp_len;
				count=temp_count;
			}
			else
			{
				char temp[2];
				temp[0]=(char)key;
				temp[1]=0;
				count=y_mb_set(mb,temp,1,hz_filter_temp);
				if(!count)
				{
					strcpy(EIM.StringGet,EIM.CandTable[EIM.SelectIndex]);
					ret=IMR_PUNC;
				}
				else
				{
					struct y_mb_context ctx;
					PhraseCalcCount=0;
					if(EIM.CandWordCount>0)
					{
						ret=IMR_COMMIT_DISPLAY;
						strcpy(EIM.StringGet,EIM.CandTable[EIM.SelectIndex]);
						if(EIM.SelectIndex!=0 && auto_move && EIM.CodeLen>=auto_move_len)
						{
							y_mb_auto_move(mb,EIM.CodeInput,EIM.StringGet,auto_move);
						}
					}
					else
					{
						ret=IMR_DISPLAY;
					}
					y_mb_push_context(mb,&ctx);
					TableReset();
					y_mb_pop_context(mb,&ctx);
					EIM.CodeInput[EIM.CodeLen++]=key;
					EIM.CodeInput[EIM.CodeLen]=0;
				}
			}
			break;
		}
		if(count>0)
		{
			if(count<0) count=0;
			int dext=hz_filter_strict && mb->ctx.result_filter;
			if(hz_filter && hz_filter_temp==0 && hz_filter_show) dext=2;
			PhraseCalcCount=0;
			PhraseListCount=count;
			TableGetCandWords(PAGE_FIRST);
			if(mb->commit_which==EIM.CodeLen)
			{
				strcpy(AutoPhrase,EIM.CandTable[EIM.SelectIndex]);
			}
			int full=y_mb_is_full(mb,EIM.CodeLen);
			if(full)
			{
				if(full==1)
					strcpy(SuperPhrase,EIM.CandTable[EIM.SelectIndex]);
				else if(EIM.CodeLen==mb->commit_which)
					strcpy(SuperPhrase,EIM.CandTable[EIM.SelectIndex]);
				// is not exact match, set full to 0
				if(CodeTips[EIM.SelectIndex][0])
					full=0;
			}
			if(full)
			{
				char *p=EIM.CandTable[EIM.SelectIndex];
				if(p[0]=='$' && !strcmp(p,"$ENGLISH"))
					return IMR_ENGLISH;
			}
			int stop=0;
			if(count==1 && !CodeTips[0][0] && !y_mb_has_next(mb,dext))
			{
				if(y_mb_is_stop(mb,key,EIM.CodeLen))
				{
					stop=1;
				}
				if(!stop && y_mb_is_pull(mb,EIM.CodeInput[0]))
				{
					stop=1;
				}
				if(!stop && l_str_has_suffix(EIM.CandTable[0],"$SPACE"))
				{
					char *p=EIM.CandTable[0];
					size_t len=strlen(p);
					if(len!=6 && !strstr(p,"$$"))
					{
						stop=1;
						p[len-6]=0;
					}
				}
			}
			/*
			 * not wildcard and only one cand and not assist mode and no left code to input
			 * 1 stop!=0 there is pull or push char
			 * 2 super mode only at CodeLen==mb->len
			 * 3 in not super mode and CodeLen>=mb->len
			 */
			if((stop || (full==1 && active_mb->commit_mode==0) ||
					(full>=1 && active_mb->commit_mode==2)) &&
					PhraseListCount==1 && !mb->ctx.result_wildcard && !y_mb_before_assist(mb) && !y_mb_has_next(mb,dext))
			{
				strcat(EIM.StringGet,EIM.CandTable[EIM.SelectIndex]);
				ret=IMR_COMMIT;
				DoTipWhenCommit();
			}
			if(ret==IMR_DISPLAY && PhraseListCount>1 && EIM.CodeLen>=mb->len && active_mb==mb)
			{
				if(EIM.Beep) EIM.Beep(YONG_BEEP_MULTI);
			}
		}
	}
	else if(bing && mb && key=='+' && EIM.CodeLen==2)
	{
		strcat(EIM.StringGet,EIM.CandTable[EIM.SelectIndex]);
		if(mb->yong && gb_strlen((uint8_t*)EIM.StringGet)==1)
		{
			int count;
			EIM.CodeInput[2]='\'';
			EIM.CodeInput[3]=0;
			EIM.CodeLen=3;
			count=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,0);
			if(count>0)
			{
				PhraseCalcCount=0;
				PhraseListCount=count;
				//max=EIM.CandWordMax;
				//EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
				TableGetCandWords(PAGE_FIRST);
				strcpy(EIM.StringGet,EIM.CandTable[0]);
			}
		}
		ret=IMR_COMMIT;
	}
	else if((key==SHIFT_ENTER || key=='\r') && EIM.CandWordCount && mb->english)
	{
		int c;
		strcpy(EIM.StringGet,EIM.CandTable[EIM.SelectIndex]);
		c=EIM.StringGet[0];
		if(key!='\r' && c>='a' && c<='z')
			EIM.StringGet[0]=c-'a'+'A';
		ret=IMR_COMMIT;
	}
	else if(key==key_zi_switch && (EIM.CodeLen>0 || (key&KEYM_MASK)))
	{
		//int max;
		int count=mb->ctx.result_count_zi;
		if(zi_mode & 0x02)
		{
			zi_mode&=0x01;
			zi_mode=!zi_mode;
			zi_mode|=0x02;
			return IMR_CLEAN;
		}
		if(!count)
			return IMR_NEXT;
		y_mb_set_zi(mb,1);
		PhraseListCount=count;
		//max=EIM.CandWordMax;
		//EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
		TableGetCandWords(PAGE_FIRST);
		return IMR_DISPLAY;
	}
	else if(EIM.CandWordCount==0 && EIM.CodeLen>0 && auto_clear>1 && key==' ')
	{
		ret=IMR_CLEAN;
	}
	else
	{
		ret=IMR_NEXT;
	}
	return ret;
}

static int TableCall(int index,...)
{
	int res=-1;
	va_list ap;
	if(!mb || mb->english || !mb->rule)
		return -1;
	va_start(ap,index);
	switch(index){
	case EIM_CALL_ADD_PHRASE:
	{
		const char *p=va_arg(ap,const char *);
		char code[64];
		if(p==NULL)
		{
			res=0;
			break;
		}
		if(0==y_mb_code_by_rule(mb,p,strlen(p),code,NULL))
		{
			y_mb_add_phrase(mb,code,p,Y_MB_APPEND,Y_MB_DIC_TEMP);
			res=0;
		}
		break;
	}
	case EIM_CALL_GET_SELECT:
	{
		int (*cb)(const char*)=va_arg(ap,void*);
		const char *p=va_arg(ap,const char *);
		if(!cb)
			break;
		res=cb(p);
		break;
	}
	default:
		break;
	}
	va_end(ap);
	return res;
}

/**********************************************************************
 * ƴ���ӿ�
 *********************************************************************/
static char CodeGet[MAX_CODE_LEN+1];		// �Ѿ���ѡ���ƴ��
static int CodeGetLen;						// ��ѡ��ƴ���ĳ���
static int CodeGetCount;					// ѡ���˶��ٴ�ƴ��
static int CodeMatch;						// ƥ���ƴ������
static int AssistMode;						// �Ƿ������븨����״̬
static uint8_t PinyinStep[MAX_CODE_LEN];	// ƴ���зֵĳ���
static uint8_t PySwitch;					// ƴ���Ƿ񾭹��ֹ��з�


typedef struct {
	int count;								// ����ĺ�ѡ����
	int mark;								// ��������ı�����Extra�Ŀ�ʼλ��
	char pinyin[16];						// �����ѡ��ƴ��
	struct y_mb_item *zi;					// ����ĺ�ѡ
	LArray *arr;							// ģ������ѡ
}EXTRA_ZI;
static EXTRA_ZI ExtraZi;
static int PredictCalcMark;					// ��������ı�����Predict�Ľ���λ��

static void ExtraZiReset(void)
{
	ExtraZi.count=0;
	ExtraZi.mark=0;
	ExtraZi.zi=NULL;
	ExtraZi.pinyin[0]=0;
	if(ExtraZi.arr)
	{
		l_ptr_array_free(ExtraZi.arr,NULL);
		ExtraZi.arr=NULL;
	}
}

static void ExtraZiFill(const char *code,int len,int res)
{
	if(mb->fuzzy)
	{
		struct y_mb_context ctx;
		int ret;
		y_mb_push_context(mb,&ctx);
		mb->ctx.result_match=1;
		ret=y_mb_set(mb,code,len,hz_filter_temp);
		if(ret>0)
		{
			if(mb->ctx.result_ci)
			{
				ExtraZi.arr=mb->ctx.result_ci;
				mb->ctx.result_ci=NULL;
			}
			else
			{
				ExtraZi.zi=mb->ctx.result_first;
			}
		}
		y_mb_pop_context(mb,&ctx);
	}
	else
	{
		ExtraZi.zi=y_mb_get_zi(mb,code,len,hz_filter_temp);
	}
	if(ExtraZi.zi)
	{
		struct y_mb_ci *p;
		for(ExtraZi.count=0,p=ExtraZi.zi->phrase;p;p=p->next)
		{
			if(p->del) continue;
			if(hz_filter_temp && p->ext) continue;
			if(!p->zi) continue;
			if(res && y_mb_in_result(mb,p)) continue;
			ExtraZi.count++;
		}
	}
	else if(ExtraZi.arr)
	{
		struct y_mb_ci *p;
		int i;
		for(ExtraZi.count=0,i=0;i<ExtraZi.arr->len;i++)
		{
			p=l_ptr_array_nth(ExtraZi.arr,i);
			if(p->del) continue;
			if(hz_filter_temp && p->ext) continue;
			if(!p->zi) continue;
			if(res && y_mb_in_result(mb,p)) continue;
			ExtraZi.count++;
		}
	}
	if(ExtraZi.count<=0)
		ExtraZiReset();
}

static int ExtraZiGet(int cur,int count,char CandTable[][MAX_CAND_LEN+1],int key)
{
	struct y_mb_ci *c;
	int i=0;
	if(ExtraZi.zi)
	{
		for(i=0,c=ExtraZi.zi->phrase;c && i<cur;c=c->next)
		{
			if(c->del || !c->zi) continue;
			if(hz_filter_temp && c->ext) continue;
			if(y_mb_in_result(mb,c)) continue;
			i++;
		}
		for(i=0;c && i<count;c=c->next)
		{
			if(c->del || !c->zi) continue;
			if(hz_filter_temp && c->ext) continue;
			if(y_mb_in_result(mb,c)) continue;
			if(key && !y_mb_assist_test(mb,c,key,0,0)) continue;
			y_mb_ci_string2(c,CandTable[i]);
			i++;
		}
	}
	if(ExtraZi.arr)
	{
		int j;
		for(j=0;j<ExtraZi.arr->len && i<count;j++)
		{
			c=l_ptr_array_nth(ExtraZi.arr,cur+j);
			if(c->del || !c->zi) continue;
			if(hz_filter_temp && c->ext) continue;
			if(y_mb_in_result(mb,c)) continue;
			if(key && !y_mb_assist_test(mb,c,key,0,0)) continue;
			y_mb_ci_string2(c,CandTable[i]);
			i++;
		}
	}
	return i;
}

static void PinyinResetPart(void)
{
	CodeGetLen=0;
	CodeGet[0]=0;
	CodeMatch=0;
	CodeGetCount=0;
	AssistMode=0;

	ExtraZiReset();
}
static void PinyinReset(void)
{
	TableReset();
	PinyinResetPart();
}

static void PinyinStripInput(void)
{
	EIM.CodeLen=py_prepare_string(EIM.CodeInput,EIM.CodeInput,&EIM.CaretPos);
}

static void PinyinAddSpace(void)
{
	if(CodeMatch<=0)
		return;
	if(CodeMatch==EIM.CaretPos)
		return;
	if(CodeMatch==EIM.CodeLen)
		return;
	memmove(EIM.CodeInput+CodeMatch+1,EIM.CodeInput+CodeMatch,
		EIM.CodeLen-CodeMatch+1);
	EIM.CodeLen++;
	if(EIM.CaretPos>CodeMatch)
		EIM.CaretPos++;	
	EIM.CodeInput[CodeMatch]=' ';
}

static int AdjustPrevStep(int start,int cur,int test)
{
	int i;
	int len;
	int step=!test;
	if(cur==0)
		return 0;
	for(i=start,len=0;PinyinStep[i];i++)
	{
		len+=PinyinStep[i];
		if(test && len==cur)
		{
			return cur;
		}
		if(len>=cur)
		{
			/* step len to previous */
			step=PinyinStep[i]-(len-cur);
			/* if the first step, one step only */
			if(!test && step==cur)
				step=1;
			/* if only one step and test use it */
			else if(test && i==start)
				step=0;
			break;
		}
	}
	return cur-step;
}

static int PinyinPredict(char *res,char *code,int len,int filter)
{
	int match,good,less;
	int count;	
	
	if(len<=0)
		return 0;
	if(code[0]==' ')
	{
		code++;
		len--;
	}

	match=y_mb_max_match(mb,code,len,-1,filter,&good,&less);
	match=good;
	if(match==0)
		return 0;

	if(!mb->split && less>1 && code[good]!=0)
	{
		int temp=0;
		if(code[good]!=' ')
		{
			y_mb_max_match(mb,code+good,len-good,-1,filter,&temp,0);
			if(temp==0)
			{
				y_mb_max_match(mb,code+less,len-less,-1,filter,&temp,0);
				if(temp>0)
					match=less;
			}
		}
	}

	count=y_mb_set(mb,code,match,filter);
	assert(count>0);
	y_mb_get_first(mb,res+strlen(res));
	code+=match;len-=match;

	return PinyinPredict(res,code,len,filter);
}

static int PinyinMoveCaretTo(int key)
{
	int i,p;
	if(EIM.CodeLen<1)
		return 0;
	if(key>='A' && key<='Z')
		key='a'+(key-'A');
	for(i=0,p=EIM.CaretPos+1;i<EIM.CodeLen;i++)
	{
		if(p>CodeMatch && EIM.CaretPos>CodeMatch && !(p&0x01))
			p++;
		else if(p<=CodeMatch && (p&0x01))
			p++;
		if(p>=EIM.CodeLen)
			p=0;
		if(EIM.CodeInput[p]==key)
		{
			EIM.CaretPos=p;
			break;
		}
		p++;
	}
	return 0;
}

static int AssistDoSearch(void)
{
	int max;
	PhraseListCount=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,hz_filter_temp);
	max=EIM.CandWordMax;
	EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
	PredictPhrase[0]=0;
	PredictCount=0;
	PhraseCalcCount=0;
	PinyinGetCandwords(PAGE_FIRST);
	return PhraseListCount;
}

static int SPDoSearch(int adjust)
{
	int max;
	char temp[128];
	char code[128];
	int GoodMatch=0;
	int CodeReal;
	int HaveResult=0;

	PinyinStripInput();
	memset(PinyinStep,2,sizeof(PinyinStep));

	PredictPhrase[0]=0;
	PredictCount=0;
	ExtraZiReset();
	
	// ˫ƴ����´�ȫƴ�����Զ������
	if(!AssistMode && CodeGetLen==0 && EIM.CodeLen>=2 && EIM.CodeLen<=4 && 
		EIM.CaretPos==EIM.CodeLen &&
		!py_is_valid_quanpin(EIM.CodeInput) &&				// �����ǺϷ���ȫƴ
		!(EIM.CodeLen==2 && py_is_valid_sp(EIM.CodeInput)))	// �����ǺϷ���˫ƴ
	{
		mb->ctx.result_match=1;
		mb->pinyin=0;
		void *old_fuzzy=mb->fuzzy;
		mb->fuzzy=NULL;
		int count=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,hz_filter_temp);
		if(count>0)
		{
			if(count>Y_MB_DATA_CALC) count=Y_MB_DATA_CALC;
			y_mb_get(mb,0,count,CalcPhrase,NULL);
			PhraseCalcCount=count;
			CodeMatch=EIM.CodeLen;
			PhraseListCount=PhraseCalcCount;
			EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
					((PhraseListCount%EIM.CandWordMax)?1:0);
			TableGetCandWords(PAGE_FIRST);
			
			mb->pinyin=1;
			mb->fuzzy=old_fuzzy;
		
			return PhraseListCount;
		}
		
		mb->pinyin=1;
		mb->fuzzy=old_fuzzy;
		//printf("here\n");
	}

	if(!AssistMode && CodeGetLen==0 && (EIM.CodeLen&1) &&
			EIM.CaretPos==EIM.CodeLen && mb->ass &&
			(EIM.CodeLen==3 || (EIM.CodeLen>=5  && CodeMatch==EIM.CodeLen-1)))
	{
		int clen;
		
		strcpy(temp,EIM.CodeInput);temp[EIM.CodeLen-1]=0;
		clen=py_conv_from_sp(temp,code,sizeof(code),0);
		if(clen>0)
		{
			int count=y_mb_set(mb,code,clen,hz_filter_temp);
			if(count>0)
			{
				int end=(EIM.CodeLen==3?0:1);
				PhraseCalcCount=y_mb_assist_get(mb,CalcPhrase,Y_MB_DATA_CALC,EIM.CodeInput[EIM.CodeLen-1],end);
				if(PhraseCalcCount)
				{
					CodeMatch=EIM.CodeLen;
					PhraseListCount=PhraseCalcCount;
					//PredictCount=ExtraCount=0;
					EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
							((PhraseListCount%EIM.CandWordMax)?1:0);
					TableGetCandWords(PAGE_FIRST);
					return PhraseListCount;
				}
			}
		}
	}
	if(AssistMode && CodeGetLen==0 && EIM.CodeLen==4 && EIM.CaretPos==4 && mb->ass)
	{
		int clen;
		strcpy(temp,EIM.CodeInput);temp[2]=0;
		clen=py_conv_from_sp(temp,code,sizeof(code),0);
		if(clen>0)
		{
			int count=y_mb_set(mb,code,clen,hz_filter_temp);
			if(count>0)
			{
				PhraseCalcCount=y_mb_assist_get2(mb,CalcPhrase,Y_MB_DATA_CALC,EIM.CodeInput+2,0);
				if(PhraseCalcCount)
				{
					CodeMatch=4;
					PhraseListCount=PhraseCalcCount;
					//PredictCount=ExtraCount=0;
					EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
							((PhraseListCount%EIM.CandWordMax)?1:0);
					TableGetCandWords(PAGE_FIRST);
					return PhraseListCount;
				}
			}
		}
	}
	if(CodeGetLen==0 && EIM.CodeLen>=2 && EIM.CodeLen<=4)
	{
		struct y_mb_ci *c;
		if(y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,0)>0 && mb->ctx.result_first)
		{
			c=mb->ctx.result_first->phrase;
			for(PhraseCalcCount=0;c!=NULL;c=c->next)
			{
				char *s=y_mb_ci_string2(c,CalcPhrase[PhraseCalcCount]);
				if(strchr(s,'$'))
				{
					PhraseCalcCount++;
				}
			}
			if(PhraseCalcCount>0)
			{
				PhraseListCount=PhraseCalcCount;
				EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
						((PhraseListCount%EIM.CandWordMax)?1:0);
				TableGetCandWords(PAGE_FIRST);
				CodeMatch=EIM.CodeLen;
				return PhraseListCount;
			}
		}
	}
	
	CodeReal=EIM.CaretPos?EIM.CaretPos:EIM.CodeLen;
	if(CodeReal<=0)
	{
		CodeMatch=0;
		PhraseListCount=0;
		EIM.CandPageCount=0;
	}
	else
	{
		int good,len=CodeReal;
		if(adjust && CodeMatch>=1)
		{
			len=AdjustPrevStep(0,CodeMatch,0);
		}
		do{
			int clen;
			strcpy(temp,EIM.CodeInput);temp[len]=0;
			//clen=py_conv_from_sp(temp,code,sizeof(code),0);
			clen=py_conv_from_sp(temp,code,sizeof(code),'\'');
			if(clen>=2 && code[clen-1]=='\'')
			{
				code[--clen]=0;
			}
			CodeMatch=y_mb_max_match(mb,code,clen,-1,hz_filter_temp,&good,NULL);
			if(len==0) CodeMatch=0;
			GoodMatch=py_pos_of_sp(temp,good);
			if(CodeMatch==clen)
			{
				CodeMatch=len;
				break;
			}
			CodeMatch=py_pos_of_sp(temp,CodeMatch);
			if(CodeMatch>=len)
				CodeMatch=len-1;
			if(CodeMatch<=0)
			{
				CodeMatch=0;
				code[0]=0;
				break;
			}
			len=AdjustPrevStep(0,CodeMatch,1);
		}while(CodeMatch>1);
		mb->ctx.result_match=(CodeMatch<EIM.CodeLen);
		len=strlen(code);
		PhraseListCount=y_mb_set(mb,code,strlen(code),hz_filter_temp);
		HaveResult=PhraseListCount>0;
		max=EIM.CandWordMax;
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
	}
	PinyinAddSpace();
	PinyinGetCandwords(PAGE_FIRST);
	CodeReal=EIM.CaretPos?EIM.CaretPos:EIM.CodeLen;
	if(CodeReal && GoodMatch<CodeReal)
	{
		struct y_mb_context ctx;
		y_mb_push_context(mb,&ctx);
		PredictCount=y_mb_predict_by_learn(mb,
				EIM.CodeInput,CodeReal,
				PredictPhrase,sizeof(PredictPhrase),CodeGetLen==0);
		y_mb_pop_context(mb,&ctx);
		/*if(PredictCount==1 && EIM.CandWordCount && 
					(!strcmp(EIM.CandTable[0],PredictPhrase) ||
					strchr(EIM.CandTable[0],'$')))
		{
			PredictPhrase[0]=0;
			PredictCount=0;
		}*/
		PhraseListCount+=PredictCount;
		max=EIM.CandWordMax;
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
	}
	if(CodeMatch>2)
	{
		memcpy(temp,EIM.CodeInput,2);temp[2]=0;
		py_conv_from_sp(temp,code,sizeof(code),0);
		ExtraZiFill(code,strlen(code),HaveResult);
		if(ExtraZi.count>0)
		{
			strcpy(ExtraZi.pinyin,temp);
			PhraseListCount+=ExtraZi.count;
			max=EIM.CandWordMax;
			EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
		}
	}
	PinyinGetCandwords(PAGE_FIRST);
	return PhraseListCount;
}

static int PinyinDoSearch(int adjust)
{
	int max;
	int GoodMatch=0;
	int CodeReal;
	int HaveResult=0;
	PhraseCalcCount=0;
	
	(void)GoodMatch;

	if(EIM.CodeInput[0]==mb->lead)
		return AssistDoSearch();
	else if(EIM.CodeInput[0]==mb->quick_lead)
		return AssistDoSearch();

	PySwitch=adjust;
	if(SP==1)
		return SPDoSearch(adjust);

	PinyinStripInput();
	py_string_step(EIM.CodeInput,EIM.CaretPos,
			PinyinStep,sizeof(PinyinStep));
	PredictPhrase[0]=0;
	PredictCount=0;
	ExtraZiReset();

	while(!adjust && !AssistMode && //(mb->ass || mb->yong) &&
			CodeGetLen==0 && mb->split>=2 && (EIM.CodeLen%mb->split==1) && 
			EIM.CaretPos==EIM.CodeLen &&
			(EIM.CodeLen==mb->split+1 || (EIM.CodeLen>=2*mb->split+1 && CodeMatch==EIM.CodeLen-1)))
	{
		char code[128];
		int clen=EIM.CodeLen-1;
		int count;

		if(mb->yong && EIM.CodeLen<mb->split*2+1)
			break;
			
		// ����������Ҫ��ȷƥ���ģʽ
		mb->ctx.result_match=1;
		
		// ���������ж�Ӧ�ı��룬��ô���ǾͲ�����ֱ�Ӹ���������
		count=y_mb_set(mb,EIM.CodeInput,EIM.CodeLen,hz_filter_temp);
		if(count>0)
		{
			if(EIM.CodeLen==mb->split+1)
			{
				// ����ڵ��ּӸ������λ�������ֱ���б��룬��ôֱ�ӷ��أ���Ҫ���ҵõ�һ����λ�ĵ�����
				CodeMatch=EIM.CodeLen;
				PhraseListCount=count;
				PhraseCalcCount=0;
				EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
						((PhraseListCount%EIM.CandWordMax)?1:0);
				TableGetCandWords(PAGE_FIRST);
				return PhraseListCount;
			}
			break;
		}
		
		strcpy(code,EIM.CodeInput);code[clen]=0;
		count=y_mb_set(mb,code,clen,hz_filter_temp);
		if(count>0)
		{
			int end=(EIM.CodeLen==mb->split+1?0:1);
			PhraseCalcCount=y_mb_assist_get(mb,CalcPhrase,Y_MB_DATA_CALC,EIM.CodeInput[EIM.CodeLen-1],end);
			if(PhraseCalcCount)
			{
				CodeMatch=EIM.CodeLen;
				PhraseListCount=PhraseCalcCount;
				EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
						((PhraseListCount%EIM.CandWordMax)?1:0);
				TableGetCandWords(PAGE_FIRST);
				return PhraseListCount;
			}
		}
		break;
	}
	
	CodeReal=EIM.CaretPos?EIM.CaretPos:EIM.CodeLen;
	if(CodeReal<=0)
	{
		CodeMatch=0;
		PhraseListCount=0;
		EIM.CandPageCount=0;
	}
	else
	{
		int good,len;
		len=CodeReal;
		if(adjust && CodeMatch>=1)
		{
			len=AdjustPrevStep(0,CodeMatch,0);
		}
		do{
			int less;
			CodeMatch=y_mb_max_match(mb,EIM.CodeInput,len,-1,hz_filter_temp,&good,&less);
			if(len==0) CodeMatch=0;
			if(CodeMatch<EIM.CaretPos && CodeMatch>good)
				CodeMatch=good;
			GoodMatch=good;
			if(CodeMatch==EIM.CodeLen)
				break;
			len=AdjustPrevStep(0,CodeMatch,1);
			if(len==CodeMatch) break;
			CodeMatch=less;
			len=AdjustPrevStep(0,CodeMatch,1);
			if(len==CodeMatch) break;
		}while(CodeMatch>1);
		mb->ctx.result_match=(CodeMatch<EIM.CodeLen);
		{
			/*char temp[CodeMatch+1],*p=EIM.CodeInput;
			for(len=0;p<EIM.CodeInput+CodeMatch;p++)
			{
				if(*p==mb->split) continue;
				temp[len++]=*p;
			}
			temp[len]=0;
			PhraseListCount=y_mb_set(mb,temp,len,hz_filter_temp);*/
			PhraseListCount=y_mb_set(mb,EIM.CodeInput,CodeMatch,hz_filter_temp);
		}
		HaveResult=PhraseListCount>0;
		max=EIM.CandWordMax;
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
	}
	PinyinAddSpace();
	PinyinGetCandwords(PAGE_FIRST);
	
	CodeReal=EIM.CaretPos?EIM.CaretPos:EIM.CodeLen;
	if(CodeReal && GoodMatch<CodeReal)
	{
		struct y_mb_context ctx;
		y_mb_push_context(mb,&ctx);
		if(mb->split>1 /*&& auto_move!=1*/)
		{
			PredictCount=y_mb_predict_by_learn(mb,EIM.CodeInput,
						CodeReal,PredictPhrase,sizeof(PredictPhrase),CodeGetLen==0);
		}
		else
		{
			PinyinPredict(PredictPhrase,EIM.CodeInput,CodeReal,hz_filter_temp);
			PredictCount=PredictPhrase[0]?1:0;
		}
		y_mb_pop_context(mb,&ctx);
		PhraseListCount+=PredictCount;
		max=EIM.CandWordMax;
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
	}

	if(CodeMatch>1)
	{
		int i=CodeMatch-1;
		if(mb->split=='\'' || mb->split==1)
		{
			i=MIN(6,i);
			for(;i>0;i--)
			{
				ExtraZiFill(EIM.CodeInput,i,HaveResult);
				if(ExtraZi.count>0)
					break;
			}
		}
		else if(mb->split>1 && i>=mb->split)
		{
			i=mb->split;
			ExtraZiFill(EIM.CodeInput,i,HaveResult);
		}
		else if(mb->split==CodeMatch && mb->simple && (CodeGetCount>0 || EIM.CodeLen>CodeMatch))
		{
			// �����ѡ������£�������simple����
			i=mb->split;
			ExtraZiFill(EIM.CodeInput,i,0);
			if(ExtraZi.count>0)
			{
				PhraseListCount=PredictCount;
				HaveResult=0;
				mb->ctx.result_count=0;
				mb->ctx.result_count_zi=0;
				mb->ctx.result_first=0;
			}
		}
		
		if(ExtraZi.count>0)
		{
			memcpy(ExtraZi.pinyin,EIM.CodeInput,i);
			ExtraZi.pinyin[i]=0;
			PhraseListCount+=ExtraZi.count;
			max=EIM.CandWordMax;
			EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
		}
	}
	PinyinGetCandwords(PAGE_FIRST);
	return PhraseListCount;
}

static void PinyinCodeRevert(void)
{
	PinyinStripInput();
	EIM.CaretPos=strlen(CodeGet);
	strcat(CodeGet,EIM.CodeInput);
	strcpy(EIM.CodeInput,CodeGet);
	EIM.CodeLen=strlen(EIM.CodeInput);
	CodeGet[0]=0;
	CodeGetLen=0;
	EIM.StringGet[0]=0;
	CodeGetCount=0;
	AssistMode=0;
	PinyinDoSearch(0);
}

static void AddExtraSplit(void)
{
	if(mb->split!='\'' || SP)
		return;
	if(CodeGetLen<=0)
		return;
	if(EIM.CodeInput[0]=='\'' || !strncmp(EIM.CodeInput," '",2))
	{
		CodeGet[CodeGetLen++]='\'';
		CodeGet[CodeGetLen]=0;
		PinyinStripInput();
		//printf("%s %s\n",CodeGet,EIM.CodeInput);
	}
}

static char *PinyinGetCandWord(int index)
{
	int pos;
	int force=(index==-1);

	if(index>=EIM.CandWordCount)
		return 0;
	if(index==-1) index=EIM.SelectIndex;
	pos=EIM.CurCandPage*EIM.CandWordMax+index;
	if(ExtraZi.count>0 && ((PhraseCalcCount==0 && pos>=PhraseListCount-ExtraZi.count) ||
			(PhraseCalcCount>0 && pos>=ExtraZi.mark)))
	{
		int step;
		if(strlen(EIM.StringGet)+strlen(EIM.CandTable[index])>Y_MB_DATA_SIZE)
			return NULL;
		strcat(EIM.StringGet,EIM.CandTable[index]);
		step=strlen(ExtraZi.pinyin);
		strcat(CodeGet+CodeGetLen,ExtraZi.pinyin);
		CodeGetLen+=step;
		CodeGetCount++;
		memmove(EIM.CodeInput,EIM.CodeInput+step,
					EIM.CodeLen-step+1);
		EIM.CaretPos-=step;
		if(EIM.CaretPos<0) EIM.CaretPos=0;
		EIM.CodeLen-=step;
		AddExtraSplit();
	}
	else if(CodeMatch)
	{
		if(strlen(EIM.StringGet)+strlen(EIM.CandTable[index])>Y_MB_DATA_SIZE)
			return NULL;
		strcat(EIM.StringGet,EIM.CandTable[index]);
		if((PhraseCalcCount && pos<PredictCalcMark) ||
				(!PhraseCalcCount && pos<PredictCount))
		{
			int CodeReal;
			CodeGetCount++;
			PinyinStripInput();
			CodeReal=EIM.CaretPos?EIM.CaretPos:EIM.CodeLen;
			memcpy(CodeGet+CodeGetLen,EIM.CodeInput,CodeReal);
			CodeGetLen+=CodeReal;
			EIM.CodeLen-=CodeReal;
			CodeGet[CodeGetLen]=0;
			memmove(EIM.CodeInput,EIM.CodeInput+EIM.CaretPos,EIM.CodeLen+1);
			EIM.CaretPos=0;
			if(auto_move)
			{
				char *code=ExtraZi.pinyin;
				if(SP==1)
				{
					char temp[MAX_CODE_LEN+1];
					py_conv_from_sp(code,temp,sizeof(temp),0);
					y_mb_auto_move(mb,temp,EIM.CandTable[index],auto_move);
				}
				else
				{
					y_mb_auto_move(mb,code,EIM.CandTable[index],auto_move);
				}
			}
		}
		else
		{
			memcpy(CodeGet+CodeGetLen,EIM.CodeInput,CodeMatch);
			CodeGetLen+=CodeMatch;
			CodeGet[CodeGetLen]=0;
			CodeGetCount++;
			memmove(EIM.CodeInput,EIM.CodeInput+CodeMatch,
					EIM.CodeLen-CodeMatch+1);
			EIM.CaretPos-=CodeMatch;
			if(EIM.CaretPos<0) EIM.CaretPos=0;
			EIM.CodeLen-=CodeMatch;
			if(auto_move)
			{
				char *code=CodeGet+CodeGetLen-CodeMatch;
				if(SP==1)
				{
					char temp[MAX_CODE_LEN+1];
					py_conv_from_sp(code,temp,sizeof(temp),0);
					y_mb_auto_move(mb,temp,EIM.CandTable[index],auto_move);
				}
				else
				{
					y_mb_auto_move(mb,code,EIM.CandTable[index],auto_move);
				}
			}
		}
		AddExtraSplit();
		if(0==EIM.CodeLen)
		{
			if(CodeGetLen==CodeMatch) /* if phrase exist */
				strcat(CodeGet,CodeTips[index]);
			CodeMatch=0;
			if((CodeGetCount>1 || (PySwitch && py_switch_save))&& auto_add)
			{
				if(SP==1)
				{
					char temp[MAX_CODE_LEN+1];
					py_conv_from_sp(CodeGet,temp,sizeof(temp),0);
					y_mb_add_phrase(mb,temp,EIM.StringGet,Y_MB_APPEND,Y_MB_DIC_USER);
				}
				else
				{
					y_mb_add_phrase(mb,CodeGet,EIM.StringGet,Y_MB_APPEND,Y_MB_DIC_USER);
				}
			}
			CodeGet[0]=0;
		}
	}
	else
	{
		if(EIM.CandWordCount)
		{
			strcpy(EIM.StringGet,EIM.CandTable[index]);
			force=1;
			
			if(PySwitch && py_switch_save&& auto_add)
			{
				if(SP==1)
				{
					char temp[MAX_CODE_LEN+1];
					py_conv_from_sp(EIM.CodeInput,temp,sizeof(temp),0);
					y_mb_add_phrase(mb,temp,EIM.StringGet,Y_MB_APPEND,Y_MB_DIC_USER);
				}
				else
				{
					y_mb_add_phrase(mb,EIM.CodeInput,EIM.StringGet,Y_MB_APPEND,Y_MB_DIC_USER);
				}
			}

			if(assoc_mode && assoc_handle && assoc_move)
			{
				y_legend_move(assoc_handle,EIM.StringGet);
			}
		}
	}
	
	EIM.SelectIndex=0;

	/* in simple or assist mode, we always commit
	 * if -1 select we commit too
	 * if mb->pinyin!=3, we can commit when no code left
	 */
	if(0==EIM.CodeLen || force ||
			(mb->lead && EIM.CodeInput[0]==mb->lead))
	{
		return EIM.StringGet;
	}
	else
	{
		PinyinDoSearch(0);
		return 0;
	}
}

static int PinyinGetCandwords(int mode)
{
	struct y_mb *active;
	int i,start,max;

	if(mode==PAGE_LEGEND)
	{
		PinyinResetPart();
		//CodeGetLen=0;
		//CodeGet[0]=0;
	}
	
	if(mode==PAGE_LEGEND)
		active=mb;
	else
		active=Y_MB_ACTIVE(mb);

	max=EIM.CandWordMax;

	if(mode==PAGE_LEGEND)
	{
		const char *from=EIM.StringGet;
		if(gb_strlen((uint8_t*)from)<assoc_begin)
			return IMR_BLOCK;
		if(assoc_handle)
		{
			PhraseCalcCount=y_legend_get(
				assoc_handle,from,strlen(from),
				assoc_count,CalcPhrase,Y_MB_DATA_CALC);
		}
		else
		{
			PhraseCalcCount=y_mb_get_legend(
				mb,from,strlen(from),
				assoc_count,CalcPhrase,Y_MB_DATA_CALC);
		}
		if(!assoc_hungry)
			PhraseCalcCount+=EIM.QueryHistory(EIM.StringGet,CalcPhrase+PhraseCalcCount,Y_MB_DATA_CALC-PhraseCalcCount);
		if(!PhraseCalcCount)
			return IMR_BLOCK;
		EIM.CodeLen=0;
		EIM.CodeInput[0]=0;
		strcpy(EIM.StringGet,EIM.Translate(hz_trad?"���P���~��":"���룺"));
		ExtraZiReset();
		PhraseListCount=PhraseCalcCount;
		EIM.CandPageCount=PhraseListCount/max+((PhraseListCount%max)?1:0);
		mode=PAGE_FIRST;
		assoc_mode=1;
	}

	if(mode==PAGE_FIRST) EIM.CurCandPage=0;
	else if(mode==PAGE_NEXT && EIM.CurCandPage+1<EIM.CandPageCount)
		EIM.CurCandPage++;
	else if(mode==PAGE_PREV && EIM.CurCandPage>0)
		EIM.CurCandPage--;

	if(EIM.CandPageCount==0)
		EIM.CandWordCount=0;
	else if(EIM.CurCandPage<EIM.CandPageCount-1)
		EIM.CandWordCount=max;
	else EIM.CandWordCount=PhraseListCount-max*(EIM.CandPageCount-1);

	start=EIM.CurCandPage*max;

	for(i=0;i<max;i++)
	{
		CodeTips[i][0]=0;
		EIM.CodeTips[i][0]=0;
	}

	if(PhraseCalcCount)
	{
		for(i=0;i<EIM.CandWordCount;i++)
			strcpy(EIM.CandTable[i],CalcPhrase[start+i]);
	}
	else
	{
		int pos=start,count=EIM.CandWordCount;
		int PhraseCount=PhraseListCount-PredictCount-ExtraZi.count;
		int cur;

		if(pos<PredictCount)
		{
			for(;count>0 && pos<PredictCount;)
			{
				const char *s=y_mb_predict_nth(PredictPhrase,pos);
				strcpy(EIM.CandTable[pos-start],s);
				count--;
				pos++;
			}
		}
		if(count>0 && pos>=PredictCount && pos<PredictCount+PhraseCount)
		{
			int m;
			cur=pos-PredictCount;
			m=MIN(count,PhraseCount-cur);
			y_mb_get(mb,cur,m,EIM.CandTable+pos-start,CodeTips+pos-start);
			count-=m;
			pos+=m;
		}
		if(count>0 && pos>=PredictCount+PhraseCount)
		{
			cur=pos-PredictCount-PhraseCount;
			ExtraZiGet(cur,count,EIM.CandTable+pos-start,0);
		}
	}
	if(EIM.SelectIndex>=EIM.CandWordCount)
		EIM.SelectIndex=0;
	if(active->hint!=0)
	for(i=0;i<EIM.CandWordCount;i++)
		strcpy(EIM.CodeTips[i],CodeTips[i]);
	if(active==mb->ass) /* it's assist mb */
	{
		/* hint the main mb's code */
		for(i=0;i<EIM.CandWordCount;i++)
		{
			int len;
			char *data=EIM.CandTable[i];
			EIM.CodeTips[i][0]=0;
			len=strlen(data);
			if((len==2 && gb_is_gbk((uint8_t*)data)) ||
					(len==4 && gb_is_gb18030_ext((uint8_t*)data)))
			{
				y_mb_get_full_code(mb,data,EIM.CodeTips[i]);
			}
			else
			{
				y_mb_get_exist_code(mb,data,EIM.CodeTips[i]);
			}
		}
	}
	else if(active==mb->quick_mb)
	{
		for(i=0;i<EIM.CandWordCount;i++)
		{
			EIM.CodeTips[i][0]=0;
		}
	}

	if(InsertMode)
	{
		int pos=EIM.CurCandPage*EIM.CandWordMax+EIM.SelectIndex;
		if(0==y_mb_code_by_rule(mb,CalcPhrase[pos],strlen(CalcPhrase[pos]),EIM.CodeInput,NULL))
			EIM.CodeLen=strlen(EIM.CodeInput);
	}

	return IMR_DISPLAY;
}

static int PinyinDoInput(int key)
{
	int i;
	
	if(!TableReady)
		return IMR_NEXT;

	key&=~KEYM_BING;
	
	if(key==key_backspace)
		key='\b';
	key=TableReplaceKey(key);

	if(AssistMode)
	{
		if(!py_assist_series)
			AssistMode=0;
		if(y_mb_is_assist_key(mb,key))
		{
			if(SP)
			{
				// reset SP 2+2 mode first
				PinyinDoSearch(0);
			}
			PhraseCalcCount=0;
			if(PredictCount>1)
			{
				int i;
				for(i=1;i<PredictCount;i++)
				{
					const char *s=y_mb_predict_nth(PredictPhrase,i);
					char temp[sizeof(struct y_mb_ci)+4];
					struct y_mb_ci *c=(struct y_mb_ci *)temp;
					c->len=4;
					strcpy((char*)c->data,s);
					if(!y_mb_assist_test(mb,c,key,0,0)) continue;
					strcpy(&CalcPhrase[PhraseCalcCount][0],s);
					PhraseCalcCount++;
				}
			}
			PredictCalcMark=PhraseCalcCount;
			if(PhraseCalcCount<Y_MB_DATA_CALC)
			{
				PhraseCalcCount+=y_mb_assist_get(mb,
						CalcPhrase+PhraseCalcCount,
						Y_MB_DATA_CALC-PhraseCalcCount,
						key,0);
			}
			if(PhraseCalcCount<Y_MB_DATA_CALC && ExtraZi.count)
			{
				ExtraZi.mark=PhraseCalcCount;
				PhraseCalcCount+=ExtraZiGet(0,Y_MB_DATA_CALC-PhraseCalcCount,CalcPhrase+PhraseCalcCount,key);
			}
			if(PhraseCalcCount)
			{
				PhraseListCount=PhraseCalcCount;
				EIM.CandPageCount=PhraseListCount/EIM.CandWordMax+
						((PhraseListCount%EIM.CandWordMax)?1:0);
				PinyinGetCandwords(PAGE_FIRST);				
				return IMR_DISPLAY;
			}
			return IMR_BLOCK;
		}
	}
	if(key==YK_BACKSPACE)
	{
		AssistMode=0;
		if(assoc_mode || EIM.WorkMode==EIM_WM_QUERY)
			return IMR_CLEAN;
		if(EIM.CodeLen==0 && CodeGetLen==0)
			return IMR_CLEAN_PASS;
		if(EIM.CaretPos==0)
		{
			if(CodeGetLen)
			{
				PinyinCodeRevert();
				return IMR_DISPLAY;
			}
			return IMR_BLOCK;
		}
		for(i=EIM.CaretPos;i<EIM.CodeLen;i++)
			EIM.CodeInput[i-1]=EIM.CodeInput[i];
		EIM.CodeLen--;
		EIM.CaretPos--;
		EIM.CodeInput[EIM.CodeLen]=0;
		if(EIM.CaretPos==0)
		{
			if(CodeGetLen)
			{
				PinyinCodeRevert();
				return IMR_DISPLAY;
			}
		}
		if(EIM.CodeLen==0 && EIM.StringGet[0]==0)
			return IMR_CLEAN;
	}
	else if(key==py_switch)
	{
		if(EIM.CodeLen==0)
			return IMR_NEXT;
		if(CodeMatch>=1)
			PinyinDoSearch(-1);
		else
			PinyinDoSearch(0);
		if(EIM.CodeInput[0]!=mb->lead)			// if assist, search phrase
			return IMR_DISPLAY;
	}
	else if(key==YK_DELETE)
	{
		if(EIM.CodeLen==0)
			return IMR_PASS;
		if(EIM.CodeLen!=EIM.CaretPos)
		{
			for(i=EIM.CaretPos;i<EIM.CodeLen;i++)
				EIM.CodeInput[i]=EIM.CodeInput[i+1];
			EIM.CodeLen--;
			EIM.CodeInput[EIM.CodeLen]=0;
		}
	}
	else if(key==YK_HOME)
	{
		if(EIM.CodeLen==0)
			return IMR_PASS;
		EIM.CaretPos=0;
	}
	else if(key==YK_END)
	{
		if(EIM.CodeLen==0)
			return IMR_PASS;
		EIM.CaretPos=EIM.CodeLen;
	}
	else if(key==YK_LEFT)
	{
		if(EIM.CodeLen==0 && !CodeGetLen)
			return IMR_PASS;
		if(EIM.CaretPos<0)
			return IMR_BLOCK;
		if(EIM.CaretPos==0 && CodeGetLen)
		{
			PinyinCodeRevert();
			return IMR_DISPLAY;
		}
		if(EIM.CaretPos>0)
			EIM.CaretPos--;		
	}
	else if(key==YK_RIGHT)
	{
		if(EIM.CodeLen==0)
			return IMR_PASS;
		if(EIM.CaretPos<0)
			return IMR_BLOCK;
		if(EIM.CaretPos<EIM.CodeLen)
			EIM.CaretPos++;
		else
			EIM.CaretPos=0;
	}
	else if(key==YK_SPACE)
	{
		if(EIM.CandWordCount==0 && CodeGetLen)
		{
			if(EIM.CodeLen)
				return IMR_BLOCK;
			if(auto_add)
				y_mb_add_phrase(mb,CodeGet,EIM.StringGet,0,Y_MB_DIC_USER);
			return IMR_COMMIT;
		}
		//if(CodeMatch || (!CodeMatch && !EIM.CodeLen) || EIM.CodeInput[0]==mb->lead)
			return IMR_NEXT;
	}
	else if(key==YK_ENTER)
	{
		if(InsertMode)
		{
			y_mb_add_phrase(mb,EIM.CodeInput,EIM.CandTable[EIM.SelectIndex],0,Y_MB_DIC_USER);
			TableReset();
			return IMR_CLEAN;
		}
		PinyinStripInput();
		return IMR_NEXT;
	}
	else if(key==key_cnen)
	{
		PinyinStripInput();
		return IMR_ENGLISH;
	}
	else if(key==YK_VIRT_REFRESH)
	{
		if(!y_mb_is_keys(mb,EIM.CodeInput))
			return IMR_NEXT;
	}
	else if(key==YK_VIRT_QUERY)
	{
		char *ph=NULL;
		if(EIM.CandWordCount)
			ph=EIM.CandTable[EIM.SelectIndex];
		else if(!EIM.CodeLen && EIM.GetSelect)
		{
			ph=EIM.GetSelect(TableOnVirtQuery);
			if(ph==NULL)
				return IMR_BLOCK;
		}
		return TableOnVirtQuery(ph);
	}
	else if(key==YK_VIRT_TIP)
	{
		int i=0;
		for(i=0;i<EIM.CandWordCount;i++)
		{
			if(gb_strlen((uint8_t*)EIM.CandTable[i])!=1)
				continue;
			y_mb_get_full_code(mb,EIM.CandTable[i],EIM.CodeTips[i]);
		}
		return IMR_DISPLAY;
	}
	else if(key==YK_VIRT_DEL)
	{
		char code[Y_MB_KEY_SIZE+1];
		int ret;
		if(EIM.CodeLen==0)
			return IMR_NEXT;
		if(InsertMode || CodeGetLen>0 || CodeMatch!=EIM.CodeLen)
			return IMR_BLOCK;
		if(SP)
		{
			py_conv_from_sp(EIM.CodeInput,code,sizeof(code),0);
		}
		else
		{
			strcpy(code,EIM.CodeInput);
			strcat(code,CodeTips[EIM.SelectIndex]);
		}
		ret=y_mb_del_phrase(mb,code,EIM.CandTable[EIM.SelectIndex]);
		if(ret==-1)
			return IMR_BLOCK;
		TableReset();
		return IMR_CLEAN;
	}
	else if(key==YK_VIRT_ADD)
	{
		char *ph=NULL;
		if(InsertMode || EIM.CodeLen)
			return IMR_BLOCK;
		if(EIM.GetSelect!=NULL)
		{
			ph=EIM.GetSelect(TableOnVirtAdd);
			if(ph==NULL)
				return IMR_BLOCK;
		}
		return TableOnVirtAdd(ph);
	}
	else if(key==key_move_up || key==key_move_down)
	{
		char code[Y_MB_KEY_SIZE+1];
		int ret;
		int pos;
		if(InsertMode)
			return IMR_BLOCK;
		pos=EIM.CurCandPage*EIM.CandWordMax+EIM.SelectIndex;
		if(ExtraZi.count>0 && !PhraseCalcCount && pos>=PhraseListCount-ExtraZi.count)
		{
			strcpy(code,ExtraZi.pinyin);
		}
		else if(ExtraZi.count>0 && PhraseCalcCount && pos>=ExtraZi.mark)
		{
			strcpy(code,ExtraZi.pinyin);
		}
		else
		{
			strcpy(code,EIM.CodeInput);
			strcat(code,CodeTips[EIM.SelectIndex]);
		}
		if(SP==1)
		{
			char temp[MAX_CODE_LEN+1];
			py_conv_from_sp(code,temp,sizeof(temp),0);
			strcpy(code,temp);
		}
		ret=y_mb_move_phrase(mb,code,EIM.CandTable[EIM.SelectIndex],
			(key==key_move_up)?-1:1);
		if(ret==-1)
			return IMR_BLOCK;
		PinyinGetCandwords(PAGE_REFRESH);
		return IMR_DISPLAY;
	}
	else if(hz_filter && key==key_filter)
	{
		if(InsertMode || EIM.CodeLen==0)
			return IMR_NEXT;
		if(!hz_filter_temp)
			hz_filter_temp=hz_filter;
		else
			hz_filter_temp=0;
	}
	else if(y_mb_is_key(mb,key) ||
		(key==mb->split && !SP &&key=='\'') ||
		(key==mb->lead && EIM.CodeLen==0) ||
		(key==mb->quick_lead && EIM.CodeLen==0) ||
		(SP==1 && key==';' && EIM.CaretPos && (py_sp_has_semi() || 
				(EIM.CaretPos>=2 && mb->ass && y_mb_is_key(mb->ass,';'))
		)))
	{
		if(CodeGetLen+EIM.CodeLen>=54)
			return IMR_BLOCK;
		if(key==mb->wildcard)
			return IMR_NEXT;
		if(EIM.CodeLen==0 && CodeGetLen==0)
		{
			EIM.CaretPos=0;
			PhraseCalcCount=0;
			EIM.StringGet[0]=0;
			EIM.SelectIndex=0;
			mb->ctx.input[0]=0;
			assoc_mode=0;
		}
		for(i=EIM.CodeLen-1;i>=EIM.CaretPos;i--)
			EIM.CodeInput[i+1]=EIM.CodeInput[i];
		EIM.CodeInput[EIM.CaretPos]=key;
		EIM.CodeLen++;
		EIM.CaretPos++;
		EIM.CodeInput[EIM.CodeLen]=0;
	}
/*
 * FIXME: ���˰�@��ѡ�ּ������ﷵ�صĻ������г�ͻ����ʱ��������
	else if(EIM.CodeLen>0 && EIM.CodeLen<32 && EIM.CaretPos==EIM.CodeLen && key=='@')
	{
		EIM.CodeInput[EIM.CodeLen++]='@';
		EIM.CodeInput[EIM.CodeLen]=0;
		EIM.CaretPos=EIM.CodeLen;
		PinyinStripInput();
		return IMR_ENGLISH;
	}
*/
	else if(key==YK_TAB)
	{
		if(EIM.CandWordCount && (mb->ass || mb->yong) &&
				!InsertMode)
		{
			if(SP && EIM.CodeLen>=4 && EIM.CodeLen<=5)
			{
				AssistMode=1;
				PinyinDoSearch(0);
				AssistMode=0;
				
				// ���û�з��ַ��ϵ�2+2ֱ�Ӹ����룬��ô���ּ�Ӹ�������ʽ
				if(PhraseCalcCount==0)
				{
					AssistMode=1;
					return IMR_BLOCK;
				}

				// ������ĴʵĻ������ָ�����ģʽ�������������Լ������дʼ�Ӹ�����
				if(mb->ctx.result_first)
				{
					struct y_mb_ci *c;
					for(c=mb->ctx.result_first->phrase;c!=NULL;c=c->next)
					{
						if(c->del) continue;
						if(c->zi) continue;
						AssistMode=1;
						break;
					}
				}
				return IMR_DISPLAY;
			}
			AssistMode=1;
			return IMR_BLOCK;
		}
		return IMR_NEXT;
	}
	else if(key>='A' && key<='Z')
	{
		if(EIM.CodeLen>=1)
			PinyinMoveCaretTo(key);
		else
			return IMR_NEXT;
	}
	else
	{
		return IMR_NEXT;
	}
	if(!InsertMode)
	{
		PinyinDoSearch(0);
		if(PhraseListCount==0 && CodeGetLen+EIM.CodeLen<=1)
		{
			PinyinReset();
			return IMR_NEXT;
		}
		else if(PhraseListCount>=1 && !AssistMode && CodeGetLen==0 && CodeMatch==EIM.CodeLen && !CodeTips[0][0])
		{
			if(l_str_has_suffix(EIM.CandTable[0],"$SPACE"))
			{
				char *p=EIM.CandTable[0];
				size_t len=strlen(p);
				if(len!=6 && !strstr(p,"$$"))
				{
					p[len-6]=0;
					strcpy(EIM.StringGet,p);
					return IMR_COMMIT;
				}
			}
		} 
	}
	return IMR_DISPLAY;
}

L_EXPORT(int tool_save_user(void *arg,void **out))
{
	if(!mb)
		return -1;
	y_mb_save_user(mb);
	return 0;
}

L_EXPORT(int tool_get_file(void *arg,void **out))
{
	if(!mb || !arg || !out)
		return -1;
	if(!strcmp(arg,"main"))
		*out=mb->main;
	else if(!strcmp(arg,"user"))
		*out=mb->user;
	else
		return -1;
	return 0;
}

L_EXPORT(int tool_optimize(void *arg,void **out))
{
	char temp[256];
	FILE *fp;
	struct y_mb_arg mb_arg;
	if(!mb)	return -1;
	
	strcpy(temp,mb->main);
	y_mb_free(mb);
	memset(&mb_arg,0,sizeof(mb_arg));
	mb_arg.dicts=EIM.GetConfig(NULL,"dicts");
	if(!mb_arg.dicts || !mb_arg.dicts[0])
		mb_arg.dicts=EIM.GetConfig("table","dicts");
	/* MB_FLAG_SLOW ���ڴ�����±�ú��� */
	mb=y_mb_load(temp,MB_FLAG_SLOW|MB_FLAG_NOUSER,NULL);
	if(!mb) return -1;	
	fp=y_mb_open_file(mb->main,(mb->encrypt?"wb":"w"));
	if(!fp) return -1;
	y_mb_dump(mb,fp,MB_DUMP_MAIN|MB_DUMP_HEAD|MB_DUMP_ADJUST,
			MB_FMT_YONG,0);
	fclose(fp);
	y_mb_free(mb);
	mb=y_mb_load(temp,mb_flag,&mb_arg);
	y_mb_load_pin(mb,EIM.GetConfig(0,"pin"));
	return 0;
}

L_EXPORT(int tool_merge_user(void *arg,void **out))
{
	FILE *fp;
	if(!mb)	return -1;
		
	fp=y_mb_open_file(mb->main,(mb->encrypt?"wb":"w"));
	if(!fp) return -1;
	y_mb_dump(mb,fp,MB_DUMP_MAIN|MB_DUMP_USER|MB_DUMP_HEAD|MB_DUMP_ADJUST,
			MB_FMT_YONG,0);
	fclose(fp);
	return 0;
}
