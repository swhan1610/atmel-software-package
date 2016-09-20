/* ---------------------------------------------------------------------------- */
/*                  Atmel Microcontroller Software Support                      */
/*                       SAM Software Package License                           */
/* ---------------------------------------------------------------------------- */
/* Copyright (c) 2015, Atmel Corporation                                        */
/*                                                                              */
/* All rights reserved.                                                         */
/*                                                                              */
/* Redistribution and use in source and binary forms, with or without           */
/* modification, are permitted provided that the following condition is met:    */
/*                                                                              */
/* - Redistributions of source code must retain the above copyright notice,     */
/* this list of conditions and the disclaimer below.                            */
/*                                                                              */
/* Atmel's name may not be used to endorse or promote products derived from     */
/* this software without specific prior written permission.                     */
/*                                                                              */
/* DISCLAIMER:  THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR   */
/* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE   */
/* DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,      */
/* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT */
/* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,  */
/* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    */
/* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING         */
/* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, */
/* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                           */
/* ---------------------------------------------------------------------------- */

/** \cond usb_massstorage
 * \page usb_massstorage USB Device Mass Storage Example
 *
 * \section Purpose
 *
 * The USB Mass storage Example will help you to get familiar with the
 * USB Device Port(UDP) on SAMV7/E7 Microcontrollers. Also
 * it can help you to be familiar with the USB Framework that is used for
 * rapid development of USB-compliant class drivers such as USB Mass
 * Storage class (MSD).
 *
 * \section Requirements
 *
 * This package is compatible with the evaluation boards listed below:
 * - SAMA5D2-VB
 * - SAMA5D2-XULT
 *
 * \section Description
 *
 * The demo simulates a SD/MMC USB disk.
 *
 * When the board running this program connected to a host (PC for example), with
 * USB cable, the board appears as a USB Disk for the host. Then the host can
 * format/read/write on the disk.
 *
 * \section Usage
 *
 *  -# Build the program and download it inside the board.
 *     Please refer to the Getting Started with SAM V71/E70 Microcontrollers.pdf
 * -# On the computer, open and configure a terminal application
 *    (e.g. HyperTerminal on Microsoft Windows) with these settings:
 *   - 115200 baud rate
 *   - 8 bits of data
 *   - No parity
 *   - 1 stop bit
 *   - No flow control
 * -# Start the application.
 * -# In the terminal window, the following text should appear:
 *     \code
 *     -- USB Device Mass Storage Example xxx --
 *     -- SAMxxxxx-xx
 *     -- Compiled: xxx xx xxxx xx:xx:xx --
 *     \endcode
 * -# When connecting USB cable to windows, the host
 *    reports a new USB %device attachment and Disk installation.
 *  . Then new "USB Mass Storage Device" and
 *    "ATMEL Mass Storage MSD USB Device" and "Generic volume" appear in
 *    hardware %device list.
 * -# You can find the new disk on host, and to create/write file to it.
 *
 * \section References
 * - usb_mass_storage/main.c
 * - memories: Storage Media interface for MSD
 * - usb: USB Framework, USB MSD driver and UDP interface driver
 *    - \ref usbd_framework
 *       - \ref usbd_api
 *    - \ref usbd_msd
 *       - \ref usbd_msd_drv
 */

/**
 * \file
 *
 * This file contains all the specific code for the
 * usb_massstorage.
 */

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "board.h"
#include "trace.h"
#include "misc/console.h"
#include "misc/cache.h"
#include "peripherals/pmc.h"
#include "peripherals/aic.h"

#ifdef CONFIG_HAVE_SDMMC
#  include "peripherals/sdmmc.h"
#elif defined(CONFIG_HAVE_HSMCI)
#  include "peripherals/hsmci.h"
#  include "peripherals/hsmcid.h"
#else
#  error No peripheral for SD/MMC devices
#endif

#include "libsdmmc/libsdmmc.h"

#include "libstoragemedia/media.h"
#include "libstoragemedia/media_private.h"
#include "libstoragemedia/media_ramdisk.h"
#include "libstoragemedia/media_sdcard.h"

#include "usb/device/msd/msd_driver.h"
#include "usb/device/msd/msd_lun.h"
#include "../usb_common/main_usb_common.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/*----------------------------------------------------------------------------
 *        Local definitions
 *----------------------------------------------------------------------------*/

/* Max number of DMA descriptors per slot, refer to sdmmc_initialize(). */
#define DMADL_CNT_MAX                 512u

/* Allocate 2 Timers/Counters, that are not used already by the libraries and
 * drivers this example depends on. */
#define TIMER0_MODULE                 ID_TC0
#define TIMER0_CHANNEL                0
#define TIMER1_MODULE                 ID_TC0
#define TIMER1_CHANNEL                1u

/** Total number of SDMMC interface */
#define BOARD_NUM_SDMMC           (2)

/** Maximum number of LUNs which can be defined. */
#define MAX_LUNS            (BOARD_NUM_SDMMC+1)

/** Size of one block in bytes. */
#define BLOCK_SIZE 512

/* Media index for different disks */
#define DRV_RAMDISK         0    /**< RAM disk */
#define DRV_EMMC_NAND       1    /**< eMMC Nand flash */
#define DRV_SDMMC_CARD      2    /**< SD card */


/** RamDisk size (WinXP can not format the disk if lower than 20K) */
#define RAMDISK_SIZE (8 * 1024 * 1024)

/** Size of the MSD IO buffer in bytes (more the better). */
#define MSD_BUFFER_SIZE (128 * BLOCK_SIZE)

/*----------------------------------------------------------------------------
 *        Global variables
 *----------------------------------------------------------------------------*/

/** MSD Driver Descriptors List */
extern const USBDDriverDescriptors msd_driver_descriptors;

/** Available media. */
struct _media medias[MAX_LUNS];

/*----------------------------------------------------------------------------
 *        Local variables
 *----------------------------------------------------------------------------*/

SECTION(".region_ddr")
ALIGNED(BLOCK_SIZE)
static uint8_t ramdisk_reserved[RAMDISK_SIZE];

#ifdef CONFIG_HAVE_SDMMC
#  define HOST0_ID                    ID_SDMMC0
#  define HOST1_ID                    ID_SDMMC1
/* Driver instance data (a.k.a. SDCard driver instance) */
static struct sdmmc_set sd_drv[BOARD_NUM_SDMMC];

#elif defined(CONFIG_HAVE_HSMCI)
#  define HOST0_ID                    ID_HSMCI0
#  define HOST1_ID                    ID_HSMCI1

/* MCI driver instance data (a.k.a. SDCard driver instance) */
static struct hsmci_set sd_drv[BOARD_NUM_SDMMC];
#endif

/* Library instance data (a.k.a. SDCard library instance) */
CACHE_ALIGNED_DDR static sSdCard sd_lib0;
CACHE_ALIGNED_DDR static sSdCard sd_lib1;
static sSdCard * sd_lib[BOARD_NUM_SDMMC] = { &sd_lib0, &sd_lib1 };

/** Device LUNs. */
static MSDLun luns[MAX_LUNS];

/** LUN read/write buffer. */
CACHE_ALIGNED_DDR static uint8_t ram_buffer[MSD_BUFFER_SIZE];

/** LUN read/write buffer. */
CACHE_ALIGNED_DDR static uint8_t sd_buffer0[MSD_BUFFER_SIZE];
CACHE_ALIGNED_DDR static uint8_t sd_buffer1[MSD_BUFFER_SIZE];
static uint8_t * sd_buffer[BOARD_NUM_SDMMC] = { sd_buffer0, sd_buffer1 };

/* Buffers dedicated to the SDMMC Driver, refer to sdmmc_initialize(). Aligning
 * them on cache lines is optional. */
#ifdef CONFIG_HAVE_SDMMC
CACHE_ALIGNED_DDR static uint32_t sd_dma_table0[DMADL_CNT_MAX * SDMMC_DMADL_SIZE];
CACHE_ALIGNED_DDR static uint32_t sd_dma_table1[DMADL_CNT_MAX * SDMMC_DMADL_SIZE];
#endif

/** Total data write to disk */
static uint32_t msd_write_total = 0;

/** Delay TO event */
static uint8_t msd_refresh = 0;

static uint8_t current_lun_num = 0;
/*-----------------------------------------------------------------------------
 *         Callback re-implementation
 *-----------------------------------------------------------------------------*/
/**
 * Invoked when a new SETUP request is received from the host. Forwards the
 * request to the Mass Storage device driver handler function.
 * \param request  Pointer to a USBGenericRequest instance.
 */
void usbd_callbacks_request_received(const USBGenericRequest *request)
{
	msd_driver_request_handler(request);
}

/**
 * Invoked when the configuration of the device changes. Resets the mass
 * storage driver.
 * \param cfgnum New configuration number.
 */
void usbd_driver_callbacks_configuration_changed(unsigned char cfgnum)
{
	msd_driver_configuration_change_handler(cfgnum);
}

/*----------------------------------------------------------------------------
 *        Callbacks
 *----------------------------------------------------------------------------*/

/**
 * Invoked when the MSD finish a READ/WRITE.
 * \param flow_direction 1 - device to host (READ10)
 *                       0 - host to device (WRITE10)
 * \param data_length Length of data transferred in bytes.
 * \param fifo_null_count Times that FIFO is NULL to wait
 * \param fifo_full_count Times that FIFO is filled to wait
 */
static void msd_callbacks_data(uint8_t flow_direction, uint32_t data_length,
		uint32_t fifo_null_count, uint32_t fifo_full_count)
{
	if (!flow_direction) {
		msd_write_total += data_length;
	}
}

/*----------------------------------------------------------------------------
 *         Internal functions
 *----------------------------------------------------------------------------*/

/**
 * Initialize SD card peripherals driver
 */
static void sd_driver_configure(void)
{
#ifdef CONFIG_HAVE_SDMMC
	struct _pmc_audio_cfg audio_pll_cfg = {
		.fracr = 0,
		.div = 3,
		.qdaudio = 24,
	};
#endif
	uint8_t rc;

	pmc_enable_peripheral(TIMER0_MODULE);
#if TIMER1_MODULE != TIMER0_MODULE
	pmc_enable_peripheral(TIMER1_MODULE);
#endif

	/* The HSMCI peripherals are clocked by the Master Clock (at least on
	 * SAMA5D4x).
	 * The SDMMC peripherals are clocked by their Peripheral Clock, the
	 * Master Clock, and a Generated Clock (at least on SAMA5D2x). */
	pmc_enable_peripheral(HOST0_ID);
	pmc_enable_peripheral(HOST1_ID);

#ifdef CONFIG_HAVE_SDMMC
	/* The regular SAMA5D2-XULT board wires on the SDMMC0 slot an e.MMC
	 * device whose fastest timing mode is High Speed DDR mode @ 52 MHz.
	 * Target a device clock frequency of 52 MHz. Use the Audio PLL and set
	 * AUDIOCORECLK frequency to 12 * (51 + 1 + 0/2^22) = 624 MHz.
	 * And set AUDIOPLLCK frequency to 624 / (5 + 1) = 104 MHz. */
	audio_pll_cfg.nd = 51;
	audio_pll_cfg.qdpmc = 5;
	pmc_configure_audio(&audio_pll_cfg);
	pmc_enable_audio(true, false);
	pmc_configure_gck(HOST0_ID, PMC_PCR_GCKCSS_AUDIO_CLK, 1 - 1);
	pmc_enable_gck(HOST0_ID);

	/* The regular SAMA5D2-XULT board wires on the SDMMC1 slot a MMC/SD
	 * connector. SD cards are the most likely devices. Since the SDMMC1
	 * peripheral supports 3.3V signaling level only, target SD High Speed
	 * mode @ 50 MHz.
	 * The Audio PLL being optimized for SDMMC0, fall back on PLLA, since,
	 * as of writing, PLLACK/2 is configured to run at 498 MHz. */
	pmc_configure_gck(HOST1_ID, PMC_PCR_GCKCSS_PLLA_CLK, 1 - 1);
	pmc_enable_gck(HOST1_ID);
#endif

	rc = board_cfg_sdmmc(HOST0_ID) ? 1 : 0;
	rc &= board_cfg_sdmmc(HOST1_ID) ? 1 : 0;
	if (!rc)
		trace_error("Failed to cfg cells\n\r");

#ifdef CONFIG_HAVE_SDMMC
	sdmmc_initialize(&sd_drv[0], HOST0_ID, TIMER0_MODULE,
	    TIMER0_CHANNEL, sd_dma_table0, ARRAY_SIZE(sd_dma_table0), false);
	sdmmc_initialize(&sd_drv[1], HOST1_ID, TIMER1_MODULE,
	    TIMER1_CHANNEL, sd_dma_table1, ARRAY_SIZE(sd_dma_table1), false);
#elif defined(CONFIG_HAVE_HSMCI)
	hsmci_initialize(&sd_drv[0], HOST0_ID,
		TIMER0_MODULE, TIMER0_CHANNEL);
	Hsmci* mci = get_hsmci_addr_from_id(HOST0_ID);
	hsmci_set_slot(mci, BOARD_HSMCI0_SLOT);
	hsmci_initialize(&sd_drv[1], HOST1_ID,
		TIMER1_MODULE, TIMER1_CHANNEL);
#endif

	/* As of writing, libsdmmc ignores the slot number */
	SDD_InitializeSdmmcMode(&sd_lib0, &sd_drv[0], 0);
	SDD_InitializeSdmmcMode(&sd_lib1, &sd_drv[1], 0);
}

/**
 * Initialize RAM to assign RamDisk block
 */
static void ramdisk_init(void)
{
	trace_info("RamDisk @ %x, size %d\n\r", (unsigned)&ramdisk_reserved, RAMDISK_SIZE);

	media_ramdisk_init(&(medias[DRV_RAMDISK]),
			(uint32_t)ramdisk_reserved / BLOCK_SIZE,
			RAMDISK_SIZE / BLOCK_SIZE,
			BLOCK_SIZE);

	lun_init(&(luns[DRV_RAMDISK]),
			&(medias[DRV_RAMDISK]),
			ram_buffer, MSD_BUFFER_SIZE,
			0, 0, 0, 0,
			msd_callbacks_data);

	current_lun_num++;
}

/**
 * Run init on the inserted card
 * \param num Controller number.
 */
static bool card_init(uint8_t num)
{
	sSdCard *pSd = sd_lib[num];
	uint8_t rc;

	trace_info("\n\r==========================================\n\r");

	rc = SD_Init(pSd);
	if (rc != SDMMC_OK) {
		trace_error("SD/MMC device initialization failed: %d\n\r", rc);
		return false;
	}
	SD_DumpStatus(pSd);
	return true;
}

/**
 * Initialize SD card to assign SdDisk bolok
 */
static void sddisk_init(void)
{
	uint8_t rc, i, sd_connected[BOARD_NUM_SDMMC];
	sSdCard *pSd;
	/* Infinite loop */
	for (i = 0; i < BOARD_NUM_SDMMC; i ++) sd_connected[i]=0;
	for (i = 0; i < BOARD_NUM_SDMMC; i ++) {

	if (SD_GetStatus(sd_lib[i]) != SDMMC_NOT_SUPPORTED) {
		if (sd_connected[i] == 0) {
				sd_connected[i] = 1;
				trace_info("Connecting to slot %x \n\r",i);
				rc = card_init(i);
				if(rc) {
					pSd = sd_lib[i];
					media_sdusb_initialize(&medias[current_lun_num], pSd);
					lun_init(&(luns[current_lun_num]), &(medias[current_lun_num]),
								sd_buffer[i], MSD_BUFFER_SIZE, 0, 0, 0, 0,
								msd_callbacks_data);
					current_lun_num++;
				}
			}
		} else {
				sd_connected[i] = 0;
				trace_info("** Card %d Disconnected\n\r",i);
		}
	}

}

/**
 * Initialize MSD Media & LUNs
 */
static void _MemoriesInitialize(void)
{
	int i;

	/* Reset all LUNs */
	for (i = 0; i < MAX_LUNS; i++)
		lun_init(&luns[i], 0, 0, 0, 0, 0, 0, 0, 0);

	/* Initialize RAM disk */
	ramdisk_init();

	/*Initialize SD Card  */
	sddisk_init();
}

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/

/**
 * \brief usb_massstorage Application entry point.
 *
 * \return Unused (ANSI-C compatibility).
 */
int main(void)
{
	console_example_info("USB Device Mass Storage Example");

	/* Initialize all USB power (off) */
	usb_power_configure();

	/* Initialize SD card peripherals driver */
	sd_driver_configure();

	/* Initialize Media */
	_MemoriesInitialize();

	/* BOT driver initialization */
	msd_driver_initialize(&msd_driver_descriptors, luns, current_lun_num);

	/* connect if needed */
	usb_vbus_configure();

	while (1) {
		/* Mass storage state machine */
		if (usbd_get_state() >= USBD_STATE_CONFIGURED) {
			msd_driver_state_machine();
			if (msd_refresh) {
				msd_refresh = 0;
				if (msd_write_total < 50 * 1000) {
					/* Flush Disk Media */
				}
				msd_write_total = 0;
			}
		}
	}
}
/** \endcond */
