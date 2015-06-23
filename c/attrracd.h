#ifndef ATTRRACD_H
#define ATTRRACD_H

/* file locations */
#define CONF_FILE 			"attrracd.conf"
#define DATA_DIR 			"./data"
#define TEMP_DIR			"./tmp"
#define MASTERD_LOCK_FILE 	"attrracd.lock"

#define SOCKET_PATH 		"attrracd_socket"
#define MAX_LENGTH 			32

#define SKIP				20

// NOT USED ANYMORE??!!

/* structur for holding configuration info */
// typedef struct {
// 	char 	machine_ip[64];
// 	char	machine_name[64];
// 	char	machine_type[64];
// 	int		h_v_switch_freq;
// 	int 	sampling_rate;
// 	int		number_of_samples;
// 	int		sample_spacing;
// 	int		receiver_delay;
// 	char 	ftp_ip[64];
// } masterd_conf_struct;

//int read_masterd_config(masterd_conf_struct* conf);

//pid_t getProcessId(const char * csProcessName);

#endif /* ATTRRACD_H */

