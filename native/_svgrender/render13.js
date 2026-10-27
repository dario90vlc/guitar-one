// Render SOLO el frontal 13_profile (iterar el diseno sin tocar los otros 12).
// node render13.js   (cwd: native/_svgrender)
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
render(path.join(OUT, "preview", "13_profile.svg"), path.join(OUT, "preview", "13_profile.png"), 1200);
render(path.join(OUT, "fondos", "13_profile_fondo.svg"), path.join(OUT, "fondos", "13_profile_fondo.png"), 1200);
render(path.join(OUT, "fondos", "13_profile_fondo.svg"), path.join(OUT, "fondos", "13_profile_fondo@2x.png"), 2400);
render(path.join(OUT, "perillas", "13_profile_perilla.svg"), path.join(OUT, "perillas", "13_profile_perilla.png"), 112);
render(path.join(OUT, "perillas", "13_profile_perilla.svg"), path.join(OUT, "perillas", "13_profile_perilla@2x.png"), 224);
render(path.join(OUT, "filmstrips", "13_profile_filmstrip.svg"), path.join(OUT, "filmstrips", "13_profile_filmstrip.png"), 112);
render(path.join(OUT, "filmstrips", "13_profile_filmstrip.svg"), path.join(OUT, "filmstrips", "13_profile_filmstrip@2x.png"), 224);
console.log("RENDER13_OK");
