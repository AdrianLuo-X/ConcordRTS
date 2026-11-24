#pragma once
#include <cstddef>
struct MapLightingProfile {
    double ambient = 1.0;
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
    double ground = 0.0;
    double level = 0.032;
};

struct MapLighting {
    bool sectionPresent = false;
    std::size_t authoredFieldCount = 0;
    MapLightingProfile base;
    MapLightingProfile ion;
    // FinalAlert's Yuri's Revenge scenario defaults for Psychic Dominator
    // lighting. Stock maps normally author these fields explicitly.
    MapLightingProfile dominator{1.5, 0.85, 0.2, 0.3, 0.0, 0.0};
    double dominatorAmbientChangeRate = 0.009;
};

