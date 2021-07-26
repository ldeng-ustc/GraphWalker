#ifndef MEMBUF_H
#define MEMBUF_H
#include <vector>
#include <list>
#include "api/datatype.hpp"

const int kOK = 0;
const int kError = 1;

class MemBuffer {
public:
    virtual int addVertex(vid_t vid) = 0;
    virtual int addEdge(vid_t s, vid_t t, bool isDel = 0) = 0;
    virtual int getNeighbors(vid_t vid, std::vector<vid_t> *out) = 0;
};

class VectorMemBuffer: public MemBuffer {
private:
    std::vector<std::vector<vid_t>> edges;
public:
    int addVertex(vid_t vid) override {
        if(vid != edges.size()) {
            return kError;
        }
        edges.push_back({});
    }

    int addEdge(vid_t s, vid_t t, bool isDel = 0) override {
        if(s > edges.size()) {
            addVertex(s);
        }
        if(t > edges.size()) {
            addVertex(t);
        }
        edges[s].push_back(t);
    }

    int getNeighbors(vid_t vid, std::vector<vid_t> *out) override {
        *out = edges[vid];
        return 0;
    }
    
};


#endif
