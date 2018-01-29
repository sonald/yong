#include "mb.h"
#define GB_LOAD_NORMAL
#include "gbk.h"
#include "pinyin.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

#include "llib.h"

#include "fuzzy.h"

/* somethin only used to debug */
#define MB_DEBUG		0
#define MB_LEAK			0

#if 0
void MB_LOG(const char *fmt,...)
{
	static FILE *fp;
	va_list ap;
	if(!fmt)
	{
		if(fp)
		{
			fclose(fp);
			fp=NULL;
		}
		return;
	}
	if(!fp) fp=y_mb_open_file("log.txt","w");
	if(!fp) return;
	va_start(ap,fmt);
	vfprintf(fp,fmt,ap);
	va_end(ap);
	fflush(fp);
}
#else
#define MB_LOG(...)
#endif

/* prev delcare */
int mb_load_data(struct y_mb *mb,FILE *fp,int dic);
static int mb_load_user(struct y_mb *mb,const char *fn);

/* mb slice start */
#define MB_SLICE_COUNT	8
static void *mb_slice_list;
struct mb_slice{
	uint8_t *p;
	void *idle;
	int left;
	int block;
	int step;
};
static struct mb_slice mb_slices[MB_SLICE_COUNT];

static void mb_slice_init(int n,...)
{
#if !MB_LEAK
	int i,j,init,block,step;
	va_list ap;
	struct mb_slice *ms;
	uint8_t *p;
	
	va_start(ap,n);
	for(i=0;i<n;i++)
	{
		init=va_arg(ap,int);
		block=va_arg(ap,int);
		step=va_arg(ap,int);
		for(j=0;j<MB_SLICE_COUNT;j++)
		{
			ms=&mb_slices[j];
			if(ms->block==block || ms->block==0)
				break;
		}
		assert(j!=MB_SLICE_COUNT);
		ms->block=block;
		ms->left+=init;
		ms->step=MAX(ms->step,step);
	}
	va_end(ap);
	for(i=0;i<MB_SLICE_COUNT;i++)
	{
		ms=&mb_slices[i];
		if(!ms->block)
			break;
		p=malloc(sizeof(void*)+ms->left*ms->block);
		ms->p=p+sizeof(void*);
		*(void**)p=mb_slice_list;
		mb_slice_list=p;
		//printf("%d %d\n",ms->block,ms->left);
	}
#endif
}

static void mb_slice_clean(void)
{
#if !MB_LEAK
	void *p,*n;
	if(!mb_slice_list)
		return;
	p=mb_slice_list;
	while(p)
	{
		n=*(void**)p;
		free(p);
		p=n;
	}
	mb_slice_list=NULL;
#endif
}

#if 0
int mb_slice_dump(void)
{
	struct mb_slice *ms;
	int i;
	for(i=0;i<MB_SLICE_COUNT;i++)
	{
		ms=&mb_slices[i];
		if(!ms->block)
			continue;
		printf("slice %d %d %d\n",ms->block,ms->left+l_slist_length(ms->idle),ms->step);
	}
	return 0;
}
#endif

static void *mb_slice_alloc(size_t size)
{
#if MB_LEAK
	return malloc(size);
#else
	struct mb_slice *ms;
	int i;
	void *ret;
	for(i=0;i<MB_SLICE_COUNT;i++)
	{
		ms=mb_slices+i;
		if(ms->block!=size)
			continue;
		if(ms->idle)
		{
			ret=ms->idle;
			ms->idle=*(void**)ret;
			return ret;
		}
		if(!ms->left)
		{
			uint8_t *p;
			p=malloc(sizeof(void*)+ms->block*ms->step);
			ms->p=p+sizeof(void*);
			ms->left=ms->step;
			*(void**)p=mb_slice_list;
			mb_slice_list=p;
			//printf("%d %d\n",i,ms->block);
		}
		ms->left--;
		ret=ms->p;
		ms->p+=ms->block;
		return ret;
	}
	assert(i!=MB_SLICE_COUNT);
	return NULL;
#endif
}

static void mb_slice_free1(size_t size,void *p)
{
#if MB_LEAK
	free(p);
#else
	struct mb_slice *ms;
	int i;
	for(i=0;i<MB_SLICE_COUNT;i++)
	{
		ms=mb_slices+i;
		if(ms->block!=size)
			continue;
		*(void**)p=ms->idle;
		ms->idle=p;
		return;
	}
	assert(i!=MB_SLICE_COUNT);
#endif
}

static void mb_slice_free_chain1(size_t size,void *p)
{
#if MB_LEAK
	void *n;
	while(p)
	{
		n=*(void**)p;
		free(p);
		p=n;
	}
#else
	struct mb_slice *ms;
	int i;
	void *n;

	for(i=0;i<MB_SLICE_COUNT;i++)
	{
		ms=mb_slices+i;
		if(ms->block!=size)
			continue;
		while(p)
		{
			n=*(void**)p;
			*(void**)p=ms->idle;
			ms->idle=p;
			p=n;
		}
		return;
	}
	assert(i!=MB_SLICE_COUNT);
#endif
}

#define mb_slice_new(t) mb_slice_alloc(sizeof(t))
#define mb_slice_free(t,p) mb_slice_free1(sizeof(t),p)
#define mb_slice_free_chain(t,p) mb_slice_free_chain1(sizeof(t),(p))

/* mb memory start */

static void *mb_malloc(size_t size)
{
#if L_WORD_SIZE==32
	if(size<=8)
		return mb_slice_alloc(8);
	else 
#endif
	if(size<=12)
		return mb_slice_alloc(12);
	else if(size<=16)
		return mb_slice_alloc(16);
	else
		return malloc(size);
}

static void mb_free(void *p,size_t size)
{
#if L_WORD_SIZE==32
	if(size<=8)
		return mb_slice_free1(8,p);
	else 
#endif
	if(size<=12)
		return mb_slice_free1(12,p);
	else if(size<=16)
		return mb_slice_free1(16,p);
	else
		free(p);
}

static void *mb_strdup(char *in)
{
	int l;
	l=strlen(in)+1;
	return memcpy(mb_malloc(l),in,l);
}

static void mb_strfree(char *p)
{
	int l;
	l=strlen(p)+1;
	mb_free(p,l);
}

/* mb slist start */
typedef void *mb_slist_t;
#define mb_slist_free(t,h) mb_slice_free_chain(t,h)

#define mb_slist_prepend(h,n) l_slist_prepend((h),(n))
#define mb_slist_append(h,n) l_slist_append((h),(n))

static mb_slist_t mb_slist_insert(mb_slist_t h,mb_slist_t n,int pos)
{
	int i;
	void **prev=0,**cur=h;
	for(i=0;i<pos && cur;i++)
	{
		prev=cur;
		cur=*cur;
	}
	*(void**)n=cur;
	if(prev)
	{
		*prev=n;
		return h;
	}
	return n;
}

static mb_slist_t mb_slist_insert_custom(mb_slist_t h,mb_slist_t n,int pos,bool (*cb)(void *))
{
	int i;
	void **prev=0,**cur=h;
	for(i=0;i<pos && cur;)
	{
		if(cb(cur))
			i++;
		prev=cur;
		cur=*cur;
	}
	*(void**)n=cur;
	if(prev)
	{
		*prev=n;
		return h;
	}
	return n;
}

static int mb_slist_pos_custom(mb_slist_t h,mb_slist_t n,bool (*cb)(void *))
{
	int i;
	void **cur=h;
	for(i=0;cur;)
	{
		if(cur==n)
			return i;
		if(cb(cur))
			i++;
		cur=*cur;
	}
	return -1;
}

static inline mb_slist_t mb_slist_insert_after(mb_slist_t h,mb_slist_t p,mb_slist_t n)
{
	*(void**)n=*(void**)p;
	*(void**)p=n;
	return h;
}

static mb_slist_t mb_slist_insert_before(mb_slist_t h,mb_slist_t p,mb_slist_t n)
{
	void **t;
	*(void**)n=p;
	if(p==h)
		return n;
	for(t=h;*t && *t!=p;t=*t);
	*t=n;	
	return h;
}

static void mb_slist_foreach(mb_slist_t h,void (*cb)(void *,void *),void *user)
{
	void *p=h;
	
	while(p)
	{
		cb(p,user);
		p=*(void**)p;
	}
}

#define mb_slist_count(h) l_slist_length(h)
#define mb_slist_nth(h,n) l_slist_nth((h),(n))
#define mb_slist_remove(h,n) l_slist_remove((h),(n))

#if MB_DEBUG
void mb_slist_assert(void *p)
{
	int i;
	for(i=0;i<Y_MB_APPEND;i++)
	{
		if(!p) return;
		p=*(void**)p;
	}
	abort();
}
#define MB_SLIST_ASSERT(p) mb_slist_assert(p)
#else
#define MB_SLIST_ASSERT(p)
#endif

static unsigned mb_int_hash(void *v)
{
	return *(unsigned*)((uint8_t*)v+2*sizeof(void*));
}

static int mb_int_cmp(const void *v1,const void *v2)
{
	unsigned a,b;
	a=*(unsigned*)((uint8_t*)v1+2*sizeof(void*));
	b=*(unsigned*)((uint8_t*)v2+2*sizeof(void*));
	return (int)(a-b);
}

static void mb_hash_foreach(LHashTable *h,LFunc cb,void *arg)
{
	LHashIter iter;
	l_hash_iter_init(&iter,h);
	while(!l_hash_iter_next(&iter))
	{
		cb(l_hash_iter_data(&iter),arg);
	}
}

#define mb_hash_insert(h,v) l_hash_table_insert(h,v)
#define mb_hash_find(h,v) l_hash_table_find((h),(v))
#define mb_hash_free(h,f) l_hash_table_free((h),(LFreeFunc)f)
#define mb_hash_new(a) l_hash_table_new(a,mb_int_hash,mb_int_cmp)

#if defined(__arm__) || defined(EMSCRIPTEN) || defined(__mips__)
static inline uint16_t mb_read_u16(const void *p)
{
	return ((uint8_t*)p)[0]|(((uint8_t*)p)[1]<<8);
}

static inline uint32_t mb_read_u32(const void *p)
{
	return ((uint8_t*)p)[0]|(((uint8_t*)p)[1]<<8)|
			(((uint8_t*)p)[2]<<16)|(((uint8_t*)p)[3]<<24);
}
#else
#define mb_read_u16(p) *(uint16_t*)(p)
#define mb_read_u32(p) *(uint32_t*)(p)
#endif

#if L_WORD_SIZE>=64

static const uintptr_t mb_key_mask[]={
	0x3FLL<<56,0x3fLL<<50,0x3fLL<<44,0x3fLL<<38,0x3fLL<<32,
	0x3FLL<<26,0x3fLL<<20,0x3fLL<<14,0x3fLL<<8,0x3fLL << 2,
};

static const int mb_key_shift[]={
	56,50,44,38,32,
	26,20,14,8,2,
};

static const uintptr_t mb_key_part[]={
	0,
	(0x3fLL<<56),
	(0x3fLL<<56)|(0x3fLL<<50),
	(0x3fLL<<56)|(0x3fLL<<50)|(0x3fLL<<44),
	(0x3fLL<<56)|(0x3fLL<<50)|(0x3fLL<<44)|(0x3fLL<<38),
	(0xffffffffLL<<32),
	(0xffffffffLL<<32)|(0x3fLL<<26),
	(0xffffffffLL<<32)|(0x3fLL<<26)|(0x3fLL<<20),
	(0xffffffffLL<<32)|(0x3fLL<<26)|(0x3fLL<<20)|(0x3fLL<<14),
	(0xffffffffLL<<32)|(0x3fLL<<26)|(0x3fLL<<20)|(0x3fLL<<14)|(0x3fLL<<8),
	0xffffffffffffffffULL
};

#else

static const uintptr_t mb_key_mask[]={
	0x3FL<<26,0x3fL<<20,0x3fL<<14,0x3fL<<8,0x3fL << 2,
};

static const int mb_key_shift[]={
	26,20,14,8,2,
};

static const  uintptr_t mb_key_part[]={
	0,
	(0x3fL<<26),
	(0x3fL<<26)|(0x3fL<<20),
	(0x3fL<<26)|(0x3fL<<20)|(0x3fL<<14),
	(0x3fL<<26)|(0x3fL<<20)|(0x3fL<<14)|(0x3fL<<8),
	0xffffffffUL
};
#endif

void y_mb_key_map_init(const char *key,int wildcard,char *map)
{
	if(key)
	{
		int i,c;
		for(i=1;(c=key[i])!=0;i++)
			map[c]=i;
	}
	if(wildcard)
	{
		map[wildcard]=Y_MB_WILDCARD;
	}
}

static uintptr_t mb_key_conv(struct y_mb *mb,const char *in,int len)
{
	int i,c;
	if(len<=Y_MB_KEY_CP)
	{
		uintptr_t p=Y_MB_KEY_MARK;
		for(i=0;i<len;i++)
		{
			c=in[i];
			c=mb->map[c];
			p|=((uintptr_t)c)<<mb_key_shift[i];
		}
		return p;
	}
	else
	{
		L_ALIGN(static char out[Y_MB_KEY_SIZE+1],sizeof(int));
		for(i=0;i<len;i++)
		{
			c=in[i];
			out[i]=mb->map[c];
		}
		out[i]=0;
		return (uintptr_t)out;
	}	
}

static inline uintptr_t mb_key_dup(uintptr_t in)
{
	if(in & Y_MB_KEY_MARK)
		return in;
	return (uintptr_t)mb_strdup((char*)in);
}

#define mb_key_free(in) \
	do{ \
		if(!((in)&Y_MB_KEY_MARK)) \
			mb_strfree((void*)in); \
	}while(0)

char *mb_key_conv_r(struct y_mb *mb,uint16_t index,uintptr_t in)
{
	static char out[Y_MB_KEY_SIZE+1];
	int i,c,pos=0;
	
	if(index)
	{
		out[0]=mb->key[index>>8];
		out[1]=mb->key[index&0xff];
		if(!out[1])
			pos=1;
		else
			pos=2;
	}

	if(!(in&Y_MB_KEY_MARK))
	{
		for(i=0;(c=((char*)in)[i])!=0;i++)
		{
			out[i+pos]=mb->key[c];
		}
	}
	else
	{
		for(i=0;i<Y_MB_KEY_CP && (c=(in&mb_key_mask[i])>>mb_key_shift[i])!=0;i++)
		{
			out[i+pos]=mb->key[c];
		}
	}
	out[i+pos]=0;
	return out;
}

static inline int mb_key_len(uintptr_t s)
{
	int i;
	if(!(s&Y_MB_KEY_MARK))
	{
		for(i=0;(((char*)s)[i])!=0;i++);
	}
	else
	{
		for(i=0;i<Y_MB_KEY_CP && (s&mb_key_mask[i])!=0;i++);
	}
	return i;
}

static int mb_key_is_part(uintptr_t full,uintptr_t part)
{
	int i,c1,c2;
	
	c1=full&Y_MB_KEY_MARK;c2=part&Y_MB_KEY_MARK;
	if(c1 && !c2)
		return 0;
	if(c1 && c2)
	{
		for(i=0;i<Y_MB_KEY_CP;i++)
		{
			c1=(full&mb_key_mask[i])>>mb_key_shift[i];
			c2=(part&mb_key_mask[i])>>mb_key_shift[i];
			if(!c2) break;
			if(c1!=c2)
				return 0;
		}
	}
	else if (!c1 && c2)
	{
		for(i=0;i<Y_MB_KEY_CP;i++)
		{
			c1=((char*)full)[i];
			c2=(part&mb_key_mask[i])>>mb_key_shift[i];
			if(c1!=c2)
				return 0;
		}
	}
	else
	{
		for(i=0;i<Y_MB_KEY_CP;i++)
		{
			c1=((char*)full)[i];
			c2=((char*)part)[i];
			if(!c2) break;
			if(c1!=c2)
				return 0;
		}
	}
	return 1;
}

static int mb_key_cmp_direct(uintptr_t s1,uintptr_t s2,int n)
{
	int c1,c2,ret;
	
	c1=s1&Y_MB_KEY_MARK;c2=s2&Y_MB_KEY_MARK;
	if(c1 && c2)
	{
		int m=MIN(n,Y_MB_KEY_CP);
		s1&=mb_key_part[m];s2&=mb_key_part[m];
		if(s1>s2) return 1;
		else if(s1==s2) return 0;
		return -1;
	}
	else if(c1 && !c2)
	{
		int m=MIN(n,Y_MB_KEY_CP);
		int i;
		for(i=0;i<m;i++)
		{
			c1=(s1&mb_key_mask[i])>>mb_key_shift[i];
			c2=((char*)s2)[i];
			ret=c1-c2;
			if(ret)
				return ret;
			/* c1!=0 here */
		}
		return (n>m)?-1:0;
	}
	else if(c2) /* !c1 && c2  */
	{
		int m=MIN(n,Y_MB_KEY_CP);
		int i;
		for(i=0;i<m;i++)
		{
			c1=((char*)s1)[i];
			c2=(s2&mb_key_mask[i])>>mb_key_shift[i];
			ret=c1-c2;
			if(ret)
				return ret;
			/* c2!=0 here */
		}
		return (n>m)?1:0;
	}
	else /* !c1 && !c2 */
	{
		return strncmp((char*)s1,(char*)s2,n);
	}
}

static int mb_key_cmp_wildcard(uintptr_t s1,uintptr_t s2,int n)
{
	int i,c1,c2,ret;
	
	c1=s1&Y_MB_KEY_MARK;c2=s2&Y_MB_KEY_MARK;
	if(c1 && c2)
	{
		int m=MIN(n,Y_MB_KEY_CP);
		for(i=0;i<m;i++)
		{
			c1=(s1&mb_key_mask[i])>>mb_key_shift[i];
			c2=(s2&mb_key_mask[i])>>mb_key_shift[i];
			if((c1==Y_MB_WILDCARD && c2) || (c2==Y_MB_WILDCARD && c1))
				continue;
			ret=c1-c2;
			if(ret)
				return ret;
		}
		return 0;
	}
	else if(c1 && !c2)
	{
		int m=MIN(n,Y_MB_KEY_CP);
		for(i=0;i<m;i++)
		{
			c1=(s1&mb_key_mask[i])>>mb_key_shift[i];
			c2=((char*)s2)[i];
			if((c1==Y_MB_WILDCARD && c2) || (c2==Y_MB_WILDCARD && c1))
				continue;
			ret=c1-c2;
			if(ret)
				return ret;
		}
		return (n>m)?-1:0;
	}
	else if(c2) /* !c1 && c2  */
	{
		int m=MIN(n,Y_MB_KEY_CP);
		for(i=0;i<m;i++)
		{
			c1=((char*)s1)[i];
			c2=(s2&mb_key_mask[i])>>mb_key_shift[i];
			if((c1==Y_MB_WILDCARD && c2) || (c2==Y_MB_WILDCARD && c1))
				continue;
			ret=c1-c2;
			if(ret)
				return ret;
		}
		return (n>m)?1:0;
	}
	else /* !c1 && !c2 */
	{
		for(i=0;i<n;i++)
		{
			c1=((char*)s1)[i];
			c2=((char*)s2)[i];
			if(!c1 || !c2)
				return c1-c2;
			if(c1==Y_MB_WILDCARD || c2==Y_MB_WILDCARD)
				continue;
			ret=c1-c2;
			if(ret)
				return ret;
		}
		return 0;
	}
}

static char *mb_data_conv_r(uint32_t in)
{
		static char out[5];
		*(uint32_t*)out=in;
		return out;
}

char *y_mb_ci_string(struct y_mb_ci *ci)
{
	static char out[Y_MB_DATA_SIZE+1];
	int len=ci->len;
	memcpy(out,ci->data,ci->len);
	out[len]=0;
	
	return out;
}

char *y_mb_ci_string2(struct y_mb_ci *ci,char *out)
{
	int len=ci->len;
	memcpy(out,ci->data,ci->len);
	out[len]=0;
	
	return out;
}

static int mb_ci_equal(const struct y_mb_ci *ci,const char *data,int dlen)
{
	if(ci->len!=dlen)
		return 0;
	return memcmp((void*)ci->data,data,dlen)==0;
}

static char enc_key[16];
static int need_enc;
L_EXPORT(int tool_set_key(void *arg,void **out))
{
	memcpy(enc_key,arg,16);
	need_enc=1;
	return 0;
}



static int get_line_iconv(char *line, size_t n, FILE *fp)
{
	char temp[4096];
	int len;
	if(!fgets(temp,sizeof(temp),fp))
		return -1;
	l_utf8_to_gb(temp,line,n);
	len=strcspn(line,"\r\n");
	line[len]=0;
	return len;
}

static int mb_add_rule(struct y_mb *mb,char *s)
{
	struct y_mb_rule *r;
	char c;
	int i;
	
	r=calloc(1,sizeof(*r));
	c=*s++;
	if(c=='a') r->a=1;
	else if(c=='e') r->a=0;
	else goto err;
	c=*s++;
	if(c<'1' || c>'9') goto err;
	r->n=c-'1'+1;
	c=*s++;
	if(r->n==1 && c>='0' && c<='5')
	{
		r->n=10+c-'0';
		c=*s++;
	}
	if(c!='=') goto err;

	for(i=0;i<Y_MB_KEY_SIZE;i++)
	{
		c=*s++;
		if(c=='p')
			r->code[i].r=0;
		else if(c=='n')
			r->code[i].r=1;
		else if(c && mb->map[(int)c])
		{
			r->code[i].d=1;
			r->code[i].p=c;
			goto next;
		}
		else
		{
			printf("not pn. %c\n",c);
			goto err;
		}
		c=*s++;
		if(c=='.')
			r->code[i].i=Y_MB_WILDCARD;
		else if(c>='1' && c<='9')
			r->code[i].i=c-'1'+1;
		else
		{
			printf("1-9.\n");
			goto err;
		}
		c=*s++;
		if(c=='.')
			r->code[i].p=Y_MB_WILDCARD;
		else if(c>='1' && c<='9')
			r->code[i].p=c-'1'+1;
		else if(c=='-' && (c=*s++)>='1' && c<='9')
			r->code[i].p=-(c-'1'+1);
		else
		{
			printf("not 1-9.\n");
			goto err;
		}
next:
		c=*s++;
		if(c==0) break;
		else if(c=='+') continue;
		else
		{
			printf("not end or next\n");
			goto err;
		}
	}
	if(mb->rule==NULL)
	{
		r->next=mb->rule;
		mb->rule=r;
	}
	else
	{
		struct y_mb_rule *p=mb->rule;
		while(p->next) p=p->next;
		p->next=r;		
	}
	return 0;
err:
	free(r);
	return -1;
}

static void mb_rule_dump(struct y_mb *mb,FILE *fp)
{
	struct y_mb_rule *r=mb->rule;
	int i;
	if(!r)
		return;

	while(r)
	{
		if(!r->n)
			break;
		fprintf(fp,"code_%c%d=",r->a?'a':'e',r->n);
		for(i=0;r->code[i].p;i++)
		{
			if(i!=0)
				fprintf(fp,"+");
			if(r->code[i].d)
			{
				fprintf(fp,"%c",r->code[i].p);
				continue;
			}
			fprintf(fp,"%c",r->code[i].r?'n':'p');
			if(r->code[i].i==Y_MB_WILDCARD)
				fprintf(fp,".");
			else
				fprintf(fp,"%d",r->code[i].i);
			if(r->code[i].p==Y_MB_WILDCARD)
				fprintf(fp,".");
			else
				fprintf(fp,"%d",r->code[i].p);
		}
		fprintf(fp,"\n");
		r=r->next;
	}
}

static inline char y_mb_code_n_key(struct y_mb *mb,struct y_mb_code *c,int i)
{
	if(i<0)
		i=c->len+i+1;
	if(c->len<=4)
	{
		return mb->key[(c->val>>(8+i*6))&0x3f];
	}
	else
	{
		return mb->key[c->data[i]];
	}
}

static void y_mb_code_get_string(const struct y_mb *mb,const struct y_mb_code *c,char *out)
{
	int len=c->len,i;
	if(len<=4)
	{
		int val=c->val;
		for(i=0;i<len;i++)
			out[i]=mb->key[(val>>(8+i*6))&0x3f];
	}
	else
	{
		for(i=0;i<len;i++)
			out[i]=mb->key[c->data[i]];
	}
	out[len]=0;
}

static int y_mb_code_cmp(const struct y_mb_code *c1,const struct y_mb_code *c2,int len)
{
	int i,mlen;
	mlen=MIN(c1->len,c2->len);
	for(i=0;i<mlen;i++)
	{
		int t1,t2,ret;
		t1=(c1->len<=4)?((c1->val>>(8+i*6))&0x3f):c1->data[i];
		t2=(c2->len<=4)?((c2->val>>(8+i*6))&0x3f):c2->data[i];
		ret=t1-t2;
		if(ret) return ret;
	}
	if(mlen<len)
	{
		return c1->len-c2->len;
	}
	return 0;
}

static struct y_mb_code *mb_code_for_rule(struct y_mb *mb,struct y_mb_code *c,int n)
{
	struct y_mb_code *ret=NULL;
	char p;
	if(n==Y_MB_WILDCARD)
		return c;
	if(n<0)
		n=-n;
	for(;c!=NULL;c=c->next)
	{
		if(c->virt)
		{
			if(c->len<n)
				return NULL;
			ret=c;
			break;
		}
		if(c->len<n)
			continue;
		if(ret && c->len<=ret->len)
			continue;
		p=y_mb_code_n_key(mb,c,0);
		if(strchr(mb->skip,p))
			continue;
		ret=c;
	}
	return ret;
}

int y_mb_code_by_rule(struct y_mb *mb,const char *s,int len,char *out,...)
{
	struct y_mb_zi *z[Y_MB_DATA_SIZE+1];
	int i;
	struct y_mb_rule *r;
	struct y_mb_zi kz;
	int ret=-1;
	va_list ap;

	out[0]=0;
	r=mb->rule;
	if(!r || len<2 || len%2)
	{
		//printf("bad len %d line: %s\n",len,s);
		return -1;
	}
	/* calc hz count, only allow gbk here */
	len>>=1;
	/* find hz info */
	for(i=0;i<len;i++)
	{
		if(gb_is_gbk((uint8_t*)s))
		{
			kz.data=mb_read_u16(s);
			s+=2;
		}
		else if(gb_is_gb18030_ext((uint8_t*)s))
		{
			kz.data=mb_read_u32(s);
			s+=4;
			len--;
		}
		else 
		{
			//printf("not pure gb18030 string %d/%d:%02x%02x%02x%02x\n",
			//	i,len,(uint8_t)s[0],(uint8_t)s[1],(uint8_t)s[2],(uint8_t)s[3]);
			return -1;
		}
		z[i]=mb_hash_find(mb->zi,&kz);
		if(!z[i])
		{
			//printf("not found zi %04x\n",kz.data);
			return -1;
		}
	}
	va_start(ap,out);
	for(;r&&out;r=r->next)
	{
		int pos=0;
		struct y_mb_code *c;
		if(!r->a && r->n!=len)
			continue;
		if(r->a && r->n>len)
			continue;
		if(pos+1>Y_MB_KEY_SIZE)
			return -1;
		for(i=0;i<Y_MB_KEY_SIZE+1;i++)
		{
			if(r->code[i].p==0)
				break;
			if(r->code[i].d)
			{
				out[pos++]=r->code[i].p;
			}
			else if(r->code[i].i==Y_MB_WILDCARD)
			{
				int j;
				if(i!=0)
					break;
				for(j=0;j<len;j++)
				{
					c=z[j]->code;					
					if(r->code[i].p!=Y_MB_WILDCARD)
					{
						c=mb_code_for_rule(mb,c,r->code[i].p);
						if(pos+1>Y_MB_KEY_SIZE)
						{
							pos=0;
							break;
						}
						out[pos++]=y_mb_code_n_key(mb,c,r->code[i].p-1);
					}
					else
					{
						int len;
						if(c->len+pos>Y_MB_KEY_SIZE)
						{
							pos=0;
							break;
						}
						len=(c->len>mb->len)?mb->len:c->len;
						if(pos+len>Y_MB_KEY_SIZE)
							return -1;
						y_mb_code_get_string(mb,c,out+pos);
						pos+=len;
					}
				}
				break;
			}
			else if(r->code[i].p==Y_MB_WILDCARD)
			{
				if(r->code[i].r)
					c=z[len-r->code[i].i]->code;
				else
					c=z[r->code[i].i-1]->code;
				if(c->len+pos>Y_MB_KEY_SIZE)
				{
					pos=0;
					break;
				}
				if(pos+c->len>Y_MB_KEY_SIZE)
					return -1;
				y_mb_code_get_string(mb,c,out+pos);
				pos+=c->len;	
			}
			else
			{
				if(r->code[i].r)
					c=z[len-r->code[i].i]->code;
				else
					c=z[r->code[i].i-1]->code;
				c=mb_code_for_rule(mb,c,r->code[i].p);
				if(!c)
				{
					pos=0;
					break;
				}
				out[pos++]=y_mb_code_n_key(mb,c,r->code[i].p-1);
			}
		}
		out[pos]=0;
		if(out[0]) ret=0;
		if(pos>0)
		{
			out=va_arg(ap,char*);
			if(out) out[i]=0;
		}
	}
	va_end(ap);
	return ret;
}

static struct y_mb_code *mb_code_new(struct y_mb *mb,const char *s,int len)
{
	struct y_mb_code *c;
	int i;
	int size=sizeof(struct y_mb_code);
	if(len>4) size+=len-3;
	c=mb_malloc(size);
	c->val=0;
	c->len=len;
	if(len<=4)
	{
		for(i=0;i<len;i++)
			c->val|=mb->map[(int)s[i]]<<(i*6+8);
	}
	else
	{
		for(i=0;i<len;i++)
			c->data[i]=mb->map[(int)s[i]];
	}
	return c;
}

static void mb_code_free(struct y_mb_code *c)
{
	int size=sizeof(struct y_mb_code);
	if(c->len>4)
		size+=c->len-3;
	mb_free(c,size);
}

static void mb_zi_free(struct y_mb_zi *p)
{
	l_slist_free(p->code,(LFreeFunc)mb_code_free);
	mb_slice_free1(sizeof(struct y_mb_zi),p);
}

static struct y_mb_ci *mb_ci_new(const char *data,int dlen)
{
	int size;
	struct y_mb_ci *c;
#if L_WORD_SIZE==32
	if(dlen<=2)
		size=sizeof(struct y_mb_ci);
	else
		size=sizeof(struct y_mb_ci)-2+dlen;
#else
	if(dlen<=6)
		size=sizeof(struct y_mb_ci);
	else
		size=sizeof(struct y_mb_ci)-6+dlen;
#endif
	c=mb_malloc(size);
	c->len=dlen;
	memcpy(c->data,data,dlen);

	return c;
}

static void mb_ci_free(struct y_mb_ci *ci)
{
	int size;
#if L_WORD_SIZE==32
	if(ci->len<=2)
		size=sizeof(struct y_mb_ci);
	else
		size=sizeof(struct y_mb_ci)-2+ci->len;
#else
	if(ci->len<=6)
		size=sizeof(struct y_mb_ci);
	else
		size=sizeof(struct y_mb_ci)-6+ci->len;
#endif
	mb_free(ci,size);
}

struct y_mb_ci *mb_ci_shadow(struct y_mb_ci *ci)
{
	int size;
	struct y_mb_ci *c;
	int dlen=ci->len;
#if L_WORD_SIZE==32
	if(dlen<=2)
		size=sizeof(struct y_mb_ci);
	else
		size=sizeof(struct y_mb_ci)-2+dlen;
#else
	if(dlen<=6)
		size=sizeof(struct y_mb_ci);
	else
		size=sizeof(struct y_mb_ci)-6+dlen;
#endif
	c=mb_malloc(size);
	memcpy(c,ci,size);
	return c;
}

static inline uint16_t mb_ci_index(struct y_mb *mb,const char *code,int len,uintptr_t *key)
{
	int c1,c2;
	c1=mb->map[(int)code[0]];
	if(len==1 || mb->nsort)
		c2=0;
	else
		c2=mb->map[(int)code[1]];
	if(key)
	{
		if(mb->nsort)
			*key=mb_key_conv(mb,code+1,len-1);
		else
			*key=mb_key_conv(mb,code+2,len-2);
	}
	return (uint16_t)(c1<<8)|c2;
}

static inline int mb_ci_index_code_len(int index)
{
	int len=0;
	if(index&0xff)
		len++;
	if(index&0xff00)
		len++;
	return len;
}

#if 0
static inline uint16_t mb_ci_index_wildcard(struct y_mb *mb,char *code,int len,char wildcard,uintptr_t *key)
{
	int c1,c2;
	if(wildcard && code[0]==wildcard && !mb->dwf)
		c1=Y_MB_WILDCARD;
	else
		c1=mb->map[(int)code[0]];
	if(len==1 || mb->nsort)
	{
		c2=0;
	}
	else
	{
		if(wildcard && code[1]==wildcard)
			c2=Y_MB_WILDCARD;
		else
			c2=mb->map[(int)code[1]];
	}
	if(key)
	{
		if(mb->nsort)
			*key=mb_key_conv(mb,code+1,len-1);
		else
			*key=mb_key_conv(mb,code+2,len-2);
	}
	return (uint16_t)(c1<<8)|c2;
}
#else
#define mb_ci_index_wildcard(mb,code,len,wildcard,key) mb_ci_index(mb,code,len,key)
#endif

static inline int mb_index_cmp_direct(uint16_t i1,uint16_t i2,int n)
{
	if(n==1)
		return (i1>>8)-(i2>>8);
	else
		return i1-i2;
}

/* wildcard in i2 is not treated as wildcard */
static int mb_index_cmp_wildcard(struct y_mb *mb,uint16_t i1,uint16_t i2,int n,char wildcard)
{
	int ret;
	int c1,c2;
	
	if(!wildcard)
		return mb_index_cmp_direct(i1,i2,n);

	/* if not equal length */
	if(!(i1&0xff) != !(i2&0xff))
		return 1;

	c1=i1>>8;c2=i2>>8;
	if(n==1)
	{
		if(!mb->dwf && c1==Y_MB_WILDCARD)
			return 0;
		return c1-c2;
	}
	ret=c1-c2;
	if(ret && (c1!=Y_MB_WILDCARD || mb->dwf))
		return ret;
	c1=i1&0xff;c2=i2&0xff;
	ret=c1-c2;
	if(!c1 || !c2)
		return ret;
	if(c1==Y_MB_WILDCARD)
		return 0;
	return ret;
}

static void mb_item_free1(struct y_mb_item *it)
{
	mb_key_free(it->code);
	l_slist_free(it->phrase,(LFreeFunc)mb_ci_free);
}

static void mb_item_free2(struct y_mb_item *it,void *unused)
{
	mb_item_free1(it);
}

int y_mb_find_code(struct y_mb *mb,const char *hz,char (*tab)[MAX_CAND_LEN+1],int max)
{
	int len;
	struct y_mb_zi *z,kz;
	struct y_mb_code *p;
	int i;
	
	if(!mb->zi) return 0;
	
	len=strlen(hz);
	if(len!=2 && len!=4)
		return 0;
	if(len==2)
	{
		if(!gb_is_gbk((uint8_t*)hz))
			return 0;
		kz.data=mb_read_u16(hz);
	}
	else
	{
		if(!gb_is_gb18030_ext((uint8_t*)hz))
			return 0;
		kz.data=mb_read_u32(hz);
	}
	z=mb_hash_find(mb->zi,&kz);
	if(!z)
		return 0;
	for(p=z->code,i=0;p && i<max;p=p->next)
	{
		if(p->virt)
		{
			continue;
		}
		if(mb->flag & MB_FLAG_ASSIST)
		{
			//sprintf(tab[i],"@%s",mb_key_conv_r(mb,0,p->data));
			tab[i][0]='@';
			y_mb_code_get_string(mb,p,tab[i]+1);
		}
		else
		{
			//strcpy(tab[i],mb_key_conv_r(mb,0,p->data));
			y_mb_code_get_string(mb,p,tab[i]);
		}
		i++;
	}
	if(mb->ass && i<max-1)
	{
		i+=y_mb_find_code(mb->ass,hz,tab+i,max-i);
	}
	else if(mb->flag & MB_FLAG_ADICT && i<max-1)
	{
		struct y_mb_index *index;
		struct y_mb_item *item;
		struct y_mb_ci *c;
		for(index=mb->index;index;index=index->next)
		{
			if(index->ci_count==0)
				continue;
			for(item=index->item;item;item=item->next)
			{
				for(c=item->phrase;c;c=c->next)
				{
					if(c->del || !c->zi || c->dic!=Y_MB_DIC_ASSIST)
						continue;
					if(!mb_ci_equal(c,hz,len))
						continue;
					char *code=mb_key_conv_r(mb,index->index,item->code);
					sprintf(tab[i],"@%s",code);
					i++;
					if(i>=max-1) goto out;
				}
			}
		}
out:;
	}
	return i;
}

int y_mb_find_simple_code(struct y_mb *mb,const char *hz,const char *code,char *out,int filter,int total)
{
	int len;
	struct y_mb_zi *z,kz;
	struct y_mb_code *p;
	int minimum;
	struct y_mb_context ctx;
	
	if(!mb->zi) return 0;
	
	len=strlen(hz);
	if(len!=2 && len!=4)
		return 0;
	if(len==2)
	{
		if(!gb_is_gbk((uint8_t*)hz))
			return 0;
		kz.data=mb_read_u16(hz);
	}
	else
	{
		if(!gb_is_gb18030_ext((uint8_t*)hz))
			return 0;
		kz.data=mb_read_u32(hz);
	}
	z=mb_hash_find(mb->zi,&kz);
	if(!z)
		return 0;
	len=minimum=strlen(code);
	y_mb_push_context(mb,&ctx);
	for(p=z->code;p!=NULL;p=p->next)
	{
		char temp[len];
		struct y_mb_ci *c;
		char *s;
		int clen;
		int pos;
		if(p->virt)
			continue;
		if(p->len>minimum)
			continue;
		y_mb_code_get_string(mb,p,temp);
		clen=strlen(temp);
		if(0==y_mb_set(mb,temp,clen,filter))
			continue;
		for(pos=0,c=mb->ctx.result_first->phrase;c!=NULL && pos<=total;c=c->next)
		{
			if(c->del) continue;
			if(filter && c->zi && c->ext) continue;
			pos++;
			if(!c->zi) continue;
			s=y_mb_ci_string(c);
			if(strcmp(s,hz)) continue;
			break;
		}
		if(!c) continue;
		if(pos>total) continue;
		s=y_mb_ci_string(c);
		if(strcmp(s,hz)) continue;
		if(y_mb_is_stop(mb,temp[clen-1],clen) && !y_mb_has_next(mb,filter))
			clen--;
		if(clen>=minimum) continue;
		if(!strcmp(code,temp)) continue;
		strcpy(out,temp);
		minimum=clen;
	}
	y_mb_pop_context(mb,&ctx);
	if(minimum<len)
		return 1;
	return 0;
}

struct y_mb_zi *mb_find_zi(struct y_mb*mb,const char *s)
{
	struct y_mb_zi *z,kz;
	if(gb_is_gbk((uint8_t*)s))
	{
		kz.data=mb_read_u16(s);
	}
	else if(gb_is_gb18030_ext((uint8_t*)s))
	{
		kz.data=mb_read_u32(s);
	}
	else
	{
		return 0;
	}
	z=mb_hash_find(mb->zi,&kz);
	return z;
}

/* 如果设定了组词码，那么就只有组词码才是好的编码 */
int y_mb_is_good_code(struct y_mb *mb,const char *code,const char *s)
{
	struct y_mb_zi *z;
	char key[Y_MB_KEY_SIZE+1];
	int clen;
		
	z=mb_find_zi(mb,s);
	if(!z || !z->code) return 1;
	if(!z->code->virt) return 1;
	clen=strlen(code);
	if(clen>z->code->len)
		return 0;
	y_mb_code_get_string(mb,z->code,key);
	return !strncmp(code,key,clen);
}

int y_mb_get_full_code(struct y_mb *mb,const char *data,char *code)
{
	struct y_mb_zi *z;
	struct y_mb_code *full=0,*p,*virt=0;
	int virt_found=0;

	z=mb_find_zi(mb,data);
	if(!z)
		return -1;
	for(p=z->code;p;p=p->next)
	{
		if(p->virt)
		{
			virt=p;
			continue;
		}
		if(p->len>mb->len)
			continue;
		if(!full)
		{
			full=p;
		}
		else if(p->len>=full->len)
		{
			if(!virt_found)
				full=p;
			if(virt && virt->len<=p->len && 
					!y_mb_code_cmp(virt,p,virt->len))
			{
				full=p;
				virt_found=1;
			}
		}
		else
		{
			if(!virt || virt_found) continue;
			if(virt->len<=p->len && !y_mb_code_cmp(virt,p,virt->len))
			{
				full=p;
				virt_found=1;
			}
		}
	}
	if(!full && virt) full=virt;
	if(!full) return -1;
	y_mb_code_get_string(mb,full,code);
	return 0;
}

void y_mb_calc_yong_tip(struct y_mb *mb,const char *code,const char *cand,char *tip)
{
	int len;
	struct y_mb_zi *z;
	struct y_mb_code *p;
	char temp[Y_MB_KEY_SIZE+1];
	int prev=5;
	
	z=mb_find_zi(mb,cand);
	if(!z)
	{
		return;
	}
	len=strlen(code);
	for(p=z->code;p;p=p->next)
	{
		if(p->virt || p->len<=len || p->len>prev)
			continue;			
		y_mb_code_get_string(mb,p,temp);
		if(!memcmp(code,temp,len))
		{
			if(!tip[0] || mb->map[(int)temp[len]]<mb->map[(int)tip[0]])
				tip[0]=temp[len];
			tip[1]=0;
			prev=p->len;
		}
	}
	return;
}

static struct y_mb_zi *mb_add_zi(struct y_mb *mb,const char *code,int clen,const char *data,int dlen,int mv)
{
	struct y_mb_code *c;
	struct y_mb_zi *z;
	int virt=(mv&0x01);

	if(!mb->zi)
		return 0;
	
	z=mb_find_zi(mb,data);
	if(z)
	{
		struct y_mb_code *p;
		for(p=z->code;p;p=p->next)
		{
			/* code equal is the same virt and code it self */
			char temp[Y_MB_KEY_SIZE+1];
			if(clen!=p->len || virt!=p->virt)
				continue;
			y_mb_code_get_string(mb,p,temp);
			if(!memcmp(temp,code,clen))
			{
				z->code=mb_slist_remove(z->code,p);
				mb_code_free(p);
				break;
			}
		}
	}
	else
	{
		uint32_t key;
		if(mv==-1)
			return 0;
			
		if(dlen==2)
			key=mb_read_u16(data);
		else
			key=mb_read_u32(data);

		z=mb_slice_new(struct y_mb_zi);
		z->code=0;
		z->data=key;
		mb_hash_insert(mb->zi,z);
	}
	if(mv==-1)
		return 0;
	c=mb_code_new(mb,code,clen);
	c->virt=virt;
	c->main=(mv&0x02)?1:0;
	if(c->virt || !z->code || !z->code->virt)
	{
		z->code=mb_slist_prepend(z->code,c);
	}
	else
	{
		z->code=mb_slist_append(z->code,c);
	}
	return z;
}

static struct y_mb_index *mb_get_index(struct y_mb *mb,uint16_t code)
{
	struct y_mb_index *p;
	int ret;
	
	if((p=mb->half)!=NULL)
	{
		ret=(int)p->index-(int)code;
		if(ret==0) return p;
		else if(ret>0) p=mb->index;
	}
	else
	{
		p=mb->index;
	}
	while(p)
	{
		ret=(int)p->index-(int)code;
		if(ret==0)
			return p;
		if(ret>0)
			break;
		p=p->next;
	}
	return 0;
}

static struct y_mb_item *mb_get_item(struct y_mb_index *index,uintptr_t key)
{
	struct y_mb_item *p;
	int ret;
	int mark;
	
	if((p=index->half)!=NULL)
	{
		ret=mb_key_cmp_direct(p->code,key,Y_MB_KEY_SIZE);
		if(ret==0) return p;
		else if(ret>0) p=index->item;
		else p=p->next;
	}
	else
	{
		p=index->item;
	}
	mark=key&Y_MB_KEY_MARK;
	for(;p!=NULL;p=p->next)
	{
		if(mark!=(p->code&Y_MB_KEY_MARK))
			continue;
		ret=mb_key_cmp_direct(p->code,key,Y_MB_KEY_SIZE);
		if(ret==0)
			return p;
		if(ret>0)
			break;
	}
	return 0;
}

static void mb_index_free1(struct y_mb_index *index)
{
	mb_slist_foreach(index->item,(LFunc)mb_item_free2,0);
	mb_slist_free(struct y_mb_item,index->item);
}

static void mb_index_free2(struct y_mb_index *index,void *unused)
{
	mb_index_free1(index);
}

static struct y_mb_index *mb_add_index(struct y_mb *mb,uint16_t code)
{
	struct y_mb_index *p,*prev,*n;
	
	prev=NULL;
	p=mb->index;
	while(p)
	{
		int ret;
		//ret=mb_index_cmp_direct(p->index,code,2);
		ret=p->index-code;
		if(ret==0)
			return p;
		if(ret>0)
			break;
		prev=p;
		p=p->next;
	}
	n=mb_slice_new(struct y_mb_index);
	n->next=p;
	n->item=NULL;
	n->half=NULL;
	n->ci_count=0;
	n->zi_count=0;
	n->ext_count=0;
	n->index=code;
	if(prev)
		prev->next=n;
	else
		mb->index=n;
	return n;
}

static struct y_mb_item *mb_add_one_code_nsort(struct y_mb *mb,struct y_mb_index *index,const char *code,int clen,int pos)
{
	uintptr_t key;
	struct y_mb_item *it;

	it=mb_slice_new(struct y_mb_item);
	it->next=NULL;
	it->phrase=NULL;
	if(clen>1)
	{
		key=mb_key_conv(mb,code+1,clen-1);
		it->code=mb_key_dup(key);
	}
	else
	{
		it->code=Y_MB_KEY_MARK;
	}
	if(pos==0)
	{
		index->item=mb_slist_prepend(index->item,it);
	}
	else if(pos==Y_MB_APPEND)
	{
		if(mb->last_index==index && mb->last_link)
		{
			mb_slist_insert_after(index->item,mb->last_link,it);
			mb->last_link=it;
		}
		else
		{
			index->item=mb_slist_insert(index->item,it,Y_MB_APPEND);
			mb->last_link=it;
			mb->last_index=index;
		}
	}
	else
	{
		index->item=mb_slist_insert(index->item,it,pos);
	}
	return it;
}

static struct y_mb_item *mb_add_one_code(struct y_mb *mb,struct y_mb_index *index,const char *code,int clen)
{
	uintptr_t key;
	struct y_mb_item *h,*l,*it;
	int ret;

	h=l=index->item;
	
	if(!l || clen<=2)
	{
		if(!l || l->code!=Y_MB_KEY_MARK)
		{
			it=mb_slice_new(struct y_mb_item);
			it->phrase=0;
			it->next=NULL;
			if(clen>2)
			{
				key=mb_key_conv(mb,code+2,clen-2);
				it->code=mb_key_dup(key);
			}
			else
			{
				it->code=Y_MB_KEY_MARK;
			}
			l=mb_slist_prepend(l,it);
			index->item=l;
		}
		mb->last_index=index;
		mb->last_link=l;
		return l;
	}
	key=mb_key_conv(mb,code+2,clen-2);
	if(mb->last_index==index && mb->last_link)
	{
		it=mb->last_link;
		ret=mb_key_cmp_direct(key,it->code,Y_MB_KEY_SIZE);
		if(ret==0) /* just got it */
			return it;
		else if(ret>0) /* search from last */
			l=it;
	}
	ret=mb_key_cmp_direct(key,l->code,Y_MB_KEY_SIZE);
	if(ret==0)
	{
		mb->last_index=index;
		mb->last_link=l;
		return l;
	}
	else if(ret<0)
	{
		it=mb_slice_new(struct y_mb_item);
		it->code=mb_key_dup(key);
		it->phrase=0;
		it->next=NULL;
		index->item=mb_slist_insert_before(h,l,it);
		mb->last_index=index;
		mb->last_link=it;
			
		return it;
	}
	while(1)
	{
		if(l->next)
		{
			ret=mb_key_cmp_direct(key,l->next->code,Y_MB_KEY_SIZE);
		}
		if(!l->next || ret<0)
		{
			it=mb_slice_new(struct y_mb_item);
			it->code=mb_key_dup(key);
			it->phrase=0;
			it->next=NULL;
			index->item=mb_slist_insert_after(h,l,it);
			mb->last_index=index;
			mb->last_link=it;
			
			return it;
		}
		else if(ret==0)
		{
			mb->last_index=index;
			mb->last_link=l->next;
	
			return l->next;
		}
		l=l->next;
	}
	/* should never here */
	return NULL;
}

static struct y_mb_item *mb_get_one_code(struct y_mb *mb,struct y_mb_index *index,const char *code,int clen)
{
	uintptr_t key;
	struct y_mb_item *it;
	int ret;

	it=index->item;
	if(!it)
	{
		return NULL;
	}
	/* even clen<2,it can get the good result */
	if(mb->nsort)
		key=mb_key_conv(mb,code+1,clen-1);
	else
		key=mb_key_conv(mb,code+2,clen-2);
	for(;it;it=it->next)
	{
		ret=mb_key_cmp_direct(key,it->code,Y_MB_KEY_SIZE);
		if(ret==0)
		{
			return it;
		}
		else if(ret<0)
		{
			break;
		}
	}
	return NULL;
}

/* only normal data should use this */
static struct y_mb_ci *mb_app_one(struct y_mb *mb,struct y_mb_ci *pos,const char *code,int clen,const char *data,int dlen,int dic)
{
	struct y_mb_ci *c;
	struct y_mb_index *index;
	int revert=0;

	if(data[0]=='~')
	{
		if(dlen==3 && gb_is_gbk((uint8_t*)data+1))
			revert=1;
		else if(dlen==5 && gb_is_gb18030_ext((uint8_t*)data+1))
			revert=1;
		if(revert)
		{
			data++;
			dlen--;
		}
	}
	else if(code[0]=='^') /* is it used to construct ci */
	{
		if(dic==Y_MB_DIC_ASSIST)
			return NULL;
		if((dlen==2 && gb_is_gbk((uint8_t*)data)) ||
					(dlen==4 && gb_is_gb18030_ext((uint8_t*)data)))
		{
			int mv=1;
			if(dic==Y_MB_DIC_MAIN) mv|=0x02;
			code+=1;clen-=1;
			mb_add_zi(mb,code,clen,data,dlen,mv);
			return NULL;
		}
		else
		{
			return NULL;
		}
	}

	c=mb_ci_new(data,dlen);
	c->dic=dic;
	c->del=0;
	c->zi=c->ext=0;
	c->simp=0;

	if((dlen==2 && gb_is_gbk((uint8_t*)data)) ||
				(dlen==4 && gb_is_gb18030_ext((uint8_t*)data)))
	{
		c->zi=1;
		c->ext=!gb_is_normal((uint8_t*)data);
		if(revert) c->ext=!c->ext;
		if(dic==Y_MB_DIC_MAIN)
			mb_add_zi(mb,code,clen,data,dlen,0);
	}
	else
	{
		if(data[0]=='$')
			c->ext=1;
	}
	mb_slist_insert_after(NULL,pos,c);
	index=mb->last_index;
	index->ci_count++;
	if(c->zi)
	{
		index->zi_count++;
		if(c->ext)
			index->ext_count++;
	}

	return c;
}

static bool mb_ci_test_pos(void *p)
{
	struct y_mb_ci *c=p;
	return c->del==0;
}

static inline int mb_code_is_valid(struct y_mb *mb,const char *code,int clen)
{
	int c;
	int i;
	for(i=0;i<clen;i++)
	{
		c=code[i];
		if((c&0x80) || !mb->map[c])
			return 0;
	}
	return 1;
}

static inline struct y_mb_ci *mb_add_one_ci(
				struct y_mb *mb,
				struct y_mb_index *index,
				struct y_mb_item *it,
				const char *code,int clen,
				const char *data,int dlen,
				int pos,int dic,int revert)
{
	int a_head=0;
	struct y_mb_ci *a_node=NULL;
	struct y_mb_ci *p=it->phrase;
	struct y_mb_ci *c=NULL;

	if(!p)
	{
		a_head=1;
	}
	else
	{
		if(pos==Y_MB_APPEND)
		{
			for(;p!=NULL;p=p->next)
			{
				if(p->len==dlen && mb_ci_equal(p,data,dlen))
				{
					if(p->del)
					{
						p->del=0;
						if(p->zi)
							mb_add_zi(mb,code,clen,data,dlen,0);
					}
					if(dic!=Y_MB_DIC_ASSIST)
					{
						p->dic=dic;
					}
					// 如果返回p，同一行中其他词会跟在返回的p后面
					// 所以返回NULL，让后面的词自动到最后去，速度会稍慢
					// 这是位置准确性和速度的一个平衡
					return NULL;
				}
				if(p->next==NULL)
				{
					a_node=p;
					break;
				}
			}
		}
		else if(pos==0)
		{
			if(p->len==dlen && mb_ci_equal(p,data,dlen))
			{
				if(p->del)
				{
					p->del=0;
					if(p->zi)
						mb_add_zi(mb,code,clen,data,dlen,0);
				}
				if(dic==Y_MB_DIC_PIN)
					p->dic=Y_MB_DIC_PIN;
				return p;
			}
			else
			{
				struct y_mb_ci *n=p->next;
				a_head=1;
				for(;n!=NULL;p=n,n=p->next)
				{
					if(n->len==dlen && mb_ci_equal(n,data,dlen))
					{
						p->next=n->next;
						c=n;c->dic=dic;
						break;
					}
				}
			}
		}
		else
		{
			struct y_mb_ci *n=p->next;
			int i;
			int del=0;
			if(p->len==dlen && mb_ci_equal(p,data,dlen))
			{
				if(p->del)
				{
					p->del=0;
					if(p->zi)
						mb_add_zi(mb,code,clen,data,dlen,0);
				}
				p->dic=dic;
				if(!n) return p;
				c=p;it->phrase=p=n;n=p->next;
			}
			for(i=1;n!=NULL;p=n,n=p->next)
			{
				if(i==pos)
				{
					a_node=p;
					if(del) break;
				}
				if(n->len==dlen && mb_ci_equal(n,data,dlen))
				{
					if(n->del)
						n->del=0;
					if(c) mb_ci_free(c);
					if(i==pos)
					{
						/* 词已经在正常位置，不需要用户词库调整 */
						if(dic==Y_MB_DIC_PIN)
							n->dic=Y_MB_DIC_PIN;
						return n;
					}
					p->next=n->next;
					c=n;c->dic=dic;
					if(a_node)
						break;
					del=1;
					n=p;
				}
				else
				{
					if(!n->del)
						i++;
				}
			}
			if(!a_node)
				a_node=p;
		}
	}
	if(a_head || a_node)
	{
		if(!c)
		{
			int ishz=revert || (dlen==2 && gb_is_gbk((uint8_t*)data)) ||
					(dlen==4 && gb_is_gb18030_ext((uint8_t*)data));
			c=mb_ci_new(data,dlen);
			c->dic=dic;
			c->del=0;
			c->zi=ishz;
			c->ext=0;
			c->simp=0;
			if(ishz)
			{
				c->ext=!gb_is_normal((uint8_t*)data);
				if(revert) c->ext=!c->ext;
				if(dic==Y_MB_DIC_MAIN)
				{
					mb_add_zi(mb,code,clen,data,dlen,0);
				}
			}
			else
			{
				if(data[0]=='$')
					c->ext=1;
			}
			index->ci_count++;
			if(c->zi)
			{
				index->zi_count++;
				if(c->ext)
					index->ext_count++;
			}
		}
		if(a_head)
		{
			c->next=it->phrase;
			it->phrase=c;
		}
		else
		{
			c->next=a_node->next;
			a_node->next=c;
		}
		return c;
	}
	return 0;
}

static struct y_mb_ci *mb_add_one(struct y_mb *mb,const char *code,int clen,const char *data,int dlen,int pos,int dic)
{
	struct y_mb_ci *ci;
	struct y_mb_item *it=NULL;
	struct y_mb_index *index=NULL;
	uint16_t index_val;
	int revert=0;

	if(code[0]=='^') /* is it used to construct ci */
	{
		if(dic==Y_MB_DIC_ASSIST)
			return NULL;
		if((dlen==2 && gb_is_gbk((uint8_t*)data)) ||
					(dlen==4 && gb_is_gb18030_ext((uint8_t*)data)))
		{
			int mv=1;
			if(dic==Y_MB_DIC_MAIN) mv|=0x02;
			code+=1;clen-=1;
			mb_add_zi(mb,code,clen,data,dlen,mv);
			return NULL;
		}
		else
		{
			return NULL;
		}
	}
	else if(code[0]=='{') /* move the pos of ci */
	{
		int temp;
		temp=strcspn(code,"}");
		if(clen>=3 && code[temp]=='}')
		{
			temp++;
			if(code[1]=='-')
			{
				pos=Y_MB_DELETE;
				code+=temp;
				clen-=temp;
			}
			else if(code[1]>='0' || code[1]<='9')
			{
				pos=atoi(code+1);
				code+=temp;
				clen-=temp;
			}
		}
	}
	
	if(!mb_code_is_valid(mb,code,clen))
	{
		return NULL;
	}

	index_val=mb_ci_index(mb,code,clen,0);
	
	if(L_UNLIKELY(data[0]=='~'))
	{
		if(dlen==3 && gb_is_gbk((uint8_t*)data+1))
			revert=1;
		else if(dlen==5 && gb_is_gb18030_ext((uint8_t*)data+1))
			revert=1;
		if(revert)
		{
			data++;
			dlen--;
		}
	}
	
	if(L_UNLIKELY(dlen<=0))
		return NULL;
	if(pos==Y_MB_DELETE)
	{
		struct y_mb_ci *p;
		int found=0;
		mb->last_link=NULL;
		mb->last_index=NULL;
		if(!dlen) return NULL;
		index=mb_get_index(mb,index_val);
		if(!index) return NULL;
		it=mb_get_one_code(mb,index,code,clen);
		if(!it) return NULL;
		for(p=it->phrase;p;p=p->next)
		{
			if(p->del) continue;
			if(!mb_ci_equal(p,data,dlen)) continue;
			if(p->zi)
			{
				index->zi_count--;
				if(p->ext)
					index->ext_count--;
				if(p->dic==Y_MB_DIC_MAIN)
					mb_add_zi(mb,code,clen,data,dlen,-1);
			}
			p->del=1;
			p->dic=dic;
			index->ci_count--;
			found=1;
			break;
		}
		if(found==0 && dic==Y_MB_DIC_USER)
			mb->dirty++;
		return NULL;
	}

	if(!index)
	{
		if(mb->last_index && mb->last_index->index==index_val)
			index=mb->last_index;
		else
		{
			index=mb_add_index(mb,index_val);
			//printf("%c%c\n",code[0],code[1]);
		}
	}
	if(mb->nsort)
	{
		it=mb_add_one_code_nsort(mb,index,code,clen,pos);
	}
	else
	{
		if(!it)
		{
			it=mb_add_one_code(mb,index,code,clen);
		}
	}
	if(L_UNLIKELY(!it))
	{
		//printf("add code %s fail\n",code);
		return NULL;
	}
	ci=mb_add_one_ci(mb,index,it,code,clen,data,dlen,pos,dic,revert);
	if(mb->trie)
	{
		int ret;
		char temp[64];
		ret=py_conv_to_sp(code,data,temp);
		if(ret==0)
		{
			trie_node_t *n=trie_tree_add(mb->trie,temp,strlen(temp));
			if(!n->data) n->data=ci;
		}
		/*else
		{
			memcpy(temp,code,clen);
			temp[clen]=0;
			printf("conv sp %s fail\n",temp);
		}*/
	}
	return ci;
}

static int mb_add_phrase_qp(struct y_mb *mb,const char *code,const char *phrase,int pos)
{
	struct y_mb_ci *c;
	char temp[64];
	int i;
	for(i=0;*code!=0;code++)
	{
		if(*code==mb->split)
			continue;
		temp[i++]=*code;
	}
	temp[i]=0;
	c=mb_add_one(mb,temp,strlen(temp),phrase,strlen(phrase),pos,Y_MB_DIC_USER);
	if(c)
	{
		mb->dirty++;
		//if(mb->dirty>=mb->dirty_max)
			y_mb_save_user(mb);
	}
	return c?0:-1;
}

static int mb_escape_data(const char *in,char *out)
{
	int i,j;
	
	for(i=0,j=0;in[i];i++)
	{
		if(in[i]==' ')
		{
			out[j++]='$';
			out[j++]='_';
		}
		else
		{
			out[j++]=in[i];
		}
	}
	out[j]=0;
	return j;
}

int y_mb_add_phrase(struct y_mb *mb,const char *code,const char *phrase,int pos,int dic)
{
	struct y_mb_ci *c;
	int clen=strlen(code);
	int dlen;
	char temp[Y_MB_DATA_SIZE+1];
	if(clen<=0 || clen>Y_MB_KEY_SIZE)
		return -1;
	dlen=mb_escape_data(phrase,temp);
	if(mb->split=='\'')
		return mb_add_phrase_qp(mb,code,temp,pos);
	if(dlen<=0 || dlen>=Y_MB_DATA_SIZE)
		return -1;
	if(mb->nsort) pos=Y_MB_APPEND;
	c=mb_add_one(mb,code,clen,temp,dlen,pos,dic);
	if(c && dic==Y_MB_DIC_USER)
	{
		mb->dirty++;
		y_mb_save_user(mb);
	}
	return c?0:-1;
}

static inline int mb_data0_is_english(const uint8_t *in,int len)
{
	return (in[0]&0x80)==0;
}

struct y_mb_ci *y_mb_code_exist(struct y_mb *mb,const char *code,int len,int count)
{
	uint16_t index_val;
	struct y_mb_index *index;
	struct y_mb_item *p;
	struct y_mb_ci *c;
	uintptr_t key=0;

	index_val=mb_ci_index(mb,code,len,&key);
			
	count<<=1;
	index=mb_get_index(mb,index_val);
	if(index==NULL)
		return 0;
	p=mb_get_item(index,key);
	if(p==NULL)
		return 0;
	for(c=p->phrase;c;c=c->next)
	{
		if(mb_data0_is_english(c->data,c->len))
			continue;
		if(count>0 && c->len!=count)
			continue;
		if(!c->del) return c;
	}

	return 0;
}

static struct y_mb_ci * mb_find_one(struct y_mb *mb,
	const char *code,const char *phrase,
	struct y_mb_index **pindex,struct y_mb_item **pitem)
{
	uint16_t index_val;
	struct y_mb_index *index;
	int ret;
	int len;
	struct y_mb_item *p;
	uintptr_t key;

	len=strlen(code);
	index_val=mb_ci_index(mb,code,len,&key);
	
	for(index=mb->index;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,len);
		if(mb->nsort && ret<0) continue;
		if(ret<0) break;
		if(ret!=0) continue;
		if(index->ci_count==0)
			continue;
		for(p=index->item;p;p=p->next)
		{
			struct y_mb_ci *c;
			ret=mb_key_cmp_direct(key,p->code,Y_MB_KEY_SIZE);
			if(ret>0) continue;
			if(ret<0) break;
			for(c=p->phrase;c;c=c->next)
			{
				if(c->del) continue;
				if(strcmp(phrase,y_mb_ci_string(c)))
				{
					continue;
				}
				*pindex=index;
				*pitem=p;
				return c;
			}
			return 0;
		}
	}
	return 0;
}

static int mb_move_phrase_qp(struct y_mb *mb,const char *code,const char *phrase,int dir)
{
	char temp[64];
	int i;
	for(i=0;*code!=0;code++)
	{
		if(*code==mb->split)
			continue;
		temp[i++]=*code;
	}
	temp[i]=0;
	return y_mb_move_phrase(mb,temp,phrase,dir);
}

static int mb_pin_phrase(struct y_mb *mb,const char *code);
int y_mb_move_phrase(struct y_mb *mb,const char *code,const char *phrase,int dir)
{
	struct y_mb_index *index;
	struct y_mb_item *item;
	struct y_mb_ci *c;
	int pos=0;
	
	if(mb->split=='\'' && strchr(code,mb->split))
		return mb_move_phrase_qp(mb,code,phrase,dir);
	
	c=mb_find_one(mb,code,phrase,&index,&item);
	if(!c) return -1;
	if(dir!=1 && dir!=-1 && dir!=0)
		return -1;
	if(dir==1 && c->next==NULL)
		return 0;
	if(dir==-1 && item->phrase==c)
		return 0;
	if(dir==0 && item->phrase==c)
		return 0;
	if(dir!=0)
	{
		pos=mb_slist_pos_custom(item->phrase,c,mb_ci_test_pos);
		if(pos<0 || pos>=Y_MB_APPEND)
			return -1;
		pos+=dir;
		if(pos<0 || pos>=Y_MB_APPEND)
			return -1;
	}
	else
	{
		dir=0;
	}
	c->dic=Y_MB_DIC_USER;
	item->phrase=mb_slist_remove(item->phrase,c);
	item->phrase=mb_slist_insert_custom(item->phrase,c,pos,mb_ci_test_pos);
	mb->dirty++;
	
	mb_pin_phrase(mb,code);

	if(mb->dirty>=mb->dirty_max)
		y_mb_save_user(mb);
	return 0;
}

static int mb_auto_move_qp(struct y_mb *mb,const char *code,const char *phrase,int auto_move)
{
	char temp[64];
	int i;
	for(i=0;*code!=0;code++)
	{
		if(*code==mb->split)
			continue;
		temp[i++]=*code;
	}
	temp[i]=0;
	return y_mb_auto_move(mb,temp,phrase,auto_move);
}

int y_mb_auto_move(struct y_mb *mb,const char *code,const char *phrase,int auto_move)
{
	struct y_mb_index *index;
	struct y_mb_item *item;
	struct y_mb_ci *c;
	int pos=0;
	
	if(!auto_move)
		return 0;
	if(mb->nomove[0])
	{
		int l=strlen(mb->nomove);
		if(!strncmp(code,mb->nomove,l))
			return 0;
	}
	
	if(mb->split=='\'' && strchr(code,mb->split))
		return mb_auto_move_qp(mb,code,phrase,auto_move);
	
	c=mb_find_one(mb,code,phrase,&index,&item);
	if(!c) return -1;
	if(item->phrase==c)
		return 0;
	else if(auto_move==2)
		pos=mb_slist_pos_custom(item->phrase,c,mb_ci_test_pos)/2;
	if(c->dic!=Y_MB_DIC_TEMP)
		c->dic=Y_MB_DIC_USER;
	item->phrase=mb_slist_remove(item->phrase,c);
	item->phrase=mb_slist_insert_custom(item->phrase,c,pos,mb_ci_test_pos);
	mb->dirty++;
	
	mb_pin_phrase(mb,code);

	if(mb->dirty>=mb->dirty_max)
		y_mb_save_user(mb);
	return 0;
}

int y_mb_del_phrase(struct y_mb *mb,const char * code,const char *phrase)
{
	struct y_mb_ci *c;
	struct y_mb_index *index;
	struct y_mb_item *item;
	
	c=mb_find_one(mb,code,phrase,&index,&item);
	if(!c) return -1;
	if(c->zi)
	{
		index->zi_count--;
		if(c->ext)
			index->ext_count--;
		if(c->dic==Y_MB_DIC_MAIN)
			mb_add_zi(mb,code,strlen(code),phrase,strlen(phrase),-1);
	}
	/*
	// 用户移动过的主词库词可能删不掉，标记为del即可
	if(c->dic==Y_MB_DIC_USER)
	{
		item->phrase=mb_slist_remove(item->phrase,c);
		mb_ci_free(c);
		MB_SLIST_ASSERT(item->phrase);
	}
	else*/
	{
		c->del=1;
	}
	index->ci_count--;
	mb->dirty++;
	
	mb_pin_phrase(mb,code);

	//if(mb->dirty>=mb->dirty_max)
		y_mb_save_user(mb);
	return 0;
}

FILE *y_mb_open_file(const char *fn,const char *mode)
{
	char temp[256];

	l_gb_to_utf8(fn,temp,sizeof(temp));fn=temp;

	if(EIM.OpenFile)
		return EIM.OpenFile(fn,mode);
	else
		return fopen(fn,mode);
}

int mb_load_english(struct y_mb *mb,FILE *fp)
{
	char line[Y_MB_KEY_SIZE+1];
	int len;
	
	for(;(len=l_get_line(line,sizeof(line),fp))>=0;)
	{
		if(len==0 || line[0]=='#')
			continue;
		if(isupper(line[0])) //only detect the first
		{
			char key[Y_MB_KEY_SIZE+1];
			int i;
			for(i=0;i<len;i++)
				key[i]=tolower(line[i]);
			key[i]=0;
			mb_add_one(mb,key,len,line,len,Y_MB_APPEND,0);
		}
		else
		{
			mb_add_one(mb,line,len,line,len,Y_MB_APPEND,0);
		}
	}
	return 0;
}

static inline int mb_zi_has_simple(struct y_mb_zi *z,int clen)
{
	struct y_mb_code *p;
	if(z) for(p=z->code;p;p=p->next)
	{
		if(p->virt)
			continue;
		if(p->len<clen)
			return 1;
	}
	return 0;
}

static int mb_has_simple(struct y_mb *mb,int clen,const char *data,int dlen)
{
	struct y_mb_zi *z=mb_find_zi(mb,data);
	return mb_zi_has_simple(z,clen);
}

static void mb_mark_simple(struct y_mb *mb)
{
	struct y_mb_index *index;
	struct y_mb_zi *z;
	
	/* 只考虑简码处理和全码不重复显示的情况 */
	if(!mb->simple && !mb->compat)
		return;
	/* 只考虑单字的情况 */
	if(!mb->zi)
		return;

	if(mb->simple) for(index=mb->index;index;index=index->next)
	{
		struct y_mb_item *it;
		for(it=index->item;it;it=it->next)
		{
			char *cur=0;
			int len=0;		/* 不用初始化，但是gcc有一个错误的未初始化警告 */
			struct y_mb_ci *c,*first=NULL;
			for(c=it->phrase;c;c=c->next)
			{
				if(c->del)				/* 已被删除 */
					continue;
				if(!first)				/* 得到第一个词组 */
					first=c;
				if(!c->zi)				/* 跳过不是字的 */
					continue;
				if(!cur)
				{
					/* 获得当前系列词组的编码 */
					cur=mb_key_conv_r(mb,index->index,it->code);
					len=strlen(cur);
					if(len==1) break; /* 不考虑一简 */
				}
				/* 查找字对应的信息 */
				z=mb_find_zi(mb,(const char*)&c->data);
				if(!z)		/* 找不到相关信息 */
					continue;
				if(mb->simple==2 &&	c==it->phrase && !c->next)
				{
					/* 无重码，对“重码时隐藏简码”功能来说不需要处理 */
					continue;
				}
				else if(mb->simple==1 || mb->simple==2)
				{
					/* 对“重码时隐藏简码”和“出简不出全“功能，标记简码 */
					if(mb_zi_has_simple(z,len))
					{
						c->simp=1;
						index->ci_count--;
						index->zi_count--;
						if(c->ext) index->ext_count--;
						continue;
					}
				}
				else if(mb->simple==3)
				{
					if(c==first && c->next && mb_zi_has_simple(z,len))
					{
						/* 只有多个词组，且自己是第一个词组的时候才考虑简码后置 */
						/* 由于链表重新排序，导出的码表不会再保持原样 */
						struct y_mb_ci *h;
						for(h=c->next;h;c=h,h=h->next)
						{
							if(mb_has_simple(mb,len,(char*)&h->data,h->len))
								continue;
							c->next=NULL;
							for(c=h;c->next;c=c->next);
							c->next=it->phrase;
							it->phrase=h;
							break;
						}
					}
					break;
				}
			}
		}
	}
	if(mb->compat)  for(index=mb->index;index;index=index->next)
	{
		struct y_mb_item *it;
		for(it=index->item;it;it=it->next)
		{
			char *cur=0;
			int len=0;
			struct y_mb_ci *c;
			for(c=it->phrase;c;c=c->next)
			{
				struct y_mb_code *p;
				if(c->del)				/* 已被删除 */
					continue;
				if(!c->zi)				/* 跳过不是字的 */
					continue;
				if(!cur)
				{
					/* 获得当前系列词组的编码 */
					cur=mb_key_conv_r(mb,index->index,it->code);
					len=strlen(cur);
					if(len==1) break; /* 不考虑一简 */
				}
				/* 查找字对应的信息 */
				z=mb_find_zi(mb,(const char*)&c->data);
				if(!z)		/* 找不到相关信息 */
					continue;
				for(p=z->code;p;p=p->next)
				{
					int i;
					if(p->virt) continue;
					if(p->len>=len) continue;
					for(i=0;i<p->len;i++)
					{
						/* 在发现编码不匹配时跳出循环 */
						if(cur[i]!=y_mb_code_n_key(mb,p,i))
							break;
					}
					if(i==p->len)
					{
						/* 有更长的编码，表明这是一个简码 */
						c->simp=1;
						break;
					}
				}
			}
		}
	}
}

static void mb_half_index(struct y_mb *mb)
{
	struct y_mb_index *p;
	int len;
	if((mb->flag&MB_FLAG_ASSIST))
		return;
	len=mb_slist_count(mb->index);
	if(len<7) return;
	mb->half=mb_slist_nth(mb->index,len>>1);
	if(mb->nsort)
		return;
	for(p=mb->index;p!=NULL;p=p->next)
	{
		len=mb_slist_count(p->item);
		if(len<7) continue;
		p->half=mb_slist_nth(p->item,len>>1);
		
		struct y_mb_item *item;
		for(item=p->item;item!=p->half;item=item->next)
		{
			int ret=mb_key_cmp_direct(item->code,p->half->code,Y_MB_KEY_SIZE);
			assert(ret<0);
		}		
	}		
}

int mb_load_data(struct y_mb *mb,FILE *fp,int dic)
{
	char line[4096];
	int len,clen,dlen;
	char *data;
	struct y_mb_ci *c;
	int in_data=(dic==Y_MB_DIC_MAIN || dic==Y_MB_DIC_USER || dic==Y_MB_DIC_PIN);

	while(!mb->cancel)
	{

			if(mb->encode==0)
			len=l_get_line(line,sizeof(line),fp);
		else
			len=get_line_iconv(line,sizeof(line),fp);

		if(L_UNLIKELY(len<0))
			break;
		else if(L_UNLIKELY((len==0 || line[0]=='#') && !mb->jing_used))
			continue;

		if(L_UNLIKELY(line[0] & 0x80))
		{
			/* no code phrase */			
			int ret;
			ret=gb_strbrk((uint8_t*)line);
			if(ret>Y_MB_DATA_SIZE)
				continue;
			if(ret!=len)
			{
				mb_add_one(mb,line+ret,len-ret,line,ret,Y_MB_APPEND,dic);
			}
			else
			{
				char code[3][Y_MB_KEY_SIZE+1];
				code[0][0]=code[1][0]=code[2][0]=0;
				ret=y_mb_code_by_rule(mb,line,len,code[0],code[1],code[2],NULL);
				if(ret==0)
				{
					int i;
					for(i=0;i<3;i++)
					{
						if(!code[i][0])
							break;
						mb_add_one(mb,code[i],strlen(code[i]),line,len,Y_MB_APPEND,dic);
						//printf("%s %s\n",code[i],line);
					}					
				}
			}
			continue;
		}
		if(L_UNLIKELY(!in_data && line[0]=='[' && !strcasecmp(line,"[DATA]")))
		{
			in_data=1;
			continue;
		}
		for(clen=0;clen<=Y_MB_KEY_SIZE && line[clen] && line[clen]!=' ' && (in_data || line[clen]!='=');clen++);
		if(line[clen]!=' ') continue;
		if(clen==0) continue;
		in_data=1;
		data=line+clen;
		c=NULL;
		do{
			while(*data==' ') data++;// skip the space char
			for(dlen=0;dlen<Y_MB_DATA_SIZE && data[dlen] && data[dlen]!=' ';dlen++);
			if(dlen<=0) break;
			if(!c || (mb->flag&MB_FLAG_SLOW))
			{
				c=mb_add_one(mb,line,clen,data,dlen,Y_MB_APPEND,dic);
				if(L_UNLIKELY(!c))
					break;
			}
			else
			{
				/* used to fast the add */
				c=mb_app_one(mb,c,line,clen,data,dlen,dic);
			}
			data+=dlen;
		}while(data[0]==' ');
	}
	return 0;
}

static int mb_load_user(struct y_mb *mb,const char *fn)
{
	FILE *fp;
	fp=y_mb_open_file(fn,"rb");
	if(!fp) return 0;
	mb_load_data(mb,fp,Y_MB_DIC_USER);
	fclose(fp);
	return 0;
}

int mb_load_assist_code(struct y_mb *mb,FILE *fp,int pos)
{
	char line[4096];
	int len,clen,dlen;
	char *data;
	int position=0;
	while(1)
	{
		len=l_get_line(line,sizeof(line),fp);
		if(len<0) break;
		position++;
		if(len==0 || line[0]=='#') continue;
		for(clen=0;clen<=Y_MB_KEY_SIZE && line[clen] && line[clen]!=' ' && line[clen]!='=';clen++);
		if(line[clen]!=' ') continue;
		data=line+clen;
		if(clen<=pos) continue;
		do{
			data++;// skip the space char
			for(dlen=0;dlen<=Y_MB_DATA_SIZE && data[dlen] && data[dlen]!=' ';dlen++);
			if(dlen<=0) break;
			if(clen>=pos && ((dlen==2 && gb_is_gbk((uint8_t*)data)) || 
					(dlen==4 && gb_is_gb18030_ext((uint8_t*)data))))
			{
				mb_add_zi(mb,line+pos,clen-pos,data,dlen,0);
			}
			data+=dlen;
		}while(data[0]==' ');
	}
	return 0;
}

int mb_load_scel(struct y_mb *mb,FILE *fp)
{
	char temp[1024];
	ssize_t ret;
	int i;
	int res=-1;
	char py_table[420][8];
	int py_count=0;
	if(!fp) return -1;
	ret=fread(temp,1,12,fp);
	if(ret!=12 && memcmp(temp,"\x40\x15\x00\x00\x44\x43\x53\x01\x01\x00\x00\x00",12))
	{
		return -1;
	}
	if(mb->pinyin && mb->split=='\'')
	{
		if(0!=fseek(fp,0x1540,SEEK_SET))
			goto out;
		if(4!=fread(temp,1,4,fp))
			goto out;
		if(memcmp(temp,"\x9d\x01\x00\x00",4))
			goto out;
		for(py_count=0;py_count<420 && ftell(fp)<0x2628-6;py_count++)
		{
			uint16_t index;
			uint16_t len;
			uint16_t data[8];
			if(1!=fread(&index,2,1,fp)) goto out;
			if(index!=py_count) goto out;
			if(1!=fread(&len,2,1,fp)) goto out;
			if(len>=14 || (len&0x01)!=0) goto out;
			if(len!=fread(data,1,len,fp)) goto out;
			data[len/2]=0;
			l_utf16_to_utf8(data,py_table[py_count],8);
			//printf("%d %s\n",index,py_table[py_count]);
		}
	}
	if(0!=fseek(fp,0x2628,SEEK_SET))
		goto out;
	while(1)
	{
		uint16_t same;
		uint16_t py_len,dlen;
		uint16_t data[64];
		if(1!=fread(&same,2,1,fp)) break;
		if(same<=0) goto out;
		if(1!=fread(&py_len,2,1,fp)) goto out;
		if(py_len<=0 || py_len>=64 || (py_len&0x01)!=0)
			goto out;
		py_len/=2;
		if(py_len!=fread(data,2,py_len,fp)) goto out;
		if(py_count>0)
		{
			temp[0]=0;
			for(i=0;i<py_len;i++)
			{
				if(data[i]>=py_count)
				{
					goto out;
				}
				strcat((char*)temp,py_table[data[i]]);
			}
		}
		for(i=0;i<same;i++)
		{
			if(1!=fread(&dlen,2,1,fp)) goto out;
			if(dlen<2 || dlen>=120 || (dlen&0x01)!=0)
				goto out;
			if(dlen!=fread(data,1,dlen,fp)) goto out;
			data[dlen/2]=0;
			l_utf16_to_gb(data,temp+512,512);
			if(py_count==0)
			{
				ret=y_mb_code_by_rule(mb,temp+512,strlen(temp+512),temp,NULL);
				if(ret!=0) temp[0]=0;
			}
			mb_add_one(mb,temp,strlen(temp),temp+512,strlen(temp+512),Y_MB_APPEND,Y_MB_DIC_SUB);
			if(1!=fread(&dlen,2,1,fp)) goto out;
			if(dlen<=0 && dlen>=256) goto out;
			//fseek(fp,dlen,SEEK_CUR);
			if(1!=fread(temp,dlen,1,fp)) goto out;
		}
	}
	res=0;
out:
	return res;

}

int y_mb_load_quick(struct y_mb *mb,const char *quick)
{
	if(!quick || !quick[0] || quick[1]!=' ')
		return -1;
	mb->quick_mb=y_mb_load(quick+2,MB_FLAG_ASSIST|MB_FLAG_NOUSER,NULL);
	if(!mb->quick_mb)
		return -1;
	mb->quick_lead=quick[0];
	return 0;
}

L_HASH_STRING(pin,struct y_mb_pin_item,data);
static void pin_free(struct y_mb_pin_item *item)
{
	if(!item)
		return;
	l_slist_free(item->list,l_free);
	l_free(item);
}
int y_mb_load_pin(struct y_mb *mb,const char *pin)
{
	FILE *fp;
	struct y_mb_index *index;
	
	if(!pin) return -1;
	
	fp=y_mb_open_file(pin,"rb");
	if(!fp) return -1;
	//assert(mb->pin==NULL);
	if(mb->pin)
	{
		l_hash_table_free(mb->pin,(LFreeFunc)pin_free);
		mb->pin=NULL;
	}
	mb->pin=l_hash_table_new(1001,(LHashFunc)pin_hash,(LCmpFunc)pin_cmp);
	mb_load_data(mb,fp,Y_MB_DIC_PIN);
	fclose(fp);	
	
	for(index=mb->index;index;index=index->next)
	{
		struct y_mb_item *it=index->item;
		while(it)
		{
			char *code;
			int clen;
			struct y_mb_ci *cp;
			int pos=0;
			struct y_mb_pin_item *item=NULL;
			
			code=mb_key_conv_r(mb,index->index,it->code);
			clen=strlen(code);
			
			if(clen>8)
			{
				it=it->next;
				continue;
			}

			cp=it->phrase;
			while(cp)
			{
				if(cp->dic==Y_MB_DIC_PIN && pos<128)
				{
					struct y_mb_pin_ci *pc;
	
					if(!item)
					{
						char data[sizeof(struct y_mb_pin_item)+32];
						struct y_mb_pin_item *key=(struct y_mb_pin_item *)data;;
						key->len=(uint8_t)clen;
						strcpy(key->data,code);
						item=l_hash_table_find(mb->pin,key);
						
						if(!item)
						{			
							item=l_alloc(sizeof(struct y_mb_pin_item)+clen+1);
							strcpy(item->data,code);
							item->len=(uint8_t)clen;
							item->next=NULL;
							item->list=NULL;
							l_hash_table_insert(mb->pin,item);
						}
					}
			
					pc=l_alloc(sizeof(struct y_mb_pin_ci)+cp->len);
					pc->next=NULL;
					pc->len=cp->len;
					pc->pos=(int8_t)pos;
					memcpy(pc->data,cp->data,cp->len);
					item->list=l_slist_prepend(item->list,pc);
					//printf("{%d}%s %s\n",pos,code,y_mb_ci_string(cp));
				}
				if(!cp->del)
					pos++;
				cp=cp->next;
			}
			it=it->next;
		}
	}
	return 0;
}

static int mb_pin_phrase(struct y_mb *mb,const char *code)
{
	char data[sizeof(struct y_mb_pin_item)+32];
	struct y_mb_pin_item *key,*item;
	struct y_mb_pin_ci *c;
	if(!mb->pin)
		return 0;
	key=(struct y_mb_pin_item *)data;
	key->len=(uint8_t)strlen(code);
	if(key->len>8)
		return 0;
	strcpy(key->data,code);
	item=l_hash_table_find(mb->pin,key);
	if(!item)
		return 0;
	for(c=item->list;c!=NULL;c=c->next)
	{
		mb_add_one(mb,code,key->len,c->data,c->len,c->pos,Y_MB_DIC_PIN);
	}
	return 0;
}

static int mb_load_sub_dict(struct y_mb *mb,const char *dict)
{
	FILE *fp=NULL;
	if(strchr(dict,'.')!=NULL)
		fp=y_mb_open_file(dict,"rb");
	if(fp)
	{
		if(l_str_has_suffix(dict,".scel"))
			mb_load_scel(mb,fp);
		else
			mb_load_data(mb,fp,Y_MB_DIC_SUB);
		fclose(fp);
	}
	else
	{
		char base[128];
		LDir *d;
		const char *f;
		int len;
		if(EIM.GetPath==NULL)
			return -1;
		len=sprintf(base,"%s/%s",EIM.GetPath("HOME"),dict);
		if(!l_file_is_dir(base))
		{
			len=sprintf(base,"%s/%s",EIM.GetPath("DATA"),dict);
			if(!l_file_is_dir(base))
			{
				return -1;
			}
		}
		d=l_dir_open(base);
		if(!d)
		{
			//printf("l_dir_open %s fail\n",base);
			return -1;
		}
		while((f=l_dir_read_name(d))!=NULL)
		{
			if(f[0]=='.') continue;
			snprintf(base+len,sizeof(base)-len,"/%s",f);
			fp=l_file_open(base,"rb",NULL);
			if(fp)
			{
				if(l_str_has_suffix(f,".scel"))
					mb_load_scel(mb,fp);
				else
					mb_load_data(mb,fp,Y_MB_DIC_SUB);
				fclose(fp);
			}
		}
		l_dir_close(d);
		
	}
	return 0;
}

struct y_mb *y_mb_new(void)
{
	struct y_mb *mb;
	mb=calloc(1,sizeof(*mb));
	return mb;
}

int y_mb_load_to(struct y_mb *mb,const char *fn,int flag,struct y_mb_arg *arg)
{
	FILE *fp;
	char line[4096];
	int len,lines;

	fp=y_mb_open_file(fn,"rb");
	if(!fp)
	{
		if(!(flag &MB_FLAG_ASSIST))
			printf("yong: open mb %s fail\n",fn);
		return -1;
	}
	
	mb->flag=flag;
	mb->main=l_strdup(fn);
	mb->dirty_max=2;

	/* set some default config here */
	mb->hint=1;
	mb->len=Y_MB_KEY_SIZE;
	strcpy(mb->key+1,"abcdefghijklmnopqrstuvwxyz");
	y_mb_key_map_init(mb->key,0,mb->map);
	for(lines=0;(len=l_get_line(line,sizeof(line),fp))>=0;lines++)
	{
		if(line[0]=='#')
			continue;
		if(len<=4 || len>200)
		{
			if(len!=0)
				printf("yong: bad line %s:%d\n",fn,lines);
			continue;
		}
		if(lines==0 && !strncmp(line,"\xef\xbb\xbf",3))
		{
			memmove(line,line+3,strlen(line+3)+1);
			mb->encode=1;
		}
		if(!strcmp(line,"encode=UTF-8"))
		{
			mb->encode=1;
		}

		else if(!strncmp(line,"name=",5))
		{
			if(len>=20)
			{
				printf("yong: bad name\n");
			}
			else
			{
				if(!mb->encode)
					strcpy(mb->name,line+5);
			}
		}
		else if(!strncmp(line,"key=",4))
		{
			int i;
			for(i=4;line[i]!=0;i++)
			{
				if(line[i]>='A' && line[i]<='Z')
				{
					mb->capital=1;
					break;
				}
			}
			mb->jing_used=strchr(line+4,'#')?1:0;
			strncpy(mb->key+1,line+4,60);
			mb->key[61]=0;
			memset(mb->map,0,sizeof(mb->map));
			y_mb_key_map_init(mb->key,0,mb->map);
		}
		else if(!strncmp(line,"key0=",5))
		{
			snprintf(mb->key0,sizeof(mb->key0),"%s",line+5);
		}
		else if(!strncmp(line,"len=",4))
		{
			mb->len=atoi(line+4);
		}
		else if(!strncmp(line,"push=",5))
		{
			char stop[8]={0};
			int i,c;
			char **list;
			list=l_strsplit(line+5,' ');
			if(list[0])
			{
				strcpy(mb->push,list[0]);
				if(list[1]) strcpy(stop,list[1]);
			}
			l_strfreev(list);
			for(i=0;i<8 && (c=stop[i]);i++)
			{
				if(c<'1' || c>'7')
				{
					printf("yong: invalid stop setting\n");
					break;
				}
				mb->stop|=1<<(c-'0');
			}
			
		}
		else if(!strncmp(line,"pull=",5))
		{
			strcpy(mb->pull,line+5);
		}
		else if(!strncmp(line,"match=",6))
		{
			if(line[6]=='1')
				mb->match=1;
		}
		else if(!strncmp(line,"wildcard=",9))
		{
			mb->wildcard_orig=line[9];
			mb->wildcard=((arg && arg->wildcard)?arg->wildcard:line[9]);
			y_mb_key_map_init(0,mb->wildcard,mb->map);
			mb->key[Y_MB_WILDCARD]=mb->wildcard;
		}
		else if(!strncmp(line,"dwf=",4))
		{
			mb->dwf=(line[4]=='1');
		}
		else if(!strcmp(line,"english=1"))
		{
			mb->english=1;
		}
		else if(!strncmp(line,"simple=",7))
		{
			mb->simple=atoi(line+7)&0x03;
		}
		else if(!strncmp(line,"compat=",7))
		{
			mb->compat=atoi(line+7)&0x03;
		}
		else if(!strncmp(line,"compact=",8))
		{
			mb->compat=atoi(line+8)&0x03;
		}
		else if(!strncmp(line,"yong=",5))
		{
			mb->yong=atoi(line+5);
		}
		else if(!strncmp(line,"pinyin=",7))
		{
			mb->pinyin=atoi(line+7);
			mb->dirty_max=20;
		}
		else if(!strcmp(line,"hint=0"))
		{
			mb->hint=0;
		}
		else if(!strncmp(line,"auto_clear=",11))
		{
			mb->auto_clear=atoi(line+11);
		}
		else if(!strncmp(line,"nomove=",7))
		{
			if(strlen(line+7)<4)
				strcpy(mb->nomove,line+7);
		}
		else if(!strncmp(line,"auto_move=",10))
		{
			mb->auto_move=atoi(line+10);
		}
		else if(!strcmp(line,"nsort=1"))
		{
			mb->nsort=1;
		}
		else if(!strncmp(line,"sloop=",6))
		{
			mb->sloop=atoi(line+6);
		}
		else if(!strncmp(line,"split=",6))
		{
			if(line[6]<='9' && line[6]>='1')
				mb->split=line[6]-'0';
			else
				mb->split=line[6];
		}
		else if(!strncmp(line,"assist=",7))
		{
			if(isgraph(line[7]) && line[8]==' ')
			{
				mb->lead=line[7];
				mb->ass_main=strdup(line+9);
			}
			else
			{
				char ass[256],*p;
				int pos=-1;
				strcpy(ass,line+7);
				p=strchr(ass,' ');
				if(p)
				{
					*p++=0;
					if(isdigit(*p))
						pos=atoi(p);
				}
				if(pos>=0 && pos<16)
				{
					struct y_mb_arg mb_arg;
					memset(&mb_arg,0,sizeof(struct y_mb_arg));
					mb_arg.apos=pos;
					mb->ass_main=strdup(line+7);
					if(!(flag&MB_FLAG_ASSIST))
						mb->ass=y_mb_load(ass,MB_FLAG_ASSIST_CODE,&mb_arg);
				}
			}
		}
		else if((!arg || !arg->dicts) && 
				!strncmp(line,"dicts=",6) && 
				mb->dicts[0]==NULL && 
				!(flag&MB_FLAG_ASSIST))
		{
			char *p=line+6;
			int i,j;
			for(i=0;i<10 && p[0];i++)
			{
				for(j=0;p[j]!=' ' && p[j]!=0;j++);
				mb->dicts[i]=malloc(j+1);
				memcpy(mb->dicts[i],p,j);
				mb->dicts[i][j]=0;
				if(!p[j]) break;
				p+=j+1;
			}
		}
		else if(!strncmp(line,"user=",5) && !(flag&MB_FLAG_ASSIST))
		{
			if(mb->user)
				free(mb->user);
			mb->user=strdup(line+5);
		}
		else if(!strncmp(line,"normal=",7) && !(flag&MB_FLAG_ASSIST))
		{
			if(mb->normal)
				free(mb->normal);
			mb->normal=strdup(line+7);
		}
		else if(!strncmp(line,"commit=",7))
		{
			int t1=0,t2=0,t3=0;
			char *p=line+7;
			t1=atoi(p);
			p=strchr(p+1,' ');
			if(p)
			{
				t2=atoi(++p);
				p=strchr(p,' ');
				if(p) t3=atoi(++p);
			}
			//printf("%d %d %d\n",t1,t2,t3);
			mb->commit_mode=(uint8_t)t1;
			mb->commit_len=(uint8_t)t2;
			mb->commit_which=(uint8_t)t3;
		}
		else if(!strncmp(line,"skip=",5))
		{
			snprintf(mb->skip,8,"%s",line+5);
		}
		else if(!strncmp(line,"bihua=",6))
		{
			if(strlen(line+6)==5)
				strcpy(mb->bihua,line+6);
		}
		else if(!strncmp(line,"code_",5) && !(flag&MB_FLAG_ASSIST))
		{
			mb_add_rule(mb,line+5);
		}
		else if(!strcasecmp(line,"[DATA]"))
		{
			break;
		}
		else if(!strchr(line,'='))
		{
			rewind(fp);
			break;
		}
		/* todo: add rule */
	}
	
	/* use arg->dicts before y_mb_init_pinyin, or this will be changed */
	if(!(flag&MB_FLAG_ASSIST) && arg && arg->dicts)
	{
		char *p=line;
		int i,j;
		l_utf8_to_gb(arg->dicts,line,sizeof(line));
		for(i=0;i<10 && p[0];i++)
		{
			for(j=0;p[j]!=' ' && p[j]!=0;j++);
			mb->dicts[i]=malloc(j+1);
			memcpy(mb->dicts[i],p,j);
			mb->dicts[i][j]=0;
			if(!p[j]) break;
			p+=j+1;
		}
	}
			
	if(mb->english)
	{
		mb_load_english(mb,fp);
	}
	else
	{
		if(!(flag&MB_FLAG_ASSIST))
		{
			FILE *fp;
			if(mb->normal)
				fp=y_mb_open_file(mb->normal,"rb");
			else
				fp=y_mb_open_file("normal.txt","rb");
			gb_load_normal(fp);
			if(fp) fclose(fp);
			
			if(mb->pinyin)
			{
				y_mb_init_pinyin(mb);
			}
		}
		if(!(flag&MB_FLAG_ASSIST) || (flag&MB_FLAG_ZI))
			mb->zi=mb_hash_new(7001);
		if(flag==(MB_FLAG_ASSIST|MB_FLAG_ZI|MB_FLAG_CODE))
			mb_load_assist_code(mb,fp,arg->apos);
		else
		{
			mb_load_data(mb,fp,Y_MB_DIC_MAIN);
		}
	}
	fclose(fp);
	mb->encode=0;
	if(!(flag&MB_FLAG_ASSIST))
	{
		int i;
		if(!mb->user)
			mb->user=strdup("user.txt");
		for(i=0;mb->dicts[i] && i<10;i++)
		{
			mb_load_sub_dict(mb,mb->dicts[i]);
		}
		if(!(flag&MB_FLAG_NOUSER))
			mb_load_user(mb,mb->user);
		if(mb->ass_main && mb->lead)
		{
			if(flag&MB_FLAG_ADICT)
			{
				fp=y_mb_open_file(mb->ass_main,"rb");
				if(fp)
				{
					mb_load_data(mb,fp,Y_MB_DIC_ASSIST);
					fclose(fp);
				}
			}
			else
			{
				int flag=MB_FLAG_ASSIST;
				/*if(mb->pinyin) */flag|=MB_FLAG_ZI;
				mb->ass=y_mb_load(mb->ass_main,flag,0);
			}
		}
	}
	if(mb->encrypt || need_enc)
		mb->encrypt=1;
	mb_mark_simple(mb);
	mb_half_index(mb);

	return 0;
}

struct y_mb *y_mb_load(const char *fn,int flag,struct y_mb_arg *arg)
{
	struct y_mb *mb;
	mb=y_mb_new();
	if(0!=y_mb_load_to(mb,fn,flag,arg))
	{
		y_mb_free(mb);
		return NULL;
	}
	return mb;
}

int y_mb_load_fuzzy(struct y_mb *mb,const char *fuzzy)
{
	FUZZY_TABLE *ft;
	if(mb->encode!=0 || mb->nsort)
		return -1;
	// 加载模糊编码表
	ft=fuzzy_table_load(fuzzy);
	if(!ft)
		return -1;
	mb->fuzzy=ft;
	return 0;
}

void y_mb_rename_user(char *fn)
{
	char dest[256];
	char orig[256];
	int fd;
	off_t len;

	if(!fn)
		return;
	if(fn[0]=='~' && fn[1]=='/')
		sprintf(orig,"%s/%s",getenv("HOME"),fn+2);
	else if(fn[0]=='/' || !EIM.GetPath)
		strcpy(orig,fn);
	else
		sprintf(orig,"%s/%s",EIM.GetPath("HOME"),fn);
	sprintf(dest,"%s.bak",orig);

	fd=open(orig,O_RDONLY);
	if(fd==-1)
		return;
	len=lseek(fd,0,SEEK_END);
	close(fd);
	
	if(len<=2)
		return;

	remove(dest);
	rename(orig,dest);
}

void y_mb_save_user(struct y_mb *mb)
{
	struct y_mb_index *index;
	FILE *fp;
	if(!mb->user || !mb->dirty)
		return;

	y_mb_rename_user(mb->user);

	fp=y_mb_open_file(mb->user,"w");
	if(!fp) return;

	for(index=mb->index;index;index=index->next)
	{
		struct y_mb_item *it=index->item;
		while(it)
		{
			char *code;
			struct y_mb_ci *cp;
			int pos=0;

			code=mb_key_conv_r(mb,index->index,it->code);
			cp=it->phrase;
			while(cp)
			{
				//todo: if cp->zi==1, something should done to keep ext info
				if(cp->del)
				{
					fprintf(fp,"{-}%s %s\n",code,y_mb_ci_string(cp));
				}
				else if(cp->dic==Y_MB_DIC_USER)
				{
					if(mb->nsort)
						fprintf(fp,"%s %s\n",code,y_mb_ci_string(cp));
					else
						fprintf(fp,"{%d}%s %s\n",pos,code,y_mb_ci_string(cp));
				}
				if(!cp->del)
					pos++;
				cp=cp->next;
			}
			it=it->next;
		}
	}
	fclose(fp);
	mb->dirty=0;
}

void y_mb_init(void)
{
#if L_WORD_SIZE==32
	mb_slice_init(
		6,
		50000,sizeof(struct y_mb_item),		1000,
		7000 ,sizeof(struct y_mb_zi),  		100,
		800  ,sizeof(struct y_mb_index),	30,
		10000, 8,                       	500,
		10000, 12,                      	500,
		8000, 16,                      		500
		);
#else
	mb_slice_init(
		5,
		50000,sizeof(struct y_mb_item),		1000,
		7000 ,sizeof(struct y_mb_zi),  		100,
		800  ,sizeof(struct y_mb_index),	30,
		500, 12,                      		100,
		10000, 16,                      	500
		);
#endif
}

void y_mb_cleanup(void)
{
	mb_slice_clean();
}

void y_mb_free(struct y_mb *mb)
{
	int i;
	
	if(!mb)
		return;
	if(mb->user && mb->dirty)
		y_mb_save_user(mb);
	for(i=0;i<10;i++)
		free(mb->dicts[i]);
	free(mb->user);
	free(mb->main);
	free(mb->ass_main);
	if(mb->zi)
		mb_hash_free(mb->zi,mb_zi_free);
	if(mb->rule)
	{
		struct y_mb_rule *r,*n;
		r=mb->rule;
		while(r)
		{
			n=r->next;
			free(r);
			r=n;
		}
	}
	if(mb->index)
	{
		mb_slist_foreach(mb->index,(LFunc)mb_index_free2,0);
		mb_slist_free(struct y_mb_index,mb->index);
	}
	trie_tree_free(mb->trie);
	y_mb_free(mb->ass);
	y_mb_free(mb->quick_mb);
	l_hash_table_free(mb->pin,(LFreeFunc)pin_free);
	
	if(mb->ctx.result_ci)
		l_ptr_array_free(mb->ctx.result_ci,NULL);
	fuzzy_table_free(mb->fuzzy);
		
	free(mb);
}

int y_mb_has_wildcard(struct y_mb *mb,const char *s)
{
	if(mb->ass && mb->ctx.input[0]==mb->lead && mb->ctx.input[1])
	{
		if(!mb->ctx.input[1])
			return 0;
		return y_mb_has_wildcard(mb->ass,s+1);
	}
	if(!mb->wildcard)
		return 0;
	return strchr(s+mb->dwf,mb->wildcard)?1:0;
}

int y_mb_is_key(struct y_mb *mb,int c)
{
	if(mb->ass && mb->lead && mb->ctx.input[0]==mb->lead)
		return y_mb_is_key(mb->ass,c);
	if(mb->quick_mb && mb->quick_lead && mb->ctx.input[0]==mb->quick_lead)
		return y_mb_is_key(mb->quick_mb,c);
	if((KEYM_MASK&c) || (c>=0x80) || c<=0 /* just for more safe */)
		return 0;
	return mb->map[c]?1:0;
}

int y_mb_is_keys(struct y_mb *mb,char *s)
{
	int i;
	if(s[0]==mb->lead && mb->ass)
		return y_mb_is_keys(mb->ass,s+1);
	for(i=0;s[i]!=0;i++)
	{
		if(!y_mb_is_key(mb,s[i]))
			return 0;
	}
	return 1;
}

int y_mb_is_full(struct y_mb *mb,int len)
{
	if(mb->ass && mb->ctx.input[0]==mb->lead)
	{
		if(!mb->ctx.input[1])
			return 0;
		return y_mb_is_full(mb->ass,len-1);
	}
	if(mb->len>len)
		return 0;
	else
		return len+1-mb->len;
}

int y_mb_is_stop(struct y_mb *mb,int c,int pos)
{
	if(mb->ass && mb->ctx.input[0]==mb->lead)
	{
		if(!mb->ctx.input[1] || pos==0)
			return 0;
		return y_mb_is_stop(mb->ass,c,pos-1);
	}
	if(KEYM_MASK&c) return 0;
	if(pos<1 || pos>7) return 0;
	if(mb->stop && !(mb->stop&(1<<pos)))
		return 0;
	return strchr(mb->push,c)?1:0;
}

int y_mb_is_pull(struct y_mb *mb,int c)
{
	if(!mb->pull[0])
		return 0;
	if(mb->pull[0]=='*')
		return 1;
	return strchr(mb->pull,c)?1:0;
}

int y_mb_before_assist(struct y_mb *mb)
{
	return (mb->ass && mb->ctx.input[0]==mb->lead && !mb->ctx.input[1]);
}

/* todo: not good enough, but can work with other workaround */
// dext 0: not filter hz 1: filter ext hz 2: only ext hz
int y_mb_has_next(struct y_mb *mb,int dext)
{
	struct y_mb_item *it,*p;
	int len=strlen(mb->ctx.input);

	if(mb->ass && mb->ctx.input[0]==mb->lead)
	{
		if(!mb->ctx.input[1])
			return 1;
		return y_mb_has_next(mb->ass,dext);
	}
	if(mb->quick_mb && mb->ctx.input[0]==mb->quick_lead)
	{
		if(!mb->ctx.input[1])
			return 1;
		return y_mb_has_next(mb->quick_mb,dext);
	}
	
	if(mb->nsort)
	{
		/**
		 * TODO:
		 * 现在只在候选项个数为1时，判断是否有后续编码
		 */
		if(mb->ctx.result_count==1)
		{
			char *code;
			struct y_mb_index *index=mb->ctx.result_index;
			it=mb->ctx.result_first;
			code=mb_key_conv_r(mb,index->index,it->code);
			if(!strcmp(code,mb->ctx.input))
				return 0;
		}
		return len<mb->len;
	}
	if(len==1)
	{
		/**
		 * TODO:
		 * 在码长为一时，没有考虑到出简不出全，单字模式，有被删除字词存在等情况 
		 * 考虑到实际使用中不太可能在一简处出问题，暂不处理
		 */
		struct y_mb_index *index;
		index=mb->ctx.result_index;
		if(index->index & 0xff)
			return 1;
		index=index->next;
		if(!index) return 0;
		if(((index->index>>8)&0xff)==mb->map[(int)mb->ctx.input[0]])
			return 1;
		return 0;
	}
	
	it=mb->ctx.result_first;
	assert(it!=NULL);

	p=it->next;
	while(p)
	{
		if(mb_key_is_part(p->code,it->code))
		{
			struct y_mb_ci *c=p->phrase;
			while(c)
			{
				/* 单字出简不出全时，忽略这些有简码的字 */
				if(c->zi && mb->simple && c->simp)
				{
					c=c->next;
					continue;
				}
				if(c->del)
				{
					c=c->next;
					continue;
				}
				if(!dext || (dext!=2 && !c->ext) || (dext && c->ext))
				{
					return 1;
				}
				c=c->next;
			}
		}
		p=p->next;
	}
	return 0;
}

int y_mb_get_simple(struct y_mb *mb,char *code,char *data,int p)
{
	uint16_t index_val;
	struct y_mb_index *index;
	struct y_mb_item *item;
	struct y_mb_ci *c;
	char *temp;
	int ret;
	index_val=mb_ci_index(mb,code,p,0);
	for(index=mb->index;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,2);
		if(ret<0) break;
		if(ret>0) continue;
		if(index->ci_count==0)
			continue;
		item=index->item;
		temp=mb_key_conv_r(mb,0,item->code);
		if(temp[0] && (temp[1] || !strchr(mb->push,temp[0])))
			break;
		for(c=item->phrase;c;c=c->next)
		{
			if(c->del)
				continue;
			strcpy(data,y_mb_ci_string(c));
			return 0;
		}
	}
	
	return -1;
}

static int y_mb_max_match_qp(struct y_mb *mb,char *s,int len,int dlen,
		int filter,int *good,int *less)
{
	trie_iter_t iter;
	trie_tree_t *trie;
	trie_node_t *n;
	char temp[128];
	py_item_t token[64+1];
	int count;
	int i;
	int tail;
	char *p;
	int match=1,exact=0,exact_l=0;

	for(i=0;i<len && s[i]!=mb->split;i++);
	if(i==len)
	{
		return -1;
	}
	
	tail=s[len];
	count=py_parse_string(s,token,0);
	token[count]=NULL;
	s[len]=tail;
	
	if(count<=0)
	{
		return -1;
	}
	py_build_sp_string(temp,token,count);

	/*if((p=strchr(temp,mb->split))!=NULL && p[1]!=0)
	{			
		//return -1;
	}*/
	p=strchr(temp,mb->split);
	if(p) *p=0;
	trie=mb->trie;
	n=trie_iter_path_first(&iter,trie,NULL,64);
	while(n!=NULL)
	{
		int cur=iter.depth;
		if(cur<len && n->self!=temp[cur])
		{
			trie_iter_path_skip(&iter);
			n=trie_iter_path_next(&iter);
			continue;
		}
		//char out[65];
		//trie_iter_get_path(&iter,out);
		//printf("%s\n",out);
		if(n->leaf)
		{
			struct y_mb_ci *c;
			c=trie_node_get_leaf(trie,n)->data;
			for(;c!=NULL;c=c->next)
			{
				//printf("%s\n",y_mb_ci_string(c));
				if(c->del) continue;
				if(dlen>0 && c->len!=dlen*2) continue;
				if(filter && c->zi && c->ext) continue;
				if(cur<len && cur>=exact)
				{
					exact_l=exact;
					exact=cur+1;
				}
				if(cur>=match)
					match=cur+1;
				break;
			}
			if(match>=len) break;
		}
		n=trie_iter_path_next(&iter);
	}
	if(good) *good=py_pos_of_qp(token,exact);
	if(less) *less=py_pos_of_qp(token,exact_l);
	/*printf("%d %d %d\n",
			py_pos_of_qp(token,match),
			py_pos_of_qp(token,exact),
			py_pos_of_qp(token,exact_l));*/
	match=py_pos_of_qp(token,match);
	if(match>=len) match=len;
	return match;
}

int y_mb_max_match_fuzzy(struct y_mb *mb,char *s,int len,int dlen,
		int filter,int *good,int *less)
{
	LArray *list;
	FUZZY_TABLE *ft=mb->fuzzy;
	int ret;
	assert(ft!=NULL);
	mb->fuzzy=NULL;
	list=fuzzy_key_list(ft,s,len,mb->split);
	if(list->len==1 || mb->nsort || y_mb_has_wildcard(mb,s))
	{
		ret=y_mb_max_match(mb,s,len,dlen,filter,good,less);
	}
	else
	{
		int max=-1;
		int good2=0,less2=0;
		int i;
		//printf("%d %s\n",list->len,(char*)l_ptr_array_nth(list,0));
		for(i=0;i<list->len;i++)
		{
			char *code=l_ptr_array_nth(list,i);
			int clen=strlen(code); 
			ret=y_mb_max_match(mb,code,clen,dlen,filter,good,less);
			if(ret>max)
			{
				good2=*good;
				if(less) less2=*less;
				max=ret;
			}
		}
		ret=max;
		*good=good2;
		if(less) *less=less2;
	}
	l_ptr_array_free(list,l_free);
	mb->fuzzy=ft;
	if(ret>len) ret=len;
	if(good && *good>len) *good=len;
	if(less && *less>len) *less=len;
	return ret;
}
//#include <time.h>
int y_mb_max_match(struct y_mb *mb,char *s,int len,int dlen,
		int filter,int *good,int *less)
{
	uint16_t index_val;
	struct y_mb_index *index;
	struct y_mb_item *item;
	int ret;
	int match=1,exact=0,exact_l=0;
	int left;
	int base;
	
	if(mb->fuzzy)
	{
		//clock_t start=clock();
		ret=y_mb_max_match_fuzzy(mb,s,len,dlen,filter,good,less);
		//printf("match %.3f\n",(clock()-start)*1.0/CLOCKS_PER_SEC);
		return ret;
	}
	
	if(mb->pinyin && mb->split=='\'' && mb->trie &&
			(ret=y_mb_max_match_qp(mb,s,len,dlen,filter,good,less))>0)
	{
		return ret;
	}
	index_val=mb_ci_index(mb,s,len,0);
	base=(len<=1 || mb->nsort)?1:2;
	s+=base;left=len-base;
	
	for(index=mb->index;index;index=index->next)
	{
		base=index->index&0xff?2:1;
		ret=mb_index_cmp_direct(index_val,index->index,base);
		if(ret<0)
			break;
		if(ret>0)
			continue;
		if(filter && index->ci_count-index->ext_count==0)
			break;
		for(item=index->item;item;item=item->next)
		{
			char *key;
			int i;
			struct y_mb_ci *c;
			key=mb_key_conv_r(mb,0,item->code);
			for(i=0;i<left && key[i];i++)
			{
				if(s[i]!=key[i]) break;
			}
			if((key[i]==0 && i+base>exact) || i+base>match)
			{
				for(c=item->phrase;c;c=c->next)
				{
					if(c->del) continue;
					/* FIXME: 这里现在只是简单的把汉字处理成两个编码，需要严格按照汉自定义来进行 */
					if(dlen>0 && c->len!=dlen*2) continue;
					if(filter && c->zi && c->ext) continue;
					if(key[i]==0 && i+base>exact)
					{
						exact_l=exact;
						exact=i+base;
					}
					if(i+base>match)
					{
						match=i+base;
					}
					break;
				}
			}
			/* if entire code match, no need to test next */
			if(match>=len) break;
			/* if the first test is enough */
			if(left<=0) break;
		}
	}
	if(good) *good=exact;
	if(less) *less=exact_l;
	return match;
}

/* detect if the simple key exist, in case of the simple key len >= clen*/
int mb_simple_exist(struct y_mb *mb,const char *s,int clen,struct y_mb_ci *c)
{
	uint32_t key;
	struct y_mb_zi *z,kz;

	if(c->len==2)
		key=*(uint16_t*)&c->data;
	else
		key=*(uint32_t*)&c->data;
	kz.data=key;
	z=mb_hash_find(mb->zi,&kz);
	if(z)
	{
		struct y_mb_code *p;
		for(p=z->code;p;p=p->next)
		{
			if(p->virt) continue;
			if(p->len>=clen)
			{
				int i;
				for(i=0;i<clen;i++)
				{
					if(s[i]!=y_mb_code_n_key(mb,p,i))
						break;
				}
				if(i==clen) return 1;
			}
		}
	}
	return 0;
}

#if 1
static int mb_simple_code_match(char *code,char *s,int len,uint8_t split)
{
	int i;
	int clen;
	clen=strlen(code);
	if(clen<=len)
		return 0;
	if(code[0]!=s[0])
		return 0;
	if(split && split>='2' && split<='7')
	{
		split-='0';
		if(clen<split*len)
			return 0;
		for(i=0;i<len;i++)
		{
			if(code[split*i]!=s[i])
				return 0;
		}
		return 1;
	}
	else
	{
		char *p;
		for(i=0;i<len;i++)
		{
			p=strchr(code,s[i]);
			if(!p) return 0;
			code=p+1;			
		}
		return 1;
	}
	return 0;
}

static int mb_simple_phrase_match(struct y_mb *mb,char *c,char *s,int len)
{
	struct y_mb_zi *z;
	struct y_mb_code *p;
	int i;
	for(i=1;i<len;i++)
	{
		if(!s[i]) break;
		if(!gb_is_gbk((uint8_t*)c+i*2))
		{
			return 0;
		}
		z=mb_find_zi(mb,c+2*i);
		if(!z)
		{
			return 0;
		}
		for(p=z->code;p;p=p->next)
		{
			if(y_mb_code_n_key(mb,p,0)==s[i])
				break;
		}
		if(!p)
		{
			return 0;
		}
	}
	return 1;
}
#endif

struct _s_item{
	struct y_mb_ci *c;
	int f;
	int m;
};
static int _s_item_cmpar(struct _s_item *it1,struct _s_item *it2)
{
	int m=it2->m-it1->m;
	if(m) return m;
	return it2->f-it1->f;
}
int y_mb_predict_simple(struct y_mb *mb,char *s,char *out,int (*freq)(const char *))
{
	int len=strlen(s);
	struct y_mb_index *index;
	struct y_mb_item *item;
	struct y_mb_ci *ci;
	uint16_t index_val;
	int ret;
	LArray *array;

	if(len<=1) return 0;
	
	array=l_array_new(51,sizeof(struct _s_item));	
	index_val=mb_ci_index(mb,s,1,0);
	for(index=mb->index;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,1);
		if(ret>0) continue;
		if(ret<0) break;
		for(item=index->item;item;item=item->next)
		{
			char *code=mb_key_conv_r(mb,index->index,item->code);
			ret=mb_simple_code_match(code,s,len,mb->split);
			if(!ret) continue;
			for(ci=item->phrase;ci;ci=ci->next)
			{
				struct _s_item item;
				char *c;
				if(ci->del || ci->len<2*len)
					continue;
				c=y_mb_ci_string(ci);
				ret=mb_simple_phrase_match(mb,c,s,len);
				if(!ret) continue;
				item.c=ci;
				item.f=(freq && ci->len<15)?freq(c):0;
				item.m=(ci->len==2*len);
				l_array_insert_sorted(array,&item,(LCmpFunc)_s_item_cmpar);
				if(!freq && array->len>50)
					array->len=50;
				if(freq && array->len>20)
					array->len=20;
			}
		}
	}
//out:
	for(ret=len=0;ret<array->len;ret++)
	{
		struct _s_item *it=l_array_nth(array,ret);
		struct y_mb_ci *ci=it->c;
		char *c;
		if(len+ci->len+1+1>MAX_CAND_LEN)
			break;
		c=y_mb_ci_string(ci);
		memcpy(out+len,c,ci->len);
		len+=ci->len;
		out[len++]=0;
	}
	out[len]=0;
	l_array_free(array,NULL);
	return ret;
}

static int mb_match_quanpin(struct y_mb *mb,struct y_mb_ci *c,int clen,const char *sep)
{
	if(sep==NULL)
	{
		if(c->zi || !mb->ctx.sp || c->len!=4)
			return 1;
		struct y_mb_zi *z=mb_find_zi(mb,(char*)&c->data+2);
		if(z)
		{
			struct y_mb_code *code=z->code;
			for(;code!=NULL;code=code->next)
			{
				if(code->len==clen)
					return 1;
			}
		}
		return 0;
	}
	if(c->zi)
		return 0;
	if(c->len==4)
	{
		struct y_mb_zi *z=mb_find_zi(mb,(char*)&c->data+2);
		if(z)
		{
			struct y_mb_code *code=z->code;
			for(;code!=NULL;code=code->next)
			{
				if(*sep==y_mb_code_n_key(mb,code,0))
					return 1;
			}
			return 0;
		}
	}
	return 1;
}

static LArray *add_fuzzy_phrase(LArray *head,struct y_mb *mb,struct y_mb_context *ctx)
{
	struct y_mb_index *index;
	struct y_mb_item *item,*p;
	char *s;
	uintptr_t key;
	uint16_t index_val;
	int len,left;
	int ret;
	int filter,filter_zi,filter_ext;
	int got=0;
	int i;
	int (*extern_match)(struct y_mb *,struct y_mb_ci *,int clen,const char*)=NULL;
	int last[4]={0};
	int count=0;
	const char *sep=NULL;
	
	index=ctx->result_index;
	item=ctx->result_first;
	
	s=ctx->input;
	len=strlen(s);
	index_val=mb_ci_index_wildcard(mb,s,len,0,&key);
	left=mb_key_len(key);
	filter=ctx->result_filter;
	filter_zi=ctx->result_filter_zi;
	filter_ext=ctx->result_filter_ext;
	if(mb->pinyin==1 && mb->split=='\'')
	{
		char *temp=alloca(len+1);
		const char*p;
		int i;
		sep=strchr(s,'\'');
		if(sep) sep++;
		for(p=s,i=0;p<s+len;p++)
		{
			if(*p=='\'') continue;
			temp[i++]=*p;
		}
		temp[i]=0;len=i;
		s=temp;
		extern_match=mb_match_quanpin;
	}
	
	for(;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,len);
		if(ret<0)
			break;
		if(ret!=0)
			continue;
		if(index->ci_count==0)
			continue;
		if(filter && index->ci_count-index->ext_count==0)
			continue;
		for(p=(index==ctx->result_index)?item:index->item;p;p=p->next)
		{
			int clen=mb_key_len(p->code)+(index->index&0xff?2:1);
			struct y_mb_ci *c;
			ret=mb_key_cmp_direct(key,p->code,left);
			if(ret>0) continue;
			if(ret<0) break;
			for(c=p->phrase;c;c=c->next)
			{
				if(c->del) continue;
				if(filter && c->zi && c->ext) continue;
				if(filter_zi && !c->zi) continue;
				if(c->zi && mb->simple && c->simp) continue;
				if(filter_ext && c->zi && !c->ext) continue;
				if(ctx->result_compat==0)
				{
					if(c->zi && mb->compat && c->simp && clen>len && mb_simple_exist(mb,s,len,c)) continue;
					if(!c->zi && mb->compat && clen>len+mb->compat-1) continue;
				}
				if(extern_match && !extern_match(mb,c,len,sep)) continue;
				
				/* 限制每个模糊音的候选，避免太多候选导致输入法长时间失去响应 */
				count++;
				if(count>1536) break;

				if(!head)
					head=l_ptr_array_new(1024);

				if(c->len>2)
					i=c->len<=8?last[(c->len+1)/2-2]:last[3];
				else
					i=0;
				for(;i<head->len;i++)
				{
					struct y_mb_ci *p=l_ptr_array_nth(head,i);
					if(p->len<c->len) continue;
					if(p->len>c->len)
					{
						l_ptr_array_insert(head,i,c);
						if(c->len+1<=8)
							last[(c->len+1)/2-1]=i+1;
						break;
					}
					if(mb_ci_equal(c,(char*)p->data,p->len))
						break;
				}
				if(i==head->len)
				{
					l_ptr_array_append(head,c);
					if(c->len+1<=8)
						last[(c->len+1)/2-1]=i+1;
				}
				got++;
				if(got==ctx->result_count)
					break;
			}
			if(got==ctx->result_count)
				break;
		}
		if(got==ctx->result_count)
			break;
	}
	return head;
}

static int mb_pin_phrase_fuzzy(struct y_mb *mb,LArray *list,const char *code)
{
	char data[sizeof(struct y_mb_pin_item)+32];
	struct y_mb_pin_item *key,*item;
	struct y_mb_pin_ci *c;
	if(!mb->pin)
		return 0;
	key=(struct y_mb_pin_item *)data;
	key->len=(uint8_t)strlen(code);
	if(key->len>8)
		return 0;
	strcpy(key->data,code);
	item=l_hash_table_find(mb->pin,key);
	if(!item)
		return 0;
	for(c=item->list;c!=NULL;c=c->next)
	{
		//mb_add_one(mb,code,key->len,c->data,c->len,c->pos,Y_MB_DIC_PIN);
		int i;
		for(i=0;i<list->len;i++)
		{
			struct y_mb_ci *p=l_ptr_array_nth(list,i);
			if(mb_ci_equal(p,(char*)c->data,c->len))
			{
				l_ptr_array_remove(list,i);
				l_ptr_array_insert(list,c->pos,p);
				break;
			}
		}
	}
	return 0;
}

//#include <time.h>
int y_mb_set_fuzzy(struct y_mb *mb,const char *s,int len,int filter)
{
	LArray *list;
	FUZZY_TABLE *ft=mb->fuzzy;
	int ret;
	assert(ft!=NULL);
	assert(mb->ctx.result_ci==NULL);
	mb->fuzzy=NULL;
	list=fuzzy_key_list(ft,s,len,mb->split);
	if(list->len==1 || mb->nsort || y_mb_has_wildcard(mb,s))
	{
		ret=y_mb_set(mb,s,len,filter);
	}
	else
	{
		struct y_mb_context first;
		int i;
		int found=0;
		int count=0;
		LArray *head=NULL;
		for(i=0;i<list->len;i++)
		{
			char *code=l_ptr_array_nth(list,i);
			int clen=strlen(code);
			ret=y_mb_set(mb,code,clen,filter);
			if(ret<=0) continue;
			found++;
			count+=ret;
			if(found==1)
			{
				y_mb_push_context(mb,&first);
				continue;
			}
			else if(found==2)
			{
				//clock_t start=clock();
				head=add_fuzzy_phrase(NULL,mb,&first);
				//printf("%.3f\n",(clock()*1.0-start)/CLOCKS_PER_SEC);
			}
			//clock_t start=clock();
			head=add_fuzzy_phrase(head,mb,&mb->ctx);
			//printf("%.3f\n",(clock()*1.0-start)/CLOCKS_PER_SEC);
		}
		if(found==1)
		{
			y_mb_pop_context(mb,&first);
		}
		else if(found>1)
		{
			mb_pin_phrase_fuzzy(mb,head,l_ptr_array_nth(list,0));
			count=head->len;
			mb->ctx.result_dummy=2;
			mb->ctx.result_ci=head;
			mb->ctx.result_count=count;
		}
		ret=count;
	}
	l_ptr_array_free(list,l_free);
	mb->fuzzy=ft;
	return ret;
}

int y_mb_set(struct y_mb *mb,const char *s,int len,int filter)
{
	int wildcard;
	uint16_t index_val;
	struct y_mb_index *index;
	int ret;
	int left;
	struct y_mb_index *index_first=0;
	struct y_mb_item *item=0;
	int count=0,count_zi=0,count_ci_ext=0;
	uintptr_t key;
	struct y_mb_context *ctx=&mb->ctx;
	int (*extern_match)(struct y_mb *,struct y_mb_ci *,int,const char *)=NULL;
	const char *orig=s;
	int orig_len=len;
	const char *sep=NULL;
	int assist_mode;

	ctx->result_dummy=0;
	if(mb->ctx.result_ci!=NULL)
	{
		l_ptr_array_free(mb->ctx.result_ci,NULL);
		mb->ctx.result_ci=NULL;
	}
	if(!len)
	{
		ctx->result_count=0;
		ctx->result_count_zi=0;
		ctx->result_count_ci_ext=0;
		return 0;
	}

	if(s[0]==mb->lead && mb->ass && s[1]!=0)
	{
		count=y_mb_set(mb->ass,s+1,len-1,0);
		strcpy(ctx->input+1,mb->ass->ctx.input);
		ctx->input[0]=mb->lead;
		return count;
	}
	if(s[0]==mb->quick_lead && mb->quick_mb && s[1]!=0)
	{
		count=y_mb_set(mb->quick_mb,s+1,len-1,0);
		strcpy(ctx->input+1,mb->quick_mb->ctx.input);
		ctx->input[0]=mb->quick_lead;
		return count;
	}
	if(mb->fuzzy)
	{
		return y_mb_set_fuzzy(mb,s,len,filter);
	}
	assist_mode=(s[0]==mb->lead && mb->ass)||(s[0]==mb->quick_lead && mb->quick_mb);
	
	if(mb->pinyin==1 && mb->split=='\'')
	{
		char *temp=alloca(len+1);
		const char*p;
		int i;
		sep=strchr(s,'\'');
		if(sep) sep++;
		for(p=s,i=0;p<s+len;p++)
		{
			if(*p=='\'') continue;
			temp[i++]=*p;
		}
		temp[i]=0;len=i;
		s=temp;
		extern_match=mb_match_quanpin;
	}

	wildcard=y_mb_has_wildcard(mb,s);
	index_val=mb_ci_index_wildcard(mb,s,len,mb->wildcard,&key);
	left=mb_key_len(key);
	if(!assist_mode && (mb->match || mb->ctx.result_match) && mb_ci_index_code_len(index_val)+left!=len)
	{
		return 0;
	}

	for(index=mb->index;index;index=index->next)
	{
		int base=index->index&0xff?2:1;
		if(wildcard)
		{
			ret=mb_index_cmp_wildcard(mb,index_val,index->index,len,mb->wildcard);
		}
		else
		{
			ret=mb_index_cmp_direct(index_val,index->index,len);
			if(ret<0) break;
		}
		if(ret!=0)
			continue;
		if(index->ci_count==0)
			continue;
		if(filter && index->ci_count-index->ext_count==0)
			continue;
		if(wildcard)
		{
			struct y_mb_item *p;			
			for(p=index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				ret=mb_key_cmp_wildcard(key,p->code,Y_MB_KEY_SIZE);
				if(ret!=0) continue;
				for(c=p->phrase;c;c=c->next)
				{
					if(c->del) continue;
					if(filter && c->zi && c->ext) continue;
					if(!item)
					{
						item=p;
						index_first=index;
					}
					count++;
					if(c->zi)
						count_zi++;
					else if(c->ext)
						count_ci_ext++;
				}
			}
		}
		else if(mb->match || ctx->result_match || len>base || mb->compat || ctx->result_filter_ci_ext)
		{
			struct y_mb_item *p;
			if((p=index->half)!=NULL)
			{
				ret=mb_key_cmp_direct(key,p->code,(ctx->result_match?Y_MB_KEY_SIZE:left));
				if(ret<=0) p=index->item;
			}
			else
			{
				p=index->item;
			}
			for(/*p=index->item*/;p;p=p->next)
			{
				int clen;
				struct y_mb_ci *c;
				ret=mb_key_cmp_direct(key,p->code,(ctx->result_match?Y_MB_KEY_SIZE:left));
				if(ret>0) continue;
				if(mb->nsort && ret<0) continue;
				if(ret<0) break;
				clen=mb_key_len(p->code)+base;
				for(c=p->phrase;c;c=c->next)
				{
					if(c->del) continue;
					if(filter && c->zi && c->ext) continue;
					if(c->zi && mb->simple && c->simp) continue;
					if(!item)
					{
						item=p;
						index_first=index;
					}
					if(c->zi && mb->compat && c->simp && clen>len && mb_simple_exist(mb,s,len,c)) continue;
					if(!c->zi && mb->compat && clen>len+mb->compat-1) continue;
					if(extern_match && !extern_match(mb,c,len,sep)) continue;
					count++;
					if(c->zi)
						count_zi++;
					else if(c->ext)
						count_ci_ext++;
					if(count==1)
					{
						item=p;
						index_first=index;
					}
					if((mb->match || ctx->result_match) && index->index!=index_val)
						break;
					if(mb->compat && len==1 && clen!=1)
						break;
				}
				if(ctx->result_filter_zi)
				{
					if((count_zi) && (mb->match || ctx->result_match))
						break;
					if(ctx->result_filter_ci_ext && count_ci_ext && (mb->match || ctx->result_match))
						break;
				}
				else if(count && (mb->match || ctx->result_match))
				{
					break;
				}
				if(mb->compat && len==1 && clen!=1)
				{
					/* not detect if found at this index, so problem
					 * will happen, when first item have no valid 
					 * phrase and with second item
					 */
					break;
				}
			}
		}
		else
		{
			if(!item)
			{
				item=index->item;
				index_first=index;
			}
			count+=index->ci_count;
			count_zi+=index->zi_count;
			if(filter)
			{
				count-=index->ext_count;
				count_zi-=index->ext_count;
			}
		}
		/* only test one index is enough, so break */
		if(!wildcard && len!=1)
			break;
		if(!wildcard && len==1 && (mb->match || ctx->result_match) && count)
			break;
	}
	if(!count && s[0]==mb->lead && mb->ass && len==1)
	{
		ctx->input[0]=s[0];
		ctx->input[1]=0;
		ctx->result_dummy=1;
		return 1;
	}
	if(!count && s[0]==mb->quick_lead && mb->quick_mb && len==1)
	{
		ctx->input[0]=s[0];
		ctx->input[1]=0;
		ctx->result_dummy=1;
		return 1;
	}
	if(!count && item && mb->compat && !wildcard)
	{
		/* in compact mode, if nothing found, but have normal match,
		 * just use the first normal match one */
		count=1;
		ctx->result_compat=1;
	}
	else
	{
		ctx->result_compat=0;
	}

	if(count)
	{
		ctx->result_count=count;
		ctx->result_first=item;
		ctx->result_filter=filter;
		ctx->result_index=index_first;
		ctx->result_wildcard=wildcard;
		ctx->result_count_zi=count_zi;
		ctx->result_count_ci_ext=count_ci_ext;
		memcpy(ctx->input,orig,orig_len);
		ctx->input[orig_len]=0;
	}
	else
	{
		// 仅仅在count>0的时候修改ctx，使空码保留上一个状态状态
		//ctx->result_count=0;
		//ctx->result_count_zi=0;
	}
	
	if(ctx->result_filter_zi)
	{
		if(ctx->result_filter_ci_ext)
			return count_zi+count_ci_ext;
		else
			return count_zi;
	}
	else
	{
		return count;
	}
}

void y_mb_set_zi(struct y_mb *mb,int zi)
{
	if(zi/* && mb->result_count_zi*/)
		mb->ctx.result_filter_zi=1;
	else
		mb->ctx.result_filter_zi=0;
}

void y_mb_set_ci_ext(struct y_mb *mb,int ci_ext)
{
	if(mb->ctx.result_filter_zi && ci_ext)
		mb->ctx.result_filter_ci_ext=1;
	else
		mb->ctx.result_filter_ci_ext=0;
}

static int y_mb_get_fuzzy(struct y_mb *mb,int at,int num,
	char cand[][MAX_CAND_LEN+1],char tip[][MAX_TIPS_LEN+1])
{
	struct y_mb_ci *c;
	int i;
	for(i=0;i<num;i++)
	{
		c=l_ptr_array_nth(((LArray*)mb->ctx.result_ci),at+i);
		y_mb_ci_string2(c,cand[i]);
		if(tip) tip[i][0]=0;
	}
	return 0;
}

int y_mb_get(struct y_mb *mb,int at,int num,
	char cand[][MAX_CAND_LEN+1],char tip[][MAX_TIPS_LEN+1])
{
	char *s;
	int filter;
	int filter_zi;
	int filter_ext;
	uintptr_t key;
	int wildcard;
	int len,left;
	int ret;
	uint16_t index_val;
	struct y_mb_index *index;
	struct y_mb_item *item;
	int skip=0,got=0;
	struct y_mb_context *ctx=&mb->ctx;
	int (*extern_match)(struct y_mb *,struct y_mb_ci *,int,const char *)=NULL;
	const char *sep=NULL;
	
	if(num==0) return 0;

	s=ctx->input;
	len=strlen(s);
	if(ctx->result_dummy==1)
	{
		if(strlen(s)==1 && strchr(".,?\":;'<>\\!@$^*_()[]&",s[0]))
			sprintf(cand[0],"$BD(%c)",s[0]);
		else
			strcpy(cand[0],s);
		return 0;
	}
	else if(ctx->result_dummy==2)
	{
		return y_mb_get_fuzzy(mb,at,num,cand,tip);
	}
		
	if(s[0]==mb->lead && mb->ass && s[1]!=0)
	{
		return y_mb_get(mb->ass,at,num,cand,tip);
	}
	else if(s[0]==mb->quick_lead && mb->quick_mb && s[1]!=0)
	{
		return y_mb_get(mb->quick_mb,at,num,cand,tip);
	}
	assert(at+num<=ctx->result_count);
	
	if(mb->pinyin==1 && mb->split=='\'')
	{
		char *temp=alloca(len+1);
		const char*p;
		int i;
		sep=strchr(s,'\'');
		if(sep) sep++;
		for(p=s,i=0;p<s+len;p++)
		{
			if(*p=='\'') continue;
			temp[i++]=*p;
		}
		temp[i]=0;len=i;
		s=temp;
		extern_match=mb_match_quanpin;
	}
	
	wildcard=ctx->result_wildcard;
	index_val=mb_ci_index_wildcard(mb,s,len,mb->wildcard,&key);
	left=mb_key_len(key);
	filter=ctx->result_filter;
	filter_zi=ctx->result_filter_zi;
	filter_ext=ctx->result_filter_ext;
	index=ctx->result_index;
	item=ctx->result_first;
	
	if(mb->nsort && !wildcard)
	{
		for(;index;index=index->next)
		{
			struct y_mb_item *p;
			ret=mb_index_cmp_direct(index_val,index->index,len);
			if(ret<0)
				break;
			if(ret!=0)
				continue;
			if(index->ci_count==0)
				continue;
			if(filter && index->ci_count-index->ext_count==0)
				continue;			
			for(p=(index==ctx->result_index)?item:index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				ret=mb_key_cmp_direct(key,p->code,Y_MB_KEY_SIZE);
				if(ret!=0) continue;
				for(c=p->phrase;c;c=c->next)
				{
					if(c->del) continue;
					if(filter && c->zi && c->ext) continue;
					if(filter_zi && !c->zi && !(c->ext && ctx->result_filter_ci_ext)) continue;
					if(filter_ext && c->zi && !c->ext) continue;
					if(skip<at)
					{
						skip++;
						continue;
					}
					strcpy(cand[got],y_mb_ci_string(c));
					if(!mb->english && tip!=NULL)
					{
						if(c->dic==Y_MB_DIC_ASSIST)
						{
							if(c->zi)
								y_mb_get_full_code(mb,cand[got],tip[got]);
							else
								y_mb_get_exist_code(mb,cand[got],tip[got]);
						}
						else
						{
							strcpy(tip[got],mb_key_conv_r(mb,index->index,p->code)+len);
						}
					}
					got++;
					if(got==num) break;
				}
				if(got==num) break;
			}
			if(got==num) return 0;
		}
		/* restore index */
		index=ctx->result_index;
	}

	for(;index;index=index->next)
	{
		if(wildcard)
		{
			ret=mb_index_cmp_wildcard(mb,index_val,index->index,len,mb->wildcard);
		}
		else
		{
			ret=mb_index_cmp_direct(index_val,index->index,len);
			if(ret<0)
				break;
		}
		if(ret!=0)
			continue;
		if(index->ci_count==0)
			continue;
		if(filter && index->ci_count-index->ext_count==0)
			continue;
		if(wildcard)
		{
			struct y_mb_item *p;
			for(p=(index==ctx->result_index)?item:index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				ret=mb_key_cmp_wildcard(key,p->code,Y_MB_KEY_SIZE);
				if(ret!=0) continue;
				for(c=p->phrase;c;c=c->next)
				{
					if(c->del) continue;
					if(filter && c->zi && c->ext) continue;
					if(filter_zi && !c->zi && !(c->ext && ctx->result_filter_ci_ext)) continue;
					if(filter_ext && c->zi && !c->ext) continue;
					if(skip<at)
					{
						skip++;
						continue;
					}
					if(!mb->english && tip!=NULL)
						strcpy(tip[got],mb_key_conv_r(mb,index->index,p->code));
					y_mb_ci_string2(c,cand[got]);
					got++;
					if(got==num) break;
				}
				if(got==num) break;
			}
			if(got==num) break;
		}
		else
		{
			struct y_mb_item *p;	
			for(p=(index==ctx->result_index)?item:index->item;p;p=p->next)
			{
				int clen=mb_key_len(p->code)+(index->index&0xff?2:1);
				struct y_mb_ci *c;
				ret=mb_key_cmp_direct(key,p->code,left);
				if(ret>0) continue;
				if(mb->nsort)
				{
					if(ret<0)
						continue;
					if(0==mb_key_cmp_direct(key,p->code,Y_MB_KEY_SIZE))
						continue; /* this have been got */
				}
				if(ret<0) break;
				for(c=p->phrase;c;c=c->next)
				{
					/* 标记为删除的 */
					if(c->del) continue;
					/* 扩展字符集的字被过滤 */
					if(filter && c->zi && c->ext) continue;
					/* 只要单字但却不是单字 */
					if(filter_zi && !c->zi && !(ctx->result_filter_ci_ext && c->ext)) continue;
					/* 出简不出全碰到了有简码的单字 */
					if(c->zi && mb->simple && c->simp) continue;
					/* 只要非常用汉字，而当前非字或者是常用汉字 */
					if(filter_ext && c->zi && !c->ext) continue;
					if(ctx->result_compat==0)
					{
						if(c->zi && mb->compat && c->simp && clen>len && mb_simple_exist(mb,s,len,c)) continue;
						if(!c->zi && mb->compat && clen>len+mb->compat-1) continue;
					}
					if(extern_match && !extern_match(mb,c,len,sep)) continue;
					/* 跳过不需要取的部分 */
					if(skip<at)
					{
						skip++;
						/* 在一简的时候，每一个非完全匹配的编码只需要一个 */
						if(mb->compat && len==1 && clen!=1) break;
						continue;
					}
					y_mb_ci_string2(c,cand[got]);
					if(!mb->english && tip!=NULL)
					{
						if(c->dic==Y_MB_DIC_ASSIST)
						{
							if(c->zi)
								y_mb_get_full_code(mb,cand[got],tip[got]);
							else
								y_mb_get_exist_code(mb,cand[got],tip[got]);
						}
						else
						{
							strcpy(tip[got],mb_key_conv_r(mb,index->index,p->code)+len);
						}
					}
					got++;
					if(got==num) break;
					if(mb->compat && len==1 && clen!=1)
						break;
				}
				if(got==num) break;
				if(mb->compat && len==1 && clen!=1)
					break;
			}
			if(got==num) break;
		}
	}
	return 0;
}

/* 现在只考虑拼音输入法的单字情况 */
int y_mb_in_result(struct y_mb *mb,struct y_mb_ci *c)
{
	struct y_mb_context *ctx=&mb->ctx;
	struct y_mb_ci *p;
	
	if(ctx->result_ci)
	{
		LArray *arr=ctx->result_ci;
		int i;		
		for(i=0;i<arr->len;i++)
		{
			p=l_ptr_array_nth(arr,i);
			if(!p->zi || p->del)
				continue;
			if(mb_ci_equal(p,(const char*)c->data,c->len))
			{
				return 1;
			}
		}
		return 0;
	}
	if(ctx->result_count_zi==0 || !ctx->result_first)
		return 0;
	for(p=ctx->result_first->phrase;p!=NULL;p=p->next)
	{
		if(!p->zi || p->del)
			continue;
		if(mb_ci_equal(p,(const char*)c->data,c->len))
		{
			return 1;
		}
	}
	return 0;
}

struct y_mb_item *y_mb_get_zi(struct y_mb *mb,const char *s,int len,int filter)
{
	struct y_mb_index *index;
	struct y_mb_item *item;
	uintptr_t key;
	uint16_t index_val;
	int ret;
	
	index_val=mb_ci_index(mb,s,len,&key);
	
	for(index=mb->index;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,2);
		if(ret<0)
			break;
		if(ret!=0)
			continue;
		if(index->zi_count==0)
			continue;
		for(item=index->item;item;item=item->next)
		{
			struct y_mb_ci *c;
			ret=mb_key_cmp_direct(key,item->code,Y_MB_KEY_SIZE);
			if(ret>0) continue;
			if(ret<0) break;
			for(c=item->phrase;c;c=c->next)
			{
				if(c->del) continue;
				if(!c->zi) continue;
				if(filter && c->zi && c->ext) continue;
				return item;
			}
			return NULL;
		}
	}

	return 0;
}

struct y_mb_ci *y_mb_get_first_zi(struct y_mb *mb,const char *s,int len,int filter)
{
	struct y_mb_item *item=y_mb_get_zi(mb,s,len,filter);
	struct y_mb_ci *ci;
	if(!item) return NULL;
	for(ci=item->phrase;ci!=NULL;ci=ci->next)
	{
		if(!ci->zi || ci->del) continue;
		if(filter && ci->zi && ci->ext) continue;
		break;
	}
	return ci;
}

struct y_mb_ci *y_mb_get_first(struct y_mb *mb,char *cand)
{
	char *s;
	int filter;
	int filter_zi;
	uintptr_t key;
	int len;
	int ret;
	uint16_t index_val;
	struct y_mb_index *index;
	struct y_mb_item *item;
	struct y_mb_context *ctx=&mb->ctx;

	s=ctx->input;
	len=strlen(s);

	assert(ctx->result_count>=1);
	index_val=mb_ci_index_wildcard(mb,s,len,mb->wildcard,&key);
	filter=ctx->result_filter;
	filter_zi=ctx->result_filter_zi;
	index=ctx->result_index;
	item=ctx->result_first;

	for(;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,len);
		if(ret<0)
			break;
		if(ret!=0)
			continue;
		if(index->ci_count==0)
			continue;
		if(filter && index->ci_count-index->ext_count==0)
			continue;
		{
			struct y_mb_item *p;	
			for(p=(index==ctx->result_index)?item:index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				ret=mb_key_cmp_direct(key,p->code,Y_MB_KEY_SIZE);
				if(ret>0) continue;
				if(mb->nsort && ret<0) continue;
				if(ret<0) break;
				for(c=p->phrase;c;c=c->next)
				{
					char *temp;
					if(c->del) continue;
					if(filter && c->zi && c->ext) continue;
					if(filter_zi && !c->zi) continue;
					temp=y_mb_ci_string(c);
					if(gb_is_ascii((uint8_t*)temp)) continue;
					if(cand) strcpy(cand,temp);
					return c;
				}
			}
		}
	}
	return 0;
}

struct _l_item{struct y_mb_ci *c;int f;};
static int _l_item_cmpar(struct _l_item *it1,struct _l_item *it2)
{return it2->f-it1->f;}
int y_mb_get_legend(struct y_mb *mb,const char *src,int slen,
		int dlen,char calc[][MAX_CAND_LEN+1],int max)
{
	struct y_mb_zi *z,kz;
	struct y_mb_code *c;
	char start[4]={0,0,0,0};
	int i=0;
	int count;
	LArray *array;
		
	if(slen<2 || !gb_is_gbk((uint8_t*)src) || !mb->zi)
		return 0;
	kz.data=mb_read_u16(src);
	z=mb_hash_find(mb->zi,&kz);
	if(!z)
		return 0;
	for(c=z->code;c;c=c->next)
	{
		int j;
		char key;
		if(c->virt) continue;
		key=y_mb_code_n_key(mb,c,0);
		for(j=0;j<i;j++)
		{
			if(start[j]==key)
				break;
		}
		if(j==i)
		{
			start[i++]=key;
			if(i==4) break;
		}
	}
	dlen*=2; /* dlen in is in hz count, so double it */
	
	array=l_array_new(max+1,sizeof(struct _l_item));
		
	for(i=0;start[i] && i<4;i++)
	{
		uint16_t index_val;
		struct y_mb_index *index;
		struct y_mb_item *p;
		char temp[2]={start[i],0};
		index_val=mb_ci_index(mb,temp,1,0);
		
		for(index=mb->index;index;index=index->next)
		{
			int ret=mb_index_cmp_direct(index_val,index->index,1);
			if(ret<0) break;
			if(ret>0) continue;
			if(index->ci_count-index->zi_count==0)
				continue;
			for(p=index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				int n;
				for(c=p->phrase,n=0;c;c=c->next)
				{
					char *res;
					if(c->del) continue;
					n++;
					if(c->zi || c->len<dlen || c->len<=slen)
						continue;
					res=y_mb_ci_string(c);
					if(!strncmp(res,src,slen))
					{
						struct _l_item item;
						item.c=c;
						item.f=-c->len*10-n; /* 词长短的优先，词序在前的优先 */
						l_array_insert_sorted(array,&item,(LCmpFunc)_l_item_cmpar);
						if(array->len>max)
							array->len=max;
					}
				}
			}
		}
	}
	count=array->len;
	for(i=0;i<count;i++)
	{
		struct _l_item *it=l_array_nth(array,i);
		char *s=y_mb_ci_string(it->c);
		strcpy(calc[i],s+slen);
	}
	l_array_free(array,NULL);
	return count;
}

struct y_mb_ci *y_mb_ci_exist(struct y_mb *mb,const char *data,int dic)
{
	struct y_mb_zi *z;
	struct y_mb_code *c;
	char start[4]={0,0,0,0};
	int i=0;
	int dlen;
	
	dlen=strlen(data);
	if(dlen<2 || !mb->zi)
		return 0;
	z=mb_find_zi(mb,data);
	
	if(!z)
		return 0;
	/* find a list of first code of the phrase */
	for(c=z->code;c;c=c->next)
	{
		int j;
		char key;
		if(c->virt) continue;
		key=y_mb_code_n_key(mb,c,0);
		for(j=0;j<i;j++)
		{
			if(start[j]==key)
				break;
		}
		if(j==i)
		{
			start[i++]=key;
			if(i==4) break;
		}
	}
	for(i=0;start[i] && i<4;i++)
	{
		uint16_t index_val;
		struct y_mb_index *index;
		struct y_mb_item *p;
		char temp[2]={start[i],0};
		index_val=mb_ci_index(mb,temp,1,NULL);
		
		for(index=mb->index;index;index=index->next)
		{
			int ret=mb_index_cmp_direct(index_val,index->index,1);
			if(ret<0) break;
			if(ret>0) continue;
			for(p=index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				for(c=p->phrase;c;c=c->next)
				{
					if(c->del || c->len!=dlen || c->dic==Y_MB_DIC_ASSIST || c->dic!=dic)
						continue;
					if(!mb_ci_equal(c,data,dlen))
						continue;
					return c;
				}
			}
		}
	}
	return NULL;
}

int y_mb_get_exist_code(struct y_mb *mb,const char *data,char *code)
{
	struct y_mb_zi *z;
	struct y_mb_code *c;
	char start[4]={0,0,0,0};
	int i=0;
	int count=0;
	int dlen;
	
	dlen=strlen(data);
	if(dlen<2 || !mb->zi)
		return 0;
	z=mb_find_zi(mb,data);
	
	if(!z)
		return 0;
	/* find a list of first code of the phrase */
	for(c=z->code;c;c=c->next)
	{
		int j;
		char key;
		if(c->virt) continue;
		key=y_mb_code_n_key(mb,c,0);
		for(j=0;j<i;j++)
		{
			if(start[j]==key)
				break;
		}
		if(j==i)
		{
			start[i++]=key;
			if(i==4) break;
		}
	}
	for(i=0;start[i] && i<4;i++)
	{
		uint16_t index_val;
		struct y_mb_index *index;
		struct y_mb_item *p;
		char temp[2]={start[i],0};
		index_val=mb_ci_index(mb,temp,1,NULL);
		
		for(index=mb->index;index;index=index->next)
		{
			int ret=mb_index_cmp_direct(index_val,index->index,1);
			if(ret<0) break;
			if(ret>0) continue;
			for(p=index->item;p;p=p->next)
			{
				struct y_mb_ci *c;
				for(c=p->phrase;c;c=c->next)
				{
					if(c->del || c->len!=dlen || c->dic==Y_MB_DIC_ASSIST)
						continue;
					if(!mb_ci_equal(c,data,dlen))
						continue;
					if(code)
						strcpy(code,mb_key_conv_r(mb,index->index,p->code));
					count++;
					goto out;
				}
			}
		}
	}
out:
	return count;
}

static int mb_super_test(struct y_mb *mb,struct y_mb_ci *c,char super)
{
	char *s;
	struct y_mb_zi *z;
	struct y_mb_code *p;
	if(c->zi || c->len<2 || !mb->zi)
		return 0;
	s=y_mb_ci_string(c);
	if(mb->yong==1)
		s+=c->len-2;
	z=mb_find_zi(mb,s);
	if(!z)
		return 0;
	for(p=z->code;p;p=p->next)
	{
		if(p->virt || p->len<3) continue;
		if(super==((p->val>>20)&0x3f))
			return 1;
	}
	return 0;
}

int y_mb_super_get(struct y_mb *mb,char calc[][MAX_CAND_LEN+1],int max,char super)
{
	char *s;
	int count=0;
	int first_match=0;
	struct y_mb_item *p;
	struct y_mb_context *ctx=&mb->ctx;

	s=ctx->input;
	if(s[0]==mb->lead)
		return 0;
	if(ctx->result_wildcard)
		return 0;
	super=mb->map[(int)super];
	if(!super)
		return 0;
	p=ctx->result_first;

	if(p)
	{
		struct y_mb_ci *c;
		int pos=0;
		for(c=p->phrase;c;c=c->next)
		{
			if(c->del || c->zi) continue;
			pos++;
			if(mb_super_test(mb,c,super))
			{
				strcpy(calc[count],y_mb_ci_string(c));
				count++;
				if(count==max)
					break;
				if(pos==1)
					first_match=1;
			}
		}
	}
	if(count>1 && first_match)
	{
		char temp[MAX_CAND_LEN+1];
		strcpy(temp,calc[0]);
		strcpy(calc[0],calc[1]);
		strcpy(calc[1],temp);
		if(count>2)
		{
			strcpy(temp,calc[2]);
			strcpy(calc[2],calc[1]);
			strcpy(calc[1],temp);
		}
	}
	return count;
}

static int mb_assist_test(struct y_mb *mb,struct y_mb_ci *c,char super,int n,int end)
{
	char *s;
	struct y_mb_zi *z,kz;
	struct y_mb_code *p;
	if(c->len<2 || !mb->zi || !super)
		return 0;
	s=y_mb_ci_string(c);
	if(end!=0)
		s+=c->len-2;
	else if(s[0]=='$' && s[1]=='[')
	{
		char *temp=gb_strchr((uint8_t*)s+2,']');
		if(!temp) return 0;
		s=s+2;
	}
	if(gb_is_gbk((uint8_t*)s))
	{
		//kz.data=*(uint16_t*)s;
		kz.data=mb_read_u16(s);
	}
	else if(gb_is_gb18030_ext((uint8_t*)s))
	{
		//kz.data=*(uint32_t*)s;
		kz.data=mb_read_u32(s);
	}
	else
	{
		return 0;
	}
	z=mb_hash_find(mb->zi,&kz);
	if(!z)
	{
		return 0;
	}
	for(p=z->code;p;p=p->next)
	{
		if(p->virt) continue;
		if(super==mb->map[(int)y_mb_code_n_key(mb,p,n)])
		{
			return 1;
		}
	}
	return 0;
}

int y_mb_is_assist_key(struct y_mb *mb,int key)
{
	int super=0;
	if((key&KEYM_MASK) || key>=128)
		return 0;
	if(mb->yong && !mb->ass)
		super=mb->map[(int)key];
	else if(mb->ass)
		super=mb->ass->map[(int)key];
	return super?1:0;
}

int y_mb_assist_test(struct y_mb *mb,struct y_mb_ci *c,char super,int n,int end)
{
	if(mb->yong && !mb->ass)
	{
		super=mb->map[(int)super];
		return mb_assist_test(mb,c,super,2+n,end);
	}
	else
	{
		if(!mb->ass) return 0;
		super=mb->ass->map[(int)super];
		return mb_assist_test(mb->ass,c,super,n,end);
	}
}

int y_mb_assist_test_hz(struct y_mb *mb,const char *s,char super)
{
	struct y_mb_zi *z,kz;
	struct y_mb_code *p;
	int n;

	if(gb_is_gbk((uint8_t*)s))
	{
		kz.data=mb_read_u16(s);
	}	
	else if(gb_is_gb18030_ext((uint8_t*)s))
	{
		kz.data=mb_read_u32(s);
	}
	else
	{
		return 0;
	}
	
	if(mb->yong && !mb->ass)
	{
		super=mb->map[(int)super];
		n=2;
	}
	else
	{
		if(!mb->ass) return 0;
		mb=mb->ass;
		super=mb->map[(int)super];
		n=0;
	}

	z=mb_hash_find(mb->zi,&kz);
	if(!z)
	{
		return 0;
	}
	for(p=z->code;p;p=p->next)
	{
		if(p->virt) continue;
		if(super==mb->map[(int)y_mb_code_n_key(mb,p,n)])
		{
			return 1;
		}
	}
	return 0;
}

int y_mb_assist_get(struct y_mb *mb,char calc[][MAX_CAND_LEN+1],int max,char super,int end)
{
	char temp[2]={super,0};
	return y_mb_assist_get2(mb,calc,max,temp,end);
}

static int y_mb_assist_get_fuzzy(struct y_mb *mb,char calc[][MAX_CAND_LEN+1],int max,char super[2],int end)
{
	int first_match=0,second_match=0;
	int i;
	int count=0;
	int pos=0;
	for(i=0;i<mb->ctx.result_count;i++)
	{
		struct y_mb_ci *c;
		pos++;
		c=l_ptr_array_nth(mb->ctx.result_ci,i);
		if(y_mb_assist_test(mb,c,super[0],0,end))
		{
			if(!first_match)
				first_match=pos;
			if(super[1])
			{
				if(!y_mb_assist_test(mb,c,super[1],1,end))
					continue;
				if(!second_match)
					second_match=pos;
			}
			strcpy(calc[count],y_mb_ci_string(c));
			count++;
			if(count==max)
				break;
		}
	}
	if(count<=1)
		return count;
	if(first_match==1)
	{
		char temp[MAX_CAND_LEN+1];
		strcpy(temp,calc[0]);
		memmove(calc,calc+1,(count-1)*(MAX_CAND_LEN+1));
		strcpy(calc[count-1],temp);
	}
	if(super[1] && second_match==first_match)
	{
		char temp[MAX_CAND_LEN+1];
		strcpy(temp,calc[0]);
		memmove(calc,calc+1,(count-1)*(MAX_CAND_LEN+1));
		strcpy(calc[count-1],temp);
	}
	return count;
}

int y_mb_assist_get2(struct y_mb *mb,char calc[][MAX_CAND_LEN+1],int max,char super[2],int end)
{
	char *s;
	int len;
	int count=0;
	int first_match=0,second_match=0;
	struct y_mb_item *p;
	struct y_mb_context *ctx=&mb->ctx;
	uintptr_t key;

	if(!mb || !(mb->ass || mb->yong) || !ctx->result_count)
		return 0;
	if(ctx->result_dummy==2 && ctx->result_ci!=NULL)
		return y_mb_assist_get_fuzzy(mb,calc,max,super,end);
	s=ctx->input;
	len=strlen(s);
	if(s[0]==mb->lead)
		return 0;
	if(ctx->result_wildcard)
		return 0;
	mb_ci_index(mb,s,len,&key);
	for(p=ctx->result_first;p && count<max;p=p->next)
	{
		struct y_mb_ci *c;
		int pos=0;
		if(mb->nsort && mb_key_cmp_direct(key,p->code,Y_MB_KEY_SIZE))
		{
			continue;
		}
		for(c=p->phrase;c;c=c->next)
		{
			if(c->del) continue;
			if(end && c->zi) continue;
			pos++;
			if(y_mb_assist_test(mb,c,super[0],0,end))
			{
				if(!first_match)
					first_match=pos;
				if(super[1])
				{
					if(!y_mb_assist_test(mb,c,super[1],1,end))
						continue;
					if(!second_match)
						second_match=pos;
				}
				strcpy(calc[count],y_mb_ci_string(c));
				count++;
				if(count==max)
					break;
			}
		}
		if(!mb->nsort) break;
	}
	if(count<=1)
		return count;
	if(first_match==1)
	{
		char temp[MAX_CAND_LEN+1];
		strcpy(temp,calc[0]);
		memmove(calc,calc+1,(count-1)*(MAX_CAND_LEN+1));
		strcpy(calc[count-1],temp);
	}
	if(super[1] && second_match==first_match)
	{
		char temp[MAX_CAND_LEN+1];
		strcpy(temp,calc[0]);
		memmove(calc,calc+1,(count-1)*(MAX_CAND_LEN+1));
		strcpy(calc[count-1],temp);
	}
	return count;
}

struct y_mb_ci *y_mb_check_assist(struct y_mb *mb,const char *s,int len,char super,int end)
{
	struct y_mb_index *index;
	struct y_mb_item *item;
	uintptr_t key;
	uint16_t index_val;
	int ret;
	
	if(!mb->ass)
		return NULL;
	
	super=mb->ass->map[(int)super];
	
	index_val=mb_ci_index(mb,s,len,&key);
	for(index=mb->index;index;index=index->next)
	{
		ret=mb_index_cmp_direct(index_val,index->index,2);
		if(ret<0)
			break;
		if(ret!=0)
			continue;

		for(item=index->item;item;item=item->next)
		{
			struct y_mb_ci *c;
			ret=mb_key_cmp_direct(key,item->code,Y_MB_KEY_SIZE);
			if(ret>0) continue;
			if(ret<0) break;
			for(c=item->phrase;c;c=c->next)
			{
				if(c->del) continue;
				if(!c->zi) continue;
				if(mb_assist_test(mb->ass,c,super,0,end))
				{
					return c;
				}
			}
			return NULL;
		}
	}

	return NULL;
}

void y_mb_push_context(struct y_mb *mb,struct y_mb_context *ctx)
{
	memcpy(ctx,&mb->ctx,sizeof(*ctx));
	mb->ctx.result_ci=NULL;
}

void y_mb_pop_context(struct y_mb *mb,struct y_mb_context *ctx)
{
	if(mb->ctx.result_ci) {
		l_ptr_array_free(mb->ctx.result_ci,NULL);
	}
	memcpy(&mb->ctx,ctx,sizeof(*ctx));
}

static void mb_zi_virt_count(struct y_mb_zi *z,int *count)
{
	/* don't dump virt code of not main */
	if(!z->code)
		return;
	if(z->code->virt && z->code->main)
		(*count)++;
}

static void mb_zi_zirt_store(struct y_mb_zi *z,struct y_mb_zi ***pz)
{
	/* is z removed or moved z->code will be 0 */
	if(!z->code || !z->code->virt || !z->code->main)
		return;
	**pz=z;
	*pz=(*pz)+1;
}

static int mb_zi_virt_cmp(const void *p1,const void *p2)
{
	struct y_mb_zi *z1=*(struct y_mb_zi**)p1;
	struct y_mb_zi *z2=*(struct y_mb_zi**)p2;
	return y_mb_code_cmp(z1->code,z2->code,Y_MB_KEY_SIZE);
}

#ifndef CFG_XIM_ANDROID



int y_mb_dump(struct y_mb *mb,FILE *fp,int option,int format,char *pre)
{
	struct y_mb_index *index;

	if(!mb)
		return 0;

	if(format==MB_FMT_YONG && (option&MB_DUMP_HEAD))
	{
		fprintf(fp,"name=%s\n",mb->name);
		fprintf(fp,"key=%s\n",mb->key+1);
		if(mb->key0[0])
			fprintf(fp,"key0=%s\n",mb->key0);
		fprintf(fp,"len=%d\n",mb->len);
		if(mb->push[0])
		{
			fprintf(fp,"push=%s",mb->push);
			if(mb->stop)
			{
				int i;
				fprintf(fp," ");
				for(i=0;i<8;i++)
				{
					if(mb->stop & (1<<i))
						fprintf(fp,"%d",i);
				}
			}
			fprintf(fp,"\n");
		}
		if(mb->pull[0])
			fprintf(fp,"pull=%s\n",mb->pull);
		if(mb->match)
			fprintf(fp,"match=1\n");
		if(mb->wildcard_orig)
			fprintf(fp,"wildcard=%c\n",mb->wildcard);
		if(mb->dwf)
			fprintf(fp,"dwf=1\n");
		if(mb->english)
			fprintf(fp,"english=1\n");
		if(mb->simple)
			fprintf(fp,"simple=%d\n",mb->simple);
		if(mb->compat)
			fprintf(fp,"compact=%d\n",mb->compat);
		if(mb->yong)
			fprintf(fp,"yong=%d\n",mb->yong);
		if(mb->pinyin)
			fprintf(fp,"pinyin=%d\n",mb->pinyin);
		if(!mb->hint)
			fprintf(fp,"hint=0\n");
		if(mb->auto_clear)
			fprintf(fp,"auto_clear=%d\n",mb->auto_clear);
		if(mb->nomove[0])
			fprintf(fp,"nomove=%s\n",mb->nomove);
		if(mb->auto_move)
			fprintf(fp,"auto_move=%d\n",mb->auto_move);
		if(mb->nsort)
			fprintf(fp,"nsort=1\n");
		if(mb->sloop)
			fprintf(fp,"sloop=%d\n",mb->sloop);
		if(mb->split>0 && mb->split<10)
			fprintf(fp,"split=%d\n",mb->split);
		else if(mb->split)
			fprintf(fp,"split=%c\n",mb->split);
		if(mb->commit_mode || mb->commit_len || mb->commit_which)
			fprintf(fp,"commit=%d %d %d\n",mb->commit_mode,
				mb->commit_len,mb->commit_which);
		if(mb->dicts[0])
		{
			int i;
			fprintf(fp,"dicts=");
			for(i=0;i<10 && mb->dicts[i];i++)
			{
				if(i) fprintf(fp," ");
				fprintf(fp,"%s",mb->dicts[i]);
			}
			fprintf(fp,"\n");
		}
		if(mb->user && strcmp(mb->user,"user.txt"))
			fprintf(fp,"user=%s\n",mb->user);
		if(mb->normal && strcmp(mb->normal,"normal.txt"))
			fprintf(fp,"normal=%s\n",mb->normal);
		if(mb->skip[0])
			fprintf(fp,"skip=%s\n",mb->skip);
		if(mb->bihua[0])
			fprintf(fp,"bihua=%s\n",mb->bihua);
		if(mb->lead && mb->ass_main)
			fprintf(fp,"assist=%c %s\n",mb->lead,mb->ass_main);
		else if(mb->ass_main)
			fprintf(fp,"assist=%s\n",mb->ass_main);
		if(mb->rule)
			mb_rule_dump(mb,fp);
		fprintf(fp,"[DATA]\n");
	}
	for(index=mb->index;index;index=index->next)
	{
		struct y_mb_item *it=index->item;
		while(it)
		{
			char *code;
			char *data;
			struct y_mb_ci *cp;
			code=mb_key_conv_r(mb,index->index,it->code);
			cp=it->phrase;
			if(!cp)
			{
				//fprintf(fp,"%s\n",code);
			}
			else if(mb->english)
			{
				fprintf(fp,"%s\n",y_mb_ci_string(cp));
			}
			else
			{
				int cped=0;

				while(cp)
				{
					if(cp->del)
						goto dump_next;
					if(cp->dic==Y_MB_DIC_MAIN)
					{
						if(!(option&MB_DUMP_MAIN))
							goto dump_next;
					}
					else if(cp->dic==Y_MB_DIC_USER)
					{
						if(!(option&MB_DUMP_USER))
							goto dump_next;
					}
					else if(cp->dic==Y_MB_DIC_TEMP)
					{
						if(!(option&MB_DUMP_TEMP))
							goto dump_next;
					}
					else if(cp->dic==Y_MB_DIC_ASSIST)
					{
						/* never dump assist mb */
						goto dump_next;
					}
					else if(!(option&MB_DUMP_DICTS))
					{
						goto dump_next;
					}
					data=y_mb_ci_string(cp);
					if(format==MB_FMT_YONG)
					{
						int revert=0;
						if(!cped)
						{
							cped=1;
							if(pre) fprintf(fp,"%s",pre);
							fprintf(fp,"%s",code);
						}
						if(cp->zi)
						{
							//if(gb_is_gb2312((uint8_t*)data) == cp->ext)
							if(gb_is_normal((uint8_t*)data) == cp->ext)
								revert=1;
						}
						if(revert)
							fprintf(fp," ~%s",data);
						else
							fprintf(fp," %s",data);
					}
					else if(format==MB_FMT_WIN)
					{
						if(pre) fprintf(fp,"%s",pre);
						fprintf(fp,"%s%s\n",y_mb_ci_string(cp),code);
					}
					else if(format==MB_FMT_FCITX)
					{
						if(pre) fprintf(fp,"%s",pre);
						fprintf(fp,"%s %s\n",code,y_mb_ci_string(cp));
					}
					else if(format==MB_FMT_SCIM)
					{
						if(pre) fprintf(fp,"%s",pre);
						fprintf(fp,"%s\t%s\t0\n",code,y_mb_ci_string(cp));
					}
dump_next:
					cp=cp->next;
				}
				if(cped && format==MB_FMT_YONG)
					fprintf(fp,"\n");
			}
			it=it->next;
		}
	}
	if(format==MB_FMT_YONG && (option&MB_DUMP_ADJUST) && mb->zi)
	{
		int count=0;
		mb_hash_foreach(mb->zi,(LFunc)mb_zi_virt_count,&count);
		if(count>0)
		{
			struct y_mb_zi **list,**p;
			int i;
			p=list=calloc(count,sizeof(struct y_mb_zi*));
			mb_hash_foreach(mb->zi,(LFunc)mb_zi_zirt_store,&p);
			qsort(list,count,sizeof(struct y_mb_zi*),mb_zi_virt_cmp);
			for(i=0;i<count;i++)
			{
				struct y_mb_zi *z=list[i];
				char code[Y_MB_KEY_SIZE+1];
				y_mb_code_get_string(mb,z->code,code);
				fprintf(fp,"^%s %s\n",code,mb_data_conv_r(z->data));
			}
			free(list);
		}
	}
	return 0;
}

static int y_mb_pick(struct y_mb *mb,FILE *fp,int option,int clen,int dlen,int filter,char *pre)
{
	struct y_mb_index *index;

	if(!mb)
		return 0;
	for(index=mb->index;index;index=index->next)
	{
		struct y_mb_item *it=index->item;
		while(it)
		{
			char *code;
			char *data;
			struct y_mb_ci *cp;
			code=mb_key_conv_r(mb,index->index,it->code);				
			cp=it->phrase;
			if(clen && clen!=strlen(code))
			{
			}
			else if(!cp)
			{
			}
			else if(mb->english)
			{
			}
			else
			{
				while(cp)
				{
					if(cp->del)
						goto dump_next;
					data=y_mb_ci_string(cp);
					if(dlen && dlen!=gb_strlen((uint8_t*)data))
						goto dump_next;
					if(filter && cp->zi && cp->ext)
						goto dump_next;
					if(cp->dic==Y_MB_DIC_MAIN)
					{
						if(!(option&MB_DUMP_MAIN))
							goto dump_next;
					}
					else if(cp->dic==Y_MB_DIC_USER)
					{
						if(!(option&MB_DUMP_USER))
							goto dump_next;
					}
					else if(cp->dic==Y_MB_DIC_TEMP)
					{
						if(!(option&MB_DUMP_TEMP))
							goto dump_next;
					}
					else if(!(option&MB_DUMP_DICTS))
					{
						goto dump_next;
					}
					if(pre) fprintf(fp,"%s",pre);
					fprintf(fp,"%s %s\n",code,y_mb_ci_string(cp));
dump_next:
					cp=cp->next;
				}
			}
			it=it->next;
		}
	}
	return 0;
}

static int y_mb_encode(struct y_mb *mb,FILE *fp,char *fn)
{
	FILE *in;
	char line[Y_MB_DATA_SIZE+1];
	char code[Y_MB_KEY_SIZE+1];
	int len;
	
	if(!strcmp(fn,"-"))
		in=stdin;
	else
		in=fopen(fn,"r");
	if(!in)
		return -1;
	for(;(len=l_get_line(line,sizeof(line),in))>=0;)
	{
		if(len==0 || line[0]==0)
			continue;
		if(y_mb_get_exist_code(mb,line,code))
		{
			fprintf(fp,"%s %s\n",code,line);
		}
		else
		{
			if(0==y_mb_code_by_rule(mb,line,strlen(line),code,NULL))
			{
				fprintf(fp,"%s %s\n",code,line);
			}
		}
	}
	if(in!=stdin)
		fclose(in);
	return 0;
}

static int y_mb_encode_py(struct y_mb *mb,FILE *fp,char *fn)
{
	FILE *in;
	char line[Y_MB_DATA_SIZE+1];
	char code[Y_MB_KEY_SIZE+1];
	int len;
	char *p;
	
	if(!strcmp(fn,"-"))
		in=stdin;
	else
		in=fopen(fn,"r");
	if(!in)
		return -1;
	for(;(len=l_get_line(line,sizeof(line),in))>=0;)
	{
		/* if it is null line */
		if(len==0 || line[0]==0)
			continue;
		/* skip space and english */
		p=line;	while(p[0] && !(p[0]&0x80)) p++;
		if(y_mb_get_exist_code(mb,p,code) && strlen(code)<=11)
		{
			fprintf(fp,"%s %s\n",code,line);
		}
		else
		{
			if(0==y_mb_code_by_rule(mb,p,strlen(line),code,NULL))
			{
				fprintf(fp,"%s %s\n",code,line);
			}
		}
	}
	if(in!=stdin)
		fclose(in);
	return 0;
}

static void mb_strip_zi(char *s,int len)
{
	int t;

	while(*s && len>0)
	{
		if(len>=4 && gb_is_gb18030_ext((uint8_t*)s))
		{
			s+=4;
			len-=2;
			continue;
		}
		else if(len>=2 && gb_is_gbk((uint8_t*)s))
		{
			s+=2;
			len-=2;
			continue;
		}
		t=strlen(s+1)+1;
		memmove(s,s+1,t);
		len--;
	}
}

int y_mb_diff(struct y_mb *mb,FILE *fp,char *fn,int strict)
{
	FILE *in;
	char line[Y_MB_DATA_SIZE+1];
	char code[Y_MB_KEY_SIZE+1];
	int len;

	if(!strcmp(fn,"-"))
		in=stdin;
	else
		in=fopen(fn,"r");
	if(!in)
		return -1;
	for(;(len=l_get_line(line,sizeof(line),in))>=0;)
	{
		mb_strip_zi(line,len);
		if(len==0 || line[0]==0)
			continue;
		if(y_mb_get_exist_code(mb,line,code))
			continue;
		if(strict)
		{
			if(y_mb_code_by_rule(mb,line,strlen(line),code,NULL)!=0)
				continue;
			if(y_mb_code_exist(mb,code,strlen(code),-1))
				continue;
		}
		fprintf(fp,"%s\n",line);
	}
	if(in!=stdin)
		fclose(in);
	return 0;
}

/* 获得按键最少的编码序列 */
static int mb_code_least(struct y_mb *mb,struct y_mb_zi *z,char *code)
{
	struct y_mb_code *c;
	int len;
	int ret=Y_MB_KEY_SIZE;
	int key;
	for(c=z->code;c;c=c->next)
	{
		int space=1;
		if(c->virt) continue;
		len=c->len;
		if((mb->yong && len==2) || (!mb->yong && len>1))
		{
			key=y_mb_code_n_key(mb,c,len-1);
			if(y_mb_is_stop(mb,key,len))
			{
				len--;
				space=0;
			}
		}
		if(len<ret)
		{
			ret=len;
			if(code)
			{
				y_mb_code_get_string(mb,c,code);
				if(space)
					strcat(code,"_");
			}
		}
	}
	return ret;
}

struct mb_stat{
	struct y_mb *mb;
	int level;
	int count[3];
};

static void mb_stat_cb(struct y_mb_zi *z,struct mb_stat *st)
{
	char *s;
	int len;
	
	s=mb_data_conv_r(z->data);
	if(gb_is_biaodian((uint8_t*)s))
		return;
	if(st->level && !gb_is_hz((uint8_t*)s))
		return;
	if(st->level==1 && !gb_is_hz1((uint8_t*)s))
		return;
	len=mb_code_least(st->mb,z,0);
	if(len<=3)
		st->count[len-1]++;
}

int y_mb_stat(struct y_mb *mb,FILE *fp,int level)
{
	struct mb_stat st={mb,level,{0,0,0}};
	
	if(!mb->zi)
		return -1;
	mb_hash_foreach(mb->zi,(LFunc)mb_stat_cb,&st);
	fprintf(fp,"%d %d %d\n",st.count[0],st.count[1],st.count[2]);
	return 0;
}

int y_mb_play(struct y_mb *mb,FILE *fp,const char *fn)
{
	char line[4096];
	char code[Y_MB_KEY_SIZE+1];
	uint8_t *s;
	int len;
	FILE *in;
	struct y_mb_zi kz,*z;
	
	int zi_count=0;
	int key_count=0;
	int lost_count=0;
	
	if(!strcmp(fn,"-"))
		in=stdin;
	else
		in=fopen(fn,"r");
	if(!in)
		return -1;
		
	for(;(len=l_get_line(line,sizeof(line),in))>=0;)
	{
		if(len==0 || line[0]==0)
			continue;
		s=(uint8_t *)line;
		while(len>0)
		{
			if(len>=4 && gb_is_gb18030_ext(s))
			{
				kz.data=*(uint32_t*)s;
				z=mb_hash_find(mb->zi,&kz);
				if(!z)
				{
					lost_count++;
				}
				else
				{
					mb_code_least(mb,z,code);
					zi_count++;
					key_count+=strlen(code);
				}
				s+=4;
				len-=4;
			}
			else if(len>=2 && gb_is_biaodian(s))
			{
				s+=2;
				len-=2;
			}
			else if(len>=2 && gb_is_gbk(s))
			{
				kz.data=*(uint16_t*)s;
				z=mb_hash_find(mb->zi,&kz);
				if(!z)
				{
					lost_count++;
				}
				else
				{
					mb_code_least(mb,z,code);
					zi_count++;
					key_count+=strlen(code);
				}
				s+=2;
				len-=2;
			}
			else
			{
				s++;
				len--;
			}
		}
	}
	
	/* todo: 1,2,3 level count, select count */
	if(zi_count)
		fprintf(fp,"%.2f\n",(double)key_count/(double)zi_count);

	if(in!=stdin)
		fclose(in);
	return 0;
}

static int _str_compar(const char **v1, const char **v2)
{
	return strcmp(*v1,*v2);
}
int y_mb_sort_file(const char *fn,int flag,const char *with)
{
	FILE *fp;
	LArray *list;
	char line[1024];
	int i;
	
	fp=fopen(fn,"rb");
	if(!fp) return 0;
	list=l_ptr_array_new(4096);
	while(!feof(fp))
	{
		int ret=l_get_line(line,sizeof(line),fp);
		if(ret==-1) break;
		char *p=l_strdup(line);
		l_ptr_array_append(list,p);
	}
	fclose(fp);
	if(with!=NULL)
	{
		char *p=l_strdup(with);
		l_ptr_array_append(list,p);
	}
	qsort(list->data,list->len,list->size,(LCmpFunc)_str_compar);
	fp=fopen(fn,"w");
	if(fp!=NULL)
	{
		const char *prev=NULL;
		for(i=0;i<list->len;i++)
		{
			const char **p=l_array_nth(list,i);
			if(!flag || !prev || (prev && strcmp(prev,*p)))
				fprintf(fp,"%s\n",*p);
			prev=*p;
		}
		fclose(fp);
	}
	l_ptr_array_free(list,l_free);
	return 0;
}

#endif

#ifdef TOOLS_TEST

#include <time.h>
EXTRA_IM EIM;

int main(int arc,char *arg[])
{
	clock_t start;
	struct y_mb *mb;
	
	if(arc!=2)
		return 0;
	y_mb_init();
	start=clock();
	mb=y_mb_load(arg[1],0,0);
	//y_mb_dump(mb,stdout,MB_DUMP_HEAD|MB_DUMP_MAIN,MB_FMT_YONG,NULL);
	y_mb_free(mb);
	fprintf(stderr,"load in %.2f seconds\n",((double)(clock()-start))/(double)CLOCKS_PER_SEC);
	y_mb_cleanup();
	return 0;
}
#endif

#ifndef CFG_XIM_ANDROID
L_EXPORT(int tool_main(int arc,char **arg))
{
	struct y_mb *mb;
	int i;
	FILE *fp;
	
	if(arc>=2 && !strcmp(arg[0],"sort"))
	{
		int flag=0;
		const char *with=NULL;
		for(i=1;i<arc;i++)
		{
			if(!strcmp(arg[i],"--merge"))
				flag|=1;
			else if(!strncmp(arg[i],"--with=",7))
				with=arg[i]+7;
			else
				y_mb_sort_file(arg[i],flag,with);
		}
		return 0;
	}

	if(arc<3)
		return -1;
		
	y_mb_init();
	mb=y_mb_load(arg[0],MB_FLAG_SLOW,0);
	if(!mb)
	{
		y_mb_cleanup();
		return -2;
	}
	if(!strcmp(arg[1],"-"))
		fp=stdout;
	else
	{
		if(strstr(arg[1],".dat") || strstr(arg[1],".bin"))
			fp=y_mb_open_file(arg[1],"wb");
		else
			fp=y_mb_open_file(arg[1],"w");
	}
	if(!fp)
	{
		y_mb_free(mb);
		y_mb_cleanup();
		return -3;
	}
	if(!strcmp(arg[2],"dump"))
	{
		int option=0,format=MB_FMT_YONG;
		char *pre=NULL;
		for(i=3;i<arc;i++)
		{
			if(!strncmp(arg[i],"--format=",9))
			{
				if(!strcmp(arg[i]+9,"win"))
					format=MB_FMT_WIN;
				else if(!strcmp(arg[i]+9,"fcitx"))
					format=MB_FMT_FCITX;
				else if(!strcmp(arg[i]+9,"scim"))
					format=MB_FMT_SCIM;
				else if(!strcmp(arg[i]+9,"yong"))
					format=MB_FMT_YONG;
			}
			else if(!strncmp(arg[i],"--option=",9))
			{
				char **list;
				int j;
				list=l_strsplit(arg[i]+9,',');
				for(j=0;list[j];j++)
				{
					if(!strcmp(list[j],"main"))
						option|=MB_DUMP_MAIN;
					else if(!strcmp(list[j],"temp"))
						option|=MB_DUMP_TEMP;
					else if(!strcmp(list[j],"user"))
						option|=MB_DUMP_USER;
					else if(!strcmp(list[j],"dicts"))
						option|=MB_DUMP_DICTS;
					else if(!strcmp(list[j],"adjust"))
						option|=MB_DUMP_ADJUST;
					else if(!strcmp(list[j],"head"))
						option|=MB_DUMP_HEAD;
					else if(!strcmp(list[j],"all"))
						option|=MB_DUMP_ALL;
				}
				l_strfreev(list);
			}
			else if(!strncmp(arg[i],"--add=",6))
			{
				FILE *tmp;
				tmp=fopen(arg[i]+6,"r");
				if(tmp)
				{
					mb_load_data(mb,tmp,Y_MB_DIC_TEMP);
					option|=MB_DUMP_TEMP;
					fclose(tmp);
				}
			}
			else if(!strncmp(arg[i],"--prefix=",9))
			{
				pre=arg[i]+9;
			}
		}
		y_mb_dump(mb,fp,option,format,pre);
	}
	else if(!strcmp(arg[2],"pick"))
	{
		int option=0,clen=0,dlen=0,filter=0;
		char *pre=NULL;
		for(i=3;i<arc;i++)
		{
			if(!strncmp(arg[i],"--option=",9))
			{
				char **list;
				int j;
				list=l_strsplit(arg[i]+9,',');
				for(j=0;list[j];j++)
				{
					if(!strcmp(list[j],"main"))
						option|=MB_DUMP_MAIN;
					else if(!strcmp(list[j],"temp"))
						option|=MB_DUMP_TEMP;
					else if(!strcmp(list[j],"user"))
						option|=MB_DUMP_USER;
					else if(!strcmp(list[j],"dicts"))
						option|=MB_DUMP_DICTS;
					else if(!strcmp(list[j],"adjust"))
						option|=MB_DUMP_ADJUST;
					else if(!strcmp(list[j],"all"))
						option|=MB_DUMP_ALL;
				}
				l_strfreev(list);
			}
			else if(!strncmp(arg[i],"--prefix=",9))
			{
				pre=arg[i]+9;
			}
			else if(!strncmp(arg[i],"--dlen=",7))
			{
				dlen=atoi(arg[i]+7);
			}
			else if(!strncmp(arg[i],"--clen=",7))
			{
				clen=atoi(arg[i]+7);
			}
			else if(!strcmp(arg[i],"--normal"))
			{
				filter=1;
			}
		}
		//printf("%x %d %d\n",option,clen,dlen);
		y_mb_pick(mb,fp,option,clen,dlen,filter,pre);
	}
	else if(!strcmp(arg[2],"encode_py"))
	{
		for(i=3;i<arc;i++)
		{
			y_mb_encode_py(mb,fp,arg[i]);
		}
	}
	else if(!strcmp(arg[2],"encode"))
	{
		for(i=3;i<arc;i++)
		{
			y_mb_encode(mb,fp,arg[i]);
		}
	}
	else if(!strcmp(arg[2],"diff"))
	{
		int strict=0;
		for(i=3;i<arc;i++)
		{
			if(!strcmp(arg[i],"--code"))
			{
				strict=1;
				continue;
			}
			y_mb_diff(mb,fp,arg[i],strict);
		}
	}
	else if(!strcmp(arg[2],"stat"))
	{
		int level=0;
		for(i=3;i<arc;i++)
		{
			if(!strncmp(arg[i],"--level=",8))
			{
				if(!strcmp(arg[i]+8,"hz1"))
					level=1;
				else if(!strcmp(arg[i]+8,"hz12"))
					level=2;
			}
		}
		y_mb_stat(mb,fp,level);
	}
	else if(!strcmp(arg[2],"play"))
	{
		for(i=3;i<arc;i++)
		{
			y_mb_play(mb,fp,arg[i]);
		}
	}
	if(fp!=stdout)
		fclose(fp);
	y_mb_free(mb);
	return 0;
}
#endif
