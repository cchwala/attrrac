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


// Open FTDI USB device
int open_device(FT_HANDLE *handle)
{
	FT_STATUS ftStatus;

	printf("Trying to open FTDI USB device\n");
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
int int_to_bytes(unsigned int n, unsigned int *byte)
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

// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int n_samples)
{
	char * pcBufRead;
	char   cBufWrite[1];
	int write_buffer_size = 1;
	int read_buffer_size = 1;
	unsigned int num[4]; 		// the bytes of n_samples
	char bytes_red[4];			// the bytes red back for error checking
	DWORD dwBytesRead, dwBytesWritten;
	
	// The max number of samples allowed is 2^24 (3 byte)
	if(n_samples < 0 || n_samples > pow(2,24)){
			printf("Unappropriate number of samples. Exit!\n");
			return 1;
			}
	// Convert n_samples to its bytes num[0] num[1] num[2]
	int_to_bytes(n_samples,num);

	pcBufRead = (char *)malloc(read_buffer_size);

	// Send the command to set the number of samples
	cBufWrite[0] = SET_NUM_SAMPLES;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	// Check what the uC returns, it should be the same command
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Sent %x \nRed  %x \n\n", cBufWrite[0], pcBufRead[0]);
	if (pcBufRead[0] != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
		free(pcBufRead);
		return 1;
	}
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	// Send the value for the number of samples
	cBufWrite[0] = num[0];
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	cBufWrite[0] = num[1];
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	cBufWrite[0] = num[2];
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	
	// Check what the uC returns, it should be the same command
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	bytes_red[0] = pcBufRead[0];
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	bytes_red[1] = pcBufRead[0];
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	bytes_red[2] = pcBufRead[0];
	bytes_red[3] = 0;	// Has to be filled up with zeros because it will be
						// converted to int which is usually!!! 4 byte
	printf("Sent n_samples = %d \nRece n_samples = %d \n", 
			n_samples,
			bytes_to_int(bytes_red));
	
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	free(pcBufRead);
	return 0;
}
	
int start_msrmnt(FT_HANDLE ftHandle, int n_samples)
{
	int i;
	int errorcount = 0;
	char * pcBufRead;
	char   cBufWrite[1];
	int write_buffer_size = 1;
	int read_buffer_size = 1;
	DWORD dwBytesRead, dwBytesWritten;

	pcBufRead = (char *)malloc(read_buffer_size);

	// Send the command to start measuring
	cBufWrite[0] = START_MSRMNT;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	printf("Written 0x%x \n", cBufWrite[0]);
	// Check what the uC returns, it should be the same command
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Received 0x%x \n", pcBufRead[0]);
	if (pcBufRead[0] != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
		free(pcBufRead);
		return 1;
	}

	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	// If the buffers are purge an error
	// occurs. Always, and mostly at
	// transmission 122. Why that????!!!???????????????????????????
 	// 
	// FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	free(pcBufRead);

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
	printf("Number of bytes red = %d \n", dwBytesRead);
	printf("Nummber of errors = %d \n\n", errorcount);

	for (i = 0; i < 5 ; i++){
		printf("Byte %d = %x \n",i, pcBufRead[i]);
	}
	printf("\n");
	free(pcBufRead);

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
	//FT_ResetDevice(ftHandle);
	FT_SetBaudRate(ftHandle, 921600);
	FT_SetUSBParameters(ftHandle, 64000, 0);
	FT_SetDtr(ftHandle);
	FT_SetRts(ftHandle);
	FT_SetFlowControl(ftHandle, FT_FLOW_RTS_CTS, 0, 0);
	FT_SetTimeouts(ftHandle, 1000, 1000);

	// Send command
	set_num_samples(ftHandle,400000);
	
	// Start measurement
	start_msrmnt(ftHandle,400000);

	FT_Close(ftHandle);
	
	return 0;
}


	

