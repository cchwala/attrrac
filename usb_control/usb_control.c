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

// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int num)
{
	char * pcBufRead;
	char   cBufWrite[1];
	int write_buffer_size = 1;
	int read_buffer_size = 1;
	DWORD dwBytesRead, dwBytesWritten;
	
	pcBufRead = (char *)malloc(read_buffer_size);

	// Send the command to set the number of samples
	printf("Send command\n");
	cBufWrite[0] = SET_NUM_SAMPLES;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	printf("Sent: %d\n", cBufWrite[0]);
	// Check what the uC returns, it should be the same command
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Received: %d \n", pcBufRead[0]);
	if (pcBufRead[0] != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
		free(pcBufRead);
		return 1;
	}
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	// Send the value for the number of samples
	printf("Send value\n");
	cBufWrite[0] = num;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	printf("Sent: %d\n", cBufWrite[0]);
	// Check what the uC returns, it should be the same command
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	printf("Received: %d \n", pcBufRead[0]);
	if (pcBufRead[0] != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	 	free(pcBufRead);
		return 1;
	}
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	free(pcBufRead);
	return 0;
}


int errorcount = 0;

int main(int argc, char *argv[])
{
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
	set_num_samples(ftHandle,100);
	
	FT_Close(ftHandle);
	
///////////////////////
// S T O P   H E R E //
///////////////////////
	return 0;

	// Check status byte
	
	// Write command
	
	// Verify command
	
	// Start measurement
	
	// Verify command
	
	// ----------------------------------- //
	// Read a large block of data from USB //
	// and check for sanity.			   //
	// ----------------------------------- //
	//large_read_usb(ftHandle,read_buffer,buffer_size);

//	// Open data file
//	fh = fopen("data.dat", "w+");
//	if(fh == NULL) {
//		printf("Cant open source file\n");
//		return 1;
//	}
//	// Write buffer to file
//	for (i = 0; i < dwBytesRead; i++){
//		fprintf(fh, "%d \n", pcBufRead[i]);
//	}
//	
//	// Data sanity check
//	printf ("DATA SANITY CHECK...\n");
//	for (i = 0; i < dwBytesRead-1; i++){
//		if (pcBufRead[i+1] - pcBufRead[i] != 1 && 
//			pcBufRead[i+1] - pcBufRead[i] != -255){
//			errorcount++;
//			printf("Error at %d : %x - %x =  %d \n", 
//					i, pcBufRead[i+1],  pcBufRead[i], 
//					pcBufRead[i+1] - pcBufRead[i]);
//		}
//	}
//	printf("Number of bytes red = %d \n", dwBytesRead);
//	printf("Nummber of errors = %d \n\n", errorcount);
//
//	// Close file handle
//	fclose(fh);
//	// Close usb device handle
//	FT_Close(ftHandle);
//	
//	return 0;
}


	

