#ifndef __OLED_H_
#define __OLED_H_

#include <stdbool.h>

#define OLED_W       128
#define OLED_H       64

void oled_init(void);
void oled_clear(void);
void oled_show_char(int x, int y, char c);
void oled_show_string(int x, int y, const char *str);
void oled_show_num(int x, int y, int num);

#endif
