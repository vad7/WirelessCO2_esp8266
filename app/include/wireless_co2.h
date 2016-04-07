#ifndef _power_meter_
#define _power_meter_

#include "sntp.h"

#define FANS_MAX 		10
#define FAN_SPEED_MAX	6

typedef struct __attribute__((packed)) {
	char	csv_delimiter; 			// ','
	uint8	iot_cloud_enable;		// use "protect/iot_cloud.ini" to send data to IoT cloud
	uint8 	sensor_rf_channel;		// 0..126 nrf24l01 channel
	uint8	address_LSB;			// Address LSB for receiving
	uint8	fans;					// numbers of fans
	uint16	fan_speed_threshold[FAN_SPEED_MAX]; //  Fan speed: CO2 < 500 ppm = 0; < 550 ppm = 1; < 600 ppm = 2; < 800 ppm = 3; < 900 ppm = 4; < 1100 ppm = 5; > = 6 (max)
	uint16	night_start;			// hh,mm
	uint16	night_end;				// hh,mm
	uint16	night_wend_start;		// hh,mm at weekend
	uint16	night_wend_end;			// hh,mm at weekend

//	char sntp_server[20];
} CFG_CO2;
CFG_CO2 cfg_co2;

typedef struct __attribute__((packed)) {
	uint8 flags;			// b1: skip
	uint8 rf_channel;
	uint8 address_LSB;
	int8 speed_min;
	int8 speed_max;
	int8 override_at_night;	// 0 - no, 1 - set =speed_night, 2 - set +speed_night
	int8 speed_night;
} CFG_FAN;
CFG_FAN cfg_fans[FANS_MAX]; // Actual number of fans stored in cfg_co2.fans

typedef struct __attribute__ ((packed)) {
	uint16_t CO2level;
	uint8_t FanSpeed;
	uint8_t Flags; // Mask: 0x80 - Setup command, 0x01 - Lowlight
} CO2_SEND_DATA;
CO2_SEND_DATA co2_send_data;

timet_t CO2_last_time;
// Cookies:
uint32 Web_ChartMaxDays; 	// ~ChartMaxDays~
uint32 Web_ShowByDay; 		// ~ShowByDay~
//

uint32 abs_64(sint64 n);
void wireless_co2_init(uint8 index) ICACHE_FLASH_ATTR;
void user_loop(void) ICACHE_FLASH_ATTR;
bool write_wireless_co2_cfg(void) ICACHE_FLASH_ATTR;
uint8_t iot_cloud_init(void) ICACHE_FLASH_ATTR;
void iot_data_clear(void) ICACHE_FLASH_ATTR;
void iot_cloud_send(uint8 fwork) ICACHE_FLASH_ATTR;

void uart_wait_tx_fifo_empty(void) ICACHE_FLASH_ATTR;
void _localtime(time_t * tim_p, struct tm * res) ICACHE_FLASH_ATTR;

#endif
