#include "bsp_Hardware.h"

#include "esp_common.h"

#include "GPIO.h"
#include "pwm.h"

#include "spi_interface.h"

#include "datsManage.h"

#include "usrInterface_keylbutton.h"
#include "usrInterface_Tips.h"
#include "timer_Activing.h"

stt_HC597_datsIn	usrDats_sensor;
stt_HC595_datsOut 	usrDats_actuator = {

	0
};

LOCAL os_timer_t timer_spi595and597_datsReales;
/*---------------------------------------------------------------------------------------------*/

LOCAL u8 ICACHE_FLASH_ATTR
spi595and597_datsReales(u8 datsIn){

	u8 loop;
	u8 datsOut_temp = datsIn;
	u8 datsIn_temp	= 0;

	spi595U7_pinRck(0);
	for(loop = 0; loop < 8; loop ++){

		spi595U7_pinClk(0);
		(datsOut_temp & 0x80)?(spi595_pinDout(1)):(spi595_pinDout(0));
		spi595U7_pinClk(1);		
		datsOut_temp <<= 1;
	}
	
	spi595U7_pinRck(1);
	spi595U7_pinClk(0);
	
	for(loop = 0; loop < 8; loop ++){

		datsIn_temp <<= 1;	
		(spi597_pinDin())?(datsIn_temp |= 0x01):(datsIn_temp &= 0xFE);
		spi595U7_pinClk(1);	
		spi595U7_pinClk(0);
	}

//	printf("\r\n[Tips_hardware]: data(%02X) has been written to 595.\n[Tips_hardware]: data(%02X) have read from 597.\n", datsIn, datsIn_temp);																												
	return datsIn_temp;
}

#if(HARDWARE_VERSION_DEBUG == 1)

LOCAL void ICACHE_FLASH_ATTR 
virtual_SPI595and597_gpioInit(void){

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO4_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO5_U);

	gpio16_input_conf();

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTCK_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTMS_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTDO_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTDI_U);

//	uint32 pwm_duty[3] = {TIPS_LEDRGB_DUTYUINT * 100, TIPS_LEDRGB_DUTYUINT * 100, TIPS_LEDRGB_DUTYUINT * 100};

//	uint32 io_info[3][3] = {

//		{PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13, 13},
//		{PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14, 14},
//		{PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15, 15}
//	};

//	pwm_init(TIPS_LEDRGB_PERIOD, pwm_duty, 3, io_info);
//	pwm_start();
}

LOCAL void ICACHE_FLASH_ATTR
timerFunCB_hw595and597datsReales(void *para){

	u8 datsOut_temp = 0;
	u8 datsIn_temp	= 0;

	u8 loop;

	const  u8 rlyTips_reales_Period = TIPS_BASECOLOR_NUM * RLY_TIPS_PERIODUNITS;
	static u8 P_rlyTips_reales = TIPS_BASECOLOR_NUM * RLY_TIPS_PERIODUNITS + 1;
	static u8 period_rgbCnt[RLY_TIPS_NUM][3] = {0};

	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_ZigbeeRst 	<< 7;	//Tips��ɫ�����ֵ����
	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_rly_2 		<< 6;
	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_rly_1 		<< 5;
	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_rly_0 		<< 4;

	TIPS_RGBSET_R(RGB_DISABLE);	//ɫֵ����
	TIPS_RGBSET_G(RGB_DISABLE);
	TIPS_RGBSET_B(RGB_DISABLE);

	if(P_rlyTips_reales > rlyTips_reales_Period){	//�Ҷ�ֵ����,ͬʱ���ֵ�޶�

		P_rlyTips_reales = 0;

		for(loop = 0; loop < RLY_TIPS_NUM; loop ++)(usrDats_actuator.func_tipsLedRGB[loop].color_R > RLY_TIPS_PERIODUNITS)?(period_rgbCnt[loop][0] = RLY_TIPS_PERIODUNITS):(period_rgbCnt[loop][0] = usrDats_actuator.func_tipsLedRGB[loop].color_R);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++)(usrDats_actuator.func_tipsLedRGB[loop].color_G > RLY_TIPS_PERIODUNITS)?(period_rgbCnt[loop][1] = RLY_TIPS_PERIODUNITS):(period_rgbCnt[loop][1] = usrDats_actuator.func_tipsLedRGB[loop].color_G);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++)(usrDats_actuator.func_tipsLedRGB[loop].color_B > RLY_TIPS_PERIODUNITS)?(period_rgbCnt[loop][2] = RLY_TIPS_PERIODUNITS):(period_rgbCnt[loop][2] = usrDats_actuator.func_tipsLedRGB[loop].color_B);
	}

	if(P_rlyTips_reales < (RLY_TIPS_PERIODUNITS * 1) && P_rlyTips_reales >= (RLY_TIPS_PERIODUNITS * 0)){	//R�Ҷ���װ

		TIPS_RGBSET_R(RGB_ENABLE);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++){

			if(period_rgbCnt[loop][0]){

				period_rgbCnt[loop][0] --;
				datsOut_temp |= 1 << loop;				
			}
		}
	}else
	if(P_rlyTips_reales < (RLY_TIPS_PERIODUNITS * 2) && P_rlyTips_reales >= (RLY_TIPS_PERIODUNITS * 1)){	//G�Ҷ���װ
	
		TIPS_RGBSET_G(RGB_ENABLE);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++){
	
			if(period_rgbCnt[loop][1]){
	
				period_rgbCnt[loop][1] --;
				datsOut_temp |= 1 << loop;			
			}
		}
	}else
	if(P_rlyTips_reales < (RLY_TIPS_PERIODUNITS * 3) && P_rlyTips_reales >= (RLY_TIPS_PERIODUNITS * 2)){	//B�Ҷ���װ
	
		TIPS_RGBSET_B(RGB_ENABLE);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++){
	
			if(period_rgbCnt[loop][2]){
	
				period_rgbCnt[loop][2] --;
				datsOut_temp |= 1 << loop;	
			}
		}
	}
	
	datsIn_temp = spi595and597_datsReales(datsOut_temp);	//Ӳ��ִ��

	usrDats_sensor.usrDcode_3 		= (datsIn_temp & 0x80) >> 7;	//597��ȡ��������ֵ������װ
	usrDats_sensor.usrDcode_2 		= (datsIn_temp & 0x40) >> 6;
	usrDats_sensor.usrDcode_1 		= (datsIn_temp & 0x20) >> 5;
	usrDats_sensor.usrDcode_0 		= (datsIn_temp & 0x10) >> 4;
	usrDats_sensor.usrKeyIn_fun_0 	= (datsIn_temp & 0x08) >> 3;
	usrDats_sensor.usrKeyIn_rly_2 	= (datsIn_temp & 0x04) >> 2;
	usrDats_sensor.usrKeyIn_rly_1 	= (datsIn_temp & 0x02) >> 1;
	usrDats_sensor.usrKeyIn_rly_0 	= (datsIn_temp & 0x01) >> 0;

	P_rlyTips_reales ++;

	{ //������ר��

		static u8 period_beep = 3;		//beepsר��
		static u8 count_beep  = 0;	

		if(count_beep < period_beep)count_beep ++;
		else{

			static u16 	tips_Period = 20 * 50 / 2;
			static u16 	tips_Count 	= 0;
			static u8 	tips_Loop 	= 2 * 4;
			static bool beeps_en 	= 1;

			count_beep = 0;

			switch(dev_statusBeeps){

				case beepsMode_standBy:{
					
					period_beep = devTips_beep.tips_Period;
					tips_Period = 20 * devTips_beep.tips_time / period_beep;
					tips_Loop 	= 2 * devTips_beep.tips_loop;
					tips_Count 	= 0;
					beeps_en 	= 1;
					dev_statusBeeps = beepsWorking;
		
				}break;
				
				case beepsWorking:{
				
					if(tips_Loop){
					
						if(tips_Count < tips_Period){
						
							tips_Count ++;
							(beeps_en)?(TIPS_BEEP_SET(!GPIO_INPUT_GET(GPIO_ID_PIN(GPIO_PIN_BEEP)))):(TIPS_BEEP_SET(1));
							
						}else{
						
							tips_Count = 0;
							beeps_en = !beeps_en;
							tips_Loop --;
						}
						
					}else{
					
						dev_statusBeeps = beepsComplete;
					}
				
				}break;
				
				case beepsComplete:{
				
					tips_Count = 0;
					beeps_en = 1;
					TIPS_BEEP_SET(1);
					dev_statusBeeps = beepsMode_null;
					
				}break;
			
				default:{
				
					TIPS_BEEP_SET(1);
					
				}break;
			}
		}
	}

	{ //1ms��ʱר�� //���ɱ�smartconfig���

		const u16 period_1ms = 10;
		static u16 counter_1ms = 0;

		if(counter_1ms < period_1ms)counter_1ms ++;
		else{

			counter_1ms = 0;

			//����������ʱҵ���߼�
			if(touchPadActCounter)touchPadActCounter --; //����ʱ��
			if(touchPadContinueCnt)touchPadContinueCnt --; //�������ʱ��
			
			//�ᴥ������ʱҵ���߼�
			if(usrKeyCount_EN)usrKeyCount ++;
			else usrKeyCount = 0;

			//tips�������ڼ�ʱ����ר��
			if(counter_tipsAct)counter_tipsAct --;
		}
	}

	{ //1s��ʱר�� //���ɱ�smartconfig���

		const u16 period_1second = 10000;
		static u16 counter_1second = 0;

		if(counter_1second < period_1second)counter_1second ++;
		else{

			counter_1second = 0;

			/*����UTCÿ����������*/
			systemUTC_current ++;
			//ϵͳ����ʱ��ά�ָ���
			sysTimeKeep_counter ++;

			/*smartconfig����ʱ���ʱ*/
			if(smartconfigOpen_flg){
			
				if(timeCounter_smartConfig_start)timeCounter_smartConfig_start --; //����ʱ
				else{

					usrSmartconfig_stop(); //smartconfig���Թر�
				}
			}
		}
	}
}

#else

LOCAL void ICACHE_FLASH_ATTR 
virtual_SPI595and597_gpioInit(void){

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO4_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO5_U);

	gpio16_input_conf();

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTDI_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTCK_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_MTMS_U);

	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_MTDO_U);


//	uint32 pwm_duty[3] = {TIPS_LEDRGB_DUTYUINT * 100, TIPS_LEDRGB_DUTYUINT * 100, TIPS_LEDRGB_DUTYUINT * 100};

//	uint32 io_info[3][3] = {

//		{PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13, 13},
//		{PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14, 14},
//		{PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15, 15}
//	};

//	pwm_init(TIPS_LEDRGB_PERIOD, pwm_duty, 3, io_info);
//	pwm_start();
}

LOCAL void ICACHE_FLASH_ATTR
timerFunCB_hw595and597datsReales(void *para){

	u8 datsOut_temp = 0;
	u8 datsIn_temp	= 0;

	u8 loop;

	const  u8 rlyTips_reales_Period = TIPS_BASECOLOR_NUM * RLY_TIPS_PERIODUNITS;
	static u8 P_rlyTips_reales = TIPS_BASECOLOR_NUM * RLY_TIPS_PERIODUNITS + 1;
	static u8 period_rgbCnt[RLY_TIPS_NUM][TIPS_BASECOLOR_NUM] = {0};

	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_ZigbeeRst 	<< 1;	//���ƽ����ֵ����
	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_rly_2 		<< 5;
	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_rly_1 		<< 6;
	datsOut_temp |= (u8)usrDats_actuator.conDatsOut_rly_0 		<< 7;
	
//	datsOut_temp |= 0x1D; //TipsӲ����λ���ߵ�ƽ��Ϩ��
	datsOut_temp &= ~0x1D; //TipsӲ����λ���͵�ƽ��Ϩ��

	TIPS_RGBSET_R(RGB_DISABLE);	//ɫֵ����
	TIPS_RGBSET_G(RGB_DISABLE);
	TIPS_RGBSET_B(RGB_DISABLE);

	if(P_rlyTips_reales > rlyTips_reales_Period){	//�Ҷ�ֵ����,ͬʱ���ֵ�޶�

		P_rlyTips_reales = 0;

		for(loop = 0; loop < RLY_TIPS_NUM; loop ++)(usrDats_actuator.func_tipsLedRGB[loop].color_R > RLY_TIPS_PERIODUNITS)?(period_rgbCnt[loop][0] = RLY_TIPS_PERIODUNITS):(period_rgbCnt[loop][0] = usrDats_actuator.func_tipsLedRGB[loop].color_R);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++)(usrDats_actuator.func_tipsLedRGB[loop].color_G > RLY_TIPS_PERIODUNITS)?(period_rgbCnt[loop][1] = RLY_TIPS_PERIODUNITS):(period_rgbCnt[loop][1] = usrDats_actuator.func_tipsLedRGB[loop].color_G);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++)(usrDats_actuator.func_tipsLedRGB[loop].color_B > RLY_TIPS_PERIODUNITS)?(period_rgbCnt[loop][2] = RLY_TIPS_PERIODUNITS):(period_rgbCnt[loop][2] = usrDats_actuator.func_tipsLedRGB[loop].color_B);
	}

	if(P_rlyTips_reales < (RLY_TIPS_PERIODUNITS * 1) && P_rlyTips_reales >= (RLY_TIPS_PERIODUNITS * 0)){	//R�Ҷ���װ

		TIPS_RGBSET_R(RGB_ENABLE);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++){

			if(period_rgbCnt[loop][0]){

				switch(loop){

					case 0:{ //��������λ
						
						period_rgbCnt[loop][0] --;
//						datsOut_temp &= ~(1 << loop);
						if(DEV_actReserve & 0x04)datsOut_temp |= 1 << loop; //ָʾ���ú�׼

					}break;

					case 1:{ //������������λ

						period_rgbCnt[loop][0] --;
//						datsOut_temp &= ~(2 << loop);
						if(DEV_actReserve & 0x02)datsOut_temp |= 2 << loop; //ָʾ���ú�׼

					}break;

					case 2:{ //������������λ

						period_rgbCnt[loop][0] --;
//						datsOut_temp &= ~(2 << loop);
						if(DEV_actReserve & 0x01)datsOut_temp |= 2 << loop; //ָʾ���ú�׼

					}break;

					case 3:{ //������������λ

						period_rgbCnt[loop][0] --;
//						datsOut_temp &= ~(2 << loop);
						datsOut_temp |= 2 << loop;						

					}break;

					default:{}break;
				}				
			}
		}
	}
	else
	if(P_rlyTips_reales < (RLY_TIPS_PERIODUNITS * 2) && P_rlyTips_reales >= (RLY_TIPS_PERIODUNITS * 1)){	//G�Ҷ���װ
	
		TIPS_RGBSET_G(RGB_ENABLE);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++){
	
			if(period_rgbCnt[loop][1]){
	
				switch(loop){

					case 0:{ //��������λ
						
						period_rgbCnt[loop][1] --;
//						datsOut_temp &= ~(1 << loop);
						if(DEV_actReserve & 0x04)datsOut_temp |= 1 << loop; //ָʾ���ú�׼

					}break;

					case 1:{ //������������λ

						period_rgbCnt[loop][1] --;
//						datsOut_temp &= ~(2 << loop);
						if(DEV_actReserve & 0x02)datsOut_temp |= 2 << loop; //ָʾ���ú�׼						

					}break;

					case 2:{ //������������λ

						period_rgbCnt[loop][1] --;
//						datsOut_temp &= ~(2 << loop);
						if(DEV_actReserve & 0x01)datsOut_temp |= 2 << loop; //ָʾ���ú�׼						

					}break;

					case 3:{ //������������λ

						period_rgbCnt[loop][1] --;
//						datsOut_temp &= ~(2 << loop);
						datsOut_temp |= 2 << loop;

					}break;

					default:{}break;
				}			
			}
		}
	}
	else
	if(P_rlyTips_reales < (RLY_TIPS_PERIODUNITS * 3) && P_rlyTips_reales >= (RLY_TIPS_PERIODUNITS * 2)){	//B�Ҷ���װ
	
		TIPS_RGBSET_B(RGB_ENABLE);
		for(loop = 0; loop < RLY_TIPS_NUM; loop ++){
	
			if(period_rgbCnt[loop][2]){
	
				switch(loop){

					case 0:{ //��������λ
						
						period_rgbCnt[loop][2] --;
//						datsOut_temp &= ~(1 << loop);
						if(DEV_actReserve & 0x04)datsOut_temp |= 1 << loop; //ָʾ���ú�׼

					}break;

					case 1:{ //������������λ

						period_rgbCnt[loop][2] --;
//						datsOut_temp &= ~(2 << loop);
						if(DEV_actReserve & 0x02)datsOut_temp |= 2 << loop; //ָʾ���ú�׼						

					}break;

					case 2:{ //������������λ

						period_rgbCnt[loop][2] --;
//						datsOut_temp &= ~(2 << loop);
						if(DEV_actReserve & 0x01)datsOut_temp |= 2 << loop; //ָʾ���ú�׼						

					}break;

					case 3:{ //������������λ

						period_rgbCnt[loop][2] --;
//						datsOut_temp &= ~(2 << loop);
						datsOut_temp |= 2 << loop;

					}break;

					default:{}break;
				}		
			}
		}
	}
	
//	datsIn_temp = spi595and597_datsReales(datsOut_temp);	//Ӳ��ִ��
//	if(datsIn_temp == spi595and597_datsReales(datsOut_temp))datsIn_cfm = datsIn_temp; //�ź�ȷ��

	datsIn_temp = spi595and597_datsReales(datsOut_temp);	//Ӳ��ִ��

//	usrDats_sensor.usrDcode_3 		= (datsIn_cfm & 0x80) >> 7;	//597��ȡ��������ֵ������װ
//	usrDats_sensor.usrDcode_2 		= (datsIn_cfm & 0x40) >> 6;
//	usrDats_sensor.usrDcode_1 		= (datsIn_cfm & 0x20) >> 5;
//	usrDats_sensor.usrDcode_0 		= (datsIn_cfm & 0x10) >> 4;
//	usrDats_sensor.usrKeyIn_fun_0 	= (datsIn_cfm & 0x08) >> 3;
//	usrDats_sensor.usrKeyIn_rly_2 	= (datsIn_cfm & 0x04) >> 2;
//	usrDats_sensor.usrKeyIn_rly_1 	= (datsIn_cfm & 0x02) >> 1;
//	usrDats_sensor.usrKeyIn_rly_0 	= (datsIn_cfm & 0x01) >> 0;

	usrDats_sensor.usrKeyIn_fun_0 	= (datsIn_temp & 0x02) >> 1;	//597��ȡ��������ֵ������װ
	usrDats_sensor.usrKeyIn_rly_2 	= (datsIn_temp & 0x08) >> 3;
	usrDats_sensor.usrKeyIn_rly_1 	= (datsIn_temp & 0x01) >> 0;
	usrDats_sensor.usrKeyIn_rly_0 	= (datsIn_temp & 0x04) >> 2;
	usrDats_sensor.usrDcode_3 		= (datsIn_temp & 0x40) >> 6;
	usrDats_sensor.usrDcode_2 		= (datsIn_temp & 0x80) >> 7;
	usrDats_sensor.usrDcode_1 		= (datsIn_temp & 0x20) >> 5;
	usrDats_sensor.usrDcode_0 		= (datsIn_temp & 0x10) >> 4;

	P_rlyTips_reales ++;

	{ //������ר��

		static u8 period_beep = 3;		//beepsר��
		static u8 count_beep  = 0;	

		if(count_beep < period_beep)count_beep ++;
		else{

			static u16 	tips_Period = 20 * 50 / 2;
			static u16 	tips_Count 	= 0;
			static u8 	tips_Loop 	= 2 * 4;
			static bool beeps_en 	= 1;

			count_beep = 0;

			switch(dev_statusBeeps){

				case beepsMode_standBy:{
					
					period_beep = devTips_beep.tips_Period;
					tips_Period = 20 * devTips_beep.tips_time / period_beep;
					tips_Loop 	= 2 * devTips_beep.tips_loop;
					tips_Count 	= 0;
					beeps_en 	= 1;
					dev_statusBeeps = beepsWorking;
		
				}break;
				
				case beepsWorking:{
				
					if(tips_Loop){
					
						if(tips_Count < tips_Period){
						
							tips_Count ++;
							(beeps_en)?(TIPS_BEEP_SET(!GPIO_INPUT_GET(GPIO_ID_PIN(GPIO_PIN_BEEP)))):(TIPS_BEEP_SET(1));
							
						}else{
						
							tips_Count = 0;
							beeps_en = !beeps_en;
							tips_Loop --;
						}
						
					}else{
					
						dev_statusBeeps = beepsComplete;
					}
				
				}break;
				
				case beepsComplete:{
				
					tips_Count = 0;
					beeps_en = 1;
					TIPS_BEEP_SET(1);
					dev_statusBeeps = beepsMode_null;
					
				}break;
			
				default:{
				
					TIPS_BEEP_SET(1);
					
				}break;
			}
		}
	}

	{ //1ms��ʱר�� //���ɱ�smartconfig���

		const u16 period_1ms = 10;
		static u16 counter_1ms = 0;

		if(counter_1ms < period_1ms)counter_1ms ++;
		else{

			counter_1ms = 0;

			//����������ʱҵ���߼�
			if(touchPadActCounter)touchPadActCounter --; //����ʱ��
			if(touchPadContinueCnt)touchPadContinueCnt --; //�������ʱ��
			
			//�ᴥ������ʱҵ���߼�
			if(usrKeyCount_EN)usrKeyCount ++;
			else usrKeyCount = 0;

			//tips�������ڼ�ʱ����ר��
			if(counter_tipsAct)counter_tipsAct --;
		}
	}

	{ //1s��ʱר�� //���ɱ�smartconfig���

		const u16 period_1second = 10000;
		static u16 counter_1second = 0;

		if(counter_1second < period_1second)counter_1second ++;
		else{

			counter_1second = 0;

			/*����UTCÿ����������*/
			systemUTC_current ++;
			//ϵͳ����ʱ��ά�ָ���
			sysTimeKeep_counter ++;

			/*smartconfig����ʱ���ʱ*/
			if(smartconfigOpen_flg){
			
				if(timeCounter_smartConfig_start)timeCounter_smartConfig_start --; //����ʱ
				else{

					usrSmartconfig_stop(); //smartconfig���Թر�
				}
			}
		}
	}
}

#endif

void ICACHE_FLASH_ATTR
dats595and597_keepRealesingStart(void){

	virtual_SPI595and597_gpioInit();

    hw_timer_init();
    hw_timer_set_func(timerFunCB_hw595and597datsReales, true);
    hw_timer_arm(100);

//	os_timer_disarm(&timer_spi595and597_datsReales);
//	os_timer_setfn(&timer_spi595and597_datsReales, timerFunCB_hw595and597datsReales, NULL);
//	os_timer_arm(&timer_spi595and597_datsReales, 1, true);
}










