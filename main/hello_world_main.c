#include <stdio.h>
#include <string.h>


#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"

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
static const char* TAG = "WIFI";
static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;


typedef struct message {
    uint8_t header[12];
    uint8_t payload[1024];
} Message;


// TODO: Saber usar nvs
uint16_t global_id  = 91;
int sock;

uint8_t current_tl_protocol;
uint8_t current_id_protocol;

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

void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}
//header(id = 24, mac=33, tp=0, idp=1, len=0)
//Recordar que este asunto es little endian!
void add_headers(Message *m, uint16_t id, uint64_t mac, uint8_t tp, uint8_t idp, uint16_t len) {
    //sacamos el id 16
    uint8_t *px = (uint8_t *)&id;
    uint8_t hi = px[1];
    uint8_t lo = px[0];
    m->header[0] = hi;
    m->header[1] = lo;

    //Sacamos la mac 48
    px = (uint8_t *)&mac;
    m->header[2] = px[5];
    m->header[3] = px[4];
    m->header[4] = px[3];
    m->header[5] = px[2];
    m->header[6] = px[1];
    m->header[7] = px[0];

    //Sacamos tp y idp 8 y 8
    m->header[8] = tp;
    m->header[9] = idp;

    //sacamos len 16
    px = (uint8_t *)&len;
    m->header[10] = px[1];
    m->header[11] = px[0];
}


void add_random_data_p0(Message *m) {
    uint8_t bat_lvl = 58;//esp_random() % 101;
    m->payload[0] = bat_lvl;
}

void add_random_data_p1(Message *m) {
    uint8_t bat_lvl = 58;//esp_random() % 101;
    m->payload[0] = bat_lvl;
    m->payload[1] = 0;
    m->payload[2] = 123;
    m->payload[3] = 121;
    m->payload[4] = 244;
}

void add_random_data_p2(Message *m) {

}

void initial_sequence() {


    uint8_t rx_buffer[2];
    recv(sock,rx_buffer, sizeof(rx_buffer), 0);

    current_id_protocol = rx_buffer[0];
    current_tl_protocol = rx_buffer[1];
    ESP_LOGI(TAG, "TLID: %d,  IDP: %d", current_tl_protocol, current_id_protocol);

    // char headers[12] = {0, 24, 0, 0, 0, 0, 0, 33, 0, 1, 0, 0};
    // header:= (120)b (120)b
        
    // Enviar mensaje "Hola Mundo"
    //send(sock, headers, 12, 0);

    //clear_headers();
    // Recibir respuesta


    //int rx_len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
    //if (rx_len < 0) {
    //    ESP_LOGE(TAG, "Error al recibir datos");
    //    return;
    //}
    //ESP_LOGI(TAG, "Datos recibidos: %s", rx_buffer);

    // Cerrar el socket
    //ESP_LOGI(TAG, "Cerrando socket! Duermo por 20 segundos");

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

void app_main(void) {
    nvs_init();
    wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    ESP_LOGI(TAG,"Conectado a WiFi!\n");

    // ---------------------------------
    // Inicializacion del primer socket
    ESP_LOGI(TAG, "Iniciando la primera conexion!");
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr.s_addr);

    // Crear un socket
    ESP_LOGI(TAG, "Abriendo socket!");
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error al crear el socket");
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

    while(1) {
        switch (current_id_protocol) {
            case 0:
                Message m = {};
                uint16_t len = 12+1;
                add_headers(&m, global_id++, 32, current_tl_protocol, current_id_protocol, len);
                add_random_data_p0(&m);
                printf("Enviando este mensaje!\n");
                print_message(&m, len);
                send(sock, (char *) &m, len, 0);
                vTaskDelay(5000/portTICK_PERIOD_MS);
                break;
            case 1:
                ESP_LOGE(TAG, "Caso %d, No implementado!", current_id_protocol);
                vTaskDelay(5000/portTICK_PERIOD_MS);
                goto close;
                break;
            case 2:
                ESP_LOGE(TAG, "Caso %d, No implementado!", current_id_protocol);
                vTaskDelay(5000/portTICK_PERIOD_MS);
                goto close;
                break;
            case 3:
                ESP_LOGE(TAG, "Caso %d, No implementado!", current_id_protocol);
                break;
            case 4:
                ESP_LOGE(TAG, "Caso %d, No implementado!", current_id_protocol);
                break;
            default:
                ESP_LOGE(TAG, "Formato equivocado!");
                break;
        }
        ESP_LOGI(TAG, "Desperte!");
    }

    // Esto no deberia pasar!
    close:
    ESP_LOGI(TAG, "Hubo un error critico!, matando la esp");
    close(sock);
}
