#include <stdio.h>
#include <string.h>

#include "math.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "inttypes.h"

// Agregar Imports de Primer Conexion WiFi


#include "lwip/sockets.h" // Para sockets

#define SERVER_IP     "192.168.4.1" // IP del servidor (su computador o raspberry)
#define SERVER_PORT   8888


// Agregar funciones de Primer Conexion WiFi (nv_init y wifi_init_sta)

//Credenciales de WiFi

#define WIFI_SSID "rpi-wifi"
#define WIFI_PASSWORD "wena4321"

// Variables de WiFi
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static const char* TAG = "CLIENT_ESP32";
static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;


typedef struct message {
    uint8_t header[12];
    uint8_t payload[1024];
} Message;


// TODO: Saber usar nvs
uint16_t global_id;
nvs_handle_t app_nvs_handle;
int sock;
int udp_sock;
uint8_t current_tl_protocol;
uint8_t current_id_protocol;
uint32_t udp_port;
char* KEY = "IDGLOB";
void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 10) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(char* ssid, char* password) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    // Set the specific fields
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ssid,
                 password);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ssid,
                 password);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
        IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
        WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

esp_err_t read_nvs(char *key, uint16_t *retval) {
    return nvs_get_u16(app_nvs_handle, key, retval);
}

esp_err_t write_nvs(char *key, uint16_t val) {
    return nvs_set_u16(app_nvs_handle,key, val);
}
void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ret = nvs_open(TAG, NVS_READWRITE, &app_nvs_handle);
    ESP_ERROR_CHECK(ret);

    ret = read_nvs(KEY, &global_id);
    if(ret != ESP_OK) {
        ESP_LOGI(TAG, "Creando un nuevo global id en 0!");
        ret = write_nvs(KEY, 0);
        ESP_ERROR_CHECK(ret);
    }
}

//header(id = 24, mac=33, tp=0, idp=1, len=0)
//Recordar que este asunto es little endian!
void add_headers(Message *m, uint16_t id, uint8_t  *mac, uint8_t tp, uint8_t idp, uint16_t len) {
    //sacamos el id 16
    uint8_t *px = (uint8_t *)&id;
    uint8_t hi = px[1];
    uint8_t lo = px[0];
    m->header[0] = lo;
    m->header[1] = hi;

    //Sacamos la mac 48
    px = mac;
    m->header[2] = px[0];//px[5];
    m->header[3] = px[1];//px[4];
    m->header[4] = px[2];//px[3];
    m->header[5] = px[3];//px[2];
    m->header[6] = px[4];//px[1];
    m->header[7] = px[5];//px[0];

    //Sacamos tp y idp 8 y 8
    m->header[8] = tp;
    m->header[9] = idp;

    //sacamos len 16
    px = (uint8_t *)&len;
    m->header[10] = px[0];
    m->header[11] = px[1];
}


void add_random_data_p0(Message *m) {
    uint8_t bat_lvl = (esp_random() % 100) + 1;
    m->payload[0] = bat_lvl;
}

void add_random_data_p1(Message *m) {
    uint8_t bat_lvl = (esp_random() % 100) + 1;
    uint32_t tstamp = 12381;
    m->payload[0] = bat_lvl;
    memcpy(&(m->payload[0])+1, &tstamp, sizeof(uint32_t));
}

void add_random_data_p2(Message *m) {
    uint8_t bat_lvl = (esp_random() % 100) + 1;
    m->payload[0] = bat_lvl;
    uint32_t tstamp = 12381;
    memcpy(&(m->payload[0])+1, &tstamp, sizeof(uint32_t));
    uint8_t temp = (esp_random() % 26) + 5;
    m->payload[5] = temp;
    uint32_t press = (esp_random() % 201) + 1000;
    memcpy(&(m->payload[5])+1, &press, sizeof(uint32_t));
    uint8_t hum = (esp_random() % 51) + 30;
    m->payload[10] = hum;
    float co = ((esp_random() % 1710) + 300)/ 10.0 ;
    memcpy(&(m->payload[10])+1, &co, sizeof(float));
}


void add_random_data_p3(Message *m) {
    uint8_t bat_lvl = (esp_random() % 100) + 1;
    m->payload[0] = bat_lvl;
    uint32_t tstamp = 12381;
    memcpy(&(m->payload[0])+1, &tstamp, sizeof(uint32_t));
    uint8_t temp = (esp_random() % 26) + 5;
    m->payload[5] = temp;
    uint32_t press = (esp_random() % 201) + 1000;
    memcpy(&(m->payload[5])+1, &press, sizeof(uint32_t));
    uint8_t hum = (esp_random() % 51) + 30;
    m->payload[10] = hum;
    float co = ((esp_random() % 171) + 30)/ 1.0F ;
    memcpy(&(m->payload[10])+1, &co, sizeof(float));


    float ampx = ((esp_random() % 611) + 590) / 100000.0F;
    float freqx = ((esp_random() % 30) + 290) / 10.0F;
    float ampy = ((esp_random() % 691) + 410) / 100000.0F;
    float freqy = ((esp_random() % 30) + 590) / 10.0F;
    float ampz = ((esp_random() % 143) + 8) / 1000.0F;
    float freqz = ((esp_random() % 30) + 890) / 10.0F;
    float RMS = sqrtf(freqy*freqy + freqx * freqx);
    memcpy(&(m->payload[14])+1, &RMS, sizeof(float));
    memcpy(&(m->payload[18])+1, &ampx, sizeof(float));
    memcpy(&(m->payload[22])+1, &freqx, sizeof(float));
    memcpy(&(m->payload[26])+1, &ampy, sizeof(float));
    memcpy(&(m->payload[30])+1, &freqy, sizeof(float));
    memcpy(&(m->payload[34])+1, &ampz, sizeof(float));
    memcpy(&(m->payload[38])+1, &freqz, sizeof(float));

}

void initial_sequence() {


    uint8_t rx_buffer[6];
    recv(sock,rx_buffer, sizeof(rx_buffer), 0);

    current_id_protocol = rx_buffer[0];
    current_tl_protocol = rx_buffer[1];
    udp_port = *((uint32_t*) &rx_buffer[2]);
    ESP_LOGI(TAG, "TLID: %d,  IDP: %d, PORT: %" PRIu32 "", current_tl_protocol, current_id_protocol, udp_port);
    
}

void print_message(Message *m, uint16_t len) {
    printf("header: [");
    for(int i = 0; i < 12-1; i++) {
        printf("%d, ",m->header[i]);
    }
    printf("%d]",m->header[12-1]);

    printf("\n");

    printf("Payload: [");
    for(int i = 0; i < len-12-1; i++) {
        printf("%d, ",m->payload[i]);
    }
    printf("%d]",m->payload[len-12-1]);
    printf("\n");


}

/*void sendinfo(int *s_tcp, int *s_udp, const void *data, size_t len,
              uint8_t tpid, struct sockaddr_in* sv_addr, size_t len_sv_addr){
    uint32_t a = sv_addr->sin_addr.s_addr;
    uint8_t *pa = (uint8_t *) &a;
    for (int i = 0; i<4; i++){
        printf("------>%d", *pa);
        pa++;
    }
    printf("Enviando mensaje a el puerto %d\n", (int)sv_addr->sin_addr.s_addr);
    if (tpid == 0) {
        send(*s_tcp, (char *)data, len ,0);
    }
    else if (tpid == 1){
        sendto(*s_udp, (char*)data, len, 0, (struct sockaddr *)sv_addr, len_sv_addr);
    }
    else{
        printf("[ERROR]\tError, wrong tpid %d", tpid);
    }
}

void recvinfo(int *s_tcp, int *s_udp, void *mem, size_t len_buff, uint8_t tpid){
    if (tpid == 0) {
        recv(*s_tcp, mem, len_buff, 0);
    }
    else if (tpid == 1) {
        recvfrom(*s_udp, mem, len_buff, 0, NULL, NULL);
    }
    else {
        printf("[ERROR]\tError, wrong tpid %d", tpid);
    }
}*/

void app_main(void) {
    nvs_init();
    wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    ESP_LOGI(TAG,"Conectado a WiFi!\n");
    uint8_t mac[8] = {0};
    esp_read_mac(mac ,ESP_MAC_WIFI_STA);

    // ---------------------------------
    // Inicializacion del primer socket
    ESP_LOGI(TAG, "Iniciando la primera conexion!");
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr.s_addr);

    // Crear un socket
    ESP_LOGI(TAG, "Abriendo socket! TCP");
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error al crear el socket TCP");
        return;
    }


    // TODO: ver los retrys!
    //  Conectar al servidor
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Error al conectar");
        goto close;
    }

    // ----------------------------------
    initial_sequence(); // aqui sacamos los primeros campos! TPID y ID-Protocol


    // TODO: Decidir bien el socket con el TPID
    // Inicializacion del segundo socket
    struct sockaddr_in server_addr_udp;
    memset(&server_addr_udp, 0, sizeof(struct sockaddr_in));
    server_addr_udp.sin_family = AF_INET;
    inet_pton(AF_INET, SERVER_IP, &(server_addr_udp.sin_addr));
    server_addr_udp.sin_port = htons(udp_port);

    ESP_LOGI(TAG, "Abriendo socket! UDP");
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        ESP_LOGE(TAG, "Error al crear el socket UDP");
        return;
    }
    while(1) {
        Message m = {};

        switch (current_id_protocol) {
            case 0: {
                uint16_t len = 12 + 1; //<- largo a enviar
                add_headers(&m, global_id, mac, current_tl_protocol, current_id_protocol, len);
                add_random_data_p0(&m);
                printf("Enviando este mensaje!\n");
                print_message(&m, len);
                //sendinfo(&sock, &udp_sock, &m, len, current_tl_protocol,&server_addr_udp, sizeof(server_addr_udp));
                if (current_tl_protocol == 0) {
                    send(sock, (char *)&m, len ,0);
                }
                else if (current_tl_protocol == 1){
                    sendto(udp_sock, (char*)&m, len, 0,
                           (struct sockaddr *) &server_addr_udp, sizeof(server_addr_udp));
                }
                //send(sock, (char *) &m, len, 0);
                break;
            }
            case 1: {
                uint16_t len = 12 + 5; //<- largo a enviar!
                add_headers(&m, global_id, mac, current_tl_protocol, current_id_protocol, len);
                add_random_data_p1(&m);
                printf("Enviando este mensaje!\n");
                print_message(&m, len);
                //sendinfo(&sock, &udp_sock, &m, len, current_tl_protocol,&server_addr_udp, sizeof(server_addr_udp));
                //send(sock, (char *) &m, len, 0);
                if (current_tl_protocol == 0) {
                    send(sock, (char *)&m, len ,0);
                }
                else if (current_tl_protocol == 1){
                    sendto(udp_sock, (char*)&m, len, 0,
                           (struct sockaddr *) &server_addr_udp, sizeof(server_addr_udp));
                }
                break;
            }
            case 2: {
                uint16_t len = 12 + 15; //<- largo a enviar!
                add_headers(&m, global_id, mac, current_tl_protocol, current_id_protocol, len);
                add_random_data_p2(&m);
                printf("Enviando este mensaje!\n");
                print_message(&m, len);
                //sendinfo(&sock, &udp_sock, &m, len, current_tl_protocol,&server_addr_udp, sizeof(server_addr_udp));
                //send(sock, (char *) &m, len, 0);
                if (current_tl_protocol == 0) {
                    send(sock, (char *) &m, len, 0);
                } else if (current_tl_protocol == 1) {
                    sendto(udp_sock, (char *) &m, len, 0,
                           (struct sockaddr *) &server_addr_udp, sizeof(server_addr_udp));
                };
                break;
            }
            case 3: {
                uint16_t len = 12 + 43; //<- largo a enviar!
                add_headers(&m, global_id, mac, current_tl_protocol, current_id_protocol, len);
                add_random_data_p3(&m);
                printf("Enviando este mensaje!\n");
                print_message(&m, len);
                //sendinfo(&sock, &udp_sock, &m, len, current_tl_protocol,&server_addr_udp, sizeof(server_addr_udp));
                //send(sock, (char *) &m, len, 0);
                if (current_tl_protocol == 0) {
                    send(sock, (char *) &m, len, 0);
                } else if (current_tl_protocol == 1) {
                    sendto(udp_sock, (char *) &m, len, 0,
                           (struct sockaddr *) &server_addr_udp, sizeof(server_addr_udp));
                }
                break;
            }
            case 4:
                ESP_LOGE(TAG, "Caso %d, No implementado!", current_id_protocol);
                break;
            default:
                ESP_LOGE(TAG, "Formato equivocado!");
                break;
        }
        global_id++;
        write_nvs(KEY, global_id);
        uint8_t rx_buffer[2];
        if (current_tl_protocol== 0) {
            recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        }
        else if (current_tl_protocol == 1) {
            recvfrom(udp_sock, rx_buffer, sizeof(rx_buffer), 0, NULL, NULL);
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Recibi el mensaje!");
        //recvinfo(&sock, &udp_sock, rx_buffer, sizeof(rx_buffer), current_tl_protocol);
        //recv(sock,rx_buffer, sizeof(rx_buffer), 0);

        current_id_protocol = rx_buffer[0];
        current_tl_protocol = rx_buffer[1];
        ESP_LOGI(TAG, "TLID: %d,  IDP: %d, PORT: %" PRIu32 "", current_tl_protocol, current_id_protocol, udp_port);

    }

    // Esto no deberia pasar!
    close:
    ESP_LOGI(TAG, "Hubo un error critico!, matando la esp");
    close(sock);
    //esp_restart();
}
