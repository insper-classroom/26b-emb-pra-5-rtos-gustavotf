/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <stdio.h>
#include <task.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pins.h"
#include "ssd1306.h"

// === Definições para SSD1306 ===

QueueHandle_t xQueueTime;
SemaphoreHandle_t xSemaphoreTrigger;
QueueHandle_t xQueueDistance;

// === Função de callback (IRQ) ===
void pin_callback(uint gpio, uint32_t events) {
    if (gpio == ECHO_PIN) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uint64_t time = to_us_since_boot(get_absolute_time());

        xQueueSendFromISR(xQueueTime, &time, &xHigherPriorityTaskWoken);
    }
}

void oled_display_init(ssd1306_t *disp) {
    i2c_init(i2c1, 400000);
    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    disp->external_vcc = false;
    ssd1306_init(disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(disp);
    ssd1306_show(disp);

    gpio_init(SSD1306_PIN_LITE);
    gpio_set_dir(SSD1306_PIN_LITE, GPIO_OUT);
    gpio_put(SSD1306_PIN_LITE, 0);
}

void led_rgb_init(void) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);
    gpio_put(LED_PIN_R, 1);

    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);
    gpio_put(LED_PIN_G, 1);

    gpio_init(LED_PIN_B);
    gpio_set_dir(LED_PIN_B, GPIO_OUT);
    gpio_put(LED_PIN_B, 1);
}

void trigger_task(void *p) {
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_OUT);
    gpio_put(TRIGGER_PIN, 0);

    while (1) {
        gpio_put(TRIGGER_PIN, 1);
        sleep_us(10);
        gpio_put(TRIGGER_PIN, 0);

        xSemaphoreGive(xSemaphoreTrigger);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void echo_task(void *p) {
    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(ECHO_PIN,
                                       GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
                                       true, &pin_callback);

    uint64_t start;
    uint64_t end;
    float distance;

    while (1) {
        if (xQueueReceive(xQueueTime, &start, portMAX_DELAY) == pdTRUE) {
            if (xQueueReceive(xQueueTime, &end, pdMS_TO_TICKS(50)) == pdTRUE) {
                distance = (end - start) / 58.0;
                xQueueSend(xQueueDistance, &distance, 0);
            }
        }
    }
}

void oled_task(void *p) {
    ssd1306_t disp;
    oled_display_init(&disp);
    led_rgb_init();

    float distance;
    char str[20];

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, portMAX_DELAY) == pdTRUE) {
            ssd1306_clear(&disp);

            if (xQueueReceive(xQueueDistance, &distance, pdMS_TO_TICKS(100)) == pdTRUE) {
                sprintf(str, "Dist: %d cm", (int) distance);
                ssd1306_draw_string(&disp, 0, 0, 1, str);

                int bar = (int) (distance * 128 / 400);
                if (bar > 128) {
                    bar = 128;
                }
                ssd1306_draw_square(&disp, 0, 30, bar, 10);

                if (distance <= 100) {
                    gpio_put(LED_PIN_R, 1);
                    gpio_put(LED_PIN_G, 0);
                    gpio_put(LED_PIN_B, 1);
                } else {
                    gpio_put(LED_PIN_R, 0);
                    gpio_put(LED_PIN_G, 0);
                    gpio_put(LED_PIN_B, 1);
                }
            } else {
                ssd1306_draw_string(&disp, 0, 0, 1, "Sensor falhou");

                gpio_put(LED_PIN_R, 0);
                gpio_put(LED_PIN_G, 1);
                gpio_put(LED_PIN_B, 1);
            }

            ssd1306_show(&disp);
        }
    }
}

int main() {
    stdio_init_all();

    xQueueTime = xQueueCreate(32, sizeof(uint64_t));
    xSemaphoreTrigger = xSemaphoreCreateBinary();
    xQueueDistance = xQueueCreate(32, sizeof(float));
    if (xQueueTime == NULL) {
        while (1) {
            // falha ao criar fila
        }
    }

    xTaskCreate(trigger_task, "Trigger", 256, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 256, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 4095, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}