#include <stdio.h>
#include <stdlib.h>
#include "ftd2xx.h"
#include <math.h>

#define BUF_SIZE 40000		

//////////////
// COMMANDS //
//////////////

// general
#define ERROR			0x00

// Commands for uC sent via USB
//
// for CPLD
#define SET_PW			0x02
#define SET_DELAY		0x04
#define SET_NUM_SAMPLES 0x06
#define SET_MODE 		0x08
#define START_MSRMNT 	0x1E
// for DS1621 thermostat
#define SET_CASE_TEMP	0x07
#define GET_CASE_TEMP	0x17
// for uC itself
#define SET_RESET_COUNT 0x09
#define GET_RESET_COUNT 0x19
// not used
#define SET_RANGE_INCR	0x05

// CPLD modes
#define CROSSPOL		0x04
#define COPOL			0x02
#define CALIBRATE		0x06
#define	RADIOMETER		0x08


///////////////
// FUNCTIONS //
///////////////

// Open FTDI USB device
int open_device(FT_HANDLE *handle)
{
	FT_STATUS ftStatus;

	//printf("Open FTDI USB device\n\n");
	ftStatus = FT_Open(0, handle);
	if(ftStatus != FT_OK) {
		// Reconnect the device
		//ftStatus = FT_CyclePort(ftHandle);
		if(ftStatus != FT_OK) {
			printf("FT_Cycle port failed \n");
			return 1;
			}
		ftStatus = FT_Open(0, handle);
		if(ftStatus != FT_OK) {
			printf("FT_Open failed twice\n");
			return 1;
			}
		printf("FT_Open failed once. Reconnected.\n");
	}
	return 0;
}

// Helper function to get single bytes of a int variable.
// LSB is byte[0].
int int_to_bytes(unsigned int n, char *byte)
{
	byte[0] =  n & 0xFF;
	byte[1] = (n & 0xFF00) >> 8; 
	byte[2] = (n & 0xFF0000) >> 16; 
	byte[3] = (n & 0xFF000000) >> 24;
	return 0;
}

// Helper function to get int value of 4 byte char array.
// Only uses the LSB because the communication with the
// uC is limited to 8 bit words. The rest of the bytes of
// the char are meaningless
unsigned int bytes_to_int(char *chars)
{
	int i, value;
	unsigned int byte[4];
	for(i = 0; i<4; i++){
		byte[i] = chars[i] & 0xFF; // Truncate chars to LSB
	}
	value = (unsigned int) byte[0] 
		        + byte[1]*pow(2,8)
				+ byte[2]*pow(2,16)
				+ byte[3]*pow(2,24);
	return value;
}

// Write a byte via USB and check its transmission
// by reading back, hopefully the same byte
int write_byte(FT_HANDLE ftHandle, char byte)
{
	char * pcBufRead;
	char   cBufWrite[1];
	int write_buffer_size = 1;
	int read_buffer_size = 1;
	DWORD dwBytesRead, dwBytesWritten;

	pcBufRead = (char *)malloc(read_buffer_size);

	// Send byte
	cBufWrite[0] = byte;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	//printf("Written 0x%x \n", cBufWrite[0]);
	
	// Check what the uC returns, it should be the same byte
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Sent: %x Received: %x \n", cBufWrite[0], pcBufRead[0]);
	if (pcBufRead[0] != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
		printf("Sent: %x Received: %x \n", cBufWrite[0], pcBufRead[0]);
		// Purge buffers
		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
		free(pcBufRead);
		return 1;
	}
	
	free(pcBufRead);

	return 0;
}

// Read a byte via USB
char read_byte(FT_HANDLE ftHandle)
{
	char * pcBufRead;
	char value;
	int read_buffer_size = 1;
	DWORD dwBytesRead;

	pcBufRead = (char *)malloc(read_buffer_size);

	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	//printf("Received 0x%x \n", pcBufRead[0]);

	value = pcBufRead[0];

	free(pcBufRead);

	return value;
}

// Write a byte array of size 'size' and after complete write,
// read it back for transmission control
int write_bytes(FT_HANDLE ftHandle, char *bytes, int size)
{
	int i;
	char * pcBufRead;
	char   cBufWrite[size];
	int write_buffer_size = size;
	int read_buffer_size = size;
	DWORD dwBytesRead, dwBytesWritten;

	pcBufRead = (char *)malloc(read_buffer_size);

	// Truncate the bytes array to the desired size
	//printf("Written  ");
	for(i = 0; i < size; i++){
		cBufWrite[i] = bytes[i];
	//	printf("%x ", cBufWrite[i]);
	}
	//printf("\n");

	// Send bytes
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	
	// Check what the uC returns, it should be the same byte
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	//printf("Received ");
	for(i = 0; i < size; i++){
	//	printf("%x ", pcBufRead[i]);
		printf("Sent: %x Received: %x \n", cBufWrite[i], pcBufRead[i]);
		if (pcBufRead[i] != cBufWrite[i])
		{
			printf("USB transmission problem.\n");
			printf("Sent: %x Received: %x \n", cBufWrite[i], pcBufRead[i]);
			FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
			free(pcBufRead);
			return 1;
		}
	}
	//printf("\n");
	
	free(pcBufRead);

	return 0;
}


// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int n_samples)
{
	char num[4]; 		// hods the 4 bytes of  n_samples
	
	// The max number of samples allowed is 2^16 (2 byte)
	if(n_samples < 0 || n_samples >= pow(2,16)){
			printf("Unappropriate number of samples. Exit!\n");
			return 1;
			}
	printf("Set number of samples to %d\n", n_samples);

	// Convert n_samples to its bytes num[0] num[1] num[2]
	int_to_bytes(n_samples,num);

	// Send command to change number of samples
	write_byte(ftHandle, SET_NUM_SAMPLES);

	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	// Send the value for the number of samples
	write_bytes(ftHandle, num, 2);

	return 0;
}

int set_delay(FT_HANDLE ftHandle, int delay)
{
	char num[4]; 		// holds the 4 bytes of delay
	
	// The max value of the delay is 2^16 (2 byte)
	if(delay < 0 || delay >= pow(2,16)){
			printf("Unappropriate value for delay. Exit!\n");
			return 1;
			}
	printf("Set delay to %d\n", delay);

	// Convert delay to its bytes num[0] num[1]
	int_to_bytes(delay,num);

	// Send command to change number of samples
	write_byte(ftHandle, SET_DELAY);

	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	// Send the value for the number of samples
	write_bytes(ftHandle, num, 2);

	return 0;
}

int set_pw(FT_HANDLE ftHandle, int pw_int)
{
	// The max number of samples allowed is 2^8 (1 byte)
	if(pw_int < 0 || pw_int >= pow(2,8)){
			printf("Unappropriate value fow pulse width. Exit!\n");
			return 1;
			}
	printf("Set pw to %d\n", pw_int);
	char pw = (char)pw_int;
	write_byte(ftHandle, SET_PW);
	write_byte(ftHandle, pw);
	return 0;
}


int set_mode(FT_HANDLE ftHandle,int int_mode)
{
	printf("Set mode to %d\n", int_mode);
	char mode = (char)int_mode;
	write_byte(ftHandle, SET_MODE);
	write_byte(ftHandle, mode);
	return 0;
}


int start_msrmnt(FT_HANDLE ftHandle, int n_samples)
{
	int i;
	int errorcount = 0;
	char * pcBufRead;
	int read_buffer_size = 1;
	DWORD dwBytesRead;

	printf("Start measuring \n");
	// Send the command to start measuring
	write_byte(ftHandle, START_MSRMNT);
	return 0;
//!!!!!
// From here on, still only a testing function of the usb transmission.
//!!!!!
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// If the buffers are purge an error
	// occurs. Always, and mostly at
	// transmission 122. Why that????!!!???????????????????????????
 	// 
	// FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	read_buffer_size = n_samples;
	pcBufRead = (char *)malloc(read_buffer_size);

	printf("READING SAMPLES FROM USB...\n");
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);

	// Data sanity check
	printf ("DATA SANITY CHECK...\n");
	for (i = 0; i < dwBytesRead-1; i++){
		if (pcBufRead[i+1] - pcBufRead[i] != 1 && 
			pcBufRead[i+1] - pcBufRead[i] != -255){
			errorcount++;
			printf("Error at %d : %x - %x =  %d \n", 
					i, pcBufRead[i+1],  pcBufRead[i], 
					pcBufRead[i+1] - pcBufRead[i]);
		}
	}
	printf("Number of bytes red = %d \n", (int)dwBytesRead);
	printf("Nummber of errors = %d \n\n", errorcount);

	for (i = 0; i < 3 ; i++){
		printf("Byte %d = %x \n",i, pcBufRead[i]);
	}
	printf("\n");
	free(pcBufRead);

	return 0;
}

int set_case_temp(FT_HANDLE ftHandle,int t)
{
	if (t < 10 && t > 50)
	{
		printf("Desired temperature not in range\n\n");
		return 1;
	}
	printf("Set case temperature to %d\n", t);
	char temp = (char)t;
	write_byte(ftHandle, SET_CASE_TEMP);
	write_byte(ftHandle, temp);
	// Only write one byte, the +- 0.5 resolution is not neccesary
	//write_byte(ftHandle, 0);
	return 0;
}

int get_case_temp(FT_HANDLE ftHandle)
{
	int t_msb, t_lsb;
	double case_temp = 0;

	printf("Get case temperature\n");

	write_byte(ftHandle, GET_CASE_TEMP);

	
	t_lsb = (int)read_byte(ftHandle);
	t_msb = (int)read_byte(ftHandle);

	// CHANGE THIS!!!!!
	// MSB has range of -55 to 125 --> see DS1621 datasheet
	// This only works for temperature > 0
	case_temp = t_msb - 0.5 * t_lsb/128;

	printf("Case temperature = %.1f \n", case_temp);
	
	return 0;
}

int set_reset_count(FT_HANDLE ftHandle)
{

	printf("Set reset count back to 1\n");
	write_byte(ftHandle, SET_RESET_COUNT);

	return 0;
}

int get_reset_count(FT_HANDLE ftHandle)
{
	int reset_count = 0;

	printf("Get reset count\n");
	write_byte(ftHandle, GET_RESET_COUNT);
	reset_count = (int)read_byte(ftHandle);

	printf("%d resets\n\n", reset_count);

	return 0;
}

/////////////
// GLOBALS //
/////////////
int errorcount = 0;

//////////
// MAIN //
//////////
int main(int argc, char *argv[])
{
	printf("\n");

	FILE * fh;
	int j,i;
	FT_HANDLE ftHandle;
	FT_STATUS ftStatus;
	int write_buffer_size, read_buffer_size;
	char * pcBufRead;
	char   cBufWrite[1];
	
	// Open FTDI USB device
	open_device(&ftHandle);

	// Config device
	FT_SetBaudRate(ftHandle, 921600);
	FT_SetUSBParameters(ftHandle, 64000, 0);
	FT_SetDtr(ftHandle);
	FT_SetRts(ftHandle);
	FT_SetFlowControl(ftHandle, FT_FLOW_RTS_CTS, 0, 0);
	FT_SetTimeouts(ftHandle, 1000, 1000);
	//FT_SetTimeouts(ftHandle, 0, 0);
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);

	if (argc == 3 && strcmp(argv[1],"set_case_temp") == 0)
		set_case_temp(ftHandle,atoi(argv[2]));
	
	else if (argc == 2 && strcmp(argv[1],"get_case_temp") == 0)
		get_case_temp(ftHandle);
		
	else if (argc == 2 && strcmp(argv[1],"set_reset_count") == 0)
		set_reset_count(ftHandle);
	
	else if (argc == 2 && strcmp(argv[1],"get_reset_count") == 0)
		get_reset_count(ftHandle);

	else if (argc == 3 && strcmp(argv[1],"set_pw") == 0)
		set_pw(ftHandle, atoi(argv[2]));

	else if (argc == 3 && strcmp(argv[1],"set_n_samples") == 0)
		set_num_samples(ftHandle, atoi(argv[2]));
	
	else if (argc == 3 && strcmp(argv[1],"set_delay") == 0)
		set_delay(ftHandle, atoi(argv[2]));

	else if (argc == 3 && strcmp(argv[1],"set_mode") == 0){
		if (strcmp(argv[2],"CROSSPOL") == 0)
			set_mode(ftHandle, CROSSPOL);
		else if (strcmp(argv[2],"COPOL") == 0)
			set_mode(ftHandle, COPOL);
		else if (strcmp(argv[2],"RADIOMETER") == 0)
			set_mode(ftHandle, RADIOMETER);
		else if (strcmp(argv[2],"CALIBRATE") == 0)
			set_mode(ftHandle, CALIBRATE);
		else{
			printf ("Unknown mode. Exit \n");
			exit(1) ;
		}
	}

	else if (argc == 2 && strcmp(argv[1],"start") == 0)
		start_msrmnt(ftHandle, 40000);

	else if (argc == 1){
		set_pw(ftHandle, 100);
		printf("\n");
		sleep(1);
		set_num_samples(ftHandle, 4000);
		printf("\n");
		sleep(1);
		set_delay(ftHandle, 600);
		printf("\n");
		sleep(1);
		set_mode(ftHandle, COPOL);
		printf("\n");
		sleep(1);
		start_msrmnt(ftHandle, 40000);
	}
		
    else if (argc == 2 && strcmp(argv[1],"-h") == 0){
		printf ("Usage: usb_control set_pw VALUE \n");
		printf ("                   set_n_samples VALUE\n");
		printf ("                   set_delay VALUE\n");
		printf ("                   set_mode (COPOL|CROSSPOL|");
		printf ("CALIBRATE|RADIOMETER)\n");
		printf ("                   start \n\n");
		exit(1);
	}

	else{
		printf ("Unknown command. Type: usb_control -h for usage\n");
		exit(1);
	}

	FT_Close(ftHandle);
	
	return 0;
}


	

