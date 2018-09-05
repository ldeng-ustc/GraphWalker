#ifndef RANDOMWALKWITHRESTARTWITHJOINT
#define RANDOMWALKWITHRESTARTWITHJOINT

#include <string>
#include <fstream>
#include <time.h>

#include "walks/walk.hpp" 
#include "api/datatype.hpp"

/**
 * Type definitions. Remember to create suitable graph shards using the
 * Sharder-program.
 */
 
class RandomWalkwithRestartwithJoint : public RandomWalk {

void updateByWalk(WalkDataType walk, unsigned walkid, unsigned exec_interval, Vertex *&vertices, WalkManager &walk_manager ){ 
            //get current time in microsecond as seed to compute rand_r
            unsigned threadid = omp_get_thread_num();
            WalkDataType nowwalk = walk;
            vid_t curId = walk_manager.getCurrentId(nowwalk) + intervals[exec_interval].first;
            vid_t dstId = curId;
            unsigned hop = walk_manager.getHop(nowwalk);
            // unsigned seed = (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count();
            unsigned seed = walk+curId+hop+(unsigned)time(NULL);
            while (dstId >= intervals[exec_interval].first && dstId <= intervals[exec_interval].second ){
                updateInfo(walk_manager, nowwalk, dstId);
                Vertex &nowVertex = vertices[dstId - intervals[exec_interval].first];
                if (nowVertex.outd > 0 && ((float)rand_r(&seed))/RAND_MAX > 0.15){
                    dstId = random_outneighbor(nowVertex, seed);
                }else{
                    dstId = walk_manager.getSourceId(walk);
                }
                hop++;
                nowwalk++;
                if(hop%nsteps == nsteps-1) break;
            }
            if( hop%nsteps != nsteps-1 ){
                int p = getInterval( dstId );
                if(p==-1) logstream(LOG_FATAL) << "Invalid p = -1 with dstId = " << dstId << std::endl;
                walk_manager.moveWalk(nowwalk, p, threadid, dstId - intervals[p].first);
                walk_manager.setMinStep( p, hop );
            }
    }
};

#endif