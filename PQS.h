/*  
 *  PQS.h
 */ 
 
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<pthread.h>
#include<unistd.h>
#include<sys/time.h>

//structure of slave info.
typedef struct customer{
	int ID;
	int arrival_time;
	int service_time;
	int priority;
}customer;

void* customer_thread(struct customer*);
void request_clerk(struct customer*);
void release_clerk();
void update_list();
