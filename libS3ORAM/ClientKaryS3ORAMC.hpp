#ifndef CLIENTS3CORAM_H
#define CLIENTS3CORAM_H

#include "config.h"
#include <pthread.h>
#include "zmq.hpp"
#include "struct_socket.h"

class ClientKaryS3ORAMC
{
private:


    //specific variable for S3CORAM
    TYPE_DATA** STASH;
    TYPE_INDEX* metaStash;
    
    
    int deepest[HEIGHT+2];
	int target[HEIGHT+2]; 
	int deepestIdx[HEIGHT+2];
    
    
	//client storage for ORAM operations
    TYPE_ID** metaData; //this is for position_map scanning optimization
    TYPE_POS_MAP* pos_map;

    //variables for retrieval operation
	TYPE_INDEX numRead;
	TYPE_DATA** sharedVector;
    
    TYPE_DATA** retrievedShare;
    TYPE_DATA* recoveredBlock;
    
    unsigned char** vector_buffer_out;	
    unsigned char** block_buffer_out; 
    unsigned char** blocks_buffer_in;
    
    //variables for eviction
    TYPE_INDEX numEvict;
    TYPE_DATA*** sharedMatrix;
	TYPE_DATA** evictMatrix;    //[H+1][(BUCKET_SIZE+1)*(BUCKET_SIZE+1)]; 
	unsigned char** evict_buffer_out;
    
    int countNumBlockInStash();
    //thread
	pthread_t thread_sockets[NUM_SERVERS];

#if defined(PRECOMP_MODE)
	TYPE_DATA** precompOnes;
	TYPE_DATA** precompZeros;
#endif




public:
    ClientKaryS3ORAMC();
    ~ClientKaryS3ORAMC();
    
    static zmq::context_t **context;
    static zmq::socket_t **socket;
    
    
    
    //main functions
    int init();
    int loadState();
    int saveState();
    int access(TYPE_ID blockID);
    int sendORAMTree();
    
    //retrieval_vector
    int getLogicalVector(TYPE_DATA* logicalVector, TYPE_ID blockID);

    //eviction_matrix
    int getEvictMatrix(TYPE_INDEX n_evict);
    

    //socket
	static void* thread_socket_func(void* args);	
    static int sendNrecv(int peer_idx, unsigned char* data_out, size_t data_out_size, unsigned char* data_in, size_t data_in_size, int CMD);

    //logging
	static unsigned long int exp_logs[9]; 	static unsigned long int thread_max;
	static char timestamp[16];


};

#endif // CLIENTS3CORAM_H
