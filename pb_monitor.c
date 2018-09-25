/**********************************************************************************************************************
*
*   File:           pb_monitor.c
*
*   Summary:        Push-button functionality for 'factory reset', 'reboot' and 'shutdown''
*
*   Element:        IESv06
*
*   Platform:       Linux
*
*   Description:  Interfaces to Gateworks System Controller using /sys/class/input/event0.
*                 First, disables hardware reset on pushbutton (pb). 
*                 Two modes of Operation:
*                 AT START-UP
*                 - Checks file (/opt/monior/fc_set) on start-up to see if pb pressed while in use.
*                 - Yes - LED Flashes red while factory reset occurs.
*                 - No  - A 10 second period where LED is solid red, allows factory reset
*                   If pushed, factory-reset occurs. Otherwise allows period to call it.
*
*                 IN-USE Power Switch MONITOR
*                 - Press push-button for less than 5 seconds will reboot
*
*                 - Press push-button for 5 to 10 seconds, LED solid red, release button and
*                   will factory reset after next reboot (writes above File).
*
*                 - Press push-button for 10 to 15 seconds, LED flashes red, release button and
*                   board will shut-down for period in file: /opt/wakeup. If file not present
*                   (or value zero), will reboot immediatly.
*
*                 - Press push-button for 15+ seconds, LED flashes green, and cancels press.
*
*   Operation: uses /sys/class/input/event0 select (blocking) and read with
*              signal timer and measurement of pb press release period.
*
*   Compile :    gcc -lrt pb_monitor.c -o pb_monitor
*                Run     :   ./test_gpio2 /dev/input/event0
*
*   NOTE: REPLACED I2C POLL version pb_monitor.sh
*
*******************************************************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>  /* true, false */
#include <sys/stat.h>

/*
 * Defines
 */
#define TIMER1_EXPIRE   10     /* seconds */
#define TIMER1_INTERVAL 2
#define FACTORY_RESET_FILE  "/opt/monitors/fc-set"

/*
 * Enumuration
 */
enum led_state {
	LED_OFF,
	LED_GREEN,
	LED_RED,
	LED_FLASH_GREEN,
	LED_FLASH_RED
};

/*
 * Global - Enum type declaration
 */
enum pb_state {
	PB_STATE_START,
	PB_STATE_INUSE
} state;

/*
 * Global Strings - Bash script system calls
 */
char str_sys_call_i2c_reset[]  = "i2cset -f -y 0 0x20 0 0";
char str_sys_call_opt_dir[]  = "mkdir -p /opt/monitors/";
char str_sys_call_check_factory_reset[]  = "/usr/local/bin/check-factory-reset.sh ";
char str_sys_call_reboot[] = "reboot";
char str_sys_call_shutdown[] = "shutdown -h now";
char str_sys_call_led[][16] = {
        {"./set_led.sh 0"},
        {"./set_led.sh 1"},
        {"./set_led.sh 2"},
        {"./set_led.sh 3"},
        {"./set_led.sh 4"}
};

/*
 **************  Functions  ****************
 */

/*
 * pb_initialise
 *
 * @brief Disables I2C Hardware reset and creates monitor directory.
 * @return void.
 */
void pb_initialise( void )
{
	/* Disable pb */
	system(str_sys_call_i2c_reset);
	/* Make Directory if not present */
	system(str_sys_call_opt_dir);
}

/*
 * timer_handler
 *
 * @brief Signal handler on timer expire
 * @param sig -
 * @param *si -
 * @param *uc -
 * @return void.
 */
static void timer_handler(int sig, siginfo_t *si, void *uc)
{
    /* On First entry Change state */
	if (state == PB_STATE_START)
	{
	    printf("Signal %d - Changes pb mode to in-use\n", sig);
	    state = PB_STATE_INUSE;
	    system(str_sys_call_led[LED_FLASH_GREEN]);
	    /* Must call check-factory-reset.sh wthout causing facory reset */
	    strcat (str_sys_call_check_factory_reset, "0");
   	    /* Call check-factory-reset.sh to perform a factory reset */
   	    system(str_sys_call_check_factory_reset);
	}
}

/*
 * make_timer
 *
 * @brief Creates timer for first period and constant interval timeouts
 *        Calls Signal handler on timer expire. The 'select' does not time-out
 *        The initial timer period allows switch between START-UP mode and INUSE mode
 *        The subsequent interval timer allows LED to be driven for reboot(flash green),
 *        factory reset on reboot(solid red) and shutdown(flash red).
 */
int make_timer( char *name, timer_t *timerID, int expireS, int intervalS )
{
	    struct sigevent         te;
	    struct itimerspec       its;
	    struct sigaction        sa;
	    int                     sigNo = SIGRTMIN;

	    /* Set up signal handler. */
	    sa.sa_flags = SA_SIGINFO;
	    sa.sa_sigaction = timer_handler;
	    sigemptyset(&sa.sa_mask);
	    if (sigaction(sigNo, &sa, NULL) == -1)
	    {
            perror("sigaction");
	    	printf( "Failed to setup signal handler\n");
	        return(-1);
	    }
	    /* Set and enable alarm */
	    te.sigev_notify = SIGEV_SIGNAL;
	    te.sigev_signo = sigNo;
	    te.sigev_value.sival_ptr = timerID;
	    if ((timer_create(CLOCK_REALTIME, &te, timerID)) == -1)
	    {
            perror("timer_create");
            printf( "Failed to create timer\n");
	        return(-1);
	    }

	    /* repeat */
	    its.it_interval.tv_sec = intervalS;  //intervalMS;
	    its.it_interval.tv_nsec = 0;
	    /* initial */
	    its.it_value.tv_sec = expireS;
	    its.it_value.tv_nsec = 0;

	    /* Start Timer */
	    if ((timer_settime(*timerID, 0, &its, NULL)) !=0)
	    {
	    	perror("timer_start");
	        printf( "timer_settime failed:%d -(%s)\n", errno, strerror(errno));
	    }
	    return(0);
}

/*
 * timespec_diff
 *
 * @brief Called from test_time to calculates pb press time and returns in result.
 */
void timespec_diff(const struct timespec *start, const struct timespec *stop,
                   struct timespec *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0)
    {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000UL;
    }
    else
    {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
}

/*
 * test_time
 *
 * @brief If start time is set, raed curent time and call function to calculate time.
 */
double test_time (const struct timespec *start, struct timespec *stop, struct timespec *duration)
{
	/* invalid if no start time set */
	if (( start->tv_sec > 0 ) || (start->tv_nsec > 0 ))
	{
		/* current time */
        if((clock_gettime( CLOCK_REALTIME, stop)) == -1)
        {
            perror("clock gettime");
            return (0);
	    }
	    timespec_diff ( start, stop, duration);
        return ((double)duration->tv_sec + ((double)duration->tv_nsec /1000000000));
	}
	return (0);
}

/*
 * process_time
 *
 * @brief Process the time so far to determine LED changes.
 *        Note accurate to time + timer interval period
 */
void process_time( unsigned long seconds)
{
    if (state == PB_STATE_INUSE)
    {
    	if (seconds >= 15)
    	{
    	    // return to heartbeat
    	    system(str_sys_call_led[LED_FLASH_GREEN]);
    	}
        else if (seconds >= 10)
        {
    	    // Flash red - release for shutdown
    	    system(str_sys_call_led[LED_FLASH_RED]);
        }
    	else if (seconds >= 5)
    	{
			// Solid red - release for factory reset
    	    system(str_sys_call_led[LED_RED]);
        }
    }
}

/*
 * process_end_time
 *
 * @brief Process the final time (accurate) to determine which button functionality to implement
 *        reboot; factory reset (on next power-up); shutdown; cancel
 */
void process_end_time( unsigned long seconds)
{
	FILE *file_ptr;
	if (seconds >= 15)
	{
	   	printf("Long Push-Button Press (15+sec) - cancelled\n");
	}
	else if (seconds >= 10)
    {
    	printf("Long Push-Button Press (10+sec) - shutdown\n");
    	system(str_sys_call_shutdown);
    }
    else if (seconds >= 5)
    {
    	printf("Long Push-Button Press (5+sec) - Enter factory reset on next reboot\n");
        /* Create File to be checked on start-up */
    	file_ptr = fopen(FACTORY_RESET_FILE, "w");
    	fclose(file_ptr);
    }
    else  /* less than 5 seconds */
    {
    	if (state == PB_STATE_INUSE)
    	{
    		/* REBOOT */
    		printf("Short Push-ButtonPress (less 5sec ) - reboot\n");
    		system(str_sys_call_reboot);
    	}
    	else
    	{
    		/* STARTUP - Factory reset */
    		printf("Factory Reset\n");
    		strcat (str_sys_call_check_factory_reset, "1");
    	    /* Call check-factory-reset.sh to perform a factory reset */
    	    system(str_sys_call_check_factory_reset);
    	    /* set mode to IN-USE */
       		state = PB_STATE_INUSE;
       		// return to heartbeat
    	    system(str_sys_call_led[LED_FLASH_GREEN]);
    	}
    }
}

/*
 * check_inuse_factory_reset
 *
 * @brief  Is there a file indicating factory-reset was requested IN-USE
 *         in which case perform factory-reset, otherwise STARTUP by setting
 *         LED to solid red, and wait time to 10 seconds (allow pb press to
 *         immediate factory reset).
 */

void check_inuse_factory_reset( int *time_start )
{
    struct stat sts;
    if (stat(FACTORY_RESET_FILE, &sts) == -1 && errno == ENOENT)
    {
//      printf ("%s not present...\n", FACTORY_RESET_FILE);
       	/* Set LED */
       	system(str_sys_call_led[LED_RED]);
       	/* Allow unit to run for 10 seconds where a button press causes factory reset */
       	*time_start = TIMER1_EXPIRE;
    }
    else
    {
    	/* File Present */
//      printf ("%s present, call factory reset...\n", FACTORY_RESET_FILE);
       	remove(FACTORY_RESET_FILE);
       	strcat (str_sys_call_check_factory_reset, "1");
       	/* Call check-factory-reset.sh to perform a factory reset */
        system(str_sys_call_check_factory_reset);
        /* set mode straight into IN-USE */
       	*time_start = TIMER1_INTERVAL;
       	state = PB_STATE_INUSE;
       	system(str_sys_call_led[LED_FLASH_GREEN]);
    }
}


/*
 ************** main Function  ****************
 *
 * @brief  Initialise
 *         Main Loop for both STARTUP mode (10 second period when a push button press causes
 *         a factory reset, and INUSE mode to process push button events based on length
 *         of push. Uses select (blocks) and read of input/event. The select time-out
 *         doesn't seem to work so a signal timer is used to set the initial START-UP time
 *         and then expire at intervals for time of press to be determined, so that
 *         the LED can change based on function. Push-button Release period is evaluated to
 *         determine operation.
 */
int main (int argc, char **argv)
{
        int fd;
        const char *device = argv[1];
        struct input_event ev[64];
        int i, rd;
        fd_set rdfs;
        int ret;
        state = PB_STATE_START;
        int time_start;
        int count;

        // push button timer press to release
        struct timespec timer_start, timer_stop, time_duration;
        double total_time;
        struct timeval timeout;

        // Signal timer to change state from startup to inuse
        timer_t timer1;
        struct sigevent te;
        struct itimerspec its;
        struct sigaction sa;

        if (!device) {
                fprintf(stderr, "No device specified\n");
                return 1;
        }

        /* Wait for interface to become available */
        count = 0;
/*
        while (count < 50)
        {
            if ((fd = open(device, O_RDONLY)) >= 0)
            {
            	printf("-------count=%d", count);
                break;
            }
            else if (count == 49)
            {
                perror("evtest");
                return EXIT_FAILURE;
            }
            count++
			usleep(200000) // 200ns
        }
*/
        // initialise
        pb_initialise();
        timer_start.tv_sec=0;
        timer_start.tv_nsec=0;

        printf("Start Push-Button Monitor\n");
        printf("Start-Mode, press push-button for factory Reset\n");

        /* Is there a file indicating factory-reset required on next boot */
        check_inuse_factory_reset( &time_start );

  		// set up signal timer
        make_timer((char*)"Timer1", &timer1, time_start, TIMER1_INTERVAL);

        /* Main Loop */
        while (1)
        {
            FD_ZERO(&rdfs);
            FD_SET(fd, &rdfs);
            // select time-out
            timeout.tv_sec  = 3;
            timeout.tv_usec = 0;
            /* Block on select until input or Signal ortmeout */
            select(fd + 1, &rdfs, NULL, NULL, &timeout);
            rd = read(fd, ev, sizeof(ev));
            if (rd < (int) sizeof(struct input_event)) {
                 if (errno != EINTR)
                 {
                	perror("read error");
                   //return 1;
                 }
            }
            else
            {
                for (i = 0; i < rd / sizeof(struct input_event); i++)
                {
                    if (ev[i].type != EV_KEY)
                        continue;
//                  printf("i=%d code=%d value=%d\n", i, ev[i].code, ev[i].value);
                    if (ev[i].code == 256)
                    {
                    	/* PUSH Button */
                        if (ev[i].value == 1)
                        {
                        	/* start timer */
                        	if((clock_gettime(CLOCK_REALTIME, &timer_start)) == -1) {
                        	    perror("clock gettime");
                        	    break;
                        	 }
//                        	 printf("Start time: %d.%09d\n", timer_start.tv_sec, timer_start.tv_nsec);
                        }
                        /* RELEASE Button */
                        else if (ev[i].value == 0)
                        {
                        	total_time = test_time( &timer_start, &timer_stop, &time_duration);
                        	if (total_time)
                        	{
//                   		printf("Stop time: %d.%09d\n", timer_stop.tv_sec, timer_stop.tv_nsec);
//                   	        printf("Duration %d.%09d\n", time_duration.tv_sec, time_duration.tv_nsec);
//                   	        printf("Total_time=%f\n",total_time);
                    	            /* Call Function to perform actions */
                      	            process_end_time((unsigned long)total_time);
                    	            /* Reset */
                    	            timer_start.tv_sec=0;
                    	            timer_start.tv_nsec=0;
                    	            system(str_sys_call_led[LED_FLASH_GREEN]);
                        	}
                        	else
                        	{
                        	    printf("Invalid Time\n");
                        	}
                        }
                    }  // if ev[i].code
                }  // for
            } // else
            /* Process the time so far to determine LED changes */
            total_time = test_time( &timer_start, &timer_stop, &time_duration);
            if (total_time)
            {
                process_time((unsigned long)total_time);
            }
        } // while
        ioctl(fd, EVIOCGRAB, (void*)0);
        close(fd);
        return EXIT_SUCCESS;





}
