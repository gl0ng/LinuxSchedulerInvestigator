#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>

#define LARGE_SCALE 1000
#define MEDIUM_SCALE 100
#define SMALL_SCALE 10

#define DEBUG 0

int processScales[3] = { SMALL_SCALE, MEDIUM_SCALE, LARGE_SCALE };
char schedulingPolicies[3][12] = { "SCHED_OTHER", "SCHED_FIFO", "SCHED_RR" };
char programs[3][6] = { "./pi", "./rw", "./mix" };
struct sched_param param;


//http://www.cs.fsu.edu/~baker/realtime/restricted/notes/utils.c
struct timespec timespec_sub(struct timespec timespec_1,struct timespec timespec_2) {       
	struct timespec rtn_val;
	int xsec;
	int sign = 1;

	if ( timespec_2.tv_nsec > timespec_1.tv_nsec ) {
		xsec = (int)((timespec_2.tv_nsec - timespec_1.tv_nsec) / (1E9 + 1));
		timespec_2.tv_nsec -= (long int)(1E9 * xsec);
		timespec_2.tv_sec += xsec;
	}

	if ( (timespec_1.tv_nsec - timespec_2.tv_nsec) > 1E9 ) {
		xsec = (int)((timespec_1.tv_nsec - timespec_2.tv_nsec) / 1E9);
		timespec_2.tv_nsec += (long int)(1E9 * xsec);
		timespec_2.tv_sec -= xsec;
	}

	rtn_val.tv_sec = timespec_1.tv_sec - timespec_2.tv_sec;
	rtn_val.tv_nsec = timespec_1.tv_nsec - timespec_2.tv_nsec;                                                 

	if (timespec_1.tv_sec < timespec_2.tv_sec) {
		sign = -1;
	}

	rtn_val.tv_sec = rtn_val.tv_sec * sign;

	return rtn_val;
}

double get_secs_diff(struct timespec clock_1, struct timespec clock_2) {
	double rtn_val;
	struct timespec diff;

	diff = timespec_sub(clock_1, clock_2);
	
	rtn_val = diff.tv_sec;
	rtn_val += (double)diff.tv_nsec/(double)1E9;

	return rtn_val;		
}


void executeProgram(char* program)
{
	char command[50];
	snprintf(command, 50, "%s", program);
    system(command);
}

void setPolicy(char* p){
	int policy;
	if(!strcmp(p, "SCHED_OTHER")){
	    policy = SCHED_OTHER;
	}
	else if(!strcmp(p, "SCHED_FIFO")){
	    policy = SCHED_FIFO;
	}
	else if(!strcmp(p, "SCHED_RR")){
	    policy = SCHED_RR;
	}
	else{
	    fprintf(stderr, "Unhandeled scheduling policy\n");
	    exit(EXIT_FAILURE);
	}
	
	param.sched_priority = sched_get_priority_max(policy);
#if DEBUG
	fprintf(stdout, "Current Scheduling Policy: %d\n", sched_getscheduler(0));
	fprintf(stdout, "Setting Scheduling Policy to: %d\n", policy);
#endif
	if(sched_setscheduler(0, policy, &param)){
	perror("Error setting scheduler policy");
	exit(EXIT_FAILURE);
	}
#if DEBUG
	fprintf(stdout, "New Scheduling Policy: %d\n", sched_getscheduler(0));
#endif
}

int main(int argc, char *argv[])
{
    /* void unused vars */
    (void) argc;
    (void) argv;

    /* Setup Local Vars */
    int i, a, n;
    
    /* Loop Through Each Program Type */
    for(a=0; a < 3; a++){
    	char* program = programs[a];
    	
		/* Loop Through Each Scheduling Type */
		for(i=0; i < 3; i++){
			char* policy = schedulingPolicies[i];
			setPolicy(policy);
			
			/* Loop Through Each Scale */
			for(n=0; n < 3; n++){
				/* Create PIDS */
				int num_pids = processScales[n];
				pid_t pids[num_pids];
				int i;
				
				/* Measure Total Time */
				struct timespec t_begin, t_end;
				double t_time;
				double p_time_total = 0, max_turn_time = 0, max_wait_time = 0, total_wait_time = 0, min_turn_time = 100, min_wait_time = 100;
				double processes = (double) processScales[n];
				clock_gettime(CLOCK_REALTIME, &t_begin);
				struct timespec p_start_times[32768];
				

				/* Start children. */
				for (i = 0; i < num_pids; ++i) {
					struct timespec p_begin;
					clock_gettime(CLOCK_REALTIME, &p_begin);
					
					if ((pids[i] = fork()) < 0) {
						perror("fork");
						abort();
					} else if (pids[i] == 0) {
						setPolicy(policy); 
						executeProgram(program);
					    //printf("Current Scheduling Policy: %d\n", sched_getscheduler(pids[i]));
						exit(0);	
					}
					p_start_times[(long) pids[i]] = p_begin;
				}

				/* Wait for children to exit. */
				int status;
				pid_t pid;
				
				while (num_pids > 0) {
					struct rusage usage;
					struct timeval utime, stime;
					struct timespec p_end, p_start;
					double p_time;

				  	pid = wait(&status);
				  	--num_pids;
#if DEBUG
				  	printf("Child with PID %ld exited with status 0x%x.\n", (long)pid, status);
#endif
			
				  	/* Calculate Wait Time of Process */
				  	getrusage(RUSAGE_SELF, &usage);
				  	utime = usage.ru_utime;
				  	stime = usage.ru_stime;
				  	long wait_secs = utime.tv_sec + stime.tv_sec;
				  	long wait_milli = utime.tv_usec + stime.tv_usec;
				  	
				  	/* Calculate Wall Time of Process */
				  	clock_gettime(CLOCK_REALTIME, &p_end);
				  	p_start = p_start_times[(long) pid];
				  	p_time = get_secs_diff(p_end, p_start);
				  	
				  	double wait_time = p_time - (wait_secs + (wait_milli / 1000000.0));
				  	
				  	/* Add Times to Benchmark */
				  	total_wait_time += wait_time;
				  	p_time_total += p_time;
				  	
				  	if(wait_time < min_wait_time){
				  		min_wait_time = wait_time;
				  	}
				  	
				  	if(p_time < min_turn_time){
				  		min_turn_time = p_time;
				  	}
				  	
				  	if(wait_time > max_wait_time){
				  		max_wait_time = wait_time;
				  	}
				  	
				  	if(p_time > max_turn_time){
				  		max_turn_time = p_time;
				  	}
				  	
				}
				/* Create Report */
				printf("****************************************************************\n");
				printf("Current Scheduling Policy: %d\n", sched_getscheduler(0));
				printf("Finished Running %s %s of scale %i.\n", program, policy, (int) processes);
				
				clock_gettime(CLOCK_REALTIME, &t_end);
				t_time = get_secs_diff(t_end, t_begin);
				
				printf("Total Real Time:         %fs\n", t_time);
				printf("Real Time Per Process:   %fs\n\n", t_time / processes);
				
				printf("Average Turnaround Time: %fs\n", p_time_total / processes);
				printf("Lowest Turnaround Time:  %fs\n", min_turn_time);
				printf("Largest Turnaround Time: %fs\n\n", max_turn_time);
				
				printf("Average Wait Time:       %fs\n", total_wait_time / processes);
				printf("Lowest Wait Time:        %fs\n", min_wait_time);
				printf("Largest Wait Time:       %fs\n", max_wait_time);
			}
		}	
	}
    
    return 0;
}
