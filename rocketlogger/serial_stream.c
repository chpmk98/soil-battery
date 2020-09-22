#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <time.h>

uint8_t inbuf[1];
uint8_t outbuf[50];

// TODOs:
//  - daemonize this program
//  - better format checking before fwrite (some lines in logfile end up f'd up occasionally)
// PS: I have no idea what I'm doing wrt serial, but this seems to output what I expect?

int main(int argc, char** argv){
    /* cli args parse */
    int help = 0;
    char *tty_path = NULL;
    int index;
    int c;
    opterr = 0;
    while ((c = getopt (argc, argv, "lht:")) != -1) {
        switch (c) {
            case 't':
                tty_path = optarg;
                break;
            case '?':
                if (optopt == 't')
                    fprintf (stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                abort();
        }
    }

    if (help) {
        printf("usage: %s [-lh] [-t tty]\n", argv[0]);
        printf("-t tty\t: set path to tty device file\n");
        exit(0);
    }

    int USB;
    if (tty_path) {
        USB = open( tty_path, O_RDWR|O_NOCTTY|O_NONBLOCK );
    }
    else {
        USB = open( "/dev/ttyACM0", O_RDWR|O_NOCTTY|O_NONBLOCK );
    }
    printf("USB = %d\n", USB);

    if( USB==-1 ){
        printf("serial port open bad :( \n");
        exit(1);
    }
    else{
        printf("serial port open good\n");
    }

    if (fcntl(USB, F_SETFL, 0) == -1)
    {
        printf("can't set tty device non-blocking\n");
        exit(1);
    }

    struct termios tty;
    memset (&tty, 0, sizeof tty);

    if( tcgetattr(USB, &tty)!= 0){
        printf("Error fetching tty config, %i\n",errno);
    }
    /* Set Baud Rate */
    cfsetospeed (&tty, (speed_t)B1200);
    cfsetispeed (&tty, (speed_t)B1200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_lflag = 0;                // no signaling chars, no echo,
    // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;            // read doesn't block
    tty.c_cc[VTIME] = 100;            // 0.5 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag |= 0;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;


    /* Flush Port, then applies attributes */
    tcflush( USB, TCIFLUSH );
    if ( tcsetattr ( USB, TCSANOW, &tty ) != 0) {
        printf("Error configuring tty, %i\n", errno);
    }

    FILE* outfile;
    int marker_state = 0;
    time_t now;
    char logstr[80];
    char filename[50];

    /* keep reading until start of line */
    while (1)
    {
        read(USB, inbuf, 1);
	if (inbuf[0] == '\n') {
	  marker_state = 1;
	}
	else {
	  marker_state = 0;
	}
        if (marker_state == 1) {
	  printf("Synced\n");
	  marker_state = 0;
	  sprintf(filename,"TEROSoutput-%lu.csv",(unsigned long)time(&now));
	  outfile = fopen(filename, "wb");
	  //TEROS-12 OUTPUT FORMAT: samples(ADDR/RAW/TMP/EC): 0+1870.34+21.1+0
	  char header[] = "timestamp,sensorID,raw_VWC,temp,EC\n";
	  fwrite(header,sizeof(header),1, outfile); // write CSV header
	  break;
        }
    }
    
    int i = 0;
    while (1)
    {
      
      read(USB, inbuf, 1);
      if (inbuf[0] == '\n') {
	outbuf[marker_state] = inbuf[0];
	marker_state = 0;

	// replace '+' w/ ',' for CSV
	for (c = 0; c<sizeof(outbuf); c++)
	  if (outbuf[c] == '+')
	    outbuf[c] = ',';

	sprintf(logstr,"%lu,%s",(unsigned long)time(&now), outbuf);
	fwrite(logstr, sizeof(char), sizeof(logstr), outfile);
	fflush(outfile);
	//clear dem buffers
	memset(outbuf, 0, sizeof(outbuf));
	memset(logstr, 0, sizeof(logstr));
	sleep(10);
      }
      else if (marker_state > sizeof(outbuf)){ //oops, overflow
	//printf("marker state %i\n", marker_state);
	marker_state = 0;
	memset(outbuf, 0, sizeof(outbuf));
      } else {
	outbuf[marker_state] = inbuf[0];
	marker_state += 1;
      }
    }
    fclose(outfile); 
    return 0;
}