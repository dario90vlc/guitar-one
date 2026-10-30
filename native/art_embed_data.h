// art_embed_data.h - generado desde amp_frontales_data.h (no editar a mano)
// Los 26 PNG que carga la app, con el ID de recurso RCDATA que les pone
// _build.ps1. La version PORTABLE los lleva dentro del .exe; si la carpeta
// amps_frontales existe al lado, manda el fichero y esto no se usa.
#pragma once
struct ArtResEntry { const WCHAR* rel; int id; };
static const ArtResEntry kArtRes[] = {
    { L"fondos\\01_american_spark_fondo.png", 1001 },
    { L"filmstrips\\01_american_spark_filmstrip@2x.png", 1002 },
    { L"fondos\\02_class_a_chime_fondo.png", 1003 },
    { L"filmstrips\\02_class_a_chime_filmstrip@2x.png", 1004 },
    { L"fondos\\03_tweed_deluxe_fondo.png", 1005 },
    { L"filmstrips\\03_tweed_deluxe_filmstrip@2x.png", 1006 },
    { L"fondos\\04_spring_clean_120_fondo.png", 1007 },
    { L"filmstrips\\04_spring_clean_120_filmstrip@2x.png", 1008 },
    { L"fondos\\05_panel_800_fondo.png", 1009 },
    { L"filmstrips\\05_panel_800_filmstrip@2x.png", 1010 },
    { L"fondos\\06_plexi_glory_fondo.png", 1011 },
    { L"filmstrips\\06_plexi_glory_filmstrip@2x.png", 1012 },
    { L"fondos\\07_blooming_orange_fondo.png", 1013 },
    { L"filmstrips\\07_blooming_orange_filmstrip@2x.png", 1014 },
    { L"fondos\\08_blues_route_fondo.png", 1015 },
    { L"filmstrips\\08_blues_route_filmstrip@2x.png", 1016 },
    { L"fondos\\09_studio_hot_fondo.png", 1017 },
    { L"filmstrips\\09_studio_hot_filmstrip@2x.png", 1018 },
    { L"fondos\\10_recto_cali_fondo.png", 1019 },
    { L"filmstrips\\10_recto_cali_filmstrip@2x.png", 1020 },
    { L"fondos\\11_german_v4_fondo.png", 1021 },
    { L"filmstrips\\11_german_v4_filmstrip@2x.png", 1022 },
    { L"fondos\\12_stage_crush_fondo.png", 1023 },
    { L"filmstrips\\12_stage_crush_filmstrip@2x.png", 1024 },
    { L"fondos\\13_profile_fondo.png", 1025 },
    { L"filmstrips\\13_profile_filmstrip@2x.png", 1026 },
};
static const int kArtResN = 26;
