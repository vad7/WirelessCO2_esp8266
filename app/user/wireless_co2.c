#include "user_config.h"
#include "bios.h"
#include "sdk/add_func.h"
#include "hw/esp8266.h"
#include "user_interface.h"
#include "web_iohw.h"
#include "flash_eep.h"
#include "webfs.h"
#include "sdk/libmain.h"
#include "driver/i2c.h"
#include "hw/gpio_register.h"
#include "wireless_co2.h"
#include "iot_cloud.h"
#include "driver/spi.h"
#include "driver/nrf24l01.h"

os_timer_t user_loop_timer DATA_IRAM_ATTR;

uint8 CO2_work_flag = 0; // 0 - not inited, 1 - wait incoming, 2 - send
uint8 CO2_send_flag;		// 0 - ready to send, 1 - sending
uint8 CO2_send_fan_idx;
uint8 nrf_last_rf_channel = 255;
uint16 receive_timeout;
#define CO2LevelAverageArrayLength 6
uint16_t CO2LevelAverageIdx = CO2LevelAverageArrayLength;
uint16_t CO2LevelAverageArray[CO2LevelAverageArrayLength];

void ICACHE_FLASH_ATTR set_new_rf_channel(uint8 ch)
{
	if(nrf_last_rf_channel != ch) {
		#if DEBUGSOO > 4
			os_printf("NRF-New ch %d\n", ch);
		#endif
		NRF24_WriteByte(NRF24_CMD_W_REGISTER | NRF24_REG_RF_CH,	ch);
		nrf_last_rf_channel = ch;
	}
}

void ICACHE_FLASH_ATTR CO2_set_fans_speed_current(void)
{
	int16_t i;
	if(CO2LevelAverageIdx == CO2LevelAverageArrayLength) { // First time
		for(i = 1; i < CO2LevelAverageArrayLength; i++)	CO2LevelAverageArray[i] = co2_send_data.CO2level;
	}
	if(CO2LevelAverageIdx >= CO2LevelAverageArrayLength) CO2LevelAverageIdx = 0;
	CO2LevelAverageArray[CO2LevelAverageIdx++] = co2_send_data.CO2level;
	uint32_t average = 0;
	for(i = 0; i < CO2LevelAverageArrayLength; i++) average += CO2LevelAverageArray[i];
	average /= CO2LevelAverageArrayLength;
	int8_t fanspeed;
	for(fanspeed = 0; fanspeed < FAN_SPEED_MAX; fanspeed++) {
		uint16_t tr = cfg_co2.fan_speed_threshold[fanspeed];
		if(average < tr) {
			// if there is a decrease of CO2 level - check delta
			if(fan_speed_previous <= fanspeed || (tr - average) >= cfg_co2.fan_speed_delta) break;
		}
	}
	fan_speed_previous = fanspeed;
	if((fanspeed += global_vars.fans_speed_override) > FAN_SPEED_MAX) fanspeed = FAN_SPEED_MAX;
	if(fanspeed < 0) fanspeed = 0;
	struct tm tm;
	time_t t = get_sntp_localtime();
	_localtime(&t, &tm);
	uint16 st, end, tt = tm.tm_hour * 60 + tm.tm_min; // min
	if(tm.tm_wday == 0 || tm.tm_wday == 6) { // Weekend?
		st = cfg_co2.night_start_wd;
		end = cfg_co2.night_end_wd;
	} else {
		st = cfg_co2.night_start;
		end = cfg_co2.night_end;
	}
	st = (st / 100) * 60 + st % 100;
	end = (end / 100) * 60 + end % 100;
	now_night = ((end > st && tt >= st && tt <= end) || (end < st && (tt >= st || tt <= end)));
	uint8 night = now_night_override == 2 ? 1 : now_night_override == 1 ? 0 : now_night;
	if(night && fanspeed > cfg_co2.fans_speed_night_max) fanspeed = cfg_co2.fans_speed_night_max;
	uint8 fan;
	for(fan = 0; fan < cfg_co2.fans; fan++) {
		CFG_FAN *f = &cfg_fans[fan];
		if(f->flags & (1<<FAN_SPEED_FORCED_BIT)) continue;
		int8_t fsp = fanspeed;
		if(night) {
			if(f->override_night == 1) fsp = f->speed_night; // =
			else if(f->override_night == 2) fsp += f->speed_night; // +
		} else {
			if(f->override_day == 1) fsp = f->speed_day; // =
			else if(f->override_day == 2) fsp += f->speed_day; // +
		}
		if(fsp < f->speed_min) fsp = f->speed_min;
		if(fsp > f->speed_max) fsp = f->speed_max;
		f->speed_current = fsp;
	}
}

void ICACHE_FLASH_ATTR user_loop(void) // call every 1 sec
{
	if(CO2_work_flag == 0) { // init
		set_new_rf_channel(cfg_co2.sensor_rf_channel);
		if(NRF24_SetAddresses(cfg_co2.address_LSB)) {
			NRF24_SetMode(NRF24_ReceiveMode);
			CO2_work_flag = 1;
		} else {
			#if DEBUGSOO > 4
				os_printf("NRF-SetAddr error\n");
			#endif
		}
		receive_timeout = global_vars.receive_timeout;
	} else if(CO2_work_flag == 1) { // wait incoming
		if(NRF24_Receive((uint8 *)&co2_send_data)) { // received
			time_t t = get_sntp_localtime();
			if(CO2_last_time) {
				uint16 d = t - CO2_last_time;
				if(average_period == 0) average_period = d;
				else average_period = (average_period + d) / 2;
			}
			if(history_co2 != NULL) { // store history co2
				if(history_co2_size == history_co2_len) {
					os_memmove(history_co2, history_co2 + 2, (history_co2_size - 1) * 2);
					history_co2_len--;
				}
				history_co2[history_co2_len++] = co2_send_data.CO2level;
			}
			CO2_last_time = t;
			#if DEBUGSOO > 4
				os_printf("NRF Received at %u, CO2: %u, F: %d (%d)\n", CO2_last_time, co2_send_data.CO2level, co2_send_data.FanSpeed, co2_send_data.Flags);
			#endif
			CO2_set_fans_speed_current();
xStartSending:
			CO2_send_fan_idx = 0;
			CO2_work_flag = 2;
			CO2_send_flag = 0;
			NRF24_Powerdown(); // need for SI24R01
			iot_cloud_send(1);
		} else if(receive_timeout) {
			if(--receive_timeout == 0) goto xStartSending;
		}
	} else if(CO2_work_flag == 2) { // send
xRepeat:
		if(cfg_co2.fans == 0) goto xNextFAN; // skip
		CFG_FAN *f = &cfg_fans[CO2_send_fan_idx];
		if(CO2_send_flag == 0) { // start
			if(f->flags & (1<<FAN_SKIP_BIT)) goto xNextFAN; // skip
			NRF24_SetMode(NRF24_TransmitMode);
			set_new_rf_channel(f->rf_channel);
			if(NRF24_SetAddresses(f->address_LSB)) {
				co2_send_data.FanSpeed = f->speed_current;
				NRF24_Transmit((uint8 *)&co2_send_data);
				CO2_send_flag = 1;
				#if DEBUGSOO > 4
					os_printf("NRF-Transmit %d <= %d (%u)\n", CO2_send_fan_idx, f->speed_current, system_get_time());
				#endif
			} else {
				#if DEBUGSOO > 4
					os_printf("NRF-SetAddr error: %d\n", CO2_send_fan_idx);
				#endif
			}
		} else { // sending..
			if(NRF24_transmit_status >= NRF24_Transmit_Ok) {
				f->transmit_last_status = NRF24_transmit_status;
				#if DEBUGSOO > 4
					os_printf("NRF-Transmit end: %d, %u\n", NRF24_transmit_status, system_get_time());
				#endif
				if(NRF24_transmit_status == NRF24_Transmit_Ok) {
					f->transmit_ok_last_time = get_sntp_localtime();
				}
				if(CO2_send_flag == 2) { // need repeat
					CO2_send_flag = 0;
					CO2_send_fan_idx = 0;
					goto xRepeat;
				}
xNextFAN:
				if(++CO2_send_fan_idx >= cfg_co2.fans) CO2_work_flag = 0;
				CO2_send_flag = 0;
			}
		}
	}
}

void  ICACHE_FLASH_ATTR send_fans_speed_now(uint8 calc_speed)
{
	if(calc_speed) CO2_set_fans_speed_current();
	if(CO2_work_flag == 2 && CO2_send_flag == 1) {
		CO2_send_flag = 2; // if now sending - repeat
	} else {
		CO2_work_flag = 2;
		CO2_send_flag = 0;
		CO2_send_fan_idx = 0;
	}
}

void ICACHE_FLASH_ATTR wireless_co2_init(uint8 index)
{
	if(flash_read_cfg(&cfg_co2, ID_CFG_CO2, sizeof(cfg_co2)) != sizeof(cfg_co2)) {
		// defaults
		os_memset(&cfg_co2, 0, sizeof(cfg_co2));
		cfg_co2.csv_delimiter = ',';
		cfg_co2.sensor_rf_channel = 2;
		cfg_co2.address_LSB = 0xC0;
		cfg_co2.fans = 0;
		cfg_co2.fan_speed_threshold[0] = 500;
		cfg_co2.fan_speed_threshold[1] = 550;
		cfg_co2.fan_speed_threshold[2] = 600;
		cfg_co2.fan_speed_threshold[3] = 800;
		cfg_co2.fan_speed_threshold[4] = 900;
		cfg_co2.fan_speed_threshold[5] = 1100;
	}
	if(flash_read_cfg(&cfg_fans, ID_CFG_FANS, sizeof(cfg_fans)) != sizeof(cfg_fans)) {
		os_memset(&cfg_fans, 0, sizeof(cfg_fans));
	}
	if(flash_read_cfg(&global_vars, ID_CFG_VARS, sizeof(global_vars)) != sizeof(global_vars)) {
		os_memset(&global_vars, 0, sizeof(global_vars));
	}
	fan_speed_previous = 0;
	//ets_timer_disarm(&user_loop_timer);
	os_timer_setfn(&user_loop_timer, (os_timer_func_t *)user_loop, NULL);
	ets_timer_arm_new(&user_loop_timer, 3000, 1, 1); // 1s, repeat
	NRF24_init(); // After init transmit must be delayed
	iot_cloud_init();
	if(history_co2 == NULL) history_co2 = os_malloc(HISTORY_CO2_BUFFER);
	if(history_co2 != NULL) history_co2_size = HISTORY_CO2_BUFFER / 2;

//	#if DEBUGSOO > 4
//		os_printf("\n");
//		for(; history_co2_len < 256; history_co2_len++) {
//			history_co2[history_co2_len] = 300 + (os_random() & 0x7FF);
//			os_printf("%d, ", history_co2[history_co2_len / 2]);
//		}
//		CO2_last_time = 1465995660;
//		average_period = 10;
//		os_printf("\n");
//	#endif
}

bool ICACHE_FLASH_ATTR write_wireless_co2_cfg(void) {
	return flash_save_cfg(&cfg_co2, ID_CFG_CO2, sizeof(cfg_co2));
}

bool ICACHE_FLASH_ATTR write_wireless_fans_cfg(void) {
	return flash_save_cfg(&cfg_fans, ID_CFG_FANS, sizeof(cfg_fans));
}

bool ICACHE_FLASH_ATTR write_global_vars_cfg(void) {
	return flash_save_cfg(&global_vars, ID_CFG_VARS, sizeof(global_vars));
}
