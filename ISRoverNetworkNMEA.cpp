/*
MIT LICENSE

Copyright 2014-2018 Inertial Sense, Inc. - http://inertialsense.com

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <string>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h> 

// STEP 1: Add Includes
// Change these include paths to the correct paths for your project
#include "../../src/ISComm.h"
#include "../../src/serialPortPlatform.h"
#include "../../src/ISStream.h"
#include "../../src/ISClient.h"
#include "../../src/ISUtilities.h"
#include "../../src/protocol_nmea.h"

using namespace std;

static cISStream *s_clientStream;

static int socket_fd;
static struct sockaddr_in server_address;


int stop_message_broadcasting(serial_port_t *serialPort, is_comm_instance_t *comm)
{
	// Stop all broadcasts on the device
	int n = is_comm_stop_broadcasts_all_ports(comm);
	if (n != serialPortWrite(serialPort, comm->buf.start, n))
	{
		printf("Failed to encode and write stop broadcasts message\r\n");
		return -3;
	}
	return 0;
}


int enable_message_broadcasting(serial_port_t *serialPort, is_comm_instance_t *comm)
{
	int n = is_comm_get_data(comm, _DID_GPS1_POS, 0, 0, 1);
	if (n != serialPortWrite(serialPort, comm->buf.start, n))
	{
		printf("Failed to encode and write get GPS message\r\n");
		return -5;
	}
	n = is_comm_get_data(comm, DID_GPS1_RTK_POS_REL, 0, 0, 1);
	if (n != serialPortWrite(serialPort, comm->buf.start, n))
	{
		printf("Failed to encode and write get GPS message\r\n");
		return -5;
	}
	n = is_comm_get_data(comm, DID_IMU, 0, 0, 1); // enable IMU messages
	if (n != serialPortWrite(serialPort, comm->buf.start, n))
	{
		printf("Failed to encode and write get IMU message\r\n");
		return -5;
	}
	return 0;
}


static struct
{
	gps_pos_t		gps;
	gps_rtk_rel_t		rel;
	uint8_t			baseCount;
} s_rx = {};

static imu_t my_imu;

void handle_uINS_data(is_comm_instance_t *comm, cISStream *clientStream)
{
	switch (comm->dataHdr.id)
	{
	
	case DID_IMU: { // handle IMU data from uINS
		is_comm_copy_to_struct(&my_imu, comm, sizeof(my_imu));
		char mybuf2[512];
		int mnm = nmea_pimu(mybuf2, sizeof(mybuf2), my_imu, "$PIMU");
		sendto(socket_fd, mybuf2, mnm, MSG_CONFIRM, (struct sockaddr *) &server_address, sizeof(server_address));
		break;
	}
	
	case DID_GPS1_RTK_POS_REL:
		is_comm_copy_to_struct(&s_rx.rel, comm, sizeof(s_rx.rel));		
		break;

	case DID_GPS1_POS:		
		is_comm_copy_to_struct(&s_rx.gps, comm, sizeof(s_rx.gps));
		string fix;
		switch (s_rx.gps.status&GPS_STATUS_FIX_MASK)
		{
		default:						fix = "None      ";		break;
		case GPS_STATUS_FIX_3D:			fix = "3D        ";		break;
		case GPS_STATUS_FIX_RTK_SINGLE:	fix = "RTK-Single";		break;
		case GPS_STATUS_FIX_RTK_FLOAT:	fix = "RTK-Float ";		break;
		case GPS_STATUS_FIX_RTK_FIX:	fix = "RTK       ";		break;
		}

		// output GGA NMEA message to specifed network port
		char mybuf[512];
		int mn = nmea_gga(mybuf, sizeof(mybuf), s_rx.gps);
		sendto(socket_fd, mybuf, mn, MSG_CONFIRM, (struct sockaddr *) &server_address, sizeof(server_address));

		// Forward our position via GGA every 5 seconds to the RTK base.
		static time_t lastTime;
		time_t currentTime = time(NULLPTR);
		if (abs(currentTime - lastTime) > 5)
		{	// Update every 5 seconds
			lastTime = currentTime;
			if ((s_rx.gps.status&GPS_STATUS_FIX_MASK) >= GPS_STATUS_FIX_3D)
			{	// GPS position is valid
				char buf[512];
				int n = nmea_gga(buf, sizeof(buf), s_rx.gps);
				clientStream->Write(buf, n);
				printf("Sending position to Base: \n%s\n", string(buf,n).c_str());
			}
			else
			{
				printf("Waiting for fix...\n");
			}
		}
		break;
	}
}


void read_uINS_data(serial_port_t* serialPort, is_comm_instance_t *comm, cISStream *clientStream)
{
	protocol_type_t ptype;

	// Get available size of comm buffer
	int n = is_comm_free(comm);

	// Read data directly into comm buffer
	if ((n = serialPortRead(serialPort, comm->buf.tail, n)))
	{
		// Update comm buffer tail pointer
		comm->buf.tail += n;

		// Search comm buffer for valid packets
		while ((ptype = is_comm_parse(comm)) != _PTYPE_NONE)
		{
			if (ptype == _PTYPE_INERTIAL_SENSE_DATA)
			{
				handle_uINS_data(comm, clientStream);
			}
		}
	}
}


void read_RTK_base_data(serial_port_t* serialPort, is_comm_instance_t *comm, cISStream *clientStream)
{
	protocol_type_t ptype;

	// Get available size of comm buffer
	int n = is_comm_free(comm);

	// Read data from RTK Base station
	if ((n = clientStream->Read(comm->buf.tail, n)))
	{
		// Update comm buffer tail pointer
		comm->buf.tail += n;

		// Search comm buffer for valid packets
		while ((ptype = is_comm_parse(comm)) != _PTYPE_NONE)
		{
			if (ptype == _PTYPE_RTCM3)
			{	// Forward RTCM3 packets to uINS
				serialPortWrite(serialPort, comm->dataPtr, comm->dataHdr.size);
				s_rx.baseCount++;
			}
		}
	}
}


int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		printf("Please pass the com port and the RTK base connection string as the 1st and 2nd arguments.\r\n");
		printf("Optional 3rd argument to specify network port number for NMEA message output (Default 25565)\r\n");
		printf("Command Arguments\": COM3 TCP:RTCM3:192.168.1.100:7777:mountpoint:username:password outputport\r\n");
		return -1;
	}

	// STEP 2: Init comm instance
	is_comm_instance_t comm;
	uint8_t buffer[2048];

	// Initialize the comm instance, sets up state tracking, packet parsing, etc.
	is_comm_init(&comm, buffer, sizeof(buffer));

	// STEP 3: Initialize and open serial port
	serial_port_t serialPort;

	// Initialize the serial port (Windows, MAC or Linux) - if using an embedded system like Arduino,
	//  you will need to handle the serial port creation, open and reads yourself. In this
	//  case, you do not need to include serialPort.h/.c and serialPortPlatform.h/.c in your project.
	serialPortPlatformInit(&serialPort);

	// Open serial, last parameter is a 1 which means a blocking read, you can set as 0 for non-blocking
	// you can change the baudrate to a supported baud rate (IS_BAUDRATE_*), make sure to reboot the uINS
	//  if you are changing baud rates, you only need to do this when you are changing baud rates.
	if (!serialPortOpen(&serialPort, argv[1], IS_BAUDRATE_921600, 0))
	{
		printf("Failed to open serial port on com port %s\r\n", argv[1]);
		return -2;
	}

	// STEP 4: Connect to the RTK base (sever)
	// Connection string follows the following format:
	// [type]:[IP or URL]:[port]:[mountpoint]:[username]:[password]
	// i.e. TCP:RTCM3:192.168.1.100:7777:mount:user:password
	if ((s_clientStream = cISClient::OpenConnectionToServer(argv[2])) == NULLPTR)
	{
		printf("Failed to open RTK base connection %s\r\n", argv[2]);
		return -2;
	}

	int error;

	// STEP 5: Stop any message broadcasting
	if ((error = stop_message_broadcasting(&serialPort, &comm)))
	{
		return error;
	}

	// STEP 6: Enable message broadcasting
	if ((error = enable_message_broadcasting(&serialPort, &comm)))
	{
		return error;
	}
	
	// get and verify specified NMEA port from command line arguments if provided
	int port_number = 25565;
	if (argc > 3) {
		port_number = atoi(argv[3]);
		if (port_number < 1024 || port_number > 49151) {
			printf("Invalid output port for NMEA messages\nMust be between 1024-49151\n");
			return -1;
		}
	}
		
	// create socket file descriptor
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd < 0) {
		printf("Failed to create socket to broadcast NMEA messages\r\n");
		return -1;
	}
	
	// fill socket address struct
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(port_number);


	// STEP 8: Handle received data

	// Main loop
	while (1)
	{
		read_uINS_data(&serialPort, &comm, s_clientStream);

		read_RTK_base_data(&serialPort, &comm, s_clientStream);

		SLEEP_MS(1);	// sleep for 1ms, serial port reads are non-blocking
	}
}

