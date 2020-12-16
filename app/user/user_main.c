#include "ets_sys.h"
#include "osapi.h"
#include "ip_addr.h"
#include "espconn.h"
#include "mem.h"
#include "driver/uart.h"
#include "user_interface.h"
#include "smartconfig.h"
#include "airkiss.h"
#include "driver/hw_timer.h"
#include "sntp.h"

#define DEVICE_TYPE 		"gh_9e2cff3dfa51" //wechat public number
#define DEVICE_ID 			"122475" //model ID
#define DEFAULT_LAN_PORT 	12476
#define STA_INFO_ADDR	0x80

#define SPI_FLASH_SIZE_MAP	6
#define SYSTEM_PARTITION_OTA_SIZE							0x7f000
#define SYSTEM_PARTITION_OTA_2_ADDR							0x101000
#define SYSTEM_PARTITION_RF_CAL_ADDR						0x3fb000
#define SYSTEM_PARTITION_PHY_DATA_ADDR						0x3fc000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR				0x3fd000

uint8_t lan_buf[200];
uint16_t lan_buf_len;
uint8 udp_sent_cnt = 0;
os_timer_t OS_Timer_IP;
os_timer_t sntp_read_timer;
static const partition_item_t at_partition_table[] = {
		{SYSTEM_PARTITION_BOOTLOADER, 0x0, 0x1000 },
		{SYSTEM_PARTITION_OTA_1,0x1000, SYSTEM_PARTITION_OTA_SIZE },
		//{SYSTEM_PARTITION_OTA_2,SYSTEM_PARTITION_OTA_2_ADDR, SYSTEM_PARTITION_OTA_SIZE },
		{SYSTEM_PARTITION_RF_CAL, SYSTEM_PARTITION_RF_CAL_ADDR, 0x1000 },
		{SYSTEM_PARTITION_PHY_DATA, SYSTEM_PARTITION_PHY_DATA_ADDR, 0x1000 },
		{SYSTEM_PARTITION_SYSTEM_PARAMETER,SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR, 0x3000 },
};
const airkiss_config_t akconf = {
		(airkiss_memset_fn) &memset,
		(airkiss_memcpy_fn) &memcpy,
		(airkiss_memcmp_fn) &memcmp, 0,
};

void ICACHE_FLASH_ATTR connect_success();
void ICACHE_FLASH_ATTR OS_Timer_IP_cb();
//void ICACHE_FLASH_ATTR hw_timmer_callback();
void ICACHE_FLASH_ATTR sntp_read_timer_callback();
void ICACHE_FLASH_ATTR my_sntp_init();

void ICACHE_FLASH_ATTR
smartconfig_done(sc_status status, void *pdata) {
	//os_printf("airkiss_version = %s\n", *(smartconfig_get_version()));
	switch (status) {
	case SC_STATUS_WAIT:
		os_printf("SC_STATUS_WAIT\n");
		break;
	case SC_STATUS_FIND_CHANNEL:
		os_printf("SC_STATUS_FIND_CHANNEL\n");
		break;
	case SC_STATUS_GETTING_SSID_PSWD:
		os_printf("SC_STATUS_GETTING_SSID_PSWD\n");
		sc_type *type = pdata;
		if (*type == SC_TYPE_ESPTOUCH) {
			os_printf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
		} else {
			os_printf("SC_TYPE:SC_TYPE_AIRKISS\n");
		}
		break;
	case SC_STATUS_LINK:
		os_printf("SC_STATUS_LINK\n");
		struct station_config *sta_conf = pdata;
		//保存STA信息
		spi_flash_erase_sector(STA_INFO_ADDR);
		spi_flash_write(STA_INFO_ADDR * 4096, (uint32 *) sta_conf, 128);

		wifi_station_set_config(sta_conf);
		wifi_station_disconnect();
		wifi_station_connect();
		break;
	case SC_STATUS_LINK_OVER:
		os_printf("SC_STATUS_LINK_OVER\n");
		if (pdata != NULL) {
			//SC_TYPE_ESPTOUCH
			uint8 phone_ip[4] = { 0 };

			os_memcpy(phone_ip, (uint8*) pdata, 4);
			os_printf("Phone ip: %d.%d.%d.%d\n", phone_ip[0], phone_ip[1],phone_ip[2], phone_ip[3]);
		} else {
			//SC_TYPE_AIRKISS - support airkiss v2.0
			//airkiss_start_discover();
		}
		smartconfig_stop();
		connect_success();
		break;
	}
}

void ICACHE_FLASH_ATTR user_init(void) {
	uart_init(74880, 74880);	// 初始化串口波特率
	os_delay_us(10000);			// 等待串口稳定
	//读取STA信息
	struct station_config STA_INFO;
	os_memset(&STA_INFO, 0, sizeof(struct station_config));
	spi_flash_read(STA_INFO_ADDR * 4096, (uint32 *) &STA_INFO, 96);	// 读出【STA参数】(SSID/PASS)
	STA_INFO.ssid[31] = 0;		// SSID最后添'\0'
	STA_INFO.password[63] = 0;	// APSS最后添'\0'

	os_printf("\n-------------------------------\n");
	os_printf("SDK version:%s\n", system_get_sdk_version());
	os_printf("STA_INFO.ssid=%s\r\nSTA_INFO.password=%s\n",
			STA_INFO.ssid,
			STA_INFO.password);
	os_printf("-------------------------------\n");
	//连接wifi
	wifi_set_opmode(0x01);
	wifi_station_set_config(&STA_INFO);
	wifi_station_connect();

	os_timer_disarm(&OS_Timer_IP);	// 关闭定时器
	os_timer_setfn(&OS_Timer_IP,(os_timer_func_t *)OS_Timer_IP_cb, NULL);	// 设置定时器
	os_timer_arm(&OS_Timer_IP, 333, true);  // 启动定时器

}

void ICACHE_FLASH_ATTR start_smartconfig(void){
	os_printf("start smartconfig...\n");
	smartconfig_set_type(SC_TYPE_AIRKISS); //SC_TYPE_ESPTOUCH,SC_TYPE_AIRKISS,SC_TYPE_ESPTOUCH_AIRKISS
	wifi_set_opmode(STATION_MODE);
	smartconfig_start(smartconfig_done);
	os_printf("Waitting for configration\n");
}

//定时器回调函数，检查wifi状态
void ICACHE_FLASH_ATTR OS_Timer_IP_cb(void){
	uint8 wifi_status = wifi_station_get_connect_status();
	switch(wifi_status){
		case STATION_IDLE:
			os_printf("waitting...\n");
			break;
		case STATION_CONNECTING:
			os_printf("connecting...\n");
			break;
		case STATION_WRONG_PASSWORD:
			os_timer_disarm(&OS_Timer_IP);
			os_printf("wrong password!!!\n");
			start_smartconfig();
			break;
		case STATION_NO_AP_FOUND:
			os_timer_disarm(&OS_Timer_IP);
			os_printf("wifi signal not found!!!\n");
			start_smartconfig();
			break;
		case STATION_CONNECT_FAIL:
			os_timer_disarm(&OS_Timer_IP);
			os_printf("connect failed!!!\n");
			start_smartconfig();
			break;
		case STATION_GOT_IP:
			os_timer_disarm(&OS_Timer_IP);
			os_printf("connect success!!!\n");
			connect_success();
			break;
	}
}

//wifi连接成功
void ICACHE_FLASH_ATTR connect_success(void){
	os_printf("hello word!\n");
//	hw_timer_init(FRC1_SOURCE,1);//设置硬件定时器
//	hw_timer_set_func(&hw_timmer_callback);//设置回调函数
//	hw_timer_arm(333333);//启动定时器
	my_sntp_init();
}

void ICACHE_FLASH_ATTR user_pre_init(void) {
	if (!system_partition_table_regist(at_partition_table,
			sizeof(at_partition_table) / sizeof(at_partition_table[0]),
			SPI_FLASH_SIZE_MAP)) {
		os_printf("system_partition_table_regist fail\n");
		while (1)
			;
	}
}

//void ICACHE_FLASH_ATTR hw_timmer_callback(){
//	os_printf("--hardware timmer--\n");
//}

void ICACHE_FLASH_ATTR
my_sntp_init(void)
{

	sntp_setservername(0,"time.pool.aliyun.com");
	sntp_setservername(1,"ntp1.aliyun.com");
	sntp_setservername(2,"2.cn.pool.ntp.org");
	sntp_init();

	os_timer_disarm(&sntp_read_timer);
	os_timer_setfn(&sntp_read_timer, sntp_read_timer_callback , NULL);
	os_timer_arm(&sntp_read_timer,6000,1);
}

void ICACHE_FLASH_ATTR
sntp_read_timer_callback(void *arg)
{
	os_printf("            ********\n");
	os_printf("------------------------------\n");
	uint32_t time = sntp_get_current_timestamp();
	os_printf("time:%d\n",time);
	os_printf("date:%s",sntp_get_real_time(time));
	os_printf("------------------------------\n");
}
