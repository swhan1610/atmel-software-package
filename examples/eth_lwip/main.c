/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2013, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/**
 *  \page eth_lwip ETH lwIP Example
 *
 *  \section Purpose
 *
 *  This project implements webserver example by using lwIP stack, It enables
 *  the device to act as a web server, sending a very short page when accessed
 *  through a browser.
 *
 *  \section Requirements
 *
 * - On-board ethernet interface.
 *
 *  \section Description
 *
 *  Please refer to the lwIP documentation for more information about
 *  the TCP/IP stack and the webserver example.
 *
 *  By default, the example does not use DHCP. If you want to use DHCP,
 *  please open file lwipopts.h and define "LWIP_DHCP" and "LWIP_UDP" to 1.
 *
 *  \section Usage
 *
 *  -# Build the program and download it inside the evaluation board. Please
 *     refer to the
 *     <a href="http://www.atmel.com/dyn/resources/prod_documents/6421B.pdf">
 *     SAM-BA User Guide</a>, the
 *     <a href="http://www.atmel.com/dyn/resources/prod_documents/doc6310.pdf">
 *     GNU-Based Software Development</a>
 *     application note or to the
 *     <a href="ftp://ftp.iar.se/WWWfiles/arm/Guides/EWARM_UserGuide.ENU.pdf">
 *     IAR EWARM User Guide</a>,
 *     depending on your chosen solution.
 *  -# On the computer, open and configure a terminal application
 *     (e.g. HyperTerminal on Microsoft Windows) with these settings:
 *    - 115200 bauds
 *    - 8 bits of data
 *    - No parity
 *    - 1 stop bit
 *    - No flow control
 *  -# Connect an Ethernet cable between the evaluation board and the network.
 *      The board may be connected directly to a computer; in this case,
 *      make sure to use a cross/twisted wired cable such as the one provided
 *      with SAMA5D4-EK / SAMA5D4-XULT.
 *  -# Start the application. It will display the following message on the terminal:
 *    \code
 *    -- ETH lwIP Example xxx --
 *    -- xxxxxx-xx
 *    -- Compiled: xxx xx xxxx xx:xx:xx --
 *      MAC 3a:1f:34:08:54:54
 *    - Host IP  192.168.1.3
 *    - Gateway IP 192.168.1.2
 *    - Net Mask 255.255.255.0
 *    \endcode
 * -# Type the IP address (Host IP in the debug log) of the device in a web
 *    browser, like this:
 *    \code
 *    http://192.168.1.3
 *    \endcode
 *    The page generated by lwIP will appear in the web browser, like below:
 *    \code
 *    Small test page.#
 *    \endcode
 *
 *  \note
 *  Make sure the IP adress of the device( the board) and the computer are in the same network.
 */

/** \file
 *
 *  This file contains all the specific code for the eth_lwip example.
 *
 */

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/


#include "board.h"
#include "board_eth.h"

#include "network/ethd.h"

#include "serial/console.h"

#include "liblwip.h"
#include "httpd.h"

#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 *         Variables
 *---------------------------------------------------------------------------*/

/* The MAC address used for demo */
static uint8_t _mac_addr[6];

/* The IP address used for demo (ping ...) */
static uint8_t _ip_addr[4] = {192, 168, 1, 3};

/* Set the default router's IP address. */
static const uint8_t _gw_ip_addr[4] = {192, 168, 1, 2};

/* The NetMask address */
static const uint8_t _netmask[4] = {255, 255, 255, 0};

/*----------------------------------------------------------------------------
 *        Local functions
 *----------------------------------------------------------------------------*/

/**
 * Input the eth number to use
 */
static uint8_t select_eth_port(void)
{
	uint8_t key, send_port = 0;

	if (ETH_IFACE_COUNT < 2)
		return send_port;

	while (1) {
		printf("\n\r");
		printf("Input an eth number '0' or '1' to initialize:\n\r");
		printf("=>");
		key = console_get_char();
		printf("%c\r\n", key);

		if (key == '0') {
			send_port = 0;
			break;
		} else if (key == '1') {
			send_port = 1;
			break;
		}
	}

	return send_port;
}

/*----------------------------------------------------------------------------
 *        Exported functions
 *----------------------------------------------------------------------------*/

/**
 *  \brief gmac_lwip example entry point.
 *
 *  \return Unused (ANSI-C compatibility).
 */
int main(void)
{
	ip_addr_t ipaddr, netmask, gw;
	struct netif NetIf, *netif;
	uint8_t eth_port = 0;

#if LWIP_DHCP
	u8_t dhcp_state = DHCP_INIT;
#endif

	/* Output example information */
	console_example_info("ETH lwIP Example");

	/* User select the port number for multiple eth */
	eth_port = select_eth_port();
	ethd_get_mac_addr(board_get_eth(eth_port), 0, _mac_addr);

	/* Display MAC & IP settings */
	printf(" - MAC%d %02x:%02x:%02x:%02x:%02x:%02x\n\r", eth_port,
	       _mac_addr[0], _mac_addr[1], _mac_addr[2],
	       _mac_addr[3], _mac_addr[4], _mac_addr[5]);

#if !LWIP_DHCP
	printf(" - Host IP  %d.%d.%d.%d\n\r", _ip_addr[0], _ip_addr[1], _ip_addr[2], _ip_addr[3]);
	printf(" - GateWay IP  %d.%d.%d.%d\n\r", _gw_ip_addr[0], _gw_ip_addr[1], _gw_ip_addr[2], _gw_ip_addr[3]);
	printf(" - Net Mask  %d.%d.%d.%d\n\r", _netmask[0], _netmask[1], _netmask[2], _netmask[3]);
#else
	printf(" - DHCP Enabled\n\r");
#endif

	/* Initialize lwIP modules */
	lwip_init();

#if !LWIP_DHCP
	IP4_ADDR(&gw, _gw_ip_addr[0], _gw_ip_addr[1], _gw_ip_addr[2], _gw_ip_addr[3]);
	IP4_ADDR(&ipaddr, _ip_addr[0], _ip_addr[1], _ip_addr[2], _ip_addr[3]);
	IP4_ADDR(&netmask, _netmask[0], _netmask[1], _netmask[2], _netmask[3]);
#else
	IP4_ADDR(&gw, 0, 0, 0, 0);
	IP4_ADDR(&ipaddr, 0, 0, 0, 0);
	IP4_ADDR(&netmask, 0, 0, 0, 0);
#endif

	netif = netif_add(&NetIf, &ipaddr, &netmask, &gw, NULL, ethif_init, ip_input);
	netif_set_default(netif);
	netif_set_up(netif);

	/* Initialize http server application */
	if (ERR_OK != httpd_init()) {
		printf("httpd_init ERR_OK!");
		return -1;
	}
	printf ("Type the IP address of the device in a web browser, http://192.168.1.3 \n\r");
	while(1) {
		/* Run polling tasks */
		ethif_poll(netif);
	}
}
