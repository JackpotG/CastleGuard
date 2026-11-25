const UpgradeEditor = (() => {
  const layout = Object.freeze({minX: .12, maxX: .88, minGap: .18, maxRow: 16});
  function requirements(node) {
    return node.prerequisites ?? (node.requires ? [{id: node.requires, level: node.requiredLevel ?? 1}] : []);
  }
  function normalize(data) {
    for (const node of [...data.upgrades, ...data.skills]) {
      node.prerequisites = requirements(node);
      delete node.requires;
      delete node.requiredLevel;
    }
    for (const node of data.upgrades) {
      if (!node.effects && Array.isArray(node.values)) node.effects = [{attribute: node.id, operation: "set", values: node.values}];
      delete node.values;
      node.track ??= "defense";
    }
    migrateLegacyLayout(data.upgrades);
    return data;
  }
  // Compatibility for old defense catalogues; new nodes always store explicit layout.
  function migrateLegacyLayout(nodes) {
    const legacy = nodes.filter((node) => node.track === "defense" && node.treeX === undefined && (node.treeRow === undefined || node.treeRow === -1));
    const rows = new Map(), visiting = new Set();
    const resolve = (node) => {
      if (!legacy.includes(node)) return node.treeRow;
      if (rows.has(node)) return rows.get(node);
      if (visiting.has(node)) throw new Error("旧城防配置的前置关系存在循环");
      visiting.add(node);
      const parents = requirements(node).map((r) => nodes.find((parent) => parent.id === r.id));
      if (parents.some((parent) => !parent)) throw new Error("旧城防配置缺少前置节点");
      const row = parents.length ? Math.max(...parents.map(resolve)) + 1 : 0;
      rows.set(node, row); visiting.delete(node);
      return row;
    };
    for (const node of legacy) node.treeRow = resolve(node);
    const sorted = [...legacy].sort((a, b) => a.id < b.id ? -1 : a.id > b.id ? 1 : 0);
    const placed = nodes.filter((node) => !legacy.includes(node));
    for (const node of sorted) {
      const count = nodes.filter((other) => other.track === node.track && other.treeRow === node.treeRow).length;
      const preferred = count === 1 ? [.5] : count === 2 ? [.27, .73] :
        Array.from({length: count}, (_, i) => Number((.12 + i * .76 / (count - 1)).toFixed(6)));
      const candidates = [...preferred, ...Array.from({length: 77}, (_, i) => Number((.12 + i * .01).toFixed(6)))];
      node.treeX = candidates.find((x) => placed.every((other) => other.track !== node.track || other.treeRow !== node.treeRow || Math.abs(other.treeX - x) >= .18 - 1e-6));
      if (node.treeX === undefined) throw new Error("旧城防配置同层节点过多，请调整布局");
      placed.push(node);
    }
  }
  function validate(data, attributes) {
    if (data.upgrades.length > 4096) return "升级节点数量过多";
    const ids = new Set();
    const setters = new Set();
    for (const node of data.upgrades) {
      if (!/^[a-z][a-z0-9_]{0,63}$/.test(node.id) || ids.has(node.id)) return "节点 ID 无效或重复";
      ids.add(node.id);
      if (!node.name?.trim() || !node.description?.trim() || !["coins", "crystals"].includes(node.currency) ||
          !node.costs?.length || node.costs.length > 50 || !node.costs.every((v) => Number.isInteger(v) && v > 0 && v <= 2147483647))
        return `${node.id} 的名称、说明、货币或价格无效`;
      if (!Number.isInteger(node.unlockStage ?? 0) || (node.unlockStage ?? 0) < 0 || (node.unlockStage ?? 0) > 2147483647) return "解锁关卡须为非负整数";
      if (!node.effects?.length) return "每个节点至少需要一个效果";
      for (const effect of node.effects) {
        const attribute = attributes.find((item) => item.id === effect.attribute);
        if (!attribute || !["set", "add", "multiply"].includes(effect.operation) ||
            effect.values?.length !== node.costs.length + 1 || effect.values.some((v) => !Number.isFinite(v) || Math.abs(v) > 99999 ||
              (effect.operation === "multiply" && v <= 0) || (effect.operation === "set" && (v < attribute.minimum || v > attribute.maximum))))
          return `${node.id} 的效果类型或等级数值无效`;
        if (effect.operation === "set") {
          if (setters.has(effect.attribute)) return `${attribute.name} 只能有一个设置基础值的效果`;
          setters.add(effect.attribute);
        }
        if ((effect.operation === "add" && effect.values[0] !== 0) || (effect.operation === "multiply" && effect.values[0] !== 1))
          return "未升级时，加值须为 0、倍率须为 1";
      }
      const track = node.track || "defense";
      if (!["bow", "magic", "defense"].includes(track)) return "节点所属设施无效";
      const parents = requirements(node);
      if (!Array.isArray(parents)) return "前置条件须为列表";
      const seen = new Set();
      for (const requirement of parents) {
        const parent = data.upgrades.find((item) => item.id === requirement?.id);
        if (!parent || parent === node || seen.has(parent.id) || !Number.isInteger(requirement.level) ||
            requirement.level < 1 || requirement.level > parent.costs.length || (parent.track || "defense") !== track)
          return `${node.id} 的前置节点或等级无效（不可重复）`;
        seen.add(parent.id);
      }
    }
    const visited = new Map();
    const visit = (node) => {
      if (visited.has(node.id)) return visited.get(node.id) === 2;
      visited.set(node.id, 1);
      if (!requirements(node).every((r) => visit(data.upgrades.find((p) => p.id === r.id)))) return false;
      visited.set(node.id, 2);
      return true;
    };
    if (!data.upgrades.every(visit)) return "前置关系存在循环";
    for (const track of ["bow", "magic", "defense"]) {
      const error = validateLayout(data.upgrades.filter((node) => (node.track || "defense") === track));
      if (error) return error;
    }
    return "";
  }
  function validateLayout(nodes) {
    for (const node of nodes) {
      if (!Number.isInteger(node.treeRow) || node.treeRow < 0 || node.treeRow > layout.maxRow ||
          !Number.isFinite(node.treeX) || node.treeX < layout.minX || node.treeX > layout.maxX)
        return `${node.id} 的树位置无效`;
      if (!requirements(node).length && node.treeRow !== 0) return "没有前置节点的节点须位于首层";
      if (nodes.some((other) => other !== node && other.treeRow === node.treeRow && Math.abs(other.treeX - node.treeX) < layout.minGap - 1e-6))
        return `${node.id} 与同层节点过近`;
      for (const requirement of requirements(node)) {
        const parent = nodes.find((other) => other.id === requirement.id);
        if (!parent || parent.treeRow >= node.treeRow) return `${node.id} 的前置节点须在更早层`;
      }
    }
    return "";
  }
  function create(data, track, parent) {
    let serial = 1;
    while (data.upgrades.some((node) => node.id === `${track}_upgrade_${serial}`)) serial++;
    let row = parent ? parent.treeRow + 1 : 0;
    let x;
    while (row <= layout.maxRow) {
      const siblings = data.upgrades.filter((node) => node.track === track && node.treeRow === row);
      x = [.5, .27, .73, layout.minX, layout.maxX].find((value) => siblings.every((node) => Math.abs(node.treeX - value) >= layout.minGap - 1e-6));
      if (x !== undefined) break;
      if (!parent) return {error: "首层已满，请先调整节点位置或添加子节点"};
      row++;
    }
    if (row > layout.maxRow) return {error: "已达到最大树层级"};
    return {node: {
      id: `${track}_upgrade_${serial}`, name: "新升级", description: "配置升级效果", currency: "coins", costs: [10],
      effects: [{attribute: track === "magic" ? "mana_flow" : track === "defense" ? "wall_health" : "arrow_damage", operation: "add", values: [0, 1]}],
      track, treeRow: row, treeX: x,
      prerequisites: parent ? [{id: parent.id, level: 1}] : [], unlockStage: 0,
    }};
  }
  return {normalize, validate, validateLayout, create, requirements, layout};
})();
if (typeof module !== "undefined") module.exports = UpgradeEditor;
