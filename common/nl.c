#include <time.h>
#include <stdio.h>

#if 0
#define START_YEAR 2001
#define END_YEAR 2050

//数组LunarMonthDay存入农历2000年到2100年每年中的月天数信息，
//阴历每月只能是29或30天，一年用12（或13）个二进制位表示，对应位为1表30天，否则为29天
static const unsigned short LunarMonthDay[] = {
// 2001.1.1 -- 2050.12.31
0xd4a8, 0xd4a0, 0xda50, 0x5aa8, 0x56a0, 0xaad8, 0x25d0, 0x92d0, 0xc958, 0xa950, //2010
0xb4a0, 0xb550, 0xb550, 0x55a8, 0x4ba0, 0xa5b0, 0x52b8, 0x52b0, 0xa930, 0x74a8, //2020
0x6aa0, 0xad50, 0x4da8, 0x4b60, 0x9570, 0xa4e0, 0xd260, 0xe930, 0xd530, 0x5aa0, //2030
0x6b50, 0x96d0, 0x4ae8, 0x4ad0, 0xa4d0, 0xd258, 0xd250, 0xd520, 0xdaa0, 0xb5a0, //2040
0x56d0, 0x4ad8, 0x49b0, 0xa4b8, 0xa4b0, 0xaa50, 0xb528, 0x6d20, 0xada0, 0x55b0};//2050

//数组LanarMonth存放农历2001年到2050年闰月的月份，如没有则为0，每字节存两年
static const unsigned char LunarMonth[]={
0x40, 0x02, 0x07, 0x00, 0x50, //2010
0x04, 0x09, 0x00, 0x60, 0x04, //2020
0x00, 0x20, 0x60, 0x05, 0x00, //2030
0x30, 0xb0, 0x06, 0x00, 0x50, //2040
0x02, 0x07, 0x00, 0x50, 0x03}; //2050

#else
#define START_YEAR 1901
#define END_YEAR 2100
//数组LunarMonthDay存入农历2000年到2100年每年中的月天数信息，
//阴历每月只能是29或30天，一年用12（或13）个二进制位表示，对应位为1表30天，否则为29天
static const unsigned short LunarMonthDay[] = {
	// 测试数据：1901.1.1 -- 2100.12.31  
    0X4ae0, 0Xa570, 0X5268, 0Xd260, 0Xd950, 0X6aa8, 0X56a0, 0X9ad0, 0X4ae8, 0X4ae0,   // 1910  
    0Xa4d8, 0Xa4d0, 0Xd250, 0Xd548, 0Xb550, 0X56a0, 0X96d0, 0X95b0, 0X49b8, 0X49b0,   // 1920  
    0Xa4b0, 0Xb258, 0X6a50, 0X6d40, 0Xada8, 0X2b60, 0X9570, 0X4978, 0X4970, 0X64b0,   // 1930  
    0Xd4a0, 0Xea50, 0X6d48, 0X5ad0, 0X2b60, 0X9370, 0X92e0, 0Xc968, 0Xc950, 0Xd4a0,   // 1940  
    0Xda50, 0Xb550, 0X56a0, 0Xaad8, 0X25d0, 0X92d0, 0Xc958, 0Xa950, 0Xb4a8, 0X6ca0,   // 1950  
    0Xb550, 0X55a8, 0X4da0, 0Xa5b0, 0X52b8, 0X52b0, 0Xa950, 0Xe950, 0X6aa0, 0Xad50,   // 1960  
    0Xab50, 0X4b60, 0Xa570, 0Xa570, 0X5260, 0Xe930, 0Xd950, 0X5aa8, 0X56a0, 0X96d0,   // 1970  
    0X4ae8, 0X4ad0, 0Xa4d0, 0Xd268, 0Xd250, 0Xd528, 0Xb540, 0Xb6a0, 0X96d0, 0X95b0,   // 1980  
    0X49b0, 0Xa4b8, 0Xa4b0, 0Xb258, 0X6a50, 0X6d40, 0Xada0, 0Xab60, 0X9370, 0X4978,   // 1990  
    0X4970, 0X64b0, 0X6a50, 0Xea50, 0X6b28, 0X5ac0, 0Xab60, 0X9368, 0X92e0, 0Xc960,   // 2000  
    0Xd4a8, 0Xd4a0, 0Xda50, 0X5aa8, 0X56a0, 0Xaad8, 0X25d0, 0X92d0, 0Xc958, 0Xa950,   // 2010  
    0Xb4a0, 0Xb550, 0Xb550, 0X55a8, 0X4ba0, 0Xa5b0, 0X52b8, 0X52b0, 0Xa930, 0X74a8,   // 2020  
    0X6aa0, 0Xad50, 0X4da8, 0X4b60, 0X9570, 0Xa4e0, 0Xd260, 0Xe930, 0Xd530, 0X5aa0,   // 2030  
    0X6b50, 0X96d0, 0X4ae8, 0X4ad0, 0Xa4d0, 0Xd258, 0Xd250, 0Xd520, 0Xdaa0, 0Xb5a0,   // 2040  
    0X56d0, 0X4ad8, 0X49b0, 0Xa4b8, 0Xa4b0, 0Xaa50, 0Xb528, 0X6d20, 0Xada0, 0X55b0,   // 2050  
    0X9370, 0X4978, 0X4970, 0X64b0, 0X6a50, 0Xea50, 0X6b20, 0Xab60, 0Xaae0, 0X92e0,   // 2060  
    0Xc970, 0Xc960, 0Xd4a8, 0Xd4a0, 0Xda50, 0X5aa8, 0X56a0, 0Xa6d0, 0X52e8, 0X52d0,   // 2070  
    0Xa958, 0Xa950, 0Xb4a0, 0Xb550, 0Xad50, 0X55a0, 0Xa5d0, 0Xa5b0, 0X52b0, 0Xa938,   // 2080  
    0X6930, 0X7298, 0X6aa0, 0Xad50, 0X4da8, 0X4b60, 0Xa570, 0X5270, 0Xd260, 0Xe930,   // 2090  
    0Xd520, 0Xdaa0, 0X6b50, 0X56d0, 0X4ae0, 0Xa4e8, 0Xa4d0, 0Xd150, 0Xd928, 0Xd520,   // 2100
};

//数组LanarMonth存放农历2001年到2050年闰月的月份，如没有则为0，每字节存两年
static const unsigned char LunarMonth[]={
	0X00, 0X50, 0X04, 0X00, 0X20,   // 1910  
    0X60, 0X05, 0X00, 0X20, 0X70,   // 1920  
    0X05, 0X00, 0X40, 0X02, 0X06,   // 1930  
    0X00, 0X50, 0X03, 0X07, 0X00,   // 1940  
    0X60, 0X04, 0X00, 0X20, 0X70,   // 1950  
    0X05, 0X00, 0X30, 0X80, 0X06,   // 1960  
    0X00, 0X40, 0X03, 0X07, 0X00,   // 1970  
    0X50, 0X04, 0X08, 0X00, 0X60,   // 1980  
    0X04, 0X0a, 0X00, 0X60, 0X05,   // 1990  
    0X00, 0X30, 0X80, 0X05, 0X00,   // 2000  
    0X40, 0X02, 0X07, 0X00, 0X50,   // 2010  
    0X04, 0X09, 0X00, 0X60, 0X04,   // 2020  
    0X00, 0X20, 0X60, 0X05, 0X00,   // 2030  
    0X30, 0Xb0, 0X06, 0X00, 0X50,   // 2040  
    0X02, 0X07, 0X00, 0X50, 0X03,   // 2050  
    0X08, 0X00, 0X60, 0X04, 0X00,   // 2060  
    0X30, 0X70, 0X05, 0X00, 0X40,   // 2070  
    0X80, 0X06, 0X00, 0X40, 0X03,   // 2080  
    0X07, 0X00, 0X50, 0X04, 0X80,   // 2090  
    0X00, 0X60, 0X04, 0X00, 0X20    // 2100 
};

#endif

/* 返回指定年的闰月 */
static int GetLeapMonth(int LunarYear)
{
	int flag;
	if((LunarYear < START_YEAR) || (LunarYear > END_YEAR))
 		return 0;
	flag = LunarMonth[(LunarYear - START_YEAR) / 2];
	if((LunarYear - START_YEAR) % 2 == 0)
		return flag >> 4;
	else
		return flag & 0x0F;
}

/* 获得指定月的天数，高两个字节表示闰月的天数 */
static unsigned int LunarMonthDays(int LunarYear,int LunarMonth)
{
	int Height,Low;
	int iBit;

	if((LunarYear < START_YEAR) || (LunarYear > END_YEAR))
		return 30;

	Height = 0;
	Low = 29;
	iBit = 16 - LunarMonth;
	if ((LunarMonth > GetLeapMonth(LunarYear)) && (GetLeapMonth(LunarYear) > 0))
		iBit--;

	if ((LunarMonthDay[LunarYear - START_YEAR] & (1 << iBit)) > 0)
		Low=30;

	if (LunarMonth == GetLeapMonth(LunarYear))
	{
		if ((LunarMonthDay[LunarYear - START_YEAR] & (1 << (iBit-1)))>0)
			Height = 30;
		else
			Height = 29;
	}

	return (unsigned int)(Low|(Height<<16));

}

//返回阴历iLunarYear年的总天数
static int LunarYearDays(int LunarYear)
{
	int Days,i;
	unsigned tmp;

	if ((LunarYear < START_YEAR) || (LunarYear > END_YEAR))
		return 0;

	Days = 0;

	for(i=1; i <= 12; i++)
	{
		tmp = LunarMonthDays(LunarYear, i);
		Days += (tmp>>16) & 0xFFFF;
		Days += tmp&0xffff;
	}
	return Days;
}

//计算从2001年1月1日过iSpanDays天后的阴历日期
//阳历2001年1月24日为阴历2001年正月初一
//阳历2001年1月1日到1月24日共有23天
static void LunarDay(int *year,int *month,int *day,int iSpanDays)
{
	unsigned int tmp;
#if START_YEAR==2001
	iSpanDays = iSpanDays - 23;
#else
	iSpanDays = iSpanDays - 49;
#endif
	*year=START_YEAR;
	*month=1;
	*day=1;
	tmp = LunarYearDays(*year);
	while(iSpanDays>=tmp)
	{
		iSpanDays = iSpanDays - tmp;
		*year=*year+1;
		tmp = LunarYearDays(*year);
	}
	tmp = LunarMonthDays(*year, *month)&0xffff;
	while (iSpanDays >= tmp)
	{
		iSpanDays = iSpanDays - tmp;
		if (*month == GetLeapMonth(*year))
		{
			tmp = (LunarMonthDays(*year, *month)>>16)&0xFFFF;
			if (iSpanDays < tmp)
				break;
			iSpanDays = iSpanDays - tmp;
		}
		*month=*month+1;
		tmp = LunarMonthDays(*year, *month)&0xffff;
	}
	*day+=iSpanDays;
}

static const char *nl_mon[]={
	"正月","二月","三月","四月",
	"五月","六月","七月","八月",
	"九月","十月","十一月","十二月"};
static const char *nl_day[]={
	"初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
	"十一","十二","十三","十四","十五","十六","十七","十八","十九",
	"二十","廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九",
	"三十"
};

#if START_YEAR!=2001
static int GetDaysToYear(int year)
{
	int LeapYears;
	LeapYears=(year/400)*(24*4+1) + ((year%400)/100)*24 + ((year%100)/4)*1;
	
	if((year%4==0 && year%100!=0) || year%400==0)
		LeapYears--;
	return LeapYears*366 + (year-1-LeapYears)*365;
}
#endif

static int GetSpanDays(int year,int yday)
{
#if START_YEAR==2001
	return (year-101)*365+(year-101)/4+yday;
#else
	return GetDaysToYear(1900+year)-GetDaysToYear(1901)+yday;
#endif
}

void y_im_nl_day(time_t t,char *s)
{
	int year,month,day;
	struct tm *tm;
	int iSpanDays;

	s[0]=0;

	tm=localtime(&t);
	if(tm==NULL)
		return;
/*#if START_YEAR==2001
	iSpanDays=(tm->tm_year-101)*365+(tm->tm_year-101)/4+tm->tm_yday;
#else
	iSpanDays=(tm->tm_year)*365+(tm->tm_year)/4+tm->tm_yday;
#endif*/
	iSpanDays=GetSpanDays(tm->tm_year,tm->tm_yday);
	LunarDay(&year,&month,&day,iSpanDays);

	if(month>=1 && month<=12 && day>=1 && day<=30)
		sprintf(s,"%s%s",nl_mon[month-1],nl_day[day-1]);
}

#if 0
int main(int arc,char *arg[])
{
	char day[256];
	y_im_nl_day(time(NULL),day);
	printf("%s\n",day);
	return 0;
}
#endif
