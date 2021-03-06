/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/** @file
 *
 * @defgroup ble_sdk_uart_over_ble_main main.c
 * @{
 * @ingroup  ble_sdk_app_nus_eval
 * @brief    UART over BLE application main file.
 *
 * This file contains the source code for a sample application that uses the Nordic UART service.
 * This application uses the @ref srvlib_conn_params module.
 */

#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "app_button.h"
#include "ble_glass_light.h"
#include "app_uart.h"
#include "app_util_platform.h"
#include "bsp.h"
#include "bsp_btn_ble.h"
#include "nrf_delay.h"
#include "nrf_drv_gpiote.h"
#include "advertiser_beacon.h"

#define NRF_LOG_MODULE_NAME "APP"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "nrf_drv_ws2812.h"
#include "pin_definitions.h"
#include "lis3dh.h"

#define IS_SRVC_CHANGED_CHARACT_PRESENT 0                                           /**< Include the service_changed characteristic. If not enabled, the server's database cannot be changed for the lifetime of the device. */

#if (NRF_SD_BLE_API_VERSION == 3)
#define NRF_BLE_MAX_MTU_SIZE            GATT_MTU_SIZE_DEFAULT                       /**< MTU size used in the softdevice enabling and to reply to a BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST event. */
#endif

#define APP_FEATURE_NOT_SUPPORTED       BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2        /**< Reply when unsupported features are requested. */

#define CENTRAL_LINK_COUNT              0                                           /**< Number of central links used by the application. When changing this number remember to adjust the RAM settings*/
#define PERIPHERAL_LINK_COUNT           1                                           /**< Number of peripheral links used by the application. When changing this number remember to adjust the RAM settings*/

#define DEVICE_NAME                     "glass_light"                               /**< Name of device. Will be included in the advertising data. */
#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */

#define APP_ADV_INTERVAL                320                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */
#define APP_ADV_TIMEOUT_IN_SECONDS      180                                         /**< The advertising timeout (in units of seconds). */

#define APP_TIMER_PRESCALER             0                                           /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                           /**< Size of timer operation queues. */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(20, UNIT_1_25_MS)             /**< Minimum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(75, UNIT_1_25_MS)             /**< Maximum acceptable connection interval (75 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   0                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)  /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER) /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

#define DEAD_BEEF                       0xDEADBEEF                                  /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define UART_TX_BUF_SIZE                256                                         /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE                256                                         /**< UART RX buffer size. */

static ble_nus_t                        m_nus;                                      /**< Structure to identify the Nordic UART Service. */
static uint16_t                         m_conn_handle = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current connection. */

static ble_uuid_t                       m_adv_uuids[] = {{BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}};  /**< Universally unique service identifier. */

APP_TIMER_DEF(m_charge_timer_id);
APP_TIMER_DEF(m_charge_led_pulse_timer_id);
#define CHARGING_TIMER_INTERVAL			APP_TIMER_TICKS(1000, APP_TIMER_PRESCALER)
#define CHARGING_LED_PULSE_LENGTH		APP_TIMER_TICKS(50, APP_TIMER_PRESCALER)

APP_TIMER_DEF(m_fade_timer_id);
#define FADE_TIMER_INTERVAL             APP_TIMER_TICKS(4000/256, APP_TIMER_PRESCALER)

nrf_drv_WS2812_pixel_t fader_setpoint_1;
nrf_drv_WS2812_pixel_t fader_setpoint_2;
nrf_drv_WS2812_pixel_t fader_current_value;

//TODO: change these to defines
nrf_drv_WS2812_pixel_t color_red =    {.red = 255};
nrf_drv_WS2812_pixel_t color_yellow = {.red = 255, .green = 255};
nrf_drv_WS2812_pixel_t color_green =  {.green = 255};
nrf_drv_WS2812_pixel_t color_cyan =   {.green = 255, .blue = 255};
nrf_drv_WS2812_pixel_t color_blue =   {.blue = 255};
nrf_drv_WS2812_pixel_t color_purple = {.red = 255, .blue = 255};
nrf_drv_WS2812_pixel_t color_white =  {.red = 255, .green = 255, .blue = 255};
nrf_drv_WS2812_pixel_t color_off;

#define BEACON_ADV_INTERVAL      760
#define BEACON_URL               "\x03goo.gl/rX4mVo" /**< https://goo.gl/pIWdir short for https://developer.nordicsemi.com/thingy/52/ */
#define BEACON_URL_LEN           14

/**@brief Function for assert macro callback.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyse
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num    Line number of the failing ASSERT call.
 * @param[in] p_file_name File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}


/**@brief Function for the GAP initialization.
 *
 * @details This function will set up all the necessary GAP (Generic Access Profile) parameters of
 *          the device. It also sets the permissions and appearance.
 */
static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *) DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

void ws2812b_set_color(char str[])
{
	nrf_drv_WS2812_pixel_t color;
    memset(&color, 0, sizeof(nrf_drv_WS2812_pixel_t));
	
	if(strncmp("red", str, 3) == 0)
	{
        color = color_red;
	}
	else if(strncmp("yellow", str, 6) == 0)
	{
		color = color_yellow;
	}
	else if(strncmp("green", str, 5) == 0)
	{
		color = color_green;
	}
	else if(strncmp("cyan", str, 4) == 0)
	{
		color = color_cyan;
	}
	else if(strncmp("blue", str, 4) == 0)
	{
		color = color_blue;
	}
	else if(strncmp("purple", str, 6) == 0)
	{
		color = color_purple;
	}
	else if(strncmp("white", str, 5) == 0)
	{
		color = color_white;
	}
	
	for(uint8_t i = 0; i < NR_OF_PIXELS; i++)
	{
		nrf_drv_WS2812_set_pixel(i, &color);
	}
	nrf_drv_WS2812_show();
}

/**@brief Function for handling the data from the Nordic UART Service.
 *
 * @details This function will process the data received from the Nordic UART BLE Service and send
 *          it to the UART module.
 *
 * @param[in] p_nus    Nordic UART Service structure.
 * @param[in] p_data   Data to be send to UART module.
 * @param[in] length   Length of the data.
 */
/**@snippet [Handling the data received over BLE] */
static void nus_data_handler(ble_nus_t * p_nus, nrf_drv_WS2812_pixel_t *p_color)
{
    #if defined(BOARD_CUSTOM)
        for(uint8_t i = 0; i < NR_OF_PIXELS; i++)
        {
            nrf_drv_WS2812_set_pixel(i, p_color);
        }
        nrf_drv_WS2812_show();
    #elif defined(BOARD_PCA10040)
        if(p_color->red)
            nrf_gpio_pin_clear(17);
        else
            nrf_gpio_pin_set(17);
        
        if(p_color->blue)
            nrf_gpio_pin_clear(19);
        else
            nrf_gpio_pin_set(19);
        
        if(p_color->green)
            nrf_gpio_pin_clear(18);
        else
            nrf_gpio_pin_set(18);
    #endif
        
        
    
}
/**@snippet [Handling the data received over BLE] */

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void)
{
    uint32_t       err_code;
    ble_nus_init_t nus_init;

    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;

    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling an event from the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module
 *          which are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by simply setting
 *       the disconnect_on_fail config parameter, but instead we use the event handler
 *       mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}


/**@brief Function for handling errors from the Connection Parameters module.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    uint32_t err_code;

    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_IDLE:
            err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
			APP_ERROR_CHECK(err_code);
            break;
        default:
            break;
    }
}


/**@brief Function for the application's SoftDevice event handler.
 *
 * @param[in] p_ble_evt SoftDevice event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t err_code;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break; // BLE_GAP_EVT_CONNECTED

        case BLE_GAP_EVT_DISCONNECTED:
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
        
            //turn off LEDs
            #if defined(BOARD_CUSTOM)
                for(uint8_t i = 0; i < NR_OF_PIXELS; i++)
                {
                    nrf_drv_WS2812_set_pixel(i, &color_off);
                }
                nrf_drv_WS2812_show();
            #elif defined(BOARD_PCA10040)
                nrf_gpio_pin_set(17);
                nrf_gpio_pin_set(18);
                nrf_gpio_pin_set(19);
            #endif
            break; // BLE_GAP_EVT_DISCONNECTED

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // Pairing not supported
            err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
            APP_ERROR_CHECK(err_code);
            break; // BLE_GAP_EVT_SEC_PARAMS_REQUEST

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break; // BLE_GATTS_EVT_SYS_ATTR_MISSING

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break; // BLE_GATTC_EVT_TIMEOUT

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break; // BLE_GATTS_EVT_TIMEOUT

        case BLE_EVT_USER_MEM_REQUEST:
            err_code = sd_ble_user_mem_reply(p_ble_evt->evt.gattc_evt.conn_handle, NULL);
            APP_ERROR_CHECK(err_code);
            break; // BLE_EVT_USER_MEM_REQUEST

        case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
        {
            ble_gatts_evt_rw_authorize_request_t  req;
            ble_gatts_rw_authorize_reply_params_t auth_reply;

            req = p_ble_evt->evt.gatts_evt.params.authorize_request;

            if (req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID)
            {
                if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ)     ||
                    (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) ||
                    (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL))
                {
                    if (req.type == BLE_GATTS_AUTHORIZE_TYPE_WRITE)
                    {
                        auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
                    }
                    else
                    {
                        auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
                    }
                    auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
                    err_code = sd_ble_gatts_rw_authorize_reply(p_ble_evt->evt.gatts_evt.conn_handle,
                                                               &auth_reply);
                    APP_ERROR_CHECK(err_code);
                }
            }
        } break; // BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST

#if (NRF_SD_BLE_API_VERSION == 3)
        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
            err_code = sd_ble_gatts_exchange_mtu_reply(p_ble_evt->evt.gatts_evt.conn_handle,
                                                       NRF_BLE_MAX_MTU_SIZE);
            APP_ERROR_CHECK(err_code);
            break; // BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST
#endif

        default:
            // No implementation needed.
            break;
    }
}


/**@brief Function for dispatching a SoftDevice event to all modules with a SoftDevice
 *        event handler.
 *
 * @details This function is called from the SoftDevice event interrupt handler after a
 *          SoftDevice event has been received.
 *
 * @param[in] p_ble_evt  SoftDevice event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_nus_on_ble_evt(&m_nus, p_ble_evt);
    on_ble_evt(p_ble_evt);
    ble_advertising_on_ble_evt(p_ble_evt);

}

static void sys_evt_dispatch(uint32_t evt_id)
{
    app_beacon_on_sys_evt(evt_id);
}

/**@brief Function for the SoftDevice initialization.
 *
 * @details This function initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void)
{
    uint32_t err_code;

    nrf_clock_lf_cfg_t clock_lf_cfg = {	 .source        = NRF_CLOCK_LF_SRC_RC,            \
										 .rc_ctiv       = 16,                                \
										 .rc_temp_ctiv  = 2,                                \
										 .xtal_accuracy = 0};

    // Initialize SoftDevice.
    SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);

    ble_enable_params_t ble_enable_params;
    err_code = softdevice_enable_get_default_config(CENTRAL_LINK_COUNT,
                                                    PERIPHERAL_LINK_COUNT,
                                                    &ble_enable_params);
    APP_ERROR_CHECK(err_code);

    //Check the ram settings against the used number of links
    CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT,PERIPHERAL_LINK_COUNT);

    // Enable BLE stack.
#if (NRF_SD_BLE_API_VERSION == 3)
    ble_enable_params.gatt_enable_params.att_mtu = NRF_BLE_MAX_MTU_SIZE;
#endif
    err_code = softdevice_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);

    // Subscribe for BLE events.
    err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);
    
    err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{
    uint32_t               err_code;
    ble_advdata_t          advdata;
    ble_advdata_t          scanrsp;
    ble_adv_modes_config_t options;

    // Build advertising data struct to pass into @ref ble_advertising_init.
    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type          = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = false;
    advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

    memset(&scanrsp, 0, sizeof(scanrsp));
    scanrsp.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    scanrsp.uuids_complete.p_uuids  = m_adv_uuids;

    memset(&options, 0, sizeof(options));
    options.ble_adv_fast_enabled  = true;
    options.ble_adv_fast_interval = APP_ADV_INTERVAL;
    options.ble_adv_fast_timeout  = APP_ADV_TIMEOUT_IN_SECONDS;

    err_code = ble_advertising_init(&advdata, &scanrsp, &options, on_adv_evt, NULL);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for placing the application in low power state while waiting for events.
 */
static void power_manage(void)
{
    uint32_t err_code = sd_app_evt_wait();
    APP_ERROR_CHECK(err_code);
}

static void ws2812_test()
{
	for(int i = 0; i < NR_OF_PIXELS; i++)
	{
		nrf_drv_WS2812_set_pixel(i, &color_red);
	}
	
	nrf_drv_WS2812_show();
	nrf_delay_ms(1000);
	
	for(int i = 0; i < NR_OF_PIXELS; i++)
	{
		nrf_drv_WS2812_set_pixel(i, &color_green);
	}
	
	nrf_drv_WS2812_show();
	nrf_delay_ms(1000);
	
	for(int i = 0; i < NR_OF_PIXELS; i++)
	{
		nrf_drv_WS2812_set_pixel(i, &color_blue);
	}
	
	nrf_drv_WS2812_show();
	nrf_delay_ms(1000);
	
	for(int i = 0; i < NR_OF_PIXELS; i++)
	{
		nrf_drv_WS2812_set_pixel(i, &color_off);
	}
	
	nrf_drv_WS2812_show();
}

static void charge_timer_handler(void *p_context)
{
	static uint8_t color;
	uint32_t err_code = app_timer_start(m_charge_led_pulse_timer_id, CHARGING_LED_PULSE_LENGTH, NULL);
	APP_ERROR_CHECK(err_code);
	
	color++;
	if(color > 2)
	{
		color = 0;
	}
	//turn on LEDs
	for(int i = 0; i < NR_OF_PIXELS; i++)
	{
		switch(color)
		{
			case 0:
				nrf_drv_WS2812_set_pixel(i, &color_red);
				break;
			case 1:
				nrf_drv_WS2812_set_pixel(i, &color_green);
				break;
			case 2:
				nrf_drv_WS2812_set_pixel(i, &color_blue);
				break;
		}
	}
	
	nrf_drv_WS2812_show();
}

static void charge_led_pulse_timer_handler(void *p_context)
{
	for(int i = 0; i < NR_OF_PIXELS; i++)
	{
		nrf_drv_WS2812_set_pixel(i, &color_off);
	}
	
	nrf_drv_WS2812_show();
}

static void charge_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
	uint32_t err_code;
    bool pin_status = nrf_drv_gpiote_in_is_set(pin);
	
	if(pin_status)
	{
		//done charging
		err_code = app_timer_stop(m_charge_timer_id);
		APP_ERROR_CHECK(err_code);
	}
	else
	{
		//charging
		err_code = app_timer_start(m_charge_timer_id, CHARGING_TIMER_INTERVAL, NULL);
		APP_ERROR_CHECK(err_code);
	}
}

static void charge_detection_init(uint32_t pin)
{
	uint32_t err_code;
	
	if(!nrf_drv_gpiote_is_init())
	{
		err_code = nrf_drv_gpiote_init();
		APP_ERROR_CHECK(err_code);
	}
	
	nrf_drv_gpiote_in_config_t pin_config = GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
	pin_config.pull = NRF_GPIO_PIN_PULLUP;
	
	err_code = nrf_drv_gpiote_in_init(pin, &pin_config, charge_pin_handler);
	APP_ERROR_CHECK(err_code);
	
	nrf_drv_gpiote_in_event_enable(pin, true);
	
	err_code = app_timer_create(&m_charge_timer_id, APP_TIMER_MODE_REPEATED, charge_timer_handler);
	APP_ERROR_CHECK(err_code);
	
	err_code = app_timer_create(&m_charge_led_pulse_timer_id, APP_TIMER_MODE_SINGLE_SHOT, charge_led_pulse_timer_handler);
	APP_ERROR_CHECK(err_code);
}

void gpio_led_init()
{
    nrf_gpio_pin_set(17);
    nrf_gpio_pin_set(18);
    nrf_gpio_pin_set(19);
    
    nrf_gpio_cfg_output(17);
    nrf_gpio_cfg_output(18);
    nrf_gpio_cfg_output(19);
}

/**@brief Function for handling a BeaconAdvertiser error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void beacon_advertiser_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the beacon timeslot functionality.
 */
static uint32_t timeslot_init(void)
{
    uint32_t err_code;
    static ble_beacon_init_t beacon_init;

    beacon_init.adv_interval  =  BEACON_ADV_INTERVAL;
    beacon_init.p_data         = (uint8_t *)BEACON_URL;
    beacon_init.data_size      = BEACON_URL_LEN;
    beacon_init.error_handler = beacon_advertiser_error_handler;

    err_code = sd_ble_gap_addr_get(&beacon_init.beacon_addr);

    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }

    // Increment device address by 2 for beacon advertising.
    beacon_init.beacon_addr.addr[0] += 2;

    app_beacon_init(&beacon_init);
    app_beacon_start();

    return NRF_SUCCESS;
}

/**@brief Application main function.
 */
int main(void)
{
    uint32_t err_code;
    
    // Initialize.
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);

    #if defined(BOARD_CUSTOM)
        nrf_drv_WS2812_init(WS2812_PIN);
        //ws2812_test();
    #elif defined(BOARD_PCA10040)
        gpio_led_init();
    #endif
    
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    
    NRF_LOG_INFO("Glass light v1.0");
    
    ble_stack_init();
    gap_params_init();
    services_init();
    advertising_init();
    conn_params_init();

    err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);

    timeslot_init();

    #if defined(BOARD_CUSTOM)
        charge_detection_init(CHARGE_STAT_PIN);
    #endif
	
    
    /*
    lis3dh_init();
    
    lis3dh_read(0x0F, data, 2);
    */
    // Enter main loop.
    for (;;)
    {
        //nrf_delay_ms(1000);
        //lis3dh_read(0x0F, data, 2);
        if (NRF_LOG_PROCESS() == false)
        {
            power_manage();
        }
    }
}


/**
 * @}
 */
