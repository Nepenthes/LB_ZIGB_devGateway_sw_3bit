#include "usrInterface_Tips.h"

#include "esp_common.h"

#include "bsp_Hardware.h"
#include "datsManage.h"

u8 counter_tipsAct = 0;

const func_ledRGB color_Tab[10] = {

	{ 0,  0,  0}, {20, 10, 31}, {31,  0,  0},
	{31,  0, 10}, {8,   0, 16}, {0,  31,  0},
	{16, 31,  0}, {31, 10,  0}, {0,   0, 31},
	{ 0, 10, 31},
};

const func_ledRGB tips_relayUnused = {31, 0,  0};

u8  counter_ifTipsFree = TIPS_SWFREELOOP_TIME; //用户操作闲置计时值 单位：s

u8 tipsInsert_swLedBKG_ON = 8; //开关 开 背景灯索引值
u8 tipsInsert_swLedBKG_OFF = 5; //开关 关 背景灯索引值

tips_Status devTips_status = status_Null;

LOCAL xTaskHandle pxTaskHandle_threadUsrTIPS;

LOCAL void tips_sysStandBy(void);
LOCAL void tips_breath(void);
/*---------------------------------------------------------------------------------------------*/

void ICACHE_FLASH_ATTR
ledBKGColorSw_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	tipsInsert_swLedBKG_ON 	= datsRead_Temp->bkColor_swON;
	tipsInsert_swLedBKG_OFF = datsRead_Temp->bkColor_swOFF;

	os_free(datsRead_Temp);
}

/*led_Tips切换至正常模式*/
void ICACHE_FLASH_ATTR
tips_statusChangeToNormal(void){

	counter_ifTipsFree = TIPS_SWFREELOOP_TIME;
	devTips_status = status_Normal;
}

void ICACHE_FLASH_ATTR
tipsLED_rgbColorSet(u8 tipsRly_Num, u8 color_R, u8 color_G, u8 color_B){

	if(devTips_status == status_Normal){

		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_R = color_R;
		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_G = color_G;
		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_B = color_B;
	}
}

LOCAL void ICACHE_FLASH_ATTR
usrTipsProcess_task(void *pvParameters){

	for(;;){

		if(!counter_ifTipsFree && (devTips_status != status_keyFree)){ //既定时间内无用户操作，tips切换至闲置模式
			
			tipsLED_rgbColorSet(0, 0, 0, 0);
			tipsLED_rgbColorSet(1, 0, 0, 0);
			tipsLED_rgbColorSet(2, 0, 0, 0);
			tipsLED_rgbColorSet(3, 0, 0, 0);
			
			devTips_status = status_keyFree;
		}

		switch(devTips_status){

			case status_sysStandBy:{


			}break;

			case status_keyFree:{

				tips_sysStandBy();
//				tips_breath();
			
			}break;

			case status_Normal:{

				(DEV_actReserve & 0x01)?((usrDats_actuator.conDatsOut_rly_0)?(tipsLED_rgbColorSet(2, color_Tab[tipsInsert_swLedBKG_ON].color_R, color_Tab[tipsInsert_swLedBKG_ON].color_G, color_Tab[tipsInsert_swLedBKG_ON].color_B)):(tipsLED_rgbColorSet(2, color_Tab[tipsInsert_swLedBKG_OFF].color_R, color_Tab[tipsInsert_swLedBKG_OFF].color_G, color_Tab[tipsInsert_swLedBKG_OFF].color_B))):(tipsLED_rgbColorSet(2, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));				
				(DEV_actReserve & 0x02)?((usrDats_actuator.conDatsOut_rly_1)?(tipsLED_rgbColorSet(1, color_Tab[tipsInsert_swLedBKG_ON].color_R, color_Tab[tipsInsert_swLedBKG_ON].color_G, color_Tab[tipsInsert_swLedBKG_ON].color_B)):(tipsLED_rgbColorSet(1, color_Tab[tipsInsert_swLedBKG_OFF].color_R, color_Tab[tipsInsert_swLedBKG_OFF].color_G, color_Tab[tipsInsert_swLedBKG_OFF].color_B))):(tipsLED_rgbColorSet(1, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));				
				(DEV_actReserve & 0x04)?((usrDats_actuator.conDatsOut_rly_2)?(tipsLED_rgbColorSet(0, color_Tab[tipsInsert_swLedBKG_ON].color_R, color_Tab[tipsInsert_swLedBKG_ON].color_G, color_Tab[tipsInsert_swLedBKG_ON].color_B)):(tipsLED_rgbColorSet(0, color_Tab[tipsInsert_swLedBKG_OFF].color_R, color_Tab[tipsInsert_swLedBKG_OFF].color_G, color_Tab[tipsInsert_swLedBKG_OFF].color_B))):(tipsLED_rgbColorSet(0, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));
				
			}break;

			default:break;
		}

		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

LOCAL void ICACHE_FLASH_ATTR
tips_breath(void){

	static u8 	localTips_Count = RLY_TIPS_PERIODUNITS - 1; //次步骤中期切换 初始值
	static bool count_FLG = 1;
	static u8 	tipsStep = 0;

	const u8 speed = 3;
	const u8 step_period = 3;
	
//	if(!localTips_Count && !count_FLG)(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++); //次步骤初期 切换下一主步骤
	if(!localTips_Count)count_FLG = 1;
	else 
	if(localTips_Count >= (RLY_TIPS_PERIODUNITS - 1)){
		
		count_FLG = 0;
		
		localTips_Count = RLY_TIPS_PERIODUNITS - 2; //快速脱离当前状态
		(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++);  //次步骤中期 切换到下一主步骤
	}
	
	if(!counter_tipsAct){
	
		(!count_FLG)?(counter_tipsAct = speed * (localTips_Count --)):(counter_tipsAct = speed * (localTips_Count ++)); //更新动作但周期时间
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

	static u8 	localTips_Count = 0; //次步骤初期切换 初始值
	static bool count_FLG = 1;
	static u8 	tipsStep = 0;
	static u8 	pwmType_A = 0,
				pwmType_B = 0,
				pwmType_C = 0,
				pwmType_D = 0;
	
	const u8 speed = 1;
	const u8 step_period = 1;
	
	if(!localTips_Count && !count_FLG)(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++); //次步骤初期 切换下一主步骤
	if(!localTips_Count)count_FLG = 1;
	else 
	if(localTips_Count > (RLY_TIPS_PERIODUNITS * 4)){
	
		count_FLG = 0;
		
//		localTips_Count = RLY_TIPS_PERIODUNITS - 2; //快速脱离当前状态
//		(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++);  //次步骤中期 切换到下一主步骤
	}
	
	if(!counter_tipsAct){
	
		(!count_FLG)?(counter_tipsAct = speed * ((localTips_Count --) % RLY_TIPS_PERIODUNITS)):(counter_tipsAct = speed * ((localTips_Count ++) % RLY_TIPS_PERIODUNITS));  //更新动作但周期时间
		
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
		
		counter_tipsAct = 3; //单周期更新时间
	
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
	
		case 0:{ /*绿*///绿起
			
			usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_A / 5;
			usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_B / 5;
			usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_C / 5;
			usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_D / 5;
			
		}break;
		
		case 1:{ /*黄*///红起
			
			usrDats_actuator.func_tipsLedRGB[3].color_R = pwmType_A;
			usrDats_actuator.func_tipsLedRGB[0].color_R = pwmType_B;
			usrDats_actuator.func_tipsLedRGB[1].color_R = pwmType_C;
			usrDats_actuator.func_tipsLedRGB[2].color_R = pwmType_D;
			
		}break;
		
		case 2:{ /*红*///绿消
			
			usrDats_actuator.func_tipsLedRGB[3].color_G = 6 - pwmType_A / 5;
			usrDats_actuator.func_tipsLedRGB[0].color_G = 6 - pwmType_B / 5;
			usrDats_actuator.func_tipsLedRGB[1].color_G = 6 - pwmType_C / 5;
			usrDats_actuator.func_tipsLedRGB[2].color_G = 6 - pwmType_D / 5;
			
		}break;
		
		case 3:{ /*粉*///蓝起
			
			usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_A / 2;
			usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_B / 2;
			usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_C / 2;
			usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_D / 2;
			
		}break;
		
		case 4:{ /*蓝*///红消
		
			usrDats_actuator.func_tipsLedRGB[3].color_R = 31 - pwmType_A;
			usrDats_actuator.func_tipsLedRGB[0].color_R = 31 - pwmType_B;
			usrDats_actuator.func_tipsLedRGB[1].color_R = 31 - pwmType_C;
			usrDats_actuator.func_tipsLedRGB[2].color_R = 31 - pwmType_D;
			
		}break;
		
		case 5:{ /*白*///绿起红起
		
			usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_A / 3;
			usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_B / 3;
			usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_C / 3;
			usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_D / 3;
			
			usrDats_actuator.func_tipsLedRGB[3].color_R = pwmType_A / 2;
			usrDats_actuator.func_tipsLedRGB[0].color_R = pwmType_B / 2;
			usrDats_actuator.func_tipsLedRGB[1].color_R = pwmType_C / 2;
			usrDats_actuator.func_tipsLedRGB[2].color_R = pwmType_D / 2;
			
		}break;
		
		case 6:{ /*dark*///全消
		
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

void ICACHE_FLASH_ATTR
usrTips_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(usrTipsProcess_task, "Process_Tips", 512, (void *)NULL, 5, &pxTaskHandle_threadUsrTIPS);

	os_printf("\npxTaskHandle_threadUsrTips is %d\n", pxTaskHandle_threadUsrTIPS);
}

