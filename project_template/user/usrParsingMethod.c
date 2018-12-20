#include "usrParsingMethod.h"

#include "esp_common.h"

void ICACHE_FLASH_ATTR
*usr_memmem(void *start, unsigned char s_len, void *find, unsigned char f_len){

	unsigned char len	= 0;
			char *p		= start, 
				 *q		= find;
	
	while((p - (char *)start + f_len) <= s_len){
	
		while(*p ++ == *q ++){
		
			len ++;
			if(len == f_len)return (p - f_len);
		}
		
		q 	= find;
		len = 0;
	}
	
	return NULL;
}

int ICACHE_FLASH_ATTR
usr_memloc(u8 str2[],u8 num_s2,u8 str1[],u8 num_s1){

	int la = num_s1;
	int i, j;
	int lb = num_s2;
	for(i = 0; i < lb; i ++)
	{
		for(j = 0; j < la && i + j < lb && str1[j] == str2[i + j]; j ++);
		if(j == la)return i;
	}
	return -1;
}

int ICACHE_FLASH_ATTR
usr_strloc(char *str2,char *str1){

	int la = strlen(str1);
	int i, j;
	int lb = strlen(str2);
	for(i = 0; i < lb; i ++)
	{
		for(j = 0; j < la && i + j < lb && str1[j] == str2[i + j]; j ++);
		if(j == la)return i;
	}
	return -1;
}

float ICACHE_FLASH_ATTR
bytesTo_float(u8 dat[4]){

	const float decimal_prtCoefficient = 10000.0F;

	float res = 0.0F;
	u16 integer_prt = ((u16)dat[0] << 8) + ((u16)dat[1]);
	u16 decimal_prt = ((u16)dat[2] << 8) + ((u16)dat[3]);

	res = (float)integer_prt + (((float)decimal_prt) / decimal_prtCoefficient);
	
	return res;
}

int ICACHE_FLASH_ATTR
ftoa(char *str, float num, int n){
	
	int 	sumI;
	float	sumF;
	int		sign = 0;
	int		temp;
	int		count = 0;
	
	char	*p;
	char	*pp;
	
	if(str == NULL)return -1;
	p = str;
	
	if(num < 0){
		
		sign = 1;
		num = 0 - num;
	}
	
	sumI = (int)num;
	sumF = num - sumI;
	
	do{
		
		temp = sumI % 10;
		*(str ++) = temp + '0';
		
	}while((sumI = sumI / 10) != 0);
	
	if(sign == 1){
		
		*(str ++) = '-';
	}
	
	pp = str;
	pp --;
	
	while(p < pp){
		
		*p = *p + *pp;
		*pp = *p - *pp;
		*p = *p - *pp;
		p ++;
		pp --;
	}
	
	*(str ++) = '.';
	
	do{
		
		temp = (int)(sumF * 10);
		*(str ++) = temp + '0';
		
		if((++ count) == n)break;
		
		sumF = sumF * 10 - temp;
		
	}while(!(sumF > -0.000001F && sumF < 0.00001F));
	
	
	*str = 0;
	
	return 0;
}	



