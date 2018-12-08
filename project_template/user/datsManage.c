#include "datsManage.h"

#include "esp_common.h"

#include "spi_flash.h"

#include "usrInterface_Tips.h"

const u8 debugLogOut_targetMAC[5] = {0x20, 0x18, 0x12, 0x34, 0x56};
const u8 serverRemote_IP_Lanbon[4] = {10, 0, 0, 224};
//const u8 serverRemote_IP_Lanbon[4] = {47,52,5,108}; //47,52,5,108 香港 //112,124,61,191 中国

const int remoteServerPort_switchTab[INTERNET_REMOTESERVER_PORTTAB_LEN] = {

	2000, 3000, 4000, 5000, 7000 
};

u8 zigbNwkReserveNodeNum_currentValue = 0; //zigb网络内当前有效节点数量

stt_scenarioOprateDats scenarioOprateDats = {0};

//u8 CTRLEATHER_PORT[USRCLUSTERNUM_CTRLEACHOTHER] = {0x1A, 0x1B, 0x1C}; //互控端口位
u8 CTRLEATHER_PORT[USRCLUSTERNUM_CTRLEACHOTHER] = {0, 0, 0}; //互控端口位

u8 COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT_NUMTAIL] = {0}; //主机管理表-互控状态管理
stt_scenarioOprateDats COLONY_DATAMANAGE_SCENE = {0}; //主机管理表-场景控制状态管理

u8 MACSTA_ID[DEV_MAC_LEN] = {0};
u8 MACAP_ID[DEV_MAC_LEN] = {0};
u8 MACDST_ID[DEV_MAC_LEN] = {1,1,1,1,1,1}; //默认不为0即可

bool deviceLock_flag = false; //设备锁标志

u8 DEV_actReserve = 0x07; //有效操作位
u8 SWITCH_TYPE = SWITCH_TYPE_SWBIT3;

static bool flashReadLocalDev_reserveFLG = true; //互斥标志:本地信息读
static bool flashWriteLocalDev_reserveFLG = true; //互斥标志:本地信息写

#if(RELAYSTATUS_REALYTIME_ENABLEIF)
	static u8 dataInfo_relayStatusRealTime_record[RECORDPERIOD_RELAYSTATUS_REALYTIME] = {0};  //继电器实时状态记录缓存
	static u8 loopInsert_relayStatusRealTime_record = 0; //继电器实时状态记录游标
	static u32 spiFlashStatus_temp = 0;
#endif
/*---------------------------------------------------------------------------------------------*/

/*更新当前开关类型对应的有效操作位*/
u8 ICACHE_FLASH_ATTR
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

void ICACHE_FLASH_ATTR
portCtrlEachOther_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	memcpy(CTRLEATHER_PORT, datsRead_Temp->port_ctrlEachother, USRCLUSTERNUM_CTRLEACHOTHER);

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
devMAC_Reales(void){

	const u8 debug_staMAC[DEV_MAC_LEN] = {0x20, 0x20, 0x15, 0x22, 0x33, 0xAA};
	const u8 debug_apMAC[DEV_MAC_LEN] = {0x20, 0x20, 0x15, 0x22, 0x33, 0xAA};

//	wifi_get_macaddr(STATION_IF, MACSTA_ID); //获取staMAC
//	wifi_get_macaddr(SOFTAP_IF, MACAP_ID); //获取apMAC

	memcpy(MACSTA_ID, debug_staMAC, 6);
	memcpy(MACAP_ID, debug_apMAC, 6);
}

void ICACHE_FLASH_ATTR
devLockIF_Reales(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	(datsRead_Temp->dev_lockIF)?(deviceLock_flag = true):(deviceLock_flag = false);

	if(datsRead_Temp)os_free(datsRead_Temp);
}

void ICACHE_FLASH_ATTR
printf_datsHtoA(const u8 *TipsHead, u8 *dats , u8 datsLen){

	u8 dats_Log[DEBUG_LOGLEN] = {0};
	u8 loop = 0;

	memset(&dats_Log[0], 0, DEBUG_LOGLEN * sizeof(u8));
	for(loop = 0; loop < datsLen; loop ++){

		sprintf((char *)&dats_Log[loop * 3], "%02X ", *dats ++);
	}
	os_printf("%s<datsLen: %d> %s.\n", TipsHead, datsLen, dats_Log);
}

void ICACHE_FLASH_ATTR
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

		case obj_bkColor_swON:{

				dats_Temp.bkColor_swON = datsSave_Temp.bkColor_swON;
			
			}break;

		case obj_bkColor_swOFF:{

				dats_Temp.bkColor_swOFF = datsSave_Temp.bkColor_swOFF;
			
			}break;

		default:break;
	}
	
	spi_flash_write((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD) * SPI_FLASH_SEC_SIZE,
				    (u32 *)&dats_Temp,
				    sizeof(stt_usrDats_privateSave));

	flashWriteLocalDev_reserveFLG = true;
}

void ICACHE_FLASH_ATTR
devParam_SlaveZigbDevListInfoSave(stt_usrDats_zigbDevListInfo datsSave_Temp){ //网关下属zigb链表信息进行flash存储

	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD);
	spi_flash_write((DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD) * SPI_FLASH_SEC_SIZE,
				    (u32 *)&datsSave_Temp,
				    sizeof(stt_usrDats_zigbDevListInfo));
}

stt_usrDats_privateSave ICACHE_FLASH_ATTR
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

stt_usrDats_zigbDevListInfo ICACHE_FLASH_ATTR
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

#if(RELAYSTATUS_REALYTIME_ENABLEIF)
void ICACHE_FLASH_ATTR
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

u8 ICACHE_FLASH_ATTR
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

void ICACHE_FLASH_ATTR
devData_recoverFactory(void){

    //flash清空
	stt_usrDats_privateSave dats_Temp = {0};
	
	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_DEVLOCALINFO_RECORD);
	spi_flash_erase_sector(DATS_LOCATION_START + FLASH_USROPREATION_ADDR_OFFSET_SLAVEDEVLIST_RECORD);
	spi_flash_write((DATS_LOCATION_START + 0) * SPI_FLASH_SEC_SIZE,
				    (u32 *)&dats_Temp,
				    sizeof(stt_usrDats_privateSave));

	//默认值填装
	dats_Temp.bkColor_swON = TIPSBKCOLOR_DEFAULT_ON;
	dats_Temp.bkColor_swOFF = TIPSBKCOLOR_DEFAULT_OFF;
	devParam_flashDataSave(obj_bkColor_swON, dats_Temp);
	devParam_flashDataSave(obj_bkColor_swOFF, dats_Temp);
}

void ICACHE_FLASH_ATTR
devFactoryRecord_Opreat(void){

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();

	if(datsRead_Temp->FLG_factory_IF){

		devData_recoverFactory();
	}

	if(datsRead_Temp)os_free(datsRead_Temp);
}






