#ifndef USB_CONTROL_H
#define USB_CONTROL_H

#include <time.h>
#include "ftd2xx.h"

/////////////
// GLOBALS //
/////////////

int slow_loop_keep_running;

//////////////
// COMMANDS //
//////////////

// Commands for uC sent via USB
//
// for CPLD settings
#define SET_PW			0x02
#define SET_DELAY		0x04
#define SET_NUM_SAMPLES 0x06
#define SET_MODE 		0x08
#define SET_ADC			0x0A
#define	GET_STATUS		0x0C
#define SET_POL_PRECEDE 0x12
#define SET_ATTEN22		0x14
#define SET_ATTEN35		0x18
#define GET_LOCK		0xD0
// for CPLD measurements
#define START_MSRMNT 	0x1E
#define START_SLOW_LOOP 0x2E
#define STOP_SLOW_LOOP	0x3E
// for DS1621 thermostat
#define SET_CASE_TEMP	0x07
#define GET_CASE_TEMP	0x17
#define SET_BOARD_TEMP  0x03
#define GET_BOARD_TEMP  0x13
// for uC itself
#define SET_RESET_COUNT 0x09
#define GET_RESET_COUNT 0x19
// for uC ADCs
#define GET_ADC4		0xA4
#define GET_ADC5		0xA5
#define GET_ADC6		0xA6
#define GET_ADC7		0xA7
#define V_REF			5		// Reference voltage at AVCC pin

// CPLD modes
#define COPOL			0x02
#define CROSSPOL		0x04
#define CALIBRATE		0x06
#define	RADIOMETER		0x08

// Function return codes, also used for uC-communication
#define ERR				0xEE
#define CPLD_BUSY		0xBB	
#define OK				0xF0
#define DONE			0xF1
#define USB_ERR			0xE1
#define ARG_ERR			0xE2
#define uC_ERR			0xE3

// USB serial number
// Must be altered if a new FTDI device is used
#define USB_SERIAL_NUM	"0x804c85c"

// Structure for passing args to threaded FT_Read
//!! add a return status
struct thread_args{
	FT_HANDLE ftHandle;
	unsigned char *pcBufRead;
	int read_buffer_size;
	DWORD dwBytesRead;
};

// Struct for all pulse generator settings
typedef struct {
	int		n_samples; 	// Number of smaples
	int		delay;		// Delay between TX and RX pulse
	int		pw;			// Pulse width of TX pulse
	int		mode;		// Opperation mode of pulse generator
	int		atten22_1;	// Attenuation setting 1 for 22 GHz
	int		atten22_2;	// Attenuation setting 2 for 22 GHz
	int		atten35_1;	// Attenuation setting 1 for 35 GHz
	int		atten35_2;	// Attenuation setting 2 for 35 GHz
} PULSE_CONF;

typedef struct{
	double mean;
	double std_dev;
	short *values;
} DATA_POINTS;

typedef struct{
//  PULSE_CONF settings;
	time_t timestamp;
	int N;	// should be n_samples/2
	DATA_POINTS* h_i_22;
	DATA_POINTS* h_q_22;
	DATA_POINTS* h_i_35;
	DATA_POINTS* h_q_35;
	DATA_POINTS* v_i_22;
	DATA_POINTS* v_q_22;
	DATA_POINTS* v_i_35;
	DATA_POINTS* v_q_35;
} DATA_STRUCT;

// FOR TESTS WITHOUT MALLOC
//
// typedef struct{
// //  PULSE_CONF settings;
// 	time_t timestamp;
// 	int N;	// should be n_samples/2
// 	DATA_POINTS h_i_22;
// 	DATA_POINTS h_q_22;
// 	DATA_POINTS h_i_35;
// 	DATA_POINTS h_q_35;
// 	DATA_POINTS v_i_22;
// 	DATA_POINTS v_q_22;
// 	DATA_POINTS v_i_35;
// 	DATA_POINTS v_q_35;
// } DATA_STRUCT;

struct i_q_data{
	double i_22;
	double q_22;
	double i_35;
	double q_35;
};

// struct holding values if I,Q data for each frequency and polarization
struct i_q_h_v_data{
	short h_i_22;
	short h_q_22;
	short h_i_35;
	short h_q_35;
	short v_i_22;
	short v_q_22;
	short v_i_35;
	short v_q_35;
};

struct data_mean{
	double h_i_22;
	double h_q_22;
	double h_i_35;
	double h_q_35;
	double v_i_22;
	double v_q_22;
	double v_i_35;
	double v_q_35;
};

///////////////
// FUNCTIONS //
///////////////

// Open FTDI USB device
int open_device(FT_HANDLE *handle);

// Read a byte via USB
int read_byte(FT_HANDLE ftHandle, char* pcBufRead);
// old version
//char read_byte(FT_HANDLE ftHandle);

// Write a byte via USB and check its transmission
// by reading back hopefully the same byte
int write_byte(FT_HANDLE ftHandle, char byte);

// Write a byte array of size 'size' and after complete write,
// read it back for transmission control
int write_bytes(FT_HANDLE ftHandle, char *bytes, int size);

// Set the number of samples for the measurement
int set_num_samples(FT_HANDLE ftHandle, int n_samples);

// Set delay for range gating
int set_delay(FT_HANDLE ftHandle, int delay);

// Set pulse width of transmitted pulse
int set_pw(FT_HANDLE ftHandle, int int_pw);

// Set measurement mode
int set_mode(FT_HANDLE ftHandle,int int_mode);

// Set delay of ADC trigger pulse after RX pulse
int set_adc(FT_HANDLE ftHandle, int adc);
	
// Set time the polarizer switch precedes the TX pulse
int set_pol_precede(FT_HANDLE ftHandle, int precede);

// Get status from CPLD (lock bits from PLOs)
int get_status(FT_HANDLE ftHandle);

// Set attenuatores for 22 GHz system
int set_atten22(FT_HANDLE ftHandle, int atten1, int atten2);

// Set attenuatores for 35 GHz system
int set_atten35(FT_HANDLE ftHandle, int atten1, int atten2);

void *FT_Read_thread(void *args);
		
//int start_msrmnt(FT_HANDLE ftHandle,int n_samples,struct i_q_h_v_data* data,int* size);
int start_msrmnt(FT_HANDLE ftHandle,int n_samples,DATA_STRUCT* data);

void *start_slow_loop(void *args);

int stop_slow_loop(FT_HANDLE ftHandle);

int set_case_temp(FT_HANDLE ftHandle,int t);

int get_case_temp(FT_HANDLE ftHandle);

int set_board_temp(FT_HANDLE ftHandle,int t);

int get_board_temp(FT_HANDLE ftHandle);

int get_lock(FT_HANDLE ftHandle);

int get_adc4(FT_HANDLE ftHandle);
int get_adc5(FT_HANDLE ftHandle);
int get_adc6(FT_HANDLE ftHandle);
int get_adc7(FT_HANDLE ftHandle);

int set_reset_count(FT_HANDLE ftHandle);

int get_reset_count(FT_HANDLE ftHandle);

int get_device_list_info();

int check_read_data (unsigned char *raw_data, DWORD n_raw, int *first, int *last);

// OBSOLETE
int raw2_i_q_data(unsigned char *raw_data, struct i_q_data *data, 
				  int N, int first, int last);

int raw2_i_q_h_v_data(unsigned char *raw_data, DATA_STRUCT *data, 
				  int N, int first, int last);
				  
double adc_transfer_funct(double raw_value);

// Helper function to get single bytes of a int variable.
// LSB is byte[0].
int int_to_bytes(unsigned int n, char *byte);

// Helper function to get int value of 4 byte char array.
// Only uses the LSB because the communication with the
// uC is limited to 8 bit words. The rest of the bytes of
// the char are meaningless
unsigned int bytes_to_int(char *chars);

DATA_STRUCT *create_data_struct(int N);

int free_data_struct(DATA_STRUCT *data);
				  
#endif /* USB_CONTROL_H */
	

