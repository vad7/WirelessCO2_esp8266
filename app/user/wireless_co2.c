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

uint8 CO2_receive_flag = 0; // 0 - not inited, 1 - wait incoming, 2 - send
uint8 CO2_send_flag;		// 0 - ready to send, 1 - sending
uint8 CO2_send_fan_idx;

// abs() 64 bit -> uinsigned 32 bit
uint32 abs_64(sint64 n)
{
	return n < 0 ? -n : n;
}

void ICACHE_FLASH_ATTR CO2_set_fans_speed(void)
{


}

void ICACHE_FLASH_ATTR user_loop(void) // call every 1 sec
{
	if(CO2_receive_flag == 0) { // init
		NRF24_WriteByte(NRF24_CMD_W_REGISTER | NRF24_REG_RF_CH,	cfg_co2.sensor_rf_channel);
		if(NRF24_SetAddresses(cfg_co2.address_LSB)) {
			NRF24_SetMode(NRF24_ReceiveMode);
			CO2_receive_flag = 1;

		} else {
			#if DEBUGSOO > 4
				os_printf("NRF-SetAddr error\n", );
			#endif
		}
	} else if(CO2_receive_flag == 1) { // wait incoming
		if(NRF24_Receive(&co2_send_data)) { // received
			CO2_last_time = get_sntp_localtime();
			CO2_set_fans_speed();
			CO2_send_fan_idx = 0;
			CO2_receive_flag = 2;
			CO2_send_flag = 0;
		}
	} else if(CO2_receive_flag == 2) { // send
		CFG_FANS
		if(CO2_send_flag == 0) { // start
			if(cfg_fans[CO2_send_fan_idx].flags & 1) goto xSkipFAN; // skip
			cfg_fans[CO2_send_fan_idx].

		}

	}



	//FRAM_speed_test();
/*	time_t time = get_sntp_localtime();
	if(time && (time - fram_store.LastTime >= TIME_STEP_SEC)) { // Passed 1 min
		ets_set_idle_cb(NULL, NULL);
		if(Sensor_Edge && abs_64((sint64)system_get_time() - PowerCntTime) > 2000000) {
			// some problem here, after 2 sec - normal state of edge = 0
			gpio_pin_intr_state_set(SENSOR_PIN, SENSOR_FRONT_EDGE);
			Sensor_Edge = 0;
		}
		user_idle_func_working = 1;
		ets_intr_unlock();
		update_cnts(time);
		ets_set_idle_cb(user_idle, NULL);
		user_idle_func_working = 0;
	}
*/
}


void ICACHE_FLASH_ATTR wireless_co2_init(uint8 index)
{
	if(flash_read_cfg(&cfg_co2, ID_CFG_CO2, sizeof(cfg_co2)) != sizeof(CFG_CO2)) {
		// defaults
		os_memset(&cfg_co2, 0, sizeof(CFG_CO2));
		cfg_co2.csv_delimiter = ',';
		cfg_co2.sensor_rf_channel = 120;
	}
	ets_timer_disarm(&user_loop_timer);
	os_timer_setfn(&user_loop_timer, (os_timer_func_t *)user_loop, NULL);
	ets_timer_arm_new(&user_loop_timer, 1000, 1, 1); // 1s, repeat
	NRF24_init(); // After init transmit must be delayed
}

bool ICACHE_FLASH_ATTR write_wireless_co2_cfg(void) {
	return flash_save_cfg(&cfg_co2, ID_CFG_CO2, sizeof(cfg_co2));
}

