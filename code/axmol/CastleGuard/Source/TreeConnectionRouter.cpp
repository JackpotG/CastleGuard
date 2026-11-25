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
struct LayerBands
{
    std::vector<int> rows;
    std::vector<std::vector<float>> channels;
    float portY(int node, int other, int source) const
    {
        if (rows[node]==rows[other]) return std::numeric_limits<float>::quiet_NaN();
        const int band=rows[node]<rows[other]?rows[node]:rows[node]-1;
        return channels[band][source];
    }
};
bool mayJoin(Edge a, Edge b, Point p, Point q, Point r, Point s, const std::vector<Rect>& nodes,
             const LayerBands& bands, float gap)
{
    if (!intersects(p,q,r,s)) return false;
    const Rect intersection{std::max(std::min(p.x,q.x),std::min(r.x,s.x)),
                            std::max(std::min(p.y,q.y),std::min(r.y,s.y)),
                            std::min(std::max(p.x,q.x),std::max(r.x,s.x)),
                            std::min(std::max(p.y,q.y),std::max(r.y,s.y))};
    // Only a common source may share an outgoing trunk. A common destination never joins wires.
    if (a.from!=b.from) return false;
    const int node=a.from;
    const float y=bands.portY(node,a.to,node),otherY=bands.portY(node,b.to,node);
    if (std::isfinite(y)&&near(y,otherY))
    {
        const float face=y>center(nodes[node]).y?nodes[node].bottom:nodes[node].top;
        if (intersection.top>=std::min(face,y)-EPS && intersection.bottom<=std::max(face,y)+EPS) return true;
    }
    const auto area=expand(nodes[node],gap*3);
    return intersection.left>=area.left-EPS && intersection.right<=area.right+EPS &&
           intersection.top>=area.top-EPS && intersection.bottom<=area.bottom+EPS;
}
struct Segment { Point a, b; Edge edge; };
struct Port { Point boundary, outside; int direction; float penalty; std::vector<Point> path; };
std::vector<Port> ports(Rect r, Point target, float gap, Point offset={})
{
    const auto c = center(r);
    // Downward trees prefer bottom -> top, while horizontal defense chains prefer facing sides.
    const bool horizontal = std::abs(target.x - c.x) > std::abs(target.y - c.y) * 2;
    const int preferred = horizontal ? (target.x >= c.x ? 1 : 3) : (target.y >= c.y ? 2 : 0);
    std::vector<Port> result = {
        {{c.x+offset.x,r.top},{c.x+offset.x,r.top-gap},1,0}, {{r.right,c.y+offset.y},{r.right+gap,c.y+offset.y},0,0},
        {{c.x+offset.x,r.bottom},{c.x+offset.x,r.bottom+gap},1,0}, {{r.left,c.y+offset.y},{r.left-gap,c.y+offset.y},0,0}};
    for (int i = 0; i < 4; ++i)
    {
        result[i].penalty = i == preferred ? 0 : gap * 2;
        result[i].path={result[i].boundary,result[i].outside};
    }
    return result;
}
void coordinates(std::vector<float>& values, float lo, float hi)
{
    values.erase(std::remove_if(values.begin(), values.end(), [&](float x) { return x < lo - EPS || x > hi + EPS; }), values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), near), values.end());
}
LayerBands makeBands(const std::vector<Rect>& nodes, const std::vector<Edge>& edges,
                     const std::vector<Rect>& obstacles, float gap)
{
    LayerBands result;
    std::vector<float> rows;
    for (auto node:nodes) rows.push_back(center(node).y);
    std::sort(rows.begin(),rows.end());
    rows.erase(std::unique(rows.begin(),rows.end(),near),rows.end());
    std::vector<float> tops(rows.size(),std::numeric_limits<float>::infinity());
    std::vector<float> bottoms(rows.size(),-std::numeric_limits<float>::infinity());
    for (auto node:nodes)
    {
        const int row=static_cast<int>(std::min_element(rows.begin(),rows.end(),[&](float a,float b){return std::abs(a-center(node).y)<std::abs(b-center(node).y);})-rows.begin());
        result.rows.push_back(row);
        tops[row]=std::min(tops[row],node.top); bottoms[row]=std::max(bottoms[row],node.bottom);
    }
    for (size_t i=1;i<rows.size();++i)
    {
        const float lo=bottoms[i-1]+gap,hi=tops[i]-gap,middle=(lo+hi)/2;
        std::vector<int> sources;
        for (const auto edge:edges)
            if (edge.from>=0&&edge.to>=0&&static_cast<size_t>(edge.from)<nodes.size()&&static_cast<size_t>(edge.to)<nodes.size()&&
                std::min(result.rows[edge.from],result.rows[edge.to])<static_cast<int>(i)&&
                std::max(result.rows[edge.from],result.rows[edge.to])>=static_cast<int>(i)) sources.push_back(edge.from);
        std::sort(sources.begin(),sources.end(),[&](int a,int b) {
            return center(nodes[a]).x==center(nodes[b]).x?a<b:center(nodes[a]).x<center(nodes[b]).x;
        });
        sources.erase(std::unique(sources.begin(),sources.end()),sources.end());
        std::vector<float> channels(nodes.size(),std::numeric_limits<float>::quiet_NaN());
        std::vector<float> candidates{lo,hi};
        const float spacing=std::max(gap*2,4.0f);
        for (size_t rank=0;rank<sources.size();++rank)
            candidates.push_back(middle+(static_cast<float>(rank)-(sources.size()-1)*.5f)*spacing);
        for (auto obstacle:obstacles) for (float y:{obstacle.top,obstacle.bottom}) candidates.push_back(y);
        coordinates(candidates,lo,hi);
        for (size_t rank=0;rank<sources.size();++rank)
        {
            const float desired=middle+(static_cast<float>(rank)-(sources.size()-1)*.5f)*spacing;
            std::stable_sort(candidates.begin(),candidates.end(),[&](float a,float b){return std::abs(a-desired)<std::abs(b-desired);});
            for (float y:candidates)
                if (std::none_of(obstacles.begin(),obstacles.end(),[&](Rect r){return y>r.top+EPS&&y<r.bottom-EPS;})&&
                    std::none_of(channels.begin(),channels.end(),[&](float used){return std::isfinite(used)&&std::abs(used-y)<spacing-EPS;}))
                    {channels[sources[rank]]=y;break;}
        }
        result.channels.push_back(std::move(channels));
    }
    return result;
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
                           Edge edge, Rect bounds, float gap, const std::vector<Segment>& occupied,
                           const LayerBands& bands, Point targetOffset, bool allowCrossing, bool preferBands)
{
    auto sources = ports(nodes[edge.from], center(nodes[edge.to]), gap);
    auto targets = ports(nodes[edge.to], center(nodes[edge.from]), gap,targetOffset);
    const float sourceY=bands.portY(edge.from,edge.to,edge.from),targetY=bands.portY(edge.to,edge.from,edge.from);
    const bool layered=preferBands&&std::isfinite(sourceY)&&std::isfinite(targetY);
    const bool longOffset=layered&&!near(sourceY,targetY)&&!near(center(nodes[edge.from]).x,center(nodes[edge.to]).x);
    if (layered)
    {
        const auto extend=[&](std::vector<Port>& options,Rect node,float y) {
            options.erase(std::remove_if(options.begin(),options.end(),[&](const Port& p) {
                return p.direction==1&&((p.boundary.y<center(node).y)==(y>center(node).y));
            }),options.end());
            for (auto& p:options)
            {
                if (p.direction==1) p.path={p.boundary,{p.boundary.x,y}};
                else p.path={p.boundary,p.outside,{p.outside.x,y}};
                p.penalty=p.direction==1?0:gap*2;
                p.outside=p.path.back(); p.direction=1;
            }
        };
        extend(sources,nodes[edge.from],sourceY); extend(targets,nodes[edge.to],targetY);
        bounds.top=std::min(sourceY,targetY); bounds.bottom=std::max(sourceY,targetY);
    }
    const auto usable = [&](const Port& p, int own) {
        if (p.outside.x < bounds.left || p.outside.x > bounds.right || p.outside.y < bounds.top || p.outside.y > bounds.bottom) return false;
        for (size_t j=1;j<p.path.size();++j)
        {
            const auto a=p.path[j-1],b=p.path[j];
            for (size_t i = 0; i < obstacles.size(); ++i)
                if (!(static_cast<int>(i)==own&&j==1) && blocked(a,b,obstacles[i])) return false;
            for (const auto& segment : occupied)
                if (intersects(a,b,segment.a,segment.b) &&
                    !mayJoin(edge,segment.edge,a,b,segment.a,segment.b,nodes,bands,gap) &&
                    (!allowCrossing || near(a.x,b.x) == near(segment.a.x,segment.b.x))) return false;
        }
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
    if (layered) for (const auto& channel:bands.channels) if (std::isfinite(channel[edge.from])) ys.push_back(channel[edge.from]);
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
        if (layered&&near(a.y,b.y)&&std::none_of(bands.channels.begin(),bands.channels.end(),[&](const auto& channel){return near(a.y,channel[edge.from]);})) return -1.0f;
        for (const auto r : obstacles) if (blocked(a,b,r)) return -1.0f;
        float cost = distance(a,b);
        for (const auto& s : occupied)
        {
            if (!intersects(a,b,s.a,s.b) || mayJoin(edge,s.edge,a,b,s.a,s.b,nodes,bands,gap)) continue;
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
        float cost=p.penalty;
        for (size_t j=1;j<p.path.size();++j) cost+=distance(p.path[j-1],p.path[j]);
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
        if (goal[cell]>=0&&(!longOffset||direction==0))
        {
            const auto& p=targets[goal[cell]];
            float finalCost=cost+p.penalty+(direction!=p.direction?gap*2:0);
            for (size_t j=1;j<p.path.size();++j) finalCost+=distance(p.path[j-1],p.path[j]);
            if (finalCost<foundCost) { foundCost=finalCost; found=state; targetPort=goal[cell]; }
        }
        for (const auto [dx,dy] : std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}})
        {
            if (longOffset&&previous[state]<0&&dy!=0) continue;
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
    std::vector<Point> middle;
    const int source=startPort[found];
    for (int cursor=found;cursor>=0;cursor=previous[cursor]) middle.push_back(point(cursor/2));
    std::reverse(middle.begin(),middle.end());
    auto result=sources[source].path;
    result.insert(result.end(),middle.begin()+1,middle.end());
    const auto& end=targets[targetPort].path;
    result.insert(result.end(),end.rbegin()+1,end.rend());
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
    const auto bands=makeBands(nodes,edges,obstacles,clearance);
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
        // Incoming wires get distinct face positions, ordered by their source geometry.
        std::vector<int> parents;
        for (const auto j:order) if (edges[j].to==edges[i].to) parents.push_back(edges[j].from);
        const auto offset=[&](bool horizontal) {
            const auto coordinate=[&](int node){return horizontal?center(nodes[node]).x:center(nodes[node]).y;};
            std::sort(parents.begin(),parents.end(),[&](int a,int b){return coordinate(a)==coordinate(b)?a<b:coordinate(a)<coordinate(b);});
            parents.erase(std::unique(parents.begin(),parents.end()),parents.end());
            const auto rank=std::find(parents.begin(),parents.end(),edges[i].from)-parents.begin();
            const auto body=nodes[edges[i].to];
            return ((rank+1.0f)/(parents.size()+1.0f)-.5f)*(horizontal?body.right-body.left:body.bottom-body.top)*.7f;
        };
        const Point targetOffset{offset(true),offset(false)};
        auto points=findPath(nodes,obstacles,edges[i],bounds,clearance,occupied,bands,targetOffset,false,true);
        // A crowded layer can need separate lanes; retain the obstacle router as a fallback.
        if (points.empty()) points=findPath(nodes,obstacles,edges[i],bounds,clearance,occupied,bands,targetOffset,false,false);
        if (points.empty()) points=findPath(nodes,obstacles,edges[i],bounds,clearance,occupied,bands,targetOffset,true,true);
        if (points.empty()) points=findPath(nodes,obstacles,edges[i],bounds,clearance,occupied,bands,targetOffset,true,false);
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
                    if (near(a.y,b.y)==near(c.y,d.y)||!intersects(a,b,c,d)||mayJoin(edges[i],edges[k],a,b,c,d,nodes,bands,clearance)) continue;
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
