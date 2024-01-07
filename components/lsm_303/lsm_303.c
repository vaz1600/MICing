#include <stdio.h>
#include "esp_err.h"

#include "lsm_303.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#define I2C_MASTER_SCL_IO           22      					/*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           21      					/*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0                          /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          100000                     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                          /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       200

/*=========================================================================
    I2C ADDRESS/BITS
    -----------------------------------------------------------------------*/
    #define LSM303_ADDRESS_ACCEL          (0x32 >> 1)         // 0011001x
    #define LSM303_ADDRESS_MAG            (0x3C >> 1)         // 0011110x
/*=========================================================================*/


/*=========================================================================
    INTERNAL MAGNETOMETER DATA TYPE
    -----------------------------------------------------------------------*/
    typedef struct lsm303MagData_s
    {
        int16_t x;
        int16_t y;
        int16_t z;
    } lsm303MagData;
/*=========================================================================*/

/*=========================================================================
    INTERNAL ACCELERATION DATA TYPE
    -----------------------------------------------------------------------*/
    typedef struct lsm303AccelData_s
    {
      int16_t x;
      int16_t y;
      int16_t z;
    } lsm303AccelData;
/*=========================================================================*/

/*=========================================================================
    CHIP ID
    -----------------------------------------------------------------------*/
    #define LSM303_ID                     (0b11010100)
/*=========================================================================*/


/**
 * @brief Read a sequence of bytes from a MPU9250 sensor registers
 */
static uint8_t read8(uint8_t reg_addr)
{
	uint8_t data;

	i2c_master_write_read_device(I2C_MASTER_NUM, LSM303_ADDRESS_ACCEL, &reg_addr, 1, &data, 1, I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);

    return data;
}

static uint8_t readb(uint8_t reg_addr, uint8_t *buf, uint8_t size)
{
	i2c_master_write_read_device(I2C_MASTER_NUM, LSM303_ADDRESS_ACCEL, &reg_addr, 1, buf, size, I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);

    return 0;
}

/**
 * @brief Write a byte to a MPU9250 sensor register
 */
static esp_err_t write8(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, LSM303_ADDRESS_ACCEL, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_RATE_MS);

    return ret;
}

/**
 * @brief i2c master initialization
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}


esp_err_t LSM303_init()
{
	esp_err_t err;

	// Enable I2C
	err = i2c_master_init();

	// Enable the accelerometer (100Hz)
	write8(LSM303_REGISTER_ACCEL_CTRL_REG1_A, 0x57);

	// LSM303DLHC has no WHOAMI register so read CTRL_REG1_A back to check
	// if we are connected or not
	uint8_t reg1_a = read8(LSM303_REGISTER_ACCEL_CTRL_REG1_A);

	if (reg1_a != 0x57)
	{
		printf("lsm303 CTRL_REG1_A is 0x%02x\r\n", reg1_a);
		return ESP_FAIL;
	}

	// Enable the HPF (2Hz)
	write8(LSM303_REGISTER_ACCEL_CTRL_REG2_A, 0x08);

	return ESP_OK;
}

esp_err_t LSM303_read(int16_t *x, int16_t *y, int16_t *z)
{
	uint8_t acc[6];
	int16_t raw_x;
	int16_t raw_y;
	int16_t raw_z;

	readb(LSM303_REGISTER_ACCEL_OUT_X_L_A | 0x80, acc, 6);

	raw_x = acc[0] | (acc[1] << 8);
	raw_y = acc[2] | (acc[3] << 8);
	raw_z = acc[4] | (acc[5] << 8);

	*x = (int16_t )raw_x;
	*y = (int16_t )raw_y;
	*z = (int16_t )raw_z;

	return ESP_OK;
}
