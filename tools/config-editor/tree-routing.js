// Geometry is implemented once in the game's TreeConnectionRouter.cpp and compiled for this view.
const TreeRouting = (() => {
  let runtime;
  async function ready() { return runtime ||= await createTreeConnectionRouter(); }
  function plan(nodes, edges, labels, bounds, clearance = 6) {
    if (!runtime) throw new Error("连线模块尚未加载");
    const allocations = [];
    const allocate = (values, integers = false) => {
      const pointer = runtime._malloc(Math.max(4, values.length * 4)); allocations.push(pointer);
      (integers ? runtime.HEAP32 : runtime.HEAPF32).set(values, pointer / 4);
      return pointer;
    };
    try {
      const nodePointer = allocate(nodes.flat()), edgePointer = allocate(edges.flat(), true), labelPointer = allocate(labels.flat());
      let cursor = runtime._treeRoute(nodePointer, nodes.length, edgePointer, edges.length, labelPointer, labels.length, ...bounds, clearance) / 4;
      const readPoints = () => {
        const count = runtime.HEAPF32[cursor++], points = [];
        for (let i = 0; i < count; i++) points.push([runtime.HEAPF32[cursor++], runtime.HEAPF32[cursor++]]);
        return points;
      };
      return edges.map(() => {
        const points = readPoints(), count = runtime.HEAPF32[cursor++], strokes = [];
        for (let i = 0; i < count; i++) strokes.push(readPoints());
        return {points, strokes};
      });
    } finally { allocations.forEach((pointer) => runtime._free(pointer)); }
  }
  return {ready, plan};
})();
if (typeof module !== "undefined") module.exports = TreeRouting;
