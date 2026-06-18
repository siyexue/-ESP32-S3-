#ifndef __MQTT_H_
#define __MQTT_H_

void mqtt_init(void);
int mqtt_publish(const char *topic,const char *data);

#endif
