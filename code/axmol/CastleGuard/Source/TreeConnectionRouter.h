#pragma once

#include <vector>

// Pure presentation geometry, shared by the game panel and the editor's WebAssembly build.
namespace TreeConnections
{
struct Point { float x = 0, y = 0; };
struct Rect { float left = 0, top = 0, right = 0, bottom = 0; };
struct Edge { int from = 0, to = 0; };
struct Route
{
    std::vector<Point> points;
    std::vector<std::vector<Point>> strokes;
};
std::vector<Route> route(const std::vector<Rect>& nodes, const std::vector<Edge>& edges,
                         const std::vector<Rect>& labels, Rect bounds, float clearance = 6);
}
