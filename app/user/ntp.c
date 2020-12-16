#include "ntp.h"
#include "c_types.h"

#define DN_Server "www.baidu.com"

struct espconn ST_NetCon;
struct espconn ST_NetCon;
ip_addr_t IP_Server;

/*
 *
 */
void get_ip_by_dns(){
	ST_NetCon.type = ESPCONN_TCP;
	ST_NetCon.proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
	ST_NetCon.proto.tcp->local_port = espconn_port();
	ST_NetCon.proto.tcp->remote_port = 80;
	espconn_gethostbyname(&ST_NetCon, DN_Server, &IP_Server, &DNS_Over_Cb);
	espconn_regist_connectcb(&ST_NetCon, tcp_connect_cb);
	espconn_regist_reconcb(&ST_NetCon, tcp_break_cb);

}

/*
 *域名解析回调函数
 */
void DNS_Over_Cb(const char *name, ip_addr_t *ipaddr, void *callback_arg){
	struct espconn *T_arg = (struct espconn *)callback_arg;
	// 域名解析失败
	if(ipaddr == NULL){
		os_printf("---- DomainName Analyse Failed ----\n");
		return;
	}else if (ipaddr != NULL && ipaddr->addr != 0){
		os_printf("\r\n---- DomainName Analyse Succeed ----\r\n");
		IP_Server.addr = ipaddr->addr;
	}
}

void tcp_connect_cb(){

}

void tcp_break_cb(){

}
