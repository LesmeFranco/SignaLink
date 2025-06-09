#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// Configuración
#define ADC_CHANNEL_0   ADC_CHANNEL_0 // GPIO0
#define ADC_CHANNEL_1   ADC_CHANNEL_1 // GPIO1
#define ADC_UNIT        ADC_UNIT_1
#define R_FIXED         16000.0 // Ohmios (resistencia fija)
#define VCC             3.3
#define N 8 // Número de muestras para el promedio

void app_main(void)
{
    // Configuración ADC oneshot
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &config);
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_1, &config);

    // Calibración (nuevo esquema) para ambos canales
    adc_cali_handle_t cali_handle_0 = NULL;
    adc_cali_curve_fitting_config_t cali_config_0 = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config_0, &cali_handle_0);

    adc_cali_handle_t cali_handle_1 = NULL;
    adc_cali_curve_fitting_config_t cali_config_1 = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config_1, &cali_handle_1);

    while (1) {
        // Promedio para Sensor 0
        int sum_adc_0 = 0;
        for (int i = 0; i < N; i++) {
            int adc_raw_0 = 0;
            adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &adc_raw_0);
            sum_adc_0 += adc_raw_0;
            vTaskDelay(pdMS_TO_TICKS(5)); // Pequeña pausa entre muestras
        }
        int adc_raw_0_avg = sum_adc_0 / N;
        int voltage_mv_0 = 0;
        adc_cali_raw_to_voltage(cali_handle_0, adc_raw_0_avg, &voltage_mv_0);
        float voltage_0 = voltage_mv_0 / 1000.0f;
        float R_flex_0 = R_FIXED * (VCC / voltage_0 - 1.0);

        // Promedio para Sensor 1
        int sum_adc_1 = 0;
        for (int i = 0; i < N; i++) {
            int adc_raw_1 = 0;
            adc_oneshot_read(adc_handle, ADC_CHANNEL_1, &adc_raw_1);
            sum_adc_1 += adc_raw_1;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        int adc_raw_1_avg = sum_adc_1 / N;
        int voltage_mv_1 = 0;
        adc_cali_raw_to_voltage(cali_handle_1, adc_raw_1_avg, &voltage_mv_1);
        float voltage_1 = voltage_mv_1 / 1000.0f;
        float R_flex_1 = R_FIXED * (VCC / voltage_1 - 1.0);

        // Estados individuales
        const char* estado_0 = "";
        if (R_flex_0 < 18000) {
            estado_0 = "muy flexionado";
        } else if (R_flex_0 < 21000) {
            estado_0 = "medio";
        } else {
            estado_0 = "recto";
        }

        const char* estado_1 = "";
        if (R_flex_1 < 18000) {
            estado_1 = "muy flexionado";
        } else if (R_flex_1 < 21000) {
            estado_1 = "medio";
        } else {
            estado_1 = "recto";
        }

        // Frase combinada
        const char* frase = "";
        if (strcmp(estado_0, "recto") == 0 && strcmp(estado_1, "recto") == 0) {
            frase = "Ambos rectos";
        } else if (strcmp(estado_0, "muy flexionado") == 0 && strcmp(estado_1, "muy flexionado") == 0) {
            frase = "Ambos muy flexionados";
        } else if (strcmp(estado_0, "medio") == 0 && strcmp(estado_1, "medio") == 0) {
            frase = "Ambos medio";
        } else if (strcmp(estado_0, "recto") == 0 && strcmp(estado_1, "muy flexionado") == 0) {
            frase = "Sensor 0 recto, Sensor 1 muy flexionado";
        } else if (strcmp(estado_0, "muy flexionado") == 0 && strcmp(estado_1, "recto") == 0) {
            frase = "Sensor 0 muy flexionado, Sensor 1 recto";
        } else if (strcmp(estado_0, "medio") == 0 && strcmp(estado_1, "recto") == 0) {
            frase = "Sensor 0 medio, Sensor 1 recto";
        } else if (strcmp(estado_0, "recto") == 0 && strcmp(estado_1, "medio") == 0) {
            frase = "Sensor 0 recto, Sensor 1 medio";
        } else if (strcmp(estado_0, "medio") == 0 && strcmp(estado_1, "muy flexionado") == 0) {
            frase = "Sensor 0 medio, Sensor 1 muy flexionado";
        } else if (strcmp(estado_0, "muy flexionado") == 0 && strcmp(estado_1, "medio") == 0) {
            frase = "Sensor 0 muy flexionado, Sensor 1 medio";
        } else {
            frase = "Combinación diferente";
        }

        printf("S0: ADC %d | Vout %.3f V | R %.1f Ω | %s || S1: ADC %d | Vout %.3f V | R %.1f Ω | %s || Frase: %s\n",
            adc_raw_0, voltage_0, R_flex_0, estado_0,
            adc_raw_1, voltage_1, R_flex_1, estado_1,
            frase);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
