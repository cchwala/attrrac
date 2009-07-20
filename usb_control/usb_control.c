#include <stdio.h>
#include <stdlib.h>
#include "ftd2xx.h"
#include <math.h>

#define BUF_SIZE 40000		

// Define commands
#define ERROR			0x00
#define SET_NUM_SAMPLES 0x01
#define SET_MODE 		0x02
#define SET_PW			0x03
#define SET_RANGE		0x04
#define SET_RANGE_INCR	0x05
#define START_MSRMNT 	0x06
#define SET_CASE_TEMP	0x07

#define GET_CASE_TEMP	0x17


// Open FTDI USB device
int open_device(FT_HANDLE *handle)
{
	FT_STATUS ftStatus;

	printf("Open FTDI USB device\n\n");
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
	printf("Written 0x%x \n", cBufWrite[0]);
	// Check what the uC returns, it should be the same byte
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Received 0x%x \n", pcBufRead[0]);
	if (pcBufRead[0] != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
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
	printf("Received 0x%x \n", pcBufRead[0]);

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
	printf("Written  ");
	for(i = 0; i < size; i++){
		cBufWrite[i] = bytes[i];
		printf("%x ", cBufWrite[i]);
	}
	printf("\n");

	// Send bytes
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	
	// Check what the uC returns, it should be the same byte
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Received ");
	for(i = 0; i < size; i++){
		printf("%x ", pcBufRead[i]);
		if (pcBufRead[i] != cBufWrite[i])
		{
			printf("USB transmission problem.\n");
			FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
			free(pcBufRead);
			return 1;
		}
	}
	printf("\n");
	
	free(pcBufRead);

	return 0;
}


// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int n_samples)
{
	char num[4]; 		// the bytes of n_samples
	
	// The max number of samples allowed is 2^24 (3 byte)
	if(n_samples < 0 || n_samples > pow(2,24)){
			printf("Unappropriate number of samples. Exit!\n");
			return 1;
			}
	// Convert n_samples to its bytes num[0] num[1] num[2]
	int_to_bytes(n_samples,num);

	// Send command to change number of samples
	write_byte(ftHandle, SET_NUM_SAMPLES);

	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	// Send the value for the number of samples
	write_bytes(ftHandle, num, 3);

	return 0;
}
	
int start_msrmnt(FT_HANDLE ftHandle, int n_samples)
{
	int i;
	int errorcount = 0;
	char * pcBufRead;
	int read_buffer_size = 1;
	DWORD dwBytesRead;

	// Send the command to start measuring
	write_byte(ftHandle, START_MSRMNT);

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

	for (i = 0; i < 5 ; i++){
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

	// CHANGE THIS
	// MSB has range of -55 to 125 --> see DS1621 datasheet
	case_temp = t_msb - 0.5 * t_lsb/128;

	printf("\nCase temperature = %.1f \n\n", case_temp);
	
	return 0;
}

int errorcount = 0;

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
	//FT_SetTimeouts(ftHandle, 1000, 1000);
	FT_SetTimeouts(ftHandle, 0, 0);
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);

	// IMPLEMENT FUNCTIONS
	// 
	set_case_temp(ftHandle,31);

	while(1){
	get_case_temp(ftHandle);
	sleep(1);
	}
	//set_num_samples(ftHandle,400000);
	
	// Start measurement
	//start_msrmnt(ftHandle,400000);

	FT_Close(ftHandle);
	
	return 0;
}


	

