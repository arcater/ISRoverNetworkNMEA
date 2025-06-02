# NTRIP Rover with NMEA output over network port

This is a modified version of the [ISNtripRoverExample](https://github.com/inertialsense/inertial-sense-sdk/tree/1.11.2/ExampleProjects/NTRIP_rover) project (tag 1.11.2) provided by Inertial Sense. In addition to supplying RTK corrections to the sensor, [IMU](https://github.com/inertialsense/docs.inertialsense.com/blob/1.11.0/docs/user-manual/com-protocol/nmea.md#pimu) and [GGA](https://github.com/inertialsense/docs.inertialsense.com/blob/1.11.0/docs/user-manual/com-protocol/nmea.md#gga) NMEA messages are output to a specified network port using UDP packets. These NMEA messages can then be parsed by another program.

## Requirements

OS: Linux

Inertial Sense SDK tag 1.11.2

Inertial Sense firmware compatible with the 1.11.2 SDK

### Tested using

Ubuntu 22.04 LTS
Nvidia Jetson AGX Orin (ARM64)
Inertial Sense uINS

## Example Output

UDP outputs captured using:

   ``` bash
   netcat -ul 25565
   ```
   
Sample output messages:  
	```
	$GNGGA,165911.400,4439.69585,N,07500.06681,W,1,21,1.24,150.90,M,-33.80,M,,*45  
	$PIMU,2201.647,0.0259,0.0450,-0.0063,0.627,-0.114,-9.362*29
	```
	
(Hello from Clarkson University)

## Suggested setup instructions

(Assuming the Inertial Sense SDK has already been setup)  

Navigate to 'ExampleProjects' directory of the SDK and run:  
   ``` bash
   git clone https://github.com/arcater/ISRoverNetworkNMEA.git
   mkdir ISRoverNetworkNMEA/build
   cd ISRoverNetworkNMEA/build
   cmake ..
   make
   ```
   
To run you must specify the serial port for the sensor and NTRIP connection info (same format as [ISNtripRoverExample](https://github.com/inertialsense/inertial-sense-sdk/tree/1.11.2/ExampleProjects/NTRIP_rover)). Output port for NMEA messages is optional (Defaults to 25565 if no value given):  
   ``` bash
   ./ISRoverNetworkNMEA /dev/ttyACM0 TCP:RTCM3:192.168.1.100:7777:mountpoint:username:password (optional)outputport
   ```
