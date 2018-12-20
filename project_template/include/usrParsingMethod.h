#ifndef __USR_PARSING_METHOD_H__
#define __USR_PARSING_METHOD_H__

#include "esp_common.h"

void *usr_memmem(void *start, unsigned char s_len, void *find, unsigned char f_len);
int usr_memloc(u8 str2[],u8 num_s2,u8 str1[],u8 num_s1);
int usr_strloc(char *str2,char *str1);
float bytesTo_float(u8 dat[4]);
int ftoa(char *str, float num, int n);

#endif