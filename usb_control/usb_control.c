#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>		// for usleep
#include "ftd2xx.h"
#include <math.h>
#include <pthread.h>
#include <sched.h>

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
#define COPOL			0x02
#define CROSSPOL		0x04
#define CALIBRATE		0x06
#define	RADIOMETER		0x08

// Function return codes
#define ERR				1
#define OK				0

// USB serial number
// Must be altered if a new FTDI device is used
#define USB_SERIAL_NUM	"0x804c85c"


///////////////
// FUNCTIONS //
///////////////

// Open FTDI USB device
int open_device(FT_HANDLE *handle)
{
	FT_STATUS ftStatus;
	
	// Use if you are sure that you know the device number
	ftStatus = FT_Open(0, handle);
	
	// Open by serial number to make sure the correct device is selected
	//ftStatus = FT_OpenEx("0x804c710", FT_OPEN_BY_SERIAL_NUMBER, handle);
	
	if(ftStatus != FT_OK) {
		printf("FT_Open failed once\n");
		// Reconnect the device
		ftStatus = FT_CyclePort(*handle);
		if(ftStatus != FT_OK) {
			printf("FT_Cycle port failed \n");
			}
	 	//ftStatus = FT_OpenEx(USB_SERIAL_NUM,FT_OPEN_BY_SERIAL_NUMBER,handle);
		ftStatus = FT_Open(0, handle);
		if(ftStatus != FT_OK) {
			printf("FT_Open failed twice\n");
			return ERR;
			}
		printf("Reconnected.\n");
	}
	return OK;
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

// Read a byte via USB
char read_byte(FT_HANDLE ftHandle)
{
	char * pcBufRead;
	char value;
	int read_buffer_size = 1;
	DWORD dwBytesRead;

	pcBufRead = (char *)malloc(read_buffer_size);

	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	
	if (dwBytesRead != 1)
		printf("No byte read. Timeout\n");

	printf("Received 0x%x \n", pcBufRead[0]);

	value = pcBufRead[0];

	free(pcBufRead);

	return value;
}

// Write a byte via USB and check its transmission
// by reading back hopefully the same byte
int write_byte(FT_HANDLE ftHandle, char byte)
{
	char byteRead = 0;
	char pcBufRead[1];
	char cBufWrite[1];
	int write_buffer_size = 1;
	int read_buffer_size = 1;
	DWORD dwBytesRead, dwBytesWritten;

	// Send byte
	cBufWrite[0] = byte;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	printf("Sent 0x%x \n", cBufWrite[0]);
	
	// Check what the uC returns, it should be the same byte
	usleep(100000);
	byteRead = read_byte(ftHandle);

	if (byteRead != cBufWrite[0])
	{
		printf("USB transmission problem.\n");
		return 1;
	}
	return 0;
}

// Write a byte array of size 'size' and after complete write,
// read it back for transmission control
int write_bytes(FT_HANDLE ftHandle, char *bytes, int size)
{
	int i;
	int status = OK;
	char * pcBufRead;
	char   cBufWrite[size];
	int write_buffer_size = size;
	int read_buffer_size = size;
	DWORD dwBytesRead, dwBytesWritten;
	
	pcBufRead = (char *)malloc(read_buffer_size);

	// Truncate the bytes array to the desired size
	for(i = 0; i < size; i++){
		cBufWrite[i] = bytes[i];
	}
	printf("\n");

	// Send bytes
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);

	// Check what the uC returns, it should be the same byte
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	for(i = 0; i < size; i++){
	printf("Sent: %x Received: %x \n", cBufWrite[i], pcBufRead[i]);
		if (pcBufRead[i] != cBufWrite[i])
		{
			printf("USB transmission problem.\n");
			FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
			status = ERR;
		}
	}
	free(pcBufRead);

	return status;
}

// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int n_samples)
{
	char num[4]; 		// holds the 4 bytes of  n_samples
	
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
	
	// Send the value for the number of samples
	write_bytes(ftHandle, num, 2);

	return 0;
}

// Set delay for range gating
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

// Set pulse width of transmitted pulse
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


// Set measurement mode (CROSSPOL | COPPOL | CALIBRATE | RADIOMETER)
int set_mode(FT_HANDLE ftHandle,int int_mode)
{
	printf("Set mode to %d\n", int_mode);
	char mode = (char)int_mode;
	write_byte(ftHandle, SET_MODE);
	write_byte(ftHandle, mode);
	return 0;
}

// Structure for passing args to threaded FT_Read
//!! add a return status
struct thread_args{
	FT_HANDLE ftHandle;
	unsigned char * pcBufRead;
	int read_buffer_size;
	DWORD dwBytesRead;
};

// obsolete??
/*void *FT_Read_threaded(FT_HANDLE ftHandle, LPVOID pcBufRead, 
					   DWORD read_buffer_size, LPDWORD dwBytesRead)
{
	FT_Read(ftHandle, pcBufRead, read_buffer_size, dwBytesRead);
}
*/

// Function pointer for the threaded call of FT_Read.
// Threading with high priority should improve usb read continuity
void *FT_Read_thread(void *args)
{
	FT_STATUS status;
	struct thread_args *a;
	a = (struct thread_args *) args;
	status = FT_Read(a->ftHandle, a->pcBufRead, a->read_buffer_size, 
											   &a->dwBytesRead);
	
	printf("THREAD EXITS. read buff size = %d \n", a->read_buffer_size);
	pthread_exit(NULL);
}

int start_msrmnt(FT_HANDLE ftHandle, int n_samples)
{
	int i;
	int errorcount = 0;
	pthread_t tdi;
	pthread_attr_t tattr;
	struct sched_param param;

	// Structure for passing args to threaded FT_Read
	struct thread_args a;
	a.ftHandle = ftHandle;
	a.read_buffer_size = n_samples;
	a.dwBytesRead = 0;

	printf("Start measuring \n");
	// Send the command to start measuring
	write_byte(ftHandle, START_MSRMNT);
	
	// Keep commented out. The buffer already contains the first 
	// data from the uC. The c programm is not fast enough
	//FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	
	//read_buffer_size = n_samples;
	a.pcBufRead = (unsigned char *)malloc(a.read_buffer_size);

	printf("READING %d SAMPLES FROM USB. CALLING THREAD...\n", n_samples);
	
	// old read function without using threads
	//FT_Read(ftHandle, a.pcBufRead, a.read_buffer_size, &a.dwBytesRead);

	// Set high thread priority for the FT_Read function and call its thread
	pthread_attr_init(&tattr);
	pthread_attr_getschedparam(&tattr, &param);
	param.sched_priority = -20;
	pthread_attr_setschedparam(&tattr, &param);
	// Start read thread an wait for its termination
	pthread_create (&tdi, &tattr, FT_Read_thread, (void *)&a);
	pthread_join(tdi, NULL);

	// Data sanity check
	printf("%d Bytes read\n", a.dwBytesRead);
	printf("DATA SANITY CHECK...\n");
	
	if (a.dwBytesRead < 4){
		printf("Too few bytes read. Error. Exiting...\n");
		free(a.pcBufRead);
		return(1);
	}

	// Following are two error checking routine. One for a 8bit and
	// the other for a 16bit counter transmitted by the uC.
	// Either the 8bit or the 16bit parts have to be commented out
	for (i = 0; i < a.dwBytesRead-3; i=i+2){
///*
		if (256*(a.pcBufRead[i+1]) + (a.pcBufRead[i]) - 
			256*(a.pcBufRead[i+3]) - (a.pcBufRead[i+2])!= 1 &&
			256*(a.pcBufRead[i+1]) + (a.pcBufRead[i]) - 
			256*(a.pcBufRead[i+3]) - (a.pcBufRead[i+2])!= -65535){
			errorcount++;
			printf("Error at %d = %d \n", i,
			256*(a.pcBufRead[i+1]) + (a.pcBufRead[i]) );
			printf("Error at %d = %d \n", i+2,
			256*(a.pcBufRead[i+3]) + (a.pcBufRead[i+2]) );
		}
//*/
/*
	if (pcBufRead[i+1] - pcBufRead[i] != 1 && 
			pcBufRead[i+1] - pcBufRead[i] != -255){
			errorcount++;
			printf("Error at %d : %x - %x =  %d \n", 
					i, pcBufRead[i+1],  pcBufRead[i], 
					pcBufRead[i+1] - pcBufRead[i]);
		}
*/
	}
	printf("Number of bytes red = %d \n", (int)a.dwBytesRead);
	printf("Nummber of errors = %d \n\n", errorcount);

	for (i = 0; i < n_samples-1 ; i=i+2){
/*
		printf("Byte %d = %x \nByte %d = %x \n", i, pcBufRead[i],
										i+1, pcBufRead[i+1]);
*/
///*
		printf("Byte %d = %x %x %d %d\n",i, a.pcBufRead[i+1], a.pcBufRead[i],
				256*a.pcBufRead[i+1] + a.pcBufRead[i],
				256*(a.pcBufRead[i+1]) + (a.pcBufRead[i]) - 
				256*(a.pcBufRead[i+3]) - (a.pcBufRead[i+2]));
//*/
	}
	
	printf("\n");
	free(a.pcBufRead);
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);

	return 0;
}

int set_case_temp(FT_HANDLE ftHandle,int t)
{
	if (t < 10 || t > 50)
	{
		printf("Desired temperature not in range\n\n");
		return 1;
	}
	printf("Set case temperature to %d\n", t);
	char temp = (char)t;
	write_byte(ftHandle, SET_CASE_TEMP);
	
	// Only write one byte, the 16 bit +- 0.5 resolution is not neccesary
	write_byte(ftHandle, temp);
	
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

	//!! CHANGE THIS!!!!!
	// MSB has range of -55 to 125 --> see DS1621 datasheet
	// This only works for temperature > 0
	case_temp = t_msb - 0.5 * t_lsb/128;

	printf("Case temperature = %.1f \n", case_temp);
	
	return 0;
}

int set_reset_count(FT_HANDLE ftHandle)
{

	printf("Set reset count back to 0\n");
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

int get_device_list_info()
{
	int i;
	FT_STATUS ftStatus;
	FT_DEVICE_LIST_INFO_NODE *devInfo;
	DWORD numDevs;

	ftStatus = FT_CreateDeviceInfoList(&numDevs);

	if (ftStatus == FT_OK){
		printf("Number of devices is %d \n", numDevs);
	}

	if (numDevs > 0){
		devInfo = (FT_DEVICE_LIST_INFO_NODE*)
					malloc(sizeof(FT_DEVICE_LIST_INFO_NODE)*numDevs);
		ftStatus = FT_GetDeviceInfoList(devInfo, &numDevs);
		if (ftStatus == FT_OK){
			for (i = 0; i < numDevs; i++){
				printf("Dev %d:\n", i);
				printf("  Flags=0x%x\n", devInfo[i].Flags);
				printf("  Type=0x%x\n", devInfo[i].Type);
				printf("  IF=0x%x\n", devInfo[i].ID);
				printf("  LocId=0x%x\n", devInfo[i].LocId);
				printf("  SerialNumber=0x%x\n", devInfo[i].SerialNumber);
				printf("  Description=%s\n", devInfo[i].Description);
				printf("  ftHandle=0x%x\n", devInfo[i].ftHandle);
			}
		}
	}
	return 0;
}

/////////////
// GLOBALS //
/////////////
int errorcount = 0;

/////////////
// M A I N //
/////////////
int main(int argc, char *argv[])
{
	printf("\n");

	FILE * fh;
	int j,i;
	FT_HANDLE ftHandle;
	FT_STATUS ftStatus;
	int status;

	// Open FTDI USB device
	status = open_device(&ftHandle);
	if (status != OK)
	{
		printf("Open device failed. Exit.\n\n");
		exit(ERR);
	}

	// obsolete? try it without and erase if unnecessary
	//FT_SetBitMode(ftHandle, 0, 0);
	
	// Config device
	FT_SetUSBParameters(ftHandle, 64000, 0);

	// Setting latency to 2 leads to com problems, but
	// it should be as short as possible...
	FT_SetLatencyTimer(ftHandle, 0);
	FT_SetDtr(ftHandle);
	FT_SetRts(ftHandle);
	FT_SetFlowControl(ftHandle, FT_FLOW_RTS_CTS, 0, 0);
	FT_SetTimeouts(ftHandle, 1000, 1000);
	
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);

	//sleep(1);
	
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

	else if (argc == 3 && strcmp(argv[1],"start") == 0)
		start_msrmnt(ftHandle, atoi(argv[2]));

	else if (argc ==2 && strcmp(argv[1],"get_device_list") == 0)
		get_device_list_info();

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
		printf (				   		     "CALIBRATE|RADIOMETER)\n");
		printf ("                   start N_SAMPLES\n\n");
		printf ("                   get_case_temp \n");
		printf ("                   set_case_temp VALUE \n\n");
		printf ("                   get_device_list \n\n");
		exit(1);
	}

	else if (argc == 2 && strcmp(argv[1],"read") == 0){
		read_byte(ftHandle);
	}

	else if (argc == 3 && strcmp(argv[1],"write") == 0){
		write_byte(ftHandle,(char)atoi(argv[2]));
	}

	else{
		printf ("Unknown command. Type: usb_control -h for usage\n");
		exit(1);
	}

	FT_Close(ftHandle);
	
	return 0;
}


	

