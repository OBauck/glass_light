

#include "nrf.h"
#include "nrf_drv_spi.h"
#include "pin_definitions.h"
#include "app_error.h"

static const nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(0);
static volatile bool spi_xfer_done;

void spi_event_handler(nrf_drv_spi_evt_t const * p_event)
{
    spi_xfer_done = true;
}

void lis3dh_init(void)
{
    nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
    spi_config.ss_pin   = ACC_CS_PIN;
    spi_config.miso_pin = ACC_MISO_PIN;
    spi_config.mosi_pin = ACC_MOSI_PIN;
    spi_config.sck_pin  = ACC_SCK_PIN;
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, spi_event_handler));
    
    //TODO: initialize int pins
}

void lis3dh_read(uint8_t addr, uint8_t *data, uint8_t size)
{
    static uint8_t cmd_byte;
    
    if(size > 2)
    {
        //address is only 6 bits long 
        cmd_byte = (addr & 0x3F) | 0xA0;
    }
    else
    {
        cmd_byte = (addr & 0x3F) | 0x80;
    }
    
    spi_xfer_done = false;
    nrf_drv_spi_transfer(&spi, &cmd_byte, 1, data, size);
    
    while (!spi_xfer_done)
    {
        __WFE();
    }
}
