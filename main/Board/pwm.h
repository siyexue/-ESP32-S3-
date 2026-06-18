#ifndef __PWM_H_
#define __PWM_H_

#include <stdint.h>

void pwm_breath_led_init(void);
void pwm_breath_task(void *pvParam);
void pwm_set_duty(uint8_t duty);
uint8_t pwm_get_duty(void);
void pwm_enable_breath(uint8_t enable);

#endif
