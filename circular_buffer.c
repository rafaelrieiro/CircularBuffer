#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// @Comments
// I thought initially to have one multithreaded version and another without it, but decided not to, because:
// If it is single threaded, thus performance is not important, then it can have extra overhead with mutex locks and unlocks :-)


// @Compile Instructions 
// gcc -O3 -pthread circular_buffer.c

#define FCBUF_DO_NOT_OVERWRITE   0x0001
#define FCBUF_OVERWRITE          0x0000
#define FCBUF_RESIZE_AUTO        0x0002
#define FCBUF_WRITE_TIMESTAMP    0x0004

#define MAXCBFSIZE 0xFFFFFFFF //uint32_t Max
#define INCREASESTEPCBUFSIZE 64
#define NUMBEROFTHREADS 12

// The hidden definition of our circular buffer structure
struct circular_buf_t {
	uint32_t * buffer;
	uint32_t head;
	uint32_t tail;
	uint32_t max; // max no. of elements
	uint32_t elemSize;
	uint32_t overwrites;
	pthread_mutex_t mutex;	 
	bool full;
};

// Opaque circular buffer structure
typedef struct circular_buf_t circular_buf_t;
// Handle type, the way users interact with the API
typedef circular_buf_t* cbuf_handle_t;
cbuf_handle_t cbufglobal;
//#include "circular_buffer.h" // putting all together in .c file instead of having .h separated
// https://embeddedartistry.com/blog/2017/05/17/creating-a-circular-buffer-in-c-and-c/

pthread_t threads[NUMBEROFTHREADS];
pthread_t threadsPut[NUMBEROFTHREADS];
uint32_t count[NUMBEROFTHREADS];
uint32_t countPut[NUMBEROFTHREADS];
cbuf_handle_t circular_buf_init(uint32_t size, uint32_t elemSize);

void circular_buf_free(cbuf_handle_t cbuf);
bool circular_buf_full(cbuf_handle_t cbuf);
bool circular_buf_empty(cbuf_handle_t cbuf);
uint32_t circular_buf_capacity(cbuf_handle_t cbuf);
uint32_t circular_buf_size(cbuf_handle_t cbuf);
uint32_t circular_buf_get_overwrites(cbuf_handle_t cbuf);

static void circular_buf_reset(cbuf_handle_t cbuf);
static void advance_pointer(cbuf_handle_t cbuf);
static void retreat_pointer(cbuf_handle_t cbuf);

void circular_buf_put(cbuf_handle_t cbuf, const void * data);
int circular_buf_get(cbuf_handle_t cbuf, void * data);
int circular_buf_resize(cbuf_handle_t cbuf, uint32_t newsize);

void *circular_buf_get_all(void* param);
void *circular_buf_put_all_sleep(void* param);
void *circular_buf_get_all_sleep(void* param);


int circular_buf_resize(cbuf_handle_t cbuf, uint32_t newsize)
{
  
  pthread_mutex_lock(&cbuf->mutex);
  if ( (newsize > circular_buf_capacity(cbuf) ) && ( newsize + circular_buf_capacity(cbuf) < MAXCBFSIZE ) ) {
	  
    cbuf->buffer = realloc(cbuf->buffer,(newsize*cbuf->elemSize));
    cbuf->max = newsize;
    cbuf->full = false;
    
    pthread_mutex_unlock(&cbuf->mutex);
    return 0; 
    }
  pthread_mutex_unlock(&cbuf->mutex);
  return -1;
}

cbuf_handle_t circular_buf_init(uint32_t size,uint32_t elemSize)
{
	if ( size > MAXCBFSIZE ) return NULL;
	  
	cbuf_handle_t cbuf = malloc(sizeof(circular_buf_t));
	cbuf->buffer = malloc(size*elemSize);
    
	cbuf->max = size;
	circular_buf_reset(cbuf);
    cbuf->elemSize = elemSize;   
    pthread_mutex_init(&cbuf->mutex,NULL);
	assert(circular_buf_empty(cbuf));

	return cbuf;
}

static void circular_buf_reset(cbuf_handle_t cbuf)
{
    assert(cbuf);

    cbuf->head = 0;
    cbuf->tail = 0;
    cbuf->full = false;
    cbuf->overwrites = 0;
}

void circular_buf_free(cbuf_handle_t cbuf)
{
	assert(cbuf);
	free(cbuf->buffer);
	pthread_mutex_destroy(&cbuf->mutex);
	free(cbuf);
}

bool circular_buf_full(cbuf_handle_t cbuf)
{
	assert(cbuf);
	return cbuf->full;
}

uint32_t circular_buf_capacity(cbuf_handle_t cbuf)
{
	assert(cbuf);
	return cbuf->max;
}

uint32_t circular_buf_size(cbuf_handle_t cbuf)
{
	assert(cbuf);
	
	pthread_mutex_lock(&cbuf->mutex);
	uint32_t size = cbuf->max;
    
	if(!cbuf->full)
	{
		if(cbuf->head >= cbuf->tail)
		{
			size = (cbuf->head - cbuf->tail);
		}
		else
		{
			size = (cbuf->max + cbuf->head - cbuf->tail);
		}
	}
    
    pthread_mutex_unlock(&cbuf->mutex);

	return size;
}

static void advance_pointer(cbuf_handle_t cbuf)
{
	assert(cbuf);

    if(cbuf->full)
   	{
		if (++(cbuf->tail) == cbuf->max) 
		{ 
			cbuf->tail = 0;
		}
	    cbuf->overwrites++;
	}

	if (++(cbuf->head) == cbuf->max) 
	{ 
		cbuf->head = 0;
	}
	cbuf->full = (cbuf->head == cbuf->tail);
    
}

static void retreat_pointer(cbuf_handle_t cbuf)
{
	assert(cbuf);
    
    cbuf->full = false;
	if (++(cbuf->tail) == cbuf->max) 
	{ 
		cbuf->tail = 0;
	}
    
}

void circular_buf_put(cbuf_handle_t cbuf, const void * data)
{
	assert(cbuf && cbuf->buffer);

    pthread_mutex_lock(&cbuf->mutex); 
    
    //cbuf->buffer[cbuf->head] = data;
    char *p = (char *)cbuf->buffer;
    p += (cbuf->head*cbuf->elemSize);
    memcpy(p,data,cbuf->elemSize);
    
    //memcpy(&cbuf->buffer[cbuf->head],data,cbuf->elemSize);
    advance_pointer(cbuf);
    pthread_mutex_unlock(&cbuf->mutex);
}

int circular_buf_get(cbuf_handle_t cbuf, void * data)
{
    int result;
    assert(cbuf && data && cbuf->buffer);
    
    pthread_mutex_lock(&cbuf->mutex);
    if(!circular_buf_empty(cbuf))
    {
        
         char *p = (char *)cbuf->buffer;
         p += (cbuf->tail*cbuf->elemSize);
         memcpy(data,p,cbuf->elemSize);
         //memmove(data,p,cbuf->elemSize);
        //*data = cbuf->buffer[cbuf->tail];
        //memcpy(data,&cbuf->buffer[cbuf->tail],cbuf->elemSize);
        retreat_pointer(cbuf);
        cbuf->full = false;
        result = 0;
    } else 
      result = -1;
    
    pthread_mutex_unlock(&cbuf->mutex);
    
    return result;
}

uint8_t circular_buf_get2(cbuf_handle_t cbuf)
{
    uint8_t aux;
    
    if(!circular_buf_empty(cbuf))
    {
        aux = cbuf->buffer[cbuf->tail];
        retreat_pointer(cbuf);
        return aux; 
    }

    return 0;
}

bool circular_buf_empty(circular_buf_t* cbuf)
{
	// We define empty as head == tail
    //return (cbuf->head == cbuf->tail);
    return (cbuf->head == cbuf->tail && !(cbuf->full));
}

uint32_t circular_buf_get_overwrites(cbuf_handle_t cbuf)
{
   return cbuf->overwrites;
}

static int test_cbuffer_overwrite_empty();  // Creates a cbuf_overwrite and tries to read an item from an empty one.

static int test_cbuffer_overwrite_empty()
{
  	
  int cbufsize = 3;
  uint32_t data;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  if (circular_buf_get(cbuf,&data) == -1 ) 
    result = 0;
  else 
    result = -1;  
  
  circular_buf_free(cbuf); 
    
  return result;     	
}

static int test_cbuffer_overwrite_full(); // Creates a cbuf_overwrite and fills it to maximum and verify it is full

static int test_cbuffer_overwrite_full()
{
  int cbufsize = 3;
  uint32_t data;
  bool result;
  int i;
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  for ( i = 0; i < cbufsize; i++)
    circular_buf_put(cbuf,(void *)&data);
   
  result = circular_buf_full(cbuf); 
  
  circular_buf_free(cbuf); 

  if ( result == true ) 
    return 0;
  else
    return -1;  
  
}

static int test_cbuffer_overwrite_get_size(); // Creates a cbuf, insert x itens and verify the size

static int test_cbuffer_overwrite_get_size()
{

  int cbufsize = 3;
  uint32_t data;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  
  if ( circular_buf_size(cbuf)  == 2)
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
    
  return result;     	
}

static int test_cbuffer_overwrite_do_one_overwrite(); // Creates a buf, and overwrite one item

static int test_cbuffer_overwrite_do_one_overwrite()
{
	
  int cbufsize = 3;
  uint32_t data = 10;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  // buffer is full
  
  data = 50;
  circular_buf_put(cbuf,(void *)&data);
  // overwrite one element
  
  circular_buf_get(cbuf,&data);
  //printf("data: %d\n",data);
  circular_buf_get(cbuf,&data);
  //printf("data: %d\n",data);
  circular_buf_get(cbuf,&data);
  //printf("data: %d\n",data);
  // get all three elements, the third should be 50 and numberof overwrites = 1
  
  if ( ( circular_buf_get_overwrites(cbuf) == 1 ) && data == 50 )
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
    
  return result;     	
	
}

static int test_cbuffer_overwrite_partial_read(); 

static int test_cbuffer_overwrite_partial_read()
{

  int cbufsize = 3;
  uint32_t data = 10;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  // buffer is full
     
  circular_buf_get(cbuf,&data);
  circular_buf_get(cbuf,&data);
  // get two from three elements, size should be one
  
  if ( ( circular_buf_get_overwrites(cbuf) == 0 ) && (circular_buf_size(cbuf) == 1 ) && (!circular_buf_full(cbuf)) )
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
    
  return result;     	

}

static int test_cbuffer_overwrite_full_read();

static int test_cbuffer_overwrite_full_read() 
{
  int cbufsize = 3;
  uint32_t data = 10;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  circular_buf_put(cbuf,(void *)&data);
  // buffer is full
     
  circular_buf_get(cbuf,&data);
  circular_buf_get(cbuf,&data);
  circular_buf_get(cbuf,&data);
  
  // get all elements
  
  if ( ( circular_buf_get_overwrites(cbuf) == 0 ) && (circular_buf_size(cbuf) == 0 ) && (!circular_buf_full(cbuf)) )
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
    
  return result;     	
}

static int test_cbuffer_overwrite_resize();

static int test_cbuffer_overwrite_resize() 
{
  int cbufsize = 3;
  uint32_t data = 10;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  data += 10;
  circular_buf_put(cbuf,(void *)&data);
  
  circular_buf_resize(cbuf,cbufsize+2);
   
  if ( (circular_buf_capacity(cbuf) == 5 ) && (!circular_buf_full(cbuf)) && (circular_buf_size(cbuf) == 2 ) )
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
    
  return result;     	
}

static int test_cbuffer_overwrite_check_read_order();

static int test_cbuffer_overwrite_check_read_order()
{
  int cbufsize = 3;
  uint32_t data = 10;
  int result = 0;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  data += 10;
  circular_buf_put(cbuf,(void *)&data);
  data += 10;
  circular_buf_put(cbuf,(void *)&data);
  // buffer is full
     
  circular_buf_get(cbuf,&data);
  
  if ( data != 10 ) result = -1;
  
  circular_buf_get(cbuf,&data);
  
  if ( data != 20 ) result = -1;
  
  circular_buf_get(cbuf,&data);
  
  if ( data != 30 ) result = -1;
  
  // get all elements
  /*
  printf("result after compare reading order %d\n",result);
  printf("get overwrites: %d\n",circular_buf_get_overwrites(cbuf));
  printf("get size: %d\n",circular_buf_size(cbuf));
  printf("get if is full:%d\n",circular_buf_full(cbuf));
  */
  if ( ( circular_buf_get_overwrites(cbuf) == 0 ) && (circular_buf_size(cbuf) == 0 ) && (circular_buf_full(cbuf)==0) && (result == 0))
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
  
  return result;

}

static int test_cbuffer_overwrite_resize_and_operate();

static int test_cbuffer_overwrite_resize_and_operate() 
{
  int cbufsize = 3;
  uint32_t data = 10;
  int result;
  
  cbuf_handle_t cbuf = circular_buf_init(cbufsize,sizeof(uint32_t));
  
  circular_buf_put(cbuf,(void *)&data);
  data += 10;
  circular_buf_put(cbuf,(void *)&data);
  
  circular_buf_resize(cbuf,cbufsize+2);
   
  if ( (circular_buf_capacity(cbuf) == 5 ) && (!circular_buf_full(cbuf)) && (circular_buf_size(cbuf) == 2 ) )
    result = 0;
  else
    result = -1;  
    
  circular_buf_get(cbuf,&data);
  //printf("Resize ANd operate data1: %d\n",data);
  circular_buf_get(cbuf,&data);
  //printf("Resize ANd operate data2: %d\n",data);
  
  if ((circular_buf_capacity(cbuf) == 5 ) &&  (!circular_buf_full(cbuf)) && circular_buf_size(cbuf) == 0 && result == 0 )
    result = 0;
  else
    result = -1;  
    
  circular_buf_free(cbuf); 
    
  return result;     	
}

static int test_cbuffer_overwrite_multiple_reading_threads() // one put, multiple gets simultaneously
{

  int cbufSize = 9000000;
  uint32_t data;
  bool result;
  int i;
  uint32_t *ptrCount[NUMBEROFTHREADS];
  uint32_t sum = 0 ;
  int thread;
  clock_t startTime,endTime;
  
  cbufglobal = circular_buf_init(cbufSize,sizeof(uint32_t));
  startTime=clock();
    
  for (i=1;i<=cbufSize;i++)
    circular_buf_put(cbufglobal,(void *)&i);
   
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
     pthread_create(&threads[thread], NULL, &circular_buf_get_all,NULL);
   
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
       pthread_join(threads[thread], (void **)&ptrCount[thread]);
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
   
     printf("Value of return count from thread %d is : %d; Percentage of Processing: %d % \n",thread,*ptrCount[thread],((*ptrCount[thread]*100)/cbufSize));
     sum += *ptrCount[thread];
   }
   
  printf("Value of return count from All Threads is : %d\n",sum);
      
  circular_buf_free(cbufglobal); 
  
  endTime=clock();
  printf("Time Elapsed: %.4f seconds\n",((double)(endTime - startTime)/CLOCKS_PER_SEC));
  
  if ( sum == cbufSize ) 
    return 0;
  else
    return -1;  

}

static int test_cbuffer_overwrite_multiple_RW_threads() // multiple puts and gets simultaneously
{

  int cbufSize = 90000000;
  uint32_t data;
  bool result;
  int i;
  uint32_t *ptrCount[NUMBEROFTHREADS];
  uint32_t *ptrCountPut[NUMBEROFTHREADS];
  uint32_t sum = 0 ;
  uint32_t sumPut = 0;
  int thread;
  clock_t startTime, endTime;
  
  cbufglobal = circular_buf_init(cbufSize,sizeof(uint32_t));
  startTime = clock();
    
  //for (i=1;i<=cbufSize;i++)
  //  circular_buf_put(cbufglobal,(void *)&i);
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
     pthread_create(&threadsPut[thread], NULL, &circular_buf_put_all_sleep,NULL);
   
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
     pthread_create(&threads[thread], NULL, &circular_buf_get_all_sleep,NULL);
   
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
       pthread_join(threads[thread], (void **)&ptrCount[thread]);
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
       pthread_join(threadsPut[thread], (void **)&ptrCountPut[thread]);
  
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	sum += *ptrCount[thread];
    sumPut += *ptrCountPut[thread];
   }   
    
   for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
   
     printf("Value of return count from thread Read %d is : %d; Percentage of Processing: %d % \n",thread,*ptrCount[thread],((*ptrCount[thread]*100)/sum));
     printf("Value of return count from thread Write %d is : %d; Percentage of Processing: %d % \n",thread,*ptrCountPut[thread],((*ptrCountPut[thread]*100)/sumPut));
     
  } 
  
  printf("Value of return count from All Threads Get is : %d\n",sum);
  printf("Value of return count from All Threads Put is : %d\n",sumPut);    
  circular_buf_free(cbufglobal); 
  endTime=clock();
  
  printf("Time Elapsed: %.4f seconds\n",((double)(endTime - startTime)/CLOCKS_PER_SEC));
  
  if ( sum == sumPut ) 
    return 0;
  else
    return -1;  

}

void *circular_buf_get_all(void* param) 
{
  
  extern cbuf_handle_t cbufglobal;
  pthread_t tid = pthread_self();
  int thread;
  uint32_t data;
  
   for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	  
	  if (pthread_equal(tid,threads[thread])) count[thread]=0;
	     
   }
    
  while (circular_buf_get(cbufglobal,&data) != -1 ) {
	 
	 //printf("Valor %d - Thread No:%d\n",data,tid); 
	 	 
	 for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
		 
	   if (pthread_equal(tid,threads[thread]))
	     count[thread]++;
     }
  } 
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	  
	   if (pthread_equal(tid,threads[thread])) pthread_exit(&count[thread]);
  }
   
}

void *circular_buf_get_all_sleep(void* param) 
{ 
  
  extern cbuf_handle_t cbufglobal;
  pthread_t tid = pthread_self();
  int thread;
  uint32_t data;
  
   for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	  
	  if (pthread_equal(tid,threads[thread])) count[thread]=0;
	     
   }
    
  while (circular_buf_get(cbufglobal,&data) != -1 ) {
	 
	 //printf("Valor %d - Thread No:%d\n",data,tid); 
	 	 
	 for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
		 
	   if (pthread_equal(tid,threads[thread])) {
		   
		count[thread]++;
		if (count[thread] % 10 == 0) usleep(300);  //make sure for testing consumer is slower than producer
       }
     }
   
  } 
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	  
	   if (pthread_equal(tid,threads[thread])) pthread_exit(&count[thread]);
  }
   
}

void *circular_buf_put_all_sleep(void* param)
{
  
  extern cbuf_handle_t cbufglobal;
  pthread_t tid = pthread_self();
  int thread;
  uint32_t data = 0;
  uint32_t numberElements = 9000;
  int i;
  
   for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	  
	  if (pthread_equal(tid,threadsPut[thread])) countPut[thread]=0;
	     
   }
    
  for (i=0; i<numberElements; i++) {
	 
	 data++;
	 circular_buf_put(cbufglobal,&data);
	 
	 //printf("Valor %d - Thread No:%d\n",data,tid); 
	 	 
	 for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
		 
	   if (pthread_equal(tid,threadsPut[thread])) {
		   
		countPut[thread]++;
		if (countPut[thread] % 10 == 0) usleep(100); //make sure for testing producer is faster than consumer
       }
     }
   
  } 
  
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
	  
	   if (pthread_equal(tid,threadsPut[thread])) pthread_exit(&countPut[thread]);
  }
   
}



int test_cbuffer_overwrite()
{
  int i;
  printf("Test CBuffer Overwrite : Get from Empty set Result : %s\n",(test_cbuffer_overwrite_empty()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Check if its full after filling with elements: %s\n",(test_cbuffer_overwrite_full()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Check Size: %s\n",(test_cbuffer_overwrite_get_size()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Do one overwrite: %s\n",(test_cbuffer_overwrite_do_one_overwrite()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Partial Read: %s\n",(test_cbuffer_overwrite_partial_read()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Full Read: %s\n",(test_cbuffer_overwrite_full_read()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Resize: %s\n",(test_cbuffer_overwrite_resize()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m"); 
  printf("Test CBuffer Overwrite : Read Check Order: %s\n",(test_cbuffer_overwrite_check_read_order()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m");
  printf("Test CBuffer Overwrite : Resize and Operate: %s\n",(test_cbuffer_overwrite_resize_and_operate()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m");
  printf("Test CBuffer Overwrite : Multiple Threads Reading: %s\n",(test_cbuffer_overwrite_multiple_reading_threads()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m");
  printf("Test CBuffer Overwrite : Multiple Threads Read/Write: %s\n",(test_cbuffer_overwrite_multiple_RW_threads()==0)?"\033[32;1;4m[OK]\033[0m":"\033[31;1;4m[NOK]\033[0m");
}


int main(int argc, char** argv)
{
  
  test_cbuffer_overwrite();
  
  //test_cbuffer_overwrite_multiple_RW_threads() ;
  
  
  cbuf_handle_t cbufFloat = circular_buf_init(10,sizeof(float));
  
  float data=1.50;
  int i;
  printf("Size of float: %d\n",sizeof(float));
  for (i=0;i<10;i++) {
	   
    circular_buf_put(cbufFloat,&data);
    printf("Valor Double before put: %f\n",data);
    data +=1.55123456789;   
  }
  
  while (  circular_buf_get(cbufFloat,&data)!= -1){
    printf("Valor Double after get: %f\n",data);
  }
    
  circular_buf_free(cbufFloat);
  
   char str[100];
   cbuf_handle_t cbufString = circular_buf_init(6,sizeof(str));
   printf("Size of str: %d\n",sizeof(str));
   
   strcpy(str,"Opa\0");
   circular_buf_put(cbufString,&str[0]);
   
   strcpy(str,"Não\0");
   circular_buf_put(cbufString,&str[0]);
   
   strcpy(str,"Sei\0");
   circular_buf_put(cbufString,&str[0]);
   
   strcpy(str,"Se\0");
   circular_buf_put(cbufString,&str[0]);

   strcpy(str,"Vai\0");
   circular_buf_put(cbufString,&str[0]);
   
   strcpy(str,"Funcionar\0");
   circular_buf_put(cbufString,&str[0]);
   
   strcpy(str,"Epa! Sobrescrevi!\0");
   circular_buf_put(cbufString,&str[0]);
   
   
   //circular_buf_put(cbufFloat,"Nao       ");
   //circular_buf_put(cbufFloat,"Sei       ");
   //circular_buf_put(cbufFloat,"Se        ");
   //circular_buf_put(cbufFloat,"Vai       ");
   //circular_buf_put(cbufFloat,"Funcionar ");
   
   
    while (circular_buf_get(cbufString,&str[0])!= -1) 
      printf("String from cbuf: %s\n",str);
  
  circular_buf_free(cbufString);
   
  /*
  int i;
  uint32_t *ptrCount[NUMBEROFTHREADS];
  uint32_t sum = 0 ;
  int thread;
  
  cbufglobal = circular_buf_init(10000);
  
  for (i=1;i<=10000;i++)
    circular_buf_put(cbufglobal,i);
   
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
     pthread_create(&threads[thread], NULL, &circular_buf_get_all,NULL);
     
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) 
       pthread_join(threads[thread], (void **)&ptrCount[thread]);
     
  for (thread = 0; thread < NUMBEROFTHREADS; thread++) {
   
     printf("Value of return count from thread %d is : %d\n",thread,*ptrCount[thread]);
     sum += *ptrCount[thread];
   }
   
   printf("Value of return count from All Threads is : %d\n",sum);
   circular_buf_free(cbufglobal);
 */
  exit(0);
}
