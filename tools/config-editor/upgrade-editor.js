const UpgradeEditor = (() => {
  function normalize(data) {
    for (const node of data.upgrades) {
      if (!node.effects && Array.isArray(node.values)) node.effects = [{attribute: node.id, operation: "set", values: node.values}];
      delete node.values;
    }
    return data;
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
      if (track !== "defense") {
        if (!Number.isInteger(node.treeRow) || node.treeRow < 0 || node.treeRow > 16 || !Number.isFinite(node.treeX) || node.treeX < .12 || node.treeX > .88)
          return `${node.id} 的树位置无效`;
        if (!node.requires && node.treeRow !== 0) return "没有前置节点的节点须位于首层";
        if (data.upgrades.some((other) => other !== node && other.track === track && other.treeRow === node.treeRow && Math.abs(other.treeX - node.treeX) < .18 - 1e-6))
          return `${node.id} 与同层节点过近`;
      }
      if (node.requires) {
        const parent = data.upgrades.find((item) => item.id === node.requires);
        if (!parent || parent === node || !Number.isInteger(node.requiredLevel) || node.requiredLevel < 1 || node.requiredLevel > parent.costs.length ||
            (track !== "defense" && (parent.track !== track || parent.treeRow >= node.treeRow))) return `${node.id} 的前置节点或等级无效`;
        const visited = new Set([node.id]);
        let cursor = parent;
        while (cursor) {
          if (visited.has(cursor.id)) return "前置关系存在循环";
          visited.add(cursor.id);
          cursor = data.upgrades.find((item) => item.id === cursor.requires);
        }
      } else if ((node.requiredLevel ?? 0) !== 0) return "无前置节点时，前置等级须为 0";
    }
    return "";
  }
  function create(data, track, parent) {
    let serial = 1;
    while (data.upgrades.some((node) => node.id === `${track}_upgrade_${serial}`)) serial++;
    let row = track === "defense" ? -1 : parent ? parent.treeRow + 1 : 0;
    let x;
    while (row <= 16) {
      const siblings = data.upgrades.filter((node) => node.track === track && node.treeRow === row);
      x = [.5, .27, .73, .12, .88].find((value) => siblings.every((node) => Math.abs(node.treeX - value) >= .18 - 1e-6));
      if (track === "defense" || x !== undefined) break;
      if (!parent) return {error: "首层已满，请先调整节点位置或添加子节点"};
      row++;
    }
    if (row > 16) return {error: "已达到最大树层级"};
    return {node: {
      id: `${track}_upgrade_${serial}`, name: "新升级", description: "配置升级效果", currency: "coins", costs: [10],
      effects: [{attribute: track === "magic" ? "mana_flow" : track === "defense" ? "wall_health" : "arrow_damage", operation: "add", values: [0, 1]}],
      ...(track === "defense" ? {} : {track, treeRow: row, treeX: x}),
      requires: parent?.id || "", requiredLevel: parent ? 1 : 0, unlockStage: 0,
    }};
  }
  return {normalize, validate, create};
})();
if (typeof module !== "undefined") module.exports = UpgradeEditor;
