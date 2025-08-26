// CODIGO BLE FUNCIONAL CON SENSADO DE LOS GESTOS DE LA MANO y SENSORES FLEX.
// El servidor se inicia, el cliente (celular) entra a este servidor del ESP32 y cuando activa las notificaciones este puede ver el sensado de los valores en hexadecimal, mientras que en el monitor serie se ve que movmiento es.

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

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Includes para la pantalla OLED y LVGL
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
#include "esp_lcd_sh1107.h"
#else
#include "esp_lcd_panel_vendor.h"
#endif

// --- Definiciones y Macros para BLE y Sensores ---
#define GATTS_TAG "GATTS_DEMO"
#define MPU_TAG "MPU6050"

#define GATTS_SERVICE_UUID_TEST_A   0x00FF
#define GATTS_CHAR_UUID_TEST_A      0xFF01
#define GATTS_DESCR_UUID_TEST_A     0x3333
#define GATTS_NUM_HANDLE_TEST_A     4

// Nuevo UUID para la característica del texto del micrófono
#define GATTS_CHAR_UUID_MIC_TEXT    0xFF02 
#define GATTS_DESCR_UUID_MIC_TEXT   0x4444
#define GATTS_NUM_HANDLE_MIC_TEXT   4

#define GATTS_SERVICE_UUID_TEST_B   0x00EE
#define GATTS_CHAR_UUID_TEST_B      0xEE01
#define GATTS_DESCR_UUID_TEST_B     0x2222
#define GATTS_NUM_HANDLE_TEST_B     4

#define ADC_UNIT        ADC_UNIT_1
#define R_FIXED         10000.0
#define VCC             3.3
#define N               8

// Canales ADC para cada flex
#define FLEX0_CHANNEL   ADC_CHANNEL_0
#define FLEX1_CHANNEL   ADC_CHANNEL_1
#define FLEX2_CHANNEL   ADC_CHANNEL_2
#define FLEX3_CHANNEL   ADC_CHANNEL_3
#define FLEX4_CHANNEL   ADC_CHANNEL_4

static char test_device_name[ESP_BLE_ADV_NAME_LEN_MAX] = "ESP_GATTS_DEMO";
#define TEST_MANUFACTURER_DATA_LEN  17
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 256 // Aumentado para texto
#define PREPARE_BUF_MAX_SIZE 1024

static uint8_t char1_str[] = {0x11,0x22,0x33};
static esp_gatt_char_prop_t a_property = 0;
static esp_gatt_char_prop_t b_property = 0;

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle[5];

static char ultimo_gesto[32] = "";
static char ultimo_estado_flex[5][20] = { "", "", "", "", "" };

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

// --- Configuración y variables del MPU6050 ---
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
#define THRESHOLD_Z_90 45.0f
#define THRESHOLD_Z_180 135.0f
#define THRESHOLD_Z_270 225.0f

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
static bool mpu_task_started = false;

// Handles BLE para notificaciones
static esp_gatt_if_t mpu_gatts_if = 0;
static uint16_t mpu_conn_id = 0;
static uint16_t mpu_char_handle = 0;
static uint16_t mic_char_handle = 0;

// --- Configuración y variables del OLED/LVGL ---
#define I2C_BUS_PORT 0
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA 6
#define EXAMPLE_PIN_NUM_SCL 7
#define EXAMPLE_PIN_NUM_RST -1
#define EXAMPLE_I2C_HW_ADDR 0x3C

#if CONFIG_EXAMPLE_LCD_CONTROLLER_SSD1306
#define EXAMPLE_LCD_H_RES 128
#define EXAMPLE_LCD_V_RES CONFIG_EXAMPLE_SSD1306_HEIGHT
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
#define EXAMPLE_LCD_H_RES 64
#define EXAMPLE_LCD_V_RES 128
#endif

#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

lv_obj_t *global_lv_label = NULL;

// --- Funciones del MPU6050 ---
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

static void leer_estado_flex(int flex_idx, int channel, adc_cali_handle_t cali, char* estado_out, size_t len) {
    int sum_adc = 0;
    for (int i = 0; i < N; i++) {
        int adc_raw = 0;
        adc_oneshot_read(adc_handle, channel, &adc_raw);
        sum_adc += adc_raw;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    int adc_avg = sum_adc / N;
    int voltage_mv = 0;
    adc_cali_raw_to_voltage(cali, adc_avg, &voltage_mv);
    float voltage = voltage_mv / 1000.0f;
    float R_flex = R_FIXED * (VCC / voltage - 1.0);

    // Clasificación según tu código original
    if (flex_idx == 4) { // MEÑIQUE N° 9
        if (R_flex < 52000 && R_flex > 45000 ) strncpy(estado_out, "muy flexionado", len);
        else if (R_flex > 20000 && R_flex < 45000) strncpy(estado_out, "medio", len);
        else if (R_flex > 52000) strncpy(estado_out, "maximo", len);
        else if (R_flex > 13000 && R_flex < 20000) strncpy(estado_out, "recto", len);
        else strncpy(estado_out, "desconocido", len);
    } else if (flex_idx == 0) { // INDICE N°10
        if (R_flex < 60000 && R_flex > 45000 ) strncpy(estado_out, "muy flexionado", len);
        else if (R_flex > 21000 && R_flex < 45000) strncpy(estado_out, "medio", len);
        else if (R_flex > 60000) strncpy(estado_out, "maximo", len);
        else if (R_flex > 13000 && R_flex < 21000) strncpy(estado_out, "recto", len);
        else strncpy(estado_out, "desconocido", len);
    } else if (flex_idx == 1) { // MAYOR N°12
        if (R_flex < 42000 && R_flex > 32000 ) strncpy(estado_out, "muy flexionado", len);
        else if (R_flex > 15000 && R_flex < 32000) strncpy(estado_out, "medio", len);
        else if (R_flex > 40000) strncpy(estado_out, "maximo", len);
        else if (R_flex > 11000 && R_flex < 15000) strncpy(estado_out, "recto", len);
        else strncpy(estado_out, "desconocido", len); 
    } else if  (flex_idx == 2) { // ANULAR N° 7 
        if (R_flex < 14500 && R_flex > 10000 ) strncpy(estado_out, "muy flexionado", len);
        else if (R_flex > 7000 && R_flex < 10000) strncpy(estado_out, "medio", len);
        else if (R_flex > 5000 && R_flex < 7000) strncpy(estado_out, "recto", len);
        else strncpy(estado_out, "desconocido", len);
    } else if (flex_idx == 3) { // GORDO N° 11
        if (R_flex < 67000 && R_flex > 58000 ) strncpy(estado_out, "muy flexionado", len);
        else if (R_flex > 20000 && R_flex < 58000) strncpy(estado_out, "medio", len);
        else if (R_flex > 67000) strncpy(estado_out, "maximo", len);
        else if (R_flex > 13000 && R_flex < 20000) strncpy(estado_out, "recto", len);
        else strncpy(estado_out, "desconocido", len);
    }
}

// --- Tarea de sensado y notificación BLE ---
static void mpu6050_task(void *arg) {
    float ax, ay, az, gx, gy, gz, ang_x, ang_y, ang_z;
    last_time_us = esp_timer_get_time();
    while (1) {
        char notify_msg[32] = {0};
        bool send = false;

        get_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &ang_x, &ang_y, &ang_z);

        // --- LÓGICA DE GESTOS ---
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

        char estado_flex[5][20];
        leer_estado_flex(0, FLEX0_CHANNEL, cali_handle[0], estado_flex[0], sizeof(estado_flex[0]));
        leer_estado_flex(1, FLEX1_CHANNEL, cali_handle[1], estado_flex[1], sizeof(estado_flex[1]));
        leer_estado_flex(2, FLEX2_CHANNEL, cali_handle[2], estado_flex[2], sizeof(estado_flex[2]));
        leer_estado_flex(3, FLEX3_CHANNEL, cali_handle[3], estado_flex[3], sizeof(estado_flex[3]));
        leer_estado_flex(4, FLEX4_CHANNEL, cali_handle[4], estado_flex[4], sizeof(estado_flex[4]));

        bool flex_cambio = false;
        for (int i = 0; i < 5; i++) {
            if (strcmp(estado_flex[i], ultimo_estado_flex[i]) != 0) {
                flex_cambio = true;
                break;
            }
        }
        bool gesto_cambio = (strcmp(notify_msg, ultimo_gesto) != 0);

        if ((send || flex_cambio) && mpu_gatts_if != 0 && mpu_char_handle != 0) {
            char mensaje_ble[256];
            snprintf(mensaje_ble, sizeof(mensaje_ble),
                "%s | Indice:%s Mayor:%s Anular:%s Gordo:%s Meñique:%s",
                notify_msg, estado_flex[0], estado_flex[1], estado_flex[2], estado_flex[3], estado_flex[4]);

            ESP_LOGI(MPU_TAG, "Gesto+Flex: %s", mensaje_ble);

            esp_err_t err = esp_ble_gatts_send_indicate(
                mpu_gatts_if,
                mpu_conn_id,
                mpu_char_handle,
                strlen(mensaje_ble),
                (uint8_t*)mensaje_ble,
                false
            );
            ESP_LOGI(MPU_TAG, "Notificado BLE: %s, err=%d", mensaje_ble, err);

            strcpy(ultimo_gesto, notify_msg);
            for (int i = 0; i < 5; i++) {
                strcpy(ultimo_estado_flex[i], estado_flex[i]);
            }
            send = false;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// --- Handlers de eventos BLE ---
void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

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

        // Agrega la nueva característica para el texto del micrófono
        gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_MIC_TEXT;
        esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,
                               ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
                               NULL, NULL);
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        if (param->add_char.char_uuid.uuid.uuid16 == GATTS_CHAR_UUID_TEST_A) {
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
            mpu_char_handle = param->add_char.attr_handle; // Guarda el handle para notificaciones
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        } else if (param->add_char.char_uuid.uuid.uuid16 == GATTS_CHAR_UUID_MIC_TEXT) {
            mic_char_handle = param->add_char.attr_handle; // Guarda el handle para la escritura
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        }
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
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);

        if (param->write.handle == mic_char_handle) {
            // Se recibió texto del micrófono, actualiza la pantalla OLED
            if (global_lv_label != NULL) {
                if (lvgl_port_lock(0)) {
                    // Copia el texto recibido y lo null-termina
                    char received_text[param->write.len + 1];
                    memcpy(received_text, param->write.value, param->write.len);
                    received_text[param->write.len] = '\0';
                    lv_label_set_text(global_lv_label, received_text);
                    lvgl_port_unlock();
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

static void gatts_profile_b_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {}

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

// --- MAIN ---
void app_main(void)
{
    esp_err_t ret;

    // --- Inicialización del Sistema ---
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // --- Configuración de BLE ---
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(GATTS_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret)); return; }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) { ESP_LOGE(GATTS_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret)); return; }
    ret = esp_bluedroid_init();
    if (ret) { ESP_LOGE(GATTS_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret)); return; }
    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(GATTS_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret)); return; }

    gl_profile_tab[PROFILE_A_APP_ID].gatts_cb = gatts_profile_a_event_handler;
    gl_profile_tab[PROFILE_B_APP_ID].gatts_cb = gatts_profile_b_event_handler;
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){ ESP_LOGE(GATTS_TAG, "gatts register error, error code = %x", ret); return; }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){ ESP_LOGE(GATTS_TAG, "gap register error, error code = %x", ret); return; }
    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret){ ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret); return; }
    ret = esp_ble_gatts_app_register(PROFILE_B_APP_ID);
    if (ret){ ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret); return; }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){ ESP_LOGE(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret); }

    // --- Inicialización Sensores (MPU6050 y Flex) ---
    ESP_LOGI(MPU_TAG, "Inicializando I2C y MPU6050...");
    ESP_ERROR_CHECK(i2c_master_init());
    mpu6050_init();
    calibrate_mpu6050(500);
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    adc_oneshot_new_unit(&init_config, &adc_handle);
    adc_oneshot_chan_cfg_t config = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12 };
    adc_oneshot_config_channel(adc_handle, FLEX0_CHANNEL, &config);
    adc_oneshot_config_channel(adc_handle, FLEX1_CHANNEL, &config);
    adc_oneshot_config_channel(adc_handle, FLEX2_CHANNEL, &config);
    adc_oneshot_config_channel(adc_handle, FLEX3_CHANNEL, &config);
    adc_oneshot_config_channel(adc_handle, FLEX4_CHANNEL, &config);
    adc_cali_curve_fitting_config_t cali_cfg;
    for (int i = 0; i < 5; i++) {
        cali_cfg.unit_id = ADC_UNIT;
        cali_cfg.chan = ADC_CHANNEL_0 + i;
        cali_cfg.atten = ADC_ATTEN_DB_12;
        cali_cfg.bitwidth = ADC_BITWIDTH_12;
        adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle[i]);
    }

    // --- Inicialización Pantalla OLED y LVGL ---
    ESP_LOGI(TAG, "Initialize I2C bus for OLED");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = EXAMPLE_I2C_HW_ADDR,
        .scl_speed_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_CMD_BITS,
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SSD1306
        .dc_bit_offset = 6,
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
        .dc_bit_offset = 0,
        .flags = { .disable_control_phase = 1, }
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = { .bits_per_pixel = 1, .reset_gpio_num = EXAMPLE_PIN_NUM_RST, };
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SSD1306
    esp_lcd_panel_ssd1306_config_t ssd1306_config = { .height = EXAMPLE_LCD_V_RES, };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh1107(io_handle, &panel_config, &panel_handle));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
#if CONFIG_EXAMPLE_LCD_CONTROLLER_SH1107
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif
    ESP_LOGI(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES,
        .double_buffer = true,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = true,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false, }
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_disp_get_scr_act(disp);
        global_lv_label = lv_label_create(scr);
        lv_label_set_long_mode(global_lv_label, LV_LABEL_LONG_SCROLL_CIRCULAR); // Cambiado a scroll circular para un mejor efecto visual.
        lv_label_set_text(global_lv_label, "Esperando texto por BLE...");
        lv_obj_set_width(global_lv_label, disp->driver->hor_res);
        lv_obj_align(global_lv_label, LV_ALIGN_CENTER, 0, 0);
        lvgl_port_unlock();
    }
    
    // --- Crea la tarea de sensado y notificación ---
    if (!mpu_task_started) {
        xTaskCreate(mpu6050_task, "mpu6050_task", 4096, NULL, 5, NULL);
        mpu_task_started = true;
    }
}