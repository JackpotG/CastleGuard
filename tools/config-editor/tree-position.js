const TreePosition = (() => {
  const requirements = typeof module !== "undefined" ? require("./upgrade-editor.js").requirements : UpgradeEditor.requirements;
  const MIN_X = .12;
  const MAX_X = .88;
  const MIN_GAP = .18;

  function plan(nodes, selected, preset) {
    let positions;
    if (preset === "children-center") {
      const children = nodes.filter((node) => requirements(node).some((r) => r.id === selected.id));
      if (!children.length) return {error: "当前节点没有直接子节点"};
      const xs = children.map((node) => node.treeX);
      positions = new Map([[selected.id, (Math.min(...xs) + Math.max(...xs)) / 2]]);
    } else if (preset === "parent-align") {
      const parents = nodes.filter((node) => requirements(selected).some((r) => r.id === node.id));
      if (!parents.length) return {error: "当前节点没有前置节点"};
      const xs = parents.map((node) => node.treeX);
      positions = new Map([[selected.id, (Math.min(...xs) + Math.max(...xs)) / 2]]);
    } else if (preset === "canvas-center") {
      positions = new Map([[selected.id, .5]]);
    } else if (preset === "row-distribute") {
      const row = nodes.filter((node) => node.treeRow === selected.treeRow)
        .map((node, index) => ({node, index}))
        .sort((a, b) => a.node.treeX - b.node.treeX || a.index - b.index);
      if (row.length > 1 && (MAX_X - MIN_X) / (row.length - 1) < MIN_GAP)
        return {error: "本层节点过多，无法保持最小间距"};
      positions = new Map(row.map(({node}, index) => [node.id,
        row.length === 1 ? .5 : MIN_X + index * (MAX_X - MIN_X) / (row.length - 1)]));
    } else return {error: "未知位置预设"};

    const row = nodes.filter((node) => node.treeRow === selected.treeRow)
      .map((node) => ({id: node.id, x: positions.get(node.id) ?? node.treeX}))
      .sort((a, b) => a.x - b.x);
    if (row.some(({x}) => !Number.isFinite(x) || x < MIN_X || x > MAX_X))
      return {error: "预设位置超出可用范围"};
    if (row.some((node, index) => index > 0 && node.x - row[index - 1].x < MIN_GAP - 1e-6))
      return {error: "预设位置与同层节点过近"};
    return {updates: [...positions].filter(([id, x]) => nodes.find((node) => node.id === id).treeX !== x)};
  }

  return {plan};
})();
if (typeof module !== "undefined") module.exports = TreePosition;
