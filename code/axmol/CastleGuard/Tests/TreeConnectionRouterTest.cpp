#include "TreeConnectionRouter.h"
#include "doctest/doctest.h"
#include <cmath>

using namespace TreeConnections;
namespace
{
bool through(Point a,Point b,Rect r)
{
    if(std::abs(a.x-b.x)<.01f) return a.x>r.left+.01f&&a.x<r.right-.01f&&std::min(a.y,b.y)<r.bottom-.01f&&std::max(a.y,b.y)>r.top+.01f;
    return a.y>r.top+.01f&&a.y<r.bottom-.01f&&std::min(a.x,b.x)<r.right-.01f&&std::max(a.x,b.x)>r.left+.01f;
}
bool crossing(Point a,Point b,Point c,Point d)
{
    const auto between=[](float x,float lo,float hi){return x>=std::min(lo,hi)-.01f&&x<=std::max(lo,hi)+.01f;};
    const bool horizontal=std::abs(a.y-b.y)<.01f, other=std::abs(c.y-d.y)<.01f;
    if(horizontal==other) return false;
    return horizontal?between(c.x,a.x,b.x)&&between(a.y,c.y,d.y):between(a.x,c.x,d.x)&&between(c.y,a.y,b.y);
}
void clearPaths(const std::vector<Rect>& nodes,const std::vector<Rect>& labels,const std::vector<Edge>& edges,const std::vector<Route>& paths)
{
    REQUIRE(paths.size()==edges.size());
    for(const auto& path:paths)
    {
        REQUIRE(path.points.size()>=2);
        for(size_t i=1;i<path.points.size();++i)
        {
            const auto a=path.points[i-1],b=path.points[i];
            CHECK((std::abs(a.x-b.x)<.01f||std::abs(a.y-b.y)<.01f));
            for(const auto r:nodes) CHECK_FALSE(through(a,b,r));
            for(const auto r:labels) CHECK_FALSE(through(a,b,r));
        }
    }
    for(size_t i=0;i<paths.size();++i) for(size_t j=0;j<i;++j)
    {
        for(const auto& a:paths[i].strokes) for(const auto& b:paths[j].strokes)
            for(size_t x=1;x<a.size();++x) for(size_t y=1;y<b.size();++y)
            {
                if(!crossing(a[x-1],a[x],b[y-1],b[y])) continue;
                const bool horizontal=std::abs(a[x-1].y-a[x].y)<.01f;
                const Point hit=horizontal?Point{b[y].x,a[x].y}:Point{a[x].x,b[y].y};
                // Shared outgoing/incoming layer buses are intentional; independent edges never join.
                bool junction=edges[i].from==edges[j].from||edges[i].to==edges[j].to;
                for(int n:{edges[i].from,edges[i].to}) if(n==edges[j].from||n==edges[j].to)
                {
                    const auto r=nodes[n];
                    if(hit.x>=r.left-18.01f&&hit.x<=r.right+18.01f&&hit.y>=r.top-18.01f&&hit.y<=r.bottom+18.01f) junction=true;
                }
                CHECK(junction);
            }
    }
}
}
TEST_SUITE("TreeConnectionRouter")
{
    TEST_CASE("a third-row multi-parent node uses the same outgoing and incoming layer buses")
    {
        const std::vector<Rect> nodes{{125,25,175,75},{365,25,415,75},
                                      {125,135,175,185},{245,135,295,185},{365,135,415,185},
                                      {275,245,325,295}};
        const std::vector<Edge> edges{{0,2},{0,3},{1,4},{4,5},{0,5}};
        const auto paths=route(nodes,edges,{}, {8,8,440,330});
        clearPaths(nodes,{},edges,paths);
        const auto& longLink=paths.back();
        REQUIRE(longLink.points.size()>=6);
        CHECK(longLink.points[1].y==105);
        CHECK(longLink.points[2].y==105);
        CHECK(longLink.points[longLink.points.size()-2].y==215);
        CHECK(longLink.points[longLink.points.size()-3].y==215);
        CHECK(longLink.strokes.size()==1);
        for(const auto& path:paths) for(size_t i=1;i<path.points.size();++i)
            if(std::abs(path.points[i-1].y-path.points[i].y)<.01f)
                CHECK((std::abs(path.points[i].y-105)<.01f||std::abs(path.points[i].y-215)<.01f));
    }
    TEST_CASE("labels below a node leave side ports available to its layer bus")
    {
        const std::vector<Rect> nodes{{125,25,175,75},{125,135,175,185},{225,245,275,295}};
        const std::vector<Rect> labels{{115,87,185,105}};
        const std::vector<Edge> edges{{0,1},{1,2},{0,2}};
        clearPaths(nodes,labels,edges,route(nodes,edges,labels,{8,8,330,330}));
    }
    TEST_CASE("same-parent siblings share a centered trunk and fork without parallel detours")
    {
        const std::vector<Rect> nodes{{125,25,175,75},{45,155,95,205},{205,155,255,205}};
        const std::vector<Edge> edges{{0,1},{0,2}};
        const auto paths=route(nodes,edges,{}, {8,8,292,240});
        clearPaths(nodes,{},edges,paths);
        for(const auto& path:paths)
        {
            REQUIRE(path.points.size()==4);
            CHECK(path.points[0].x==150);
            CHECK(path.points[0].y==75);
            CHECK(path.points[1].x==150);
            CHECK(path.points[1].y==115);
            CHECK(path.points[2].y==115);
            REQUIRE(path.strokes.size()==1);
            CHECK(path.strokes[0].size()==path.points.size());
        }
    }
    TEST_CASE("a sibling bus moves around labels while keeping its common fork")
    {
        const std::vector<Rect> nodes{{125,25,175,75},{45,155,95,205},{205,155,255,205}};
        const std::vector<Rect> labels{{100,100,120,125}};
        const std::vector<Edge> edges{{0,1},{0,2}};
        const auto paths=route(nodes,edges,labels, {8,8,292,240});
        clearPaths(nodes,labels,edges,paths);
        REQUIRE(paths[0].points.size()==4);
        REQUIRE(paths[1].points.size()==4);
        CHECK(paths[0].points[1].y==paths[1].points[1].y);
        CHECK(paths[0].points[1].y!=115);
    }
    TEST_CASE("a multi-row link detours around an aligned intermediate node and its label")
    {
        const std::vector<Rect> nodes{{125,25,175,75},{125,135,175,185},{125,245,175,295}};
        const std::vector<Rect> labels{{105,190,195,210}};
        const std::vector<Edge> edges{{0,2},{0,1},{1,2}};
        const auto paths=route(nodes,edges,labels,{8,8,292,330});
        clearPaths(nodes,labels,edges,paths);
        CHECK(paths[0].points.size()>2);
    }
    TEST_CASE("independent crossing links use empty corridors and preserve both endpoints")
    {
        const std::vector<Rect> nodes{{35,25,85,75},{215,25,265,75},{35,245,85,295},{215,245,265,295}, {125,135,175,185}};
        const std::vector<Edge> edges{{0,3},{1,2},{0,4},{4,2}};
        const auto paths=route(nodes,edges,{}, {8,8,292,330});
        clearPaths(nodes,{},edges,paths);
    }
    TEST_CASE("dense non-planar dependencies never draw intersecting independent strokes")
    {
        const std::vector<Rect> nodes{{35,25,75,65},{125,25,165,65},{215,25,255,65},
                                      {35,245,75,285},{125,245,165,285},{215,245,255,285}};
        std::vector<Edge> edges;
        for(int from=0;from<3;++from) for(int to=3;to<6;++to) edges.push_back({from,to});
        clearPaths(nodes,{},edges,route(nodes,edges,{}, {8,8,292,330}));
    }
    TEST_CASE("horizontal branches and invalid references remain safe")
    {
        const std::vector<Rect> nodes{{25,90,65,130},{125,90,165,130},{225,90,265,130}};
        const std::vector<Edge> edges{{0,1},{1,2},{0,2}};
        clearPaths(nodes,{},edges,route(nodes,edges,{}, {8,8,292,220}));
        const auto invalid=route(nodes,{{-1,2},{0,9},{0,0}},{},{8,8,292,220});
        for(const auto& path:invalid) CHECK(path.points.empty());
    }
}
