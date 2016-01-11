/*  PQS.c
 * 	Thomas Watkin
 *
 *  A linux based program simulating customers arriving at a bank
 *  made to gain a grasp on multi-threaded programming using mutexes and
 *  semaphores in c. Based on an input file, a number of customers, with
 *  differing arrival times, service times, and priorities arrive at a bank,
 *  each represented by a new thread.
 *  When a thread has the highest priority (Priority > ServiceTime > ArrivalTime > Position in input file)
 *  it it attempts to meet its service time, in time as the active thread that we sleep on.
 *
 */
#include "PQS.h"
#include <errno.h>
#define MAX_CUSTOMERS 100

/********global variables (critical section)*****************************************/
pthread_mutex_t mutex_modify_critical_section = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_make_request = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_with_clerk = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_time_guard = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t enterServCond = PTHREAD_COND_INITIALIZER;
pthread_cond_t inServCond = PTHREAD_COND_INITIALIZER;

int current_customer = 0;//the id of the customer currently with the clerk
int current_priority = 0;
int current_atime = 0;
int current_stime = 0;
int next_cust;
int clerk_avail = 1;//if the clerk available
int num_waiting = 0;//number of customers waiting to get to the clerk
int time_served;//the total number of gold coins you have got
float serv_start;
struct timeval origin;
struct timeval c_start;
struct timeval c_end;
struct timeval relative_now;
struct timespec wait;
struct customer* waiting_customers[MAX_CUSTOMERS];//waiting slaves array


/********dig_thread*************************************************************
*		This function is called with each thread creation
*		INPUT: a pointer to a slave struct
*		OUTPUT: detailed information regarding when the slave digs, submits and terminates
********************************************************************************/
void* customer_thread(struct customer* customer){
	//print info. when the slave begins to work
	usleep(customer->arrival_time * 100000);
	pthread_mutex_lock(&mutex_modify_critical_section);
	printf("customer %2d arrives: arrival time (%.2f), service time (%.1f), priority (%2d). \n",
		customer->ID, (float) customer->arrival_time / 10.0,
		(float) customer->service_time / 10.0, customer->priority);
	pthread_mutex_unlock(&mutex_modify_critical_section);

	while(customer->service_time > 0){
		//attempt to enter service with the clerk
		request_clerk(customer);

		pthread_mutex_lock(&mutex_time_guard);
		//request is successful! enter service with the clerk
		pthread_mutex_lock(&mutex_with_clerk);

		pthread_mutex_lock(&mutex_modify_critical_section);
		current_customer = customer->ID;//mark the current customer
		current_priority = customer->priority;
		current_atime = customer->arrival_time;
		current_stime = customer->service_time;
		gettimeofday(&c_start,NULL);
		wait.tv_sec = c_start.tv_sec + (customer->service_time / 10);
		wait.tv_nsec = (c_start.tv_usec + 100000UL*(customer->service_time % 10))*1000UL;
		if(wait.tv_nsec >= 1000000000){
			wait.tv_sec += 1;
			wait.tv_nsec -= 1000000000;
		}

		timersub(&c_start, &origin, &relative_now);

		serv_start = relative_now.tv_sec + (relative_now.tv_usec / 1000000.0);
		printf("The clerk starts serving customer %2d at time %.2f\n", customer->ID, serv_start);
		pthread_mutex_unlock(&mutex_modify_critical_section);

		int ret;

		while(1){
			ret = pthread_cond_timedwait(&inServCond, &mutex_with_clerk, &wait);
			if(ret == 0 || ret == ETIMEDOUT)
				break;
			else if(ret == EINVAL){
				perror("INVALID INPUT FOR INSERVCOND, MUTEX, OR ABSTIME");
				break;
			} else if(ret == EPERM){
				perror("MUTEX NO LONGER OWNED BY WAITING THREAD");
				break;
			}

		}

		if(ret == ETIMEDOUT){
			customer->service_time = 0;
			pthread_mutex_lock(&mutex_modify_critical_section);

			gettimeofday(&c_end,NULL);
			timersub(&c_end, &origin, &relative_now);
			serv_start = relative_now.tv_sec + (relative_now.tv_usec / 1000000.0);
			printf("The clerk finishes the service to customer %2d at time %.2f. \n",
				customer->ID, serv_start);
		//printf("The clerk finishes serving customer %2d at time %ld. \n", customer->ID, (long)c_end.tv_sec);
		//printf("And %ld microseconds. \n",  (long)c_end.tv_usec);
		//printf("The wait seconds were %ld \n", (long)wait.tv_sec);
		//printf("And %ld microseconds. \n",  (long)wait.tv_nsec);
			pthread_mutex_unlock(&mutex_modify_critical_section);
			pthread_mutex_unlock(&mutex_with_clerk);
			release_clerk();
		} else if(ret == 0){
			pthread_mutex_lock(&mutex_modify_critical_section);
			gettimeofday(&c_end,NULL);
			timersub(&c_end, &c_start, &relative_now);
			time_served = relative_now.tv_sec*10 + (relative_now.tv_usec / 100000.0);
			customer->service_time -= time_served;

			pthread_mutex_unlock(&mutex_modify_critical_section);

			pthread_mutex_unlock(&mutex_with_clerk);
		}
		pthread_mutex_unlock(&mutex_time_guard);
		if(ret == EINVAL || ret == EPERM)
			return 0;
	}

	return 0;
}

/****************************************Begin Monitor********************************************/

/********request_treasury*****************************************
 *		This function will allow only one slave at a time
 *		to enter the treasury
 *		INPUT: a pointer to a slave struct
 *		OUTPUT: one slave will be able to enter the treasury
 *		while others will wait outside
 *****************************************************************/
void request_clerk(struct customer* customer){
	//lock mutex_make_request
	pthread_mutex_lock(&mutex_make_request);

	//request to enter the treasure, on success, return;
	if(clerk_avail == 1 && num_waiting == 0){
		//set current slave
		//set the treasury unavailable
		clerk_avail--;
		pthread_mutex_unlock(&mutex_make_request);
		return;
	}

	//If the customer coming has a higher priority
	//or has a matching arrival and higher
	//priority attributes (eg. lower ID#, lower service time)
	//then interrupt the service
	if(customer->priority > current_priority ||
		(customer->priority == current_priority &&
		customer->arrival_time == current_atime &&
			(customer->service_time < current_stime ||
				(customer->service_time == current_stime &&
				customer->ID < current_customer)))){

		pthread_mutex_lock(&mutex_modify_critical_section);
		//Announce the interrupt, then mark the traits of the new customer
		//So that the leaving customer won't try and re-enter upon being kicked out
		//remove the chance of a race scenario
		printf("customer %2d interrupts the service of lower-priority customer %2d. \n",
			customer->ID, current_customer);
		//update customer traits
		current_customer = customer->ID;
		current_priority = customer->priority;
		current_atime = customer->arrival_time;
		current_stime = customer->service_time;

		pthread_mutex_unlock(&mutex_modify_critical_section);
		//then send the signal to interrupt the old customer
		pthread_cond_broadcast(&inServCond);

		pthread_mutex_unlock(&mutex_make_request);
		return;
	}

	//otherwise, add customer into sorted waiting list
	pthread_mutex_lock(&mutex_modify_critical_section);
	update_list(customer);
	pthread_mutex_unlock(&mutex_modify_critical_section);

	//wait, if (1) the customer is not at the head of the waiting list or (2) the clerk is unavailable, wait
	while((customer->ID != waiting_customers[0]->ID) || (clerk_avail != 1)){
		//print wait info.
		printf("Customer no.%2d is waiting for the leaving of customer no.%2d.\n",
			customer->ID, current_customer);
		//wait on the enter service conditional variable, release mutex_make_request
		pthread_cond_wait(&enterServCond, &mutex_make_request);
	}

	//get out of the waiting list, keep the order (FIFS) of left customers
	pthread_mutex_lock(&mutex_modify_critical_section);
	clerk_avail--;//the treasury becomes unavailable
	int i;
	for (i = 0; i < num_waiting; i++)
		waiting_customers[i] = waiting_customers[i+1];
	num_waiting--;

	pthread_mutex_unlock(&mutex_modify_critical_section);

	//release mutex_make_request
	pthread_mutex_unlock(&mutex_make_request);
}

/********release_clerk*****************************************
 *		This function will make the clerk available
 *		again and wake up the waiting customers
 *		INPUT: nothing
 *		OUTPUT: clerk_avail is increased and waiting threads
 *		woken up
 **************************************************************/
void release_clerk(){
	//clerk is available again
	pthread_mutex_lock(&mutex_modify_critical_section);
	clerk_avail++;
	pthread_mutex_unlock(&mutex_modify_critical_section);
	//wake up all the waiting threads
	pthread_cond_broadcast(&enterServCond);
}

void update_list(struct customer* customer){
	int i;
	//sort through list to find where the customer should be,
	//based on priority, arrival time, service time, then id
	for (i = 0; i < num_waiting; i++){
		if(waiting_customers[i]->priority > customer->priority){
			continue;
		} else if(waiting_customers[i]->priority == customer->priority){
			if(waiting_customers[i]->arrival_time < customer->arrival_time){
				continue;
			} else if(waiting_customers[i]->arrival_time == customer->arrival_time){
				if(waiting_customers[i]->service_time < customer->service_time){
					continue;
				} else if(waiting_customers[i]->service_time == customer->service_time){
					if(waiting_customers[i]->ID < customer->ID){
						continue;
					} else{
						break;
					}
				} else if(waiting_customers[i]->service_time > customer->service_time){
					break;
				}
			} else if(waiting_customers[i]->arrival_time > customer->arrival_time){
				break;
			}
		} else if(waiting_customers[i]->priority < customer->priority){
			break;
		}
	}
	int j;
	//move all the existing customers after the new one's spot up to make room
	for(j=num_waiting; j>i; j--){
		waiting_customers[j] = waiting_customers[j-1];
	}
	//then put the new customer in its spot, and increment the list count
	waiting_customers[i] = customer;
	num_waiting++;
}

/*******************************************End of Monitor********************************************/


int main(int argc, char* argv[]){
	//initialize
	FILE *fp;
	int num_customers;
	int id;
	int aT;
	int sT;
	int p;
	if(argc != 2){
		printf("Invalid number of arguments\n");
		return -1;
	}
	fp = fopen(argv[1] , "r");
	if(fp == NULL){
		perror("Error opening file");
		return -1;
	}

	//Set the origin time for the program
	gettimeofday(&origin,NULL);

	fscanf(fp,"%d", &num_customers);
	pthread_t thread_array[num_customers];
	//printf("%d\n", num_customers);
	struct customer* customer_array[num_customers];
	int i;
	for(i=0; i < num_customers; i++){
		struct customer *populate = malloc(sizeof(struct customer));
		fscanf(fp, "%d%*[:]%d%*[,]%d%*[,]%d", &id, &aT, &sT, &p);
		//printf("%d:%d,%d,%d\n", id, aT, sT, p);
		populate->ID = id;
		populate->arrival_time = aT;
		populate->service_time = sT;
		populate->priority = p;
		customer_array[i] = populate;
		if(pthread_create(&thread_array[i], 0, (void*)customer_thread,
		(void*)customer_array[i])){
			printf("There was an error creating the thread\n");
			return -1;
		}else
			printf("Customer no.%2d thread created.\n", customer_array[i]->ID);
	}

	//wait for each thread to terminate before exiting the program

	for(i = 0; i < num_customers; i++){
		if(pthread_join(thread_array[i], NULL)){
			printf("There was an error in pthread_join.\n");
			return -1;
		}
	}
	printf("\n###### END OF PQS ######\n");
	return 0;
}
