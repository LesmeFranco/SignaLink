#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/i2c.h"
#include "esp_timer.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "sdkconfig.h"

#define GATTS_TAG "GATTS_DEMO"

#define GATTS_SERVICE_UUID_TEST_A   0x00FF
#define GATTS_CHAR_UUID_TEST_A      0xFF01
#define GATTS_DESCR_UUID_TEST_A     0x3333
#define GATTS_NUM_HANDLE_TEST_A     4

#define GATTS_SERVICE_UUID_TEST_B   0x00EE
#define GATTS_CHAR_UUID_TEST_B      0xEE01
#define GATTS_DESCR_UUID_TEST_B     0x2222
#define GATTS_NUM_HANDLE_TEST_B     4

static char test_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = "ESP_GATTS_DEMO";

#define TEST_MANUFACTURER_DATA_LEN  17
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 0x40
#define PREPARE_BUF_MAX_SIZE 1024

static uint8_t char1_str[] = {0x11,0x22,0x33};
static esp_gatt_char_prop_t a_property = 0;
static esp_gatt_char_prop_t b_property = 0;

static esp_attr_value_t gatts_demo_char1_val =
{
    .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,
    .attr_len     = sizeof(char1_str),
    .attr_value   = char1_str,
};

static uint8_t adv_config_done = 0;
#define adv_config_flag      (1 << 0)
#define scan_rsp_config_flag (1 << 1)

static uint8_t adv_service_uuid128[32] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xEE, 0x00, 0x00, 0x00,
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define PROFILE_NUM 2
#define PROFILE_A_APP_ID 0
#define PROFILE_B_APP_ID 1

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gatts_cb = NULL,
        .gatts_if = ESP_GATT_IF_NONE,
    },
    [PROFILE_B_APP_ID] = {
        .gatts_cb = NULL,
        .gatts_if = ESP_GATT_IF_NONE,
    },
};

typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;

static prepare_type_env_t a_prepare_write_env;
static prepare_type_env_t b_prepare_write_env;

// ==== MPU6050 CONFIGURACIÓN Y FUNCIONES ====
#define I2C_MASTER_SDA_IO    6
#define I2C_MASTER_SCL_IO    7
#define I2C_MASTER_NUM       0
#define I2C_MASTER_FREQ_HZ   400000

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

#define THRESHOLD_CENTERED_RANGE 30.0f
#define THRESHOLD_REVERSE_RANGE 30.0f
#define THRESHOLD_X_RIGHT 30.0f
#define THRESHOLD_X_LEFT 330.0f
#define THRESHOLD_Y_FORWARD 45.0f
#define THRESHOLD_Y_BACKWARD 315.0f
#define THRESHOLD_Z_90 90.0f
#define THRESHOLD_Z_180 180.0f
#define THRESHOLD_Z_270 270.0f

static const char *MPU_TAG = "MPU6050";

typedef struct {
    float accel_offset_x, accel_offset_y, accel_offset_z;
    float gyro_offset_x, gyro_offset_y, gyro_offset_z;
} mpu6050_calibration_t;

static mpu6050_calibration_t calibration = {0};
static int64_t last_time_us = 0;
static float current_yaw = 0.0;

static bool msg_hand_centered_triggered = false;
static bool msg_hand_reverse_triggered = false;
static bool msg_x_right_triggered = false;
static bool msg_x_left_triggered = false;
static bool msg_y_forward_triggered = false;
static bool msg_y_backward_triggered = false;
static bool msg_z_90_triggered = false;
static bool msg_z_180_triggered = false;
static bool msg_z_270_triggered = false;

// Handles BLE para notificaciones
static esp_gatt_if_t mpu_gatts_if = 0;
static uint16_t mpu_conn_id = 0;
static uint16_t mpu_char_handle = 0;

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
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, write_buf, 2, 1000 / portTICK_PERIOD_MS);
}

static esp_err_t mpu6050_register_read_bytes(uint8_t reg_addr, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_ADDR, &reg_addr, 1, data, len, 1000 / portTICK_PERIOD_MS);
}

static void mpu6050_init(void) {
    ESP_ERROR_CHECK(mpu6050_register_write_byte(PWR_MGMT_1, 0x00));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(mpu6050_register_write_byte(GYRO_CONFIG, 0x00));
    ESP_ERROR_CHECK(mpu6050_register_write_byte(ACCEL_CONFIG, 0x00));
    ESP_LOGI(MPU_TAG, "MPU6050 inicializado");
}

static int16_t read_raw_data(uint8_t register_addr) {
    uint8_t data[2];
    if (mpu6050_register_read_bytes(register_addr, data, 2) != ESP_OK) return 0;
    return (data[0] << 8) | data[1];
}

static void calibrate_mpu6050(int samples) {
    long ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
    for (int i = 0; i < samples; i++) {
        ax += read_raw_data(ACCEL_XOUT_H);
        ay += read_raw_data(ACCEL_YOUT_H);
        az += read_raw_data(ACCEL_ZOUT_H);
        gx += read_raw_data(GYRO_XOUT_H);
        gy += read_raw_data(GYRO_YOUT_H);
        gz += read_raw_data(GYRO_ZOUT_H);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    calibration.accel_offset_x = (float)ax / samples;
    calibration.accel_offset_y = (float)ay / samples;
    calibration.accel_offset_z = ((float)az / samples) - 16384.0;
    calibration.gyro_offset_x = (float)gx / samples;
    calibration.gyro_offset_y = (float)gy / samples;
    calibration.gyro_offset_z = (float)gz / samples;
    ESP_LOGI(MPU_TAG, "Calibración completa");
}

static void calculate_euler(float ax, float ay, float az, float *roll, float *pitch) {
    *roll = atan2(ax, az) * (180.0 / M_PI);
    float temp_pitch = atan2(ay, az) * (180.0 / M_PI);
    *pitch = fmod(temp_pitch + 180.0, 360.0);
    if (*pitch < 0) *pitch += 360.0;
    if (*roll < 0) *roll += 360.0;
}

static void get_sensor_data(float *accel_x_g, float *accel_y_g, float *accel_z_g,
                            float *gyro_x_dps, float *gyro_y_dps, float *gyro_z_dps,
                            float *angle_x_deg, float *angle_y_deg, float *angle_z_deg) {
    int16_t raw_ax = read_raw_data(ACCEL_XOUT_H);
    int16_t raw_ay = read_raw_data(ACCEL_YOUT_H);
    int16_t raw_az = read_raw_data(ACCEL_ZOUT_H);
    int16_t raw_gx = read_raw_data(GYRO_XOUT_H);
    int16_t raw_gy = read_raw_data(GYRO_YOUT_H);
    int16_t raw_gz = read_raw_data(GYRO_ZOUT_H);

    *accel_x_g = ((float)raw_ax - calibration.accel_offset_x) / 16384.0;
    *accel_y_g = ((float)raw_ay - calibration.accel_offset_y) / 16384.0;
    *accel_z_g = ((float)raw_az - calibration.accel_offset_z) / 16384.0;
    *gyro_x_dps = ((float)raw_gx - calibration.gyro_offset_x) / 131.0;
    *gyro_y_dps = ((float)raw_gy - calibration.gyro_offset_y) / 131.0;
    *gyro_z_dps = ((float)raw_gz - calibration.gyro_offset_z) / 131.0;

    calculate_euler(*accel_x_g, *accel_y_g, *accel_z_g, angle_x_deg, angle_y_deg);

    int64_t now = esp_timer_get_time();
    float dt = (float)(now - last_time_us) / 1000000.0;
    current_yaw += (*gyro_z_dps * dt);
    last_time_us = now;
    if (current_yaw > 360.0) current_yaw -= 360.0;
    else if (current_yaw < 0.0) current_yaw += 360.0;
    *angle_z_deg = current_yaw;
}

// --- Tarea FreeRTOS para sensar y notificar gestos ---
static void mpu6050_task(void *arg) {
    float ax, ay, az, gx, gy, gz, ang_x, ang_y, ang_z;
    last_time_us = esp_timer_get_time();
    while (1) {
        get_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &ang_x, &ang_y, &ang_z);

        ESP_LOGI(MPU_TAG, "Datos: ax=%.2f ay=%.2f az=%.2f ang_x=%.2f ang_y=%.2f ang_z=%.2f", ax, ay, az, ang_x, ang_y, ang_z);


        char notify_msg[32] = {0};
        bool send = false;

        bool is_x_centered = (ang_x <= THRESHOLD_CENTERED_RANGE || ang_x >= (360.0f - THRESHOLD_CENTERED_RANGE));
        bool is_y_centered = (ang_y >= (180.0f - THRESHOLD_CENTERED_RANGE) && ang_y <= (180.0f + THRESHOLD_CENTERED_RANGE));
        if (is_x_centered && is_y_centered) {
            if (!msg_hand_centered_triggered) {
                strcpy(notify_msg, "MANO_CENTRADA");
                send = true;
                msg_hand_centered_triggered = true;
                msg_hand_reverse_triggered = false;
                msg_x_right_triggered = false;
                msg_x_left_triggered = false;
                msg_y_forward_triggered = false;
                msg_y_backward_triggered = false;
            }
        } else {
            msg_hand_centered_triggered = false;
        }

        bool is_x_reverse = (ang_x >= (180.0f - THRESHOLD_REVERSE_RANGE) && ang_x <= (180.0f + THRESHOLD_REVERSE_RANGE));
        bool is_y_reverse = (ang_y <= THRESHOLD_REVERSE_RANGE || ang_y >= (360.0f - THRESHOLD_REVERSE_RANGE));
        if (is_x_reverse && is_y_reverse) {
            if (!msg_hand_reverse_triggered) {
                strcpy(notify_msg, "MANO_REVES");
                send = true;
                msg_hand_reverse_triggered = true;
                msg_hand_centered_triggered = false;
                msg_x_right_triggered = false;
                msg_x_left_triggered = false;
                msg_y_forward_triggered = false;
                msg_y_backward_triggered = false;
            }
        } else {
            msg_hand_reverse_triggered = false;
        }

        if (!msg_hand_centered_triggered && !msg_hand_reverse_triggered) {
            if (ang_x >= THRESHOLD_X_RIGHT && ang_x < 180.0f) {
                if (!msg_x_right_triggered) {
                    strcpy(notify_msg, "MANO_IZQUIERDA");
                    send = true;
                    msg_x_right_triggered = true;
                }
            } else {
                msg_x_right_triggered = false;
            }
            if (ang_x <= THRESHOLD_X_LEFT && ang_x > 180.0f) {
                if (!msg_x_left_triggered) {
                    strcpy(notify_msg, "MANO_DERECHA");
                    send = true;
                    msg_x_left_triggered = true;
                }
            } else {
                msg_x_left_triggered = false;
            }
            if (ang_y >= THRESHOLD_Y_FORWARD && ang_y < 180.0f) {
                if (!msg_y_forward_triggered) {
                    strcpy(notify_msg, "MANO_ADELANTE");
                    send = true;
                    msg_y_forward_triggered = true;
                }
            } else {
                msg_y_forward_triggered = false;
            }
            if (ang_y <= THRESHOLD_Y_BACKWARD && ang_y > 180.0f) {
                if (!msg_y_backward_triggered) {
                    strcpy(notify_msg, "MANO_ATRAS");
                    send = true;
                    msg_y_backward_triggered = true;
                }
            } else {
                msg_y_backward_triggered = false;
            }
        }

        // Lógica para el Eje Z (Yaw de la mano) - Rotación de la muñeca
        if (ang_z >= THRESHOLD_Z_90 && ang_z < THRESHOLD_Z_180) {
            if (!msg_z_90_triggered) {
                strcpy(notify_msg, "Z_90");
                send = true;
                msg_z_90_triggered = true;
            }
        } else {
            msg_z_90_triggered = false;
        }
        if (ang_z >= THRESHOLD_Z_180 && ang_z < THRESHOLD_Z_270) {
            if (!msg_z_180_triggered) {
                strcpy(notify_msg, "Z_180");
                send = true;
                msg_z_180_triggered = true;
            }
        } else {
            msg_z_180_triggered = false;
        }
        if (ang_z >= THRESHOLD_Z_270) {
            if (!msg_z_270_triggered) {
                strcpy(notify_msg, "Z_270");
                send = true;
                msg_z_270_triggered = true;
            }
        } else if (ang_z < THRESHOLD_Z_270 && msg_z_270_triggered) {
            msg_z_270_triggered = false;
        }

        // Enviar notificación BLE si corresponde
        if (send && mpu_gatts_if && mpu_conn_id && mpu_char_handle) {
            esp_ble_gatts_send_indicate(
                mpu_gatts_if,
                mpu_conn_id,
                mpu_char_handle,
                strlen(notify_msg),
                (uint8_t*)notify_msg,
                false
            );
            ESP_LOGI(MPU_TAG, "Notificado BLE: %s", notify_msg);
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }
}

// --- BLE HANDLERS Y CALLBACKS ---
void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~adv_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        adv_config_done &= (~scan_rsp_config_flag);
        if (adv_config_done == 0){
            esp_ble_gap_start_advertising(&adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising start failed, status %d", param->adv_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTS_TAG, "Advertising start successfully");
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TAG, "Advertising stop failed, status %d", param->adv_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTS_TAG, "Advertising stop successfully");
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
         ESP_LOGI(GATTS_TAG, "Connection params update, status %d, conn_int %d, latency %d, timeout %d",
                  param->update_conn_params.status,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_A;
        esp_ble_gap_set_device_name(test_device_name);
        esp_ble_gap_config_adv_data(&adv_data);
        adv_config_done |= adv_config_flag;
        esp_ble_gap_config_adv_data(&scan_rsp_data);
        adv_config_done |= scan_rsp_config_flag;
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_A);
        break;
    case ESP_GATTS_CREATE_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_A;
        esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
        a_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               a_property,
                               &gatts_demo_char1_val, NULL);
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        mpu_char_handle = param->add_char.attr_handle; // Para notificaciones MPU6050
        break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        break;
    case ESP_GATTS_CONNECT_EVT:
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        mpu_gatts_if = gatts_if;
        mpu_conn_id = param->connect.conn_id;
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_WRITE_EVT, handle = %d", param->write.handle);
        esp_gatt_status_t status = ESP_GATT_OK;
        // Responde siempre, sea descriptor o característica
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);

        if (param->write.handle == gl_profile_tab[PROFILE_A_APP_ID].descr_handle && param->write.len == 2) {
            uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
            if (descr_value == 0x0001) {
                ESP_LOGI(GATTS_TAG, "Notificaciones habilitadas");
            } else if (descr_value == 0x0002) {
                ESP_LOGI(GATTS_TAG, "Indications habilitadas");
            } else if (descr_value == 0x0000) {
                ESP_LOGI(GATTS_TAG, "Notificaciones/Indications deshabilitadas");
            }
        }
        break;
    }
    default:
        break;
    }
}

static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    // Puedes dejarlo vacío o igual que el ejemplo de Espressif si no usas el perfil B
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(GATTS_TAG, "Reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }
    for (int idx = 0; idx < PROFILE_NUM; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
            if (gl_profile_tab[idx].gatts_cb) {
                gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

// ==== MAIN ====
void app_main(void)
{
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTS_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    gl_profile_tab[PROFILE_A_APP_ID].gatts_cb = gatts_profile_a_event_handler;
    gl_profile_tab[PROFILE_B_APP_ID].gatts_cb = gatts_profile_b_event_handler;

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gap register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
        return;
    }
    ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
    if (ret){
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
        return;
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }

    // === INICIALIZACIÓN MPU6050 ===
    ESP_LOGI(MPU_TAG, "Inicializando I2C y MPU6050...");
    ESP_ERROR_CHECK(i2c_master_init());
    mpu6050_init();
    calibrate_mpu6050(500);

    // === CREA LA TAREA DE SENSADO Y NOTIFICACIÓN ===
    xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL);
}