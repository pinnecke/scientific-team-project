#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include "priority_queue.h"
#include "thread_pool.h"
#include "thread_pool_monitoring.h"
#include <time.h>
#include <pthread.h>

#define WORK_SIZE 4096
#define NUM_TASKS 2048
#define MEASURE_COUNT 10 

static double test_results_pu[64];
static long long test_results_ps1[64];
static long long test_results_ps2[64];

static long long test_results_baseline[64];
static long long test_results_pool[64];

void work_large(void* args)
{
	double* res = args;
	double exp = *res;

	double v = 0.0;
	for(double i = 0; i < WORK_SIZE; ++i)
	{
		v += pow(i, 1 / exp);
	}
	*res = v;
}

void work(void* args)
{
	int* res = args;
	int sum = 0;
	for(int i = 0; i <= *res; ++i)
		sum += i * i / 8;

	*res = sum;
//	printf("%d %d\n", n, sum);
}

void resize_test()
{
	thread_pool_t* pool = thread_pool_create(2, 0);
	task_handle_t hndl;
	const int numThreads = 1 << 11;
	thread_task_t    tasks[numThreads];
	int results[numThreads];
	for(int i = numThreads-1; i >= 0; --i){
		results[i] = i;
		tasks[i].args = &results[i];
		tasks[i].routine = work;
		tasks[i].priority = 0;
		if(i == 0) tasks[i].priority = 1;
		thread_pool_enqueue_task(&tasks[i], pool, &hndl);
		if( i == numThreads * 1 / 3) thread_pool_resize(pool, 4);
		if(i == numThreads * 2 / 3) thread_pool_resize(pool, 3);
		if(i == numThreads * 3 / 4) thread_pool_resize(pool, 2);
	}

//	thread_pool_wait_for_task(pool, &hndl);
	thread_pool_wait_for_all(pool);
	thread_pool_free(pool);

	// verify results
	int sum = 0;
	for(int i = 0; i < numThreads; ++i)
	{
		sum += i*i / 8;
		if(results[i] != sum) printf("error at %d: %d != %d\n", i, sum, results[i]);
	}
	printf("interleaving test completed.\n");

}

void performance_test(int numThreads, int numTasks, FILE *pu, FILE *ps)
{
//	clock_t begin = clock();
//	double exp = 2.7;
//	for(int i = 0; i < 2048; ++i)
//		work_large(&exp);
//	clock_t end = clock();
//	float time = (double)(end - begin) / CLOCKS_PER_SEC;
//	time /= 2048;
//	printf("time for one task: time: %f | %f\n", time, exp);

	thread_pool_t* pool = thread_pool_create(numThreads, 1);

	double results[numTasks];
	thread_task_t tasks[numTasks];

	for(int i = 0; i < numTasks; ++i)
	{
		results[i] = (double)(i+1);
		tasks[i].args = &results[i];
		tasks[i].routine = work_large;
		tasks[i].priority = 0;
		thread_pool_enqueue_task(&tasks[i], pool, NULL);
	}
	sleep(1);

	float a = thread_pool_get_time_working(pool);
    thread_pool_stats stats = thread_pool_get_stats(pool);
//	printf("fraction of time spend working: %f\n",a);

	fprintf(pu, "%i %f\n", numThreads, a);
    fprintf(ps, "%i %lli %lli\n", numThreads, stats.avg_complete_time, stats.avg_wait_time);

	thread_pool_free(pool);
}

float wait_performance_test(int numThreads, int numTasks)
{
	thread_pool_t* pool = thread_pool_create(numThreads, 1);

	double results[numTasks];
	thread_task_t tasks[numTasks];

	for(int i = 0; i < numTasks; ++i)
	{
		results[i] = (double)(i+1);
		tasks[i].args = &results[i];
		tasks[i].routine = work_large;
		tasks[i].priority = i;
		task_handle_t hndl;
		thread_pool_enqueue_task(&tasks[i], pool, &hndl);
		//if(i % 11 == 0) thread_pool_wait_for_task(pool, &hndl);
	}
	thread_pool_wait_for_all(pool);

	float a = thread_pool_get_time_working(pool);
//	printf("fraction of time spend working: %f\n",a);
    thread_pool_stats stats = thread_pool_get_stats(pool);
	test_results_pu[numThreads] += a;
	test_results_ps1[numThreads] += stats.avg_complete_time;
	test_results_ps2[numThreads] += stats.avg_wait_time;

	thread_pool_free(pool);
	return a;
}

void cmp_pool(int numThreads)
{
	thread_pool_t* pool = thread_pool_create(numThreads, 1);
	struct timespec begin, end;

	clock_gettime(CLOCK_MONOTONIC, &begin);
	double results[numThreads];
	thread_task_t tasks[numThreads];
	
	for(int i = 0; i < numThreads; ++i)
	{
		results[i] = (double)(i+1);
		tasks[i].args = &results[i];
		tasks[i].routine = work_large;
		thread_pool_enqueue_task(&tasks[i], pool, NULL);
	}

	thread_pool_wait_for_all(pool);
	
	clock_gettime(CLOCK_MONOTONIC, &end);
	test_results_pool[numThreads] += __get_time_diff(&begin, &end);

	thread_pool_free(pool);
}

void cmp_baseline(int numThreads)
{
	struct timespec begin, end;
	clock_gettime(CLOCK_MONOTONIC, &begin);

	double results[numThreads];
	thread_task_t tasks[numThreads];

	pthread_t threads[numThreads];
    for (int tid = 1; tid < numThreads; tid++)
    {
		results[tid] = (double)(tid+1);
        pthread_create(&threads[tid] , NULL, &work_large, &results[tid]);
    }
	// Let the calling thread process one task before waiting
    work_large(&results[0]);

    for (register int tid = 1; tid < numThreads; tid++) {
        pthread_join(threads[tid], NULL);
    }

	clock_gettime(CLOCK_MONOTONIC, &end);
	test_results_baseline[numThreads] += __get_time_diff(&begin, &end);
}

int main()
{
/*	priority_queue_t queue;
	priority_queue_init(&queue);

	for(size_t i = 0; i < 256; ++i)
	{
		priority_queue_push(&queue, (void*)(255-i), 255-i);	
	}

	while(!priority_queue_is_empty(&queue))
	{
		size_t i = (size_t)priority_queue_pop(&queue);
		printf("%I64d\n",i);
	}*/

	//resize_test();
	FILE *ps;
	FILE *pu;
	FILE *puw;
	FILE *psw;
	FILE *pb;

    ps = fopen("Statistics/pool_avg.csv","w+");
    fprintf(ps, "Threads BusyTime IdleTime \n");
	pb = fopen("Statistics/pool_baseline.csv","w+");
    fprintf(pu, "Thread pool vs Baseline \n");

    psw = fopen("Statistics/pool_avg_wait.csv","w+");
    fprintf(psw, "Threads BusyTime IdleTime \n");
    puw = fopen("Statistics/pool_util_wait.csv","w+");
    fprintf(puw, "Threads Util \n");
	pu = fopen("Statistics/pool_util.csv","w+");
	fprintf(pu, "Threads Util \n");
	
	//fprintf(puw, "%i %f \n",numThreads, a);
	//for(int i = 2; i< 33; ++i){
	//	performance_test(i, 4000,ps, pu);
//	}

//	float sum = 0.f;

	for(int j = 0; j < MEASURE_COUNT; j++) 
	{

		for(int i = 2; i < 33; i = i * 2)
		{
			wait_performance_test(i, 10000);
			cmp_baseline(i);
			cmp_pool(i);
		}
	}
	for(int i = 2; i < 33; i = i * 2)
	{
		fprintf(pu, "%i %f \n",i, test_results_pu[i] / MEASURE_COUNT);
		fprintf(puw, "%i %f \n",i, test_results_pu[i] / MEASURE_COUNT);
		fprintf(pb, "%i %lli %lli \n", i, test_results_baseline[i] / (MEASURE_COUNT), test_results_pool[i] / (MEASURE_COUNT));
		fprintf(ps, "%i %lli %lli \n", i, test_results_ps1[i] / MEASURE_COUNT, test_results_ps2[i] / MEASURE_COUNT);
		fprintf(psw, "%i %lli %lli \n", i, test_results_ps1[i] / MEASURE_COUNT, test_results_ps2[i] / MEASURE_COUNT);
		//wait_performance_test(i, 6000,psw, puw);

	}
//	sum /= 1000;
//	printf("average: %f\n", sum);
//	getchar();
	return 0;
}
