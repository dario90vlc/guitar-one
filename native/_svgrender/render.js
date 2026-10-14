// Renderiza los SVG de amps_frontales/out a PNG (misma salida que el viejo cairosvg)
// node render.js   (cwd: native/_svgrender)
const fs = require("fs");
const path = require("path");
const { Resvg } = require("@resvg/resvg-js");

const OUT = path.join(__dirname, "..", "amps_frontales", "out");

function render(svgPath, pngPath, width) {
  const svg = fs.readFileSync(svgPath);
  const r = new Resvg(svg, {
    fitTo: { mode: "width", value: width },
    font: { loadSystemFonts: true, fontDirs: [path.join(__dirname, "fonts"), "C:\\Windows\\Fonts"], defaultFontFamily: "Arial" },
  });
  fs.writeFileSync(pngPath, r.render().asPng());
}

const dirs = ["preview", "fondos", "perillas", "filmstrips"];
const list = [];
for (const d of dirs)
  for (const f of fs.readdirSync(path.join(OUT, d)))
    if (f.endsWith(".svg")) list.push([d, f]);

let n = 0;
for (const [d, f] of list) {
  const base = f.replace(/\.svg$/, "");
  const svgP = path.join(OUT, d, f);
  if (d === "preview") {
    render(svgP, path.join(OUT, d, base + ".png"), 1200);
  } else if (d === "fondos") {
    render(svgP, path.join(OUT, d, base + ".png"), 1200);
    render(svgP, path.join(OUT, d, base + "@2x.png"), 2400);
  } else if (d === "perillas") {
    render(svgP, path.join(OUT, d, base + ".png"), 112);
    render(svgP, path.join(OUT, d, base + "@2x.png"), 224);
  } else if (d === "filmstrips") {
    render(svgP, path.join(OUT, d, base + ".png"), 112);
    render(svgP, path.join(OUT, d, base + "@2x.png"), 224);
  }
  n++;
  console.log("ok", d + "/" + f);
}
console.log("RENDER_OK", n, "svgs");
