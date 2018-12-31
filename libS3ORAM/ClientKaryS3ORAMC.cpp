#include "ClientKaryS3ORAMC.hpp"

/*
 * ClientKaryS3ORAMC.cpp
 *
 *  Created on: Mar 15, 2017
 *      Author: ceyhunozkaptan, thanghoang
 */

#include "ClientKaryS3ORAMC.hpp"
#include "Utils.hpp"
#include "S3ORAM.hpp"


zmq::context_t** ClientKaryS3ORAMC::context = new zmq::context_t*[NUM_SERVERS];
zmq::socket_t**  ClientKaryS3ORAMC::socket = new zmq::socket_t*[NUM_SERVERS];


//inherent
unsigned long int ClientKaryS3ORAMC::exp_logs[9];
unsigned long int ClientKaryS3ORAMC::thread_max = 0;
char ClientKaryS3ORAMC::timestamp[16];


//inherent + specific
ClientKaryS3ORAMC::ClientKaryS3ORAMC()
{
    //specific
    this->STASH = new TYPE_DATA*[STASH_SIZE];
    this->metaStash = new TYPE_INDEX[STASH_SIZE];
    for(int i = 0 ; i < STASH_SIZE; i++)
    {
        this->STASH[i] = new TYPE_DATA[DATA_CHUNKS];
        memset(this->STASH[i],0,sizeof(TYPE_DATA)*DATA_CHUNKS);
        this->metaStash[i] = -1;
    }
    
	this->sharedMatrix = new TYPE_DATA**[NUM_SERVERS];
	for (TYPE_INDEX i = 0 ; i < NUM_SERVERS; i++)
	{
		this->sharedMatrix[i] = new TYPE_DATA*[H+1];
		for(TYPE_INDEX j = 0 ; j < H+1; j++)
		{
			this->sharedMatrix[i][j] = new TYPE_DATA[evictMatSize];
		}
	}
    
	this->evictMatrix = new TYPE_DATA*[H+1];
	for(TYPE_INDEX i = 0 ; i < H+1; i++)
	{
		this->evictMatrix[i] = new TYPE_DATA[evictMatSize];
	}
    this->evict_buffer_out = new unsigned char*[NUM_SERVERS];
    for (TYPE_INDEX i = 0 ; i < NUM_SERVERS ; i++)
    {
        this->evict_buffer_out[i] = new unsigned char[ 2* ( (sizeof(TYPE_DATA)*DATA_CHUNKS) + 
                                                        (H+1)*evictMatSize*sizeof(TYPE_DATA) ) + 
                                                        sizeof(TYPE_INDEX)];
    }
    
    //inherent
	this->pos_map = new TYPE_POS_MAP[NUM_BLOCK+1];
    
    this->metaData = new TYPE_ID*[NUM_NODES];
	for (int i = 0 ; i < NUM_NODES; i++)
    {
        this->metaData[i] = new TYPE_ID[BUCKET_SIZE];
        memset(this->metaData[i],0,sizeof(TYPE_ID)*BUCKET_SIZE);
    }
    
    retrievedShare = new TYPE_DATA*[NUM_SERVERS];
	for(int k = 0 ; k < NUM_SERVERS; k++)
    {
        retrievedShare[k] = new TYPE_DATA[DATA_CHUNKS];
    }
    recoveredBlock = new TYPE_DATA[DATA_CHUNKS];
    
    blocks_buffer_in = new unsigned char*[NUM_SERVERS];
    
    for(int i = 0; i < NUM_SERVERS ; i++)
    {
        blocks_buffer_in[i] = new unsigned char[sizeof(TYPE_DATA)*DATA_CHUNKS];
    }
    
	
	this->sharedVector = new TYPE_DATA*[NUM_SERVERS];
	for(int i = 0; i < NUM_SERVERS; i++)
	{
		this->sharedVector[i] = new TYPE_DATA[(H+1)*BUCKET_SIZE];
	}
	
    this->vector_buffer_out = new unsigned char*[NUM_SERVERS];
    for (TYPE_INDEX i = 0 ; i < NUM_SERVERS ; i++)
    {
        this->vector_buffer_out[i] = new unsigned char[sizeof(TYPE_INDEX)+(H+1)*BUCKET_SIZE*sizeof(TYPE_DATA)]; 
    }
    
        
    
	#if defined(PRECOMP_MODE) // ================================================================================================
		this->precompOnes = new TYPE_DATA*[NUM_SERVERS];
		for (TYPE_INDEX i = 0 ; i < NUM_SERVERS ; i++){
			this->precompOnes[i] = new TYPE_DATA[PRECOMP_SIZE];
		}
		
		this->precompZeros = new TYPE_DATA*[NUM_SERVERS];
		for (TYPE_INDEX i = 0 ; i < NUM_SERVERS ; i++){
			this->precompZeros[i] = new TYPE_DATA[PRECOMP_SIZE];
		}
		
		S3ORAM ORAM;
		
		auto start = time_now;
		ORAM.precomputeShares(0, precompZeros, PRECOMP_SIZE);
		ORAM.precomputeShares(1, precompOnes, PRECOMP_SIZE);
		auto end = time_now;
		cout<< "	[ClientKaryS3ORAMC] " << 2*PRECOMP_SIZE << " Logical Values Precomputed in" << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
	#endif //defined(PRECOMP_MODE) ================================================================================================
	
    


	time_t now = time(0);
	char* dt = ctime(&now);
	FILE* file_out = NULL;
	string path = clientLocalDir + "lastest_config";
	string info = "Height of Tree: " + to_string(HEIGHT) + "\n";
	info += "Number of Blocks: " + to_string(NUM_BLOCK) + "\n";
	info += "Bucket Size: " + to_string(BUCKET_SIZE) + "\n";
	info += "Eviction Rate: " + to_string(EVICT_RATE) + "\n";
	info += "Block Size (B): " + to_string(BLOCK_SIZE) + "\n";
	info += "ID Size (B): " + to_string(sizeof(TYPE_ID)) + "\n";
	info += "Number of Chunks: " + to_string(DATA_CHUNKS) + "\n";
	info += "Total Size of Data (MB): " + to_string((NUM_BLOCK*(BLOCK_SIZE+sizeof(TYPE_ID)))/1048576.0) + "\n";
	info += "Total Size of ORAM (MB): " + to_string(BUCKET_SIZE*NUM_NODES*(BLOCK_SIZE+sizeof(TYPE_ID))/1048576.0) + "\n";
	
	#if defined(PRECOMP_MODE)
		info += "PRECOMPUTATION MODE: Active\n";
	#else
		info += "PRECOMPUTATION MODE: Inactive\n";
	#endif 
	
	if((file_out = fopen(path.c_str(),"w+")) == NULL){
		cout<< "	File Cannot be Opened!!" <<endl;
		exit;
	}
	fputs(dt, file_out);
	fputs(info.c_str(), file_out);
	fclose(file_out);
	
    
    
    //socket
    for(int i = 0 ; i < NUM_SERVERS;i ++)
    {
        context[i] = new zmq::context_t(1);
        socket[i] = new zmq::socket_t(*context[i],ZMQ_REQ);
        string send_address = SERVER_ADDR[i]+ ":" + std::to_string(SERVER_PORT+i*NUM_SERVERS+i);
        cout<<"Connecting to "<<send_address<<" for communication with Server " << i << " ...";
        socket[i]->connect(send_address);
        cout<<"OK!"<<endl;
    }
    
    
    
	tm *now_time = localtime(&now);
	if(now != -1)
		strftime(timestamp,16,"%d%m_%H%M",now_time);
		
}

ClientKaryS3ORAMC::~ClientKaryS3ORAMC()
{
}


/**
 * Function Name: init (inherent)
 *
 * Description: Initialize shared ORAM data on disk storage of the client
 * and creates logging and configuration files
 * 
 * @return 0 if successful
 */ 
int ClientKaryS3ORAMC::init()
{
    this->numRead = 0;
    this->numEvict = 0;

    auto start = time_now;
    auto end = time_now;

    for ( TYPE_INDEX i = 0 ; i <= NUM_BLOCK; i ++ )
    {
        this->pos_map[i].pathID = -1;
        this->pos_map[i].pathIdx = -1;
    }
    
    start = time_now;
    S3ORAM ORAM;
    ORAM.build(this->pos_map,this->metaData);
    
    end = time_now;
	cout<<endl;
    cout<< "Elapsed Time for Setup on Disk: "<<std::chrono::duration_cast<std::chrono::seconds>(end-start).count()<<" s"<<endl;
    cout<<endl;
    std::ofstream output;
    string path2 = clientLocalDir + "lastest_config";
    output.open(path2, std::ios_base::app);
    output<< "INITIALIZATION ON CLIENT: Performed\n";
    output.close();

    //save the state to file
    saveState();
    
    //transfer to servers all ORAM structure (send files) //IMPLEMENT LATER
	
    return 0;
}



/**
 * Function Name: load (inherent+specific)
 *
 * Description: Loads client storage data from disk for previously generated ORAM structure 
 * in order to continue ORAM operations. Loaded data includes postion map, current number of evictions,
 * current number of reads/writes.
 * 
 * @return 0 if successful
 */ 
int ClientKaryS3ORAMC::loadState()
{
	FILE* local_data = NULL;
	if((local_data = fopen(clientTempPath.c_str(),"rb")) == NULL){
		cout<< "	[load] File Cannot be Opened!!" <<endl;
		exit(0);
	}
	
    fread(this->pos_map, 1, (NUM_BLOCK+1)*sizeof(TYPE_POS_MAP), local_data);
	fread(&this->numEvict, sizeof(this->numEvict), 1, local_data);
	fread(&this->numRead, sizeof(this->numRead), 1, local_data);
    for(int i = 0 ; i < STASH_SIZE ; i ++)
    {
        fread(&this->metaStash[i], sizeof(TYPE_INDEX), 1, local_data);
    }
    for(int i = 0 ; i < STASH_SIZE ; i ++)
    {
        fread(this->STASH[i], sizeof(TYPE_DATA) * DATA_CHUNKS, 1, local_data);
    }
    
    //scan position map to load bucket metaData (for speed optimization)
    TYPE_INDEX fullPathIdx[H+1];
    S3ORAM ORAM;
    for(TYPE_INDEX i = 1 ; i < NUM_BLOCK+1; i++)
    {
        ORAM.getFullPathIdx(fullPathIdx,pos_map[i].pathID);
        this->metaData[fullPathIdx[pos_map[i].pathIdx/ BUCKET_SIZE]][pos_map[i].pathIdx%BUCKET_SIZE] = i;
    }
    
	std::ofstream output;
	string path = clientLocalDir + "lastest_config";
	output.open(path, std::ios_base::app);
	output<< "SETUP FROM LOCAL DATA\n";
	output.close();
	
    return 0;
}



int ClientKaryS3ORAMC::saveState()
{
    // 11. store local info to disk
	FILE* local_data = NULL;
	if((local_data = fopen(clientTempPath.c_str(),"wb+")) == NULL){
		cout<< "	[ClientKaryS3ORAMC] File Cannot be Opened!!" <<endl;
		exit(0);
	}
	fwrite(this->pos_map, 1, (NUM_BLOCK+1)*sizeof(TYPE_POS_MAP), local_data);
	fwrite(&this->numEvict, sizeof(this->numEvict), 1, local_data);
	fwrite(&this->numRead, sizeof(this->numRead), 1, local_data);
    for(int i = 0 ; i < STASH_SIZE ; i ++)
    {
        fwrite(&this->metaStash[i], sizeof(TYPE_INDEX), 1, local_data);
    }
    for(int i = 0 ; i < STASH_SIZE ; i ++)
    {
        fwrite(this->STASH[i], sizeof(TYPE_DATA) * DATA_CHUNKS, 1, local_data);
    }
    
	fclose(local_data);
    
    return 0;
}



/**
 * Function Name: sendORAMTree (inherent)
 *
 * Description: Distributes generated and shared ORAM buckets to servers over network
 * 
 * @return 0 if successful
 */  
int ClientKaryS3ORAMC::sendORAMTree()
{
    unsigned char*  oram_buffer_out = new unsigned char [BUCKET_SIZE*sizeof(TYPE_DATA)*DATA_CHUNKS]; 
    memset(oram_buffer_out,0, BUCKET_SIZE*sizeof(TYPE_DATA)*DATA_CHUNKS);
    int CMD = CMD_SEND_ORAM_TREE;       
    unsigned char buffer_in[sizeof(CMD_SUCCESS)];
	unsigned char buffer_out[sizeof(CMD)];

    memcpy(buffer_out, &CMD,sizeof(CMD));
    
    zmq::context_t context(1);
    zmq::socket_t socket(context,ZMQ_REQ);

    struct_socket thread_args[NUM_SERVERS];
    for (int i = 0; i < NUM_SERVERS; i++)
    {
		string ADDR = SERVER_ADDR[i]+ ":" + std::to_string(SERVER_PORT+i*NUM_SERVERS+i); 
		cout<< "	[sendORAMTree] Connecting to " << ADDR <<endl;
        socket.connect( ADDR.c_str());
            
        socket.send(buffer_out, sizeof(CMD));
		cout<< "	[sendORAMTree] Command SENT! " << CMD <<endl;
        socket.recv(buffer_in, sizeof(CMD_SUCCESS));
		
        for(TYPE_INDEX j = 0 ; j < NUM_NODES; j++)
        {
            //load data to buffer
            FILE* fdata = NULL;
            string path = rootPath + to_string(i) + "/" + to_string(j);
            if((fdata = fopen(path.c_str(),"rb")) == NULL)
            {
                cout<< "	[sendORAMTree] File Cannot be Opened!!" <<endl;
                exit(0);
            }
            long lSize;
            fseek (fdata , 0 , SEEK_END);
            lSize = ftell (fdata);
            rewind (fdata);
            if(fread(oram_buffer_out ,1 , BUCKET_SIZE*sizeof(TYPE_DATA)*DATA_CHUNKS, fdata) != sizeof(char)*lSize){
                cout<< "	[sendORAMTree] File loading error be Read!!" <<endl;
                exit(0);
            }
            fclose(fdata);
            //send to server i
            socket.send(oram_buffer_out,BUCKET_SIZE*sizeof(TYPE_DATA)*DATA_CHUNKS,0);
            socket.recv(buffer_in,sizeof(CMD_SUCCESS));
        }
        socket.disconnect(ADDR.c_str());
    }
    socket.close();	
    return 0;
}

int ClientKaryS3ORAMC::countNumBlockInStash()
{
    int count = 0;
    for(int i = 0 ; i < STASH_SIZE ; i++)
    {
        if(metaStash[i]!=-1)
        {
            count++;
        }
    }
    return count;
}


/**
 * Function Name: access (specific)
 *
 * Description: Starts access operation for a block with its ID to be retrived from distributed servers. 
 * This operations consists of several subroutines: generating shares for logical access vector, 
 * retrieving shares from servers, recovering secret block from shares, assigning new path for the block,
 * re-share/upload the block back to servers, run eviction subroutine acc. to EVICT_RATE
 * 
 * @param blockID: (input) ID of the block to be retrieved
 * @return 0 if successful
 */  
int ClientKaryS3ORAMC::access(TYPE_ID blockID) 
{
    
	auto start = time_now;
    auto end = time_now;
	struct_socket thread_args[NUM_SERVERS];
    
    S3ORAM ORAM;
	cout << "================================================================" << endl;
	cout << "STARTING ACCESS OPERATION FOR BLOCK-" << blockID <<endl; 
	cout << "================================================================" << endl;
	
    // 1. get the path & index of the block of interest
    TYPE_INDEX pathID = pos_map[blockID].pathID;
	cout << "	[ClientKaryS3ORAMC] PathID = " << pathID <<endl;
    cout << "	[ClientKaryS3ORAMC] Location = " << 	pos_map[blockID].pathIdx <<endl;
    
    // 2. create select query
	TYPE_DATA logicVector[(H+1)*BUCKET_SIZE];
	
    
    
	start = time_now;
	int BlockIdxInStash = getLogicalVector(logicVector, blockID);
	
    end = time_now;
	cout<< "	[ClientKaryS3ORAMC] Logical Vector Created in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
    exp_logs[0] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	
	// 3. create query shares
	#if defined (PRECOMP_MODE) 
		start = time_now;
		for (TYPE_INDEX i = 0; i < (H+1)*BUCKET_SIZE; i++)
		{
			if (logicVector[i] == 0){
				for (int j = 0; j < NUM_SERVERS; j++){
					this->sharedVector[j][i] = this->precompZeros[j][0];
				}
			}
			else{
				for (int j = 0; j < NUM_SERVERS; j++){
					this->sharedVector[j][i] = this->precompOnes[j][0];
				}
			}
		}
		end = time_now;
		cout<< "	[ClientKaryS3ORAMC] Shared Vector Created in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
		exp_logs[1] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	#else //defined (PRECOMP_MODE) ================================================================================================
		start = time_now;
		ORAM.getSharedVector(logicVector, this->sharedVector,(H+1)*BUCKET_SIZE);
		end = time_now;
		cout<< "	[ClientKaryS3ORAMC] Shared Vector Created in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
		exp_logs[1] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	#endif //defined (PRECOMP_MODE) ================================================================================================
	
	// 4. send to server & receive the answer
    
    start = time_now;
    for (int i = 0; i < NUM_SERVERS; i++)
    {
        memcpy(&vector_buffer_out[i][0], &pathID, sizeof(pathID));
        memcpy(&vector_buffer_out[i][sizeof(pathID)], &this->sharedVector[i][0], (H+1)*BUCKET_SIZE*sizeof(TYPE_DATA));
    
        thread_args[i] = struct_socket(i, vector_buffer_out[i], sizeof(pathID)+(H+1)*BUCKET_SIZE*sizeof(TYPE_DATA), blocks_buffer_in[i], sizeof(TYPE_DATA)*DATA_CHUNKS,CMD_REQUEST_BLOCK,NULL);

		pthread_create(&thread_sockets[i], NULL, &ClientKaryS3ORAMC::thread_socket_func, (void*)&thread_args[i]);
    }
    
    
    memset(recoveredBlock,0,sizeof(TYPE_DATA)*DATA_CHUNKS);
    for(int i = 0 ; i < NUM_SERVERS; i++)
    {
        memset(retrievedShare[i],0,sizeof(TYPE_DATA)*DATA_CHUNKS);
    }
    
    for (int i = 0; i < NUM_SERVERS; i++)
    {
        pthread_join(thread_sockets[i], NULL);
        memcpy(retrievedShare[i],blocks_buffer_in[i],sizeof(TYPE_DATA)*DATA_CHUNKS);
        cout << "	[ClientKaryS3ORAMC] From Server-" << i+1 << " => BlockID = " << retrievedShare[i][0]<< endl;
    }
    end = time_now;
    cout<< "	[ClientKaryS3ORAMC] All Shares Retrieved in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
    exp_logs[2] = thread_max;
    thread_max = 0;
	
    // 5. recover the block
	start = time_now;
	ORAM.simpleRecover(retrievedShare, recoveredBlock);
	end = time_now;
	cout<< "	[ClientKaryS3ORAMC] Recovery Done in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
	exp_logs[3] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	
    cout << "	[ClientKaryS3ORAMC] Block-" << recoveredBlock[0] <<" is Retrieved" <<endl;
   
    if (recoveredBlock[0] == blockID || (recoveredBlock[0]==0 && BlockIdxInStash!=-1))
        cout << "	[ClientKaryS3ORAMC] SUCCESS!!!!!!" << endl;
    else if(recoveredBlock[0]==0)
    {
        if(BlockIdxInStash!=-1)
        {
            cout << "	[ClientKaryS3ORAMC] SUCCESS!!!!!!" << endl;
            cout << "	[ClientKaryS3ORAMC] BLOCK-"<<STASH[BlockIdxInStash]<<" RETRIEVED FROM STASH[ " <<BlockIdxInStash<<"]!!!!!!" << endl;
        }
        else
        {
            cout << "	[ClientKaryS3ORAMC] ERROR!!!!!!!!" << endl;
            exit(0);
        }
    } 
    else
    {
        cout << "	[ClientKaryS3ORAMC] ERROR!!!!!!!!" << endl;
        exit(0);
    }
        
    //assert((recoveredBlock[0] == blockID || (recoveredBlock[0]==0 && BlockIdxInStash!=-1))  && "ERROR: RECEIEVED BLOCK IS NOT CORRECT!!!!!!");
	
    // 6. update position map
    
    // 6.1. assign to random path
    pos_map[blockID].pathID = Utils::RandBound(N_leaf)+(NUM_NODES-N_leaf);
    
    
    TYPE_INDEX fullPathIdx[H+1];
    ORAM.getFullPathIdx(fullPathIdx,pathID);
    
    if(BlockIdxInStash==-1)
    {
        this->metaData[fullPathIdx[pos_map[blockID].pathIdx / BUCKET_SIZE ]][pos_map[blockID].pathIdx % BUCKET_SIZE] = 0;
        //put the block into Stash
        for(int i = 0 ; i < STASH_SIZE ; i++)
        {
            if(metaStash[i]==-1)
            {
                metaStash[i] = pos_map[blockID].pathID;
                memcpy(STASH[i],recoveredBlock,BLOCK_SIZE);
                pos_map[blockID].pathIdx = -1; //-1 means it is residing in Stash
                break;
            }
        }
        
    }
    else
    {
        metaStash[BlockIdxInStash] = pos_map[blockID].pathID;
    }
    
    for(int i = 0 ; i < NUM_SERVERS; i++)
    {
        memcpy(&evict_buffer_out[i][2*(sizeof(TYPE_DATA)*DATA_CHUNKS + (H+1)*evictMatSize*sizeof(TYPE_DATA))], &numEvict, sizeof(TYPE_INDEX));
    }
    unsigned long long currBufferIdx = 0;
    for(int e = 0 ; e < 2; e++)
    {
        cout << "================================================================" << endl;
        cout << "STARTING EVICTION-" << this->numEvict+1 <<endl;
        cout << "================================================================" << endl;
        
        // 9.1. create permutation matrices
        for(TYPE_INDEX i = 0 ; i < H+1; i++)
        {
            memset(this->evictMatrix[i], 0, evictMatSize*sizeof(TYPE_DATA));
        }
        start = time_now;
        this->getEvictMatrix(numEvict);
        end = time_now;
        cout<< "	[ClientKaryS3ORAMC] Evict Matrix Created in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
        exp_logs[5] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
            
            
        // 9.2. create shares of  permutation matrices 
		cout<< "	[ClientKaryS3ORAMC] Sharing Evict Matrix..." << endl;
		
		boost::progress_display show_progress((H+1)*evictMatSize);
        TYPE_DATA matrixShares[NUM_SERVERS];
        start = time_now;
        for (TYPE_INDEX i = 0; i < H+1; ++i) 
        {
            for (TYPE_INDEX j = 0; j < evictMatSize; ++j)
            {
                ORAM.createShares(this->evictMatrix[i][j], matrixShares);
                for (int k = 0; k < NUM_SERVERS; k++) 
                {   
                    this->sharedMatrix[k][i][j] = matrixShares[k];
                }
				++show_progress;
            }
        }
        end = time_now;
        cout<< "	[ClientKaryS3ORAMC] Shared Matrix Created in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
        exp_logs[6] = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
        
        // 9.3. create shares of a selected block in stash
        TYPE_DATA chunkShares[NUM_SERVERS];
        if(target[0]>=0)
        {
            for(int u = 0 ; u < DATA_CHUNKS; u++ )
            {
                ORAM.createShares(this->STASH[deepestIdx[0]][u], chunkShares);
                for(int k = 0; k < NUM_SERVERS; k++) 
                {
                    memcpy(&evict_buffer_out[k][currBufferIdx + u*sizeof(TYPE_DATA)], &chunkShares[k], sizeof(TYPE_DATA));
                }
            }
            memset(STASH[deepestIdx[0]],0,sizeof(TYPE_DATA)*DATA_CHUNKS);
            metaStash[deepestIdx[0]] = -1;
        }
        else
        {
            for(int u = 0 ; u < DATA_CHUNKS; u++ )
            {
                ORAM.createShares(this->STASH[0][u], chunkShares);
                for(int k = 0; k < NUM_SERVERS; k++) 
                {
                    memcpy(&evict_buffer_out[k][currBufferIdx + u*sizeof(TYPE_DATA)], &chunkShares[k], sizeof(TYPE_DATA));
                }
            }
        }
        
        for (int i = 0; i < NUM_SERVERS; i++)
        {
            for (TYPE_INDEX y = 0 ; y < H+1; y++)
            {
                memcpy(&evict_buffer_out[i][currBufferIdx + sizeof(TYPE_DATA)*DATA_CHUNKS + y*evictMatSize*sizeof(TYPE_DATA)], this->sharedMatrix[i][y], evictMatSize*sizeof(TYPE_DATA));
            }
        }
        
        currBufferIdx += sizeof(TYPE_DATA)*DATA_CHUNKS + (H+1)*evictMatSize*sizeof(TYPE_DATA);
        
        this->numEvict = (numEvict+1) % N_leaf;
    }
        
    // 9.3. send shares of permutation matrices & stash block to servers
    start = time_now;
            
    for (int i = 0; i < NUM_SERVERS; i++)
    {
        thread_args[i] = struct_socket(i, evict_buffer_out[i], 2*( (sizeof(TYPE_DATA)*DATA_CHUNKS) +  (H+1)*evictMatSize*sizeof(TYPE_DATA) ) + sizeof(TYPE_INDEX), NULL,0, CMD_SEND_EVICT,  NULL);
        pthread_create(&thread_sockets[i], NULL, &ClientKaryS3ORAMC::thread_socket_func, (void*)&thread_args[i]);
    }
			
    for (int i = 0; i < NUM_SERVERS; i++)
    {
        pthread_join(thread_sockets[i], NULL);
    }
    end = time_now;
    cout<< "	[ClientKaryS3ORAMC] Eviction DONE in " << std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count()<< " ns"<<endl;
    
    exp_logs[8] = thread_max;
    thread_max = 0;
    
    cout << "================================================================" << endl;
    cout << "EVICTION-" << this->numEvict+ 1 << " & " << this->numEvict+2 <<" COMPLETED" << endl;
    cout << "================================================================" << endl;

    saveState();
	
    // 12. write log
	Utils::write_list_to_file(to_string(HEIGHT)+"_" + to_string(BLOCK_SIZE)+"_client_" + timestamp + ".txt",logDir, exp_logs, 9);
	memset(exp_logs, 0, sizeof(unsigned long int)*9);
	
    
    cout << "================================================================" << endl;
	cout << "ACCESS OPERATION FOR BLOCK-" << blockID << " COMPLETED." << endl; 
	cout << "================================================================" << endl;
	
    cout << "NUMBER OF BLOCKS IN TASH: " << countNumBlockInStash() <<endl;
    if(countNumBlockInStash() > STASH_SIZE/2)
    {
        cout <<"HALF OF STASH FULL!"<<endl;
        cin.get();
    }
    else if(countNumBlockInStash() == STASH_SIZE)
    {
        cout<<"Stash FULL!!!, OVERFLOW!!!"<<endl;
        cin.get();
    }
    return 0;
}

/**
 * Function Name: getLogicalVector (specific)
 *
 * Description: Generates logical retrieve vector by putting '1' for the exact index of 
 * accessed block and '0' for the rest on its assigned path
 * 
 * @param logicalVector: (output) Logical retrieve vector to retrive the block.
 * @param blockID: (input) ID of the block to be retrieved.
 * @return 0 if successful
 */  
int ClientKaryS3ORAMC::getLogicalVector(TYPE_DATA* logicalVector, TYPE_ID blockID)
{
    memset (logicalVector,0,sizeof(TYPE_DATA)*(H+1)*BUCKET_SIZE);
    
    for(int i = 0 ; i < STASH_SIZE; i++)
    {
        if(this->STASH[i][0]==blockID)
        {
            return i; //return its position in stash
        }
    }
    TYPE_INDEX loc = pos_map[blockID].pathIdx;
	logicalVector[loc] = 1;
    
	return -1;
}


/**
 * Function Name: getEvictMatrix (specific)
 *
 * Description: Generates logical eviction matrix to evict blocks from root to leaves according to 
 * eviction number and source, destination and sibling buckets by scanning position map.
 * 
 * @param evictMatrix: (output) Logical eviction matrix for eviction routine
 * @param n_evict: (input) Eviction number
 * @return 0 if successful
 */  
int ClientKaryS3ORAMC::getEvictMatrix(TYPE_INDEX n_evict)
{
	S3ORAM ORAM;
    
    TYPE_INDEX fullEvictPathIdx[HEIGHT+1];
    string strEvictPath = ORAM.getEvictString(n_evict);
    ORAM.getFullEvictPathIdx(fullEvictPathIdx,strEvictPath);
    
    TYPE_INDEX evictPathID = fullEvictPathIdx[HEIGHT];
    
    TYPE_INDEX metaPath[BUCKET_SIZE*(HEIGHT+1)];
   
    for(int h = 0 ; h < HEIGHT+1; h++)
    {
        for(int z = 0 ; z < BUCKET_SIZE; z++)
        {
            metaPath[h*BUCKET_SIZE+z] = pos_map[metaData[fullEvictPathIdx[h]][z]].pathID;
        }
    }
    ORAM.prepareDeepest(metaStash,metaPath,evictPathID,deepest);
    ORAM.prepareTarget(metaPath,evictPathID,deepest,target);
    ORAM.getDeepestBucketIdx(metaStash,metaPath,evictPathID,deepestIdx);
    
    for(int h = 0 ; h < HEIGHT +1 ; h++)
    {
        memset(evictMatrix[h],0,sizeof(TYPE_INDEX)*evictMatSize);
    }

    int currPickedSlotIdx = deepestIdx[0];
    int currTarget = target[0];
    TYPE_ID currPickedBlockID = 0;
    if(currTarget >= 0)
    {
        currPickedBlockID = STASH[currPickedSlotIdx][0];
        metaStash[currPickedSlotIdx] = -1;
    }
    int pickedEmptySlotIdx[HEIGHT+1] ={-1};
    for(int h = 1 ; h < HEIGHT +2; h++) //h=1 denotes root level
    {
        if(currTarget==h || currTarget < 0)
        {
            if(target[h]>h)
            {
                currPickedSlotIdx = deepestIdx[h];
                
                //release the slot of this block to be empty
                metaPath[(h-1)*BUCKET_SIZE+currPickedSlotIdx] = -1;
                //pick the block at this level
                evictMatrix[h-1][(BUCKET_SIZE)*(BUCKET_SIZE+1) + (currPickedSlotIdx+1)] = 1;
                
            }
            if(currTarget == h)
            {
                //drop the holding block to this level
                int empty_slot_idx = ORAM.getEmptySlot(metaPath,(h-1));
                pickedEmptySlotIdx[h-1] = empty_slot_idx;
                evictMatrix[h-1][empty_slot_idx*(BUCKET_SIZE+1) + 0] = 1;
                
            }
            currTarget = target[h];
            if(target[h] > h)
            {
                currPickedBlockID = metaData[fullEvictPathIdx[h-1]][currPickedSlotIdx];
                //metaData[fullEvictPathIdx[h-1]][currPickedSlotIdx] = 0;
                
            }
            
        }
        else // if (currTarget >h) //not drop yet, but move to the holding block to deeper level
        {
            evictMatrix[h-1][BUCKET_SIZE * (BUCKET_SIZE+1) + 0] = 1;
        }
        //keep real blocks remain at the same position
        for(int z = 0 ; z < BUCKET_SIZE; z++)
        {
            if(metaPath[(h-1)*BUCKET_SIZE + z] > 0)
            {
                evictMatrix[h-1][z * (BUCKET_SIZE+1) + z+1] = 1;
            }
        }
    }

  //at the end, update the final position of blocks
    for(int h = H+1 ; h > 0; h--)
    {
        if(target[h]>0)
        {
            int residingLevel = target[h];
            metaData[fullEvictPathIdx[residingLevel-1]][pickedEmptySlotIdx[residingLevel-1]] = metaData[fullEvictPathIdx[h-1]][deepestIdx[h]];
            TYPE_ID blockID = metaData[fullEvictPathIdx[h-1]][deepestIdx[h]];
            metaData[fullEvictPathIdx[h-1]][deepestIdx[h]] = 0;
            pos_map[blockID].pathIdx = (residingLevel-1)*BUCKET_SIZE + pickedEmptySlotIdx[residingLevel-1];
        }
    }
    // at stash
    if(target[0]>0)
    {
        int residingLevel = target[0];
        metaData[fullEvictPathIdx[residingLevel-1]][pickedEmptySlotIdx[residingLevel-1]] = STASH[deepestIdx[0]][0];
        metaStash[deepestIdx[0]] = -1;
        pos_map[STASH[deepestIdx[0]][0]].pathIdx = (residingLevel-1)*BUCKET_SIZE + pickedEmptySlotIdx[residingLevel-1];
    } 
	return 0;
}
 
 
/**
 * Function Name: thread_socket_func & send (inherent)
 *
 * Description: Generic threaded socket function for send and receive operations
 * 
 * @return 0 if successful
 */  
void* ClientKaryS3ORAMC::thread_socket_func(void* args)
{
	struct_socket* opt = (struct_socket*) args;
	
	
	sendNrecv(opt->peer_idx, opt->data_out, opt->data_out_size, opt->data_in, opt->data_in_size, opt->CMD);

		
    pthread_exit((void*)opt);
}
int ClientKaryS3ORAMC::sendNrecv(int peer_idx, unsigned char* data_out, size_t data_out_size, unsigned char* data_in, size_t data_in_size, int CMD)
{	
    unsigned char buffer_in[sizeof(CMD_SUCCESS)];
	unsigned char buffer_out[sizeof(CMD)];
	
    try
    {
        cout<< "	[ThreadSocket] Sending Command to"<< SERVER_ADDR[peer_idx] << endl;
        memcpy(buffer_out, &CMD,sizeof(CMD));
        socket[peer_idx]->send(buffer_out, sizeof(CMD));
		cout<< "	[ThreadSocket] Command SENT! " << CMD <<endl;
        socket[peer_idx]->recv(buffer_in, sizeof(CMD_SUCCESS));
		
		auto start = time_now;
		cout<< "	[ThreadSocket] Sending Data..." << endl;
		socket[peer_idx]->send (data_out, data_out_size);
		cout<< "	[ThreadSocket] Data SENT!" << endl;
        if(data_in_size == 0)
            socket[peer_idx]->recv(buffer_in,sizeof(CMD_SUCCESS));
        else
            socket[peer_idx]->recv(data_in,data_in_size);
            
		auto end = time_now;
		if(thread_max < std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count())
			thread_max = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
	}
    catch (exception &ex)
    {
        cout<< "	[ThreadSocket] Socket error!"<<endl;
		exit(0);
    }
	return 0;
}

