#include "ServerKaryS3ORAMC.hpp"

#include "Utils.hpp"
#include "struct_socket.h"

#include "S3ORAM.hpp"
#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>

#include "struct_thread_computation.h"
#include "struct_thread_loadData.h"


unsigned long int ServerKaryS3ORAMC::server_logs[13];
unsigned long int ServerKaryS3ORAMC::thread_max = 0;


zmq::context_t** ServerKaryS3ORAMC::context_send = new zmq::context_t*[NUM_SERVERS-1];
zmq::socket_t**  ServerKaryS3ORAMC::socket_send = new zmq::socket_t*[NUM_SERVERS-1];
    

zmq::context_t** ServerKaryS3ORAMC::context_recv = new zmq::context_t*[NUM_SERVERS-1];
zmq::socket_t** ServerKaryS3ORAMC::socket_recv = new zmq::socket_t*[NUM_SERVERS-1];
char ServerKaryS3ORAMC::timestamp[16];

ServerKaryS3ORAMC::ServerKaryS3ORAMC(TYPE_INDEX serverNo, int selectedThreads) 
{
	
    //specific
    this->evict_buffer_in = new unsigned char[  2 * ((sizeof(TYPE_DATA) * DATA_CHUNKS) + 
                                                (H+1)*evictMatSize*sizeof(TYPE_DATA) ) + 
                                                sizeof(TYPE_INDEX)];
        
    this->shares_buffer_in = new unsigned char*[NUM_SERVERS-1];
	for (TYPE_INDEX k = 0 ; k < NUM_SERVERS-1; k++)
	{
		this->shares_buffer_in[k] = new unsigned char[2 * ((BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS)];
	}

	this->shares_buffer_out = new unsigned char*[NUM_SERVERS-1];
	for (TYPE_INDEX k = 0 ; k < NUM_SERVERS-1; k++)
	{
		this->shares_buffer_out[k] = new unsigned char[2*((BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS)];
	}
    
    
    bucket_buffer = new unsigned char[(BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS];
   
   
    this->cross_product_vector = new zz_p**[2];
    this->evictMatrix = new zz_p***[2];
    this->ownShares = new TYPE_DATA***[2];
    this->BUCKET_DATA = new TYPE_DATA**[2];
    this->thread_compute = new pthread_t*[2];

    for(int e = 0 ; e < 2; e++)
    {
        this->cross_product_vector[e] = new zz_p*[DATA_CHUNKS];
        for (TYPE_INDEX k = 0 ; k < DATA_CHUNKS; k++)
        {
            this->cross_product_vector[e][k]  = new zz_p[BUCKET_SIZE+1];	
        }
        this->evictMatrix[e] = new zz_p**[H+1];
        for(TYPE_INDEX y = 0 ; y < H+1; y++)
        {
            this->evictMatrix[e][y] = new zz_p*[BUCKET_SIZE+1];
            for(TYPE_INDEX i = 0 ; i < BUCKET_SIZE+1; i++)
            {
                this->evictMatrix[e][y][i] = new zz_p[BUCKET_SIZE+1];
            }
            
        }
        this->ownShares[e] = new TYPE_DATA**[NUM_SERVERS];
        for(TYPE_INDEX i = 0 ; i < NUM_SERVERS ;  i++)
        {
            this->ownShares[e][i] = new TYPE_DATA*[DATA_CHUNKS];
            for(TYPE_INDEX ii = 0 ; ii < DATA_CHUNKS ;  ii++)
            {
                this->ownShares[e][i][ii] = new TYPE_DATA[BUCKET_SIZE+1];
            }
        }
        this->BUCKET_DATA[e] = new TYPE_DATA*[DATA_CHUNKS];
        for (TYPE_INDEX y = 0 ; y < DATA_CHUNKS ; y++)
        {
            this->BUCKET_DATA[e][y] = new TYPE_DATA[BUCKET_SIZE+1];
        }
    
        this->thread_compute[e] = new pthread_t[numThreads];
    }	
	

    //specific for S3ORAM
    //this->block_buffer_in = new unsigned char[sizeof(TYPE_DATA)*DATA_CHUNKS+ sizeof(TYPE_INDEX)];
    
    
    //inherent
	this->CLIENT_ADDR = "tcp://*:" + std::to_string(SERVER_PORT+(serverNo)*NUM_SERVERS+serverNo);
    
    this->numThreads = selectedThreads;
    
	cout<<endl;
	cout << "=================================================================" << endl;
	cout<< "Starting Server-" << serverNo+1 <<endl;
	cout << "=================================================================" << endl;
	this->serverNo = serverNo;
	
	TYPE_INDEX m = 0;
	for (TYPE_INDEX k = 0 ; k < NUM_SERVERS; k++)
	{
		if(k != serverNo)
		{
			this->others[m] = k;
			m++;
		}
	}
    sumBlock = new TYPE_DATA[DATA_CHUNKS];
	
	this->select_buffer_in = new unsigned char[sizeof(TYPE_INDEX)+(H+1)*BUCKET_SIZE*sizeof(TYPE_DATA)];
	this->block_buffer_out = new unsigned char[sizeof(TYPE_DATA)*DATA_CHUNKS];

	
	this->dot_product_vector = new zz_p*[DATA_CHUNKS];
	for (TYPE_INDEX k = 0 ; k < DATA_CHUNKS; k++)
	{
		this->dot_product_vector[k] = new zz_p[BUCKET_SIZE*(H+1)];
	}
	
	
	time_t rawtime = time(0);
	tm *now = localtime(&rawtime);

	if(rawtime != -1)
		strftime(timestamp,16,"%d%m_%H%M",now);
        
    //socket

    
    for(int i = 0 ; i < NUM_SERVERS-1;i ++)
    {
        context_send[i] = new zmq::context_t(1);
        socket_send[i] = new zmq::socket_t(*context_send[i],ZMQ_REQ);
        string send_address = SERVER_ADDR[this->others[i]] + ":" + std::to_string(SERVER_PORT+this->others[i]*NUM_SERVERS+this->serverNo);
        cout<<"Opening "<<send_address<<" for sending...";
        socket_send[i]->connect(send_address);
        cout<<"OK!"<<endl;
                
        context_recv[i] = new zmq::context_t(2);
        socket_recv[i] = new zmq::socket_t(*context_recv[i],ZMQ_REP);
        string recv_address = "tcp://*:" + std::to_string(SERVER_PORT+(serverNo)*(NUM_SERVERS)+this->others[i]);
        cout<<"Opening "<<recv_address<<" for listening...";
        socket_recv[i]->bind(recv_address);
        cout<<"OK!"<<endl;
        
    }
   
    	
}

ServerKaryS3ORAMC::ServerKaryS3ORAMC()
{
}

ServerKaryS3ORAMC::~ServerKaryS3ORAMC()
{
}


/**
 * Function Name: start
 *
 * Description: Starts the server to wait for a command from the client. 
 * According to the command, server performs certain subroutines for distributed ORAM operations.
 * 
 * @return 0 if successful
 */ 
int ServerKaryS3ORAMC::start()
{
	int ret = 1;
	int CMD;
    unsigned char buffer[sizeof(CMD)];
    zmq::context_t context(1);
    zmq::socket_t socket(context,ZMQ_REP);
    
	cout<< "[Server] Socket is OPEN on " << this->CLIENT_ADDR << endl;
    socket.bind(this->CLIENT_ADDR.c_str());

	while (true) 
	{
		cout<< "[Server] Waiting for a Command..." <<endl;
        socket.recv(buffer,sizeof(CMD));
		
        memcpy(&CMD, buffer, sizeof(CMD));
		cout<< "[Server] Command RECEIVED!" <<endl;
		
        socket.send((unsigned char*)CMD_SUCCESS,sizeof(CMD_SUCCESS));
        
        switch(CMD)
        {
			case CMD_SEND_ORAM_TREE: //inherent
				cout<<endl;
				cout << "=================================================================" << endl;
				cout<< "[Server] Receiving ORAM Data..." <<endl;
				cout << "=================================================================" << endl;
				this->recvORAMTree(socket);
				cout << "=================================================================" << endl;
				cout<< "[Server] ORAM Data RECEIVED!" <<endl;
				cout << "=================================================================" << endl;
				cout<<endl;
				break;
			case CMD_REQUEST_BLOCK: //inherent
				cout<<endl;
				cout << "=================================================================" << endl;
				cout<< "[Server] Receiving Logical Vector..." <<endl;
				cout << "=================================================================" << endl;
				this->retrieve(socket);
				cout << "=================================================================" << endl;
				cout<< "[Server] Block Share SENT" <<endl;
				cout << "=================================================================" << endl;
				cout<<endl;
				break;
          case CMD_SEND_EVICT:  //specific
				cout<<endl;
				cout << "=================================================================" << endl;
				cout<< "Receiving Eviction Matrix..." <<endl;
				cout << "=================================================================" << endl;
				this->evict(socket);
				cout << "=================================================================" << endl;
				cout<< "[Server] EVICTION and DEGREE REDUCTION DONE!" <<endl;
				cout << "=================================================================" << endl;
				cout<<endl;
				break;
			default:
				break;
		}
	}
	
	ret = 0;
    return ret;
}


/**
 * Function Name: recvORAMTree //inherent
 *
 * Description: Distributes generated and shared ORAM buckets to servers over network
 * 
 * @return 0 if successful
 */  
 
int ServerKaryS3ORAMC::recvORAMTree(zmq::socket_t& socket)
{
    int ret = 1;
    for(int i = 0 ; i < NUM_NODES;i++)
    {
        socket.recv(bucket_buffer,BUCKET_SIZE*sizeof(TYPE_DATA)*DATA_CHUNKS,0);
        string path = rootPath + to_string(serverNo) + "/" + to_string(i);
    
        FILE* file_out = NULL;
        if((file_out = fopen(path.c_str(),"wb+")) == NULL)
        {
            cout<< "	[recvORAMTree] File Cannot be Opened!!" <<endl;
            exit(0);
        }
        fwrite(bucket_buffer, 1, BUCKET_SIZE*sizeof(TYPE_DATA)*DATA_CHUNKS, file_out);
        fclose(file_out);
        socket.send((unsigned char*)CMD_SUCCESS,sizeof(CMD_SUCCESS),0);
       
    }
	 cout<< "	[recvORAMTree] ACK is SENT!" <<endl;
	
	ret = 0;
    return ret ;
}


/**
 * Function Name: retrieve
 *
 * Description: Starts retrieve operation for a block by receiving logical access vector and path ID from the client. 
 * According to path ID, server performs dot-product operation between its block shares on the path and logical access vector.
 * The result of the dot-product is send back to the client.
 * 
 * @param socket: (input) ZeroMQ socket instance for communication with the client
 * @return 0 if successful
 */  
int ServerKaryS3ORAMC::retrieve(zmq::socket_t& socket)
{
	Utils::write_list_to_file(to_string(HEIGHT) + "_" + to_string(BLOCK_SIZE) + "_server" + to_string(serverNo)+ "_" + timestamp + ".txt",logDir, server_logs, 13);
	memset(server_logs, 0, sizeof(unsigned long int)*13);
	
	int ret = 1;
	
	auto start = time_now;
	socket.recv(select_buffer_in,sizeof(TYPE_INDEX)+(H+1)*BUCKET_SIZE*sizeof(TYPE_DATA),0);
	auto end = time_now;
	cout<< "	[SendBlock] PathID and Logical Vector RECEIVED in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() << " ns" <<endl;
    server_logs[0] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	
	TYPE_INDEX pathID;
	memcpy(&pathID, select_buffer_in, sizeof(pathID));
    
    
    zz_p sharedVector[(H+1)*BUCKET_SIZE];
    memcpy(sharedVector, &select_buffer_in[sizeof(pathID)], (H+1)*BUCKET_SIZE*sizeof(TYPE_DATA));
    cout<< "	[SendBlock] PathID is " << pathID <<endl;
	
    
    S3ORAM ORAM;
	TYPE_INDEX fullPathIdx[H+1];
    ORAM.getFullPathIdx(fullPathIdx, pathID);
	
    //use thread to load data from files
    start = time_now;
    int step = ceil((double)DATA_CHUNKS/(double)numThreads);
    int endIdx;
    THREAD_LOADDATA loadData_args[numThreads];
    for(int i = 0, startIdx = 0; i < numThreads , startIdx < DATA_CHUNKS; i ++, startIdx+=step)
    {
        if(startIdx+step > DATA_CHUNKS)
            endIdx = DATA_CHUNKS;
        else
            endIdx = startIdx+step;
            
        loadData_args[i] = THREAD_LOADDATA(this->serverNo, startIdx, endIdx, this->dot_product_vector, fullPathIdx,H+1);
        pthread_create(&thread_compute[0][i], NULL, &ServerKaryS3ORAMC::thread_loadRetrievalData_func, (void*)&loadData_args[i]);
        
        /*cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        pthread_setaffinity_np(thread_compute[0][i], sizeof(cpu_set_t), &cpuset);*/
    }
    
    for(int i = 0, startIdx = 0 ; i < numThreads , startIdx < DATA_CHUNKS; i ++, startIdx+=step)
    {
        pthread_join(thread_compute[0][i],NULL);
    }
    end = time_now;
	long load_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
    cout<< "	[SendBlock] Path Nodes READ from Disk in " << load_time << " ns"<<endl;
    server_logs[1] = load_time;

    start = time_now;
    //Multithread for dot product computation
    THREAD_COMPUTATION dotProduct_args[numThreads];
    endIdx = 0;
    step = ceil((double)DATA_CHUNKS/(double)numThreads);
    for(int i = 0, startIdx = 0 ; i < numThreads , startIdx < DATA_CHUNKS; i ++, startIdx+=step)
    {
        if(startIdx+step > DATA_CHUNKS)
            endIdx = DATA_CHUNKS;
        else
            endIdx = startIdx+step;
			
        dotProduct_args[i] = THREAD_COMPUTATION( startIdx, endIdx, this->dot_product_vector, sharedVector, (H+1)*BUCKET_SIZE, sumBlock);
        pthread_create(&thread_compute[0][i], NULL, &ServerKaryS3ORAMC::thread_dotProduct_func, (void*)&dotProduct_args[i]);
		
        /*cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        pthread_setaffinity_np(thread_compute[0][i], sizeof(cpu_set_t), &cpuset);*/
    }
    
    for(int i = 0, startIdx = 0 ; i < numThreads , startIdx < DATA_CHUNKS; i ++, startIdx+=step)
    {
        pthread_join(thread_compute[0][i],NULL);
    }
    
    end = time_now;
    cout<< "	[SendBlock] Block Share CALCULATED in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
    server_logs[2] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();

    memcpy(block_buffer_out,sumBlock,sizeof(TYPE_DATA)*DATA_CHUNKS);
    
    start = time_now;
    cout<< "	[SendBlock] Sending Block Share with ID-" << sumBlock[0] <<endl;
    socket.send(block_buffer_out,sizeof(TYPE_DATA)*DATA_CHUNKS);
    end = time_now;
    cout<< "	[SendBlock] Block Share SENT in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
    server_logs[3] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
    
    ret = 0;
    return ret ;
}


/**
 * Function Name: evict
 *
 * Description: Starts eviction operation with the command of the client by receiving eviction matrix
 * and eviction path no from the client. According to eviction path no, the server performs 
 * matrix multiplication with its buckets and eviction matrix to evict blocks. After eviction operation,
 * the degree of the sharing polynomial doubles. Thus all the servers distributes their shares and perform 
 * degree reduction routine simultaneously. 
 * 
 * @param socket: (input) ZeroMQ socket instance for communication with the client
 * @return 0 if successful
 */  
int ServerKaryS3ORAMC::evict(zmq::socket_t& socket)
{
    S3ORAM ORAM;
    TYPE_INDEX n_evict;
    
    int ret;
	
	cout<< "	[evict] Receiving Evict Matrix..." <<endl;;
	auto start = time_now;
	socket.recv(evict_buffer_in, 2*(sizeof(TYPE_DATA)*DATA_CHUNKS + (H+1)*evictMatSize*sizeof(TYPE_DATA) ) + sizeof(TYPE_INDEX), 0);
	auto end = time_now;
	cout<< "	[evict] RECEIVED! in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
	server_logs[6] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	
    
	TYPE_INDEX evictPath;
    unsigned long long currBufferIdx = 0;
    
    
    for(int e = 0 ; e < 2; e++)
    {
        //holdBlock
        for(int i = 0  ; i < DATA_CHUNKS; i++)
        {
            memcpy(this->cross_product_vector[e][i],&evict_buffer_in[currBufferIdx +  i*sizeof(TYPE_DATA)],sizeof(TYPE_DATA));
        }
        for (TYPE_INDEX y = 0 ; y < H+1 ; y++)
        {
            for (TYPE_INDEX i = 0 ; i < BUCKET_SIZE+1; i++)
            {
                memcpy(this->evictMatrix[e][y][i], &evict_buffer_in[currBufferIdx + (sizeof(TYPE_DATA)*DATA_CHUNKS) + y*evictMatSize*sizeof(TYPE_DATA) + i*(BUCKET_SIZE+1)*sizeof(TYPE_DATA)], (BUCKET_SIZE+1)*sizeof(TYPE_DATA));
            }
        }
        currBufferIdx += (sizeof(TYPE_DATA)*DATA_CHUNKS + (H+1)*evictMatSize*sizeof(TYPE_DATA));
    }
    memcpy(&n_evict, &evict_buffer_in[currBufferIdx], sizeof(TYPE_INDEX));
	
        
    
    TYPE_INDEX fullEvictPathIdx[2][HEIGHT+1];
    for(int e = 0 ; e < 2 ; e++)
    {
        string evict_str = ORAM.getEvictString(n_evict);
        string strEvictPath = ORAM.getEvictString(n_evict);
        ORAM.getFullEvictPathIdx(fullEvictPathIdx[e],strEvictPath);
        n_evict = (n_evict+1) % N_leaf;
    }
    
    int es = 0, ee  = 1;
    unsigned long long buffer_length;
    
    FILE* file_out = NULL;
    string path;
    THREAD_LOADDATA loadData_args[2][numThreads];
    struct_socket recvSocket_args[NUM_SERVERS-1];
    struct_socket sendSocket_args[NUM_SERVERS-1];
    TYPE_DATA shares[NUM_SERVERS];
    TYPE_DATA sum;
            
    for(int h = 0; h < H+1 ; h++)
    {
        //since the root conflicts with two eviction paths, we process it first separately
        if(h==0)
        {
            buffer_length =  ((BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS);
        }
        else
        {
            ee = 2;
            es = 0;
            buffer_length =  2*((BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS);
        }   
start:
        cout<<endl;
		cout << "	==============================================================" << endl;
		cout<< "	[evict] Starting Eviction-" << h+1 <<endl;
        
        //== THREADS FOR LISTENING =======================================================================================
        cout<< "	[evict] Creating Threads for Receiving Ports..." << endl;
        for(TYPE_INDEX k = 0; k < NUM_SERVERS-1; k++)
        {
            //recvSocket_args[k] = struct_socket("tcp://*:" + std::to_string(SERVER_PORT+(serverNo)*(NUM_SERVERS)+this->others[k]), NULL, 0, shares_buffer_in[k], buffer_length, NULL,false);
            recvSocket_args[k] = struct_socket(k, NULL, 0, shares_buffer_in[k], buffer_length, NULL,false);
            pthread_create(&thread_recv[k], NULL, &ServerKaryS3ORAMC::thread_socket_func, (void*)&recvSocket_args[k]);
        }
        cout << "	[evict] CREATED!" << endl;
        //===============================================================================================================
        
        start = time_now;
        int step = ceil((double)DATA_CHUNKS/(double)numThreads);
        int endIdx;
        for(int e = es ; e < ee; e++)
        {
            // Multithread for loading data from disk
            for(int i = 0, startIdx = 0; i < numThreads , startIdx < DATA_CHUNKS; i ++, startIdx+=step)
            {
                if(startIdx+step > DATA_CHUNKS)
                    endIdx = DATA_CHUNKS;
                else
                    endIdx = startIdx+step;
                loadData_args[e][i] = THREAD_LOADDATA(this->serverNo, startIdx, endIdx, this->cross_product_vector[e], fullEvictPathIdx[e][h]);
                pthread_create(&thread_compute[e][i], NULL, &ServerKaryS3ORAMC::thread_readBucket_func, (void*)&loadData_args[e][i]);
                
                /*cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(i, &cpuset);
                pthread_setaffinity_np(thread_compute[e][i], sizeof(cpu_set_t), &cpuset);*/
            }
        }
        for(int e = es ; e < ee; e++)
        {
            for(int i = 0, startIdx = 0 ; i < numThreads , startIdx < DATA_CHUNKS; i ++, startIdx+=step)
            {
                pthread_join(thread_compute[e][i],NULL);
            }
        }
        end = time_now;
        long load_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        cout<< "	[evict] Evict Nodes READ from Disk in " << load_time <<endl;
        server_logs[7] += load_time;
            
        //perform matrix product
        cout<< "	[evict] Multiplying Evict Matrix..." << endl;
        start = time_now;
        this->multEvictTriplet(h,es,ee); 	// SERVER SIDE COMPUTATION
        end = time_now;
        cout<< "	[evict] Multiplied in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
        server_logs[8] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
            
        currBufferIdx = 0;
        
        cout<< "	[evict] Creating Shares for Reduction..." << endl;
        for(int e = es ; e < ee; e++)
        {
            // Distribution & Degree Reduction
            
            int m = 0;
            
            start = time_now;
            for(int u = 0 ; u <DATA_CHUNKS; u++)
            {
                for(TYPE_INDEX j = 0; j < BUCKET_SIZE+1; j++)
                {
                    ORAM.createShares(this->BUCKET_DATA[e][u][j], shares); // EACH SERVER CALCULATES AND DISTRIBUTES SHARES
                    for(TYPE_INDEX k = 0; k < NUM_SERVERS; k++)
                    {
                        if (k == this->serverNo)
                        {
                            ownShares[e][this->serverNo][u][j] = shares[k];
                        }
                        else
                        {
                            memcpy(&shares_buffer_out[m][currBufferIdx + ((u*(BUCKET_SIZE+1) +  j)*sizeof(TYPE_DATA))], &shares[k], sizeof(TYPE_DATA));
                            m++;
                        }
                    }
                    m = 0;
                }
            }
            currBufferIdx += ((BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS);
        }
        end = time_now;
        cout<< "	[evict] Shares CREATED in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count() <<endl;
        server_logs[9] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
    
		//== THREADS FOR SENDING ============================================================================================
		cout<< "	[evict] Creating Threads for Sending Shares..."<< endl;;
		for (int i = 0; i < NUM_SERVERS-1; i++)
		{
			sendSocket_args[i] = struct_socket(i,  shares_buffer_out[i], buffer_length, NULL, 0, NULL, true);
			pthread_create(&thread_send[i], NULL, &ServerKaryS3ORAMC::thread_socket_func, (void*)&sendSocket_args[i]);
		}
		cout<< "	[evict] CREATED!" <<endl;
		//=================================================================================================================
		cout<< "	[evict] Waiting for Threads..." <<endl;
		for (int i = 0; i < NUM_SERVERS-1; i++)
		{
			pthread_join(thread_send[i], NULL);
			pthread_join(thread_recv[i], NULL);
		}
		
		cout<< "	[evict] DONE!" <<endl;
		server_logs[10] += thread_max;
		thread_max = 0;
		
		cout << "	[evict] Deserializing Received Shares" << endl;
        
        currBufferIdx = 0;
		for(int e = es ; e < ee; e++)
        {
            for(int u = 0 ; u < DATA_CHUNKS; u ++)
            {
                int m = 0;
                for(TYPE_INDEX k = 0; k < NUM_SERVERS; k++)
                {
                    if (k == this->serverNo)
                    {
                        
                    }
                    else
                    {
                        memcpy(ownShares[e][k][u], &shares_buffer_in[m][currBufferIdx + u*(BUCKET_SIZE+1)*sizeof(TYPE_DATA)], (BUCKET_SIZE+1)*sizeof(TYPE_DATA));   
                        m++;
                    }
                }
            }
            currBufferIdx += (BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS;
            
            memset(bucket_buffer,0,(BUCKET_SIZE+1)*sizeof(TYPE_DATA)*DATA_CHUNKS);

            cout << "	[evict] Calculating New Shares (Degree Reduction)" << endl;
            
            start = time_now;
            for(int u = 0 ; u <DATA_CHUNKS; u++)
            {
                for(TYPE_INDEX j = 0; j < BUCKET_SIZE+1; j++)
                {
                    sum = 0;
                    for (TYPE_INDEX l = 0; l < NUM_SERVERS; l++)
                    {
                        sum = (sum + Utils::mulmod(vandermonde[l], ownShares[e][l][u][j])) % P;
                    }

                    memcpy(&bucket_buffer[(u*(BUCKET_SIZE+1) + j)*sizeof(TYPE_DATA)], &sum, sizeof(TYPE_DATA));
                }
            }
            end = time_now;
            server_logs[11] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
            
            //write to file
            start = time_now;
            
           
            path = rootPath + to_string(serverNo) + "/" + to_string(fullEvictPathIdx[e][h]); 
            
            if((file_out = fopen(path.c_str(),"wb+")) == NULL)
            {
                cout<< "	[evict] File Cannot be Opened!!" <<endl;
                exit(0);
            }
            unsigned long long iter =0;
            for(int i = 0 ; i < DATA_CHUNKS; i++)
            {
                fwrite(&bucket_buffer[iter], 1, BUCKET_SIZE*sizeof(TYPE_DATA), file_out);
                iter += (BUCKET_SIZE*sizeof(TYPE_DATA));
                memcpy(this->cross_product_vector[e][i],&bucket_buffer[iter],sizeof(TYPE_DATA));
                iter += sizeof(TYPE_DATA);
            }
            
            fclose(file_out);
            end = time_now;
            server_logs[12] += std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
            if (h==0 && es == 0)
            {
                es =1;
                ee =2;
                goto start;
            }
        }
        cout<< "	[evict] Reduction DONE in " << server_logs[11] <<endl;
        cout<< "	[evict] Written to Disk in " << server_logs[12] <<endl;
		cout<< "	[evict] TripletEviction-" << h+1 << " COMPLETED!"<<endl;
    }
    
    socket.send((unsigned char*)CMD_SUCCESS,sizeof(CMD_SUCCESS));
	cout<< "	[evict] ACK is SENT!" <<endl;

    return 0;
}


/**
 * Function Name: multEvictTriplet //specific but can make it inherent by putting SIZE n as param
 *
 * Description: Performs matrix multiplication between received eviction matrix and affected buckets
 * for eviction operation 
 * 
 * @param evictMatrix: (input) Received eviction matrix from the clietn
 * @return 0 if successful
 */  
int ServerKaryS3ORAMC::multEvictTriplet(int level, int es, int ee)
{
	//thread implementation
    THREAD_COMPUTATION crossProduct_args[2][numThreads];
    int endIdx;
    int step = ceil((double)DATA_CHUNKS/(double)numThreads);
    int nConcurrBucket;
    
    for(int e = es ; e < ee ; e++)
    {
        for(int i = 0, startIdx = 0 ; i < numThreads; i ++, startIdx+=step)
        {
            if(startIdx+step > DATA_CHUNKS)
                endIdx = DATA_CHUNKS;
            else
                endIdx = startIdx+step;
        
            crossProduct_args[e][i] = THREAD_COMPUTATION(startIdx, endIdx, this->cross_product_vector[e], BUCKET_SIZE+1, evictMatrix[e][level], this->BUCKET_DATA[e] );
            pthread_create(&thread_compute[e][i], NULL, &ServerKaryS3ORAMC::thread_crossProduct_func, (void*)&crossProduct_args[e][i]);
            
            /*cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(i, &cpuset);
            pthread_setaffinity_np(thread_compute[e][i], sizeof(cpu_set_t), &cpuset);*/
        }
    }
    for(int e = es ; e < ee; e ++)
    {
        for(int i  = 0 ; i <numThreads ; i++)
        {
            pthread_join(thread_compute[e][i],NULL);
        }
	}
	return 0;
}


/**
 * Function Name: thread_socket_func & send & recv
 *
 * Description: Generic threaded socket functions for send and receive operations
 * 
 * @return 0 if successful
 */  
void *ServerKaryS3ORAMC::thread_socket_func(void* args)
{
    struct_socket* opt = (struct_socket*) args;
	
	if(opt->isSend)
	{
		auto start = time_now;
		send(opt->peer_idx, opt->data_out, opt->data_out_size);
		auto end = time_now;
		if(thread_max < std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count())
			thread_max = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	}
	else
	{
		recv(opt->peer_idx, opt->data_in, opt->data_in_size);
	}
    pthread_exit((void*)opt);
}
int ServerKaryS3ORAMC::send(int peer_idx, unsigned char* input, size_t inputSize)
{
	unsigned char buffer_in[sizeof(CMD_SUCCESS)];
	
    try
    {
		//cout<< "	[ThreadedSocket] Sending to Server" <<  << endl;
		socket_send[peer_idx]->send (input, inputSize);
		cout<< "	[ThreadedSocket] Data SENT!" << peer_idx << endl;
        
        socket_send[peer_idx]->recv(buffer_in, sizeof(CMD_SUCCESS));
        cout<< "	[ThreadedSocket] ACK RECEIVED!" << peer_idx << endl;
    }
    catch (exception &ex)
    {
        goto exit;
    }

exit:
	//socket.disconnect(ADDR.c_str());
	//socket.close();
	return 0;
}
int ServerKaryS3ORAMC::recv(int peer_idx, unsigned char* output, size_t outputSize)
{
	try
    {
		//cout<< "	[ThreadedSocket] Waiting Client on " << ADDR << endl;
		socket_recv[peer_idx]->recv (output, outputSize);
		cout<< "	[ThreadedSocket] Data RECEIVED! " << peer_idx <<endl;
        
        socket_recv[peer_idx]->send((unsigned char*)CMD_SUCCESS,sizeof(CMD_SUCCESS));
        cout<< "	[ThreadedSocket] ACK SENT! "  << peer_idx<<endl;
    }
    catch (exception &ex)
    {
        cout<<"Socket error!";
        goto exit;
    }
    
exit:
	//socket.close();
	return 0;
}


/**
 * Function Name: thread_dotProduct_func
 *
 * Description: Threaded dot-product operation 
 * 
 */  
void *ServerKaryS3ORAMC::thread_dotProduct_func(void* args)
{
    THREAD_COMPUTATION* opt = (THREAD_COMPUTATION*) args;
  
    //std::cout << " CPU # " << sched_getcpu() << "\n";
    for(int k = opt->startIdx; k < opt->endIdx; k++)
    {
        opt->dot_product_output[k] = InnerProd_LL(opt->data_vector[k],opt->select_vector,opt->vector_length,P,zz_p::ll_red_struct());
    }
}


/**
 * Function Name: thread_crossProduct_func //specific but can make to be inherent by putting size into arg param
 *
 * Description: Threaded cross-product operation 
 * 
 */  
void *ServerKaryS3ORAMC::thread_crossProduct_func(void* args)
{
    THREAD_COMPUTATION* opt = (THREAD_COMPUTATION*) args;
    
    for(int l = opt->startIdx ; l < opt->endIdx; l++) //fix this later
    {
        for(int k = 0 ; k < BUCKET_SIZE+1; k++)
        {
            opt->cross_product_output[l][k] = InnerProd_LL(opt->data_vector_triplet[l],opt->evict_matrix[k],opt->vector_length,P,zz_p::ll_red_struct());
        }
    }
    
    pthread_exit((void*)opt);
}


/**
 * Function Name: thread_loadRetrievalData_func
 *
 * Description: Threaded load function to read buckets in a path from disk storage
 * 
 */  
void* ServerKaryS3ORAMC::thread_loadRetrievalData_func(void* args)
{
    THREAD_LOADDATA* opt = (THREAD_LOADDATA*) args;
    
    unsigned long int load_time = 0;
    FILE* file_in = NULL;
    string path;
    
    for(int i = 0; i < opt->fullPathIdx_length; i++)
    {
        file_in = NULL;
        path = rootPath + to_string(opt->serverNo) + "/" + to_string(opt->fullPathIdx[i]);
        if((file_in = fopen(path.c_str(),"rb")) == NULL){
            cout<< "	[SendBlock] File cannot be opened!!" <<endl;
            exit;
        }
        fseek(file_in,BUCKET_SIZE*(opt->startIdx)*sizeof(TYPE_DATA),SEEK_SET);
        for (int k = opt->startIdx ; k < opt->endIdx; k++)
        {
            for(int j = 0 ; j < BUCKET_SIZE; j ++)
            {
                fread(&opt->data_vector[k][i*BUCKET_SIZE+j],1,sizeof(TYPE_DATA),file_in);
            }
        }
        fclose(file_in);
    }
}


/**
 * Function Name: thread_readBucket_func
 *
 * Description: Threaded load function to read triplet buckets from disk storage
 * 
 */  
void* ServerKaryS3ORAMC::thread_readBucket_func(void* args)
{
    THREAD_LOADDATA* opt = (THREAD_LOADDATA*) args;
    
    unsigned long int load_time = 0;
    FILE* file_in = NULL;
    string path;
    
    file_in = NULL;
    path = rootPath + to_string(opt->serverNo) + "/" + to_string(opt->idx);
    if((file_in = fopen(path.c_str(),"rb")) == NULL)
    {
        cout<< "	[SendBlock] File Cannot be Opened!!" <<endl;
        exit(0);
    }
    fseek(file_in,BUCKET_SIZE*(opt->startIdx)*sizeof(TYPE_DATA),SEEK_SET);
		
    for (int k = opt->startIdx ; k < opt->endIdx; k++)
    {
        fread(&opt->data_vector[k][1],1,sizeof(TYPE_DATA)*BUCKET_SIZE,file_in);
    }
    fclose(file_in);

}




