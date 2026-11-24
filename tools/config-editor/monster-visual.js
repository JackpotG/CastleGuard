// Mirrors the static DrawNode shapes in MainScene::applyMonsterSkin.
const MonsterVisual = (() => {
  const bodyColors = {
    normal: "#b8382e", fast: "#e68026", ranged: "#9e4db8", shield: "#47859e",
    bomber: "#383333", flyer: "#e0e0d1", healer: "#6bc773", splitter: "#d95999",
    rager: "#8c1a14", ghost: "#cce0f2", summoner: "#592e80", aegis: "#f2d147",
    boss: "#732659",
  };

  function draw(canvas, type, baseBodyRadius, bodyScale, detail = false) {
    const context = canvas.getContext("2d");
    const width = canvas.width, height = canvas.height;
    context.clearRect(0, 0, width, height);
    const radius = baseBodyRadius * bodyScale;
    if (!bodyColors[type] || !Number.isFinite(radius) || radius <= 0) return;

    const extent = Math.max(radius * 2.8, radius * 2 + 7);
    const targetRadius = detail ? Math.min(18 + radius * .75, 54) :
      5 + radius * .25;
    const zoom = Math.min(targetRadius / radius, (height - (detail ? 28 : 8)) / extent);
    const paintedHeight = extent * zoom;
    const baseline = (height + paintedHeight) / 2;
    const k = radius / 11;
    const circle = (x, y, r, color) => {
      context.beginPath(); context.arc(x, y, r, 0, Math.PI * 2);
      context.fillStyle = color; context.fill();
    };
    const ring = (x, y, r, color, thickness) => {
      context.beginPath(); context.arc(x, y, r, 0, Math.PI * 2);
      context.strokeStyle = color; context.lineWidth = thickness; context.stroke();
    };
    const triangle = (points, color) => {
      context.beginPath(); context.moveTo(...points[0]);
      context.lineTo(...points[1]); context.lineTo(...points[2]); context.closePath();
      context.fillStyle = color; context.fill();
    };
    const line = (x1, y1, x2, y2, color, thickness) => {
      context.beginPath(); context.moveTo(x1, y1); context.lineTo(x2, y2);
      context.strokeStyle = color; context.lineWidth = thickness;
      context.lineCap = "round"; context.stroke();
    };
    const rect = (x, y, w, h, color) => {
      context.fillStyle = color; context.fillRect(x, y, w, h);
    };

    context.fillStyle = "#3f604b28";
    context.beginPath();
    context.ellipse(width / 2, baseline + 3, radius * zoom * .9, detail ? 5 : 2, 0, 0, Math.PI * 2);
    context.fill();
    context.save();
    context.translate(width / 2, baseline);
    context.scale(zoom, -zoom);
    circle(0, radius, radius, bodyColors[type]);
    if (type !== "ghost") {
      triangle([[-radius * .7, radius * 1.7], [-radius * .2, radius * 1.8],
        [-radius * .5, radius * 2.3]], "#801f1a");
      triangle([[radius * .7, radius * 1.7], [radius * .2, radius * 1.8],
        [radius * .5, radius * 2.3]], "#801f1a");
    }
    circle(-5 * k, radius + 3 * k, 2.5 * k, "#fff");
    circle(5 * k, radius + 3 * k, 2.5 * k, "#fff");

    if (type === "shield") {
      ring(0, radius, radius + 4, "#8ce6ff", 3);
    } else if (type === "bomber") {
      circle(0, radius, radius * .45, "#ff8c1a");
      line(0, radius * 1.8, 3 * k, radius * 2.5, "#998066", 2);
      circle(3 * k, radius * 2.6, 2.2 * k, "#ffbf26");
    } else if (type === "flyer") {
      const wing = "#b3c7d9";
      triangle([[-radius * .8, radius * 1.2], [-radius * 2, radius * 1.9],
        [-radius * .8, radius * .6]], wing);
      triangle([[radius * .8, radius * 1.2], [radius * 2, radius * 1.9],
        [radius * .8, radius * .6]], wing);
    } else if (type === "healer") {
      const cy = radius * 2.4;
      rect(-1.8 * k, cy - 6 * k, 3.6 * k, 12 * k, "#fff");
      rect(-6 * k, cy - 1.8 * k, 12 * k, 3.6 * k, "#fff");
    } else if (type === "splitter") {
      line(0, radius * .3, 0, radius * 1.8, "#ffffffd9", 2.5);
    } else if (type === "rager") {
      line(-8 * k, radius + 8 * k, -2 * k, radius + 5 * k, "#260d0d", 2.5);
      line(8 * k, radius + 8 * k, 2 * k, radius + 5 * k, "#260d0d", 2.5);
    } else if (type === "summoner") {
      line(radius * 1.2, 0, radius * 1.2, radius * 2.6, "#734d33", 3);
      circle(radius * 1.2, radius * 2.8, 2.5 * k, "#d98cff");
    } else if (type === "aegis") {
      ring(0, radius, radius + 5, "#ffd940", 4);
    } else if (type === "boss") {
      for (let i = -1; i <= 1; i++) {
        const x = i * radius * .55;
        triangle([[x - radius * .22, radius * 1.9], [x + radius * .22, radius * 1.9],
          [x, radius * 2.5]], "#ffcc33");
      }
    }
    context.restore();
  }

  return {draw};
})();
