/*
 * ota_update.h
 *
 *  Created on: Apr 19, 2023
 *      Author: FOTA
 */

#ifndef INC_OTA_UPDATE_H_
#define INC_OTA_UPDATE_H_

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "main.h"


/* our bootloader commands */
#define BL_GET_HELP               0x48 //This command is used to get the supported commands.
#define BL_GET_VER		          0x51 //This command is used to read the bootloader version from the MCU.
#define BL_FLASH_ERASE            0x56 //This command is used to mass erase or sector erase of the user flash.
#define BL_MEM_WRITE_SIZE	      0x57 //This command is used to get size of the data.
#define BL_MEM_WRITE_ADDRESS      0x58 //This command is used to get address of the data.
#define BL_MEM_WRITE_DATA         0x110 //This command is used to write data in to different memories of the MCU.
#define BL_GO_TO_ADDR	          0x55 //This command is used to jump bootloader to specified address.
#define FIRMWARE_OVER_THE_AIR     0x76 //This command is used to update the firmware of the application
#define GOTO_BL                   0x66 //This command is used to goto bl from user app
/* app version */
#define APP_VERSION               0x02
/* addresses */
#define ADDR_VALID                0x00
#define ADDR_INVALID              0x01
/* BL_MEM_ERASE_CMD */
#define FLASH_ERASE_FAILED        0x01
#define FLASH_ERASE_SUCCESS       0x00
#define INVALID_SECTOR            0x12
/* BL_MEM_WRITE_CMD */
#define FLASH_WRITE_FAILED        0x01
#define FLASH_WRITE_SUCCESS       0x00
#define NO_AVAILABLE_BYTES        0x78



uint32_t get_Active_Bank_no(void);
void toggleBankAndReset(void);

void bootloader_handle_gethelp_cmd(void);
void bootloader_handle_getver_cmd(void);
void bootloader_handle_go_cmd(void);
void bootloader_handle_flash_erase_cmd(void);
void bootloader_handle_mem_write_size_cmd(void);
void bootloader_handle_mem_write_address_cmd(void);
void bootloader_handle_mem_write_data_cmd(void);

void bootloader_can_read_data(void);
void bootloader_can_write_data(uint32_t length_of_data);

uint8_t get_app_version(void);
uint8_t verify_address(uint32_t go_address);
uint8_t execute_flash_erase(uint32_t initial_sector_number , uint32_t number_of_sector);
uint8_t execute_mem_write(void);
uint8_t UpdateAPP(void);
void check_rx_counter(void);
void check_command_counter(void);

#endif /* INC_OTA_UPDATE_H_ */
