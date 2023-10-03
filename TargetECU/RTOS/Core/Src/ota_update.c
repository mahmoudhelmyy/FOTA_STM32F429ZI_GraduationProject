/*
 * ota_update.c
 *
 *  Created on: Apr 19, 2023
 *      Author: FOTA
 */
#include "ota_update.h"

uint8_t supported_commands[5] = {
BL_GET_VER,
BL_GET_HELP,
BL_GO_TO_ADDR,
BL_FLASH_ERASE,
BL_MEM_WRITE_DATA, };
uint16_t SIZE;
uint32_t ADDRESS = 0x08100000;
uint8_t first_time = 1;

/* Shared variables ---------------------------------------------------------*/
extern CAN_HandleTypeDef hcan1;
extern RTC_HandleTypeDef hrtc;
extern UART_HandleTypeDef huart1;

extern CAN_RxHeaderTypeDef RxHeader;
extern CAN_TxHeaderTypeDef TxHeader;
extern uint32_t TxMailbox;
extern uint8_t RxData[8];
extern uint8_t TxData[8];
uint8_t ack_no = 0;

void go_to_bootloader(void) {
	// Write Back Up Register 1 Data
	HAL_PWR_EnableBkUpAccess();
	// Writes a data in a RTC Backup data Register 1
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x0001);
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0x0001);
	HAL_PWR_DisableBkUpAccess();
	HAL_NVIC_SystemReset();
}

void bootloader_handle_gotobl(void) {
	// Write Back Up Register 1 Data
	HAL_PWR_EnableBkUpAccess();
	// Writes a data in a RTC Backup data Register 0
	HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x0001);
	HAL_PWR_DisableBkUpAccess();
	HAL_NVIC_SystemReset();
}

void toggleBankAndReset() {
	FLASH_AdvOBProgramInitTypeDef OBInit;
	HAL_FLASH_Unlock();
	//__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);
	HAL_FLASH_OB_Unlock();
	HAL_FLASHEx_AdvOBGetConfig(&OBInit);
	OBInit.OptionType = OPTIONBYTE_BOOTCONFIG;

	if (((OBInit.BootConfig) & (OB_DUAL_BOOT_ENABLE)) == OB_DUAL_BOOT_ENABLE) {
		OBInit.BootConfig = OB_DUAL_BOOT_DISABLE;
	} else {
		OBInit.BootConfig = OB_DUAL_BOOT_ENABLE;
	}
	if (HAL_FLASHEx_AdvOBProgram(&OBInit) != HAL_OK) {

		while (1) {
			HAL_Delay(1000);
			HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);
		}
	}
	if (HAL_FLASH_OB_Launch() != HAL_OK) {

		while (1) {
			HAL_Delay(100);
			HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);
		}
	}
	HAL_FLASH_OB_Lock();
	HAL_FLASH_Lock();
	HAL_NVIC_SystemReset();

}

uint32_t get_Active_Bank_no(void) {
	uint32_t checkBank = READ_BIT(SYSCFG->MEMRMP, SYSCFG_MEMRMP_UFB_MODE);
	if (checkBank == 0) {
		printf("Program Running in Bank 1 \r\n");
	} else {
		printf("Program Running in Bank 2 \r\n");
	}
	return checkBank;
}

void sendHeartbeat() {
	uint32_t dummy;

	HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &dummy);
}

/****************************************************************************************
 ********************* implementation of bootloader command handle functions *************
 *****************************************************************************************/

/*Helper function to handle BL_GET_HELP command
 * Bootloader sends out All supported Command codes
 */
void bootloader_handle_gethelp_cmd(void) {
	TxHeader.StdId = BL_GET_HELP;

	for (uint32_t i = 0; i < 5; i++) {
		TxData[i] = supported_commands[i];
	}

	//send the supported commands to the node_mcu
	bootloader_can_write_data(1);
}

/*Helper function to handle BL_GET_VER command */
void bootloader_handle_getver_cmd(void) {
	uint8_t app_version;

	app_version = get_app_version();

	TxHeader.StdId = BL_GET_VER;

	TxData[0] = app_version;

	bootloader_can_write_data(1);
}

/*Helper function to handle BL_GO_TO_ADDR command */
void bootloader_handle_go_cmd(void) {
	TxHeader.StdId = BL_GO_TO_ADDR;

	uint8_t VERIFICATION_ADDRESS = (uint8_t) ADDR_INVALID;

	uint32_t *ptr_address = &RxData;

	uint32_t address = *(ptr_address);

	VERIFICATION_ADDRESS = verify_address(address);

	if (VERIFICATION_ADDRESS == (uint8_t) ADDR_VALID) {
		TxData[0] = (uint8_t) VERIFICATION_ADDRESS;

		//tell node_mcu that address is fine
		bootloader_can_write_data(1);

		/*jump to "go" address.
		 we dont care what is being done there.
		 host must ensure that valid code is present over there
		 Its not the duty of bootloader. so just trust and jump */

		/* Not doing the below line will result in hardfault exception for ARM cortex M */
		//watch : https://www.youtube.com/watch?v=VX_12SjnNhY
		uint32_t go_address = address;

		go_address += 1; //make T bit =1

		void (*lets_jump)(void) = (void *)go_address;

		lets_jump();

	} else {
		TxData[0] = (uint8_t) VERIFICATION_ADDRESS;

		//tell host that address is invalid
		bootloader_can_write_data(1);
	}
}

/*Helper function to handle BL_FLASH_ERASE command */
void bootloader_handle_flash_erase_cmd(void) {
	uint8_t ERASE_STATUS = (uint8_t) FLASH_ERASE_FAILED;

	uint32_t *ptr = &RxData;
	uint32_t initial_sector = *(ptr);
	uint32_t number_of_sectors = *(ptr + 1);

	ERASE_STATUS = execute_flash_erase(initial_sector, number_of_sectors);

	TxHeader.StdId = BL_FLASH_ERASE;

	if (ERASE_STATUS == (uint8_t) FLASH_ERASE_SUCCESS) {
		TxData[0] = (uint8_t) FLASH_ERASE_SUCCESS;
	} else if (ERASE_STATUS == (uint8_t) FLASH_ERASE_FAILED) {
		TxData[0] = (uint8_t) FLASH_ERASE_FAILED;
	} else {
		TxData[0] = (uint8_t) INVALID_SECTOR;
	}

	//bootloader_can_write_data(1);
}

void bootloader_handle_mem_write_size_cmd(void) {
	uint16_t *ptr_size = &RxData;
	SIZE = *(ptr_size);
	TxHeader.StdId = BL_MEM_WRITE_SIZE;

	ack_no++;

	//bootloader_can_write_data(1);
}

void bootloader_handle_mem_write_address_cmd(void) {
	uint32_t *ptr_address = &RxData;

	ADDRESS = *(ptr_address);
}

/*Helper function to handle BL_MEM_WRITE_DATA command */
void bootloader_handle_mem_write_data_cmd() {
	uint8_t WRITE_STATUS = (uint8_t) FLASH_WRITE_FAILED;
	uint8_t VERIFICATION_ADDRESS = (uint8_t) ADDR_INVALID;
	TxHeader.StdId = BL_MEM_WRITE_DATA;
	uint32_t mem_address = ADDRESS;
	VERIFICATION_ADDRESS = verify_address(mem_address);
	if (VERIFICATION_ADDRESS == (uint8_t) ADDR_VALID) {
		//execute mem write
		WRITE_STATUS = execute_mem_write();
		if (WRITE_STATUS == (uint8_t) FLASH_WRITE_SUCCESS) {
			TxData[0] = (uint8_t) FLASH_WRITE_SUCCESS;
		} else {
			TxData[0] = (uint8_t) FLASH_WRITE_FAILED;
		}
	} else {
		TxData[0] = (uint8_t) ADDR_INVALID;
	}
	//inform host about the status
	//bootloader_can_write_data(1);
}

/* USER CODE BEGIN 4 */
/* This function read the data from can1 */
void bootloader_can_read_data() {
	switch (RxHeader.StdId) {
	case BL_GET_HELP:
		bootloader_handle_gethelp_cmd();
		break;
	case BL_GET_VER:
		bootloader_handle_getver_cmd();
		break;
	case BL_FLASH_ERASE:
		bootloader_handle_flash_erase_cmd();
		break;
	case BL_MEM_WRITE_SIZE:
		bootloader_handle_mem_write_size_cmd();
		break;
	case BL_MEM_WRITE_ADDRESS:
		bootloader_handle_mem_write_address_cmd();
		break;
	case BL_MEM_WRITE_DATA:
		bootloader_handle_mem_write_data_cmd();
		break;
	case BL_GO_TO_ADDR:
		bootloader_handle_go_cmd();
		break;
	case FIRMWARE_OVER_THE_AIR:
		UpdateAPP();
		break;
	case GOTO_BL:
		bootloader_handle_gotobl();
		break;
	}
}

/* This function write data into can1 */
void bootloader_can_write_data(uint32_t length_of_data) {
	for (uint32_t i = 0; i < length_of_data; i++) {
		HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox);
	}

	memset(TxData, 0, 8);
}

/*Just returns the macro value */
uint8_t get_app_version(void) {
	return (uint8_t) APP_VERSION;
}

//verify the address sent by the host .
uint8_t verify_address(uint32_t go_address) {
	/*so the valid addresses which we can jump to are
	 external memory & SRAM & flash memory */

	if (go_address >= SRAM1_BASE && go_address <= SRAM1_END) {
		return (uint8_t) ADDR_VALID;
	} else if (go_address >= SRAM2_BASE && go_address <= SRAM2_END) {
		return (uint8_t) ADDR_VALID;
	} else if (go_address >= SRAM3_BASE && go_address <= SRAM3_END) {
		return (uint8_t) ADDR_VALID;
	} else if (go_address >= FLASH_BASE && go_address <= FLASH_END) {
		return (uint8_t) ADDR_VALID;
	} else if (go_address >= BKPSRAM_BASE && go_address <= BKPSRAM_END) {
		return (uint8_t) ADDR_VALID;
	} else
		return (uint8_t) ADDR_INVALID;
}

uint8_t execute_flash_erase(uint32_t initial_sector_number,
		uint32_t number_of_sector) {
	//we have totally 12 sectors in one bank .. sector[0 to 11]
	//number_of_sector has to be in the range of 0 to 11
	// if sector_number = 0xff , that means mass erase !

	FLASH_EraseInitTypeDef flashErase_handle;
	uint32_t sectorError = 0;
	uint8_t erase_status = 0x01;

	if (number_of_sector > 23)
		return (uint8_t) INVALID_SECTOR;

	if ((initial_sector_number == 0xFFFFFFFF) || (number_of_sector <= 23)) {
		if (number_of_sector == (uint32_t) 0xFFFFFFFF) {
			flashErase_handle.TypeErase = FLASH_TYPEERASE_MASSERASE;
			flashErase_handle.Banks = FLASH_BANK_1;
		} else {
			/*Here we are just calculating how many sectors needs to erased */
			uint32_t remanining_sector = 24 - number_of_sector;
			if (number_of_sector > remanining_sector) {
				number_of_sector = remanining_sector;
			}
			flashErase_handle.TypeErase = FLASH_TYPEERASE_SECTORS;
			flashErase_handle.Sector = initial_sector_number; // this is the initial sector
			flashErase_handle.NbSectors = number_of_sector;
		}

		/*Get access to touch the flash registers */
		HAL_FLASH_Unlock();
		flashErase_handle.VoltageRange = FLASH_VOLTAGE_RANGE_3; // our MCU will work on this voltage range
		erase_status = (uint8_t) HAL_FLASHEx_Erase(&flashErase_handle,
				&sectorError);
		HAL_FLASH_Lock();

		return (uint8_t) erase_status;
	}

	return (uint8_t) INVALID_SECTOR;
}

/*This function writes the contents of pBuffer to  "mem_address" byte by byte */
//Note1 : Currently this function supports writing to Flash only .
//Note2 : This functions does not check whether "mem_address" is a valid address of the flash range.
uint8_t execute_mem_write() {
	HAL_StatusTypeDef write_status = HAL_ERROR;

	uint32_t *ptr_data = &RxData;

	//We have to unlock flash module to get control of registers
	HAL_FLASH_Unlock();

	//Here we program the flash byte by byte
	write_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, ADDRESS,
			ptr_data[0]);

	write_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, ADDRESS + 4,
			ptr_data[1]);

	ADDRESS = ADDRESS + 8;

	HAL_FLASH_Lock();

	return (uint8_t) write_status;
}

uint8_t UpdateAPP() {
	uint8_t erase_status = HAL_ERROR;
	HAL_StatusTypeDef write_status = HAL_ERROR;
	ack_no++;

	if (first_time) {
		ADDRESS = (uint32_t) 0x08110000;

		if (get_Active_Bank_no() == 0) {
			erase_status = execute_flash_erase(16, 3);
		} else {
			erase_status = execute_flash_erase(4, 3);
		}
		first_time = 0;
	} else {
		erase_status = FLASH_ERASE_SUCCESS;
	}

	if (erase_status == FLASH_ERASE_SUCCESS) {
		SIZE -= 1;
		write_status = execute_mem_write();
		if (SIZE == 0) {
			if (ack_no == 2) {
				ack_no = 0;
				bootloader_can_write_data(1);
			}
			toggleBankAndReset();
		}
	} else {
		TxHeader.StdId = FIRMWARE_OVER_THE_AIR;
		TxData[0] = erase_status;
		bootloader_can_write_data(1);
		return erase_status;
	}
	TxHeader.StdId = FIRMWARE_OVER_THE_AIR;
	TxData[0] = write_status;
	if (ack_no == 2) {
		ack_no = 0;
		bootloader_can_write_data(1);
	}

	return write_status;
}
