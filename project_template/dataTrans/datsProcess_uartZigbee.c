#include "datsProcess_uartZigbee.h"

#include "esp_common.h"

#include "uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "datsProcess_socketsNetwork.h"

#include "usrParsingMethod.h"
#include "datsManage.h"
#include "bsp_Hardware.h"
#include "timer_Activing.h"
#include "hwPeripherial_Actuator.h"

extern bool nwkInternetOnline_IF;
extern u16	sysTimeKeep_counter;

bool nwkZigbOnline_IF = false;	//zigb网络在线标志
u16  nwkZigb_currentPANID = 0;

u16	 sysZigb_random = 0x1234;

xQueueHandle xMsgQ_Zigb2Socket; //zigbee到socket数据中转消息队列
xQueueHandle xMsgQ_uartToutDats_dataSysRespond; //串口数据接收超时断帧数据队列-协议栈或系统回复数据
xQueueHandle xMsgQ_uartToutDats_dataRemoteComing; //串口数据接收超时断帧数据队列-远端数据
xQueueHandle xMsgQ_uartToutDats_rmDataReqResp; //串口数据接收超时断帧数据队列-远端数据请求后的应答
xQueueHandle xMsgQ_uartToutDats_scenarioCtrlResp;  //串口数据接收超时断帧数据队列-场景控制远端应答
xQueueHandle xMsgQ_timeStampGet; //网络时间戳获取消息队列
xQueueHandle xMsgQ_zigbFunRemind; //zigb功能触发消息队列

#define FifoFullIntr_lengthLimit	120 //fifo溢出值定义
#define TimeOutIntr_timeLimit	100 //超时接收时间定义
#define fifoLength  1024 //串口缓存长度

LOCAL uint16 uartBuff_baseInsert = 0; //缓存起始插入点,续收游标
LOCAL uint16 uartBuff_baseInsertStart = 0; //插入点起始值（当上一帧解析完成后还留有残帧且残帧头为FE时，则当前帧从残帧向后延续）
LOCAL uint8 fifoFull_CNT = 0; //串口溢出计次
LOCAL uint8 uart0_rxTemp[fifoLength] = {0};

stt_dataRemoteReq localZigbASYDT_bufQueueRemoteReq[zigB_remoteDataTransASY_QbuffLen] = {0}; //常规远端数据请求发送队列
stt_dataScenarioReq localZigbASYDT_bufQueueScenarioReq[zigB_ScenarioCtrlDataTransASY_QbuffLen] = {0}; //场景控制远端数据请求发送队列
u8 localZigbASYDT_scenarioCtrlReserveAllnum = 0; //当前剩余有效的场景操作单位数目

LOCAL xTaskHandle pxTaskHandle_threadZigbee;

LOCAL os_timer_t timer_zigbNodeDevDetectManage;
/*---------------------------------------------------------------------------------------------*/

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_Z2S(void){

	xMsgQ_Zigb2Socket = xQueueCreate(30, sizeof(stt_threadDatsPass));
	if(0 == xMsgQ_Zigb2Socket)return FAIL;
	else return OK;
}

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_uartToutDatsRcv(void){

	xMsgQ_uartToutDats_dataSysRespond = xQueueCreate(30, sizeof(sttUartRcv_sysDat));
	if(0 == xMsgQ_uartToutDats_dataSysRespond)return FAIL;
	else xMsgQ_uartToutDats_dataRemoteComing = xQueueCreate(50, sizeof(sttUartRcv_rmoteDatComming));
	if(0 == xMsgQ_uartToutDats_dataRemoteComing)return FAIL;
	else xMsgQ_uartToutDats_rmDataReqResp = xQueueCreate(30, sizeof(sttUartRcv_rmDatsReqResp));
	if(0 == xMsgQ_uartToutDats_rmDataReqResp)return FAIL;
	else xMsgQ_uartToutDats_scenarioCtrlResp = xQueueCreate(50, sizeof(sttUartRcv_scenarioCtrlResp));
	if(0 == xMsgQ_uartToutDats_scenarioCtrlResp)return FAIL;
	else return OK;
}

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_timeStampGet(void){

	xMsgQ_timeStampGet = xQueueCreate(5, sizeof(u32_t));
	if(0 == xMsgQ_timeStampGet)return FAIL;
	else return OK;
}

LOCAL STATUS ICACHE_FLASH_ATTR
appMsgQueueCreat_zigbFunRemind(void){

	xMsgQ_zigbFunRemind = xQueueCreate(5, sizeof(enum_zigbFunMsg));
	if(0 == xMsgQ_zigbFunRemind)return FAIL;
	else return OK;
}

/*zigbee节点设备链表重复节点优化去重*/ 
LOCAL void ICACHE_FLASH_ATTR
zigbDev_delSame(nwkStateAttr_Zigb *head)	
{
    nwkStateAttr_Zigb *p,*q,*r;
    p = head->next; 
    while(p != NULL)    
    {
        q = p;
        while(q->next != NULL) 
        {
            if(q->next->nwkAddr == p->nwkAddr || (!memcmp(q->next->macAddr, p->macAddr, DEVMAC_LEN) && (q->next->devType == p->devType))) 
            {
                r = q->next; 
                q->next = r->next;   
				os_free(r);
            }
            else
                q = q->next;
        }

        p = p->next;
    }
}

/*zigbee节点设备链表新增（注册）节点设备（将设备信息注册进链表）*/ 
LOCAL u8 ICACHE_FLASH_ATTR
zigbDev_eptCreat(nwkStateAttr_Zigb *pHead,nwkStateAttr_Zigb pNew){
	
	nwkStateAttr_Zigb *pAbove = pHead;
	nwkStateAttr_Zigb *pFollow;
	u8 nCount = 0;
	
	nwkStateAttr_Zigb *pNew_temp = (nwkStateAttr_Zigb *) os_zalloc(sizeof(nwkStateAttr_Zigb));
	pNew_temp->nwkAddr 				= pNew.nwkAddr;
	memcpy(pNew_temp->macAddr, pNew.macAddr, DEVMAC_LEN);
	pNew_temp->devType 				= pNew.devType;
	pNew_temp->onlineDectect_LCount = pNew.onlineDectect_LCount;
	pNew_temp->next	   = NULL;
	
	while(pAbove->next != NULL){
		
		nCount ++;
		pFollow = pAbove;
		pAbove	= pFollow->next;
	}
	
	pAbove->next = pNew_temp;
	return ++nCount;
}

/*zigbee节点提取从设备链表中，根据网络短地址;！！！谨记使用完节点信息后将内存释放！！！*/
LOCAL nwkStateAttr_Zigb ICACHE_FLASH_ATTR
*zigbDev_eptPutout_BYnwk(nwkStateAttr_Zigb *pHead,u16 nwkAddr,bool method){	//method = 1,源节点地址返回，操作返回内存影响源节点信息; method = 0,映射信息地址返回，操作返回内存，不影响源节点信息.
	
	nwkStateAttr_Zigb *pAbove = pHead;
	nwkStateAttr_Zigb *pFollow;
	
	nwkStateAttr_Zigb *pTemp = (nwkStateAttr_Zigb *)os_zalloc(sizeof(nwkStateAttr_Zigb));
	pTemp->next = NULL;
	
	while(!(pAbove->nwkAddr == nwkAddr) && pAbove->next != NULL){
		
		pFollow = pAbove;
		pAbove	= pFollow->next;
	}
	
	if(pAbove->nwkAddr == nwkAddr){
		
		if(!method){
			
			pTemp->nwkAddr 				= pAbove->nwkAddr;
			memcpy(pTemp->macAddr, pAbove->macAddr, DEVMAC_LEN);
			pTemp->devType 				= pAbove->devType;
			pTemp->onlineDectect_LCount = pAbove->onlineDectect_LCount;
		}else{
			
			os_free(pTemp);
			pTemp = pAbove;	
		}
		
		return pTemp;
	}else{
		
		os_free(pTemp);
		return NULL;
	}	
} 

/*zigbee节点提取从设备链表中，根据节点设备MAC地址和设备类型;！！！谨记使用完节点信息后将内存释放！！！*/
LOCAL nwkStateAttr_Zigb ICACHE_FLASH_ATTR
*zigbDev_eptPutout_BYpsy(nwkStateAttr_Zigb *pHead,u8 macAddr[DEVMAC_LEN],u8 devType,bool method){		//method = 1,源节点地址返回，操作返回内存影响源节点信息; method = 0,映射信息地址返回，操作返回内存，不影响源节点信息.
	nwkStateAttr_Zigb *pAbove = pHead;
	nwkStateAttr_Zigb *pFollow;
	
	nwkStateAttr_Zigb *pTemp = (nwkStateAttr_Zigb *)os_zalloc(sizeof(nwkStateAttr_Zigb));
	pTemp->next = NULL;
	
	while(!(!memcmp(pAbove->macAddr, macAddr, DEVMAC_LEN) && pAbove->devType == devType) && pAbove->next != NULL){
		
		pFollow = pAbove;
		pAbove	= pFollow->next;
	}
	
	if(!memcmp(pAbove->macAddr, macAddr, DEVMAC_LEN) && pAbove->devType == devType){
		
		if(!method){
			
			pTemp->nwkAddr 				= pAbove->nwkAddr;
			memcpy(pTemp->macAddr, pAbove->macAddr, DEVMAC_LEN);
			pTemp->devType 				= pAbove->devType;
			pTemp->onlineDectect_LCount = pAbove->onlineDectect_LCount;
		}else{

			os_free(pTemp);
			pTemp = pAbove;	
		}
		
		return pTemp;
	}else{
		
		os_free(pTemp);
		return NULL;
	}	
}

/*zigbee删除设备节点信息从链表中，根据节点设备网络短地址*/ 
LOCAL bool ICACHE_FLASH_ATTR
zigbDev_eptRemove_BYnwk(nwkStateAttr_Zigb *pHead,u16 nwkAddr){
	
	nwkStateAttr_Zigb *pAbove = pHead;
	nwkStateAttr_Zigb *pFollow;
	
	nwkStateAttr_Zigb *pTemp;
	
	while(!(pAbove->nwkAddr == nwkAddr) && pAbove->next != NULL){
		
		pFollow = pAbove;
		pAbove	= pFollow->next;
	}
	
	if(pAbove->nwkAddr == nwkAddr){
		
		pTemp = pAbove;
		pFollow->next = pAbove->next;
		os_free(pTemp);
		return true;
	}else{
		
		return false;
	}
}

/*zigbee删除设备节点信息从链表中，根据节点设备MAC地址和设备类型*/
LOCAL bool ICACHE_FLASH_ATTR
zigbDev_eptRemove_BYpsy(nwkStateAttr_Zigb *pHead,u8 macAddr[DEVMAC_LEN],u8 devType){
	
	nwkStateAttr_Zigb *pAbove = pHead;
	nwkStateAttr_Zigb *pFollow;
	
	nwkStateAttr_Zigb *pTemp;
	
	while(!(!memcmp(pAbove->macAddr, macAddr, DEVMAC_LEN) && pAbove->devType == devType) && pAbove->next != NULL){
		
		pFollow = pAbove;
		pAbove	= pFollow->next;
	}
	
	if(!memcmp(pAbove->macAddr, macAddr, DEVMAC_LEN) && pAbove->devType == devType){
		
		 pTemp = pAbove;
		 pFollow->next = pAbove->next;
		 os_free(pTemp);
		 return true;
	}else{
		
		return false;
	}
}

/*zigbee节点设备链表长度测量*/
LOCAL u8 ICACHE_FLASH_ATTR
zigbDev_chatLenDectect(nwkStateAttr_Zigb *pHead){
	
	nwkStateAttr_Zigb *pAbove = pHead;
	nwkStateAttr_Zigb *pFollow;
	u8 loop;
	
	while(pAbove->next != NULL){
		
		loop ++;
		pFollow = pAbove;
		pAbove	= pFollow->next;
	}

	return loop;
}

/*zigbee节点设备信息链表遍历，将所有节点设备类型和设备MAC地址打包输出*/
LOCAL u8 ICACHE_FLASH_ATTR
ZigBdevDispList(nwkStateAttr_Zigb *pHead,u8 DevInform[]){
	
	nwkStateAttr_Zigb *Disp = pHead;
	u8 loop = 0;
	
	if(0 == zigbDev_chatLenDectect(pHead)){
		
		return 0;
	}

	while(Disp->next != NULL){
	
		Disp = Disp->next;
		
		memcpy(&DevInform[loop * (DEVMAC_LEN + 1)], Disp->macAddr, DEVMAC_LEN);
		DevInform[loop * (DEVMAC_LEN + 1) + 5] = Disp->devType;
		loop ++;
	}
	
	return loop;
}


LOCAL STATUS
uartTX_char(uint8 uart, uint8 TxChar)
{
    while (true) {
        uint32 fifo_cnt = READ_PERI_REG(UART_STATUS(uart)) & (UART_TXFIFO_CNT << UART_TXFIFO_CNT_S);

        if ((fifo_cnt >> UART_TXFIFO_CNT_S & UART_TXFIFO_CNT) < 126) {
            break;
        }
    }

    WRITE_PERI_REG(UART_FIFO(uart) , TxChar);
    return OK;
}

LOCAL STATUS
uart_putDats(uint8 uart, char *dats, uint8 datsLen){

	while(datsLen --){

		uartTX_char(uart, *dats ++);
	}
}

LOCAL void
myUart0datsTrans_intr_funCB(void *para){

	uint8 RcvChar;
	uint8 uart_no = UART0;//UartDev.buff_uart_no;
	uint8 fifo_Num = 0;
	uint8 buf_idx = 0;

	uint32 uart_intr_status = READ_PERI_REG(UART_INT_ST(uart_no)) ;

	while (uart_intr_status != 0x0) {
		if (UART_FRM_ERR_INT_ST == (uart_intr_status & UART_FRM_ERR_INT_ST)) {
			//os_printf("FRM_ERR\r\n");
			WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_FRM_ERR_INT_CLR);
		} 
		else if (UART_RXFIFO_FULL_INT_ST == (uart_intr_status & UART_RXFIFO_FULL_INT_ST)) { //溢出接头
//			os_printf(">>>>>>>>>>>>fifo full.\n");
			fifo_Num = (READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT;
			buf_idx = 0;

			if(!fifoFull_CNT){ //若单周期从未溢出，则进行相关值初始化

				memset(&uart0_rxTemp[uartBuff_baseInsertStart], 0, (fifoLength - uartBuff_baseInsertStart) * sizeof(uint8));
				uartBuff_baseInsert = uartBuff_baseInsertStart; //续收游标初始化
			}

			while (buf_idx < fifo_Num) { //取溢出之内数据
				
				uart0_rxTemp[buf_idx + uartBuff_baseInsert] = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
//				uartTX_char(UART0, READ_PERI_REG(UART_FIFO(UART0)) & 0xFF);
				buf_idx++;
			}

			uartBuff_baseInsert += FifoFullIntr_lengthLimit; //续收游标更新
			fifoFull_CNT ++; //溢出次数更新

			WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
		} 
		else if (UART_RXFIFO_TOUT_INT_ST == (uart_intr_status & UART_RXFIFO_TOUT_INT_ST)) { //超时截漏
//			os_printf("tout\r\n");
			
			fifo_Num = (READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT;
			buf_idx = 0;

			u16 uartBuff_rcvLength = 0; //单周期串口接收到的缓存总长度（中转）
			
			if(!fifoFull_CNT){ //单周期内，fifo溢出次数是否为零

				memset(&uart0_rxTemp[uartBuff_baseInsertStart], 0, (fifoLength - uartBuff_baseInsertStart) * sizeof(uint8)); //单个接收周期内若非存在溢出，则缓存清零
				uartBuff_baseInsert =  uartBuff_baseInsertStart; //续收游标初始化
				uartBuff_rcvLength = uartBuff_baseInsertStart + fifo_Num; //单周期缓存接收数据总长度更新
			}
			else{

				uartBuff_rcvLength = uartBuff_baseInsertStart + fifo_Num + fifoFull_CNT * FifoFullIntr_lengthLimit; //单周期缓存接收数据总长度更新
				fifoFull_CNT = 0; //溢出次数清零，超时收尾，必清零
			}

			while (buf_idx < fifo_Num) { //取超时之前所接收到的数据
				
				uart0_rxTemp[buf_idx + uartBuff_baseInsert] = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
//				uartTX_char(UART0, READ_PERI_REG(UART_FIFO(UART0)) & 0xFF);
				buf_idx++;
			}

			{ //按条件填装队列

				const u8 cmd_remoteDataComing[2] 	= {0x44, 0x81};
				const u8 cmd_remoteNodeOnline[2] 	= {0x45, 0xCA};
				const u8 cmd_rmdataReqResp[2] 		= {0x44, 0x80}; 
				u16 frameNum_reserve = uartBuff_rcvLength;
				u16 frameHead_insert = 0; //帧头索引
				u8 frameParsing_num = 0; //已解析的数据帧数量
				u8 frameTotal_Len = 0; //单总帧长缓存

				while(frameNum_reserve){

					sttUartRcv_rmoteDatComming mptr_rmoteDatComming;
					sttUartRcv_sysDat mptr_sysDat;
					sttUartRcv_rmDatsReqResp mptr_rmDatsReqResp;
					sttUartRcv_scenarioCtrlResp mptr_scenarioCtrlResp;

					frameTotal_Len = uart0_rxTemp[frameHead_insert + 1] + 5; //单帧长更新（拆包）
				
					if((uart0_rxTemp[frameHead_insert] == ZIGB_FRAME_HEAD) && (frameNum_reserve >= frameTotal_Len)){ //超时断帧多收包只要断帧帧头为FE就没事，可做多重解析，重要的是帧头不是FE就有事

						frameParsing_num ++;

						if(!memcmp(&uart0_rxTemp[frameHead_insert + 2], cmd_remoteDataComing, 2) || !memcmp(&uart0_rxTemp[frameHead_insert + 2], cmd_remoteNodeOnline, 2)){ //收到远端数据填装
						
							memcpy(mptr_rmoteDatComming.dats, &uart0_rxTemp[frameHead_insert], frameTotal_Len);
							mptr_rmoteDatComming.datsLen = frameTotal_Len;

							if(uart0_rxTemp[frameHead_insert + 21] == ZIGB_FRAMEHEAD_CTRLLOCAL && uart0_rxTemp[frameHead_insert + 24] == FRAME_MtoSCMD_cmdConfigSearch){ //搜索码回码优先处理

								xQueueSendToFront(xMsgQ_uartToutDats_dataRemoteComing, (void *)&mptr_rmoteDatComming, ( portTickType ) 0);
								
							}else
							if(uart0_rxTemp[frameHead_insert + 21] == CTRLSECENARIO_RESPCMD_SPECIAL && frameTotal_Len == 26){ //场景控制远端响应回复

								mptr_scenarioCtrlResp.respNwkAddr = (((u16)uart0_rxTemp[frameHead_insert + 8] & 0x00FF) << 0) + (((u16)uart0_rxTemp[frameHead_insert + 9] & 0x00FF) << 8); //远端响应短地址加载
								xQueueSend(xMsgQ_uartToutDats_scenarioCtrlResp, (void *)&mptr_scenarioCtrlResp, ( portTickType ) 0);
//								os_printf("Q_s push.\n");
								
							}else{

								xQueueSend(xMsgQ_uartToutDats_dataRemoteComing, (void *)&mptr_rmoteDatComming, ( portTickType ) 0);
							}

//							os_printf("Q_r push.\n");
							
						}
						else
						if(!memcmp(&uart0_rxTemp[frameHead_insert + 2], cmd_rmdataReqResp, 2)){ //远端数据请求专用应答队列填装
						
							memcpy(mptr_rmDatsReqResp.dats, &uart0_rxTemp[frameHead_insert], frameTotal_Len);
							mptr_rmDatsReqResp.datsLen = frameTotal_Len;

							xQueueSend(xMsgQ_uartToutDats_rmDataReqResp, (void *)&mptr_rmDatsReqResp, ( portTickType ) 0);

						}else{ //剩下的不分类了，都是系统响应数据
						
							memcpy(mptr_sysDat.dats, &uart0_rxTemp[frameHead_insert], frameTotal_Len);
							mptr_sysDat.datsLen = frameTotal_Len;

							xQueueSend(xMsgQ_uartToutDats_dataSysRespond, (void *)&mptr_sysDat, ( portTickType ) 0);
//							os_printf("Q_s push.\n");

						}

						frameHead_insert += frameTotal_Len; //索引更新
						frameNum_reserve -= frameTotal_Len; //剩余数据长度更新
						uartBuff_baseInsertStart = 0; //完整帧情况下，串口缓存插入点起始值直接为0
						
					}
					else{

//						os_printf("tout_err.\n");
						if(frameNum_reserve > 0){

							if(uart0_rxTemp[frameHead_insert] == ZIGB_FRAME_HEAD){ //有残帧且帧头合法，那么就是合法残帧，进行处理

								memcpy(uart0_rxTemp, &uart0_rxTemp[frameHead_insert], frameNum_reserve); //合法残帧保留到下一帧合并
								uartBuff_baseInsertStart = frameNum_reserve; //串口缓存插入点起始值更新
								frameNum_reserve = 0;
								
							}else{ //进行非法残帧处理

								u8 	loop = 0;
								bool frameHead_bingo = false;
								for(loop = 1; loop < frameTotal_Len; loop ++){ //上一帧标的长度范围内回溯寻找正确帧头（范围内不包含原帧头，不能重复回溯），再没找到就抛弃（实际串口数据流观察中发现，连包内小几率会出现缺漏字节的残帧混杂其中，即标的帧长与实际帧长不一致，为保证残包后的完整包不被浪费，作此业务逻辑）

									if(uart0_rxTemp[frameHead_insert - loop] == ZIGB_FRAME_HEAD){

										frameHead_bingo = true;
										break;
									}
								}
								if(frameHead_bingo == true){

									frameHead_insert -= loop; //数据索引更新
									frameNum_reserve += loop; //数据长度剩余数更新
									continue;
									
								}else{

									uartBuff_baseInsertStart = 0; //未找到合法帧头，抛弃残帧
									frameNum_reserve = 0; //数据长度剩余数强制清零
								}	
							}

							os_printf(">>>>>>>>>errFrame tail:%02X-%02X-%02X, legal_impStart:%02X.\n",	uart0_rxTemp[uartBuff_rcvLength - 3],
																										uart0_rxTemp[uartBuff_rcvLength - 2],
																										uart0_rxTemp[uartBuff_rcvLength - 1],
																										uartBuff_baseInsertStart);
						}
					}				
				}

//				if(uart0_rxTemp[1] != (fifo_Num - 5)){
//				
//					os_printf("F_Head:%02X, A_Len:%02X(+5), R_Len:%04X, frame_P:%02d.\n", uart0_rxTemp[0], uart0_rxTemp[1], uartBuff_rcvLength, frameParsing_num);
//				}
			}

//			printf_datsHtoA("[Tips_uart]:", uart0_rxTemp, fifo_Num);

			if(NULL != usr_memmem(uart0_rxTemp, fifo_Num, "hellow mf", 9))
				uart_putDats(UART0, "hellow fy\n", 10);

			WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
		} 
		else if (UART_TXFIFO_EMPTY_INT_ST == (uart_intr_status & UART_TXFIFO_EMPTY_INT_ST)) {
			os_printf("empty\n\r");
			WRITE_PERI_REG(UART_INT_CLR(uart_no), UART_TXFIFO_EMPTY_INT_CLR);
			CLEAR_PERI_REG_MASK(UART_INT_ENA(UART0), UART_TXFIFO_EMPTY_INT_ENA);
		} else {
			//skip
		}

		uart_intr_status = READ_PERI_REG(UART_INT_ST(uart_no));
	}

}

void ICACHE_FLASH_ATTR
uart0Init_datsTrans(void){

	UART_WaitTxFifoEmpty(UART0);

	UART_ConfigTypeDef uart_config;
	UART_IntrConfTypeDef uart_intr;

    uart_config.baud_rate     		= BIT_RATE_115200;
    uart_config.data_bits     		= UART_WordLength_8b;
    uart_config.parity       	 	= USART_Parity_None;
    uart_config.stop_bits     		= USART_StopBits_1;
    uart_config.flow_ctrl      		= USART_HardwareFlowControl_None;
    uart_config.UART_RxFlowThresh 	= 0;
    uart_config.UART_InverseMask 	= UART_None_Inverse;
	UART_ParamConfig(UART0, &uart_config);
	
//	uart_intr.UART_IntrEnMask = UART_RXFIFO_TOUT_INT_ENA | UART_FRM_ERR_INT_ENA | UART_RXFIFO_FULL_INT_ENA | UART_TXFIFO_EMPTY_INT_ENA;
//	uart_intr.UART_RX_FifoFullIntrThresh = 10;
//	uart_intr.UART_RX_TimeOutIntrThresh = 2;
//	uart_intr.UART_TX_FifoEmptyIntrThresh = 20;

	uart_intr.UART_IntrEnMask = UART_RXFIFO_TOUT_INT_ENA | UART_RXFIFO_FULL_INT_ENA; //只是能fifo溢出中断 与 超时中断
	uart_intr.UART_RX_FifoFullIntrThresh = FifoFullIntr_lengthLimit;
	uart_intr.UART_RX_TimeOutIntrThresh = TimeOutIntr_timeLimit; // 2单位超时已是调试最小值，继续减小将会导致提前断帧,(tips:超时断帧多收包只要断帧帧头为FE就没事，重要的是帧头不是FE就有事，也就是说超时时间大一点没关系，不要太小了，太小了就会产生残帧)

	UART_IntrConfig(UART0, &uart_intr);

	UART_intr_handler_register(myUart0datsTrans_intr_funCB, NULL);
	ETS_UART_INTR_ENABLE();

	uart_putDats(UART0, "hellow world!!!\n", 16);
}

/*数据异或校验*///ZNP协议帧
LOCAL uint8 ICACHE_FLASH_ATTR
XORNUM_CHECK(u8 buf[], u8 length){

	uint8 loop = 0;
	uint8 valXOR = buf[0];
	
	for(loop = 1;loop < length;loop ++)valXOR ^= buf[loop];
	
	return valXOR;
}

/*zigbee数据帧加载*/
LOCAL uint8 ICACHE_FLASH_ATTR
ZigB_TXFrameLoad(uint8 frame[],uint8 cmd[],uint8 cmdLen,uint8 dats[],uint8 datsLen){		

	const uint8 frameHead = ZIGB_FRAME_HEAD;	//ZNP,SOF帧头.
	uint8 xor_check = datsLen;					//异或校验，帧尾
	uint8 loop = 0;
	uint8 ptr = 0;
	
	frame[ptr ++] = frameHead;
	frame[ptr ++] = datsLen;
	
	memcpy(&frame[ptr],cmd,cmdLen);
	ptr += cmdLen;
	for(loop = 0;loop < cmdLen;loop ++)xor_check ^= cmd[loop];

	memcpy(&frame[ptr],dats,datsLen);
	ptr += datsLen;
	for(loop = 0;loop < datsLen;loop ++)xor_check ^= dats[loop];	
	
	frame[ptr ++] = xor_check;
	
	return ptr;
}

/*zigbee系统控制帧帧加载*/
LOCAL uint8 ICACHE_FLASH_ATTR
ZigB_sysCtrlFrameLoad(u8 datsTemp[], frame_zigbSysCtrl dats){

	datsTemp[0] = dats.command;
	datsTemp[1] = dats.datsLen;
	memcpy((char *)&datsTemp[2], (char *)dats.dats, dats.datsLen);
}

/*zigbee单指令数据请求*/
bool ICACHE_FLASH_ATTR 
zigb_datsRequest(u8 frameREQ[],	//请求帧
					  u8 frameREQ_Len,	//请求帧长度
					  u8 resp_cmd[2],	//预期应答指令
					  datsZigb_reqGet *datsRX,	//预期应答数据
					  u16 timeWait, //超时时间
					  remoteDataReq_method method){ //是否死磕

	sttUartRcv_sysDat rptr_uartDatsRcv;
	portBASE_TYPE xMsgQ_rcvResult = pdFALSE;
	uint16 	datsRcv_tout= timeWait;

	if(!method.keepTxUntilCmp_IF)uartZigbee_putDats(UART0, frameREQ, frameREQ_Len); //非死磕，超时前只发一次
	
	while(datsRcv_tout --){

		vTaskDelay(1);

		if(method.keepTxUntilCmp_IF){ //死磕模式下周期性发送指令

			if((datsRcv_tout % method.datsTxKeep_Period) == 0)uartZigbee_putDats(UART0, frameREQ, frameREQ_Len);
		}
	
		xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataSysRespond, (void *)&rptr_uartDatsRcv,	(portTickType) 0);
		while(xMsgQ_rcvResult == pdTRUE){
			
			if((rptr_uartDatsRcv.dats[0] == ZIGB_FRAME_HEAD) &&
			   (rptr_uartDatsRcv.dats[rptr_uartDatsRcv.datsLen - 1] == XORNUM_CHECK(&(rptr_uartDatsRcv.dats[1]), rptr_uartDatsRcv.datsLen - 2)) &&
			   (!memcmp(&(rptr_uartDatsRcv.dats[2]), resp_cmd, 2))
			   ){

					memcpy(datsRX->cmdResp, &rptr_uartDatsRcv.dats[2], 2);
					memcpy(datsRX->frameResp, rptr_uartDatsRcv.dats, rptr_uartDatsRcv.datsLen);
					datsRX->frameRespLen = rptr_uartDatsRcv.datsLen;
					return true;
			}
			xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataSysRespond, (void *)&rptr_uartDatsRcv,	(portTickType) 0);
		}
	}

	vTaskDelay(1);

	return false;
}

/*zigbee单指令下发及响应验证*/
bool ICACHE_FLASH_ATTR 
zigb_VALIDA_INPUT(uint8 REQ_CMD[2],			//指令
				       uint8 REQ_DATS[],	//数据
				       uint8 REQdatsLen,	//数据长度
				       uint8 ANSR_frame[],	//响应帧
				       uint8 ANSRdatsLen,	//响应帧长度
				       uint8 times,uint16 timeDelay){//循环次数，单次等待时间
					   
#define zigbDatsTransLen 128

//	uint8 	dataTXBUF[zigbDatsTransLen] = {0};
	uint8 	*dataTXBUF = (u8 *)os_zalloc(sizeof(u8) * zigbDatsTransLen);
	uint8 	loop = 0;
	uint8 	datsTX_Len;
	uint16 	local_timeDelay = timeDelay;
	sttUartRcv_sysDat rptr_uartDatsRcv;
	portBASE_TYPE xMsgQ_rcvResult = pdFALSE;

	bool	result_REQ = false;
	
	datsTX_Len = ZigB_TXFrameLoad(dataTXBUF, REQ_CMD, 2, REQ_DATS, REQdatsLen);

	for(loop = 0;loop < times;loop ++){

		u16 datsRcv_tout = timeDelay;

		uartZigbee_putDats(UART0, dataTXBUF, datsTX_Len);
		while(datsRcv_tout --){

			vTaskDelay(1);
			xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataSysRespond, (void *)&rptr_uartDatsRcv, (portTickType) 0);
			while(xMsgQ_rcvResult == pdTRUE){	//遍历响应队列查找
			
				if(usr_memmem(rptr_uartDatsRcv.dats, rptr_uartDatsRcv.datsLen, ANSR_frame, ANSRdatsLen)){

					result_REQ = true;
					break;
				}
				xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataSysRespond, (void *)&rptr_uartDatsRcv,	(portTickType) 0);
			}if(true == result_REQ)break;
		}if(true == result_REQ)break;
	}

	os_free(dataTXBUF);
	
	return result_REQ;
}

/*zigbee通信簇注册*/
LOCAL bool ICACHE_FLASH_ATTR 
zigb_clusterSet(u16 deviveID, u8 endPoint){

	const datsAttr_ZigbInit default_param = {{0x24,0x00},{0x0E,0x0D,0x00,0x0D,0x00,0x0D,0x00,0x01,0x00,0x00,0x01,0x00,0x00},0x0D,{0xFE,0x01,0x64,0x00,0x00,0x65},0x06,20}; //数据簇注册，默认参数
	const u8 frameResponse_Subs[6] = {0xFE,0x01,0x64,0x00,0xB8,0xDD}; //响应替补帧，若数据簇已被注册
	
#define paramLen_clusterSet 100

	u8 paramTX_temp[paramLen_clusterSet] = {0};

	bool resultSet = false;
	
	memcpy(paramTX_temp, default_param.zigbInit_reqDAT, default_param.reqDAT_num);
	paramTX_temp[0] = endPoint;
	paramTX_temp[3] = (uint8)((deviveID & 0x00ff) >> 0);
	paramTX_temp[4] = (uint8)((deviveID & 0xff00) >> 8);
	
	resultSet = zigb_VALIDA_INPUT((u8 *)default_param.zigbInit_reqCMD,
								  (u8 *)paramTX_temp,
								  default_param.reqDAT_num,
								  (u8 *)default_param.zigbInit_REPLY,
								  default_param.REPLY_num,
								  2, 	//2次以内无正确响应则失败
								  default_param.timeTab_waitAnsr);

	if(resultSet)return resultSet;
	else{

		return zigb_VALIDA_INPUT((u8 *)default_param.zigbInit_reqCMD,
								 (u8 *)paramTX_temp,
								 default_param.reqDAT_num,
								 (u8 *)frameResponse_Subs,
								 6,
								 2, 	//2次以内无正确响应则失败
								 default_param.timeTab_waitAnsr);

	}
}

/*zigbee多通信簇注册*/
LOCAL bool ICACHE_FLASH_ATTR 
zigb_clusterMultiSet(devDatsTrans_portAttr devPort[], u8 Len){

	u8 loop = 0;

	for(loop = 0; loop < Len; loop ++){

		if(!zigb_clusterSet(devPort[loop].deviveID, devPort[loop].endPoint))return false;
	}

	return true;
}

/*zigbee互控通讯簇注册*/
LOCAL bool ICACHE_FLASH_ATTR 
zigb_clusterCtrlEachotherCfg(void){

	u8 loop = 0;
	bool config_Result = false;

	for(loop = 0; loop < USRCLUSTERNUM_CTRLEACHOTHER; loop ++){

		if(CTRLEATHER_PORT[loop] > 0x10 && CTRLEATHER_PORT[loop] < 0xFF){

			config_Result = zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, CTRLEATHER_PORT[loop]);
			if(!config_Result)break;
			
		}else{

			config_Result = true;
		}
	}

	return config_Result;
}

/*zigbee网络开放及关闭操作*/
LOCAL bool ICACHE_FLASH_ATTR
zigbNetwork_OpenIF(bool opreat_Act, u8 keepTime){

	const datsAttr_ZigbInit default_param = {{0x26,0x08}, {0xFF,0xFF,0x00}, 0x03, {0xFE,0x01,0x66,0x08,0x00,0x6F}, 0x06, 500};	//命令帧，默认参数
#define nwOpenIF_paramLen 64

	bool result_Set = false;

	u8 paramTX_temp[nwOpenIF_paramLen] = {0};
	
	memcpy(paramTX_temp,default_param.zigbInit_reqDAT,default_param.reqDAT_num);
	if(true == opreat_Act)paramTX_temp[2] = keepTime;
	else paramTX_temp[2] = 0x00;
	
	result_Set = zigb_VALIDA_INPUT(	(u8 *)default_param.zigbInit_reqCMD,
									(u8 *)paramTX_temp,
									default_param.reqDAT_num,
									(u8 *)default_param.zigbInit_REPLY,
									default_param.REPLY_num,
									3,		//3次以内无正确响应则失败
									default_param.timeTab_waitAnsr);	

	if(result_Set)os_printf("[Tips_uartZigb]: zigb nwkOpreat success.\n");
	else os_printf("[Tips_uartZigb]: zigbSystime nwkOpreat fail.\n");

	return result_Set;
}

/*zigbee系统时间设置*/
LOCAL bool ICACHE_FLASH_ATTR
zigB_sysTimeSet(u32_t timeStamp){

	const datsAttr_ZigbInit default_param = {{0x21,0x10},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},0x0B,{0xFE,0x01,0x61,0x10,0x00},0x05,30}; //zigbee系统时间设置，默认参敿
	u8 timeStampArray[11] = {0};
	bool resultSet = false;
	u32_t timeStamp_temp = timeStamp;

	if(sysTimeZone_H <= 12){
	
		timeStamp_temp += (3600UL * (long)sysTimeZone_H + 60UL * (long)sysTimeZone_M); //时区正
		
	}else
	if(sysTimeZone_H > 12 && sysTimeZone_H <= 24){
	
		timeStamp_temp -= (3600UL * (long)(sysTimeZone_H - 12) + 60UL * (long)sysTimeZone_M); //时区负
		
	}else
	if(sysTimeZone_H == 30 || sysTimeZone_H == 31){ 
		
		timeStamp_temp += (3600UL * (long)(sysTimeZone_H - 17) + 60UL * (long)sysTimeZone_M); //时区特殊
	}
	
	timeStampArray[0] = (u8)((timeStamp_temp & 0x000000ff) >> 0);
	timeStampArray[1] = (u8)((timeStamp_temp & 0x0000ff00) >> 8);
	timeStampArray[2] = (u8)((timeStamp_temp & 0x00ff0000) >> 16);
	timeStampArray[3] = (u8)((timeStamp_temp & 0xff000000) >> 24);
	
	resultSet = zigb_VALIDA_INPUT((u8 *)default_param.zigbInit_reqCMD,
								  (u8 *)timeStampArray,
								  11,
								  (u8 *)default_param.zigbInit_REPLY,
								  default_param.REPLY_num,
								  2,	//2次以内无正确响应则失败
								  default_param.timeTab_waitAnsr);
	
//	os_printf("[Tips_uartZigb]: zigbee sysTime set result: %d.\n", resultSet);
	if(resultSet){

//		os_printf("[Tips_uartZigb]: zigbSystime set success.\n");
	}
	else os_printf("[Tips_uartZigb]: zigbSystime set fail.\n");

	return resultSet;

}

/*zigbee系统时间获取*/
LOCAL bool ICACHE_FLASH_ATTR
zigB_sysTimeGetRealesWithLocal(void){

	u32_t timeStamp_temp = 0;
	datsZigb_reqGet *local_datsParam = (datsZigb_reqGet *)os_zalloc(sizeof(datsZigb_reqGet));
	const u8 frameREQ_zigbSysTimeGet[5] = {0xFE, 0x00, 0x21, 0x11, 0x30};	//zigb系统时间获取指令帧
	const u8 cmdResp_zigbSysTimeGet[2] 	= {0x61, 0x11};	//zigb系统时间获取预期响应指令
	bool resultREQ = false;
	const remoteDataReq_method datsReq_method = {0};

	resultREQ = zigb_datsRequest((u8 *)frameREQ_zigbSysTimeGet,
								 5,
								 (u8 *)cmdResp_zigbSysTimeGet,
								 local_datsParam,
								 30,
								 datsReq_method);

	if(true == resultREQ){

		/*本地系统UTC更新*/
		timeStamp_temp |= (((u32_t)(local_datsParam->frameResp[4]) << 0)  & 0x000000FF);
		timeStamp_temp |= (((u32_t)(local_datsParam->frameResp[5]) << 8)  & 0x0000FF00);
		timeStamp_temp |= (((u32_t)(local_datsParam->frameResp[6]) << 16) & 0x00FF0000);
		timeStamp_temp |= (((u32_t)(local_datsParam->frameResp[7]) << 24) & 0xFF000000);
		systemUTC_current = timeStamp_temp + ZIGB_UTCTIME_START;  //zigb系统协议UTC补偿

		/*本地系统格式时间更新*/
		u16 Y_temp16 = ((u16)local_datsParam->frameResp[13] << 0) | ((u16)local_datsParam->frameResp[14] << 8);
		u8  Y_temp8 = 0;
		u8  M_temp8 = 0;
		
		u8 Y = (u8)(Y_temp16 % 2000);
		u8 M = local_datsParam->frameResp[11];
		u8 D = local_datsParam->frameResp[12];
		u8 W = 0;
		
		/*计算缓存赋*/
		Y_temp8 = Y;
		if(M == 1 || M == 2){ //一月和二月当作去年十三月和十四月
		
			M_temp8 = M + 12;
			Y_temp8 --;
		}
		else M_temp8 = M;
		
		/*开始计算*/
		W =	 Y_temp8 + (Y_temp8 / 4) + 5 - 40 + (26 * (M_temp8 + 1) / 10) + D - 1;	//蔡勒公式
		W %= 7; 
		
		/*计算结果赋值*/
		W?(systemTime_current.time_Week = W):(systemTime_current.time_Week = 7);
		
		systemTime_current.time_Month = 	M;
		systemTime_current.time_Day = 		D;
		systemTime_current.time_Year = 		Y;
		
		systemTime_current.time_Hour = 		local_datsParam->frameResp[8];
		systemTime_current.time_Minute =	local_datsParam->frameResp[9];
		systemTime_current.time_Second = 	local_datsParam->frameResp[10];

		/*系统本地时间维持计时值校准更新*/
		sysTimeKeep_counter = systemTime_current.time_Minute * 60 + systemTime_current.time_Second; //本地时间维持更新

//		printf_datsHtoA("[Tips_uartZigb]: resultDats:", local_datsParam->frameResp, local_datsParam->frameRespLen);
	}

	os_free(local_datsParam);
	
//	return timeStamp_temp;
	return resultREQ;
}

/*zigbee硬件初始化复位*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_resetInit(void){

//	portTickType xLastWakeTime;
//	portTickType xFrequencyDelay = 0;

//	usrDats_actuator.conDatsOut_ZigbeeRst = 0;
//	xLastWakeTime = xTaskGetTickCount();
//	xFrequencyDelay = 100;
//	vTaskDelayUntil(&xLastWakeTime, xFrequencyDelay);
//	usrDats_actuator.conDatsOut_ZigbeeRst = 1;
//	xLastWakeTime = xTaskGetTickCount();
//	xFrequencyDelay = 300;
//	vTaskDelayUntil(&xLastWakeTime, xFrequencyDelay);

	#define zigbInit_loopTry 	3
	#define zigbInit_onceWait 	500
	
#if(ZNP_TARGET_DEVICE == ZNPDEVICE_CC2530)
	const u8 initCmp_Frame[11] = {0xFE, 0x06, 0x41, 0x80, 0x01, 0x02, 0x00, 0x02, 0x06, 0x03, 0xC3};
#elif(ZNP_TARGET_DEVICE == ZNPDEVICE_CC2538)
	const u8 initCmp_Frame[11] = {0xFE, 0x06, 0x41, 0x80, 0x00, 0x02, 0x00, 0x02, 0x06, 0x03, 0xC2};
#endif

	u8 	loop = 0;
	u16 timeWait = 0;
	sttUartRcv_sysDat rptr_uartDatsRcv;
	portBASE_TYPE xMsgQ_rcvResult = pdFALSE;
	bool result_Init = false;

	for(loop = 0; loop < zigbInit_loopTry; loop ++){

		usrDats_actuator.conDatsOut_ZigbeeRst = ZIGB_HARDWARE_RESET_LEVEL;
		vTaskDelay(10);
		usrDats_actuator.conDatsOut_ZigbeeRst = ZIGB_HARDWARE_NORMAL_LEVEL;

		os_printf("[Tips_uartZigb]: Zigbee hwReset try loop : %d\n", loop + 1);

		timeWait = zigbInit_onceWait;
		while(timeWait --){

			vTaskDelay(1);
			xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataSysRespond, (void *)&rptr_uartDatsRcv,  (portTickType) 0);
			while(xMsgQ_rcvResult == pdTRUE){

				if(!memcmp(rptr_uartDatsRcv.dats, initCmp_Frame, 11)){

					result_Init = true;
					break;
				}
				xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataSysRespond, (void *)&rptr_uartDatsRcv,  (portTickType) 0);
			}if(result_Init == true)break;
		}if(result_Init == true)break;
	}

	if(result_Init)os_printf("[Tips_uartZigb]: Zigbee hwReset success!\n");
	else os_printf("[Tips_uartZigb]: Zigbee hwReset fail!\n");

	return result_Init;
}

/*zigbee PANID获取*/
LOCAL u16 ICACHE_FLASH_ATTR
ZigB_getPanIDCurrent(void){

	u16 PANID_temp = 0;
	datsZigb_reqGet *local_datsParam = (datsZigb_reqGet *)os_zalloc(sizeof(datsZigb_reqGet));
	const u8 frameREQ_zigbPanIDGet[6] 	= {0xFE, 0x01, 0x26, 0x06, 0x06, 0x27};	//zigb PANID获取指令帧
	const u8 cmdResp_zigbPanIDGet[2] 	= {0x66, 0x06};	//zigb PANID获取预期响应指令
	bool resultREQ = false;
	const remoteDataReq_method datsReq_method = {0};

	resultREQ = zigb_datsRequest((u8 *)frameREQ_zigbPanIDGet,
								 6,
								 (u8 *)cmdResp_zigbPanIDGet,
								 local_datsParam,
								 30,
								 datsReq_method);

	if(true == resultREQ){

		PANID_temp |= (((u16)(local_datsParam->frameResp[5]) << 0)	& 0x00FF);
		PANID_temp |= (((u16)(local_datsParam->frameResp[6]) << 8)	& 0xFF00);

//		printf_datsHtoA("[Tips_uartZigb]: resultDats:", local_datsParam->frameResp, local_datsParam->frameRespLen);
	}

	os_free(local_datsParam);
	return PANID_temp;
}

/*zigbee IEEE地址获取*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_getIEEEAddr(void){

	datsZigb_reqGet *local_datsParam = (datsZigb_reqGet *)os_zalloc(sizeof(datsZigb_reqGet));
	const u8 frameREQ_zigbPanIDGet[6] 	= {0xFE, 0x01, 0x26, 0x06, 0x01, 0x20};	//zigb IEEE地址获取指令帧
	const u8 cmdResp_zigbPanIDGet[2] 	= {0x66, 0x06};	//zigb IEEE地址获取预期响应指令
	bool resultREQ = false;
	const remoteDataReq_method datsReq_method = {0};

	resultREQ = zigb_datsRequest((u8 *)frameREQ_zigbPanIDGet,
								 6,
								 (u8 *)cmdResp_zigbPanIDGet,
								 local_datsParam,
								 30,
								 datsReq_method);

	if(true == resultREQ){

		u16 loop = 0;

		for(loop = 0; loop < DEV_MAC_LEN; loop ++){

			MACSTA_ID[DEV_MAC_LEN - loop - 1] = local_datsParam->frameResp[5 + loop];
		}
	
//		printf_datsHtoA("[Tips_uartZigb]: resultDats:", local_datsParam->frameResp, local_datsParam->frameRespLen);
	}

	os_free(local_datsParam);
	return resultREQ;
}

/*zigbee IEEE地址获取*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_getRandom(void){

	u16 PANID_temp = 0;
	datsZigb_reqGet *local_datsParam = (datsZigb_reqGet *)os_zalloc(sizeof(datsZigb_reqGet));
	const u8 frameREQ_zigbPanIDGet[5] 	= {0xFE, 0x00, 0x21, 0x0C, 0x2D};	//zigb 系统随机数获取指令帧
	const u8 cmdResp_zigbPanIDGet[2] 	= {0x61, 0x0C};	//zigb 系统随机数获取预期响应指令
	bool resultREQ = false;
	const remoteDataReq_method datsReq_method = {0};

	resultREQ = zigb_datsRequest((u8 *)frameREQ_zigbPanIDGet,
								 5,
								 (u8 *)cmdResp_zigbPanIDGet,
								 local_datsParam,
								 30,
								 datsReq_method);

	if(true == resultREQ){

		sysZigb_random = 0;

		sysZigb_random |= (u16)local_datsParam->frameResp[4] << 0;
		sysZigb_random |= (u16)local_datsParam->frameResp[5] << 8;

		sysZigb_random %= ZIGB_PANID_MAXVAL;
	
//		printf_datsHtoA("[Tips_uartZigb]: resultDats:", local_datsParam->frameResp, local_datsParam->frameRespLen);
	}

	os_free(local_datsParam);
	return resultREQ;
}

/*zigbee初始化状态自检*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_inspectionSelf(bool hwReset_IF){ //是否硬件复位
	
	datsZigb_reqGet *local_datsParam = (datsZigb_reqGet *)malloc(sizeof(datsZigb_reqGet));

	const datsAttr_ZigbInit default_param = {{0x26,0x08}, {0xFC,0xFF,0x00}, 0x03, {0xFE,0x01,0x66,0x08,0x00,0x6F}, 0x06, 500};	//命令帧，默认参数
	const u8 frameREQ_zigbJoinNWK[5] 	= {0xFE, 0x00, 0x26, 0x00, 0x26};	//zigb激活网络指令帧
	const u8 cmdResp_zigbJoinNWK[2] 	= {0x45, 0xC0};	//zigb激活网络预期响应指令
	const remoteDataReq_method datsReq_method = {0};
	bool resultREQ = false;

	if(hwReset_IF)resultREQ = ZigB_resetInit();
	else resultREQ = true;
	
	if(true == resultREQ){

		resultREQ = zigb_datsRequest((u8 *)frameREQ_zigbJoinNWK,
									 5,
									 (u8 *)cmdResp_zigbJoinNWK,
									 local_datsParam,
									 800,
									 datsReq_method);
	}

	if(true == resultREQ){

//		printf_datsHtoA("[Tips_uartZigb]: resultDats:", local_datsParam->frameResp, local_datsParam->frameRespLen);

		if(local_datsParam->frameResp[4] != 0x09)resultREQ = false;
		else{

			resultREQ = zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, ZIGB_ENDPOINT_CTRLSECENARIO);	//设备ID默认13，注册场景控制端点口—12
			if(resultREQ)resultREQ = zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, ZIGB_ENDPOINT_CTRLNORMAL);	//设备ID默认13，注册常规端点口—13
			if(resultREQ)resultREQ = zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, ZIGB_ENDPOINT_CTRLSYSZIGB); //设备ID默认13，注册系统交互端点口—14
			if(resultREQ)resultREQ = zigb_clusterCtrlEachotherCfg(); //互控端口注册
			if(resultREQ)resultREQ = zigbNetwork_OpenIF(0, 0); //关闭网络
		}
	}

	
	if(true == resultREQ){

		resultREQ = zigb_VALIDA_INPUT(	(u8 *)default_param.zigbInit_reqCMD,
										(u8 *)default_param.zigbInit_reqDAT,
										default_param.reqDAT_num,
										(u8 *)default_param.zigbInit_REPLY,
										default_param.REPLY_num,
										3,		//3次以内无正确响应则失败
										default_param.timeTab_waitAnsr);	
	}

	os_printf("[Tips_uartZigb]: Zigbee inspection result is : %d\n", resultREQ);

	os_free(local_datsParam);
	return resultREQ;
}

/*zigbee网络激活重新连接*/
LOCAL void ICACHE_FLASH_ATTR
ZigB_nwkReconnect(void){

	static u8 reconnectStep = 1;
	static u8 reconnectTryloop = 0;

	switch(reconnectStep){

		os_printf("[Tips_uartZigb]: ZigbeeNwk reconnect start.\n");

		case 1:{

			if(ZigB_inspectionSelf(false)){
			
				reconnectTryloop = 0;
				reconnectStep = 2;
			}
			else{
			
				reconnectTryloop ++;
				os_printf("[Tips_uartZigb]: Zigbee reconnectStep 1 try loop%d.\n", reconnectTryloop);
				if(reconnectTryloop > 3){
			
					reconnectTryloop = 0;
					reconnectStep = 1;
				}
			}
			
		}break;

		case 2:{

			reconnectStep = 1;
			
			xQueueReset(xMsgQ_timeStampGet);
			xQueueReset(xMsgQ_Socket2Zigb);
			xQueueReset(xMsgQ_zigbFunRemind);
			
			nwkZigbOnline_IF = true;
			
			os_printf("[Tips_uartZigb]: ZigbeeNwk reconnect compelete.\n");
			
		}break;
	}
}

/*zigbee初始化*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_NwkCreat(uint16_t PANID, uint8_t CHANNELS){		

#define	zigbNwkCrateCMDLen 	10	//指令个数
	
#define loop_PANID		5	//指令索引
#define loop_CHANNELS	6	//指令索引

#if(ZNP_TARGET_DEVICE == ZNPDEVICE_CC2530)
	const datsAttr_ZigbInit ZigbInit_dats[zigbNwkCrateCMDLen] = {

		{	{0x41,0x00},	{0x00},					0x01,	{0xFE,0x06,0x41,0x80,0x02,0x02,0x00,0x02,0x06,0x03,0xC0},	0x0B,	500	},	//复位	
		{	{0x41,0x00},	{0x00},					0x01,	{0xFE,0x06,0x41,0x80,0x02,0x02,0x00,0x02,0x06,0x03,0xC0},	0x0B,	500	},	//复位
		{	{0x41,0x00},	{0x00},					0x01,	{0xFE,0x06,0x41,0x80,0x02,0x02,0x00,0x02,0x06,0x03,0xC0},	0x0B,	500	},	//复位
		{	{0x26,0x05},	{0x03,0x01,0x03},		0x03,	{0xFE,0x01,0x66,0x05,0x00,0x62},							0x06,	10	},	//寄存器初始化，参数清空
		{	{0x41,0x00},	{0x00},					0x01,	{0xFE,0x06,0x41,0x80,0x02,0x02,0x00,0x02,0x06,0x03,0xC0},	0x0B,	500	},	//二次复位
		{	{0x27,0x02},	{0x34,0x12},			0x02,	{0xFE,0x01,0x67,0x02,0x00,0x64},							0x06,	10	},	//PAN_ID设置
		{	{0x27,0x03},	{0x00,0x80,0x00,0x00},	0x04,	{0xFE,0x01,0x67,0x03,0x00,0x65},							0x06,	10	},	//信道寄存器配置
		{	{0x26,0x05},	{0x87,0x01,0x00},		0x03,	{0xFE,0x01,0x66,0x05,0x00,0x62},							0x06,	10	},	//角色设置（协调器）
		{	{0x26,0x00},	{0},					0x00,	{0xFE,0x01,0x45,0xC0,0x09,0x8D},							0x06,	800 },	//以既定角色入网/建立网络
		{	{0x26,0x08}, 	{0xFC,0xFF,0x00}, 		0x03, 	{0xFE,0x01,0x66,0x08,0x00,0x6F}, 							0x06, 	20  },	//创建成功后关闭网络
	};
#elif(ZNP_TARGET_DEVICE == ZNPDEVICE_CC2538)
	const datsAttr_ZigbInit ZigbInit_dats[zigbNwkCrateCMDLen] = {

		{	{0x41,0x00},	{0x00}, 				0x01,	{0xFE,0x06,0x41,0x80,0x00,0x02,0x00,0x02,0x06,0x03,0xC2},	0x0B,	500 },	//复位 <--differ
		{	{0x41,0x00},	{0x00}, 				0x01,	{0xFE,0x06,0x41,0x80,0x00,0x02,0x00,0x02,0x06,0x03,0xC2},	0x0B,	500 },	//复位 <--differ
		{	{0x41,0x00},	{0x00}, 				0x01,	{0xFE,0x06,0x41,0x80,0x00,0x02,0x00,0x02,0x06,0x03,0xC2},	0x0B,	500 },	//复位 <--differ
		{	{0x26,0x05},	{0x03,0x01,0x03},		0x03,	{0xFE,0x01,0x66,0x05,0x00,0x62},							0x06,	10	},	//寄存器初始化，参数清空
		{	{0x41,0x00},	{0x00}, 				0x01,	{0xFE,0x06,0x41,0x80,0x00,0x02,0x00,0x02,0x06,0x03,0xC2},	0x0B,	500 },	//二次复位 <--differ
		{	{0x27,0x02},	{0x34,0x12},			0x02,	{0xFE,0x01,0x67,0x02,0x00,0x64},							0x06,	10	},	//PAN_ID设置
		{	{0x27,0x03},	{0x00,0x80,0x00,0x00},	0x04,	{0xFE,0x01,0x67,0x03,0x00,0x65},							0x06,	10	},	//信道寄存器配置
		{	{0x26,0x05},	{0x87,0x01,0x00},		0x03,	{0xFE,0x01,0x66,0x05,0x00,0x62},							0x06,	10	},	//角色设置（协调器）
		{	{0x26,0x00},	{0},					0x00,	{0xFE,0x01,0x45,0xC0,0x09,0x8D},							0x06,	800 },	//以既定角色入网/建立网络
		{	{0x26,0x08},	{0xFC,0xFF,0x00},		0x03,	{0xFE,0x01,0x66,0x08,0x00,0x6F},							0x06,	20	},	//创建成功后关闭网络
	};
#endif
	
#define zigbNwkCrate_paramLen 100
	u8 paramTX_temp[zigbNwkCrate_paramLen] = {0};
	
	u8  loop;
	u32 chnl_temp = 0x00000800UL << CHANNELS;
	
	for(loop = 1;loop < zigbNwkCrateCMDLen;loop ++){
		
		memset(paramTX_temp, 0, zigbNwkCrate_paramLen * sizeof(uint8));

		if(loop == 1){

			if(!ZigB_resetInit())loop = 0;
			else{

				os_printf("[Tips_uartZigb]: Zigbee nwkCreat step:%d complete !!!\n", loop);
			}			
		}
		
		switch(loop){	//自选参参数替换默认参数
		
			case loop_PANID:{
			
				paramTX_temp[0] = (uint8)((PANID & 0x00ff) >> 0);
				paramTX_temp[1] = (uint8)((PANID & 0xff00) >> 8);
			}break;
			
			case loop_CHANNELS:{
			
				paramTX_temp[0] = (uint8)((chnl_temp & 0x000000ff) >>  0);
				paramTX_temp[1] = (uint8)((chnl_temp & 0x0000ff00) >>  8);
				paramTX_temp[2] = (uint8)((chnl_temp & 0x00ff0000) >> 16);
				paramTX_temp[3] = (uint8)((chnl_temp & 0xff000000) >> 24);
			}break;
			
			default:{
			
				memcpy(paramTX_temp,ZigbInit_dats[loop].zigbInit_reqDAT,ZigbInit_dats[loop].reqDAT_num);
				
			}break;
		}

		if(loop > 1){

			if(false == zigb_VALIDA_INPUT((u8 *)ZigbInit_dats[loop].zigbInit_reqCMD,
										  (u8 *)paramTX_temp,
										  ZigbInit_dats[loop].reqDAT_num,
										  (u8 *)ZigbInit_dats[loop].zigbInit_REPLY,
										  ZigbInit_dats[loop].REPLY_num,
										  3,
										  ZigbInit_dats[loop].timeTab_waitAnsr)
										 )loop = 0;
			else{
			
				os_printf("[Tips_uartZigb]: Zigbee nwkCreat step:%d complete !!!\n", loop);
			}
		}
	}

	os_printf("[Tips_uartZigb]: Zigbee nwkCreat all complete !!!\n");

	bool result_Set = zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, ZIGB_ENDPOINT_CTRLSECENARIO); //设备ID默认13，注册场景控制端点口—12
	if(result_Set)result_Set =zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, ZIGB_ENDPOINT_CTRLNORMAL); //设备ID默认13，注册常规端点口—13
	if(result_Set)result_Set = zigb_clusterSet(ZIGB_CLUSTER_DEFAULT_DEVID, ZIGB_ENDPOINT_CTRLSYSZIGB); //设备ID默认13，注册系统交互端点口—14
	if(result_Set)result_Set = zigb_clusterCtrlEachotherCfg();

	return result_Set;
}

/*zigbee数据接收*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_datsRemoteRX(datsAttr_ZigbTrans *datsRX, u32 timeWait){
	
#define zigbcmdRX_Len 2
	const u8 cmdRX[zigbcmdRX_Len][2] = {
	
		{0x44,0x81},	//接收到无线节点数据
		{0x45,0xCA},	//无线节点上线通知
	};

	u8 *ptr = NULL;
	u8 loop = 0;
	
	sttUartRcv_rmoteDatComming rptr_uartDatsRcv;
	portBASE_TYPE xMsgQ_rcvResult = pdFALSE;

	datsRX->datsType = zigbTP_NULL;

	xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_dataRemoteComing, (void *)&rptr_uartDatsRcv,  (portTickType)timeWait);
	if(xMsgQ_rcvResult == pdTRUE){
	
		for(loop = 0;loop < zigbcmdRX_Len;loop ++){
		
			ptr = usr_memmem(rptr_uartDatsRcv.dats, rptr_uartDatsRcv.datsLen, (u8*)cmdRX[loop], 2);
			
			if(ptr != NULL){
			
				switch(loop){
				
					case 0:{
					
						if(ZIGB_FRAME_HEAD == *(ptr - 2) && //信息格式校验:帧头与异或校验
						   rptr_uartDatsRcv.dats[rptr_uartDatsRcv.datsLen - 1] == XORNUM_CHECK(&rptr_uartDatsRcv.dats[1], rptr_uartDatsRcv.datsLen - 2)){		
						   
							/*验证通过，信息填装*/
							datsRX->datsSTT.stt_MSG.ifBroadcast = *(ptr + 10);
							datsRX->datsSTT.stt_MSG.Addr_from	= (((u16)*(ptr + 6) & 0x00FF) << 0) + (((u16)*(ptr + 7) & 0x00FF) << 8);
							datsRX->datsSTT.stt_MSG.srcEP		= *(ptr + 8);
							datsRX->datsSTT.stt_MSG.dstEP		= *(ptr + 9);
							datsRX->datsSTT.stt_MSG.ClusterID	= (((u16)*(ptr + 4) & 0x00FF) << 0) + (((u16)*(ptr + 5) & 0x00FF) << 8);
							datsRX->datsSTT.stt_MSG.LQI 		= *(ptr + 11);
							datsRX->datsSTT.stt_MSG.datsLen 	= *(ptr + 18);
							memset(datsRX->datsSTT.stt_MSG.dats,0,datsRX->datsSTT.stt_MSG.datsLen * sizeof(u8));
							memcpy(datsRX->datsSTT.stt_MSG.dats,(ptr + 19),*(ptr + 18));
							
							datsRX->datsType = zigbTP_MSGCOMMING;
							return true;
						}
						else{
						   
							rptr_uartDatsRcv.dats[usr_memloc((u8 *)rptr_uartDatsRcv.dats, rptr_uartDatsRcv.datsLen, (u8 *)cmdRX[loop], 2)] = 0xFF;	//非指定数据则主动污染本段后，再向后复柿
							loop --;	//原段信息向后复查
						}
					}break;
					
					case 1:{
					
						if(ZIGB_FRAME_HEAD == *(ptr - 2) && //信息格式校验:帧头与异或校验
						   rptr_uartDatsRcv.dats[rptr_uartDatsRcv.datsLen - 1] == XORNUM_CHECK(&rptr_uartDatsRcv.dats[1], rptr_uartDatsRcv.datsLen - 2)){
						
							/*验证通过，信息填装*/
							datsRX->datsSTT.stt_ONLINE.nwkAddr_fresh = (((u16)*(ptr + 2) & 0x00FF) << 0) + (((u16)*(ptr + 3) & 0x00FF) << 8);
							
							datsRX->datsType = zigbTP_ntCONNECT;
							return true;
						}
						else{
						   
							rptr_uartDatsRcv.dats[usr_memloc((u8 *)rptr_uartDatsRcv.dats, rptr_uartDatsRcv.datsLen, (u8 *)cmdRX[loop], 2)] = 0xFF;	//非指定数据则主动污染本段后，再向后复柿
							loop --;	//原段信息向后复查
						}
					}break;
					
					default:break;
				}
			}
		}
	}
	
	return false;
}

/*zigbee PANID更新*/
LOCAL bool ICACHE_FLASH_ATTR
ZigB_PANIDReales(bool inspection_IF){ //是否自检

	stt_usrDats_privateSave *datsRead_Temp = devParam_flashDataRead();
	stt_usrDats_privateSave datsSave_Temp = {0};
	u16 panID_temp = ZigB_getPanIDCurrent();

	bool result = false; 
	static u8 inspectionFail_count = 0; //自检失败次数---防止更换模块不更换主控导致主控残留信息不一致而一直进行错误自检

	os_printf("PANID_current is : %04X.\n", panID_temp);
	os_printf("PANID_flash is : %04X.\n", datsRead_Temp->panID_default);

//	datsRead_Temp->panID_default = 0;

	if((datsRead_Temp->panID_default != panID_temp) || //PANID为零或
	   (!datsRead_Temp->panID_default) || //与本地存储不相符
	   (inspectionFail_count >= 3)){ //或自检失败超过三次

		inspectionFail_count = 0;

		if((!datsRead_Temp->panID_default) || (datsRead_Temp->panID_default == 0xFFFF))datsSave_Temp.panID_default = sysZigb_random; //本地存储PANID为空则赋随机值
		else{

			datsSave_Temp.panID_default = datsRead_Temp->panID_default;
		}

		nwkZigb_currentPANID = datsSave_Temp.panID_default;
		panID_temp = datsSave_Temp.panID_default;
		devParam_flashDataSave(obj_panID_default, datsSave_Temp);
		
		result = ZigB_NwkCreat(panID_temp, 4);
		
	}else{

		nwkZigb_currentPANID = datsRead_Temp->panID_default; //默认PANID更新

		if(inspection_IF){

			result = ZigB_inspectionSelf(1);
			
		}else{

			result = true;
		}
	}

	if(datsRead_Temp)os_free(datsRead_Temp);

	if(result){

		os_printf("panID reales success.\n");
		inspectionFail_count = 0;
	}
	else{

		os_printf("panID reales fail.\n");
		inspectionFail_count ++;
	}

	return result;
}

/*zigbee数据发送*/
LOCAL void ICACHE_FLASH_ATTR
ZigB_remoteDatsSend(u16 DstAddr, //地址
						  u8 dats[], //数据
                          u8 datsLen, //数据长度
                          u8 port){ //端口

	const u8 zigbProtocolCMD_dataSend[2] = {0x24,0x01};
	const u8 TransID = 13;
	const u8 Option	 = 0;
	const u8 Radius	 = 7;

	u8 buf_datsTX[32] = {0};
	u8 datsTX[48] = {0};
	u8 datsTX_Len = 0;

	buf_datsTX[0] = (uint8)((DstAddr & 0x00ff) >> 0);
	buf_datsTX[1] = (uint8)((DstAddr & 0xff00) >> 8);
	buf_datsTX[2] = port;
	buf_datsTX[3] = port;
	buf_datsTX[4] = ZIGB_CLUSTER_DEFAULT_CULSTERID;
	buf_datsTX[6] = TransID;
	buf_datsTX[7] = Option;
	buf_datsTX[8] = Radius;
	buf_datsTX[9] = datsLen;
	memcpy(&buf_datsTX[10],dats,datsLen);	

	datsTX_Len = ZigB_TXFrameLoad(datsTX, (u8 *)zigbProtocolCMD_dataSend, 2, (u8 *)buf_datsTX, datsLen + 10);

	uartZigbee_putDats(UART0, datsTX, datsTX_Len);
}

/*zigbee数据发送 - 非阻塞异步*/
LOCAL bool ZigB_datsTX_ASY( uint16 	DstAddr,
							   	uint8  	SrcPoint,
							   	uint8  	DstPoint,
							   	uint8 	ClustID,
							   	uint8  	dats[],
							   	uint8  	datsLen,
							   	stt_dataRemoteReq bufQueue[],
							   	uint8   BQ_LEN){

	const u8 TransID = 13;
	const u8 Option	 = 0;
	const u8 Radius	 = 7;

	const u8 cmd_dataReq[2] = {0x24, 0x01};
	const u8 cmd_dataResp[2] = {0x44, 0x80};

#define zigbTX_datsTransLen_ASR 96
	uint8 buf_datsTX[zigbTX_datsTransLen_ASR] = {0};
	uint8 buf_datsRX[zigbTX_datsTransLen_ASR] = {0};

	u8 loop = 0;

	for(loop = 0; loop < BQ_LEN; loop ++){

		if(!bufQueue[loop].repeat_Loop){

			memset(bufQueue[loop].dataReq, 0, sizeof(u8) * zigbTX_datsTransLen_ASR);
			memset(bufQueue[loop].dataResp, 0, sizeof(u8) * 8);

			//发送帧填装
			buf_datsTX[0] = (uint8)((DstAddr & 0x00ff) >> 0);
			buf_datsTX[1] = (uint8)((DstAddr & 0xff00) >> 8);
			buf_datsTX[2] = DstPoint;
			buf_datsTX[3] = SrcPoint;
			buf_datsTX[4] = ClustID;
			buf_datsTX[6] = TransID;
			buf_datsTX[7] = Option;
			buf_datsTX[8] = Radius;
			buf_datsTX[9] = datsLen;
			memcpy(&buf_datsTX[10], dats, datsLen);
			bufQueue[loop].dataReq_Len = ZigB_TXFrameLoad(bufQueue[loop].dataReq, (u8 *)cmd_dataReq, 2, (u8 *)buf_datsTX, datsLen + 10);

			//应答帧填装
			buf_datsRX[0] = 0x00;
			buf_datsRX[1] = SrcPoint;
			buf_datsRX[2] = TransID;

//			bufQueue[loop].dataResp_Len = 3;
			bufQueue[loop].dataResp_Len = ZigB_TXFrameLoad(bufQueue[loop].dataResp, (u8 *)cmd_dataResp, 2, (u8 *)buf_datsRX, 3);

			bufQueue[loop].repeat_Loop = zigB_remoteDataTransASY_txReapt; //使能发送 10 次

//			if(DstAddr == 0xDB04){

//				os_printf(">>>txPagLen:%d, dataLen:%d.\n", bufQueue[loop].dataReq_Len, datsLen);
//			}

			return true;
		}
	}

	os_printf(">>>dataRM reqQ full.\n");

	return false; 
}

/*zigbee场景数据发送 - 非阻塞异步*/
LOCAL bool ZigB_ScenarioTX_ASY( uint16 	DstAddr,
									  uint8  SrcPoint,
									  uint8  DstPoint,
									  uint8  ClustID,
									  uint8  dats[],
									  uint8  datsLen,
									  stt_dataScenarioReq bufQueue[],
									  uint8  BQ_LEN){

	const u8 TransID = 13;
	const u8 Option	 = 0;
	const u8 Radius	 = 7;

	const u8 cmd_dataReq[2] = {0x24, 0x01};
	const u8 cmd_dataResp[2] = {0x44, 0x80};

#define zigbTX_ScenarioLen_ASR 16
	uint8 buf_datsTX[zigbTX_datsTransLen_ASR] = {0};	

	u8 loop = 0;

	for(loop = 0; loop < BQ_LEN; loop ++){

		if(!bufQueue[loop].repeat_Loop){

			memset(bufQueue[loop].dataReq, 0, sizeof(u8) * 16);			

			//发送帧填装
			buf_datsTX[0] = (uint8)((DstAddr & 0x00ff) >> 0);
			buf_datsTX[1] = (uint8)((DstAddr & 0xff00) >> 8);
			buf_datsTX[2] = DstPoint;
			buf_datsTX[3] = SrcPoint;
			buf_datsTX[4] = ClustID;
			buf_datsTX[6] = TransID;
			buf_datsTX[7] = Option;
			buf_datsTX[8] = Radius;
			buf_datsTX[9] = datsLen;
			memcpy(&buf_datsTX[10], dats, datsLen);
			bufQueue[loop].dataReq_Len = ZigB_TXFrameLoad(bufQueue[loop].dataReq, (u8 *)cmd_dataReq, 2, (u8 *)buf_datsTX, datsLen + 10);

			//远端应答地址填装
			bufQueue[loop].dataRespNwkAddr = DstAddr;

			bufQueue[loop].repeat_Loop = zigB_ScenarioCtrlDataTransASY_txReapt; //使能发送 20 次
			bufQueue[loop].scenarioOpreatCmp_flg = 0;

			return true;
		}
	}

	os_printf(">>>scenarioCT reqQ full.\n");

	return false; 
}

LOCAL void ICACHE_FLASH_ATTR
timer_zigbNodeDevDetectManage_funCB(void *para){

	nwkStateAttr_Zigb *pHead_listDevInfo = (nwkStateAttr_Zigb *)para;

	if(0 == zigbDev_chatLenDectect(pHead_listDevInfo)){ //表内无节点直接返回
		
		vTaskDelay(10);
		return;
	}

	while(pHead_listDevInfo->next != NULL){
	
		pHead_listDevInfo = pHead_listDevInfo->next;
		
		if(pHead_listDevInfo->onlineDectect_LCount > 0)pHead_listDevInfo->onlineDectect_LCount --;
		else{
			
			u16 p_nwkRemove = pHead_listDevInfo->nwkAddr;
			
//			while(false == zigbDev_eptRemove_BYnwk((nwkStateAttr_Zigb *)pHead_listDevInfo,p_nwkRemove));

			os_printf("[Tips_uartZigb]: nodeDev remove result is %d\n", zigbDev_eptRemove_BYnwk((nwkStateAttr_Zigb *)para, p_nwkRemove));

			os_printf("[Tips_uartZigb]: nodeDev(nwkAddr: 0x%04X) has been optimized cause inactive.\n", p_nwkRemove);
		
		}
	}
}

LOCAL void ICACHE_FLASH_ATTR
zigbeeDataTransProcess_task(void *pvParameters){

#define zigB_mThread_dispLen  		150	//打印信息缓存
	char disp[zigB_mThread_dispLen];
#define zigB_mThread_datsMSG_Len 	100	//打印数据缓存
	char dats_MSG[zigB_mThread_datsMSG_Len];

#define zigB_datsTX_bufferLen 		96	//zigB数据发送缓存
	u8 datsKernel_TXbuffer[zigB_datsTX_bufferLen];

	u8 local_insertRecord_datsReqNormal = 0; //静态值：本地普通非阻塞远端数据请求队列索引
	u8 local_insertRecord_datsReqScenario = 0; //静态值：场景集群控制非阻塞远端数据请求队列索引
	u8 local_ctrlRecord_reserveLoopInsert = zigB_ScenarioCtrlDataTransASY_opreatOnceNum; //静态值：场景可用操作单元数目

	stt_threadDatsPass mptr_Z2S;
	stt_threadDatsPass rptr_S2Z;	//通信线程消息队列，socket通信主线程到zigbee通信主线程
	u32_t rptr_timeStamp;
	enum_zigbFunMsg rptr_zigbFunRm;
	portBASE_TYPE xMsgQ_rcvResult = pdFALSE;

	u8 loop = 0;

	datsAttr_ZigbTrans *local_datsRX = (datsAttr_ZigbTrans *)os_zalloc(sizeof(datsAttr_ZigbTrans));	//数据接收缓存

	nwkStateAttr_Zigb *zigbDevList_Head = (nwkStateAttr_Zigb *)os_zalloc(sizeof(nwkStateAttr_Zigb));	//节点设备信息链表 表头创建
	const u16 zigbDetect_nwkNodeDev_Period = 1000;	//节点设备链表检测定时器更新周期（单位：ms）
	const u16 zigDev_lifeCycle = 240;	//节点设备心跳周期（单位：s），周期内无心跳更新，节点设备将被判决死亡同时从链表中优化清除
	nwkStateAttr_Zigb *ZigbDevNew_temp;	//节点设备信息缓存
	nwkStateAttr_Zigb ZigbDevNew_tempInsert; //节点设备插入链表前预缓存

	/*节点设备链表更新检查*/
	os_timer_disarm(&timer_zigbNodeDevDetectManage);
	os_timer_setfn(&timer_zigbNodeDevDetectManage, (os_timer_func_t *)timer_zigbNodeDevDetectManage_funCB, zigbDevList_Head);
	os_timer_arm(&timer_zigbNodeDevDetectManage, zigbDetect_nwkNodeDev_Period, true);

	while(!ZigB_resetInit());

	os_printf("ZIGB sysRandom get result is: %d.\n", ZigB_getRandom()); //PANID预取随机数更新
	os_printf("IEEE to MAC result is: %d.\n", ZigB_getIEEEAddr()); //MAC更新
	while(!ZigB_PANIDReales(1)); //panid自检/更新

	nwkZigbOnline_IF = true;

	for(;;){

		/*zigb网络是否在线*/
		if(nwkZigbOnline_IF){//zigb网络在线

			/*>>>>>>zigb系统功能响应<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
			xMsgQ_rcvResult = xQueueReceive(xMsgQ_zigbFunRemind, (void *)&rptr_zigbFunRm, 0);
			if(xMsgQ_rcvResult == pdTRUE){

				frame_zigbSysCtrl datsTemp_zigbSysCtrl = {0}; //系统控制数据帧缓存
				bool nodeCMDtranslate_EN = false; //远程数据传输使能

				switch(rptr_zigbFunRm){

					case msgFun_nwkOpen:{ //开发网络，使能新节点加入

						zigbNetwork_OpenIF(1, ZIGBNWKOPENTIME_DEFAULT); //自身响应网络开放请求

//						datsTemp_zigbSysCtrl.command = ZIGB_SYSCMD_NWKOPEN;
//						datsTemp_zigbSysCtrl.dats[0] = ZIGBNWKOPENTIME_DEFAULT;
//						datsTemp_zigbSysCtrl.datsLen = 1;

//						nodeCMDtranslate_EN = true;

					}break;

					case msgFun_nodeSystimeSynchronous:{ //设置子节点系统时间，进行网络时间同步

						u32_t timeStmap_temp = 0UL;

//						if(nwkInternetOnline_IF){ //internet在线则获取sntp_UTC下发

//							timeStmap_temp = sntp_get_current_timestamp();
//							
//						}else{ //否则直接取本地UTC

//							timeStmap_temp = systemUTC_current;
//						}

						timeStmap_temp = systemUTC_current;
						
						datsTemp_zigbSysCtrl.command = ZIGB_SYSCMD_TIMESET;
						datsTemp_zigbSysCtrl.dats[0] = (u8)((timeStmap_temp & 0x000000FF) >> 0); //UTC
						datsTemp_zigbSysCtrl.dats[1] = (u8)((timeStmap_temp & 0x0000FF00) >> 8);
						datsTemp_zigbSysCtrl.dats[2] = (u8)((timeStmap_temp & 0x00FF0000) >> 16);
						datsTemp_zigbSysCtrl.dats[3] = (u8)((timeStmap_temp & 0xFF000000) >> 24);
						datsTemp_zigbSysCtrl.dats[4] = (u8)(sysTimeZone_H); //时区_时
						datsTemp_zigbSysCtrl.dats[5] = (u8)(sysTimeZone_M); //时区_分
						datsTemp_zigbSysCtrl.dats[6] = 0; //后期调整为下发时区，但不作时区补偿
						datsTemp_zigbSysCtrl.datsLen = 6;
						
						nodeCMDtranslate_EN = true;
					
					}break;

					case msgFun_localSystimeZigbAdjust:{ //读取zigbee内时间，并将系统本地时间与其同步

						zigB_sysTimeGetRealesWithLocal();
						nodeCMDtranslate_EN = false;
						
					}break;

					case msgFun_portCtrlEachoRegister:{ //立即注册互控端口

						bool result_Set = zigb_clusterCtrlEachotherCfg();
						nodeCMDtranslate_EN = false;
					
					}break;

					case msgFun_panidRealesNwkCreat:{ //PANID网络立即更新

						ZigB_PANIDReales(false);
						nodeCMDtranslate_EN = false;
					
					}break;

					case msgFun_scenarioCrtl:{ //场景控制即刻群发

						u8 loop = 0;
						u8 datsSend_temp[1] = {0};

						os_printf("scenario opreatNum:%d.\n", scenarioOprateDats.devNode_num);
						memset(localZigbASYDT_bufQueueScenarioReq, 0, sizeof(stt_dataScenarioReq) * zigB_ScenarioCtrlDataTransASY_QbuffLen); //前次操作冲刷
						localZigbASYDT_scenarioCtrlReserveAllnum = scenarioOprateDats.devNode_num; //当前剩余有效的场景操作单位数目更新
						for(loop = 0; loop < scenarioOprateDats.devNode_num; loop ++){

							nwkStateAttr_Zigb *infoZigbDevRet_temp = zigbDev_eptPutout_BYpsy(zigbDevList_Head, 
																							 scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC, 
																							 DEVZIGB_DEFAULT, 
																							 false);
							if(infoZigbDevRet_temp){ //网络短地址获取

								datsSend_temp[0] = scenarioOprateDats.scenarioOprate_Unit[loop].devNode_opStatus; //数据填装
								ZigB_ScenarioTX_ASY( infoZigbDevRet_temp->nwkAddr, //异步远端数据请求队列加载
 													 ZIGB_ENDPOINT_CTRLSECENARIO,
													 ZIGB_ENDPOINT_CTRLSECENARIO,
													 ZIGB_CLUSTER_DEFAULT_CULSTERID,
													 datsSend_temp,
													 1,
													 localZigbASYDT_bufQueueScenarioReq,
													 zigB_ScenarioCtrlDataTransASY_QbuffLen);

								if(infoZigbDevRet_temp)os_free(infoZigbDevRet_temp);
								
							}else{

								os_printf("scenario zigbAddr get fail with mac:%02X %02X %02X %02X %02X-(%2d).\n", 	scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[0],
																											 		scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[1],
																											 		scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[2],
																											 		scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[3],
																											 		scenarioOprateDats.scenarioOprate_Unit[loop].devNode_MAC[4],
																											 		loop);
							}
						}

						memset(&scenarioOprateDats, 0, sizeof(stt_scenarioOprateDats));  //数据复位

						datsTemp_zigbSysCtrl.command = ZIGB_SYSCMD_DATATRANS_HOLD; //主动使子设备通信挂起，为场景通信让路
						datsTemp_zigbSysCtrl.dats[0] = 30; //默认挂起30s
						datsTemp_zigbSysCtrl.datsLen = 1;
						
						nodeCMDtranslate_EN = true;

					}break;

					case msgFun_dtPeriodHoldPst:{

						datsTemp_zigbSysCtrl.command = ZIGB_SYSCMD_DATATRANS_HOLD;
						datsTemp_zigbSysCtrl.dats[0] = 30; //默认挂起15s
						datsTemp_zigbSysCtrl.datsLen = 1;
						
						nodeCMDtranslate_EN = true;
						
					}break;

					default:{

						nodeCMDtranslate_EN = false;
							
					}break;
				}

				if(nodeCMDtranslate_EN){

					bool TXCMP_FLG = false;

					memset(datsKernel_TXbuffer, 0, sizeof(u8) * zigB_datsTX_bufferLen);
					ZigB_sysCtrlFrameLoad(datsKernel_TXbuffer, datsTemp_zigbSysCtrl);

					TXCMP_FLG = ZigB_datsTX_ASY(0xFFFF, 
												ZIGB_ENDPOINT_CTRLSYSZIGB,
												ZIGB_ENDPOINT_CTRLSYSZIGB,
												ZIGB_CLUSTER_DEFAULT_CULSTERID,
												(u8 *)datsKernel_TXbuffer,
												2 + datsTemp_zigbSysCtrl.datsLen, //命令长度 1 + 数据长度说明 1 + 数据长度 n 
												localZigbASYDT_bufQueueRemoteReq,
												zigB_remoteDataTransASY_QbuffLen);
				}
			}
			
			/*>>>>>>zigb本地互控向外同步<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
			if(EACHCTRL_realesFLG){
			
				u8 loop = 0;
				bool TXCMP_FLG = false;
				u8 datsTemp_zigbCtrlEachother[1] = {0};
				u8 datsTempLen_zigbCtrlEachother = 1; //互控数据仅一字节

				if(devStatus_ctrlEachO_IF == true){ //互控同步信息发送使能

					devStatus_ctrlEachO_IF = false;

					for(loop = 0; loop < USRCLUSTERNUM_CTRLEACHOTHER; loop ++){ //三个开关位分别判定
					
						if(EACHCTRL_realesFLG & (1 << loop)){ //互控有效位判断
						
							EACHCTRL_realesFLG &= ~(1 << loop); //互控有效位清零
							
							datsTemp_zigbCtrlEachother[0] = (status_actuatorRelay >> loop) & 0x01; //有效位开关状态填装
							
							if((CTRLEATHER_PORT[loop] > CTRLEATHER_PORT_NUMSTART) && CTRLEATHER_PORT[loop] < CTRLEATHER_PORT_NUMTAIL){ //判定是否为有效互控端口

								(datsTemp_zigbCtrlEachother[0])?(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[loop]] = STATUSLOCALEACTRL_VALMASKRESERVE_ON):(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[loop]] = STATUSLOCALEACTRL_VALMASKRESERVE_OFF); //集群控制信息管理，互控信息掩码更新

								TXCMP_FLG = ZigB_datsTX_ASY(0xFFFF, 
															CTRLEATHER_PORT[loop],
															CTRLEATHER_PORT[loop],
															ZIGB_CLUSTER_DEFAULT_CULSTERID,
															(u8 *)datsTemp_zigbCtrlEachother,
															datsTempLen_zigbCtrlEachother,
															localZigbASYDT_bufQueueRemoteReq,
															zigB_remoteDataTransASY_QbuffLen);
							}
						}
					}
				}
			}
			
			/*>>>>>>zigb系统时间设置<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
			xMsgQ_rcvResult = xQueueReceive(xMsgQ_timeStampGet, (void *)&rptr_timeStamp, 0);
			if(xMsgQ_rcvResult == pdTRUE){
	
				if(rptr_timeStamp)zigB_sysTimeSet(rptr_timeStamp - ZIGB_UTCTIME_START); //zigbee UTC 负补偿
	
	//			if(rptr_timeStamp == zigB_sysTimeGet())os_printf("time right.\n");
	
	//			os_printf("[Tips_uartZigb]: zigbee msgQ is: 0x%08X.\n", rptr_timeStamp);
			}
	
			/*>>>>>>sockets数据处理<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
			xMsgQ_rcvResult = xQueueReceive(xMsgQ_Socket2Zigb, (void *)&rptr_S2Z, 0);
			if(xMsgQ_rcvResult == pdTRUE){
	
				switch(rptr_S2Z.msgType){
	
					case conventional:{
	
						u16 zigb_nwkAddrTemp = 0xFFFF;
						bool TXCMP_FLG		 = false;
	
						if((rptr_S2Z.dats.dats_conv.dats[3] == FRAME_MtoSCMD_cmdConfigSearch) && //若为配置指令，则广播
						   (rptr_S2Z.dats.dats_conv.datsFrom == datsFrom_ctrlLocal) ){	
						   
						   	{

								const  u8 localPeriod_nwkTrig = 2;
								static u8 localCount_searchREQ = 4;

								if(localCount_searchREQ < localPeriod_nwkTrig)localCount_searchREQ ++;
								else{ //localPeriod_nwkTrig次搜索触发一次开放网络，取决于搜索码发送周期

									localCount_searchREQ = 0;
									usrZigbNwkOpen_start(); //配置搜索时通知网内所有节点开放网络加入窗口
								}
						   	}

							os_printf("[Tips_ZIGB-NWKmsg]: rcvMsg local cmd: %02X !!!\n", rptr_S2Z.dats.dats_conv.dats[3]);
							zigb_nwkAddrTemp = 0xFFFF;
							
						}
						else{
	
							nwkStateAttr_Zigb *infoZigbDevRet_temp = zigbDev_eptPutout_BYpsy(zigbDevList_Head, rptr_S2Z.dats.dats_conv.macAddr, DEVZIGB_DEFAULT, false);
							if(NULL != infoZigbDevRet_temp){
							
								zigb_nwkAddrTemp =	infoZigbDevRet_temp->nwkAddr;
								os_free(infoZigbDevRet_temp);
							
							}else{
	
								zigb_nwkAddrTemp = 0;
							}
						}
	
						if(zigb_nwkAddrTemp){
	
							memset(datsKernel_TXbuffer, 0, sizeof(u8) * zigB_datsTX_bufferLen);
							memcpy(datsKernel_TXbuffer, rptr_S2Z.dats.dats_conv.dats, rptr_S2Z.dats.dats_conv.datsLen);
						
							TXCMP_FLG = ZigB_datsTX_ASY(zigb_nwkAddrTemp,	
														ZIGB_ENDPOINT_CTRLNORMAL,
														ZIGB_ENDPOINT_CTRLNORMAL,
														ZIGB_CLUSTER_DEFAULT_CULSTERID,
														(u8 *)datsKernel_TXbuffer,
														rptr_S2Z.dats.dats_conv.datsLen,
														localZigbASYDT_bufQueueRemoteReq,
														zigB_remoteDataTransASY_QbuffLen);
						}
						
					}break;
	
					case listDev_query:{
	
	
					}break;
	
					default:{
	
	
					}break;
				}
			}
			else{
	
	//			os_printf("[TEST]: receive msg fail, result: %d !!!\n", xResult);
			}
	
			/*>>>>>>zigbee数据处理<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
			if(true == ZigB_datsRemoteRX(local_datsRX, 0)){
	
				memset(disp, 0, zigB_mThread_dispLen * sizeof(char));
				memset(dats_MSG, 0, zigB_mThread_datsMSG_Len * sizeof(char));
	
				switch(local_datsRX->datsType){
	
					//======一级协议层数据类型：数据传输=====//
					case zigbTP_MSGCOMMING:{

						//端点口判定
						switch(local_datsRX->datsSTT.stt_MSG.srcEP){	

							case ZIGB_ENDPOINT_CTRLSECENARIO:{

								

							}break;

							case ZIGB_ENDPOINT_CTRLNORMAL:{//端点口数据解析：常规控制

//								/*数据包log输出*/
//								for(loop = 0;loop < local_datsRX->datsSTT.stt_MSG.datsLen;loop ++){
//								
//									sprintf((char *)&dats_MSG[loop * 3],"%02X ",local_datsRX->datsSTT.stt_MSG.dats[loop]);
//								}
//								os_printf("[Tips_uartZigb]: datsRcv from 0x%04X<len:%d> : %s.\n", local_datsRX->datsSTT.stt_MSG.Addr_from, local_datsRX->datsSTT.stt_MSG.datsLen, dats_MSG);
			
								/*数据处理*/
								u8 devMAC_Temp[DEVMAC_LEN] = {0};
								threadDatsPass_objDatsFrom datsFrom_obj = datsFrom_ctrlLocal;
								switch(local_datsRX->datsSTT.stt_MSG.dats[0]){	//取MAC
								
									case ZIGB_FRAMEHEAD_CTRLLOCAL:{
			
										memcpy(devMAC_Temp, &(local_datsRX->datsSTT.stt_MSG.dats[5]), DEVMAC_LEN);	//数据包下标5-9为MAC地址,MAC前一位为校验码
										datsFrom_obj = datsFrom_ctrlLocal;
									
									}break;
									
									case ZIGB_FRAMEHEAD_CTRLREMOTE:{
								
										memcpy(devMAC_Temp, &(local_datsRX->datsSTT.stt_MSG.dats[17]), DEVMAC_LEN); //数据包下标17-21为MAC地址,MAC前一位为校验码
										datsFrom_obj = datsFrom_ctrlRemote;
									
									}break;
								
									case ZIGB_FRAMEHEAD_HEARTBEAT:{
								
										memcpy(devMAC_Temp, &(local_datsRX->datsSTT.stt_MSG.dats[3 + 1]), DEVMAC_LEN);	//数据包下标4-8为MAC地址,MAC前一位为校验码
										datsFrom_obj = datsFrom_heartBtRemote;
									
									}break;

									case DTMODEKEEPACESS_FRAMEHEAD_ONLINE:{

										memcpy(devMAC_Temp, &(local_datsRX->datsSTT.stt_MSG.dats[2]), DEVMAC_LEN); //数据包下标2-6为MAC地址,MAC前一位为校验码
										datsFrom_obj = datsFrom_heartBtRemote;
										
									}break;
								
									default:{
								
										memcpy(devMAC_Temp, &(local_datsRX->datsSTT.stt_MSG.dats[5]), DEVMAC_LEN);	//数据包下标5-9为MAC地址,MAC前一位为校验码
										datsFrom_obj = datsFrom_ctrlLocal;
										
									}break;
								}

								if(!memcmp(debugLogOut_targetMAC ,devMAC_Temp, 5)){

									/*数据包log输出*/
									os_printf("[Tips_ZIGB-ZBdats]: rcv msg(Len: %d) from MAC:<%02X %02X %02X %02X %02X>-nwkAddr[%04X].\n",
											  local_datsRX->datsSTT.stt_MSG.datsLen,
											  devMAC_Temp[0],
											  devMAC_Temp[1],
											  devMAC_Temp[2],
											  devMAC_Temp[3],
											  devMAC_Temp[4],
											  local_datsRX->datsSTT.stt_MSG.Addr_from);
								}

//								/*数据包log输出*/
//								os_printf("[Tips_ZIGB-ZBdats]: rcv msg(Len: %d) from MAC:<%02X %02X %02X %02X %02X>-nwkAddr[%04X].\n",
//										  local_datsRX->datsSTT.stt_MSG.datsLen,
//										  devMAC_Temp[0],
//										  devMAC_Temp[1],
//										  devMAC_Temp[2],
//										  devMAC_Temp[3],
//										  devMAC_Temp[4],
//										  local_datsRX->datsSTT.stt_MSG.Addr_from);

								/*数据处理-节点设备链表更新*/
								ZigbDevNew_temp = zigbDev_eptPutout_BYnwk(zigbDevList_Head, local_datsRX->datsSTT.stt_MSG.Addr_from, true);
								if(NULL == ZigbDevNew_temp){	//判断是否为新增节点设备，是则更新生命周期，否则添加进链表
								
									if(local_datsRX->datsSTT.stt_MSG.Addr_from != 0 && local_datsRX->datsSTT.stt_MSG.datsLen >= (DEVMAC_LEN + 1)){	//数据来源判断（本地广播时自己也会收到 则不理会）和 数据长度判断（数据包含MAC地址和设备类型，因此长度必大于该长度之和）
									
										ZigbDevNew_tempInsert.nwkAddr = local_datsRX->datsSTT.stt_MSG.Addr_from;
										memcpy(ZigbDevNew_tempInsert.macAddr, devMAC_Temp, DEVMAC_LEN);
										ZigbDevNew_tempInsert.devType = DEVZIGB_DEFAULT;	//数据包下发默认设备（开关）类型
										ZigbDevNew_tempInsert.onlineDectect_LCount = zigDev_lifeCycle;
										ZigbDevNew_tempInsert.next = NULL;
										
										zigbDev_eptCreat(zigbDevList_Head, ZigbDevNew_tempInsert);	//zigbee节点设备信息注册进链表
										zigbDev_delSame(zigbDevList_Head);	//设备链表优化 去重
									}
									
								}else{
								
									ZigbDevNew_temp->onlineDectect_LCount = zigDev_lifeCycle; //更新节点设备在列表中的生命周期
									ZigbDevNew_temp = NULL;
									if(ZigbDevNew_temp)os_free(ZigbDevNew_temp); //缓存释放
								}
			
								/*数据处理-数据通过消息队列传送至socket通信主线程*/
								if((local_datsRX->datsSTT.stt_MSG.dats[0] == ZIGB_FRAMEHEAD_CTRLLOCAL) && (local_datsRX->datsSTT.stt_MSG.dats[3] == FRAME_MtoSCMD_cmdConfigSearch)){ //本地配置指令添加 网络短地址 <供调试使用>

									local_datsRX->datsSTT.stt_MSG.dats[20] = (local_datsRX->datsSTT.stt_MSG.Addr_from & 0xFF00) >> 8;
									local_datsRX->datsSTT.stt_MSG.dats[21] = (local_datsRX->datsSTT.stt_MSG.Addr_from & 0x00FF) >> 0;
									local_datsRX->datsSTT.stt_MSG.dats[4] = frame_Check(&local_datsRX->datsSTT.stt_MSG.dats[5], 28);
								}
								
								mptr_Z2S.msgType = conventional;
								memcpy(mptr_Z2S.dats.dats_conv.dats, local_datsRX->datsSTT.stt_MSG.dats, local_datsRX->datsSTT.stt_MSG.datsLen);
								mptr_Z2S.dats.dats_conv.datsLen = local_datsRX->datsSTT.stt_MSG.datsLen;
								memcpy(mptr_Z2S.dats.dats_conv.macAddr, devMAC_Temp, DEVMAC_LEN);
								mptr_Z2S.dats.dats_conv.devType = DEVZIGB_DEFAULT;
								mptr_Z2S.dats.dats_conv.datsFrom = datsFrom_obj;
								xQueueSend(xMsgQ_Zigb2Socket, (void *)&mptr_Z2S, 0);

							}break;

							case ZIGB_ENDPOINT_CTRLSYSZIGB:{//端点口数据解析：zigbee系统控制交互专用端口

								frame_zigbSysCtrl datsTempRX_zigbSysCtrl = {0}; //系统控制数据帧缓存
								frame_zigbSysCtrl datsTempTX_zigbSysCtrl = {0}; //系统控制数据帧缓存
								bool nodeCMDtranslate_EN = false; //远程数据传输使能

								datsTempRX_zigbSysCtrl.command = local_datsRX->datsSTT.stt_MSG.dats[0]; //命令加载
//								os_printf("[Tips_uartZigb]: node[0x%04X] sysCtrlCMD<%02X> coming.\n", local_datsRX->datsSTT.stt_MSG.Addr_from, datsTempRX_zigbSysCtrl.command);
								memcpy(datsTempRX_zigbSysCtrl.dats, &(local_datsRX->datsSTT.stt_MSG.dats[2]), local_datsRX->datsSTT.stt_MSG.dats[1]); //数据加载
								switch(datsTempRX_zigbSysCtrl.command){

									case ZIGB_SYSCMD_EACHCTRL_REPORT:{
									
										COLONY_DATAMANAGE_CTRLEATHER[datsTempRX_zigbSysCtrl.dats[0]] = datsTempRX_zigbSysCtrl.dats[1]; //数据管理，最后一次互控值更新<dats[0]、dats[2]、dats[4]为端口号，dats[1]、dats[3]、dats[5]为端口号对应互控开关状态值>
										COLONY_DATAMANAGE_CTRLEATHER[datsTempRX_zigbSysCtrl.dats[2]] = datsTempRX_zigbSysCtrl.dats[3];
										COLONY_DATAMANAGE_CTRLEATHER[datsTempRX_zigbSysCtrl.dats[4]] = datsTempRX_zigbSysCtrl.dats[5];
										datsTempTX_zigbSysCtrl.dats[0] = 0;
										datsTempTX_zigbSysCtrl.command = ZIGB_SYSCMD_EACHCTRL_REPORT;
										datsTempTX_zigbSysCtrl.datsLen = 1;

										nodeCMDtranslate_EN = true; //接受成功，进行响应
									
									}break;

									case ZIGB_SYSCMD_COLONYPARAM_REQPERIOD:{

										u8 loop = 0;
										u8 DSTMAC_Temp[5] = {0};
										bool scenarioOp_findIF = false;

										memcpy(DSTMAC_Temp, datsTempRX_zigbSysCtrl.dats, 5); //远端MAC地址加载
										for(loop = 0; loop < COLONY_DATAMANAGE_SCENE.devNode_num; loop ++){ //最近一次场景值获取

											if(!memcmp(COLONY_DATAMANAGE_SCENE.scenarioOprate_Unit[loop].devNode_MAC, DSTMAC_Temp, 5)){ //根据MAC找场景操作数据，找到了就跳出

												datsTempTX_zigbSysCtrl.dats[0] = COLONY_DATAMANAGE_SCENE.scenarioOprate_Unit[loop].devNode_opStatus;
												scenarioOp_findIF = true;
												break;
											}
										}
										if(!scenarioOp_findIF)datsTempTX_zigbSysCtrl.dats[0] = 0xff; //找不到对应场景，则置无效值(大于3bit操作值则为无效值)

//										os_printf("[Tips_uartZigb]: queryData is %02X %02X %02X.\n", datsTempRX_zigbSysCtrl.dats[6], datsTempRX_zigbSysCtrl.dats[7], datsTempRX_zigbSysCtrl.dats[8]);

										datsTempTX_zigbSysCtrl.dats[1] = COLONY_DATAMANAGE_CTRLEATHER[datsTempRX_zigbSysCtrl.dats[6]]; //最近一次端口A互控操作值获取
										datsTempTX_zigbSysCtrl.dats[2] = COLONY_DATAMANAGE_CTRLEATHER[datsTempRX_zigbSysCtrl.dats[7]]; //最近一次端口B互控操作值获取
										datsTempTX_zigbSysCtrl.dats[3] = COLONY_DATAMANAGE_CTRLEATHER[datsTempRX_zigbSysCtrl.dats[8]]; //最近一次端口C互控操作值获取
										datsTempTX_zigbSysCtrl.command = ZIGB_SYSCMD_COLONYPARAM_REQPERIOD;
										datsTempTX_zigbSysCtrl.datsLen = 4;

										nodeCMDtranslate_EN = true; //集群控制信息，查询响应

									}break;

									default:break;
								}

								if(nodeCMDtranslate_EN){
								
									bool TXCMP_FLG = false;
								
									memset(datsKernel_TXbuffer, 0, sizeof(u8) * zigB_datsTX_bufferLen);
									ZigB_sysCtrlFrameLoad(datsKernel_TXbuffer, datsTempTX_zigbSysCtrl);

									TXCMP_FLG = ZigB_datsTX_ASY(local_datsRX->datsSTT.stt_MSG.Addr_from, 
																ZIGB_ENDPOINT_CTRLSYSZIGB,
																ZIGB_ENDPOINT_CTRLSYSZIGB,
																ZIGB_CLUSTER_DEFAULT_CULSTERID,
																(u8 *)datsKernel_TXbuffer,
																2 + datsTempTX_zigbSysCtrl.datsLen, //命令长度 1 + 数据长度说明 1 + 数据长度 n 
																localZigbASYDT_bufQueueRemoteReq,
																zigB_remoteDataTransASY_QbuffLen);
								}

							}break;

							default:{//其余端口

								u8 srcPoint = local_datsRX->datsSTT.stt_MSG.srcEP;

								if(srcPoint > CTRLEATHER_PORT_NUMSTART && srcPoint < CTRLEATHER_PORT_NUMTAIL){ //余下端口：0x11<17> - 0xfe<254>用作互控

									u8 statusRelay_temp = status_actuatorRelay; //当前开关状态缓存

									if((srcPoint == CTRLEATHER_PORT[0]) && (0 != CTRLEATHER_PORT[0])){ //开关位 1 互控绑定端口判定
									
										swCommand_fromUsr.actMethod = relay_OnOff;
										statusRelay_temp &= ~(1 << 0); //动作缓存位清零
										swCommand_fromUsr.objRelay = statusRelay_temp | local_datsRX->datsSTT.stt_MSG.dats[0] << 0; //bit0 开关动作位 动作响应
										(local_datsRX->datsSTT.stt_MSG.dats[0])?(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[0]] = STATUSLOCALEACTRL_VALMASKRESERVE_ON):(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[0]] = STATUSLOCALEACTRL_VALMASKRESERVE_OFF); //集群控制信息管理，互控信息掩码更新
									}
									else
									if((srcPoint == CTRLEATHER_PORT[1]) && (0 != CTRLEATHER_PORT[1])){ //开关位 2 互控绑定端口判定
									
										swCommand_fromUsr.actMethod = relay_OnOff;
										statusRelay_temp &= ~(1 << 1); //动作缓存位清零
										swCommand_fromUsr.objRelay = statusRelay_temp | local_datsRX->datsSTT.stt_MSG.dats[0] << 1; //bit1 开关动作位 动作响应
										(local_datsRX->datsSTT.stt_MSG.dats[0])?(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[1]] = STATUSLOCALEACTRL_VALMASKRESERVE_ON):(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[1]] = STATUSLOCALEACTRL_VALMASKRESERVE_OFF); //集群控制信息管理，互控信息掩码更新
									}
									else
									if((srcPoint == CTRLEATHER_PORT[2]) && (0 != CTRLEATHER_PORT[2])){ //开关位 3 互控绑定端口判定
									
										swCommand_fromUsr.actMethod = relay_OnOff;
										statusRelay_temp &= ~(1 << 2); //动作缓存位清零
										swCommand_fromUsr.objRelay = statusRelay_temp | local_datsRX->datsSTT.stt_MSG.dats[0] << 2; //bit2 开关动作位 动作响应
										(local_datsRX->datsSTT.stt_MSG.dats[0])?(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[2]] = STATUSLOCALEACTRL_VALMASKRESERVE_ON):(COLONY_DATAMANAGE_CTRLEATHER[CTRLEATHER_PORT[2]] = STATUSLOCALEACTRL_VALMASKRESERVE_OFF); //集群控制信息管理，互控信息掩码更新
									}
								}

								devStatus_pushIF = true; //开关状态数据推送
							
							}break;
						}
	
					}break;
	
					//======协议层数据类型：新节点上线=====//
					case zigbTP_ntCONNECT:{
	
						os_printf("[Tips_uartZigb]: new node[0x%04X] online.\n", local_datsRX->datsSTT.stt_ONLINE.nwkAddr_fresh);
						
					}break;
	
					default:{
	
						
					
					}break;
				}
			}

		}
		else{//zigb网络掉线处理

			ZigB_nwkReconnect();
		}

		/*>>>>>>zigb非阻塞远端数据传输<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<*/
		{

			portBASE_TYPE xMsgQ_rcvResult = pdFALSE;
			sttUartRcv_rmDatsReqResp rptr_uartDatsRcv;
			sttUartRcv_scenarioCtrlResp rptr_scenarioCtrlResp;

			u8 loop_Insert_temp = 0; //索引缓存

			do{ //队列接收应答判断后将对应指令数据从发送队列中移除

				xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_rmDataReqResp, (void *)&rptr_uartDatsRcv, (portTickType) 0);
				if(xMsgQ_rcvResult == pdTRUE){

//					os_printf(">>>:Qcome: Len-%02d, H-%02X, T-%02X.\n", rptr_uartDatsRcv.datsLen, rptr_uartDatsRcv.dats[0], rptr_uartDatsRcv.dats[rptr_uartDatsRcv.datsLen - 1]);

					while(loop_Insert_temp < zigB_remoteDataTransASY_QbuffLen){ //普通数据转发应答判断

						if( localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].repeat_Loop){

							if(!memcmp(rptr_uartDatsRcv.dats, localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp, localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp_Len)){

								localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].repeat_Loop = 0; //有正确的应答响应，提前结束数据发送
								memset(localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp, 0, sizeof(u8) * localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp_Len); //对应应答缓存清零
								memcpy(&localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp], &localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp + 1], (zigB_remoteDataTransASY_QbuffLen - loop_Insert_temp - 1) * sizeof(stt_dataRemoteReq)); //缓存整理

//								os_printf("bingo.\n");

								break; //当前可用应答使用完毕，一次应答只能用一次，不能重复共用
								
							}else{

//								os_printf(">>>:mShud: Len-%02d, h-%02X, t-%02X.\n", localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp_Len, localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp[0], localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp[localZigbASYDT_bufQueueRemoteReq[loop_Insert_temp].dataResp_Len - 1]);
							}
						}

						loop_Insert_temp ++;	
					}
					
					loop_Insert_temp = 0;  //谨记清零,单次应答只用单次取消指令下达
				}
				
			}
			while(xMsgQ_rcvResult == pdTRUE);

			if(local_insertRecord_datsReqNormal < zigB_remoteDataTransASY_QbuffLen){ //异步非阻塞常规数据请求

				if(!localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].dataReqPeriod && localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].repeat_Loop){
				
					localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].dataReqPeriod = zigB_remoteDataTransASY_txPeriod; //轮发周期更新
					localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].repeat_Loop --; //轮发次数更新
				
					uartZigbee_putDats(UART0, localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].dataReq, localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].dataReq_Len);
					vTaskDelay(zigB_remoteDataTransASY_txUartOnceWait);

					if(!localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].repeat_Loop){

						os_printf("preFail_rmDatatx warning, nwkAddr<0x%02X%02X>.\n", localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].dataReq[5], localZigbASYDT_bufQueueRemoteReq[local_insertRecord_datsReqNormal].dataReq[4]);
					}
				}
				
				local_insertRecord_datsReqNormal ++;
				
			}else{

				local_insertRecord_datsReqNormal = 0;
			}

			if(localZigbASYDT_scenarioCtrlReserveAllnum){ //场景异步发送操作是否可用

				os_timer_disarm(&timer_zigbNodeDevDetectManage); //场景操作期间，设备链表监管定时器挂起暂停

				do{
					
					xMsgQ_rcvResult = xQueueReceive(xMsgQ_uartToutDats_scenarioCtrlResp, (void *)&rptr_scenarioCtrlResp, (portTickType) 0);
					if(xMsgQ_rcvResult == pdTRUE){
	
						while(loop_Insert_temp < zigB_ScenarioCtrlDataTransASY_opreatOnceNum){ //场景控制数据应答判断-单轮
						
							if(localZigbASYDT_bufQueueScenarioReq[loop_Insert_temp].repeat_Loop){
						
								if(rptr_scenarioCtrlResp.respNwkAddr == localZigbASYDT_bufQueueScenarioReq[loop_Insert_temp].dataRespNwkAddr){
						
									memset(&localZigbASYDT_bufQueueScenarioReq[loop_Insert_temp], 0, sizeof(stt_dataScenarioReq)); //有正确的应答响应，提前结束数据发送
									rptr_scenarioCtrlResp.respNwkAddr = 0; //对应应答地址缓存清零
									localZigbASYDT_bufQueueScenarioReq[loop_Insert_temp].scenarioOpreatCmp_flg = 1; //单元场景成功完成标志置位更新
	
//									os_printf("bingo.\n");
								
									break; //当前可用应答使用完毕，一次应答只能用一次，不能重复共用
									
								}else{
						
//									os_printf(">>>addrShud: %04X, addrQ:%04X.\n", localZigbASYDT_bufQueueScenarioReq[loop_Insert_temp].dataRespNwkAddr, rptr_scenarioCtrlResp.respNwkAddr);
								}
								
							}else{

							}
						
							loop_Insert_temp ++;	
						}
	
						loop_Insert_temp = 0;  //谨记清零,单次应答只用单次取消指令下达

					}
				}
				while(xMsgQ_rcvResult == pdTRUE);
	
				if(local_insertRecord_datsReqScenario < zigB_ScenarioCtrlDataTransASY_opreatOnceNum){ //异步非阻塞场景控制数据请求
	
					if(!localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].dataReqPeriod && localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].repeat_Loop){
					
						localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].dataReqPeriod = zigB_ScenarioCtrlDataTransASY_txPeriod; //轮发周期更新
						localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].repeat_Loop --; //轮发次数更新
					
						uartZigbee_putDats(UART0, localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].dataReq, localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].dataReq_Len);
						vTaskDelay(zigB_ScenarioCtrlDataTransASY_txUartOnceWait);
	
						if(!localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].repeat_Loop){
	
							os_printf("preFail_qScenario warning, nwkAddr<0x%02X%02X>.\n", localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].dataReq[5], localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].dataReq[4]);
						}
					}
					else if(localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].scenarioOpreatCmp_flg){ //场景单元完成标志判断并记录

						local_ctrlRecord_reserveLoopInsert --; //场景单位操作单轮可用数更新(单元成功)
						
					}
					else if(!localZigbASYDT_bufQueueScenarioReq[local_insertRecord_datsReqScenario].repeat_Loop){ //场景单元完成标志判断并记录

						local_ctrlRecord_reserveLoopInsert --; //场景单位操作单轮可用数更新(单元重复发送次数失效)
						
					}

					local_insertRecord_datsReqScenario ++; //索引更新
					
				}
				else{

					if(!local_ctrlRecord_reserveLoopInsert){ //场景单位操作可用数为0，执行下一轮
					
						local_ctrlRecord_reserveLoopInsert = zigB_ScenarioCtrlDataTransASY_opreatOnceNum; //场景单位操作单轮可用数复位
						(localZigbASYDT_scenarioCtrlReserveAllnum < zigB_ScenarioCtrlDataTransASY_opreatOnceNum)?\
						(localZigbASYDT_scenarioCtrlReserveAllnum = 0):\
						(localZigbASYDT_scenarioCtrlReserveAllnum -= zigB_ScenarioCtrlDataTransASY_opreatOnceNum); 	//场景单位操作整场可用数更新（禁止溢出操作）
						memcpy(&localZigbASYDT_bufQueueScenarioReq[0], &localZigbASYDT_bufQueueScenarioReq[zigB_ScenarioCtrlDataTransASY_opreatOnceNum], localZigbASYDT_scenarioCtrlReserveAllnum * sizeof(stt_dataScenarioReq)); //可用操作数据前挪
	
						os_printf("scenario onceLoopOpt over, reserve Num:%d.\n", localZigbASYDT_scenarioCtrlReserveAllnum);
						
					}else{
					
//						os_printf("scenario onceOpt fail:%d.\n", reserve_loopInsert);
					}
	
					local_insertRecord_datsReqScenario = 0;
					local_ctrlRecord_reserveLoopInsert = zigB_ScenarioCtrlDataTransASY_opreatOnceNum; //单轮重计
				}

			}
			else{

				os_timer_arm(&timer_zigbNodeDevDetectManage, zigbDetect_nwkNodeDev_Period, true); //恢复运行设备链表监管定时器
			}
		}
		vTaskDelay(1);
	}

	vTaskDelete(NULL);
}

void ICACHE_FLASH_ATTR
zigbee_mainThreadStart(void){

	portBASE_TYPE xReturn = pdFAIL;

	appMsgQueueCreat_Z2S();
	appMsgQueueCreat_uartToutDatsRcv();
	appMsgQueueCreat_timeStampGet();
	appMsgQueueCreat_zigbFunRemind();
	
	xReturn = xTaskCreate(zigbeeDataTransProcess_task, "Process_Zigbee", 2048, (void *)NULL, 4, &pxTaskHandle_threadZigbee);
	os_printf("\nppxTaskHandle_threadZigbee is %d\n", pxTaskHandle_threadZigbee);
}



