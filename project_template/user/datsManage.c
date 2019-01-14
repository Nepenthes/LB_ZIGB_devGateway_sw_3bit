#include "datsManage.h"

#include "esp_common.h"

#include "spi_flash.h"

#include "usrInterface_Tips.h"

#include "hwPeripherial_Actuator.h"

const u8 debugLogOut_targetMAC[5] = {0x20, 0x18, 0x12, 0x34, 0x56};
const u8 serverRemote_IP_Lanbon[4] = {10, 0, 0, 224};
//const u8 serverRemote_IP_Lanbon[4] = {47,52,5,108}; //47,52,5,108 香港 //112,124,61,191 中国

const int remoteServerPort_switchTab[INTERNET_REMOTESERVER_PORTTAB_LEN] = {

	2000, 3000, 4000, 5000, 7000 
};

u8 zigbNwkReserveNodeNum_currentValue = 0; //zigb网络内当前有效节点数量

stt_scenarioOprateDats scenarioOprateDats = {0}; //场景操作数据缓存

//u8 CTRLEATHER_PORT[USRCLUSTERNUM_CTRLEACHOTHER] = {0x1A, 0x1B, 0x1C}; //互控端口位-test
u8 CTRLEATHER_PORT[USRCLUSTERNUM_CTRLEACHOTHER] = {0, 0, 0}; //互控端口位

u8 COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT_NUMTAIL] = {0}; //主机管理表-互控状态管理
stt_scenarioOprateDats COLONY_DATAMANAGE_SCENE = {0}; //主机管理表-场景控制状态管理

u8 MACSTA_ID[DEV_MAC_LEN] = {0};
u8 MACAP_ID[DEV_MAC_LEN] = {0};
u8 MACDST_ID[DEV_MAC_LEN] = {1,1,1,1,1,1}; //默认不为0即可

bool deviceLock_flag = false; //设备锁标志

u8 DEV_actReserve = 0x07; //有效操作位
u8 SWITCH_TYPE = SWITCH_TYPE_CURTAIN;	

static bool flashReadLocalDev_reserveFLG = true; //互斥标志:本地信息读
static bool flashWriteLocalDev_reserveFLG = true; //互斥标志:本地信息写

#if(RELAYSTATUS_REALYTIME_ENABLEIF)
	static u8 dataInfo_relayStatusRealTime_record[RECORDPERIOD_RELAYSTATUS_REALYTIME] = {0};  //继电器实时状态记录缓存
	static u8 loopInsert_relayStatusRealTime_record = 0; //继电器实时状态记录游标
	static u32 spiFlashStatus_temp = 0;
#endif
/*---------------------------------------------------------------------------------------------*/

/*更新当前开关类型对应的有效操作位*/
u8 
switchTypeReserve_GET(void){

	u8 act_Reserve = 0x07;

	if(SWITCH_TYPE == SWITCH_TYPE_SWBIT3){
		
		act_Reserve = 0x07;
		
	}else
	if(SWITCH_TYPE == SWITCH_TYPE_SWBIT2){
		
		act_Reserve = 0x05;
	
	}else
	if(SWITCH_TYPE == SWITCH_TYPE_SWBIT1){
	
		act_Reserve = 0x02;
	}
	
	return act_Reserve;
}

void 
portCtrlEachOther_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	memcpy(CTRLEATHER_PORT, datsRead_Temp->port_ctrlEachother, USRCLUSTERNUM_CTRLEACHOTHER);

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void 
devMAC_Reales(void){

	const u8 debug_staMAC[DEV_MAC_LEN] = {0x20, 0x20, 0x00, 0x00, 0x00, 0xAA};
	const u8 debug_apMAC[DEV_MAC_LEN] = {0x20, 0x20, 0x00, 0x00, 0x00, 0xBB};
	
#if(DEV_MAC_SOURCE_DEF == DEV_MAC_SOURCE_WIFI)
	wifi_get_macaddr(STATION_IF, MACSTA_ID); //获取staMAC
	wifi_get_macaddr(SOFTAP_IF, MACAP_ID); //获取apMAC
#else
	memcpy(MACSTA_ID, debug_staMAC, 6);
	memcpy(MACAP_ID, debug_apMAC, 6);
#endif
}

void 
devLockIF_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	(datsRead_Temp->dev_lockIF)?(deviceLock_flag = true):(deviceLock_flag = false);

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void 
printf_datsHtoA(const u8 *TipsHead, u8 *dats , u8 datsLen){

	u8 dats_Log[DEBUG_LOGLEN] = {0};
	u8 loop = 0;

	memset(&dats_Log[0], 0, DEBUG_LOGLEN * sizeof(u8));
	for(loop = 0; loop < datsLen; loop ++){

		sprintf((char *)&dats_Log[loop * 3], "%02X ", *dats ++);
	}
	os_printf("%s<datsLen: %d> %s.\n", TipsHead, datsLen, dats_Log);
}

void 
printf_datsHtoB(const u8 *TipsHead, u8 *dats , u8 datsLen){

	u8 dats_Log[DEBUG_LOGLEN] = {0};
	u8 loop = 0;
	u8 loopa = 0;

	memset(&dats_Log[0], 0, DEBUG_LOGLEN * sizeof(u8));
	for(loop = 0; loop < datsLen; loop ++){

		for(loopa = 0; loopa < 8; loopa ++){

			(dats[loop] & (0x80 >> loopa))?(dats_Log[8 * loop + loopa + loop] = '1'):(dats_Log[8 * loop + loopa + loop] = '0'); 
		}

		if(loop < datsLen - 1)dats_Log[8 * (loop + 1) + loop] = '-';
	}

	os_printf("%s<bit length: %d> %s.\n", TipsHead, datsLen * 8, dats_Log);
}

void 
devParam_flashDataSave(devDatsSave_Obj dats_obj, stt_usrDats_privateSave datsSave_Temp){	//设备参数写入flash 掉电存储

	stt_usrDats_privateSave dats_Temp = {0};

	while(!flashWriteLocalDev_reserveFLG)vTaskDelay(1);
	flashWriteLocalDev_reserveFLG = false;

	spi_flash_read((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD) * SPI_FLASH_SEC_SIZE,
				   (u32 *)&dats_Temp,
				   sizeof(stt_usrDats_privateSave));
	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD);

	switch(dats_obj){	

		case obj_rlyStaute_flg:{

				dats_Temp.rlyStaute_flg = datsSave_Temp.rlyStaute_flg;
				
			}break;

		case obj_dev_lockIF:{
		
				dats_Temp.dev_lockIF = datsSave_Temp.dev_lockIF;
			
			}break;

		case obj_test_dats:{
		
				dats_Temp.test_dats = datsSave_Temp.test_dats;
				
			}break;

		case obj_timeZone_H:{

				dats_Temp.timeZone_H = datsSave_Temp.timeZone_H;

			}break;

		case obj_timeZone_M:{

				dats_Temp.timeZone_M = datsSave_Temp.timeZone_M;

			}break;

		case obj_devCurtainOrbitalPeriod:{

				dats_Temp.devCurtain_orbitalPeriod = datsSave_Temp.devCurtain_orbitalPeriod;
		
			}break;

		case obj_devCurtainOrbitalCounter:{

				dats_Temp.devCurtain_orbitalCounter = datsSave_Temp.devCurtain_orbitalCounter;

			}break;

		case obj_serverIP_default:{

				memcpy(dats_Temp.serverIP_default, datsSave_Temp.serverIP_default, 4);
				
			}break;

		case obj_swTimer_Tab:{

				memcpy(dats_Temp.swTimer_Tab, datsSave_Temp.swTimer_Tab, 3 * 8);
			
			}break;

		case obj_swDelay_flg:{

				dats_Temp.swDelay_flg = datsSave_Temp.swDelay_flg;
			
			}break;

		case obj_swDelay_periodCloseLoop:{

				dats_Temp.swDelay_periodCloseLoop = datsSave_Temp.swDelay_periodCloseLoop;
			
			}break;

		case obj_devNightModeTimer_Tab:{

				memcpy(dats_Temp.devNightModeTimer_Tab, datsSave_Temp.devNightModeTimer_Tab, 3 * 2);

			}break;

		case obj_port_ctrlEachother:{

				memcpy(dats_Temp.port_ctrlEachother, datsSave_Temp.port_ctrlEachother, 3);
			
			}break;

		case obj_panID_default:{

				dats_Temp.panID_default = datsSave_Temp.panID_default;
			
			}break;

		case obj_paramBKColorInsert:{

				memcpy(&(dats_Temp.param_bkLightColorInsert), &(datsSave_Temp.param_bkLightColorInsert), sizeof(bkLightColorInsert_paramAttr));

			}break;

		default:break;
	}
	
	spi_flash_write((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD) * SPI_FLASH_SEC_SIZE,
				    (u32 *)&dats_Temp,
				    sizeof(stt_usrDats_privateSave));

	flashWriteLocalDev_reserveFLG = true;
}

void 
devParam_SlaveZigbDevListInfoSave(stt_usrDats_zigbDevListInfo datsSave_Temp){ //网关下属zigb链表信息进行flash存储

	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD);
	spi_flash_write((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD) * SPI_FLASH_SEC_SIZE,
				    (u32 *)&datsSave_Temp,
				    sizeof(stt_usrDats_zigbDevListInfo));
}

void 
devParam_scenarioDataLocalSave(stt_scenarioDataLocalSave *datsSave_Temp){ //场景数据本地存储 --针对zigb场景开关子设备的同步化操作

	/*每扇区4096 Bytes，每个场景设备数量限定在100个以内，则每扇区可存储6个场景数据，但是这样需要进行覆盖操作，内存开销大，所以直接一个扇区存一个场景*/

	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SCENARIODATA_RECORD + datsSave_Temp->scenarioDataSave_InsertNum);
	spi_flash_write((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SCENARIODATA_RECORD + datsSave_Temp->scenarioDataSave_InsertNum) * SPI_FLASH_SEC_SIZE,
					(u32 *)datsSave_Temp,
					sizeof(stt_scenarioDataLocalSave));

	if(datsSave_Temp)os_free(datsSave_Temp);

//	os_printf(">>>data save sizeof is %d.\n", sizeof(stt_scenarioDataLocalSave));
}

stt_usrDats_privateSave 
*devParam_flashDataRead(void){		 //从flash读取设备参数

	while(!flashReadLocalDev_reserveFLG)vTaskDelay(1);

	flashReadLocalDev_reserveFLG = false;

	stt_usrDats_privateSave *dats_Temp = (stt_usrDats_privateSave *)os_zalloc(sizeof(stt_usrDats_privateSave));

	spi_flash_read((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD) * SPI_FLASH_SEC_SIZE,
				   (u32 *)dats_Temp,
				   sizeof(stt_usrDats_privateSave));

	flashReadLocalDev_reserveFLG = true;

	return dats_Temp;	//谨记释放内存		
	
}

stt_usrDats_zigbDevListInfo 
*devParam_SlaveZigbDevListInfoRead(void){ //网关下属zigb链表信息从flash读取

	stt_usrDats_zigbDevListInfo *dats_Temp = (stt_usrDats_zigbDevListInfo *)os_zalloc(sizeof(stt_usrDats_zigbDevListInfo));

	if(dats_Temp){

		spi_flash_read((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD) * SPI_FLASH_SEC_SIZE,
					   (u32 *)dats_Temp,
					   sizeof(stt_usrDats_zigbDevListInfo));
		
		return dats_Temp;	//谨记释放内存
	}

	return NULL;
}

stt_scenarioDataLocalSave
*devParam_scenarioDataLocalRead(u8 scenario_insertNum){

	stt_scenarioDataLocalSave *dats_Temp = (stt_scenarioDataLocalSave *)os_zalloc(sizeof(stt_scenarioDataLocalSave));
	
	if(dats_Temp){

		spi_flash_read((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SCENARIODATA_RECORD + scenario_insertNum) * SPI_FLASH_SEC_SIZE,
					   (u32 *)dats_Temp,
					   sizeof(stt_scenarioDataLocalSave));

		dats_Temp->scenarioDataSave_InsertNum = scenario_insertNum;
		
		return dats_Temp;	//谨记释放内存
	}

	return NULL;
}

#if(RELAYSTATUS_REALYTIME_ENABLEIF)
void 
devParamDtaaSave_relayStatusRealTime(u8 currentRelayStatus){

	u8 *dataInfoTemp_relayStatus = (u8 *)os_zalloc(sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);

	if(loopInsert_relayStatusRealTime_record >= RECORDPERIOD_RELAYSTATUS_REALYTIME){

		spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SPERELAYSTATUS_RECORD);
		memset(dataInfo_relayStatusRealTime_record, 0xff, sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);
		loopInsert_relayStatusRealTime_record = 0;

		spi_flash_read_status(&spiFlashStatus_temp);
	}

	dataInfo_relayStatusRealTime_record[loopInsert_relayStatusRealTime_record] = currentRelayStatus;
	os_printf("insert reales: %d, status:%02X.\n", loopInsert_relayStatusRealTime_record, dataInfo_relayStatusRealTime_record[loopInsert_relayStatusRealTime_record]);
	loopInsert_relayStatusRealTime_record ++;
	memcpy(dataInfoTemp_relayStatus, dataInfo_relayStatusRealTime_record, sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);

	spi_flash_write_status(spiFlashStatus_temp);
	spi_flash_write((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SPERELAYSTATUS_RECORD) * SPI_FLASH_SEC_SIZE,
					(u32 *)dataInfoTemp_relayStatus,
					sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);

	if(dataInfoTemp_relayStatus)os_free(dataInfoTemp_relayStatus); //内存释放	
}

u8 
devDataRecovery_relayStatus(void){

	u32 relayStatus_temp = 0;
	u8 *dataInfoTemp_relayStatus = (u8 *)os_zalloc(sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);
	
	spi_flash_read( (DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SPERELAYSTATUS_RECORD) * SPI_FLASH_SEC_SIZE,
					(u32 *)dataInfoTemp_relayStatus,
					sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);
	
	for(loopInsert_relayStatusRealTime_record = 0; loopInsert_relayStatusRealTime_record <= RECORDPERIOD_RELAYSTATUS_REALYTIME; loopInsert_relayStatusRealTime_record ++){

		if(dataInfoTemp_relayStatus[loopInsert_relayStatusRealTime_record] == 0xff){

			os_printf("insert catch: %d.\n", loopInsert_relayStatusRealTime_record);
			(!loopInsert_relayStatusRealTime_record)?(relayStatus_temp = 0):(relayStatus_temp = dataInfoTemp_relayStatus[-- loopInsert_relayStatusRealTime_record]);
			break;
		}
	}

	printf_datsHtoA("status enum: ", dataInfoTemp_relayStatus, 20);

	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SPERELAYSTATUS_RECORD);

	if(dataInfoTemp_relayStatus)os_free(dataInfoTemp_relayStatus); //内存释放

	memset(dataInfo_relayStatusRealTime_record, 0xff, sizeof(u8) * RECORDPERIOD_RELAYSTATUS_REALYTIME);
	loopInsert_relayStatusRealTime_record = RECORDPERIOD_RELAYSTATUS_REALYTIME;

	return relayStatus_temp;
}
#endif

void 
devData_recoverFactory(void){

    //flash清空
	stt_usrDats_privateSave dats_Temp = {0};

	char ssid[32] = "no_ssid";
	char password[64] = "no_password"; 
	struct station_config station_cfgParam = {0};

	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD);
	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD);
	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SPERELAYSTATUS_RECORD);
	spi_flash_write((DATS_LOCATION_START + 0) * SPI_FLASH_SEC_SIZE,
				    (u32 *)&dats_Temp,
				    sizeof(stt_usrDats_privateSave));

	//打生日标记 -(生日标记占用大小为1bit 赋0即可打标记，扇区清零已作用执行)

	//默认值填装
	memset(&(dats_Temp.param_bkLightColorInsert), 0xff, sizeof(bkLightColorInsert_paramAttr)); //背光灯出厂值填装，全填0xff，使其初始化时被动赋值为默认值
	devParam_flashDataSave(obj_paramBKColorInsert, dats_Temp); //存储执行
	dats_Temp.devCurtain_orbitalPeriod = CURTAIN_ORBITAL_PERIOD_INITTIME; //窗帘轨道时间出厂值填装
	devParam_flashDataSave(obj_devCurtainOrbitalPeriod, dats_Temp); //存储执行

	//路由器信息丢弃
	station_cfgParam.bssid_set = 0;
	memcpy(&station_cfgParam.ssid, ssid, 32);
	memcpy(&station_cfgParam.password, password, 64);
	wifi_station_set_config(&station_cfgParam); 
}

void 
devFactoryRecord_Opreat(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	if(datsRead_Temp->FLG_factory_IF){

		os_printf(">>>factory cover!!!\n");
		vTaskDelay(10);
		devData_recoverFactory();
	}

	if(datsRead_Temp)os_free(datsRead_Temp);
}






