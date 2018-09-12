#include "usrInterface_Tips.h"

#include "esp_common.h"

#include "timer_Activing.h"
#include "bsp_Hardware.h"
#include "datsManage.h"

extern bool nwkZigbOnline_IF;
extern bool nwkInternetOnline_IF;

u8 counter_tipsAct = 0;

const func_ledRGB color_Tab[10] = {

	{ 0,  0,  0}, {20, 10, 31}, {31,  0,  0},
	{31,  0, 10}, {8,   0, 16}, {0,  31,  0},
	{16, 31,  0}, {31, 10,  0}, {0,   0, 31},
	{ 0, 10, 31},
};

const func_ledRGB tips_relayUnused = {31, 0,  0};

u8  counter_ifTipsFree = TIPS_SWFREELOOP_TIME; //�û��������ü�ʱֵ ��λ��s

u8 tipsInsert_swLedBKG_ON = 8; //���� �� ����������ֵ
u8 tipsInsert_swLedBKG_OFF = 5; //���� �� ����������ֵ

u8 timeCount_zigNwkOpen = 0; //zigb���翪��ʱ���ʱ����

tips_Status devTips_status = status_Normal; //ϵͳtips״̬
tips_devNwkStatus devNwkTips_status = devNwkStaute_nwkAllAbnormal; //����tips״̬������zigb��wifi��

LOCAL xTaskHandle pxTaskHandle_threadUsrTIPS;

LOCAL void tips_sysStandBy(void);
LOCAL void tips_breath(void);
LOCAL void tips_specified(u8 tips_Type);
LOCAL void thread_tipsGetDark(u8 funSet);
/*---------------------------------------------------------------------------------------------*/

void ICACHE_FLASH_ATTR
ledBKGColorSw_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	tipsInsert_swLedBKG_ON 	= datsRead_Temp->bkColor_swON;
	tipsInsert_swLedBKG_OFF = datsRead_Temp->bkColor_swOFF;

	if(tipsInsert_swLedBKG_ON 	== 0xff)tipsInsert_swLedBKG_ON = 8;
	if(tipsInsert_swLedBKG_OFF 	== 0xff)tipsInsert_swLedBKG_ON = 5;

	if(datsRead_Temp)os_free(datsRead_Temp);
}

/*led_Tips�л�������ģʽ*/
void ICACHE_FLASH_ATTR
tips_statusChangeToNormal(void){

	counter_ifTipsFree = TIPS_SWFREELOOP_TIME;
	devTips_status = status_Normal;
}

/*led_Tips�л���AP����ģʽ*/
void ICACHE_FLASH_ATTR
tips_statusChangeToAPFind(void){

	devTips_status = status_tipsAPFind;
	thread_tipsGetDark(0x0F);
}

/*led_Tips�л���zigb���翪��ģʽ*/
void ICACHE_FLASH_ATTR
tips_statusChangeToZigbNwkOpen(u8 timeopen){

	timeCount_zigNwkOpen = timeopen;
	devTips_status = status_tipsNwkOpen;
	thread_tipsGetDark(0x0F);
}

void ICACHE_FLASH_ATTR
tipsLED_rgbColorSet(u8 tipsRly_Num, u8 gray_R, u8 gray_G, u8 gray_B){

	if(	(devTips_status == status_Normal) 	||
	    (devTips_status == status_Night) ){

		u8 bright_coef = 1; //����ϵ��

		if(devTips_status == status_Night){

			bright_coef = 4;
		}

		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_R = gray_R / bright_coef;
		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_G = gray_G / bright_coef;
		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_B = gray_B / bright_coef;
	}
}

LOCAL void ICACHE_FLASH_ATTR
thread_tipsGetDark(u8 funSet){ //ռλ��ɫֵ

	if((funSet & 0x01) >> 0)usrDats_actuator.func_tipsLedRGB[0].color_R = usrDats_actuator.func_tipsLedRGB[0].color_G = usrDats_actuator.func_tipsLedRGB[0].color_B = 0;
	if((funSet & 0x02) >> 1)usrDats_actuator.func_tipsLedRGB[1].color_R = usrDats_actuator.func_tipsLedRGB[1].color_G = usrDats_actuator.func_tipsLedRGB[1].color_B = 0;
	if((funSet & 0x04) >> 2)usrDats_actuator.func_tipsLedRGB[2].color_R = usrDats_actuator.func_tipsLedRGB[2].color_G = usrDats_actuator.func_tipsLedRGB[2].color_B = 0;
	if((funSet & 0x08) >> 3)usrDats_actuator.func_tipsLedRGB[3].color_R = usrDats_actuator.func_tipsLedRGB[3].color_G = usrDats_actuator.func_tipsLedRGB[3].color_B = 0;
}

LOCAL void ICACHE_FLASH_ATTR
devNwkStatusTips_refresh(void){

	u8 nwkStatus = 0;

	if(nwkZigbOnline_IF)nwkStatus |= 0x01;	//bit0��ʾzigb״̬
	if(nwkInternetOnline_IF)nwkStatus |= 0x02; //bit1��ʾwifi״̬

	if( (devNwkTips_status != devNwkStaute_zigbNwkOpen) || 
		(devNwkTips_status != devNwkStaute_wifiNwkFind) ){

		switch(nwkStatus){

			case 0:{

				devNwkTips_status = devNwkStaute_nwkAllAbnormal;

			}break;

			case 1:{

				devNwkTips_status = devNwkStaute_zigbNwkNormalOnly;

			}break;

			case 2:{

				devNwkTips_status = devNwkStaute_wifiNwkNormalOnly;

			}break;

			case 3:{

				devNwkTips_status = devNwkStaute_nwkAllNormal;

			}break;
		}		
	}
}

LOCAL void ICACHE_FLASH_ATTR
usrTipsProcess_task(void *pvParameters){

	for(;;){

		if(ifNightMode_sw_running_FLAG){ //�豸����ҹ��ģʽ��Tipsģʽǿ���л�
		
			devTips_status = status_Night;
			
		}else{ //�豸��ҹ��ģʽ����������
			
			if(devTips_status == status_Night)tips_statusChangeToNormal(); //��ǰ��Ϊҹ��ģʽ�����л�����ģʽ
		
			if(!counter_ifTipsFree &&  //ָ��ʱ��δ������tips�л�������ģʽ
			   (devTips_status == status_Normal) ){ //����ģʽ�²ſ����л�������ά��
			    
				thread_tipsGetDark(0x0F);
				devTips_status = status_keyFree;
			}
		}

		switch(devTips_status){

			case status_sysStandBy:{


			}break;

			case status_keyFree:{

				tips_sysStandBy();
//				tips_breath();
			
			}break;

			case status_Night:
			case status_Normal:{

				/*zigb״ָ̬ʾ*/
				(DEV_actReserve & 0x01)?((usrDats_actuator.conDatsOut_rly_0)?(tipsLED_rgbColorSet(2, color_Tab[tipsInsert_swLedBKG_ON].color_R, color_Tab[tipsInsert_swLedBKG_ON].color_G, color_Tab[tipsInsert_swLedBKG_ON].color_B)):(tipsLED_rgbColorSet(2, color_Tab[tipsInsert_swLedBKG_OFF].color_R, color_Tab[tipsInsert_swLedBKG_OFF].color_G, color_Tab[tipsInsert_swLedBKG_OFF].color_B))):(tipsLED_rgbColorSet(2, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));				
				(DEV_actReserve & 0x02)?((usrDats_actuator.conDatsOut_rly_1)?(tipsLED_rgbColorSet(1, color_Tab[tipsInsert_swLedBKG_ON].color_R, color_Tab[tipsInsert_swLedBKG_ON].color_G, color_Tab[tipsInsert_swLedBKG_ON].color_B)):(tipsLED_rgbColorSet(1, color_Tab[tipsInsert_swLedBKG_OFF].color_R, color_Tab[tipsInsert_swLedBKG_OFF].color_G, color_Tab[tipsInsert_swLedBKG_OFF].color_B))):(tipsLED_rgbColorSet(1, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));				
				(DEV_actReserve & 0x04)?((usrDats_actuator.conDatsOut_rly_2)?(tipsLED_rgbColorSet(0, color_Tab[tipsInsert_swLedBKG_ON].color_R, color_Tab[tipsInsert_swLedBKG_ON].color_G, color_Tab[tipsInsert_swLedBKG_ON].color_B)):(tipsLED_rgbColorSet(0, color_Tab[tipsInsert_swLedBKG_OFF].color_R, color_Tab[tipsInsert_swLedBKG_OFF].color_G, color_Tab[tipsInsert_swLedBKG_OFF].color_B))):(tipsLED_rgbColorSet(0, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));

				/*zigb����״ָ̬ʾ*/
				{
					static u16 tips_Counter = 0;
					const u16 tips_Period = 200;

					static bool tips_Type = 0;

					devNwkStatusTips_refresh(); //tips״̬ˢ��
					
					if(tips_Counter < tips_Period)tips_Counter ++;
					else{

						tips_Type = !tips_Type;
						tips_Counter = 0;

						switch(devNwkTips_status){
						
							case devNwkStaute_nwkAllNormal:{ //����
						
								(tips_Type)?(tipsLED_rgbColorSet(3, 0, 31, 0)):(tipsLED_rgbColorSet(3, 0, 31, 0));
							
							}break;
						
							case devNwkStaute_zigbNwkNormalOnly:{ //����

								(tips_Type)?(tipsLED_rgbColorSet(3, 0, 0, 31)):(tipsLED_rgbColorSet(3, 0, 0, 31));
						
							}break;
						
							case devNwkStaute_wifiNwkNormalOnly:{ //����

								(tips_Type)?(tipsLED_rgbColorSet(3, 15, 10, 31)):(tipsLED_rgbColorSet(3, 15, 10, 31));
						
							}break;
						
							case devNwkStaute_nwkAllAbnormal:{ //����

								(tips_Type)?(tipsLED_rgbColorSet(3, 31, 0, 0)):(tipsLED_rgbColorSet(3, 31, 0, 0));
						
							}break;
						
							case devNwkStaute_zigbNwkOpen:{ //����

								(tips_Type)?(tipsLED_rgbColorSet(3, 0, 0, 31)):(tipsLED_rgbColorSet(3, 0, 0, 0));
						
							}break;
							
							case devNwkStaute_wifiNwkFind:{ //����

								(tips_Type)?(tipsLED_rgbColorSet(3, 15, 10, 31)):(tipsLED_rgbColorSet(3, 0, 0, 0));
						
							}break;
						
							default:break;
						}
					}
				}
			}break;

			case status_tipsNwkOpen:{

				if(timeCount_zigNwkOpen)tips_specified(0);
				else{

					tips_statusChangeToNormal();
				}

			}break;

			case status_tipsAPFind:{

				tips_specified(1);

			}break;

			default:break;
		}

		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

LOCAL void ICACHE_FLASH_ATTR
tips_breath(void){

	static u8 	localTips_Count = RLY_TIPS_PERIODUNITS - 1; //�β��������л� ��ʼֵ
	static bool count_FLG = 1;
	static u8 	tipsStep = 0;

	const u8 speed = 3;
	const u8 step_period = 3;
	
//	if(!localTips_Count && !count_FLG)(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++); //�β������ �л���һ������
	if(!localTips_Count)count_FLG = 1;
	else 
	if(localTips_Count >= (RLY_TIPS_PERIODUNITS - 1)){
		
		count_FLG = 0;
		
		localTips_Count = RLY_TIPS_PERIODUNITS - 2; //�������뵱ǰ״̬
		(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++);  //�β������� �л�����һ������
	}
	
	if(!counter_tipsAct){
	
		(!count_FLG)?(counter_tipsAct = speed * (localTips_Count --)):(counter_tipsAct = speed * (localTips_Count ++)); //���¶���������ʱ��
	}	
	
	switch(tipsStep){
	
		case 0:{
		
			usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - localTips_Count;
			
		}break;
		
		case 2:{
		
			usrDats_actuator.func_tipsLedRGB[2].color_G = 31 - localTips_Count;
			
		}break;
		
		default:break;
	}
}

LOCAL void ICACHE_FLASH_ATTR
tips_fadeOut(void){

	static u8 	localTips_Count = 0; //�β�������л� ��ʼֵ
	static bool count_FLG = 1;
	static u8 	tipsStep = 0;
	static u8 	pwmType_A = 0,
				pwmType_B = 0,
				pwmType_C = 0,
				pwmType_D = 0;
	
	const u8 speed = 1;
	const u8 step_period = 1;
	
	if(!localTips_Count && !count_FLG)(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++); //�β������ �л���һ������
	if(!localTips_Count)count_FLG = 1;
	else 
	if(localTips_Count > (RLY_TIPS_PERIODUNITS * 4)){
	
		count_FLG = 0;
		
//		localTips_Count = RLY_TIPS_PERIODUNITS - 2; //�������뵱ǰ״̬
//		(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++);  //�β������� �л�����һ������
	}
	
	if(!counter_tipsAct){
	
		(!count_FLG)?(counter_tipsAct = speed * ((localTips_Count --) % RLY_TIPS_PERIODUNITS)):(counter_tipsAct = speed * ((localTips_Count ++) % RLY_TIPS_PERIODUNITS));  //���¶���������ʱ��
		
		if(localTips_Count >= 00 && localTips_Count< 32)pwmType_A = localTips_Count - 0;
		if(localTips_Count >= 32 && localTips_Count< 64)pwmType_B = localTips_Count - 32;
		if(localTips_Count >= 64 && localTips_Count< 96)pwmType_C = localTips_Count - 64;
		if(localTips_Count >= 96 && localTips_Count< 128)pwmType_D = localTips_Count - 96;
	}
	
	switch(tipsStep){
	
		case 0:{
			
			if(count_FLG){ 
				
				usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_A;
				usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_B;
				usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_C;
				usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_D;
				
				usrDats_actuator.func_tipsLedRGB[3].color_R = 31 - pwmType_A;
				usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - pwmType_B;
				usrDats_actuator.func_tipsLedRGB[1].color_R = 31 - pwmType_C;
				usrDats_actuator.func_tipsLedRGB[2].color_R = 31 - pwmType_D;
				
			}else{ 
				
				usrDats_actuator.func_tipsLedRGB[3].color_R = 31 - pwmType_D;
				usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - pwmType_C;
				usrDats_actuator.func_tipsLedRGB[1].color_R = 31 - pwmType_B;
				usrDats_actuator.func_tipsLedRGB[2].color_R = 31 - pwmType_A;
				
				usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_D;
				usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_C;
				usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_B;
				usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_A;
			}
			
		}break;
			
		case 1:{
			
			if(count_FLG){ 
				
				usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_A / 4;
				usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_B / 4;
				usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_C / 4;
				usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_D / 4;
				
				usrDats_actuator.func_tipsLedRGB[3].color_R = 31 - pwmType_A;
				usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - pwmType_B;
				usrDats_actuator.func_tipsLedRGB[1].color_R = 31 - pwmType_C;
				usrDats_actuator.func_tipsLedRGB[2].color_R = 31 - pwmType_D;
				
			}else{ 
				
				usrDats_actuator.func_tipsLedRGB[3].color_R = 31 - pwmType_D;
				usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - pwmType_C;
				usrDats_actuator.func_tipsLedRGB[1].color_R = 31 - pwmType_B;
				usrDats_actuator.func_tipsLedRGB[2].color_R = 31 - pwmType_A;
				
				usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_D / 4;
				usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_C / 4;
				usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_B / 4;
				usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_A / 4;
			}
			
		}break;
			
		default:break;
	}
}


LOCAL void ICACHE_FLASH_ATTR
tips_sysStandBy(void){

//	u8 code timUnit_period = 0;
//	static u8 timUnit_count = 0;
	
	static u8 localTips_Count = 0;
	const  u8 localTips_Period = 128;
	static u8 tipsStep = 0;
	static u8 pwmType_A = 0,
		      pwmType_B = 0,
			  pwmType_C = 0,
			  pwmType_D = 0;
	
	if(!counter_tipsAct){
		
		counter_tipsAct = 3; //�����ڸ���ʱ��
	
		if(localTips_Count > localTips_Period){
		
			localTips_Count = 0;
			(tipsStep > 7)?(tipsStep = 0):(tipsStep ++);
			pwmType_A = pwmType_B = pwmType_C = pwmType_D = 0;
		}
		else{
		
			localTips_Count ++;
			
			if(localTips_Count >= 00 && localTips_Count< 32)pwmType_A = localTips_Count - 0;
			if(localTips_Count >= 32 && localTips_Count< 64)pwmType_B = localTips_Count - 32;
			if(localTips_Count >= 64 && localTips_Count< 96)pwmType_C = localTips_Count - 64;
			if(localTips_Count >= 96 && localTips_Count< 128)pwmType_D = localTips_Count - 96;
		}
	}
	
	switch(tipsStep){
	
		case 0:{ /*��*///����
			
			usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_A / 5;
			usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_B / 5;
			usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_C / 5;
			usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_D / 5;
			
		}break;
		
		case 1:{ /*��*///����
			
			usrDats_actuator.func_tipsLedRGB[3].color_R = pwmType_A;
			usrDats_actuator.func_tipsLedRGB[0].color_R = pwmType_B;
			usrDats_actuator.func_tipsLedRGB[1].color_R = pwmType_C;
			usrDats_actuator.func_tipsLedRGB[2].color_R = pwmType_D;
			
		}break;
		
		case 2:{ /*��*///����
			
			usrDats_actuator.func_tipsLedRGB[3].color_G = 6 - pwmType_A / 5;
			usrDats_actuator.func_tipsLedRGB[0].color_G = 6 - pwmType_B / 5;
			usrDats_actuator.func_tipsLedRGB[1].color_G = 6 - pwmType_C / 5;
			usrDats_actuator.func_tipsLedRGB[2].color_G = 6 - pwmType_D / 5;
			
		}break;
		
		case 3:{ /*��*///����
			
			usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_A / 2;
			usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_B / 2;
			usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_C / 2;
			usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_D / 2;
			
		}break;
		
		case 4:{ /*��*///����
		
			usrDats_actuator.func_tipsLedRGB[3].color_R = 31 - pwmType_A;
			usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - pwmType_B;
			usrDats_actuator.func_tipsLedRGB[1].color_R = 31 - pwmType_C;
			usrDats_actuator.func_tipsLedRGB[2].color_R = 31 - pwmType_D;
			
		}break;
		
		case 5:{ /*��*///�������
		
			usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_A / 3;
			usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_B / 3;
			usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_C / 3;
			usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_D / 3;
			
			usrDats_actuator.func_tipsLedRGB[3].color_R = pwmType_A / 2;
			usrDats_actuator.func_tipsLedRGB[0].color_R = pwmType_B / 2;
			usrDats_actuator.func_tipsLedRGB[1].color_R = pwmType_C / 2;
			usrDats_actuator.func_tipsLedRGB[2].color_R = pwmType_D / 2;
			
		}break;
		
		case 6:{ /*dark*///ȫ��
		
			usrDats_actuator.func_tipsLedRGB[3].color_B = 15 - pwmType_A / 2;
			usrDats_actuator.func_tipsLedRGB[0].color_B = 15 - pwmType_B / 2;
			usrDats_actuator.func_tipsLedRGB[1].color_B = 15 - pwmType_C / 2;
			usrDats_actuator.func_tipsLedRGB[2].color_B = 15 - pwmType_D / 2;
			
			usrDats_actuator.func_tipsLedRGB[3].color_G = 10 - pwmType_A / 3;
			usrDats_actuator.func_tipsLedRGB[0].color_G = 10 - pwmType_B / 3;
			usrDats_actuator.func_tipsLedRGB[1].color_G = 10 - pwmType_C / 3;
			usrDats_actuator.func_tipsLedRGB[2].color_G = 10 - pwmType_D / 3;
			
			usrDats_actuator.func_tipsLedRGB[3].color_R = 15 - pwmType_A / 2;
			usrDats_actuator.func_tipsLedRGB[0].color_R = 15 - pwmType_B / 2;
			usrDats_actuator.func_tipsLedRGB[1].color_R = 15 - pwmType_C / 2;
			usrDats_actuator.func_tipsLedRGB[2].color_R = 15 - pwmType_D / 2;
			
		}break;
		
		default:{}break;
	}
}

LOCAL void ICACHE_FLASH_ATTR
tips_specified(u8 tips_Type){ //tips���

	static u8 	localTips_Count = 0; //��������л���ʼֵ
	static bool count_FLG = 1;
	static u8 	tipsStep = 0;
	static u8 	pwmType_A = 0,
				pwmType_B = 0,
				pwmType_C = 0,
				pwmType_D = 0;
	
	const u8 speed_Mol = 5,
	   		 speed_Den = 2;
	const u8 step_period = 0;
	
	if(!localTips_Count && !count_FLG){
	
		if(tipsStep < step_period)tipsStep ++; //�β�����ڣ��л�����һ������
		else{
		
			tipsStep = 0;
			
			pwmType_A = pwmType_B = pwmType_C = pwmType_D = 0;
		}
	}
	if(!localTips_Count)count_FLG = 1;
	else 
	if(localTips_Count > 80){
	
		count_FLG = 0;
		
//		localTips_Count = COLORGRAY_MAX - 2; //�������뵱ǰ״̬
//		(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++);  //�β������ڣ��л�����һ����
	}
	
	if(!counter_tipsAct){
	
		(!count_FLG)?(counter_tipsAct = ((localTips_Count --) % RLY_TIPS_PERIODUNITS) / speed_Mol * speed_Den):(counter_tipsAct = ((localTips_Count ++) % RLY_TIPS_PERIODUNITS) / speed_Mol * speed_Den); //���µ�����ʱ��
		
		if(localTips_Count >= 00 && localTips_Count < 32)pwmType_A = localTips_Count - 0; 
		if(localTips_Count >= 16 && localTips_Count < 48)pwmType_B = localTips_Count - 16;
		if(localTips_Count >= 32 && localTips_Count < 64)pwmType_C = localTips_Count - 32;
		if(localTips_Count >= 48 && localTips_Count < 80)pwmType_D = localTips_Count - 48;
	}
	
	switch(tips_Type){
	
		case 0:{
		
			switch(tipsStep){
			
				case 0:{
					
					if(count_FLG){ 
						
						if(localTips_Count < 46){
						
							if(localTips_Count % 15 > 10)usrDats_actuator.func_tipsLedRGB[3].color_B = 31;else usrDats_actuator.func_tipsLedRGB[3].color_B = 0;
						
						}else{
						
							if(localTips_Count > 45)usrDats_actuator.func_tipsLedRGB[2].color_B = 31;
							if(localTips_Count > 50)usrDats_actuator.func_tipsLedRGB[1].color_B = 31;
							if(localTips_Count > 60)usrDats_actuator.func_tipsLedRGB[0].color_B = 31;
							if(localTips_Count > 70)usrDats_actuator.func_tipsLedRGB[3].color_B = 31;
						}
						
					}else{ 
						
						usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_D;
						usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_C;
						usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_B;
						usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_A;
					}
					
				}break;
					
				default:break;
			}
			
		}break;
		
		case 1:{
		
			switch(tipsStep){
			
				case 0:{
					
					if(count_FLG){ 
						
						if(localTips_Count < 46){
						
							if(localTips_Count % 15 > 10){

								usrDats_actuator.func_tipsLedRGB[3].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[3].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[3].color_B = 31;
							}
							else{
								
								usrDats_actuator.func_tipsLedRGB[3].color_R = 0;
								usrDats_actuator.func_tipsLedRGB[3].color_G = 0;
								usrDats_actuator.func_tipsLedRGB[3].color_B = 0;						
							}
						
						}else{
						
							if(localTips_Count > 45){
								
								usrDats_actuator.func_tipsLedRGB[3].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[3].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[3].color_B = 31;
							}
							if(localTips_Count > 50){
								
								usrDats_actuator.func_tipsLedRGB[0].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[0].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[0].color_B = 31;
							}
							if(localTips_Count > 60){
								
								usrDats_actuator.func_tipsLedRGB[1].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[1].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[1].color_B = 31;
							}
							if(localTips_Count > 70){
								
								usrDats_actuator.func_tipsLedRGB[2].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[2].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[2].color_B = 31;
							}
						}
						
					}
					else{ 
						
						usrDats_actuator.func_tipsLedRGB[3].color_R = pwmType_D / 2;
						usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_D / 3;
						usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_D;

						usrDats_actuator.func_tipsLedRGB[2].color_R = pwmType_A / 2;
						usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_A / 3;
						usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_A;

						usrDats_actuator.func_tipsLedRGB[1].color_R = pwmType_B / 2;
						usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_B / 3;
						usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_B;

						usrDats_actuator.func_tipsLedRGB[0].color_R = pwmType_C / 2;
						usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_C / 3;
						usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_C;
					}
					
				}break;
					
				default:break;
			}
			
		}break;
		
		default:break;
	}
}


void ICACHE_FLASH_ATTR
usrTips_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(usrTipsProcess_task, "Process_Tips", 512, (void *)NULL, 2, &pxTaskHandle_threadUsrTIPS);

	os_printf("\npxTaskHandle_threadUsrTips is %d\n", pxTaskHandle_threadUsrTIPS);
}

