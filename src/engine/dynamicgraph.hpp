
#ifndef DEF_DYNAMIC_GRAPH
#define DEF_DYNAMIC_GRAPH

#include <algorithm>

#include "engine/staticgraph.hpp"

class DynamicGraph : public StaticGraph {
public:

    /** memory buffer management **/
    vid_t *ebuffer; //edge buffer
    vid_t *immutable_ebuffer; // immutable edge buffer for compaction
    eid_t bufcap; //max number of edges in ebuffer and immutable_ebuffer
    eid_t bufsize; //number of edges in ebuffer 

    /** edge logs mangement **/
    bid_t ngroups; //number of log groups 
    vid_t nverts_per_grp; //number of vertices per log group
    uint8_t nbits_nverts_per_grp; //number of bits of nverts_per_grp
    eid_t logcap; //capcity of logs in a memory group log buffer
    size_t logsize; //capcity (Bytes) of disk log file
    std::vector<eid_t> nglogs; //number of logs in a memory group log buffer
    std::vector<vid_t*> glogs; //memory group log buffer
    std::vector<BitMap> bitmaps; //bitmap to indicate whether a vertex has edges in log file

        
public:
        
    DynamicGraph(metrics &_m, std::string _base_filename, size_t _blocksize, 
        size_t buffersize = 0, vid_t _nverts_per_grp = 0, size_t _logsize = 0)
        : StaticGraph(_m, _base_filename, 1, 1, _blocksize*1024), 
        ebuffer(NULL), immutable_ebuffer(NULL), bufcap((buffersize * 1024 * 1024) / (sizeof(vid_t)*2)), bufsize(0), 
        ngroups(0), nverts_per_grp(_nverts_per_grp), nbits_nverts_per_grp(log(_nverts_per_grp)/log(2)),
        logcap(16*1024), logsize(_logsize*1024){

        /**block management **/
        blocks.push_back(0);
        blocks.push_back(1);

        /** memory buffer management **/
        ebuffer = (vid_t*) malloc(bufcap*sizeof(vid_t)*2);
        immutable_ebuffer = NULL;
        
        /** edge logs mangement **/
        addLogGroup();

        logstream(LOG_INFO) << "buffer capacity = " << bufcap << " edges(" << buffersize << "MB).\n" 
                            << "number of log groups = " << ngroups << ", nverts_per_grp = " << nverts_per_grp << ", nbits_nverts_per_grp = " << (int)nbits_nverts_per_grp << ".\n" 
                            << "each group's logcap = " << logcap << " edges, logsize = " << _logsize << "KB.\n" 
                            << "nblocks = " << nblocks << ", blocksize = " << _blocksize << "KB.\n"
                            << std::endl;
    }
        
    virtual ~DynamicGraph() {
        m.start_time("destroyGraph");
        if(immutable_ebuffer != NULL) free(immutable_ebuffer);
        if(ebuffer != NULL) free(ebuffer);

        for(bid_t g = 0; g < ngroups; g++){
            if(glogs[g] != NULL) free(glogs[g]);
        }
        m.stop_time("destroyGraph");
    }

    inline bid_t getBlockByVertexId(vid_t v){
        return getBlockByGroupId(v >> nbits_nverts_per_grp);
    }

    inline bid_t getBlockByGroupId(bid_t g){
        return (bid_t)binarySearch(blocks.data(), g, 0, nblocks);
    }

    inline void addLogGroup(){
        m.start_time("addLogGroup");
        nglogs.push_back(0);
        glogs.push_back(NULL);
        BitMap bitmap = BitMap(nverts_per_grp/8);
        bitmaps.push_back(bitmap);
        glogs[ngroups] = (vid_t*)malloc(logcap*sizeof(vid_t)*2);
        ngroups++;
        blocks[nblocks] = ngroups;
        m.stop_time("addLogGroup");
    }
    
    inline void addVertex(){
        N++;
        if(N % nverts_per_grp == 0) addLogGroup();
    }
    
    void addEdge(vid_t s, vid_t t, bool isDel = 0){
        if(s == N) addVertex();
        if(t == N) addVertex();
        if(!(s <= N && t <= N)) logstream(LOG_FATAL) << s << " " << t << " " << N << std::endl;
        assert(s <= N && t <= N);
        
        if(bufsize >= bufcap){
            immutable_ebuffer = ebuffer;
            flush();
        }
        ebuffer[2*bufsize] = s;
        ebuffer[2*bufsize+1] = t;
        bufsize++;
    }

    /***
    * Flush edge logs from memory buffer to disk log file
    * Incluing edge logs classification, each graph block equipment with a log file
    ***/
    void flush(){
        m.start_time("_2_flush_");

        //2. edge log classification by block
        m.start_time("_3_flush_2_classifyLog");
        for(eid_t e = 0; e < bufcap; e++){
            vid_t v = immutable_ebuffer[2*e];
            bid_t g = (bid_t)(v >> nbits_nverts_per_grp);
            eid_t i = 2*nglogs[g];
            glogs[g][i] = v;
            glogs[g][i+1] = immutable_ebuffer[2*e+1];
            nglogs[g]++;
            bitmaps[g].bitmapSet(v & (nverts_per_grp-1));
            if(nglogs[g] == logcap){
                writeLog(g);
            }
        }
        m.stop_time("_3_flush_2_classifyLog");

        //2. edge log classification by block
        m.start_time("_3_flush_3_writeLog");
        for(bid_t g = 0; g < ngroups; g++){
            if(nglogs[g] > 0){
                writeLog(g);
            }
        }
        m.stop_time("_3_flush_3_writeLog");

        bufsize = 0;

        m.stop_time("_2_flush_");
    }

    /***
    * Write logs of a log group to log file
    ***/
    void writeLog(bid_t g){
        m.start_time("_4_flush_3_writeLogs");
        if(nglogs[g] > 0){
            std::string logfile = logname(base_filename, g);
            appendfile(logfile, glogs[g], nglogs[g] * 2 * sizeof(vid_t));
            size_t fsize = filesize(logfile);
            // logstream(LOG_INFO) << "Write logs of group " << g  << ", nglogs[g] = " << nglogs[g] << ", logfile size = " << fsize << ", logsize = " << logsize << std::endl;
            if(fsize >= logsize){ 
                // logstream(LOG_WARNING) << "Start getBlockByGroupId of " << g << "..." << std::endl;
                bid_t p = getBlockByGroupId(g);
                // logstream(LOG_INFO) << "Log group " << g << " is full, start compact block " << p << " : [" << blocks[p] << ", " << blocks[p+1] << ")..." << std::endl;
                compaction(p);
                m.start_time("_4_flush_3_writeLogs_removeLogfiles");
                for(bid_t g1 = blocks[p]; g1 < blocks[p+1]; g1++){
                    remove(logname(base_filename, g1).c_str());
                    bitmaps[g1].bitmapReset();
                }
                m.stop_time("_4_flush_3_writeLogs_removeLogfiles");
            }
            nglogs[g] = 0;
        }
        m.stop_time("_4_flush_3_writeLogs");
    }

    void writeLogfiles(){
        for(bid_t g = 0; g < ngroups; g++){
            writeLog(g);
        }
    }

    /***
    * Merge several log groups to a block subgraph(CSR)
    ***/
    void compaction(bid_t p){
        m.start_time("_5_compaction_");
        
        m.start_time("_compaction_1_loadSubGraph");
        vid_t nverts = 0, *csr = NULL;
        eid_t nedges = 0, *beg_pos = NULL;
        loadSubGraph(p, beg_pos, csr, &nverts, &nedges);
        m.stop_time("_compaction_1_loadSubGraph");

        //8. Rewrite the CSR to disk
        m.start_time("_compaction_2_writeSubGraph");
        // logstream(LOG_DEBUG) << "Start to writeSubGraph for block_" << p << ": [" << blocks[p] << ", " << blocks[p+1] <<"), nverts = " << nverts << ", nedges = " << nedges << ", filesize = " << nedges*sizeof(vid_t)/1024/1024 << "MB."<< std::endl;
        writeSubGraph(p, csr, nedges, beg_pos, nverts);
        m.stop_time("_compaction_2_writeSubGraph");

        //9. Free memory
        m.start_time("_compaction_3_free");
        if(beg_pos != NULL) free(beg_pos);
        if(csr != NULL) free(csr);
        m.stop_time("_compaction_3_free");
        
        m.stop_time("_5_compaction_");

    }

    
    void loadSubGraph(bid_t p, eid_t * &beg_pos, vid_t * &csr, vid_t *nverts, eid_t *nedges){

        //1. Load the CSR from disk
        m.start_time("_load_1_loadCSR");
        bid_t ngrps = blocks[p+1]-blocks[p];
        *nverts = ngrps << nbits_nverts_per_grp;
        if(p == nblocks-1) *nverts = N - (blocks[p] << nbits_nverts_per_grp);
        // logstream(LOG_DEBUG) << "Start loadSubGraph : " << p << ": [" << blocks[p] << ", " << blocks[p+1] << ") with nverts = " << *nverts << "..." << std::endl;
        loadSubGraphCSR(p, beg_pos, csr, *nverts, nedges);
        m.stop_time("_load_1_loadCSR");
        // logstream(LOG_INFO) << "After loadSubGraphCSR : " << p << ", nverts = " << *nverts << ", nedges = " << *nedges << std::endl;

        //2. Load the logs of block_p
        m.start_time("_load_2_loadLog");
        vid_t** logs = (vid_t**)malloc(ngrps*sizeof(vid_t*));
        eid_t* nlogs = (eid_t*)malloc(ngrps*sizeof(eid_t));
        for(bid_t g = 0; g < ngrps; g++){
            std::string logfile = logname(base_filename, blocks[p]+g);
            nlogs[g] = readfile(logfile, &logs[g]) / (sizeof(vid_t)*2);
        }
        m.stop_time("_load_2_loadLog");

        //2. compute new degree for each vertex
        m.start_time("_load_3_computeDegree");
        vid_t stv = blocks[p] << nbits_nverts_per_grp;
        eid_t* newdeg = (eid_t*)malloc((*nverts)*sizeof(eid_t));
        memset(newdeg, 0, (*nverts)*sizeof(eid_t));
        for(bid_t g = 0; g < ngrps; g++){
            for(eid_t e = 0; e < nlogs[g]; e++){
                logs[g][2*e] -= stv;
                newdeg[logs[g][2*e]]++;
            }
            *nedges += nlogs[g];
            // logstream(LOG_WARNING) << g << " " << nlogs[g] << " " << *nedges << std::endl;
        }
        m.stop_time("_load_3_computeDegree");
        // logstream(LOG_INFO) << "After loadSubGraphlogs and computed degrees : " << p << ", nverts = " << *nverts << ", nedges = " << *nedges << std::endl;

        //5. Compute new begpos
        m.start_time("_load_4_compBeg");
        vid_t * newcsr = (vid_t*) malloc((*nedges)*sizeof(vid_t));
        eid_t * newbeg_pos = (eid_t*) malloc((*nverts+1)*sizeof(eid_t));
        eid_t begoff = 0;
        newbeg_pos[0] = 0;
        for(vid_t v = 0; v < *nverts; v++){
            if(newdeg[v] > 0)   begoff += newdeg[v];
            newbeg_pos[v+1] = beg_pos[v+1] + begoff;
        }
        assert(newbeg_pos[*nverts] == *nedges);
        m.stop_time("_load_4_compBeg"); 

        //6. copy CSR
        m.start_time("_load_5_copyCSR");
        for(vid_t v = 0; v < *nverts; v++){
            if(beg_pos[v+1]-beg_pos[v] > 0){
                vid_t u = v;
                while(newdeg[v]==0 && v < *nverts-1) v++;
                memcpy(newcsr+newbeg_pos[u], csr+beg_pos[u], (beg_pos[v+1]-beg_pos[u])*sizeof(vid_t));
            }
        }
        m.stop_time("_load_5_copyCSR");

        //7. write edge log to CSR
        m.start_time("_load_6_mergeLog2CSR");
        for(bid_t g = 0; g < ngrps; g++){
            for(eid_t e = 0; e < nlogs[g]; e++){
                vid_t v = logs[g][2*e];
                eid_t pos = newbeg_pos[v+1] - newdeg[v];
                newcsr[pos] = logs[g][2*e+1];
                newdeg[v]--;
            }
        }
        m.stop_time("_load_6_mergeLog2CSR");

        
        m.start_time("_load_7_free");
        for(bid_t g = 0; g < ngrps; g++){
            if(logs[g] != NULL) free(logs[g]);
        }
        if(logs != NULL) free(logs);
        if(nlogs != NULL) free(nlogs);
        if(beg_pos != NULL) free(beg_pos);
        if(csr != NULL) free(csr);
        csr = (vid_t*)newcsr;
        beg_pos = (eid_t*)newbeg_pos;
        m.stop_time("_load_7_free");
    }

    void loadSubGraphCSR(bid_t p, eid_t * &beg_pos, vid_t * &csr, vid_t nverts, eid_t *nedges){
        std::string blkname = blockname(base_filename, blocks[p]);
        loadBegpos(blkname, beg_pos, nverts);
        *nedges = beg_pos[nverts] - beg_pos[0];
        // logstream(LOG_ERROR) << "loadSubGraphCSR : p = " << p << ": [" << blocks[p] << ", " << blocks[p+1] <<"), nverts = " << nverts << ", *nedges = " << *nedges << ", beg_pos[0] = " << beg_pos[0] << ", beg_pos[nverts] = " << beg_pos[nverts] << std::endl;
        loadCSR(blkname, csr, *nedges);
    }

    void loadBegpos(std::string bname, eid_t * &beg_pos, vid_t nverts, vid_t off = 0){

        beg_pos = (eid_t*)malloc((nverts+1)*sizeof(eid_t));

        std::string beg_posname = bname + ".beg_pos";
        FILE *tryf = fopen(beg_posname.c_str(), "r");
        if (tryf == NULL) { // Not found block beg_pos file
            // logstream(LOG_WARNING) << "Could not find the block beg_pos file : " << beg_posname << std::endl;
            memset(beg_pos, 0, (nverts+1)*sizeof(eid_t));
            return ;
        }
        fclose(tryf);

        int beg_posf = open(beg_posname.c_str(),O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        if (beg_posf < 0) {
            logstream(LOG_FATAL) << "Could not load :" << beg_posname << ", error: " << strerror(errno) << std::endl;
        }
        assert(beg_posf > 0);

        /* read beg_pos file */
        size_t nread = preada(beg_posf, beg_pos, (size_t)(nverts+1)*sizeof(eid_t), (size_t)(off)*sizeof(eid_t));
        if(nread == 0) {
            memset(beg_pos, 0, (nverts+1)*sizeof(eid_t));
        }else{
            vid_t rnverts = nread / sizeof(eid_t) - 1;
            // logstream(LOG_WARNING) << "rnverts = " << rnverts << ", nverts = " << nverts << std::endl;
            for(vid_t v = rnverts+1; v <= nverts; v++)
            beg_pos[v] = beg_pos[rnverts];
        }

        close(beg_posf);
    }

    void loadCSR(std::string bname, vid_t * &csr, eid_t nedges, eid_t off = 0){
        if(nedges <= 0) return;

        std::string csrname = bname + ".csr";
        int csrf = open(csrname.c_str(), O_RDONLY | O_CREAT, S_IROTH | S_IWOTH | S_IWUSR | S_IRUSR);
        if (csrf < 0) {
            logstream(LOG_FATAL) << "Could not load :" << csrname << ", error: " << strerror(errno) << std::endl;
        }
        assert(csrf > 0);

        /* read csr file */
        csr = (vid_t*)malloc(nedges*sizeof(vid_t));
        size_t nread = preada(csrf, csr, nedges*sizeof(vid_t), off*sizeof(vid_t));
        assert(nread == nedges*sizeof(vid_t));

        close(csrf); 
    }

    void writeSubGraph(bid_t p, vid_t* csr, eid_t nedges, eid_t* beg_pos, vid_t nverts){ 
        // logstream(LOG_NONE) << nedges*sizeof(vid_t) << " " << blocksize << " " << blocks[p+1]  << " " << blocks[p] << std::endl;
        if( (size_t)(nedges*sizeof(vid_t)) > blocksize && blocks[p+1] - blocks[p] > 1){
            splitSubGraph(p, csr, nedges, beg_pos, nverts);
        }else{
            m.start_time("_compaction_2__writeSubGraphCSR");
            writeSubGraphCSR(p, csr, nedges, beg_pos, nverts);
            m.stop_time("_compaction_2__writeSubGraphCSR");
            // logstream(LOG_DEBUG) << "writeSubGraphCSR for block_" << p << ": [" << blocks[p] << ", " << blocks[p+1] <<"), nverts = " << nverts << ", nedges = " << nedges << ", filesize = " << nedges*sizeof(vid_t)/1024/1024 << "MB."<< std::endl;
        }
    }

    void splitSubGraph(bid_t p, vid_t* csr, eid_t nedges, eid_t* beg_pos, vid_t nverts){
        m.start_time("_compaction_2__splitSubGraph");
        vid_t nverts1 = (vid_t)binarySearch(beg_pos, nedges/2, 0, nverts);
        bid_t g = (nverts1 >> nbits_nverts_per_grp);
        if(g == 0) g = 1;
        auto it = blocks.begin() + p + 1;
        blocks.insert(it, g + blocks[p] );
        nblocks++;

        nverts1 = g << nbits_nverts_per_grp;
        eid_t nedges1 = beg_pos[nverts1];

        // logstream(LOG_WARNING) << "Split subgraph " << p << ": [" << blocks[p] << ", " << blocks[p+2] << ") with " << nverts << " vertices, into :" << std::endl;
        // logstream(LOG_INFO) << " " << p << ": [" << blocks[p] << ", " << blocks[p+1] << "), #vertices = " << nverts1 << ", #edges = " << nedges1 << std::endl;
        // logstream(LOG_INFO) << " " << p+1 << ": [" << blocks[p+1] << ", " << blocks[p+2] << "), #vertices = " << nverts - nverts1 << ", #edges = " << nedges-nedges1 << std::endl;

        writeSubGraph(p, csr, nedges1, beg_pos, nverts1);
        for(vid_t v = nverts1; v <= nverts; v++)    beg_pos[v] -= nedges1;
        bid_t nextp = getBlockByGroupId(blocks[p]+g);
        // logstream(LOG_DEBUG) << "p = " << p << ": [" << blocks[p] << ", " << blocks[p+1] << "), nextp = " << nextp << ": [" << blocks[nextp] << ", " << blocks[nextp+1] << ")" << std::endl;
        // logstream(LOG_DEBUG) << "nverts1 = " << nverts1 << "beg_pos[nverts1] = " << beg_pos[nverts1] << ", beg_pos[nverts1+1] = " << beg_pos[nverts1+1] << std::endl;
        writeSubGraph(nextp, csr+nedges1, nedges-nedges1, beg_pos+nverts1, nverts-nverts1);
        beg_pos[nverts] += nedges1;
        m.stop_time("_compaction_2__splitSubGraph");
    }

    void writeSubGraphCSR(bid_t p, vid_t* csr, eid_t nedges, eid_t* beg_pos, vid_t nverts){
        std::string blkname = blockname(base_filename, blocks[p]);
        std::string beg_posname = blkname + ".beg_pos";
        writefile(beg_posname, beg_pos, (size_t)(nverts+1)*sizeof(eid_t));
        std::string csrname = blkname + ".csr";
        writefile(csrname, csr, (size_t)nedges*sizeof(vid_t));
        // logstream(LOG_ERROR) << "writeSubGraphCSR : p = " << p << ": [" << blocks[p] << ", " << blocks[p+1] <<"), nverts = " << nverts << ", *nedges = " << nedges << ", beg_pos[0] = " << beg_pos[0] << ", beg_pos[nverts] = " << beg_pos[nverts] << std::endl;
    }

    std::vector<vid_t> getNeighbors(vid_t v){
        if(v >= N){
            logstream(LOG_ERROR) << "Invalid vertex id : " << v << " >= " << N << std::endl;
            std::vector<vid_t> neighbors;
            return neighbors;
        }

        bid_t g = v >> nbits_nverts_per_grp;
        bid_t p = getBlockByGroupId(g);
        // logstream(LOG_DEBUG) << "v = " << v << ", p = " << p << ", s = " << s << ", segs[p].size() = " << segs[p].size() << std::endl;

        m.start_time("test_searchNeighbors_1_InCSR");
        std::vector<vid_t> neighbors = getNeighborsInCSR(v, p);
        // logstream(LOG_DEBUG) << "After loadBlock, # of neighbors = " << neighbors.size() << std::endl;
        m.stop_time("test_searchNeighbors_1_InCSR");

        // load and search logs of block_p
        if(bitmaps[g].bitmapGet( v & (nverts_per_grp-1) )){

            m.start_time("test_searchNeighbors_2_InLogfile");
            m.start_time("test_searchNeighbors_2_InLogfile_1_readfile");
            // std::string logfile = segmentname(base_filename, p, segs[p][s]) + ".log";
            bid_t g = v >> nbits_nverts_per_grp;
            std::string logfile = logname(base_filename, g);
            vid_t* logs;
            eid_t nlogs = readfile(logfile, &logs) / (sizeof(vid_t)*2);
            m.stop_time("test_searchNeighbors_2_InLogfile_1_readfile");
            m.start_time("test_searchNeighbors_2_InLogfile_2_searchInfile");
            for(eid_t e = 0; e < nlogs; e++){
                if(logs[2*e] == v){
                    neighbors.push_back(logs[2*e+1]);
                }
            }
            m.stop_time("test_searchNeighbors_2_InLogfile_2_searchInfile");
            m.start_time("test_searchNeighbors_2_InLogfile_3_freeLog");
            if(logs != nullptr) free(logs);
            // logstream(LOG_DEBUG) << "After loadLog, # of neighbors = " << neighbors.size() << std::endl;
            m.stop_time("test_searchNeighbors_2_InLogfile_3_freeLog");
            m.stop_time("test_searchNeighbors_2_InLogfile");

        }

        // search logs of memory edge buffer
        m.start_time("test_searchNeighbors_3_InMembuf");
        for(eid_t e = 0; e < bufsize; e++){
            if(ebuffer[2*e] == v){
                neighbors.push_back(ebuffer[2*e+1]);
            }
        }
        // logstream(LOG_DEBUG) << "After searchBuffer, # of neighbors = " << neighbors.size() << std::endl;
        m.stop_time("test_searchNeighbors_3_InMembuf");

        return neighbors;
    }

    std::vector<vid_t> getNeighborsInCSR(vid_t v, bid_t p){
        std::vector<vid_t> neighbors;

        std::string blkname = blockname(base_filename, blocks[p]);
        // logstream(LOG_WARNING) << v << " " << segname << std::endl;
        eid_t *beg_pos;// = (eid_t*)malloc(2*sizeof(eid_t));
        vid_t off = v - (blocks[p] << nbits_nverts_per_grp);
        loadBegpos(blkname, beg_pos, 1, off);

        eid_t nedges = beg_pos[1] - beg_pos[0];
        if(nedges > 0){
            // logstream(LOG_WARNING) << nedges << std::endl;
            vid_t *csr;// = (vid_t*)malloc(nedges*sizeof(vid_t));
            loadCSR(blkname, csr, nedges, beg_pos[0]);
            for(eid_t i = 0; i < nedges; i++){
                neighbors.push_back(csr[i]);
            }
            if(csr != NULL) free(csr);
        }
        if(beg_pos != NULL) free(beg_pos);

        return neighbors;
    }


};

#endif