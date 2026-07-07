#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

struct timeval start_time;

void handle_sigint(int sig) {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    double elapsed_secs = (current_time.tv_sec - start_time.tv_sec) + 
                          (current_time.tv_usec - start_time.tv_usec) / 1000000.0;
    
    printf(" %.3f\n", elapsed_secs);
    exit(0);
}

int main() {
    struct timeval current_time;
    gettimeofday(&start_time, NULL);
    
    signal(SIGINT, handle_sigint);

    double next_print = 0.0;

    printf("Stopwatch started. Press Ctrl+C to stop.\n");

    while (1) {
        gettimeofday(&current_time, NULL);
        
        double elapsed_secs = (current_time.tv_sec - start_time.tv_sec) + 
                              (current_time.tv_usec - start_time.tv_usec) / 1000000.0;

        if (elapsed_secs >= next_print) {
            printf("%.1f ", elapsed_secs);
            fflush(stdout);
            next_print += 0.1;
        }

        // Sleep for 1/100 of a second (10,000 microseconds)
        usleep(10000);
    }

    return 0;
}
