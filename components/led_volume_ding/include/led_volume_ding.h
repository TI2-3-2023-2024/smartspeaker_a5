enum led_effects {
    LED_OFF,
    LED_ON
};

void config_master(void);
void send_command(uint8_t *message, size_t len);
void turn_on_white_delay(void);
void turn_off(void);
void app_main(void);