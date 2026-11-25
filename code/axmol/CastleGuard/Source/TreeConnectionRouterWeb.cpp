#include "TreeConnectionRouter.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

namespace { std::vector<float> output; }
extern "C" EMSCRIPTEN_KEEPALIVE const float* treeRoute(const float* nodeData, int nodeCount,
    const int* edgeData, int edgeCount, const float* labelData, int labelCount,
    float left, float top, float right, float bottom, float clearance)
{
    using namespace TreeConnections;
    std::vector<Rect> nodes, labels;
    std::vector<Edge> edges;
    for(int i=0;i<nodeCount;++i) nodes.push_back({nodeData[i*4],nodeData[i*4+1],nodeData[i*4+2],nodeData[i*4+3]});
    for(int i=0;i<labelCount;++i) labels.push_back({labelData[i*4],labelData[i*4+1],labelData[i*4+2],labelData[i*4+3]});
    for(int i=0;i<edgeCount;++i) edges.push_back({edgeData[i*2],edgeData[i*2+1]});
    output.clear();
    for(const auto& connection:route(nodes,edges,labels,{left,top,right,bottom},clearance))
    {
        const auto append=[](const std::vector<Point>& points) {
            output.push_back(static_cast<float>(points.size()));
            for(const auto p:points) {output.push_back(p.x);output.push_back(p.y);}
        };
        append(connection.points);
        output.push_back(static_cast<float>(connection.strokes.size()));
        for(const auto& stroke:connection.strokes) append(stroke);
    }
    return output.data();
}
#endif
