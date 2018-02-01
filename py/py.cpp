#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <pinyin.h>

#include "py.h"

extern "C" 
{
#include "yong.h"
#include "lmacros.h"
#include "llib.h"
}

#define TRACE(fmt, ...) do { \
    fprintf(stderr, "%s" #fmt "\n", __func__, ##__VA_ARGS__); \
} while (0)

static struct PYContext {
    pinyin_context_t *py_ctx;
    pinyin_instance_t * py_instance;
} CTX;

static int PY_Init(const char *arg);
static void PY_Reset(void);
static char *PY_GetCandWord(int index);
static int PY_GetCandWords(int mode);
static int PY_Destroy(void);
static int PY_DoInput(int key);

L_EXPORT(EXTRA_IM EIM) {
    "py",
    0,
    PY_Reset,
    PY_DoInput,
    PY_GetCandWords,
    PY_GetCandWord,
    PY_Init,
    PY_Destroy,
};

int PY_Init(const char *arg)
{
    TRACE();
    CTX.py_ctx = pinyin_init("/usr/lib/x86_64-linux-gnu/libpinyin/data", "/home/sonald/.yong/data");
    strcpy(EIM.Name, "libpinyin");
	EIM.Reset			=	PY_Reset;
	EIM.DoInput		=	PY_DoInput;
	EIM.GetCandWords	=	PY_GetCandWords;
	EIM.GetCandWord	=	PY_GetCandWord;
	EIM.Init			=	PY_Init;
	EIM.Destroy		=	PY_Destroy;

    pinyin_option_t options = PINYIN_CORRECT_ALL | USE_DIVIDED_TABLE | USE_RESPLIT_TABLE | DYNAMIC_ADJUST;
    pinyin_set_options(CTX.py_ctx, options);
    CTX.py_instance = pinyin_alloc_instance(CTX.py_ctx);
    return 0;
}

void PY_Reset(void)
{
    TRACE();
	EIM.CodeInput[0]=0;
	EIM.CodeLen=0;
	EIM.CurCandPage=EIM.CandPageCount=EIM.CandWordCount=0;
    EIM.CaretPos = 0;
	EIM.SelectIndex=0;
}

static char *u2gb(const char *u)
{
	char data[256];
	l_utf8_to_gb(u,data,sizeof(data));
	return l_strdup(data);
}

char *PY_GetCandWord(int index)
{
    TRACE("index = %d", index);
    strcat(EIM.StringGet, EIM.CandTable[index]);
    return EIM.StringGet;
}

int PY_GetCandWords(int mode)
{
    TRACE();

    pinyin_parse_more_full_pinyins(CTX.py_instance, EIM.CodeInput);
    pinyin_guess_sentence_with_prefix(CTX.py_instance, "");
    pinyin_guess_full_pinyin_candidates(CTX.py_instance, 0);

    guint len = 0;
    pinyin_get_n_candidate(CTX.py_instance, &len);
    EIM.CandWordCount = MIN(EIM.CandWordMax, len);
    for (size_t i = 0; i < MIN(EIM.CandWordMax, len); ++i) {
        lookup_candidate_t * candidate = NULL;
        pinyin_get_candidate(CTX.py_instance, i, &candidate);

        const char* word = NULL;
        pinyin_get_candidate_string(CTX.py_instance, candidate, &word);
        printf("%s\t", word);
        char* gb = u2gb(word);
        strcpy(EIM.CandTable[i], gb);
        l_free(gb);
    }

    printf("\n");

    //pinyin_train(CTX.py_instance);
    //pinyin_reset(CTX.py_instance);
    //pinyin_save(CTX.py_ctx);
    return IMR_DISPLAY;
}

int PY_Destroy(void)
{
    TRACE();

    pinyin_free_instance(CTX.py_instance);
    CTX.py_instance = NULL;

    pinyin_mask_out(CTX.py_ctx, 0x0, 0x0);
    pinyin_save(CTX.py_ctx);
    pinyin_fini(CTX.py_ctx);
    CTX.py_ctx = NULL;
    return 0;
}

int PY_DoInput(int key)
{
    TRACE("%c", key);
    if (key >= 'a' && key <= 'z') {
        EIM.CodeInput[EIM.CaretPos++] = key;
        EIM.CodeLen++;
        EIM.CodeInput[EIM.CodeLen] = 0;

        PY_GetCandWords(0);
        return IMR_DISPLAY;
    }
    return IMR_NEXT;
}

