#include <stdio.h>
#include <sys/io.h>
#include <string.h>

int wd_set(int timeout)
{
	double wd_inc_time = 30.5e-6;
	unsigned long t = (unsigned long) timeout / wd_inc_time ; // 10 sec reset till reset
	
	if (timeout > 500)
	{
		printf("Value for timeout to large!\n");
		return 1;
	}
		
	// set permission for this process
	iopl(3);
	
	// write 3 bytes of timeout interval
	outb((t >> 16) & 0xff, 0x6c);
	outb((t >> 8 ) & 0xff, 0x6b);
	outb((t >> 0 ) & 0xff, 0x6a);
	
	printf("Watchdog timeout set to %d seconds\n", timeout );
	
	// set reset event to system reset
	outb(0xd0, 0x69);
	
	return 0;
}

int wd_enable()
{
	unsigned char c;
	
	iopl(3);
	
	c = inb(0x68);
	c |= 0x40;
	outb(c, 0x68);
	
	printf("Watchdog enabled!\n");
	return 0;
}

int wd_disable()
{
	unsigned char c;
	
	iopl(3);
	
//	c = inb(0x68);
//	c |= 0x00;
//	outb(c, 0x68);
	outb(0x00, 0x68);
	
	printf("Watchdog disabled!\n");
	return 0;
}

int wd_feed()
{
	iopl(3);
	outb(0x00, 0x67);
	return 0;
}

int main(int argc, char *argv[])
{
	if(argc < 2 || argc > 3){
		printf("Wrong number of arguments\n");
		return 0;
	}
	
	if((strcmp(argv[1],"set") == 0) && argc == 3){
		wd_set(atoi(argv[2]));
	}
	else if((strcmp(argv[1],"enable") == 0) && argc == 2){
		wd_enable();
	}
	else if((strcmp(argv[1],"disable") == 0) && argc == 2){
		wd_disable();
	}
	else if((strcmp(argv[1],"feed") == 0) && argc == 2){
		wd_feed();
	}
	else if((strcmp(argv[1],"--help") == 0) || (strcmp(argv[1],"-h") == 0)){
		printf("watchdog set VALUE\n");
		printf("         enable\n");
		printf("         disable\n");
		printf("         feed\n");
	}
	else{
		printf("Unknown command or wrong number of arguments.\n");
	}
	return 0;
}



