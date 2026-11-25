#include "TreeConnectionRouter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace TreeConnections
{
namespace
{
constexpr float EPS = .01f;
bool near(float a, float b) { return std::abs(a - b) < EPS; }
float distance(Point a, Point b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
bool same(Point a, Point b) { return distance(a, b) < EPS; }
Rect expand(Rect r, float gap) { return {r.left - gap, r.top - gap, r.right + gap, r.bottom + gap}; }
Point center(Rect r) { return {(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }
bool inside(Point p, Rect r)
{
    return p.x > r.left + EPS && p.x < r.right - EPS && p.y > r.top + EPS && p.y < r.bottom - EPS;
}
bool blocked(Point a, Point b, Rect r)
{
    if (near(a.y, b.y))
        return a.y > r.top + EPS && a.y < r.bottom - EPS &&
               std::min(a.x, b.x) < r.right - EPS && std::max(a.x, b.x) > r.left + EPS;
    return a.x > r.left + EPS && a.x < r.right - EPS &&
           std::min(a.y, b.y) < r.bottom - EPS && std::max(a.y, b.y) > r.top + EPS;
}
bool within(float n, float a, float b) { return n >= std::min(a, b) - EPS && n <= std::max(a, b) + EPS; }
bool intersects(Point a, Point b, Point c, Point d)
{
    const bool horizontal = near(a.y, b.y), otherHorizontal = near(c.y, d.y);
    if (horizontal != otherHorizontal)
        return horizontal ? within(c.x, a.x, b.x) && within(a.y, c.y, d.y)
                          : within(a.x, c.x, d.x) && within(c.y, a.y, b.y);
    return horizontal ? near(a.y, c.y) && std::max(std::min(a.x,b.x),std::min(c.x,d.x)) <= std::min(std::max(a.x,b.x),std::max(c.x,d.x)) + EPS
                      : near(a.x, c.x) && std::max(std::min(a.y,b.y),std::min(c.y,d.y)) <= std::min(std::max(a.y,b.y),std::max(c.y,d.y)) + EPS;
}
bool mayJoin(Edge a, Edge b, Point p, Point q, Point r, Point s, const std::vector<Rect>& nodes, float gap)
{
    if (!intersects(p,q,r,s)) return false;
    const Rect intersection{std::max(std::min(p.x,q.x),std::min(r.x,s.x)),
                            std::max(std::min(p.y,q.y),std::min(r.y,s.y)),
                            std::min(std::max(p.x,q.x),std::max(r.x,s.x)),
                            std::min(std::max(p.y,q.y),std::max(r.y,s.y))};
    for (int node : {a.from,a.to}) if (node==b.from || node==b.to)
    {
        const auto area=expand(nodes[node],gap*3);
        if (intersection.left>=area.left-EPS && intersection.right<=area.right+EPS &&
            intersection.top>=area.top-EPS && intersection.bottom<=area.bottom+EPS) return true;
    }
    return false;
}
struct Segment { Point a, b; Edge edge; };
struct Port { Point boundary, outside; int direction; float penalty; };
std::vector<Port> ports(Rect r, Point target, float gap)
{
    const auto c = center(r);
    // Downward trees prefer bottom -> top, while horizontal defense chains prefer facing sides.
    const bool horizontal = std::abs(target.x - c.x) > std::abs(target.y - c.y) * 2;
    const int preferred = horizontal ? (target.x >= c.x ? 1 : 3) : (target.y >= c.y ? 2 : 0);
    std::vector<Port> result = {
        {{c.x,r.top},{c.x,r.top-gap},1,0}, {{r.right,c.y},{r.right+gap,c.y},0,0},
        {{c.x,r.bottom},{c.x,r.bottom+gap},1,0}, {{r.left,c.y},{r.left-gap,c.y},0,0}};
    for (int i = 0; i < 4; ++i) result[i].penalty = i == preferred ? 0 : gap * 2;
    return result;
}
void coordinates(std::vector<float>& values, float lo, float hi)
{
    values.erase(std::remove_if(values.begin(), values.end(), [&](float x) { return x < lo - EPS || x > hi + EPS; }), values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), near), values.end());
}
std::vector<Point> simplify(const std::vector<Point>& points)
{
    std::vector<Point> result;
    for (const auto p : points)
    {
        if (!result.empty() && same(result.back(), p)) continue;
        while (result.size() >= 2)
        {
            const auto a = result[result.size()-2], b = result.back();
            if (!((near(a.x,b.x) && near(b.x,p.x)) || (near(a.y,b.y) && near(b.y,p.y)))) break;
            result.pop_back();
        }
        result.push_back(p);
    }
    return result;
}
std::vector<Point> findPath(const std::vector<Rect>& nodes, const std::vector<Rect>& obstacles,
                           Edge edge, Rect bounds, float gap, const std::vector<Segment>& occupied, bool allowCrossing)
{
    auto sources = ports(nodes[edge.from], center(nodes[edge.to]), gap);
    auto targets = ports(nodes[edge.to], center(nodes[edge.from]), gap);
    const auto usable = [&](const Port& p, int own) {
        if (p.outside.x < bounds.left || p.outside.x > bounds.right || p.outside.y < bounds.top || p.outside.y > bounds.bottom) return false;
        for (size_t i = 0; i < obstacles.size(); ++i)
            if (static_cast<int>(i) != own && blocked(p.boundary,p.outside,obstacles[i])) return false;
        for (const auto& segment : occupied)
            if (intersects(p.boundary,p.outside,segment.a,segment.b) &&
                !mayJoin(edge,segment.edge,p.boundary,p.outside,segment.a,segment.b,nodes,gap) &&
                (!allowCrossing || near(p.boundary.x,p.outside.x) == near(segment.a.x,segment.b.x))) return false;
        return true;
    };
    sources.erase(std::remove_if(sources.begin(), sources.end(), [&](const Port& p) { return !usable(p,edge.from); }),sources.end());
    targets.erase(std::remove_if(targets.begin(), targets.end(), [&](const Port& p) { return !usable(p,edge.to); }),targets.end());
    if (sources.empty() || targets.empty()) return {};
    std::vector<float> xs{bounds.left,bounds.right}, ys{bounds.top,bounds.bottom};
    for (const auto r : obstacles)
    {
        for (float x : {r.left-gap,r.left,r.right,r.right+gap}) xs.push_back(x);
        for (float y : {r.top-gap,r.top,r.bottom,r.bottom+gap}) ys.push_back(y);
    }
    for (const auto& p : sources) { xs.push_back(p.outside.x); ys.push_back(p.outside.y); }
    for (const auto& p : targets) { xs.push_back(p.outside.x); ys.push_back(p.outside.y); }
    for (const auto& s : occupied)
    {
        for (float x : {s.a.x-gap,s.a.x+gap,s.b.x-gap,s.b.x+gap}) xs.push_back(x);
        for (float y : {s.a.y-gap,s.a.y+gap,s.b.y-gap,s.b.y+gap}) ys.push_back(y);
    }
    coordinates(xs,bounds.left,bounds.right); coordinates(ys,bounds.top,bounds.bottom);
    const int width = static_cast<int>(xs.size()), height = static_cast<int>(ys.size()), count = width * height;
    const auto index = [&](Point p) {
        return static_cast<int>(std::min_element(ys.begin(),ys.end(),[&](float a,float b){return std::abs(a-p.y)<std::abs(b-p.y);})-ys.begin()) * width +
               static_cast<int>(std::min_element(xs.begin(),xs.end(),[&](float a,float b){return std::abs(a-p.x)<std::abs(b-p.x);})-xs.begin());
    };
    const auto point = [&](int i) -> Point { return {xs[i%width],ys[i/width]}; };
    std::vector<char> free(count,true);
    for (int i = 0; i < count; ++i) for (const auto r : obstacles)
        if (inside(point(i),r)) { free[i]=false; break; }
    const auto segmentCost = [&](Point a,Point b) {
        for (const auto r : obstacles) if (blocked(a,b,r)) return -1.0f;
        float cost = distance(a,b);
        for (const auto& s : occupied)
        {
            if (!intersects(a,b,s.a,s.b) || mayJoin(edge,s.edge,a,b,s.a,s.b,nodes,gap)) continue;
            if (!allowCrossing || near(a.x,b.x) == near(s.a.x,s.b.x)) return -1.0f;
            cost += (bounds.right-bounds.left+bounds.bottom-bounds.top)*4;
        }
        return cost;
    };
    std::vector<float> best(count*2,std::numeric_limits<float>::infinity());
    std::vector<int> previous(count*2,-1), startPort(count*2,-1);
    using Entry = std::pair<float,int>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& p = sources[i];
        const int state = index(p.outside)*2+p.direction;
        const float cost = distance(p.boundary,p.outside)+p.penalty;
        if (cost < best[state]) { best[state]=cost; startPort[state]=static_cast<int>(i); queue.push({cost,state}); }
    }
    float foundCost = std::numeric_limits<float>::infinity();
    int found = -1, targetPort = -1;
    std::vector<int> goal(count,-1);
    for (size_t i=0;i<targets.size();++i) goal[index(targets[i].outside)]=static_cast<int>(i);
    while (!queue.empty())
    {
        const auto [cost,state] = queue.top(); queue.pop();
        if (cost > best[state]+EPS || cost >= foundCost) continue;
        const int cell=state/2, direction=state%2, x=cell%width, y=cell/width;
        if (goal[cell]>=0)
        {
            const auto& p=targets[goal[cell]];
            const float finalCost=cost+distance(p.boundary,p.outside)+p.penalty+(direction!=p.direction?gap*2:0);
            if (finalCost<foundCost) { foundCost=finalCost; found=state; targetPort=goal[cell]; }
        }
        for (const auto [dx,dy] : std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}})
        {
            const int nx=x+dx,ny=y+dy;
            if (nx<0||nx>=width||ny<0||ny>=height) continue;
            const int next=ny*width+nx, nd=dy==0?0:1, ns=next*2+nd;
            if (!free[next]) continue;
            const float movement=segmentCost(point(cell),point(next));
            if (movement<0) continue;
            const float candidate=cost+movement+(direction!=nd?gap*2:0);
            if (candidate+EPS>=best[ns]) continue;
            best[ns]=candidate; previous[ns]=state; startPort[ns]=startPort[state]; queue.push({candidate,ns});
        }
    }
    if (found<0) return {};
    std::vector<Point> result{targets[targetPort].boundary};
    const int source=startPort[found];
    for (int cursor=found;cursor>=0;cursor=previous[cursor]) result.push_back(point(cursor/2));
    result.push_back(sources[source].boundary);
    std::reverse(result.begin(),result.end());
    return simplify(result);
}
}

std::vector<Route> route(const std::vector<Rect>& nodes,const std::vector<Edge>& edges,
                         const std::vector<Rect>& labels,Rect bounds,float clearance)
{
    std::vector<Route> result(edges.size());
    std::vector<Rect> obstacles;
    for (const auto r:nodes) obstacles.push_back(expand(r,clearance));
    for (const auto r:labels) obstacles.push_back(expand(r,clearance));
    std::vector<size_t> order;
    for (size_t i=0;i<edges.size();++i)
        if (edges[i].from>=0&&edges[i].to>=0&&edges[i].from!=edges[i].to&&
            static_cast<size_t>(edges[i].from)<nodes.size()&&static_cast<size_t>(edges[i].to)<nodes.size()) order.push_back(i);
    // Reserve nearby links first; long links must find an empty corridor around them.
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b) {
        return distance(center(nodes[edges[a].from]),center(nodes[edges[a].to])) <
               distance(center(nodes[edges[b].from]),center(nodes[edges[b].to]));
    });
    std::vector<Segment> occupied;
    for (const auto i:order)
    {
        auto points=findPath(nodes,obstacles,edges[i],bounds,clearance,occupied,false);
        if (points.empty()) points=findPath(nodes,obstacles,edges[i],bounds,clearance,occupied,true);
        result[i].points=points;
        for (size_t j=1;j<points.size();++j) occupied.push_back({points[j-1],points[j],edges[i]});
    }
    // Fixed layouts can encode a non-planar graph. Leave an unambiguous gap at any unavoidable crossing.
    for (size_t i=0;i<result.size();++i)
    {
        const auto& points=result[i].points;
        std::vector<Point> stroke;
        for (size_t j=1;j<points.size();++j)
        {
            const auto a=points[j-1],b=points[j]; const float length=distance(a,b);
            std::vector<std::pair<float,float>> cuts;
            for (size_t k=0;k<i;++k)
                for (size_t n=1;n<result[k].points.size();++n)
                {
                    const auto c=result[k].points[n-1],d=result[k].points[n];
                    if (near(a.y,b.y)==near(c.y,d.y)||!intersects(a,b,c,d)||mayJoin(edges[i],edges[k],a,b,c,d,nodes,clearance)) continue;
                    const Point crossing=near(a.y,b.y)?Point{c.x,a.y}:Point{a.x,c.y};
                    const float at=distance(a,crossing),half=std::max(4.0f,clearance*.7f);
                    cuts.push_back({std::max(0.0f,at-half),std::min(length,at+half)});
                }
            std::sort(cuts.begin(),cuts.end());
            float cursor=0;
            const auto at=[&](float t)->Point{return {a.x+(b.x-a.x)*t/length,a.y+(b.y-a.y)*t/length};};
            if(stroke.empty()) stroke.push_back(a);
            for(const auto [lo,hi]:cuts)
            {
                if(lo>cursor+EPS) { stroke.push_back(at(lo)); if(stroke.size()>1) result[i].strokes.push_back(simplify(stroke)); }
                stroke.clear(); cursor=std::max(cursor,hi); stroke.push_back(at(cursor));
            }
            if(cursor<length-EPS) stroke.push_back(b);
        }
        if(stroke.size()>1) result[i].strokes.push_back(simplify(stroke));
    }
    return result;
}
}
