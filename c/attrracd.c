/*
 * attrracd.c - Master control daemon for ATTRRAC
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <syslog.h>
#include <signal.h>
#include <math.h>
#include <pthread.h>

/* for socket things */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <string.h>

#include "ftd2xx.h"

#include "helper.h"
#include "attrracd.h"
#include "usb_control.h"


/* G L O B A L S */

/* Handle for USB device */
FT_HANDLE ftHandle;

/* flag changed by SIGINT that stops the main loop if == 0 */
int keep_running = 1;

/* Struct for all pulse generator settings */
// struct pulse_conf{
// 	int		n_samples; 	// Number of samples
// 	int		pw;	   	// Pulse width of TX pulse
// 	int		delay;		// Delay between TX and RX pulse
// 	int 		pol_preced; 	// time the polarization switching precedes TX
// 	int     	adc_delay;	// delay of ADC after RX
// 	int		mode;		// Opperation mode of pulse generator
// 	int		atten22_1;	// Attenuation setting 1 for 22 GHz
// 	int		atten22_2;	// Attenuation setting 2 for 22 GHz
// 	int		atten35_1;	// Attenuation setting 1 for 35 GHz
// 	int		atten35_2;	// Attenuation setting 2 for 35 GHz
// } pulse_conf;

PULSE_CONF pulse_conf;

/* F U N C T I O N S */

/* Function getting called by signal handler when receiving SIGINT */
void terminate()
{
	syslog (LOG_NOTICE, "Got SIGINT.\n");
	keep_running = 0;
}

int set_default(FT_HANDLE ftHandle)
{
	int status;
	
	status = set_num_samples(ftHandle, 512);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.n_samples = 512;
	
	status = set_delay(ftHandle, 222);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.delay = 222;
	
	status = set_pw(ftHandle, 10);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.pw = 10;
	
	status = set_adc(ftHandle, 1);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.adc_delay = 1;
	
	status = set_pol_precede(ftHandle, 0);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.pol_preced = 0;
	
	status = set_mode(ftHandle, COPOL);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.mode = COPOL;
	
	status = set_atten22(ftHandle, 0, 0);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.atten22_1 = 0;
	pulse_conf.atten22_2 = 0;
	
	status = set_atten35(ftHandle, 0, 0);
	if (status != OK){
		printf("error %d\n", status);
		return status;
	}
	pulse_conf.atten35_1 = 0;
	pulse_conf.atten35_2 = 0;
	
	return status;
}


/* State machine for commands sent via socket */
int handle_socket_con(int fdSock)
{
	char message1[MAX_LENGTH];		// socket messages
	char message2[MAX_LENGTH];
	char message3[MAX_LENGTH];
	int status = OK;				// function return status
	
	read(fdSock, message1, MAX_LENGTH);
	read(fdSock, message2, MAX_LENGTH);
	read(fdSock, message3, MAX_LENGTH);
	
	if (strcmp(message1,"set_case_temp") == 0)
		set_case_temp(ftHandle,atoi(message2));
			
	else if (strcmp(message1,"get_case_temp") == 0)
		get_case_temp(ftHandle);
	
	else if (strcmp(message1,"set_board_temp") == 0)
		set_board_temp(ftHandle,atoi(message2));
			
	else if (strcmp(message1,"get_board_temp") == 0)
		get_board_temp(ftHandle);
				
	else if (strcmp(message1,"set_reset_count") == 0)
		set_reset_count(ftHandle);
			
	else if (strcmp(message1,"get_reset_count") == 0)
		get_reset_count(ftHandle);
		
	else if (strcmp(message1,"set_default") == 0){
		status = set_default(ftHandle);
		if (status != OK) printf("error %d\n", status);
	}
	
	else if (strcmp(message1,"set_pw") == 0){
		status = set_pw(ftHandle, atoi(message2));
		if (status != OK) printf("error %d\n", status);
		pulse_conf.pw = atoi(message2);
	}
		
	else if (strcmp(message1,"set_n_samples") == 0){
		status = set_num_samples(ftHandle, atoi(message2));
		if (status != OK) printf("error %d\n", status);
		pulse_conf.n_samples = atoi(message2);
	}
			
	else if (strcmp(message1,"set_delay") == 0){
		if (atoi(message2) < pulse_conf.pw + 5){
			printf("DELAY TO SMALL. DELAY MUST BE AT LEAST PW+5\n");
			return ARG_ERR;
		}
		status = set_delay(ftHandle, atoi(message2));
		if (status != OK) printf("error %d\n", status);
		pulse_conf.delay = atoi(message2);
	}
	
	else if (strcmp(message1,"set_adc_delay") == 0){
		status = set_adc(ftHandle, atoi(message2));
		if (status != OK) printf("error %d\n", status);
	}
	
	else if (strcmp(message1,"set_pol_precede") == 0){
		status = set_pol_precede(ftHandle, atoi(message2));
		if (status != OK) printf("error %d\n", status);
	}
	
	else if (strcmp(message1,"get_status") == 0){
		status = get_status(ftHandle);
		if (status != OK) printf("error %d\n", status);
	}
	
	else if (strcmp(message1,"set_atten22") == 0){
		status = set_atten22(ftHandle, atoi(message2), atoi(message3));
		if (status != OK) printf("error %d\n", status);
		pulse_conf.atten22_1 = atoi(message2);
		pulse_conf.atten22_2 = atoi(message3);
	}
	
	else if (strcmp(message1,"set_atten35") == 0){
		status = set_atten35(ftHandle, atoi(message2), atoi(message3));
		if (status != OK) printf("error %d\n", status);
		pulse_conf.atten35_1 = atoi(message2);
		pulse_conf.atten35_2 = atoi(message3);
	}
	
	else if (strcmp(message1,"set_loop_freq") == 0){
		status = set_loop_freq(ftHandle, atoi(message2));
		if (status != OK) printf("error %d\n", status);
	}	
	
	else if (strcmp(message1,"set_mode") == 0){
		if (strcmp(message2,"CROSSPOL") == 0){
			status = set_mode(ftHandle, CROSSPOL);
			if (status != OK) printf("error %d\n", status);
			pulse_conf.mode = CROSSPOL;
		}
		else if (strcmp(message2,"COPOL") == 0){
			status = set_mode(ftHandle, COPOL);
			if (status != OK) printf("error %d\n", status);
			pulse_conf.mode = COPOL;
		}
		else if (strcmp(message2,"RADIOMETER") == 0){
			status = set_mode(ftHandle, RADIOMETER);
			if (status != OK) printf("error %d\n", status);
			pulse_conf.mode = RADIOMETER;
		}
		else if (strcmp(message2,"CALIBRATE") == 0){
			status = set_mode(ftHandle, CALIBRATE);
			if (status != OK) printf("error %d\n", status);
			pulse_conf.mode = CALIBRATE;
		}
		else{
			syslog (LOG_NOTICE, "Unknown mode.\n");
			return ARG_ERR;
		}
	}
		
	else if (strcmp(message1,"start") == 0){
		int N = pulse_conf.n_samples/2;			// n/2 samples per polarization
		int n_bytes_to_read = 9*pulse_conf.n_samples; 	// 9 bytes data per polarization
		
		DATA_STRUCT *data = create_data_struct(N);
		
		time_t t_now;
		struct tm *ts;
		char filename[23], sys_string[100];
		
		int max_tries = 3;
		int tries = 0;
		int retry = 0;
		
		// read burst and retry if it fails
		for(tries=0; tries<max_tries; tries++)
		{
			syslog(LOG_NOTICE, "n_of_tries %d\n", tries);
		 
			retry = 0;
 
			// Start measurement and read data
			status = start_msrmnt(ftHandle, n_bytes_to_read, data);
			if (status != OK){
				syslog(LOG_ERR, "start_msrmnt: error %d\n", status);
				FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
				retry = 1;
			}
		  
			// check if number of bytes read is OK
			if(data->N != pulse_conf.n_samples/2){
				syslog(LOG_ERR, "n_bytes_read wrong\n");
				FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
				retry = 1;
			}
			
			// if no retry is needed
			if (retry == 0){
				// open file with timestamped filename
				time(&t_now);
				ts =gmtime(&t_now);
				strftime(filename, 23, "iq_%Y%m%d_%H%M%S.dat", ts);
				FILE *iq_file = fopen(filename,"w");

				// write header
				fprintf(iq_file, "# 35_H_I 35_H_Q 22_H_I 22_H_Q");
				fprintf(iq_file, " 35_V_I 35_V_Q 22_V_I 22_V_Q\n");				
	
				// write data
				int j = 0;
				for (j = 0; j< data->N; j++){	
					fprintf(iq_file,"%6d %6d %6d %6d %6d %6d %6d %6d %6d\n", j, 
							data->h_i_35->values[j], data->h_q_35->values[j],
							data->h_i_22->values[j], data->h_q_22->values[j], 
							data->v_i_35->values[j], data->v_q_35->values[j],
							data->v_i_22->values[j], data->v_q_22->values[j]);
				}
				// close file
				fclose(iq_file);
				// move file
				sprintf(sys_string, "mv %s /root/data_to_send/", filename);
				system(sys_string);
				// exit loop, because nomore try is needed
				break;
			}
		}
		free_data_struct(data);
	}
	
	else if (strcmp(message1,"radar") == 0){
		int N = pulse_conf.n_samples/2;					
		int n_bytes_to_read = 9*pulse_conf.n_samples;
		
		DATA_STRUCT *data = create_data_struct(N);
		
		// test without malloc
		//DATA_STRUCT d;
		//DATA_STRUCT *data = &d;
		
		time_t t_now;
		struct tm *ts;
		
		char filename[23], sys_string[100];		
		
		// open file with timestamped filename
		time(&t_now);
		ts =gmtime(&t_now);
		strftime(filename, 27, "radar_%Y%m%d_%H%M_%S.dat", ts);
		FILE *rad_file = fopen(filename,"w");
				
		//FILE *rad_file = fopen("rad_data.dat","w"); // file for I_Q_data
	
		fprintf(rad_file, "# delay 35_H_A 35_H_P 22_H_A 22_H_P");
		fprintf(rad_file, " 35_V_A 35_V_P 22_V_A 22_V_P\n");
		
		int delay;
		for (delay = pulse_conf.pw + 6; delay < 235; delay += 2){
			status = set_delay(ftHandle, delay);
			if (status != OK) printf("error %d\n", status);
			pulse_conf.delay = delay;
				
			status = start_msrmnt(ftHandle, n_bytes_to_read, data);
			if (status != OK){
			printf("error %d\n", status);
			free(data);
			return ERR;
			}
				
			mean(data, SKIP);
			std_dev(data, SKIP);
			
			printf("%6d %6d %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f ",
					pulse_conf.delay, data->N,
					data->h_a_22->mean, data->h_p_22->mean, 
					data->h_a_35->mean, data->h_p_35->mean,
					data->v_a_22->mean, data->v_p_22->mean, 
					data->v_a_35->mean, data->v_p_35->mean);
			printf("%5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f\n",
 					data->h_a_22->std_dev, data->h_p_22->std_dev, 
 					data->h_a_35->std_dev, data->h_p_35->std_dev,
 					data->v_a_22->std_dev, data->v_p_22->std_dev, 
 					data->v_a_35->std_dev, data->v_p_35->std_dev);					
			
			// Write to file
			fprintf(rad_file, "%6d %6d %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f ",
					pulse_conf.delay, data->N, 
					data->h_a_35->mean, data->h_p_35->mean,
					data->h_a_22->mean, data->h_p_22->mean,  
					data->v_a_35->mean, data->v_p_35->mean,
					data->v_a_22->mean, data->v_p_22->mean);	
			fprintf(rad_file, "%5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f %5.1f\n",
 					data->h_a_22->std_dev, data->h_p_22->std_dev, 
 					data->h_a_35->std_dev, data->h_p_35->std_dev,
 					data->v_a_22->std_dev, data->v_p_22->std_dev, 
 					data->v_a_35->std_dev, data->v_p_35->std_dev);					
		}
		fclose(rad_file);
		free_data_struct(data);
		// move data to transfer directory
		sprintf(sys_string, "mv %s /root/data_to_send/", filename);
		system(sys_string);
	}
	
	else if (strcmp(message1,"start_slow_loop") == 0){
		pthread_t slow_loop_thread;
		struct thread_args a;
		a.ftHandle = ftHandle;
		a.read_buffer_size = pulse_conf.n_samples;
		a.conf = &pulse_conf;
		
		struct sched_param param;
		memset(&param, 0, sizeof(param));
		param.sched_priority = 95;
		int policy = SCHED_OTHER;
		
		int rc;
		
		// Start a thread for the slow measurement loop.
		// Do not wait for it to return.
		// It can be stopped by calling stop_slow_loop.
		rc = pthread_create(&slow_loop_thread, NULL, start_slow_loop, &a);
		rc = pthread_setschedparam(slow_loop_thread, policy, &param);
	}
	
	else if (strcmp(message1,"stop_slow_loop") == 0)
		stop_slow_loop(ftHandle);
	
	else if(strcmp(message1,"get_lock") == 0)
		get_lock(ftHandle);
	
	else if (strcmp(message1,"get_adc4") == 0)
		get_adc4(ftHandle);

	else if (strcmp(message1,"get_adc5") == 0)
		get_adc5(ftHandle);
	
	else if (strcmp(message1,"get_adc6") == 0)
		get_adc6(ftHandle);
	
	else if (strcmp(message1,"get_adc7") == 0)
		get_adc7(ftHandle);
	
	else if (strcmp(message1,"get_device_list") == 0)
		get_device_list_info();
		
	else if (strcmp(message1,"read") == 0){
		char byte;
		read_byte(ftHandle, &byte);
	}
		
	else if (strcmp(message1,"write") == 0){
		write_byte(ftHandle,(char)atoi(message2));
	}
		
	else if (strcmp(message1,"purge") == 0){
		FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
	}
		
	else if (strcmp(message1,"quit") == 0){
		keep_running = 0;
	}
	else{
		syslog (LOG_NOTICE, "Unknown command.\n");
		return ARG_ERR;
	}
	
	return status;
}


/***********/
/* M A I N */
/***********/
int main()
{
	struct sockaddr_in strAddr;
	socklen_t lenAddr;
	int fdSock;
	int fdConn;
	int status;
	
	/* delete unfinished data files */
	system("rm *.dat");
	
	/* open log */
	setlogmask (LOG_UPTO (LOG_NOTICE));
	openlog ("attrracd", LOG_CONS | LOG_NDELAY, LOG_LOCAL1);
	syslog (LOG_NOTICE, "##### Program started by User %d ####", getuid ());
	 	
	/* look for and create lock file */
	int fdlock = get_lock_file(MASTERD_LOCK_FILE);
	if (fdlock == -1){
		syslog (LOG_ERR, "Lock file already exists. Is masterd "
				 "still running? Exiting.");
		closelog();
		exit(1);
	}
	
	/* define the signal handler */
	struct sigaction act;
	/* function "terminate()" will be called if... */
	act.sa_handler = terminate;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	/* ...we receive SIGNINT */
	sigaction(SIGINT, &act, 0);
	
	/* open USB device */
	status = open_device(&ftHandle);
	if (status != OK)
	{
		syslog(LOG_ERR, "Open device failed. Exit.");
		/* erase lockfile */
		close(fdlock);
		unlink(MASTERD_LOCK_FILE);
		/* close syslog */
		closelog();
		exit(1);
	}
	// Config device
	FT_SetUSBParameters(ftHandle, 64000, 0);
	// Setting latency to 2 ms leads to com problems, but
	// it should be as short as possible...
	//
	// Setting it to 0 ms worked well with libftdi.so.0.47.
	//
	// With 1.0.2 we get a lot of read errors. Trying it now with 2 ms
	//
	FT_SetLatencyTimer(ftHandle, 0);
	FT_SetDtr(ftHandle);
	FT_SetRts(ftHandle);
	FT_SetFlowControl(ftHandle, FT_FLOW_RTS_CTS, 0, 0);
	FT_SetTimeouts(ftHandle, 15000, 15000);
	FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
		
	/* daemonize */
	
	/* read config file */
//	masterd_conf_struct masterd_conf;
//	read_masterd_config(&masterd_conf);
	
	/* start or restart daqd and sendd */
	// quick and dirty... --> rewrite?! 
//	system ("killall -SIGINT daqd");
//	syslog (LOG_NOTICE, "starting daqd...");
//	system ("./daqd &");

	// open Inet_socket
	if ((fdSock=socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		syslog (LOG_ERR, "Could not open socket. Exit.");
		/* erase lockfile */
		close(fdlock);
		unlink(MASTERD_LOCK_FILE);
		/* close usb device */
		FT_Close(ftHandle);
		/* close syslog */
		closelog();
		exit(1);
	}
	// set inet socket
	strAddr.sin_family=AF_INET;
	strAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	//strAddr.sin_addr.s_addr = htons(INADDR_ANY);
	strAddr.sin_port = htons(1111);
	lenAddr=sizeof(struct sockaddr);
	// bind socket to port
	if (bind(fdSock, (struct sockaddr*)&strAddr, lenAddr) != 0) {
		syslog (LOG_ERR, "Could not bind socket. Exit");
		exit(1);
	}
	// start listening
	if (listen(fdSock, 5) != 0) {
		syslog (LOG_ERR, "Socket could not listen. Exit");
		exit(1);
	}
	
	lenAddr = sizeof(struct sockaddr_in);
	
	/* main loop : runs as long as the signal handler did *
	 * not receive a SIGINT to change "keep_running" to 0 */
	while(keep_running == 1){
		usleep(5000);		
		/* accept socket connection */
		fdConn=accept(fdSock, (struct sockaddr*)&strAddr, &lenAddr);
		syslog (LOG_INFO, "Socket connection accepted");
		/* handle and evaluate incomming data */
		status = handle_socket_con(fdConn);
		if (status != OK){
			syslog (LOG_ERR, "Socket handler error number %d", status);
		}
		/* close connection descriptor */
		close(fdConn);
	}
	/* clean up and exit*/
	syslog (LOG_NOTICE, "clean up and exit\n");
	/* close lockfile descriptor */
	close(fdlock);
	/* close socket */
	close(fdSock);
	/* erase lockfile */
	unlink(MASTERD_LOCK_FILE);
	closelog();
	return OK;
}

