#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace catalog {

struct CropRect {
  float x = 0.f, y = 0.f, w = 1.f, h = 1.f;
  float angleDeg = 0.f;  // straighten [-45, 45]
};

struct EditSettings {
  float    exposure    = 0.f;  // EV [-3, 3]
  float    temperature = 0.f;  // relative [-100, 100]
  float    contrast    = 0.f;  // [-100, 100]
  float    saturation  = 0.f;  // [-100, 100]
  CropRect crop;

  bool isIdentity() const {
    return exposure == 0.f && temperature == 0.f &&
           contrast == 0.f && saturation  == 0.f &&
           crop.x == 0.f && crop.y == 0.f &&
           crop.w == 1.f && crop.h == 1.f && crop.angleDeg == 0.f;
  }

  std::string toJson() const {
    nlohmann::json j;
    j["exposure"]    = exposure;
    j["temperature"] = temperature;
    j["contrast"]    = contrast;
    j["saturation"]  = saturation;
    j["crop"] = {{"x", crop.x}, {"y", crop.y}, {"w", crop.w},
                 {"h", crop.h}, {"angle", crop.angleDeg}};
    return j.dump();
  }

  static EditSettings fromJson(const std::string& s) {
    EditSettings e;
    try {
      auto j = nlohmann::json::parse(s);
      e.exposure    = j.value("exposure",    0.f);
      e.temperature = j.value("temperature", 0.f);
      e.contrast    = j.value("contrast",    0.f);
      e.saturation  = j.value("saturation",  0.f);
      if (j.contains("crop")) {
        const auto& c = j["crop"];
        e.crop.x        = c.value("x",     0.f);
        e.crop.y        = c.value("y",     0.f);
        e.crop.w        = c.value("w",     1.f);
        e.crop.h        = c.value("h",     1.f);
        e.crop.angleDeg = c.value("angle", 0.f);
      }
    } catch (...) {}
    return e;
  }
};

}  // namespace catalog
