#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ----------- CONFIGURACIÓN MPU6050 -----------
#define I2C_MASTER_SDA_IO    6
#define I2C_MASTER_SCL_IO    7
#define I2C_MASTER_NUM       0
#define I2C_MASTER_FREQ_HZ   400000 // 400 KHz

#define MPU6050_ADDR         0x68
#define PWR_MGMT_1           0x6B
#define ACCEL_XOUT_H         0x3B
#define ACCEL_YOUT_H         0x3D
#define ACCEL_ZOUT_H         0x3F
#define GYRO_XOUT_H          0x43
#define GYRO_YOUT_H          0x45
#define GYRO_ZOUT_H          0x47
#define GYRO_CONFIG          0x1B
#define ACCEL_CONFIG         0x1C

static const char *TAG = "MPU6050";

// Estructura para offsets de calibración
typedef struct {
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
} mpu6050_calibration_t;

mpu6050_calibration_t calibration = {0};
static int64_t last_time_us = 0;
static float current_yaw = 0.0;

// Umbrales para lógica de mano
#define THRESHOLD_CENTERED_RANGE 30.0f
#define THRESHOLD_REVERSE_RANGE 30.0f
#define THRESHOLD_X_RIGHT 30.0f
#define THRESHOLD_X_LEFT 330.0f
#define THRESHOLD_Y_FORWARD 45.0f
#define THRESHOLD_Y_BACKWARD 315.0f

// Banderas para mensajes
static bool msg_hand_centered_triggered = false;
static bool msg_hand_reverse_triggered = false;
static bool msg_x_right_triggered = false;
static bool msg_x_left_triggered = false;
static bool msg_y_forward_triggered = false;
static bool msg_y_backward_triggered = false;

// ----------- FUNCIONES MPU6050 -----------

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t mpu6050_register_write_byte(uint8_t reg_addr, uint8_t data) {
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_FREQ_HZ / 100);
}

static esp_err_t mpu6050_register_read_bytes(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_ADDR, &reg_addr, 1, data, len, I2C_MASTER_FREQ_HZ / 100);
}

void mpu6050_init(void) {
    ESP_ERROR_CHECK(mpu6050_register_write_byte(PWR_MGMT_1, 0x00));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(mpu6050_register_write_byte(GYRO_CONFIG, 0x00));
    ESP_ERROR_CHECK(mpu6050_register_write_byte(ACCEL_CONFIG, 0x00));
    ESP_LOGI(TAG, "MPU6050 inicializado");
}

int16_t read_raw_data(uint8_t register_addr) {
    uint8_t data[2];
    esp_err_t err = mpu6050_register_read_bytes(register_addr, data, 2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error leyendo datos crudos del registro 0x%02X: %s", register_addr, esp_err_to_name(err));
        return 0;
    }
    return (data[0] << 8) | data[1];
}

void calibrate_mpu6050(int samples) {
    ESP_LOGI(TAG, "Iniciando calibracion del MPU6050... ¡Mantenga el sensor quieto!");
    long accel_x_sum = 0, accel_y_sum = 0, accel_z_sum = 0;
    long gyro_x_sum = 0, gyro_y_sum = 0, gyro_z_sum = 0;
    for (int i = 0; i < samples; i++) {
        accel_x_sum += read_raw_data(ACCEL_XOUT_H);
        accel_y_sum += read_raw_data(ACCEL_YOUT_H);
        accel_z_sum += read_raw_data(ACCEL_ZOUT_H);
        gyro_x_sum += read_raw_data(GYRO_XOUT_H);
        gyro_y_sum += read_raw_data(GYRO_YOUT_H);
        gyro_z_sum += read_raw_data(GYRO_ZOUT_H);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    calibration.accel_offset_x = (float)accel_x_sum / samples;
    calibration.accel_offset_y = (float)accel_y_sum / samples;
    calibration.accel_offset_z = ((float)accel_z_sum / samples) - 16384.0;
    calibration.gyro_offset_x = (float)gyro_x_sum / samples;
    calibration.gyro_offset_y = (float)gyro_y_sum / samples;
    calibration.gyro_offset_z = (float)gyro_z_sum / samples;
    ESP_LOGI(TAG, "Calibracion completa!");
    ESP_LOGI(TAG, "Offsets Acelerometro: X=%.2f, Y=%.2f, Z=%.2f", calibration.accel_offset_x, calibration.accel_offset_y, calibration.accel_offset_z);
    ESP_LOGI(TAG, "Offsets Giroscopio: X=%.2f, Y=%.2f, Z=%.2f", calibration.gyro_offset_x, calibration.gyro_offset_y, calibration.gyro_offset_z);
}

void calculate_euler(float accel_x, float accel_y, float accel_z, float *roll, float *pitch) {
    *roll = atan2(accel_x, accel_z) * (180.0 / M_PI);
    float temp_pitch = atan2(accel_y, accel_z) * (180.0 / M_PI);
    *pitch = fmod(temp_pitch + 180.0, 360.0);
    if (*pitch < 0) *pitch += 360.0;
    if (*roll < 0) *roll += 360.0;
}

void get_sensor_data(float *accel_x_g, float *accel_y_g, float *accel_z_g,
                     float *gyro_x_dps, float *gyro_y_dps, float *gyro_z_dps,
                     float *angle_x_deg, float *angle_y_deg, float *angle_z_deg) {
    int16_t raw_accel_x = read_raw_data(ACCEL_XOUT_H);
    int16_t raw_accel_y = read_raw_data(ACCEL_YOUT_H);
    int16_t raw_accel_z = read_raw_data(ACCEL_ZOUT_H);
    int16_t raw_gyro_x = read_raw_data(GYRO_XOUT_H);
    int16_t raw_gyro_y = read_raw_data(GYRO_YOUT_H);
    int16_t raw_gyro_z = read_raw_data(GYRO_ZOUT_H);

    *accel_x_g = ((float)raw_accel_x - calibration.accel_offset_x) / 16384.0;
    *accel_y_g = ((float)raw_accel_y - calibration.accel_offset_y) / 16384.0;
    *accel_z_g = ((float)raw_accel_z - calibration.accel_offset_z) / 16384.0;
    *gyro_x_dps = ((float)raw_gyro_x - calibration.gyro_offset_x) / 131.0;
    *gyro_y_dps = ((float)raw_gyro_y - calibration.gyro_offset_y) / 131.0;
    *gyro_z_dps = ((float)raw_gyro_z - calibration.gyro_offset_z) / 131.0;

    calculate_euler(*accel_x_g, *accel_y_g, *accel_z_g, angle_x_deg, angle_y_deg);

    int64_t current_time_us = esp_timer_get_time();
    float dt_s = (float)(current_time_us - last_time_us) / 1000000.0;
    current_yaw += (*gyro_z_dps * dt_s);
    last_time_us = current_time_us;
    if (current_yaw > 360.0) current_yaw -= 360.0;
    else if (current_yaw < 0.0) current_yaw += 360.0;
    *angle_z_deg = current_yaw;
}

// ----------- CONFIGURACIÓN BLE CLIENTE -----------

#define GATTC_TAG "GATTC_DEMO"
#define REMOTE_SERVICE_UUID        0x00FF
#define REMOTE_NOTIFY_CHAR_UUID    0xFF01
#define PROFILE_NUM      1
#define PROFILE_A_APP_ID 0
#define INVALID_HANDLE   0

static char remote_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = "ESP_GATTS_DEMO";
static bool connect    = false;
static bool get_server = false;
static esp_gattc_char_elem_t *char_elem_result   = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;

static esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

static esp_bt_uuid_t remote_filter_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_NOTIFY_CHAR_UUID,},
};

static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = NULL, // Se asigna más adelante
        .gattc_if = ESP_GATT_IF_NONE,
    },
};

// ----------- ENVÍO DE DATOS MPU6050 POR BLE -----------

static void send_mpu6050_data_task(void *arg) {
    float accel_x_g, accel_y_g, accel_z_g;
    float gyro_x_dps, gyro_y_dps, gyro_z_dps;
    float angle_x_deg, angle_y_deg, angle_z_deg;

    while (1) {
        get_sensor_data(&accel_x_g, &accel_y_g, &accel_z_g,
                        &gyro_x_dps, &gyro_y_dps, &gyro_z_dps,
                        &angle_x_deg, &angle_y_deg, &angle_z_deg);

        printf("Eje X %.0f°, Eje Y %.0f°, Eje Z %.0f°\n",
               angle_x_deg, angle_y_deg, angle_z_deg);

        // Lógica de mensajes de mano
        bool is_x_centered = (angle_x_deg <= THRESHOLD_CENTERED_RANGE || angle_x_deg >= (360.0f - THRESHOLD_CENTERED_RANGE));
        bool is_y_centered = (angle_y_deg >= (180.0f - THRESHOLD_CENTERED_RANGE) && angle_y_deg <= (180.0f + THRESHOLD_CENTERED_RANGE));
        if (is_x_centered && is_y_centered) {
            if (!msg_hand_centered_triggered) {
                printf("¡MANO CENTRADA!\n");
                msg_hand_centered_triggered = true;
                msg_x_right_triggered = false;
                msg_x_left_triggered = false;
                msg_y_forward_triggered = false;
                msg_y_backward_triggered = false;
            }
        } else {
            msg_hand_centered_triggered = false;
        }
        bool is_x_reverse = (angle_x_deg >= (180.0f - THRESHOLD_REVERSE_RANGE) && angle_x_deg <= (180.0f + THRESHOLD_REVERSE_RANGE));
        bool is_y_reverse = (angle_y_deg <= THRESHOLD_REVERSE_RANGE || angle_y_deg >= (360.0f - THRESHOLD_REVERSE_RANGE));
        if (is_x_reverse && is_y_reverse) {
            if (!msg_hand_reverse_triggered) {
                printf("¡MANO AL REVES!\n");
                msg_hand_reverse_triggered = true;
                msg_x_right_triggered = false;
                msg_x_left_triggered = false;
                msg_y_forward_triggered = false;
                msg_y_backward_triggered = false;
            }
        } else {
            msg_hand_reverse_triggered = false;
        }
        if (!msg_hand_centered_triggered) {
            if (angle_x_deg >= THRESHOLD_X_RIGHT && angle_x_deg < 180.0f) {
                if (!msg_x_right_triggered) {
                    printf("¡Mano inclinada a la IZQUIERDA! (Eje X > %.0f°)\n", THRESHOLD_X_RIGHT);
                    msg_x_right_triggered = true;
                }
            } else {
                msg_x_right_triggered = false;
            }
            if (angle_x_deg <= THRESHOLD_X_LEFT && angle_x_deg > 180.0f) {
                if (!msg_x_left_triggered) {
                    printf("¡Mano inclinada a la DERECHA! (Eje X < %.0f°)\n", 360.0f - THRESHOLD_X_LEFT);
                    msg_x_left_triggered = true;
                }
            } else {
                msg_x_left_triggered = false;
            }
        }
        if (!msg_hand_centered_triggered) {
            if (angle_y_deg >= THRESHOLD_Y_FORWARD && angle_y_deg < 180.0f) {
                if (!msg_y_forward_triggered) {
                    printf("¡Mano inclinada hacia ADELANTE! (Eje Y > %.0f°)\n", THRESHOLD_Y_FORWARD);
                    msg_y_forward_triggered = true;
                }
            } else {
                msg_y_forward_triggered = false;
            }
            if (angle_y_deg <= THRESHOLD_Y_BACKWARD && angle_y_deg > 180.0f) {
                if (!msg_y_backward_triggered) {
                    printf("¡Mano inclinada hacia ATRÁS! (Eje Y < %.0f°)\n", 360.0f - THRESHOLD_Y_BACKWARD);
                    msg_y_backward_triggered = true;
                }
            } else {
                msg_y_backward_triggered = false;
            }
        }

        // Empaquetar los datos como string
        char data_to_send[64];
        snprintf(data_to_send, sizeof(data_to_send), "X:%.0f Y:%.0f Z:%.0f\n", angle_x_deg, angle_y_deg, angle_z_deg);

        // Enviar por BLE si está conectado y el handle es válido
        if (connect && gl_profile_tab[PROFILE_A_APP_ID].char_handle != 0) {
            esp_ble_gattc_write_char(
                gl_profile_tab[PROFILE_A_APP_ID].gattc_if,
                gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                strlen(data_to_send),
                (uint8_t*)data_to_send,
                ESP_GATT_WRITE_TYPE_RSP,
                ESP_GATT_AUTH_REQ_NONE
            );
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ----------- RESTO DEL CÓDIGO BLE (callbacks, main, etc.) -----------
// (Aquí va todo tu código BLE cliente, igual que ya tienes en tu proyecto.)
// Solo debes agregar la llamada a la tarea y la inicialización del MPU6050 en app_main:

// ... (callbacks BLE: gattc_profile_event_handler, esp_gap_cb, esp_gattc_cb, etc.) ...

// Prototipos de los callbacks requeridos por el stack BLE
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

// Callback para eventos GAP
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    // Aquí puedes manejar eventos GAP si lo necesitas, o dejarlo vacío si no
    // Ejemplo: ESP_LOGI(GATTC_TAG, "Evento GAP recibido: %d", event);
}

// Callback para eventos GATTC
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
    // Llama al handler del perfil (puedes expandir esto si tienes varios perfiles)
    if (gattc_if == ESP_GATT_IF_NONE || gattc_if == gl_profile_tab[PROFILE_A_APP_ID].gattc_if) {
        if (gl_profile_tab[PROFILE_A_APP_ID].gattc_cb) {
            gl_profile_tab[PROFILE_A_APP_ID].gattc_cb(event, gattc_if, param);
        }
    }
}

static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,
    },
};

void app_main(void)
{
    // Inicialización BLE (igual que tu código)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gap register failed, error code = %x", __func__, ret);
        return;
    }
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "%s gattc register failed, error code = %x", __func__, ret);
        return;
    }
    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTC_TAG, "%s gattc app register failed, error code = %x", __func__, ret);
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTC_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    // Inicializa el MPU6050
    ESP_LOGI(GATTC_TAG, "Inicializando I2C y MPU6050...");
    ESP_ERROR_CHECK(i2c_master_init());
    mpu6050_init();
    calibrate_mpu6050(500);

    // Crea la tarea para enviar los datos
    xTaskCreate(send_mpu6050_data_task, "send_mpu6050_data_task", 4096, NULL, 5, NULL);
}