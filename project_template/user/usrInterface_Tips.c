#include "usrInterface_Tips.h"

#include "esp_common.h"

#include "timer_Activing.h"
#include "bsp_Hardware.h"
#include "datsManage.h"
#include "hwPeripherial_Actuator.h"
#include "usrInterface_keylbutton.h"

extern bool nwkZigbOnline_IF;
extern bool nwkInternetOnline_IF;

u16 counter_tipsAct = 0;

const func_ledRGB color_Tab[TIPS_SWBKCOLOR_TYPENUM] = {

	{ 0,  0,  0}, {20, 10, 31}, {31,  0,  0},
	{31,  0, 10}, {8,   0, 16}, {0,  31,  0},
	{16, 31,  0}, {31, 10,  0}, {0,   0, 31},
	{ 0, 10, 31},
};

const func_ledRGB tips_relayUnused = {0, 0,  0};

u8  counter_ifTipsFree = TIPS_SWFREELOOP_TIME; //用户操作闲置计时值 单位：s

bkLightColorInsert_paramAttr devBackgroundLight_param = {0}; //背光灯索引参数缓存

u8 timeCount_zigNwkOpen = 0; //zigb网络开放时间计时计数

bool ifHorsingLight_running_FLAG = true; //跑马灯运行标志，默认开

tips_Status devTips_status 			= status_Normal; //系统tips状态
tips_devNwkStatus devNwkTips_status = devNwkStaute_nwkAllAbnormal; //网络tips状态（包含zigb和wifi）

sound_Attr devTips_beep  	= {0, 0, 0}; //beeps触发属性
enum_beeps dev_statusBeeps	= beepsMode_null; //蜂鸣器工作状态指示

LOCAL xTaskHandle pxTaskHandle_threadUsrTIPS;

LOCAL void tips_sysButtonReales(void);
LOCAL void tips_breath(void);
LOCAL void tips_specified(u8 tips_Type);
LOCAL void tips_sysTouchReset(void);
LOCAL void tips_sysStandBy(void);
LOCAL void thread_tipsGetDark(u8 funSet);
/*---------------------------------------------------------------------------------------------*/

void 
ledBKGColorSw_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	switch(SWITCH_TYPE){
	
		case SWITCH_TYPE_CURTAIN:{

			devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press =\
				datsRead_Temp->param_bkLightColorInsert.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press;
			devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce =\
				datsRead_Temp->param_bkLightColorInsert.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce;

			if(devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press > (TIPS_SWBKCOLOR_TYPENUM - 1))devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press = 8; //蓝
			if(devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce > (TIPS_SWBKCOLOR_TYPENUM - 1))devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce = 5; //绿
		
		}break;
		
		case SWITCH_TYPE_SWBIT1:
		case SWITCH_TYPE_SWBIT2:
		case SWITCH_TYPE_SWBIT3:{

			devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open =\
				datsRead_Temp->param_bkLightColorInsert.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open;
			devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close =\
				datsRead_Temp->param_bkLightColorInsert.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close;
				
			if(devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open > (TIPS_SWBKCOLOR_TYPENUM - 1))devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open = 8; //蓝
			if(devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close > (TIPS_SWBKCOLOR_TYPENUM - 1))devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close = 5; //绿
			
		}break;
		
		default:{}break;
	}

	if(datsRead_Temp)os_free(datsRead_Temp);
}

/*led_Tips切换至正常模式*/
void 
tips_statusChangeToNormal(void){

	counter_ifTipsFree = TIPS_SWFREELOOP_TIME;
	devTips_status = status_Normal;
}

/*led_Tips切换至AP配置模式*/
void 
tips_statusChangeToAPFind(void){

	devTips_status = status_tipsAPFind;
	devNwkTips_status = devNwkStaute_wifiNwkFind;
	thread_tipsGetDark(0x0F);
}

/*led_Tips切换至zigb网络开放模式*/
void 
tips_statusChangeToZigbNwkOpen(u8 timeopen){

	timeCount_zigNwkOpen = timeopen;
	devTips_status = status_tipsNwkOpen;
	devNwkTips_status = devNwkStaute_zigbNwkOpen;
	thread_tipsGetDark(0x0F);
}

/*led_Tips切换至触摸IC复位模式*/
void 
tips_statusChangeToTouchReset(void){

	devTips_status = status_touchReset;
	thread_tipsGetDark(0x0F);
}

/*led_Tips切换至触摸IC复位模式*/
void 
tips_statusChangeToFactoryRecover(void){

	devTips_status = status_sysStandBy;
	thread_tipsGetDark(0x0F);
}

/*非阻塞触发tips_tips*/
void beeps_usrActive(u8 tons, u8 time, u8 loop){

	if(!ifNightMode_sw_running_FLAG){ //非夜间模式，声音有效

		devTips_beep.tips_Period = tons;
		devTips_beep.tips_time = time;
		devTips_beep.tips_loop = loop;
		dev_statusBeeps = beepsMode_standBy;
	}
}

/*led_Tips颜色设置*/
void 
tipsLED_rgbColorSet(u8 tipsRly_Num, u8 gray_R, u8 gray_G, u8 gray_B){

	if(	(devTips_status == status_Normal) 	||
	    (devTips_status == status_Night) ){

		u8 bright_coef = 1; //亮度系数

		if(devTips_status == status_Night){

			bright_coef = 4;
		}

		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_R = gray_R / bright_coef;
		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_G = gray_G / bright_coef;
		usrDats_actuator.func_tipsLedRGB[tipsRly_Num].color_B = gray_B / bright_coef;
	}
}

LOCAL void 
thread_tipsGetDark(u8 funSet){ //占位清色值

	if((funSet & 0x01) >> 0)usrDats_actuator.func_tipsLedRGB[0].color_R = usrDats_actuator.func_tipsLedRGB[0].color_G = usrDats_actuator.func_tipsLedRGB[0].color_B = 0;
	if((funSet & 0x02) >> 1)usrDats_actuator.func_tipsLedRGB[1].color_R = usrDats_actuator.func_tipsLedRGB[1].color_G = usrDats_actuator.func_tipsLedRGB[1].color_B = 0;
	if((funSet & 0x04) >> 2)usrDats_actuator.func_tipsLedRGB[2].color_R = usrDats_actuator.func_tipsLedRGB[2].color_G = usrDats_actuator.func_tipsLedRGB[2].color_B = 0;
	if((funSet & 0x08) >> 3)usrDats_actuator.func_tipsLedRGB[3].color_R = usrDats_actuator.func_tipsLedRGB[3].color_G = usrDats_actuator.func_tipsLedRGB[3].color_B = 0;
}

LOCAL void 
devNwkStatusTips_refresh(void){

	u8 nwkStatus = 0;

	if(nwkZigbOnline_IF)nwkStatus |= 0x01;	//bit0表示zigb状态
	if(nwkInternetOnline_IF)nwkStatus |= 0x02; //bit1表示wifi状态

	if( (devNwkTips_status != devNwkStaute_zigbNwkOpen) &&
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

LOCAL void 
usrTipsProcess_task(void *pvParameters){

	for(;;){

		if(ifNightMode_sw_running_FLAG){ //设备处于夜间模式，Tips模式强制切换
		
			if(devTips_status == status_Normal || //其它系统级tips不受夜间模式影响
			   devTips_status == status_keyFree){

				devTips_status = status_Night;
			}
			
		}else{ //设备非夜间模式，可以运行
			
			if(devTips_status == status_Night)tips_statusChangeToNormal(); //当前若为夜间模式，则切回正常模式
		
			if(!counter_ifTipsFree &&  //指定时间未操作，tips切换至空闲模式
			   (devTips_status == status_Normal) && //正常模式下才可以切换，否则维持
			   ifHorsingLight_running_FLAG ){ //运行标志使能
			    
				thread_tipsGetDark(0x0F);
				devTips_status = status_keyFree;
			}
		}

		switch(devTips_status){

			case status_sysStandBy:{

				tips_sysStandBy();

			}break;

			case status_keyFree:{

				tips_sysButtonReales();
//				tips_breath();
			
			}break;

			case status_Night:
			case status_Normal:{

				u8 relayStatus_tipsTemp = 0;

				if(!timeCount_zigNwkOpen && !timeCounter_smartConfig_start)devNwkTips_status = devNwkStaute_nwkAllNormal;
				
				if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1){
				
					relayStatus_tipsTemp |= status_actuatorRelay & 0x01; //第一位显存装填
					relayStatus_tipsTemp = relayStatus_tipsTemp << 1; //第一位显存处理
	
				}else
				if(SWITCH_TYPE == SWITCH_TYPE_SWBIT2){

					relayStatus_tipsTemp |= status_actuatorRelay & 0x02; //第二位显存填装
					relayStatus_tipsTemp = relayStatus_tipsTemp << 1; //第二位显存处理
					relayStatus_tipsTemp |= status_actuatorRelay & 0x01; //第一位显存填装

				}else
				if(SWITCH_TYPE == SWITCH_TYPE_SWBIT3)
				{

					relayStatus_tipsTemp = status_actuatorRelay; //直接装填
					
				}else
				if(SWITCH_TYPE == SWITCH_TYPE_CURTAIN)
				{

					relayStatus_tipsTemp = status_actuatorRelay; //直接装填
				}

				/*继电器状态指示*/
				switch(SWITCH_TYPE){

					case SWITCH_TYPE_CURTAIN:{

						switch(relayStatus_tipsTemp){ //非占位指示

							case 0x01:{

								(tipsLED_rgbColorSet(2, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_B));
								(tipsLED_rgbColorSet(1, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_B));
								(tipsLED_rgbColorSet(0, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_B));
							}break;

							case 0x04:{
							
								(tipsLED_rgbColorSet(2, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_B));
								(tipsLED_rgbColorSet(1, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_B));
								(tipsLED_rgbColorSet(0, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_B));
							}break;

							default:{

								(tipsLED_rgbColorSet(2, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_B));
								(tipsLED_rgbColorSet(1, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_press].color_B));
								(tipsLED_rgbColorSet(0, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.cuertain_BKlight_Param.cuertain_BKlightColorInsert_bounce].color_B));

							}break;
						}

					}break;

					default:{

						(DEV_actReserve & 0x01)?\
							((relayStatus_tipsTemp & 0x01)?\
								(tipsLED_rgbColorSet(2, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_B)):\
								(tipsLED_rgbColorSet(2, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_B))):\
							(tipsLED_rgbColorSet(2, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B)); 			
						(DEV_actReserve & 0x02)?\
							((relayStatus_tipsTemp & 0x02)?\
								(tipsLED_rgbColorSet(1, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_B)):\
								(tipsLED_rgbColorSet(1, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_B))):\
							(tipsLED_rgbColorSet(1, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B)); 			
						(DEV_actReserve & 0x04)?\
							((relayStatus_tipsTemp & 0x04)?\
								(tipsLED_rgbColorSet(0, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_open].color_B)):\
								(tipsLED_rgbColorSet(0, color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_R, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_G, 
														color_Tab[devBackgroundLight_param.sw3bitIcurtain_BKlight_Param.sw3bit_BKlight_Param.sw3bit_BKlightColorInsert_close].color_B))):\
							(tipsLED_rgbColorSet(0, tips_relayUnused.color_R, tips_relayUnused.color_G, tips_relayUnused.color_B));

					}break;
				}

				/*zigb网络状态指示*/
				{
					static u16 tips_Counter = 0;
					const u16 tips_Period = 40;

					static bool tips_Type = 0;

					devNwkStatusTips_refresh(); //tips状态刷新
					
					if(tips_Counter < tips_Period)tips_Counter ++;
					else{

						tips_Type = !tips_Type;
						tips_Counter = 0;

						switch(devNwkTips_status){
						
							case devNwkStaute_nwkAllNormal:{ //常绿
						
								(tips_Type)?(tipsLED_rgbColorSet(3, 0, 31, 0)):(tipsLED_rgbColorSet(3, 0, 31, 0));
							
							}break;
						
							case devNwkStaute_zigbNwkNormalOnly:{ //常蓝

								(tips_Type)?(tipsLED_rgbColorSet(3, 0, 0, 31)):(tipsLED_rgbColorSet(3, 0, 0, 31));
						
							}break;
						
							case devNwkStaute_wifiNwkNormalOnly:{ //常白

								(tips_Type)?(tipsLED_rgbColorSet(3, 15, 10, 31)):(tipsLED_rgbColorSet(3, 15, 10, 31));
						
							}break;
						
							case devNwkStaute_nwkAllAbnormal:{ //常红

								(tips_Type)?(tipsLED_rgbColorSet(3, 31, 0, 0)):(tipsLED_rgbColorSet(3, 31, 0, 0));
						
							}break;
						
							case devNwkStaute_zigbNwkOpen:{ //绿闪

								(tips_Type)?(tipsLED_rgbColorSet(3, 0, 31, 0)):(tipsLED_rgbColorSet(3, 0, 0, 0));
						
							}break;
							
							case devNwkStaute_wifiNwkFind:{ //白闪

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

			case status_touchReset:{

				tips_sysTouchReset();

			}break;

			default:break;
		}

		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

LOCAL void 
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

LOCAL void 
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

void tips_sysTouchReset(void){
	
	static bool tipsStep = 0;

	if(counter_tipsAct){
	
		if(tipsStep){
		
			usrDats_actuator.func_tipsLedRGB[3].color_R = 0;
			usrDats_actuator.func_tipsLedRGB[0].color_R = 0;
			usrDats_actuator.func_tipsLedRGB[1].color_R = 0;
			usrDats_actuator.func_tipsLedRGB[2].color_R = 0;

		}else{
		
			usrDats_actuator.func_tipsLedRGB[3].color_R = 31;
			usrDats_actuator.func_tipsLedRGB[0].color_R = 31;
			usrDats_actuator.func_tipsLedRGB[1].color_R = 31;
			usrDats_actuator.func_tipsLedRGB[2].color_R = 31;
		}
		
	}else{
	
		counter_tipsAct = 400;
		tipsStep = !tipsStep;
	}
}

void tips_sysStandBy(void){

	usrDats_actuator.func_tipsLedRGB[3].color_R = 31;
	usrDats_actuator.func_tipsLedRGB[0].color_R = 31;
	usrDats_actuator.func_tipsLedRGB[1].color_R = 31;
	usrDats_actuator.func_tipsLedRGB[2].color_R = 31;
}


LOCAL void 
tips_sysButtonReales(void){

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

LOCAL void 
tips_specified(u8 tips_Type){ //tips类别

	static u8 	localTips_Count = 0; //步骤初期切换初始值
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
	
		if(tipsStep < step_period)tipsStep ++; //次步骤初期，切换至下一主步骤
		else{
		
			tipsStep = 0;
			
			pwmType_A = pwmType_B = pwmType_C = pwmType_D = 0;
		}
	}
	if(!localTips_Count)count_FLG = 1;
	else 
	if(localTips_Count > 80){
	
		count_FLG = 0;
		
//		localTips_Count = COLORGRAY_MAX - 2; //快速脱离当前状态
//		(tipsStep >= step_period)?(tipsStep = 0):(tipsStep ++);  //次步骤中期，切换至下一步骤
	}
	
	if(!counter_tipsAct){
	
		(!count_FLG)?(counter_tipsAct = ((localTips_Count --) % RLY_TIPS_PERIODUNITS) / speed_Mol * speed_Den):(counter_tipsAct = ((localTips_Count ++) % RLY_TIPS_PERIODUNITS) / speed_Mol * speed_Den); //更新单周期时间
		
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
						
							if(localTips_Count % 15 > 10)usrDats_actuator.func_tipsLedRGB[3].color_G = 31;
							else usrDats_actuator.func_tipsLedRGB[3].color_G = 0;
						
						}else{
						
							if(localTips_Count > 45)usrDats_actuator.func_tipsLedRGB[2].color_G = 31;
							if(localTips_Count > 50)usrDats_actuator.func_tipsLedRGB[1].color_G = 31;
							if(localTips_Count > 60)usrDats_actuator.func_tipsLedRGB[0].color_G = 31;
							if(localTips_Count > 70)usrDats_actuator.func_tipsLedRGB[3].color_G = 31;
						}
						
					}else{ 
						
						usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_D;
						usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_C;
						usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_B;
						usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_A;
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
								usrDats_actuator.func_tipsLedRGB[3].color_B = 15;
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
								usrDats_actuator.func_tipsLedRGB[3].color_B = 15;
							}
							if(localTips_Count > 50){
								
								usrDats_actuator.func_tipsLedRGB[0].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[0].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[0].color_B = 15;
							}
							if(localTips_Count > 60){
								
								usrDats_actuator.func_tipsLedRGB[1].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[1].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[1].color_B = 15;
							}
							if(localTips_Count > 70){
								
								usrDats_actuator.func_tipsLedRGB[2].color_R = 15;
								usrDats_actuator.func_tipsLedRGB[2].color_G = 10;
								usrDats_actuator.func_tipsLedRGB[2].color_B = 15;
							}
						}
						
					}
					else{ 
						
						usrDats_actuator.func_tipsLedRGB[3].color_R = pwmType_D / 2;
						usrDats_actuator.func_tipsLedRGB[3].color_G = pwmType_D / 3;
						usrDats_actuator.func_tipsLedRGB[3].color_B = pwmType_D / 2;

						usrDats_actuator.func_tipsLedRGB[2].color_R = pwmType_A / 2;
						usrDats_actuator.func_tipsLedRGB[2].color_G = pwmType_A / 3;
						usrDats_actuator.func_tipsLedRGB[2].color_B = pwmType_A / 2;

						usrDats_actuator.func_tipsLedRGB[1].color_R = pwmType_B / 2;
						usrDats_actuator.func_tipsLedRGB[1].color_G = pwmType_B / 3;
						usrDats_actuator.func_tipsLedRGB[1].color_B = pwmType_B / 2;

						usrDats_actuator.func_tipsLedRGB[0].color_R = pwmType_C / 2;
						usrDats_actuator.func_tipsLedRGB[0].color_G = pwmType_C / 3;
						usrDats_actuator.func_tipsLedRGB[0].color_B = pwmType_C / 2;
					}
					
				}break;
					
				default:break;
			}
			
		}break;
		
		default:break;
	}
}

void 
usrTips_ThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	xReturn = xTaskCreate(usrTipsProcess_task, "Process_Tips", 512, (void *)NULL, 2, &pxTaskHandle_threadUsrTIPS);

	os_printf("\npxTaskHandle_threadUsrTips is %d\n", pxTaskHandle_threadUsrTIPS);
}

