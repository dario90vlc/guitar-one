// amp_frontales_data.h - generado desde amp_frontales_v2 (no editar a mano)
#pragma once
// nall/ax/ay = todos los huecos impresos del frontal (coords del fondo 1200x540).
// map[k] = indice del hueco que aloja el param k (kAmpKnobNames); -1 = sin hueco.
// strip = filmstrip@2x (101 fotogramas, 224px; frame = round(valor)).
struct AmpArtInfo { const WCHAR* fondo; const WCHAR* strip; int nall; float ax[10]; float ay[10]; int map[6]; };
static const AmpArtInfo kAmpArt[13] = {
  { L"fondos\\01_american_spark_fondo.png", L"filmstrips\\01_american_spark_filmstrip@2x.png", 7, { 200.0f,333.3f,466.7f,600.0f,733.3f,866.7f,1000.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f }, { 4,3,2,1,0,5 } },   // SPARKLE TWIN
  { L"fondos\\02_class_a_chime_fondo.png", L"filmstrips\\02_class_a_chime_filmstrip@2x.png", 6, { 200.0f,360.0f,520.0f,680.0f,840.0f,1000.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f }, { 3,2,5,1,0,4 } },   // CHIME 30
  { L"fondos\\03_tweed_deluxe_fondo.png", L"filmstrips\\03_tweed_deluxe_filmstrip@2x.png", 3, { 380.0f,600.0f,820.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f }, { 1,-1,-1,-1,0,2 } },   // AMBER TWEED
  { L"fondos\\04_spring_clean_120_fondo.png", L"filmstrips\\04_spring_clean_120_filmstrip@2x.png", 8, { 200.0f,314.3f,428.6f,542.9f,657.1f,771.4f,885.7f,1000.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f }, { 4,1,2,3,0,5 } },   // CHORUS 120
  { L"fondos\\05_panel_800_fondo.png", L"filmstrips\\05_panel_800_filmstrip@2x.png", 6, { 200.0f,360.0f,520.0f,680.0f,840.0f,1000.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f }, { 5,1,2,3,4,0 } },   // MARAUDER 800
  { L"fondos\\06_plexi_glory_fondo.png", L"filmstrips\\06_plexi_glory_filmstrip@2x.png", 6, { 200.0f,360.0f,520.0f,680.0f,840.0f,1000.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f }, { 5,1,2,3,4,0 } },   // LEAD 1959
  { L"fondos\\07_blooming_orange_fondo.png", L"filmstrips\\07_blooming_orange_filmstrip@2x.png", 6, { 200.0f,360.0f,520.0f,680.0f,840.0f,1000.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f }, { 0,4,3,2,1,5 } },   // TANGERINE 120
  { L"fondos\\08_blues_route_fondo.png", L"filmstrips\\08_blues_route_filmstrip@2x.png", 7, { 200.0f,333.3f,466.7f,600.0f,733.3f,866.7f,1000.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f }, { 4,3,2,1,0,5 } },   // LONDON BLUES
  { L"fondos\\09_studio_hot_fondo.png", L"filmstrips\\09_studio_hot_filmstrip@2x.png", 6, { 200.0f,360.0f,520.0f,680.0f,840.0f,1000.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f }, { 4,1,2,3,0,5 } },   // SESSION HOT
  { L"fondos\\10_recto_cali_fondo.png", L"filmstrips\\10_recto_cali_filmstrip@2x.png", 7, { 200.0f,333.3f,466.7f,600.0f,733.3f,866.7f,1000.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f }, { 0,1,2,3,5,4 } },   // DUO RECTO
  { L"fondos\\11_german_v4_fondo.png", L"filmstrips\\11_german_v4_filmstrip@2x.png", 8, { 200.0f,314.3f,428.6f,542.9f,657.1f,771.4f,885.7f,1000.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f }, { 0,1,2,3,6,4 } },   // KRAFT V4
  { L"fondos\\12_stage_crush_fondo.png", L"filmstrips\\12_stage_crush_filmstrip@2x.png", 7, { 200.0f,333.3f,466.7f,600.0f,733.3f,866.7f,1000.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f }, { 0,2,3,4,1,5 } },   // HAVOC 100
  { L"fondos\\13_profile_fondo.png", L"filmstrips\\13_profile_filmstrip@2x.png", 5, { 672.0f,748.0f,824.0f,900.0f,976.0f,0.0f,0.0f,0.0f,0.0f,0.0f }, { 164.0f,164.0f,164.0f,164.0f,164.0f,0.0f,0.0f,0.0f,0.0f,0.0f }, { 0,1,2,3,4,-1 } },   // PERFIL
};
