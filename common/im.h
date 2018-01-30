#ifndef _IM_H_
#define _IM_H_

#include "yong.h"
#include "bihua.h"
#include "english.h"
#include "s2t.h"
#include "layout.h"

typedef struct{
	void *handle;
	EXTRA_IM *eim;

	int Index;
	int CandWord;
	int Preedit;
	int Biaodian;
	int Trad;
	int TradDef;
	int Bing;
	int AssocLen;
	int AssocLoop;
	int InAssoc;
	int Beep;
	int BingSkip[2];
	int Hint;				// 编码提示

	char StringGet[(MAX_CAND_LEN+1)*3/2];
	char CodeInput[(MAX_CODE_LEN+1)*3];
	int CodeLen;
	char CandTable[10][(MAX_CAND_LEN+1)*2];
	char CodeTips[10][(MAX_TIPS_LEN+1)*2];

	char StringGetEngine[MAX_CAND_LEN+2];
	char CodeInputEngine[MAX_CODE_LEN+2];
	char CandTableEngine[10][MAX_CAND_LEN+1];
	char CodeTipsEngine[10][MAX_TIPS_LEN+1];

	int cursor_h;
	double CandPosX[33];
	double CandPosY[33];
	int BihuaMode;
	int EnglishMode;
	int ChinglishMode;
	int SelectMode;
	int StopInput;
	char Page[32];
	double PageLen;
	double PagePosX;
	double PagePosY;
	double CodePos[4];
	
	Y_LAYOUT *layout;
}IM;

/* this one defined at main.c */
extern IM im;

int InitExtraIM(IM *im,EXTRA_IM *eim,const char *arg);
int LoadExtraIM(IM *im,const char *fn);
const char *YongFullChar(int key);
const char *YongGetPunc(int key,int bd,int peek);
EXTRA_IM *YongCurrentIM(void);

#define CURRENT_EIM YongCurrentIM

#endif/*_FCITX_H_*/
