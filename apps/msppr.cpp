
#define KEEPWALKSINDISK 1

#include <string>
#include <fstream>
#include <cmath>
#include <fstream>

#include "api/graphwalker_basic_includes.hpp"
#include "walks/randomwalkwithstop.hpp"
#include "walks/discretedistribution.hpp"
#include "util/toplist.hpp"
#include "util/comperror.hpp"

class MultiSourcePersonalizedPageRank : public RandomWalkwithStop{
public:
    vid_t firstsource, numsources;
    wid_t walkspersource;
    hid_t maxwalklength;
    DiscreteDistribution *visitfrequencies;

public:

    void initializeApp(vid_t _firstsource, vid_t _numsources, wid_t _walkspersource, hid_t _maxwalklength){
        firstsource = _firstsource;
        numsources = _numsources;
        walkspersource = _walkspersource;
        maxwalklength = _maxwalklength;
        initializeRW(numsources*walkspersource, maxwalklength);
        #ifdef KEEPWALKSINDISK
            visitfrequencies = new DiscreteDistribution[numsources];
        #else
            visitfrequencies = new DiscreteDistribution[numsources];
        #endif
        logstream(LOG_INFO) << "Successfully allocate visitfrequencies memory for each each source, with numsources = " << numsources << std::endl;
    }

    void startWalksbyApp( WalkManager &walk_manager  ){
        logstream(LOG_INFO) << "Start walks ! Total walk number = " << numsources*walkspersource << std::endl;
        tid_t execthreads = get_option_int("execthreads", omp_get_max_threads());
        omp_set_num_threads(execthreads);
        // walk_manager.pwalks[0][0].reserve(numsources*walkspersource);
        #pragma omp parallel for schedule(static)
        for(vid_t s = firstsource; s < firstsource+numsources; s++){
            // logstream(LOG_INFO) << "Start walks from s : " << s << std::endl;
            sid_t p = getInterval(s);
            walk_manager.minstep[p] = 0;
            walk_manager.walknum[p] += walkspersource;
            vid_t cur = s - intervals[p];
            WalkDataType walk = walk_manager.encode(s-firstsource, cur, 0);
            for( wid_t j = 0; j < walkspersource; j++ ){
                walk_manager.pwalks[0][p].push_back(walk);
            }
            // #ifdef KEEPWALKSINDISK
            //     if(s%50000==0) walk_manager.freshIntervalWalks();
            // #endif
            if(s%50000==0) logstream(LOG_DEBUG) << s << std::endl;
        }
        #ifdef KEEPWALKSINDISK
            walk_manager.freshIntervalWalks();
        #endif
    }

    void updateInfo(vid_t s, vid_t dstId, tid_t threadid, hid_t hop){
        #ifdef KEEPWALKSINDISK
            visitfrequencies[s].add(dstId);
        #else
            visitfrequencies[s].add(dstId);
        #endif
    }

    /**
     * Called before an execution interval is started.
     */
    void before_exec_interval(sid_t exec_interval, vid_t window_st, vid_t window_en, WalkManager &walk_manager) {
        #ifdef KEEPWALKSINDISK
            walk_manager.readIntervalWalks(exec_interval);
        #endif
    }
    
    /**
     * Called after an execution interval has finished.
     */
    void after_exec_interval(sid_t exec_interval, vid_t window_st, vid_t window_en, WalkManager &walk_manager) {
        walk_manager.walknum[exec_interval] = 0;
		walk_manager.minstep[exec_interval] = 0xffff;
        tid_t nthreads = get_option_int("execthreads", omp_get_max_threads());
        for(tid_t t = 0; t < nthreads; t++)
            walk_manager.pwalks[t][exec_interval].clear();
        for( hid_t p = 0; p < nshards; p++){
            if(p == exec_interval ) continue;
            #ifndef KEEPWALKSINDISK
                walk_manager.walknum[p] = 0;
            #endif
			for(tid_t t=0;t<nthreads;t++){
				walk_manager.walknum[p] += walk_manager.pwalks[t][p].size();
            }
        }

        #ifdef KEEPWALKSINDISK
            walk_manager.writeIntervalWalks(exec_interval);
        #endif
    }
};


int main(int argc, const char ** argv) {
    /* GraphChi initialization will read the command line
     arguments and the configuration file. */
    set_argc(argc, argv);
    
    /* Metrics object for keeping track of performance count_invectorers
     and other information. Currently required. */
    metrics m("multi-source-personalizedpagerank");
    
    /* Basic arguments for application */
    std::string filename = get_option_string("file", "../../raid0_mnop/LiveJournal/soc-LiveJournal1.txt");  // Base filename
    vid_t firstsource = get_option_int("firstsource", 1); // vertex id of start source
    vid_t numsources = get_option_int("numsources", 100000); // Number of sources
    wid_t walkspersource = get_option_int("walkspersource", 2000); // Number of steps
    hid_t maxwalklength = get_option_int("maxwalklength", 10); // Number of steps per walk
    float prob = get_option_float("prob", 0.2); // prob of chose min step
    unsigned long long shardsize = get_option_long("shardsize", 10485760); // Size of shard, represented in KB
    
    /* Detect the number of shards or preprocess an input to create them */
    sid_t nshards = convert_if_notexists(filename, shardsize);

    /* Run */
    MultiSourcePersonalizedPageRank program;
    program.initializeApp(firstsource, numsources, walkspersource, maxwalklength);
    graphwalker_engine engine(filename, shardsize, nshards, m);
    engine.run(program, prob);

    program.visitfrequencies[0].getTop(20);

    /* Report execution metrics */
    metrics_report(m);
    return 0;
}