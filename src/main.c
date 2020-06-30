#include "esp_common.h"
#include "esp_wifi.h"
#include "esp8266/esp8266.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "espconn/espconn_tcp.h"
#include "gpio.h"
// Ajouter 
// void gpio_config(GPIO_ConfigTypeDef *pGPIOConfig);

// #define ETS_GPIO_INTR_ENABLE() _xt_isr_unmask(1 << ETS_GPIO_INUM) //ENABLE INTERRUPTS
// #define ETS_GPIO_INTR_DISABLE() _xt_isr_mask(1 << ETS_GPIO_INUM) //DISABLE INTERRUPTS

// dans gpio.h

// Dectection mouvement à l'aide d'un HC-SR505 PIR.
// au démarrage, l'esp8266 tente de se connecter à la box avec une IP 192.168.1.2x
// une fois connecté, le device s'endors. Si une détection du capteur a lieu, alors l'esp8266 se reveille,
// se connecter à la box et réalise un http post de la détection puis se rendors

// Gestion sleep esp8266
// Gestion connexion wifi
// Gestion http post

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define WIFI_SSID       "freeboxMock"
#define WIFI_PASS       "pisterr63"
#define MAXIMUM_RETRY   3
#define SERVER_IP       "http://api.mockpoc.fr/light/list"
#define SERVER_PORT     80

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;
xSemaphoreHandle semaphore;

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;
    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

LOCAL void ICACHE_FLASH_ATTR recv_cb(void *arg, char *data, uint16_t len) {
    // Store the IP address from the sender of this data.
    struct espconn *conn = (struct espconn *)arg;
    os_printf("Received %d bytes from "IPSTR" \n", len, IP2STR(conn->proto.tcp->remote_ip));

    // Send out the received data via serial in hexadecimal, 16 characters to a line.
    for (uint16_t ii = 0; ii < len; ii++) {
        os_printf("%02x ", data[ii]);
        if ((ii % 16) == 15) {
            // We have reached the end of a line
            os_printf("\n");
        }
    }
}

LOCAL void ICACHE_FLASH_ATTR tcp_connect_cb(void *arg) 
{
    struct espconn *conn = (struct espconn *)arg;
    os_printf("TCP connection received from "IPSTR":%d\n",
              IP2STR(conn->proto.tcp->remote_ip), conn->proto.tcp->remote_port);
    espconn_regist_recvcb(conn, recv_cb);
}

void espconn_connect_cb(void* arg)
{
    printf("esp connect cb, id : %d\n", (int)arg);
    // xSemaphoreGive(semaphore);
}
void espconn_reconnect_cb(void* arg, sint8 err)
{
    printf("esp reconnect cb\n");
}

void dns_found_cb(const char *name, ip_addr_t *ipaddr, void *callback_arg)
{
    printf("dns found cb\n");
    xSemaphoreGive(semaphore);
}

void espconn_recv_cb(void* arg, char* pdata, unsigned short len)
{
    printf("receive data cb ! \n");
    printf("pdata");
}

void espconn_sent_cb(void* arg)
{
    printf("send data cb  !\n");
}

void send_data(void)
{
    printf("sending...\n");
    // Structure holding the TCP connection information.
    LOCAL struct espconn tcp_conn;

    // TCP specific protocol structure.
    LOCAL esp_tcp tcp_proto;

    tcp_proto.connect_callback = espconn_connect_cb;
    tcp_proto.reconnect_callback = espconn_reconnect_cb;
    tcp_proto.disconnect_callback = espconn_connect_cb;
    tcp_proto.write_finish_fn = espconn_connect_cb;

    tcp_proto.local_ip[0] = 192;
    tcp_proto.local_ip[1] = 168;
    tcp_proto.local_ip[2] = 1;
    tcp_proto.local_ip[3] = 30;
    // tcp_proto.remote_ip[0] = 78;
    // tcp_proto.remote_ip[1] = 224;
    // tcp_proto.remote_ip[0] = 39;
    // tcp_proto.remote_ip[0] = 24;
    // Set up the TCP server.
    tcp_proto.local_port = 80;
    tcp_proto.remote_port = 80;

    tcp_conn.type = ESPCONN_TCP;
    tcp_conn.state = ESPCONN_NONE;
    tcp_conn.proto.tcp = &tcp_proto;
    tcp_conn.recv_callback = espconn_recv_cb;
    tcp_conn.sent_callback = espconn_sent_cb;
    ip_addr_t ipAddr;
    dns_init();
    err_t  err = espconn_gethostbyname(&tcp_conn, SERVER_IP, &ipAddr, dns_found_cb);
    uint8_t message[] = "GET /light/list HTTP/1.1\r\n   \
    Host: api.mockpoc.fr\r\n                            \
    \r\n";                                                       
    xSemaphoreTake(semaphore, portMAX_DELAY);
    printf("ip adr : %d.%d.%d.%d\n", tcp_proto.remote_ip[0], tcp_proto.remote_ip[1], tcp_proto.remote_ip[2], tcp_proto.remote_ip[3] );
    espconn_regist_connectcb(&tcp_conn, tcp_connect_cb);
    espconn_connect(&tcp_conn); 
    espconn_accept(&tcp_conn);
    // xSemaphoreTake(semaphore, portMAX_DELAY);
    int8_t res = espconn_send(&tcp_conn, message, sizeof(message));
    printf("data sent !\n");
}

LOCAL void ICACHE_FLASH_ATTR wifi_event_cb(System_Event_t *event) {
    struct ip_info info;

    // To determine what actually happened, we need to look at the event.
    switch (event->event_id) {
        case EVENT_STAMODE_CONNECTED: {
            // We are connected as a station, but we don't have an IP address yet.
            char ssid[33];
            uint8_t len = event->event_info.connected.ssid_len;
            if (len > 32) {
                len = 32;
                }
            strncpy(ssid, event->event_info.connected.ssid, len + 1);
            os_printf("Received EVENT_STAMODE_CONNECTED. "
                      "SSID = %s, BSSID = "MACSTR", channel = %d.\n",
                      ssid, MAC2STR(event->event_info.connected.bssid), event->event_info.connected.channel);
            break;
        }
        case EVENT_STAMODE_DISCONNECTED: {
            // We have been disconnected as a station.
            char ssid[33];
            uint8_t len = event->event_info.connected.ssid_len;
            if (len > 32) {
                len = 32;
            }
            strncpy(ssid, event->event_info.connected.ssid, len + 1);
            os_printf("Received EVENT_STAMODE_DISCONNECTED. "
                      "SSID = %s, BSSID = "MACSTR", channel = %d.\n",
                      ssid, MAC2STR(event->event_info.disconnected.bssid), event->event_info.disconnected.reason);
            break;
        }
        case EVENT_STAMODE_GOT_IP:
            // We have an IP address, ready to run. Return the IP address, too.
            os_printf("Received EVENT_STAMODE_GOT_IP. IP = "IPSTR", mask = "IPSTR", gateway = "IPSTR"\n", 
                      IP2STR(&event->event_info.got_ip.ip.addr), 
                      IP2STR(&event->event_info.got_ip.mask.addr),
                      IP2STR(&event->event_info.got_ip.gw));
            break;
        case EVENT_STAMODE_DHCP_TIMEOUT:
            // We couldn't get an IP address via DHCP, so we'll have to try re-connecting.
            os_printf("Received EVENT_STAMODE_DHCP_TIMEOUT.\n");
            wifi_station_disconnect();
            wifi_station_connect();
            break;
    }
}

void wifi_event_handler_cb(System_Event_t *event)
{
    switch(event->event_id) 
    {
    case EVENT_STAMODE_SCAN_DONE :         /**< ESP8266 station finish scanning AP */
        printf("EVENT_STAMODE_SCAN_DONE\n");
        break;
    case EVENT_STAMODE_CONNECTED :         /**< ESP8266 station connected to AP */
        printf("EVENT_STAMODE_CONNECTED\n");
        break; 
    case EVENT_STAMODE_DISCONNECTED :      /**< ESP8266 station disconnected to AP */
        printf("EVENT_STAMODE_DISCONNECTED\n");
        break; 
    case EVENT_STAMODE_AUTHMODE_CHANGE :   /**< the auth mode of AP connected by ESP8266 station changed */
        printf("EVENT_STAMODE_AUTHMODE_CHANGE\n");
        break; 
    case EVENT_STAMODE_GOT_IP :            /**< ESP8266 station got IP from connected AP */
        printf("EVENT_STAMODE_GOT_IP\n");
        xSemaphoreGive(semaphore);
        break; 
    case EVENT_STAMODE_DHCP_TIMEOUT :      /**< ESP8266 station dhcp client got IP timeout */
        printf("EVENT_STAMODE_DHCP_TIMEOUT\n");
        break; 
    case EVENT_SOFTAPMODE_STACONNECTED :   /**< a station connected to ESP8266 soft-AP */
        printf("EVENT_SOFTAPMODE_STACONNECTED\n");
        break; 
    case EVENT_SOFTAPMODE_STADISCONNECTED : /**< a station disconnected to ESP8266 soft-AP */
        printf("EVENT_SOFTAPMODE_STADISCONNECTED\n");
        break;
    case EVENT_SOFTAPMODE_PROBEREQRECVED : /**< Receive probe request packet in soft-AP interface */
        printf("EVENT_SOFTAPMODE_PROBEREQRECVED\n");
        break; 
    defaut :
        printf("unknown event\n");
    }
}


void wifi_init_stack(void)
{
    struct station_config config = {
    .ssid = WIFI_SSID,         /**< SSID of target AP*/
    .password = WIFI_PASS,     /**< password of target AP*/
    .bssid_set = 0,        /**< whether set MAC address of target AP or not. Generally, station_config.bssid_set needs to be 0; and it needs to be 1 only when users need to check the MAC address of the AP.*/
    .bssid = ""         /**< MAC address of target AP*/
    };

    bool ret =  wifi_set_event_handler_cb(wifi_event_handler_cb);
    if(ret == true) printf("cb registered ! \n");
    ret = wifi_set_opmode(STATION_MODE);
    if(ret == true) printf("station mode set ! \n");
    ret = wifi_station_set_config(&config);
    if(ret == true) printf("station mode configured ! \n");
    ret = wifi_station_connect();
    if(ret == true) printf("connected ! \n");
}

void gpio_0_intr(void* pArg)
{
    uint16 gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    if (gpio_status & (BIT(0)))
    {
        printf("pressed ! \n");
    }
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
}

void init_gpio(void)
{
    GPIO_ConfigTypeDef gpioCfg = {
    .GPIO_Pin = GPIO_Pin_0,
    .GPIO_Mode = GPIO_Mode_Input,
    .GPIO_Pullup = GPIO_PullUp_EN,
    .GPIO_IntrType =GPIO_PIN_INTR_NEGEDGE
    };
    gpio_config(&gpioCfg);
    gpio_intr_handler_register(gpio_0_intr, NULL);
    ETS_GPIO_INTR_ENABLE();
}

void http_post(void)
{

}

void init_sleep(void)
{

}

void task_main(void* ignore)
{
    xSemaphoreTake(semaphore, portMAX_DELAY);
    init_gpio();
    // wifi_init_stack();
    // xSemaphoreTake(semaphore, portMAX_DELAY);
    // printf("will send message ...\n");
    // send_data();
    // read_state();
    while(1)
    {
        vTaskDelay(100);
    }
    // http_post();
    // init_sleep();
}


void user_init(void)
{
    printf("Starting ...\n");
    printf("sdk version : %s\n", system_get_sdk_version());
    vSemaphoreCreateBinary( semaphore );
    xTaskCreate(&task_main, "startup", 2048, NULL, 1, NULL);
}