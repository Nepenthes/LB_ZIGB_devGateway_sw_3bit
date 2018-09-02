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


