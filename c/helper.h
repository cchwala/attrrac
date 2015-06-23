#ifndef HELPER_H
#define HELPER_H

#include "usb_control.h"


int mean(DATA_STRUCT *data, int skip);

int std_dev(DATA_STRUCT *data, int skip);

double amp(int I, int q);
double pha(int I, int q);

int get_lock_file(char* filename);


#endif /* HELPER_H */
