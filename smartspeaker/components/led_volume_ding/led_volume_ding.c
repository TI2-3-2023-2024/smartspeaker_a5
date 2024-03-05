#include <stdio.h>
#include "led_volume_ding.h"
#include <driver/i2c.h>

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

void config_master(void)
{
    i2c_config_t conf_master = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 18,         // select GPIO specific to your project
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = 23,         // select GPIO specific to your project
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 10000,  // select frequency specific to your project
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf_master));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 1024, 0));
}

void send_command(uint8_t *message, size_t len)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(handle));
    ESP_ERROR_CHECK(i2c_master_write_byte(handle, (0x69 << 1) | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write(handle, message, len, true));
    ESP_ERROR_CHECK(i2c_master_stop(handle));
    ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, handle, portMAX_DELAY));
    i2c_cmd_link_delete(handle);
}

void turn_on_white_delay(void)
{
    for (int i = 0; i < 30; i++)
    {
        uint8_t message[] = {LED_ON, i, 25, 25, 25};
        send_command(message, ARRAY_SIZE(message));
        vTaskDelay(50/portTICK_PERIOD_MS);
    }
}

void turn_off(void)
{
    uint8_t message[] = {LED_OFF};
    send_command(message, ARRAY_SIZE(message));
}