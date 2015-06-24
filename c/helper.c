/*
 * helper.c - Some helper functions
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

#include "attrracd.h"
#include "usb_control.h"
#include "helper.h"

#define PI 3.14159265

/* Calculate the mean value of each array in data struct*/
int mean(DATA_STRUCT *data, int skip)
{ 
	int i;
	double N = (double)data->N - skip;
	signed long sum_h_i_22 = 0;
	signed long sum_h_q_22 = 0;
	signed long sum_h_i_35 = 0;
	signed long sum_h_q_35 = 0;
	signed long sum_v_i_22 = 0;
	signed long sum_v_q_22 = 0;
	signed long sum_v_i_35 = 0;
	signed long sum_v_q_35 = 0;
	
	signed long sum_h_a_22 = 0;
	signed long sum_h_a_35 = 0;
	signed long sum_v_a_22 = 0;
	signed long sum_v_a_35 = 0;
		
	for (i = skip; i < data->N; i++){
		sum_h_i_22 += data->h_i_22->values[i];
		sum_h_q_22 += data->h_q_22->values[i];
		sum_h_i_35 += data->h_i_35->values[i];
		sum_h_q_35 += data->h_q_35->values[i];
		sum_v_i_22 += data->v_i_22->values[i];
		sum_v_q_22 += data->v_q_22->values[i];
		sum_v_i_35 += data->v_i_35->values[i];
		sum_v_q_35 += data->v_q_35->values[i];
		
		sum_h_a_22 += amp(data->h_i_22->values[i], data->h_q_22->values[i]);
		sum_h_a_35 += amp(data->h_i_35->values[i], data->h_q_35->values[i]);
		sum_v_a_22 += amp(data->v_i_22->values[i], data->v_q_22->values[i]);
		sum_v_a_35 += amp(data->v_i_35->values[i], data->v_q_35->values[i]);
	}
	data->h_i_22->mean = (double)sum_h_i_22/N;
	data->h_q_22->mean = (double)sum_h_q_22/N;
	data->h_i_35->mean = (double)sum_h_i_35/N;
	data->h_q_35->mean = (double)sum_h_q_35/N;
	data->v_i_22->mean = (double)sum_v_i_22/N;
	data->v_q_22->mean = (double)sum_v_q_22/N;
	data->v_i_35->mean = (double)sum_v_i_35/N;
	data->v_q_35->mean = (double)sum_v_q_35/N;
	
	data->h_a_22->mean = (double)sum_h_a_22/N;
	data->h_p_22->mean = pha(data->h_i_22->mean, data->h_q_22->mean);
	data->h_a_35->mean = (double)sum_h_a_35/N;
	data->h_p_35->mean = pha(data->h_i_35->mean, data->h_q_35->mean);
	data->v_a_22->mean = (double)sum_v_a_22/N;
	data->v_p_22->mean = pha(data->v_i_22->mean, data->v_q_22->mean);
	data->v_a_35->mean = (double)sum_v_a_35/N;
	data->v_p_35->mean = pha(data->v_i_35->mean, data->v_q_35->mean);
	
	
/*	printf("%d %d\n", sum_h_i_22, sum_v_i_35);
	printf("%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f \n",
			data->h_i_22->mean, data->h_q_22->mean, 
		    data->h_i_35->mean, data->h_q_35->mean,
			data->v_i_22->mean, data->v_q_22->mean, 
		    data->v_i_35->mean, data->v_q_35->mean);*/
			
	return OK;
}

/* Calculate the standard deviation of an array of shorts */
//
// CHECK THIS FOR ERRORS !!!!!!!!!!!!!!
//
int std_dev(DATA_STRUCT *data, int skip)
{
	
	int i;
	double N = (double)data->N - skip;
	signed long std_dev_h_i_22 = 0;
	signed long std_dev_h_q_22 = 0;
	signed long std_dev_h_i_35 = 0;
	signed long std_dev_h_q_35 = 0;
	signed long std_dev_v_i_22 = 0;
	signed long std_dev_v_q_22 = 0;
	signed long std_dev_v_i_35 = 0;
	signed long std_dev_v_q_35 = 0;
	
	signed long std_dev_h_a_22 = 0;
	signed long std_dev_h_p_22 = 0;
	signed long std_dev_h_a_35 = 0;
	signed long std_dev_h_p_35 = 0;
	signed long std_dev_v_a_22 = 0;
	signed long std_dev_v_p_22 = 0;
	signed long std_dev_v_a_35 = 0;
	signed long std_dev_v_p_35 = 0;
	
	for (i = skip; i < data->N; i++){
		std_dev_h_i_22 += pow(data->h_i_22->values[i] - data->h_i_22->mean, 2);
		std_dev_h_q_22 += pow(data->h_q_22->values[i] - data->h_q_22->mean, 2);
		std_dev_h_i_35 += pow(data->h_i_35->values[i] - data->h_i_35->mean, 2);
		std_dev_h_q_35 += pow(data->h_q_35->values[i] - data->h_q_35->mean, 2);
		std_dev_v_i_22 += pow(data->v_i_22->values[i] - data->v_i_22->mean, 2);
		std_dev_v_q_22 += pow(data->v_q_22->values[i] - data->v_q_22->mean, 2);
		std_dev_v_i_35 += pow(data->v_i_35->values[i] - data->v_i_35->mean, 2);
		std_dev_v_q_35 += pow(data->v_q_35->values[i] - data->v_q_35->mean, 2);
		
		std_dev_h_a_22 += pow(data->h_a_22->values[i] - data->h_a_22->mean, 2);
		std_dev_h_a_35 += pow(data->h_a_35->values[i] - data->h_a_35->mean, 2);
		std_dev_v_a_22 += pow(data->v_a_22->values[i] - data->v_a_22->mean, 2);
		std_dev_v_a_35 += pow(data->v_a_35->values[i] - data->v_a_35->mean, 2);

		// STD_DEV of the phase angle?? (How to do this mathematically correct????)
		std_dev_h_p_22 += pow(data->h_i_22->values[i] - data->h_i_22->mean, 2)/2 + pow(data->h_q_22->values[i] - data->h_q_22->mean, 2)/2;;
		std_dev_h_p_35 += pow(data->h_i_35->values[i] - data->h_i_35->mean, 2)/2 + pow(data->h_q_35->values[i] - data->h_q_35->mean, 2)/2;;
		std_dev_v_p_22 += pow(data->v_i_22->values[i] - data->v_i_22->mean, 2)/2 + pow(data->v_q_22->values[i] - data->v_q_22->mean, 2)/2;;
		std_dev_v_p_35 += pow(data->v_i_35->values[i] - data->v_i_35->mean, 2)/2 + pow(data->v_q_35->values[i] - data->v_q_35->mean, 2)/2;;
	}
	
	//??????????????????????????????????????????
	//?? HAS THIS TO BE NORMALIZED BY N OR N-1??
	data->h_i_22->std_dev = sqrt((double)std_dev_h_i_22/(N-1));
	data->h_q_22->std_dev = sqrt((double)std_dev_h_q_22/(N-1));
	data->h_i_35->std_dev = sqrt((double)std_dev_h_i_35/(N-1));
	data->h_q_35->std_dev = sqrt((double)std_dev_h_q_35/(N-1));
	data->v_i_22->std_dev = sqrt((double)std_dev_v_i_22/(N-1));
	data->v_q_22->std_dev = sqrt((double)std_dev_v_q_22/(N-1));
	data->v_i_35->std_dev = sqrt((double)std_dev_v_i_35/(N-1));
	data->v_q_35->std_dev = sqrt((double)std_dev_v_q_35/(N-1));
	
	data->h_a_22->std_dev = sqrt((double)std_dev_h_a_22/(N-1));
	data->h_p_22->std_dev = sqrt((double)std_dev_h_p_22/(N-1));
	data->h_a_35->std_dev = sqrt((double)std_dev_h_a_35/(N-1));
	data->h_p_35->std_dev = sqrt((double)std_dev_h_p_35/(N-1));
	data->v_a_22->std_dev = sqrt((double)std_dev_v_a_22/(N-1));
	data->v_p_22->std_dev = sqrt((double)std_dev_v_p_22/(N-1));
	data->v_a_35->std_dev = sqrt((double)std_dev_v_a_35/(N-1));
	data->v_p_35->std_dev = sqrt((double)std_dev_v_p_35/(N-1));
	
	return OK;
}

double amp(int I, int Q)
{
	return sqrt(I*I + Q*Q);
}

double pha(int I, int Q)
{
	if(I > 0) return atan((double)Q/I) * 180 / PI;
	if(I < 0) return atan((double)Q/I) * 180 / PI + 180;
	if(I == 0 && Q > 0) return 90;
	if(I == 0 && Q < 0) return 270;
	else return 0;
}

/* function to create and check lock files */
int get_lock_file(char* filename)
{
	int fdlock;
	struct flock fl;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 1;

	if((fdlock = open(filename, O_WRONLY|O_CREAT, 0666)) == -1){
		syslog (LOG_ERR, "Error creating lock file");
		return -1;
	}

	if(fcntl(fdlock, F_SETLK, &fl) == -1){
		syslog (LOG_ERR, "Error creating lock file");
		return -1;
	}
	syslog (LOG_INFO, "Lock file created"); 
	return fdlock;
}

/*------------------------------------------*
 * function to get PID of a process by name *
 *------------------------------------------*/

// WORKS ONLY ON BSD ==> FIX?!
// pid_t getProcessId(const char * csProcessName)
// {
// 	struct kinfo_proc *sProcesses = NULL, *sNewProcesses;
// 	pid_t  iCurrentPid;
// 	int    aiNames[4];
// 	N_t iNamesLength;
// 	int    i, iRetCode, iNumProcs;
// 	N_t iSize;
// 
// 	iSize = 0;
// 	aiNames[0] = CTL_KERN;
// 	aiNames[1] = KERN_PROC;
// 	aiNames[2] = KERN_PROC_ALL;
// 	aiNames[3] = 0;
// 	iNamesLength = 3;
// 
// 	iRetCode = sysctl(aiNames, iNamesLength, NULL, &iSize, NULL, 0);
// 
//         /*
// 	* Allocate memory and populate info in the  processes structure
// 	*/
// 	do {
// 		iSize += iSize / 10;
// 		sNewProcesses = realloc(sProcesses, iSize);
// 
// 		if (sNewProcesses == 0) {
// 			if (sProcesses)
// 				free(sProcesses);
// 			errx(1, "could not reallocate memory");
// 		}
// 		sProcesses = sNewProcesses;
// 		iRetCode = sysctl(aiNames, iNamesLength, sProcesses, &iSize, NULL, 0);
// 	} while (iRetCode == -1 && errno == ENOMEM);
// 
// 	iNumProcs = iSize / Nof(struct kinfo_proc);
//       
// 	/*
// 	 * Search for the given process name and return its pid.
//    	 */
// 	for (i = 0; i < iNumProcs; i++) {
// 		iCurrentPid = sProcesses[i].kp_proc.p_pid;
// 		if( strncmp(csProcessName, sProcesses[i].kp_proc.p_comm, MAXCOMLEN) == 0 ) {
// 			free(sProcesses);
// 			return iCurrentPid;
// 		}
// 	}
// 
//         /*
// 	 * Clean up and return -1 because the given proc name was not found
// 	 */
// 	free(sProcesses);
// 	return (-1);
// }
