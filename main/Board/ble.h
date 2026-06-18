/*
 * @Author: 思夜雪
 * @Date: 2026-06-17
 * @Description: BLE 调光模块 — 手机通过 nRF Connect 写入亮度值控制 LED
 * @FilePath: \hello_world\main\Board\ble.h
 */

#ifndef __BLE_H_
#define __BLE_H_

#include <stdint.h>

void ble_init(void);
uint8_t ble_get_brightness(void);

#endif