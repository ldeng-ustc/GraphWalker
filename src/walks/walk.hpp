#ifndef DEF_GRAPHWALKER_WALK
#define DEF_GRAPHWALKER_WALK

#include <iostream>
#include <cstdio>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <queue>

#include "metrics/metrics.hpp"
#include "api/filename.hpp"
#include "api/io.hpp"
#include "walks/walkbuffer.hpp"

class WalkManager
{
protected:
	std::string base_filename;
	bid_t nblocks;
	tid_t nthreads;
	metrics &m;
public:
	wid_t* walknum; //number of tptal walks of each block
	wid_t* dwalknum; //number of disk walks of each block
	hid_t* minstep;
	WalkBuffer **pwalks;

	bid_t curp; //current block id
	WalkDataType *curwalks; // all walks of current block
	wid_t walksum;

	bid_t updateWalkNumCount;

public:
	WalkManager(metrics &_m,bid_t _nblocks, tid_t _nthreads, std::string _base_filename):base_filename(_base_filename), nblocks(_nblocks), nthreads(_nthreads), m(_m){
		pwalks = new WalkBuffer*[nthreads];
		for(tid_t i = 0; i < nthreads; i++)
			pwalks[i] = new WalkBuffer[nblocks];

		walknum = (wid_t*)malloc(nblocks*sizeof(wid_t));
		dwalknum = (wid_t*)malloc(nblocks*sizeof(wid_t));
		minstep = (hid_t*)malloc(nblocks*sizeof(hid_t));
		memset(walknum, 0, nblocks*sizeof(wid_t));
		memset(dwalknum, 0, nblocks*sizeof(wid_t));
		memset(minstep, 0xffff, nblocks*sizeof(hid_t));

		mkdir((base_filename+"_GraphWalker/walks/").c_str(), 0777);	

		updateWalkNumCount = 0;
	}

	~WalkManager(){
		for(bid_t p = 0; p < nthreads; p++)
			delete [] pwalks[p];
		delete [] pwalks;
		free(walknum);
		free(dwalknum);
		free(minstep);
	}

	WalkDataType encode( vid_t sourceId, vid_t currentId, hid_t hop ){
		assert( hop < 16384 );
		return (( (WalkDataType)sourceId & 0xffffff ) << 40 ) |(( (WalkDataType)currentId & 0x3ffffff ) << 14 ) | ( (WalkDataType)hop & 0x3fff ) ;
	}

	vid_t getSourceId( WalkDataType walk ){
		return (vid_t)( walk >> 40 ) & 0xffffff;
	}

	vid_t getCurrentId( WalkDataType walk ){
		return (vid_t)( walk >> 14 ) & 0x3ffffff;
	}

	hid_t getHop( WalkDataType walk ){
		return (hid_t)(walk & 0x3fff) ;
	}

	WalkDataType reencode( WalkDataType walk, vid_t toVertex ){
		hid_t hop = getHop(walk);
		vid_t source = getSourceId(walk);
		walk = encode(source,toVertex,hop);
		return walk;
	}

	void moveWalk( WalkDataType walk, bid_t p, tid_t t, vid_t toVertex ){
		if(pwalks[t][p].size_w == WALK_BUFFER_SIZE){
            // logstream(LOG_DEBUG) << "Walk buffer : pwalks["<< (int)t <<"]["<< p <<"] is ful with size_w = " << pwalks[t][p].size_w << " , WALK_BUFFER_SIZE = " << WALK_BUFFER_SIZE << std::endl;
			writeWalks2Disk(t,p);
        }
        assert(pwalks[t][p].size_w < WALK_BUFFER_SIZE);
		walk = reencode( walk, toVertex );
		pwalks[t][p].push_back( walk );
	}

	void writeWalks2Disk(tid_t t, bid_t p){
		m.start_time("w_writeWalks2Disk");
		std::string walksfile = walksname( base_filename, p );
		int f = open(walksfile.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
		pwritea( f, &pwalks[t][p][0], pwalks[t][p].size_w*sizeof(WalkDataType) );
		dwalknum[p] += pwalks[t][p].size_w;
		pwalks[t][p].size_w = 0;
		close(f);
		m.stop_time("w_writeWalks2Disk");
	}

	wid_t getCurrentWalks(bid_t p){
		m.start_time("w_getCurrentWalks");
		curwalks = (WalkDataType*)malloc(walknum[p]*sizeof(WalkDataType));
		if(dwalknum[p] > 0){
			readWalksfromDisk(p);
		}
		wid_t count = dwalknum[p];
		dwalknum[p] = 0;
		for(tid_t t = 0; t < nthreads; t++){
			if(pwalks[t][p].size_w > 0){
				for(wid_t w = 0; w < pwalks[t][p].size_w; w++)
					curwalks[count+w] = pwalks[t][p][w];
				count += pwalks[t][p].size_w;
				pwalks[t][p].size_w = 0;
			}
		}
		assert(count == walknum[p]);
		m.stop_time("w_getCurrentWalks");
		return count;
	}

	void readWalksfromDisk(bid_t p){
		m.start_time("w_readWalksfromDisk");

		std::string walksfile = walksname( base_filename, p );
		int f = open(walksfile.c_str(),O_RDWR, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
		if (f < 0) {
			logstream(LOG_FATAL) << "Could not load :" << walksfile << " error: " << strerror(errno) << std::endl;
		}
		assert(f > 0);
		/* read from file*/
		preada(f, &curwalks[0], dwalknum[p]*sizeof(WalkDataType), 0);
		close(f);
		/* remove the walk file*/
		unlink(walksfile.c_str()); 

		m.stop_time("w_readWalksfromDisk");
	}

	void updateWalkNum(bid_t p){
		m.start_time("clear curwalks");
		free(curwalks);
		curwalks = NULL;
		walknum[p] = 0;
		minstep[p] = 0xffff;
		m.stop_time("clear curwalks");
		updateWalkNumCount++;
		// if(updateWalkNumCount >= 10){
			m.start_time("updateWalkNum");
			updateWalkNumCount = 0;
			walksum = 0;
			#pragma omp parallel for schedule(static)
				for(p = 0; p < nblocks; p++){
					walknum[p] = dwalknum[p];
					for(tid_t t = 0; t < nthreads; t++){
						walknum[p] += pwalks[t][p].size_w;
					}
					walksum += walknum[p];
				}
			m.stop_time("updateWalkNum");
		// }
	}

     void setMinStep(bid_t p, hid_t hop ){
		if(minstep[p] > hop)
		{
			#pragma omp critical
			{
				minstep[p] = hop;
			}
		}
     }

     bid_t blockWithMaxWalks(){
		wid_t maxw = 0, maxp = 0;
		for(bid_t p = 0; p < nblocks; p++) {
			if( maxw < walknum[p] ){
				maxw = walknum[p];
				maxp = p;
			}
	   	}
		return maxp;
     }

     bid_t blockWithMinStep(){
		hid_t mins = 0xffff, minp = 0;
		for(bid_t p = 0; p < nblocks; p++) {
			if( mins > minstep[p] ){
				mins = minstep[p];
				minp = p;
			}
	   	}
		return minp;
     }

     bid_t blockWithMaxWeight(){
		float maxwt = 0;
		bid_t maxp = 0;
		for(bid_t p = 0; p < nblocks; p++) {
			if(  maxwt < (float)walknum[p]/minstep[p] ){
				maxwt = (float)walknum[p]/minstep[p];
				maxp = p;
			}
	   	}
		return maxp;
     }

     bid_t blockWithRandom(){
		bid_t ranp = rand() % nblocks;
		return ranp;
     }

	bid_t chooseBlock(float prob){
		float cc = ((float)rand())/RAND_MAX;
		if( cc < prob ){
			return blockWithMinStep();
		}
		return blockWithMaxWalks();
	}

     void printWalksDistribution(bid_t exec_block ){
		//print walk number decrease trend
		std::string walk_filename = base_filename + ".walks";
		std::ofstream ofs;
	    ofs.open(walk_filename.c_str(), std::ofstream::out | std::ofstream::app );
	   	wid_t sum = 0;
	  	for(bid_t p = 0; p < nblocks; p++) {
	      		sum += walknum[p];
	   	}
	  	ofs << exec_block << " \t " << walknum[exec_block] << " \t " << sum << std::endl;
	 	ofs.close();
     }    

};

#endif