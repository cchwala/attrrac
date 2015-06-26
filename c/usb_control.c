#include <stdio.h>
#include <stdlib.h>
//#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>		// for usleep
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <syslog.h>
#include <time.h>

#include "helper.h"
#include "ftd2xx.h"
#include "usb_control.h"



/***********************/
/* BASIC USB FUNCTIONS */
/***********************/

// Open FTDI USB device
int open_device(FT_HANDLE *handle)
{
	FT_STATUS ftStatus;
	
	// Use if you are sure that you know the device number
	ftStatus = FT_Open(0, handle);
	
	// Open by serial number to make sure the correct device is selected
	//ftStatus = FT_OpenEx("0x804c710", FT_OPEN_BY_SERIAL_NUMBER, handle);
	
	if(ftStatus != FT_OK) {
		syslog(LOG_NOTICE, "FT_Open failed once\n");
		// Reconnect the device
		// !!
		// !! CyclePort not supported in Linux
		// !!
		//ftStatus = FT_CyclePort(*handle);
		if(ftStatus != FT_OK) {
			syslog(LOG_NOTICE, "FT_Cycle port failed \n");
			}
	 	//ftStatus = FT_OpenEx(USB_SERIAL_NUM,FT_OPEN_BY_SERIAL_NUMBER,handle);
		ftStatus = FT_Open(0, handle);
		if(ftStatus != FT_OK) {
			syslog(LOG_NOTICE, "FT_Open failed twice\n");
			return USB_ERR;
			}
		syslog(LOG_NOTICE, "Reconnected.\n");
	}
	return OK;
}

// Read a byte via USB
int read_byte(FT_HANDLE ftHandle, char* value)
{
	FT_STATUS ftStatus;
	char * pcBufRead;
	//char value;
	int read_buffer_size = 1;
	DWORD dwBytesRead;

	pcBufRead = (char *)malloc(read_buffer_size);

	ftStatus = FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	if(ftStatus != FT_OK){
			syslog(LOG_NOTICE, "FT_Read failed \n");
			return USB_ERR;		
	}
	
	if (dwBytesRead != 1){
		syslog(LOG_NOTICE, "No byte read. Timeout\n");
		return USB_ERR;
	}

	//syslog(LOG_NOTICE, "Received 0x%x \n", pcBufRead[0]);

	*value = pcBufRead[0];

	free(pcBufRead);

	return OK;
}

// Write a byte via USB and check its transmission
// by reading back hopefully the same byte
int write_byte(FT_HANDLE ftHandle, char byte)
{
	int status;
	char byteRead = 0;
	char cBufWrite[1];
	int write_buffer_size = 1;
	DWORD dwBytesWritten;

	// Send byte
	cBufWrite[0] = byte;
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);
	//syslog(LOG_NOTICE, "Sent 0x%x \n", cBufWrite[0]);
	
	// Check what the uC returns, it should be the same byte
	status = read_byte(ftHandle, &byteRead);

	if (byteRead != cBufWrite[0])
	{
		syslog(LOG_NOTICE, "USB transmission problem. Sent: %x Read: %x\n",cBufWrite[0],byteRead);
		return USB_ERR;
	}
	return OK;
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

	// Send bytes
	FT_Write(ftHandle, cBufWrite, write_buffer_size, &dwBytesWritten);

	// Check what the uC returns, it should be the same byte
	FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
	for(i = 0; i < size; i++){
	//syslog(LOG_NOTICE, "Sent: %x Received: %x \n", cBufWrite[i], pcBufRead[i]);
		if (pcBufRead[i] != cBufWrite[i])
		{
			syslog(LOG_NOTICE, "USB transmission problem.\n");
			FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
			status = USB_ERR;
		}
	}
	free(pcBufRead);

	return status;
}


/*******************************/
/* COMMAND INTERFACE FUNCTIONS */
/*******************************/

// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int n_samples)
{
	char num[4]; 			// holds the 4 bytes of  n_samples
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The max number of samples allowed tecnically possible would be 2^24
	// but 4e6 is enough. N has to be even because we want N/2 samples for
	// each polarization
	if(n_samples < 0 || n_samples > 4000000 || (n_samples%2 != 0)){
		syslog(LOG_NOTICE, "Unappropriate number of samples. N must be even. Exit!\n");
		return ARG_ERR;
	}
	syslog(LOG_NOTICE, "Set number of samples to %d\n", n_samples);

	// Convert n_samples to its bytes num[0] num[1] num[2]
	int_to_bytes(n_samples,num);

	// Send command to change number of samples
	status = write_byte(ftHandle, SET_NUM_SAMPLES);
	if (status != OK) 			return status;
	
	// Send the value for the number of samples
	status = write_bytes(ftHandle, num, 3);
	if (status != OK) 			return status;
	
	// Check CPLD_BUSY status
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	// Set USB timeouts so that they fit to n_samples
	int sampl_freq = 25000; // in Hz
	int tout = ceil(1000*n_samples/sampl_freq)+500; // in miliseconds
	if (tout < 3000) tout = 3000;
	FT_SetTimeouts(ftHandle, tout, tout);
	syslog(LOG_NOTICE, "Set timeout to %d\n", tout);

	return OK;
}

// Set delay for range gating
int set_delay(FT_HANDLE ftHandle, int delay)
{
	char num[4]; 			// holds the 4 bytes of delay
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The max value of the delay is 500 clock cycles (20ns*500 = 3km)
	if(delay < 0 || delay > 500){
		syslog(LOG_NOTICE, "Unappropriate value for delay. Exit!\n");
		return ARG_ERR;
	}
	syslog(LOG_NOTICE, "Set delay to %d\n", delay);

	// Convert delay to its bytes num[0] num[1]
	int_to_bytes(delay,num);

	// Send command to change number of samples
	status = write_byte(ftHandle, SET_DELAY);
	if (status != OK) 			return status;
	
	// Send the value for the number of samples
	status = write_bytes(ftHandle, num, 2);
	if (status != OK) 			return status;
	
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;

	return OK;
}

// Set pulse width of transmitted pulse
int set_pw(FT_HANDLE ftHandle, int int_pw)
{
	char pw = (char)int_pw;	// value in char format for uC
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The max number for pulse width is 2^8 (1 byte)
	if(int_pw < 0 || int_pw >= pow(2,8)){
		syslog(LOG_NOTICE, "Unappropriate value fow pulse width. Exit!\n");
		return ARG_ERR;
	}
	syslog(LOG_NOTICE, "Set pw to %d\n", int_pw);
	
	
	// Send command to change pulse width
	status = write_byte(ftHandle, SET_PW);
	if (status != OK) 			return status;
	
	// Send new pulse width value
	status = write_byte(ftHandle, pw);
	if (status != OK) 			return status;
	
	// Check CPLD_BUSY status
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}


// Set measurement mode (CROSSPOL | COPPOL | CALIBRATE | RADIOMETER)
int set_mode(FT_HANDLE ftHandle,int int_mode)
{
	char mode = (char)int_mode;	// char format fpr uC
	unsigned char uC_status;	// status returned by uC
	int status;					// usb_function return status
	
	syslog(LOG_NOTICE, "Set mode to %d\n", int_mode);
	
	if (mode != CROSSPOL && mode != COPOL && mode != CALIBRATE 
	 && mode != RADIOMETER){
		syslog(LOG_NOTICE, "wrong mode value\n");
		return ARG_ERR;
	}
	
	// Send command to change mode
	status = write_byte(ftHandle, SET_MODE);
	if (status != OK) 			return status;
	
	// Send new mode value
	write_byte(ftHandle, mode);
	if (status != OK) 			return status;
	
	// Check CPLD_BUSY status
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}

// Set delay of ADC trigger pulse after RX pulse
int set_adc(FT_HANDLE ftHandle, int int_adc)
{
	char adc = (char)int_adc;// value in char format for uC
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The max number for ADC delay is 2^8 (1 byte)
	if(int_adc < 0 || int_adc >= pow(2,8)){
		syslog(LOG_NOTICE, "Unappropriate value for ADC delay. Exit!\n");
		return ARG_ERR;
	}
	syslog(LOG_NOTICE, "Set ADC delay to %d\n", int_adc);
	
	// Send command to change pulse width
	status = write_byte(ftHandle, SET_ADC);
	if (status != OK) 			return status;
	
	// Send new ADC delay value
	status = write_byte(ftHandle, adc);
	if (status != OK) 			return status;
	
	// Check CPLD_BUSY status
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}
	
// Set time the polarizer switch precedes the TX pulse
// The value is multiplied by 32 in the CPLD. With the
// 50 MHz clock this makes 640ns per byte
int set_pol_precede(FT_HANDLE ftHandle, int int_precede)
{
	char precede = (char)int_precede; 
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The max value for pol_precede is 2^8 (1 byte)
	if(int_precede < 0 || int_precede >= pow(2,8)){
		syslog(LOG_NOTICE, "Unappropriate value for pol_precede. Exit!\n");
		return ARG_ERR;
	}
	syslog(LOG_NOTICE, "Set pol_precede to %d\n", precede);

	// OBSOLETE
	// Convert pol_precede to its bytes num[0] num[1]
	//int_to_bytes(precede,num);

	// Send command to change number of samples
	status = write_byte(ftHandle, SET_POL_PRECEDE);
	if (status != OK) 			return status;
	
	// Send the value for the number of samples
	status = write_byte(ftHandle, precede);
	if (status != OK) 			return status;
	
	// Check if CPLD is busy
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;

	return OK;
}

// Get status from CPLD (lock bits from PLOs)
int get_status(FT_HANDLE ftHandle)
{
	char cpld_status;
	int status = OK;
	
	syslog(LOG_NOTICE, "Get CPLD status\n");

	// write command
	status = write_byte(ftHandle, GET_STATUS);
	if (status != OK)			return status;
	
	// get cpld status
	status = read_byte(ftHandle, &cpld_status);
	if (status != OK)  			return status;
	
	syslog(LOG_NOTICE, "CPLD status = %.d \n", cpld_status);
	syslog(LOG_NOTICE, "CPLD status = %.d \n", cpld_status);
	
	return OK;
}

// Set attenuatores for 22 GHz system
int set_atten22(FT_HANDLE ftHandle, int atten1, int atten2)
{
	// variables holding the bytes of the attenuator level integers
	char num1[4];
	char num2[4];
	// variable transmitted to uC only contains lowest bytes of atten1 and atten2
	char num[2];
	
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The two attenuators each have 5 control bytes corresponding to 1,2,4,8,16 dB
	if(atten1 < 0 || atten2 > 31 || atten2 < 0 || atten2 > 31){
		syslog(LOG_NOTICE, "Unappropriate value for atten22.\n");
		return ARG_ERR;
	}
	
	

	int_to_bytes(atten1, num1);
	int_to_bytes(atten2, num2);
	
	// only the lowest byte for atten1 and atten2 are importatnt
	num[0] = num1[0];
	num[1] = num2[0];
	
	// Write command
	status = write_byte(ftHandle, SET_ATTEN22);
	if (status != OK) 			return status;
	
	// Write value
	status = write_bytes(ftHandle, num, 2);
	if (status != OK) 			return status;
	
	// Check CPLD_BUSY status
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	syslog(LOG_NOTICE, "Set atten22 to %d %d\n", atten1, atten2);
	
	return OK;
}

// Set attenuators for 35 GHz system
int set_atten35(FT_HANDLE ftHandle, int atten1, int atten2)
{
		
	// variables holding the bytes of the attenuator level integers
	char num1[4];
	char num2[4];
	// variable transmitted to uC only contains lowest bytes of atten1 and atten2
	char num[2];
	unsigned char uC_status;// status returned by uC
	int status;				// usb_function return status
	
	// The two attenuators each have 5 control bytes corresponding to 1,2,4,8,16 dB
	if(atten1 < 0 || atten2 > 31 || atten2 < 0 || atten2 > 31){
		syslog(LOG_NOTICE, "Unappropriate value for atten35.\n");
		return 1;
	}
	
	int_to_bytes(atten1, num1);
	int_to_bytes(atten2, num2);
	
	// only the lowest byte for atten1 and atten2 are importatnt
	num[0] = num1[0];
	num[1] = num2[0];
	
	// Write command
	status = write_byte(ftHandle, SET_ATTEN35);
	if (status != OK) 			return status;
	
	// Write value
	status = write_bytes(ftHandle, num, 2);
	if (status != OK) 			return status;
	
	// Check CPLD_BUSY status
	status = read_byte(ftHandle, &uC_status);
	if (status != OK) 			return status;
	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
	else if (uC_status != OK)   return uC_ERR;
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	syslog(LOG_NOTICE, "Set atten35 to %d %d\n", atten1, atten2);
	
	return OK;
}


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
	syslog(LOG_NOTICE, "Thread function: Calling FT_Read\n");
	status = FT_Read(a->ftHandle, a->pcBufRead, a->read_buffer_size, 
											   &a->dwBytesRead);
	
	syslog(LOG_NOTICE, "THREAD EXITS. read buff size = %d \n", a->read_buffer_size);
	pthread_exit(NULL);
}

int start_msrmnt(FT_HANDLE ftHandle, int n_bytes_to_read, DATA_STRUCT *data)
{
	//int i, j;
	//unsigned char uC_status;// status returned by uC
	//int status;				// usb_function return status
	
	int first = 0; 	// first usefull byte in pcBufRead
	int last = 0;  	// last usefull byte in pcBufRead
	
// 	pthread_t tdi;
// 	pthread_attr_t tattr;
// 	struct sched_param param;
	// Structure for passing args to threaded FT_Read
	struct thread_args a;
	a.ftHandle = ftHandle;
	a.read_buffer_size = n_bytes_to_read;
	a.dwBytesRead = 0;

	a.pcBufRead = (unsigned char *)malloc(a.read_buffer_size);
	
	// Send the command to start measuring
	write_byte(ftHandle, START_MSRMNT);
	
	// Check CPLD_BUSY status
// 	status = read_byte(ftHandle, &uC_status);
// 	if (status != OK) 			return status;
// 	if (uC_status == CPLD_BUSY) return CPLD_BUSY;
// 	else if (uC_status != OK)   return uC_ERR;
// 	
// 	// Wait for done message
// 	status = read_byte(ftHandle, &uC_status);
// 	if (status != OK)  			return status;
// 	if (uC_status != DONE)		return uC_ERR;
	
//	return OK;

	// old read function without using threads
	FT_Read(ftHandle, a.pcBufRead, a.read_buffer_size, &a.dwBytesRead);
	
// 	unsigned char count = 0;
// 	for(i=0; i<a.read_buffer_size; i++){
// 		if ( i % 9 == 0 ){
// 			a.pcBufRead[i] = count;
// 			count += 3;
// 		}
// 		else{
// 			a.pcBufRead[i] = 77;
// 		}
// 	}
// 	a.dwBytesRead = a.read_buffer_size;

//	syslog(LOG_NOTICE, "Number of bytes read = %d \n", (int)a.dwBytesRead);

	if (a.dwBytesRead < 9){
		syslog(LOG_NOTICE, "Too few bytes (N<9) read. Error. Exiting...\n");
		free(a.pcBufRead);
		return ERR;
	}
	
	if (a.dwBytesRead != a.read_buffer_size){
		syslog(LOG_NOTICE, "To few bytes read. Read_buffer = %d n_bytes_read = %d\n",
			    a.read_buffer_size, (int)a.dwBytesRead);
		free(a.pcBufRead);
		return ERR;
	}
								
	// Check read out data for errors
	check_read_data (a.pcBufRead, a.dwBytesRead, &first, &last);
	//printf ("first = %d \nlast  = %d\n", first, last);

	// The counter goes 0,3,6,9,11,13... 
	// The counter byte for the first used set of bits should be even
	// because polarization is switched for every measurement, thus we
	// need a defined start state: 	even = H/V switch high
	//									 = 22 GHz V 
	//									 = 35 GHz H
	if (a.pcBufRead[first] % 2 != 0){
		first = first+9;
		syslog(LOG_NOTICE, "changed ifrst\n");
	}
	
	int diff, N;
	diff = last - first;
	
	if (diff < 20){
		syslog(LOG_NOTICE, "too few usefull bytes found\n");
		free(a.pcBufRead);
		return ERR;
	}
	
	if (diff % 9 != 0){
		syslog(LOG_NOTICE, "first or last counter byte wrongly selected\n");
		free(a.pcBufRead);
		return ERR;
	}

	// DEBUG: write raw data to file
// 	FILE *raw_file;
// 	raw_file = fopen("raw_data.dat","w");
// 	
// 	int i;
// 	for(i=0; i<a.dwBytesRead; i++){
// 		if(i%9 == 0)
// 			fprintf(raw_file, "\n");
// 		fprintf(raw_file, "%4x ", a.pcBufRead[i]);
// 	}
// 	fclose(raw_file);
		

	// Each data point in i_q_h_v_data[] holds 8 values 
	// (I_35,Q_35,I_22,Q_22 each for H and V polarization)
	// Each value consists of two bytes raw data : 2x8 = 16
	// And each H and V value has a counter byte : 2x1 = 2   = 18 bytes
	// 9 is added to diff because we need the number of 9 byte block read in,
	// not only the difference.
	// !! STRANGE BEHAVIOR HERE !! after multiple executions foo is NaN
	//int foo = (diff+9)/18;
	N = floor((diff+9)/18);
	
	data->N = N;
	
	raw2_i_q_h_v_data(a.pcBufRead, data, N, first, last);
	
	free(a.pcBufRead);
//	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);

	return OK;
}

void *start_slow_loop(void *args)
{
	unsigned char done_message;
	struct thread_args *a = (struct thread_args*) args;
	FT_HANDLE ftHandle = a->ftHandle;
	int read_buffer_size = 9*a->read_buffer_size;
	DWORD dwBytesRead;
	unsigned char *pcBufRead = (unsigned char *)malloc(read_buffer_size);
	
	int N = floor(read_buffer_size/2);
	
	DATA_STRUCT *data = create_data_struct(N);
	
	int t_msb, t_lsb;
	char c_msb, c_lsb;
	double case_temp = 0;
	double board_temp = 0;
	
	struct timeval tim;
	double t;
	
	int accel1, accel2;
	
	int reset_count = 0;
	char c_reset_count;
	
	// build timestamp
	
	// Send the command to start measuring
	write_byte(ftHandle, START_SLOW_LOOP);
	
	slow_loop_keep_running = 1;
	
	time_t t_now, t_old;
	struct tm *ts;
	int tm_min_old, tm_min_diff;
	char filename[23], filename_old[23], sys_string[100];
	
	// open file with timestamped filename
	time(&t_now);
	ts = gmtime(&t_now);
	strftime(filename, 23, "loop_%Y%m%d_%H%M.dat", ts);
	FILE *loop_file = fopen(filename,"w");
        // Write header
        fprintf(loop_file, "# FILE_TYPE  = SLOW_LOOP_v2 \n");
        fprintf(loop_file, "# n_sample   = %d \n", a->conf->n_samples);
        fprintf(loop_file, "# pw         = %d \n", a->conf->pw);
        fprintf(loop_file, "# delay      = %d \n", a->conf->delay);
        fprintf(loop_file, "# pol_preced = %d \n", a->conf->pol_preced);
        fprintf(loop_file, "# adc_delay  = %d \n", a->conf->adc_delay);
        fprintf(loop_file, "#\n");
        fprintf(loop_file, "time;            I_h_35;  Q_h_35;  I_h_22;  Q_h_22;  "
                                            "I_v_35;  Q_v_35;  I_v_22;  Q_v_22;  "
                                            "T_case;   T_pcb;  accel1;  accel2;  resets\n");
        
        
	int foo_count = 0;
	
	syslog(LOG_NOTICE, "Starting slow loop\n");
	tm_min_old = ts->tm_min;
	
	
	// QUICK FIX
	// With low timeout values we get many missing bytes...
	// Why that. Should not take to long to read samples here.
	//
	// CHECK THIS !!!!!!!!!!!!!!!
	//
	// Maybe a problem of block size which is the largest for
	// fast burst transfer
	//
	FT_SetTimeouts(ftHandle, 15000, 15000);
	
	// loop to continuously read in bursts of n_samples
	// as long as slow_loop_keep_running is true
	while(slow_loop_keep_running == 1){
		// open new file every minute
		time(&t_now);
		ts = gmtime(&t_now);
		tm_min_diff = abs(ts->tm_min - tm_min_old);
		if(tm_min_diff != 0 && ts->tm_min%1 == 0){
			strcpy (filename_old, filename);
			// new filename
			strftime(filename, 23, "loop_%Y%m%d_%H%M.dat", ts);
			// close old file
			fclose(loop_file);
			// zip file to data_send directory
			sprintf(sys_string, "gzip -c %s > /root/data_to_send/%s.gz", 
					filename_old, filename_old);
			system(sys_string);
			// remove file
			sprintf(sys_string, "rm %s", filename_old);
			system(sys_string);
			// open new file
			loop_file = fopen(filename,"w");
			tm_min_old = ts->tm_min;
			// Write header
			fprintf(loop_file, "# FILE_TYPE  = SLOW_LOOP_v2 \n");
			fprintf(loop_file, "# n_sample   = %d \n", a->conf->n_samples);
			fprintf(loop_file, "# pw         = %d \n", a->conf->pw);
			fprintf(loop_file, "# delay      = %d \n", a->conf->delay);
			fprintf(loop_file, "# pol_preced = %d \n", a->conf->pol_preced);
			fprintf(loop_file, "# adc_delay  = %d \n", a->conf->adc_delay);
			fprintf(loop_file, "#\n");
                        fprintf(loop_file, "time;            I_h_35;  Q_h_35;  I_h_22;  Q_h_22;  "
                                                            "I_v_35;  Q_v_35;  I_v_22;  Q_v_22;  "
                                                            "T_case;   T_pcb;  accel1;  accel2;  resets\n");
		}
		
		// read in case temperature
		// explanation see function get_case_temp
		read_byte(ftHandle, &c_lsb);
		read_byte(ftHandle, &c_msb);
		t_lsb = (int)c_lsb;
		t_msb = (int)c_msb;
		case_temp = t_msb - 0.5 * t_lsb/128;
		
		// get current time
		gettimeofday(&tim, NULL);
		
		//fprintf(loop_file, ctime(&tim.tv_sec));
		fprintf(loop_file, "%ld.%03ld; ", tim.tv_sec, tim.tv_usec/1000);
		
		// read in board temperature
		read_byte(ftHandle, &c_lsb);
		read_byte(ftHandle, &c_msb);
		t_lsb = (int)c_lsb;
		t_msb = (int)c_msb;
		board_temp = t_msb - 0.5 * t_lsb/128;
		
		// read in accelerometer
		read_byte(ftHandle, &c_lsb);
		read_byte(ftHandle, &c_msb);
		accel1 = ((double)c_msb*256 + (double)c_lsb);
		read_byte(ftHandle, &c_lsb);
		read_byte(ftHandle, &c_msb);
		accel2 = ((double)c_msb*256 + (double)c_lsb);
		
		// get reset count
		read_byte(ftHandle, &c_reset_count);
		reset_count = (int)c_reset_count;
		
		// read in data
		FT_Read(ftHandle, pcBufRead, read_buffer_size, &dwBytesRead);
			
		// read done message
		//read_byte(ftHandle, &done_message);
		//if (done_message != DONE)		return uC_ERR;
		
		// check data
		if (dwBytesRead != read_buffer_size){
			syslog(LOG_NOTICE, "slow_loop: too few byte read\n");
		}
		// erase this and write a clear read in and check routine!!!!!!!!!!!!!
		int first, last;
		check_read_data (pcBufRead, dwBytesRead, &first, &last);
		int diff = last-first;
		N = floor((diff+9)/18);
		
		
		// if there is a glitch in the data purge USB buffer and write
		// error to file
		if ((last-first-9)%18 != 0){
			syslog(LOG_NOTICE, "last - first is not mod 18!!\n");
			FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
			FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX); // safer to do this twice
			fprintf(loop_file, 
					"9999; 9999; 9999; 9999; 9999; 9999; 9999; 9999; % 7.1f; % 7.1f; % 7d; % 7d; % 7d\n",
					case_temp, board_temp, accel1, accel2, reset_count);
		}
		else{	
			// process data
			raw2_i_q_h_v_data(pcBufRead, data, N, first, last);
			mean(data, 0); // 0 because no sample shall be skipped
			std_dev(data, 0);	

			// build timestamp
			
			// write data to file

			
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			// ADD STANDARD DEVIATION !!!!!!
			// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!
/*			fprintf(loop_file, 
			"%5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %d %d %d\n",
						data->h_a_35->mean, data->h_p_35->mean,
						data->h_a_22->mean, data->h_p_22->mean,  
						data->v_a_35->mean, data->v_p_35->mean,
						data->v_a_22->mean, data->v_p_22->mean,
						case_temp, board_temp, accel1, accel2, reset_count);
*/
			fprintf(loop_file, 
 			"% 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7.1f; % 7d; % 7d; % 7d\n",
 						data->h_i_35->mean, data->h_q_35->mean,
 						data->h_i_22->mean, data->h_q_22->mean,  
 						data->v_i_35->mean, data->v_q_35->mean,
 						data->v_i_22->mean, data->v_q_22->mean,
 						case_temp, board_temp, accel1, accel2, reset_count);
						
// 			foo_count++;
// 			if(foo_count % 20 == 0){
// 				syslog(LOG_NOTICE, "%5.1f %5.1f %5.1f %5.1f \n", 
// 					   sqrt(pow(data->h_i_35->mean,2) + pow(data->h_q_35->mean,2)),
// 					   sqrt(pow(data->v_i_35->mean,2) + pow(data->v_q_35->mean,2)),
// 					   sqrt(pow(data->h_i_22->mean,2) + pow(data->h_q_22->mean,2)),
// 					   sqrt(pow(data->v_i_22->mean,2) + pow(data->v_q_22->mean,2)) );
//	 		}
		}		
	}	// end loop
  
	// Send the command to stop measuring
	write_byte(ftHandle, STOP_SLOW_LOOP);
	
	// Purge buffers, because there may be some data from slow_loop that
	// was not read in.
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);

	syslog(LOG_NOTICE, "Slow low stopped\n");

	fclose(loop_file);
	free_data_struct(data);
	free(pcBufRead);

	return NULL;
}

int stop_slow_loop(FT_HANDLE ftHandle)
{
	// set flag to stop measurement loop and make thread to return
	slow_loop_keep_running = 0;
	
	return OK;
}

int set_case_temp(FT_HANDLE ftHandle,int t)
{
	if (t < 10 || t > 50)
	{
		syslog(LOG_NOTICE, "Desired temperature not in range\n\n");
		return 1;
	}
	syslog(LOG_NOTICE, "Set case temperature to %d\n", t);
	char temp = (char)t;
	write_byte(ftHandle, SET_CASE_TEMP);
	
	// Only write one byte, the 16 bit +- 0.5 resolution is not neccesary
	write_byte(ftHandle, temp);
	
	return 0;
}

int get_case_temp(FT_HANDLE ftHandle)
{
	int t_msb, t_lsb;
	char c_t_msb, c_t_lsb;
	double case_temp = 0;

	syslog(LOG_NOTICE, "Get case temperature\n");

	write_byte(ftHandle, GET_CASE_TEMP);
	
//	t_lsb = (int)read_byte(ftHandle);
//	t_msb = (int)read_byte(ftHandle);

	read_byte(ftHandle, &c_t_lsb);
	read_byte(ftHandle, &c_t_msb);
	t_lsb = (int)c_t_lsb;
	t_msb = (int)c_t_msb;
		
	//!! CHANGE THIS!!!!!
	// MSB has range of -55 to 125 --> see DS1621 datasheet
	// This only works for temperature > 0
	//
	// Should work because signed char (c_t_msb) is casted to int (t_msb),
	// which is already the right case temperatures msb. t_lsb indicastes only 
	// a 0.5 addition.
	case_temp = t_msb - 0.5 * t_lsb/128;

	syslog(LOG_NOTICE, "Case temperature %.1f \n", t_msb - 0.5 * t_lsb/128);
	syslog (LOG_NOTICE, "Case temperature = %.1f \n", case_temp);
	
	return 0;
}

int set_board_temp(FT_HANDLE ftHandle,int t)
{
	if (t < 10 || t > 50)
	{
		syslog(LOG_NOTICE, "Desired temperature not in range\n\n");
		return 1;
	}
	syslog(LOG_NOTICE, "Set board temperature to %d\n", t);
	char temp = (char)t;
	write_byte(ftHandle, SET_BOARD_TEMP);
	
	// Only write one byte, the 16 bit +- 0.5 resolution is not neccesary
	write_byte(ftHandle, temp);
	
	return 0;
}

int get_board_temp(FT_HANDLE ftHandle)
{
	int t_msb, t_lsb;
	char c_t_msb, c_t_lsb;
	double case_temp = 0;

	syslog(LOG_NOTICE, "Get board temperature\n");

	write_byte(ftHandle, GET_BOARD_TEMP);
	
//	t_lsb = (int)read_byte(ftHandle);
//	t_msb = (int)read_byte(ftHandle);

	read_byte(ftHandle, &c_t_lsb);
	read_byte(ftHandle, &c_t_msb);
	t_lsb = (int)c_t_lsb;
	t_msb = (int)c_t_msb;
		
	//!! CHANGE THIS!!!!!
	// MSB has range of -55 to 125 --> see DS1621 datasheet
	// This only works for temperature > 0
	//
	// Should work because signed char (c_t_msb) is casted to int (t_msb),
	// which is already the right case temperatures msb. t_lsb indicastes only 
	// a 0.5 addition.
	case_temp = t_msb - 0.5 * t_lsb/128;

	syslog(LOG_NOTICE, "Board temperature %.1f \n", t_msb - 0.5 * t_lsb/128);
	syslog (LOG_NOTICE, "Board temperature = %.1f \n", case_temp);
	
	return OK;
}

int set_loop_freq(FT_HANDLE ftHandle, int f)
{
	if (f != 5 && f != 10 && f != 20)
	{
		syslog(LOG_NOTICE, "Loop frequency not supported (chose 20 Hz, 10 Hz or 5 Hz)\n\n");
		return ARG_ERR;
	}
	syslog(LOG_NOTICE, "Set loop_freq to %d\n", f);
	if (f == 5) write_byte(ftHandle, SET_LOOP_FREQ_5);
	else if (f == 10) write_byte(ftHandle, SET_LOOP_FREQ_10);
	else if (f == 20) write_byte(ftHandle, SET_LOOP_FREQ_20);
	
	return OK;
}

int get_lock(FT_HANDLE ftHandle)
{
	unsigned char value, uC_status;
	int status;	
	
	syslog(LOG_NOTICE, "Get lock indicators\n");
	
	write_byte(ftHandle, GET_LOCK);
	
	read_byte(ftHandle, &value);
	
	syslog(LOG_NOTICE, "Lock indicators: %d\n", (int)value);
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}
	


int get_adc4(FT_HANDLE ftHandle)
{
	unsigned char c_msb, c_lsb, uC_status;
	double adc_value = 0;
	int status;
	
	syslog(LOG_NOTICE, "Get adc4 value\n");

	write_byte(ftHandle, GET_ADC4);
	
	read_byte(ftHandle, &c_lsb);
	read_byte(ftHandle, &c_msb);
		
	adc_value = ((double)c_msb*256 + (double)c_lsb) * V_REF/1024;
	
	syslog(LOG_NOTICE, "ADC4 %.4f \n", adc_value);
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}

int get_adc5(FT_HANDLE ftHandle)
{
	unsigned char c_msb, c_lsb, uC_status;
	double adc_value = 0;
	int status;
	
	syslog(LOG_NOTICE, "Get adc5 value\n");

	write_byte(ftHandle, GET_ADC5);
	
	read_byte(ftHandle, &c_lsb);
	read_byte(ftHandle, &c_msb);
		
	adc_value = ((double)c_msb*256 + (double)c_lsb) * V_REF/1024;
	
	syslog(LOG_NOTICE, "ADC5 %.4f \n", adc_value);
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}

int get_adc6(FT_HANDLE ftHandle)
{
	unsigned char c_msb, c_lsb, uC_status;
	double adc_value = 0;
	int status;
	
	syslog(LOG_NOTICE, "Get adc6 value\n");

	write_byte(ftHandle, GET_ADC6);
	
	read_byte(ftHandle, &c_lsb);
	read_byte(ftHandle, &c_msb);
		
	adc_value = ((double)c_msb*256 + (double)c_lsb) * V_REF/1024;
	
	syslog(LOG_NOTICE, "ADC6 %.4f \n", adc_value);
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}

int get_adc7(FT_HANDLE ftHandle)
{
	unsigned char c_msb, c_lsb, uC_status;
	double adc_value = 0;
	int status;
	
	syslog(LOG_NOTICE, "Get adc7 value\n");

	write_byte(ftHandle, GET_ADC7);
	
	read_byte(ftHandle, &c_lsb);
	read_byte(ftHandle, &c_msb);
		
	adc_value = ((double)c_msb*256 + (double)c_lsb) * V_REF/1024;
	
	syslog(LOG_NOTICE, "ADC7 %.4f \n", adc_value);
	
	// Wait for done message
	status = read_byte(ftHandle, &uC_status);
	if (status != OK)  			return status;
	if (uC_status != DONE)		return uC_ERR;
	
	return OK;
}

int set_reset_count(FT_HANDLE ftHandle)
{

	syslog(LOG_NOTICE, "Set reset count back to 0\n");
	write_byte(ftHandle, SET_RESET_COUNT);

	return 0;
}

int get_reset_count(FT_HANDLE ftHandle)
{
	int reset_count = 0;
	char c_reset_count;
	
	syslog(LOG_NOTICE, "Get reset count\n");
	write_byte(ftHandle, GET_RESET_COUNT);
//	reset_count = (int)read_byte(ftHandle);
	read_byte(ftHandle, &c_reset_count);
	reset_count = (int)c_reset_count;

	syslog(LOG_NOTICE, "%d resets\n\n", reset_count);
	syslog (LOG_NOTICE, "Resets = %d \n", reset_count);
	
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
		syslog(LOG_NOTICE, "Number of devices is %d \n", (int)numDevs);
	}

	if (numDevs > 0){
		devInfo = (FT_DEVICE_LIST_INFO_NODE*)
					malloc(sizeof(FT_DEVICE_LIST_INFO_NODE)*numDevs);
		ftStatus = FT_GetDeviceInfoList(devInfo, &numDevs);
		if (ftStatus == FT_OK){
			for (i = 0; i < numDevs; i++){
				syslog(LOG_NOTICE, "Dev %d:\n", i);
				syslog(LOG_NOTICE, "  Flags=0x%x\n", (int)devInfo[i].Flags);
				syslog(LOG_NOTICE, "  Type=0x%x\n", (int)devInfo[i].Type);
				syslog(LOG_NOTICE, "  IF=0x%x\n", (int)devInfo[i].ID);
				syslog(LOG_NOTICE, "  LocId=0x%x\n", (int)devInfo[i].LocId);
				syslog(LOG_NOTICE, "  SerialNumber=0x%x\n",(int) devInfo[i].SerialNumber);
				syslog(LOG_NOTICE, "  Description=%s\n", devInfo[i].Description);
				syslog(LOG_NOTICE, "  ftHandle=0x%x\n", (int)devInfo[i].ftHandle);
			}
		}
	}
	return 0;
}


/**********************/
/* ADC Data Functions */
/**********************/

int check_read_data (unsigned char *raw_data, DWORD n_raw, int *first, int *last)
{
	/* Search raw data for structure
		byte 1: counter
		byte 2: X,X,X,X,D3,D2,D1,D0  (X is meaningless)	| Channel 1
		byte 3: D11,10,9,8,7,6,5,4						| Channel 1
		byte 4: XX....D0								| Channel 2
		byte 5: D11...4									| Channel 2
		byte 6: ...										| Channel 3
		byte 7: ...										| Channel 3
		byte 8: ...										| Channel 4
		byte 9: ...										| Channel 4
		byte 10: counter+3 
		...
		..
		.
		note: Counter increment is +3 so that the overflow does not
		      happen after 256 times, because this could correlate
			  with a FIFO full state and thus a loss of data
	*/
	int first_temp = 0;
	int last_temp = 0;
	
	int i,j;
	for (i = 0; i < n_raw-9; i++){
		//syslog(LOG_NOTICE, "i = %d : %d - %d\n", i, raw_data[i], raw_data[i+9]);
		// find the first byte that could be a counter byte
		if (raw_data[i]-raw_data[i+9]==-3 || 
		    (raw_data[i] == 253 && raw_data[i+9] == 0 ) ||
			(raw_data[i] == 254 && raw_data[i+9] == 1 ) ||
			(raw_data[i] == 255 && raw_data[i+9] == 2 ) ){
			first_temp = i;
			last_temp = i; 
			// check if the following counter bytes realy are counter bytes
			for (j = i; j < n_raw-9; j = j + 9){
				if (raw_data[j]-raw_data[j+9]==-3 || 
					(raw_data[j] == 253 && raw_data[j+9] == 0 ) ||
					(raw_data[j] == 254 && raw_data[j+9] == 1 ) ||
					(raw_data[j] == 255 && raw_data[j+9] == 2 ) ){
					last_temp = j;
					//syslog(LOG_NOTICE, "j = %d : %d - %d\n", j, raw_data[j], raw_data[j+9]);
				}
				// if the checked byte is a counter no more, exit this search loop
				else {
					//syslog(LOG_NOTICE, "found %d %d\n", first_temp, last_temp);
					break;
				}
			}
		}
		// if counter bytes for more than 10 counted blocks where found, assume the
		// structure of the raw data was recognized correctly
		if (last_temp - first_temp > 10*9){
			break;
		}
	}
	
	// Check if one more 9-byte block was read but was not found by the 
	// above algorithm. It's not possible that more bytes where missed, but a
	// complete 9-byte block at the end will not have been recognized
	//
	// 8 data_bytes from counter at last_temp and 9 bytes for next block
	if (n_raw - last_temp > 8+9 &&
		(raw_data[last_temp]-raw_data[last_temp+9]==-3 || 
		(raw_data[last_temp] == 253 && raw_data[last_temp+9] == 0 ) ||
		(raw_data[last_temp] == 254 && raw_data[last_temp+9] == 1 ) ||
		(raw_data[last_temp] == 255 && raw_data[last_temp+9] == 2 )) ){
			last_temp = last_temp + 9;
		}	
			
	*first = first_temp;
	*last = last_temp;
			
	return OK;
}

// Convert the bitsteam described in check_read_data to a usefull format
// which holds the eight 12 bit valuse for the four recorded channels for
// the two polarizations
int raw2_i_q_h_v_data(unsigned char *raw_data, DATA_STRUCT *data, 
				  int N, int first, int last)
{
	int i;
	//int temp;
	int j = 0; // counter for data[j]
	
	if ((last-first-9)%18 != 0){
		syslog(LOG_NOTICE, "last - first is not mod 18!!\n");
	}
	
	//FILE *file;
	//file = fopen("out.dat","w");
	
	for (i = first; i<=last; i = i + 18){
		//syslog(LOG_NOTICE, "j = %d", j);
		
		// Assign the right 2 bytes and drop the  4 bits 
		// of the first byte int the stream because they are meaningless
		data->h_q_35->values[j] = (raw_data[i+1] & '\x0F') + 16*raw_data[i+2];
		data->h_i_35->values[j] = (raw_data[i+3] & '\x0F') + 16*raw_data[i+4];
		data->v_q_22->values[j] = (raw_data[i+5] & '\x0F') + 16*raw_data[i+6];
		data->v_i_22->values[j] = (raw_data[i+7] & '\x0F') + 16*raw_data[i+8];
		data->v_q_35->values[j] = (raw_data[i+10] & '\x0F') + 16*raw_data[i+11];
		data->v_i_35->values[j] = (raw_data[i+12] & '\x0F') + 16*raw_data[i+13];
		data->h_q_22->values[j] = (raw_data[i+14] & '\x0F') + 16*raw_data[i+15];
		data->h_i_22->values[j] = (raw_data[i+16] & '\x0F') + 16*raw_data[i+17];
				
		// Transform from unsigned raw data to signed
		if (data->h_q_35->values[j] > 2047) data->h_q_35->values[j] += -4096;
		if (data->h_i_35->values[j] > 2047) data->h_i_35->values[j] += -4096;
		if (data->v_q_22->values[j] > 2047) data->v_q_22->values[j] += -4096;
		if (data->v_i_22->values[j] > 2047) data->v_i_22->values[j] += -4096;
		if (data->v_q_35->values[j] > 2047) data->v_q_35->values[j] += -4096;
		if (data->v_i_35->values[j] > 2047) data->v_i_35->values[j] += -4096;
		if (data->h_q_22->values[j] > 2047) data->h_q_22->values[j] += -4096;
		if (data->h_i_22->values[j] > 2047) data->h_i_22->values[j] += -4096;
		
		// correct ADC offsets (calibration done in lab on 09.July.2010)
		data->h_q_35->values[j] -= ADC_OFFSET_Q_35;
		data->h_i_35->values[j] -= ADC_OFFSET_I_35;
		data->v_q_22->values[j] -= ADC_OFFSET_Q_22;
		data->v_i_22->values[j] -= ADC_OFFSET_I_22;
		data->v_q_35->values[j] -= ADC_OFFSET_Q_35;
		data->v_i_35->values[j] -= ADC_OFFSET_I_35;
		data->h_q_22->values[j] -= ADC_OFFSET_Q_22;
		data->h_i_22->values[j] -= ADC_OFFSET_I_22;
		
		//fprintf(file,"%d: %d %d %d %d %d %d %d %d\n",
		//	j, data[j].h_q_35, data[j].h_i_35, data[j].h_q_22, data[j].h_i_22,
		//	   data[j].v_q_35, data[j].v_i_35, data[j].v_q_22, data[j].v_i_22);
		j++;
	}
	//fclose(file);
	// number of samples derived from loop above
	data->N=j;
	return OK;
}


// OBSOLETE
// int raw2_i_q_data(unsigned char *raw_data, struct i_q_data *data, 
// 				  int N, int first, int last)
// {
// 	int i;
// 	//int temp;
// 	int j = 0; // counter for data[j]
// 	
// 	FILE *file;
// 	file = fopen("out.dat","w");
// 	
// 	for (i = first; i<=last; i = i + 9){
// 		//syslog(LOG_NOTICE, "%d : %d %x %x %x %x %x %x %x %x\n", i, raw_data[i], raw_data[i+1], 
// 		//	    raw_data[i+2], raw_data[i+3], raw_data[i+4], raw_data[i+5], 
// 		//	    raw_data[i+6], raw_data[i+7], raw_data[i+8]);
// 		//syslog(LOG_NOTICE, "%d : %d %d %d %d %d\n", i, raw_data[i], 
// 		//	    raw_data[i+1]+16*raw_data[i+2],	raw_data[i+3]+16*raw_data[i+4], 
// 		//	    raw_data[i+5]+16*raw_data[i+6], raw_data[i+7]+16*raw_data[i+8]);
// 		
// 		// Assign the right 2 bytes and drop the  4 bits 
// 		// of the first byte int the stream because they are meaningless
// 		
// 		data[j].q_35 = (double) (256*raw_data[i+1] & '\xF0') + raw_data[i+2];
// 		data[j].i_35 = (double) (256*raw_data[i+3] & '\xF0') + raw_data[i+4];
// 		data[j].q_22 = (double) (256*raw_data[i+5] & '\xF0') + raw_data[i+6];
// 		data[j].i_22 = (double) (256*raw_data[i+7] & '\xF0') + raw_data[i+8];
// 		
// 		// MAYBE BETTER TO SAFE THE RAW DATA DIRECTLY, NEEDS LESS SPACE
// 		// Convert to real voltage
// 		//data[j].q_35 = adc_transfer_funct(data[j].q_35);
// 		//data[j].i_35 = adc_transfer_funct(data[j].i_35);
// 		//data[j].q_22 = adc_transfer_funct(data[j].q_22);
// 		//data[j].i_22 = adc_transfer_funct(data[j].i_22);
// 		
// //		syslog(LOG_NOTICE, "%d : %x %x %f %d \n", i, raw_data[i+1], raw_data[i+2],
// //			   data[j].q_35, (256*raw_data[i+1]) + raw_data[i+2] );
// 		
// //		syslog(LOG_NOTICE, "%d: %6.2f %6.2f %6.2f %6.2f\n", j, 
// //			   data[j].q_35, data[j].i_35, data[j].q_22, data[j].i_22);
// 		
// 			fprintf(file,"%d: %6.2f %6.2f %6.2f %6.2f\n", j, 
// 				data[j].q_35, data[j].i_35, data[j].q_22, data[j].i_22);
// 
// 		j++;
// 	}
// 	fclose(file);
// 	return 0;
// }


double adc_transfer_funct(double raw_value)
{
	double lsb = 2.441406; // LSB (in mV) of the ADC max1609 V_ref intern
	double value = 0;
	
	if (raw_value >= 0 && raw_value <= 2047){
			value = 0.5*lsb + raw_value*lsb;
	}
	else if (raw_value >= 2048 && raw_value <= 4095){
			value = +0.5*lsb + (raw_value - 4096)*lsb;
	}
	else{
		syslog(LOG_NOTICE, "ADC TRANSFER ERROR\n");
	}
// ADD ERROR CHECKING	
	return value;
}


/********************/
/* HELPER FUNCTIONS */
/********************/

// function to get single bytes of a int variable. LSB is byte[0].
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

// Allocate memory for data struct and return its pointer
DATA_STRUCT *create_data_struct(int N)
{
	DATA_STRUCT *data = malloc(sizeof *data);
	//data = (DATA_STRUCT*) malloc(sizeof (DATA_STRUCT)); // above one is better
	//DATA_POINTS *p = malloc(sizeof *p);
	//data->h_i_22 = p;
	data->h_i_22 = malloc(sizeof *(data->h_i_22));
	data->h_q_22 = malloc(sizeof *(data->h_q_22));
	data->h_i_35 = malloc(sizeof *(data->h_i_35));
	data->h_q_35 = malloc(sizeof *(data->h_q_35));
	data->v_i_22 = malloc(sizeof *(data->v_i_22));
	data->v_q_22 = malloc(sizeof *(data->v_q_22));
	data->v_i_35 = malloc(sizeof *(data->v_i_35));
	data->v_q_35 = malloc(sizeof *(data->v_q_35));

	data->h_a_22 = malloc(sizeof *(data->h_a_22));
	data->h_p_22 = malloc(sizeof *(data->h_p_22));
	data->h_a_35 = malloc(sizeof *(data->h_a_35));
	data->h_p_35 = malloc(sizeof *(data->h_p_35));
	data->v_a_22 = malloc(sizeof *(data->v_a_22));
	data->v_p_22 = malloc(sizeof *(data->v_p_22));
	data->v_a_35 = malloc(sizeof *(data->v_a_35));
	data->v_p_35 = malloc(sizeof *(data->v_p_35));
	
	data->h_i_22->values = malloc(sizeof *(data->h_i_22->values) * N);
	data->h_q_22->values = malloc(sizeof *(data->h_q_22->values) * N);
	data->h_i_35->values = malloc(sizeof *(data->h_i_35->values) * N);
	data->h_q_35->values = malloc(sizeof *(data->h_q_35->values) * N);
	data->v_i_22->values = malloc(sizeof *(data->v_i_22->values) * N);
	data->v_q_22->values = malloc(sizeof *(data->v_q_22->values) * N);
	data->v_i_35->values = malloc(sizeof *(data->v_i_35->values) * N);
	data->v_q_35->values = malloc(sizeof *(data->v_q_35->values) * N);
	
	data->h_a_22->values = malloc(sizeof *(data->h_a_22->values) * N);
	data->h_p_22->values = malloc(sizeof *(data->h_p_22->values) * N);
	data->h_a_35->values = malloc(sizeof *(data->h_a_35->values) * N);
	data->h_p_35->values = malloc(sizeof *(data->h_p_35->values) * N);
	data->v_a_22->values = malloc(sizeof *(data->v_a_22->values) * N);
	data->v_p_22->values = malloc(sizeof *(data->v_p_22->values) * N);
	data->v_a_35->values = malloc(sizeof *(data->v_a_35->values) * N);
	data->v_p_35->values = malloc(sizeof *(data->v_p_35->values) * N);

	return data;
}

// Free alocated memory of data struct
int free_data_struct(DATA_STRUCT *data)
{
	free(data->h_i_22->values);
	free(data->h_q_22->values);
	free(data->h_i_35->values);
	free(data->h_q_35->values);
	free(data->v_i_22->values);
	free(data->v_q_22->values);
	free(data->v_i_35->values);
	free(data->v_q_35->values);
	free(data->h_i_22);
	free(data->h_q_22);
	free(data->h_i_35);
	free(data->h_q_35);
	free(data->v_i_22);
	free(data->v_q_22);
	free(data->v_i_35);
	free(data->v_q_35);
	
	free(data->h_a_22->values);
	free(data->h_p_22->values);
	free(data->h_a_35->values);
	free(data->h_p_35->values);
	free(data->v_a_22->values);
	free(data->v_p_22->values);
	free(data->v_a_35->values);
	free(data->v_p_35->values);
	free(data->h_a_22);
	free(data->h_p_22);
	free(data->h_a_35);
	free(data->h_p_35);
	free(data->v_a_22);
	free(data->v_p_22);
	free(data->v_a_35);
	free(data->v_p_35);
	
	free(data);
	
	return OK;
}


/////////////
// M A I N //
/////////////
// int main(int argc, char *argv[])
// {
// 	syslog(LOG_NOTICE, "\n");
// 
// 	FILE * fh;
// 	int j,i;
// 	
// 	// Variables for ftdi usb device
// 	FT_HANDLE ftHandle;
// 	int status;
// 	
// 	// Open FTDI USB device
// 	status = open_device(&ftHandle);
// 	if (status != OK)
// 	{
// 		syslog(LOG_NOTICE, "Open device failed. Exit.\n\n");
// 		exit(ERR);
// 	}
// 
// 	// Config device
// 	FT_SetUSBParameters(ftHandle, 64000, 0);
// 
// 	// Setting latency to 2 leads to com problems, but
// 	// it should be as short as possible...
// 	FT_SetLatencyTimer(ftHandle, 0);
// 	FT_SetDtr(ftHandle);
// 	FT_SetRts(ftHandle);
// 	FT_SetFlowControl(ftHandle, FT_FLOW_RTS_CTS, 0, 0);
// 	FT_SetTimeouts(ftHandle, 2000, 2000);
// 	
// 	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
// 	
// 
// 	// Variables for socket hadling
// 	int socket_fd, client_socket_fd, t, len;
// 	struct sockaddr_un host_name, client_name;
// 	char str[100];
// 	
// 	// Open a socket
// 	if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
// 		perror("socket");
// 		exit(1);
// 	}
// 	// Bind the socket to an address
// 	host_name.sun_family = AF_UNIX;
// 	strcpy(host_name.sun_path, SOCK_PATH);
// 	unlink(host_name.sun_path);
// 	//len = strlen(local.sun_path) + sizeof(local.sun_familiy);
// 	if (bind(socket_fd, &host_name, SUN_LEN(&host_name)) == -1) {
// 		perror("bind");
// 		exit(1);
// 	}
// 
// 	// Listen for one connection
// 	if (listen(socket_fd, 1) == -1) {
// 		perror("listen");
// 		exit(1);
// 	}
// 
// 	do {
// 		socklen_t client_name_len;
// 		int length;
// 		char* text;
// 
// 		// Accept a connection
// 		client_socket_fd = accept(socket_fd, &client_name, &client_name_len);
// 		// read the length of text message
// 		if (read(client_socket, &length, sizeof(length)) == 0) {
// 			perro("read 0");
// 		}
// 		// Allocate buffer
// 		text = (char*) malloc(length);
// 		// Read the text
// 		read(client_socket, text, length);
// 		syslog(LOG_NOTICE, "%s\n", text);
// 		free(text);
// 	} while (1);
// 	
// 	// Close our end of connection
// 	close(socket_fd);
// 
// //	sleep(1);
// //	usleep(10000);
// 
// 	if (argc == 3 && strcmp(argv[1],"set_case_temp") == 0)
// 		set_case_temp(ftHandle,atoi(argv[2]));
// 	
// 	else if (argc == 2 && strcmp(argv[1],"get_case_temp") == 0)
// 		get_case_temp(ftHandle);
// 		
// 	else if (argc == 2 && strcmp(argv[1],"set_reset_count") == 0)
// 		set_reset_count(ftHandle);
// 	
// 	else if (argc == 2 && strcmp(argv[1],"get_reset_count") == 0)
// 		get_reset_count(ftHandle);
// 
// 	else if (argc == 3 && strcmp(argv[1],"set_pw") == 0)
// 		set_pw(ftHandle, atoi(argv[2]));
// 
// 	else if (argc == 3 && strcmp(argv[1],"set_n_samples") == 0)
// 		set_num_samples(ftHandle, atoi(argv[2]));
// 	
// 	else if (argc == 3 && strcmp(argv[1],"set_delay") == 0)
// 		set_delay(ftHandle, atoi(argv[2]));
// 
// 	else if (argc == 3 && strcmp(argv[1],"set_mode") == 0){
// 		if (strcmp(argv[2],"CROSSPOL") == 0)
// 			set_mode(ftHandle, CROSSPOL);
// 		else if (strcmp(argv[2],"COPOL") == 0)
// 			set_mode(ftHandle, COPOL);
// 		else if (strcmp(argv[2],"RADIOMETER") == 0)
// 			set_mode(ftHandle, RADIOMETER);
// 		else if (strcmp(argv[2],"CALIBRATE") == 0)
// 			set_mode(ftHandle, CALIBRATE);
// 		else{
// 			printf ("Unknown mode. Exit \n");
// 			exit(1) ;
// 		}
// 	}
// 
// 	else if (argc == 3 && strcmp(argv[1],"start") == 0)
// 		start_msrmnt(ftHandle, atoi(argv[2]));
// 
// 	else if (argc ==2 && strcmp(argv[1],"get_device_list") == 0)
// 		get_device_list_info();
// 
// 	else if (argc == 1){
// 		set_pw(ftHandle, 100);
// 		syslog(LOG_NOTICE, "\n");
// 		sleep(1);
// 		set_num_samples(ftHandle, 4000);
// 		syslog(LOG_NOTICE, "\n");
// 		sleep(1);
// 		set_delay(ftHandle, 600);
// 		syslog(LOG_NOTICE, "\n");
// 		sleep(1);
// 		set_mode(ftHandle, COPOL);
// 		syslog(LOG_NOTICE, "\n");
// 		sleep(1);
// 		start_msrmnt(ftHandle, 40000);
// 	}
// 		
//     else if (argc == 2 && strcmp(argv[1],"-h") == 0){
// 		printf ("Usage: usb_control set_pw VALUE \n");
// 		printf ("                   set_n_samples VALUE\n");
// 		printf ("                   set_delay VALUE\n");
// 		printf ("                   set_mode (COPOL|CROSSPOL|");
// 		printf (				   		     "CALIBRATE|RADIOMETER)\n");
// 		printf ("                   start N_SAMPLES\n\n");
// 		printf ("                   get_case_temp \n");
// 		printf ("                   set_case_temp VALUE \n\n");
// 		printf ("                   get_device_list \n\n");
// 		exit(1);
// 	}
// 
// 	else if (argc == 2 && strcmp(argv[1],"read") == 0){
// 		read_byte(ftHandle);
// 	}
// 
// 	else if (argc == 3 && strcmp(argv[1],"write") == 0){
// 		write_byte(ftHandle,(char)atoi(argv[2]));
// 	}
// 
// 	else if (argc == 2 && strcmp(argv[1],"purge") == 0){
// 		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
// 	}
// 
// 	else{
// 		printf ("Unknown command. Type: usb_control -h for usage\n");
// 		exit(1);
// 	}
// 
// 	FT_Close(ftHandle);
// 	
// 	return 0;
// }
	

