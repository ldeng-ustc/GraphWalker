
#define DYNAMICEDATA 1

#include <string>
#include <fstream>
#include <cmath>
#include <fstream>

#include "api/graphwalker_basic_includes.hpp"
#include "walks/randomwalkwithrestart.hpp"
#include "util/toplist.hpp"

class PersonalizedPageRank : public RandomWalkwithRestart{
private:
    vid_t source;
    unsigned nsources;
    unsigned N, R, L;
    unsigned *vertex_value;
    std::string vertex_value_file;
    vid_t cur_window_st;

public:
    void initializeApp( unsigned _N, vid_t _source, unsigned _nsources, unsigned _R, unsigned _L, float tail, std::string _basefilename ){
        N = _N;
        source = _source;
        nsources = _nsources;
        R = _R;
        L = _L;
        vertex_value_file = filename_vertex_data(_basefilename);
        vertex_value = (unsigned *)malloc(N*sizeof(unsigned));
        memset(vertex_value, 0, N*sizeof(unsigned));
        int f = open(vertex_value_file.c_str(), O_WRONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        assert(f >= 0);
        pwritea(f, vertex_value, sizeof(unsigned)*N, 0);
        close(f);
        free(vertex_value);
        initializeRW( source, nsources*R, L, tail );
    }

    void startWalksbyApp( WalkManager &walk_manager  ){
        for( unsigned i = 0; i < nsources; i++ ){
            for( unsigned j = 0; j < R; j++ ){
                vid_t s = source + i;
                int p = getInterval(s);
                vid_t cur = s - intervals[p].first;
                WalkDataType walk = walk_manager.encode(i, cur, 0);
                walk_manager.walks[p].push_back(walk);
                walk_manager.minstep[p] = 0;
            }
        }
        walk_manager.freshIntervalWalks();
    }

    void updateInfo(vid_t dstId){
        // std::cout << "dstId, st : " << dstId << " " << cur_window_st << std::endl;
        #pragma omp critical
        {
            vertex_value[dstId-cur_window_st]++;
        }
    }

    /**
     * Called before an execution interval is started.
     */
    void before_exec_interval(vid_t window_st, vid_t window_en) {
        /*load vertex value*/
        // std::cout << "before_exec_interval : " << window_st << " " << window_en << std::endl;
        cur_window_st = window_st;
        unsigned  window_len =  window_en -  window_st + 1;
        vertex_value = (unsigned*)malloc(sizeof(unsigned)*window_len);
        int f = open(vertex_value_file.c_str(), O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        assert(f >= 0);
        preada(f, vertex_value, sizeof(unsigned)*window_len, sizeof(unsigned)*window_st);
        close(f);
    }
    
    /**
     * Called after an execution interval has finished.
     */
    void after_exec_interval(vid_t window_st, vid_t window_en) {
         /*write back vertex back*/
        unsigned  window_len =  window_en -  window_st + 1;
        int f = open(vertex_value_file.c_str(), O_WRONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        assert(f >= 0);
        pwritea(f, vertex_value, sizeof(unsigned)*window_len, sizeof(unsigned)*window_st);
        close(f);
        free(vertex_value);
    }

    void writeFile(){
        // compute the sum of counting
        vertex_value = (unsigned*)malloc(sizeof(unsigned)*N);
        int fv = open(vertex_value_file.c_str(), O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        assert(fv >= 0);
        preada(fv, vertex_value, sizeof(unsigned)*N, sizeof(unsigned)*0);
        close(fv);
        unsigned sum = 0;
        for( unsigned i = 0; i < N; i++ )
            sum += vertex_value[i];
        std::cout << "sum : " << sum << std::endl;
        free(vertex_value);

        // conpute the counting probability
        unsigned maxwindow = 400000000;
        vid_t st = 0, len = 0;
        while( st < N ){
            len = N-st < maxwindow ? N-st : maxwindow;
            std::cout << " s , len : " << st << " " << len << std::endl;
            // len = min( maxwindow, N - st );
            vertex_value = (unsigned*)malloc(sizeof(unsigned)*len);
            fv = open(vertex_value_file.c_str(), O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
            assert(fv >= 0);
            preada(fv, vertex_value, sizeof(unsigned)*len, sizeof(unsigned)*st);
            close(fv);
            float *visit_prob = (float*)malloc(sizeof(float)*len);
            for( unsigned i = 0; i < len; i++ )
                visit_prob[i] = vertex_value[i] * 1.0 / sum;
            int fp = open(vertex_value_file.c_str(), O_WRONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
            assert(fp >= 0);
            pwritea(fp, visit_prob, sizeof(unsigned)*len, sizeof(unsigned)*st);
            close(fp);
            free(vertex_value);
            free(visit_prob);
            st += len;
        }
    }

};


int main(int argc, const char ** argv) {
    /* GraphChi initialization will read the command line
     arguments and the configuration file. */
    set_argc(argc, argv);
    
    /* Metrics object for keeping track of performance count_invectorers
     and other information. Currently required. */
    metrics m("randomwalk");
    
    /* Basic arguments for application */
    std::string filename = get_option_string("file", "/home/wang/Documents/dataset/LiveJournal/soc-LiveJournal1.txt");  // Base filename
    int nvertices = get_option_int("nvertices", 4847571); // Number of vertices
    int source = get_option_int("source", 0); // vertex id of start source
    int nsources = get_option_int("nsources", 1); // Number of sources
    int R = get_option_int("R", 100000); // Number of steps
    int L = get_option_int("L", 20); // Number of steps per walk
    float tail = get_option_float("tail", 0.05); // Ratio of stop long tail
    float prob = get_option_float("prob", 0.2); // prob of chose min step
    
    /* Detect the number of shards or preprocess an input to create them */
    int nshards = convert_if_notexists(filename, get_option_string("nshards", "auto"));

    /* Run */
    PersonalizedPageRank program;
    program.initializeApp( nvertices, source, nsources, R, L, tail, filename );
    graphwalker_engine engine(filename, nshards, m);
    engine.run(program, prob);
    
    program.writeFile();
    /* List top 20 */
    int ntop = 50;
    std::vector< vertex_value<VertexDataType> > top = get_top_vertices<VertexDataType>(filename, ntop);
    std::cout << "Print top 20 vertices: " << std::endl;
    for(int i=0; i < (int) top.size(); i++) {
        std::cout << (i+1) << ". " << top[i].vertex << "\t" << top[i].value << std::endl;
    }
    // read the accurate value
        float *ppv = (float*)malloc(nvertices*sizeof(float));
        std::string PPV_file = "/home/wang/Documents/dataset/LiveJournal/PPR0.vout";
        int f1 = open(PPV_file.c_str(), O_RDONLY, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        if (f1 < 0) {
            logstream(LOG_ERROR) << "Could not open " << PPV_file << " error: " << strerror(errno) << std::endl;
        }
        assert(f1 >= 0);
        preada(f1, ppv, nvertices*sizeof(float), 0);
        close(f1);
        //compute the error
        float err = 0;
        for( int i = 0; i < ntop; i++ ){
            err += fabs(top[i].value - ppv[top[i].vertex])/ppv[top[i].vertex];//(ppv[i]-visit_prob[i])*(ppv[i]-visit_prob[i]);
        }
        err = err / ntop;
        std::cout << "Error : " << err << std::endl;

        std::ofstream errfile;
        errfile.open("/home/wang/Documents/graph processing system/dataset/LiveJournal1/ppv0.error", std::ofstream::app);
        errfile << err << "\n" ;
        errfile.close();
        free(ppv);


    //std::cout << "average degree : " << program.count << " " << program.degree*1.0/program.count << std::endl;

    /* Report execution metrics */
    metrics_report(m);
    return 0;
}