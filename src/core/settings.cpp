#include "pulsescope/settings.hpp"

#include "pulsescope/json.hpp"
#include "pulsescope/validate.hpp"

namespace pulsescope {

Status ValidateSettings(const Settings& settings) {
  if (settings.theme != "dark" && settings.theme != "light") {
    return Status{ErrorCode::InvalidArgument, "theme must be dark or light"};
  }
  Status st = ValidateText(settings.traceDirectory, 1024);
  if (!st) return st;
  if (settings.timelinePixels < 32 || settings.timelinePixels > 8192) {
    return Status{ErrorCode::InvalidArgument, "timelinePixels out of range"};
  }
  if (settings.maxEvents < 1000 || settings.maxEvents > 50'000'000) {
    return Status{ErrorCode::InvalidArgument, "maxEvents out of range"};
  }
  if (!(settings.regressionFraction >= 0.0 && settings.regressionFraction <= 1.0)) {
    return Status{ErrorCode::InvalidArgument, "regressionFraction out of range"};
  }
  if (settings.liveIntervalMs < 100 || settings.liveIntervalMs > 10'000) {
    return Status{ErrorCode::InvalidArgument, "liveIntervalMs out of range"};
  }
  return Status::Ok();
}

std::string SettingsToJson(const Settings& settings) {
  Json root = Json::Object();
  root.Set("schema", Json::String("pulsescope.settings.v1"));
  root.Set("theme", Json::String(settings.theme));
  root.Set("traceDirectory", Json::String(settings.traceDirectory));
  root.Set("timelinePixels", Json::Uint(settings.timelinePixels));
  root.Set("maxEvents", Json::Uint(settings.maxEvents));
  root.Set("regressionFraction", Json::Number(settings.regressionFraction));
  root.Set("liveIntervalMs", Json::Uint(settings.liveIntervalMs));
  root.Set("kernelCswitch", Json::Bool(settings.kernelCswitch));
  root.Set("kernelReady", Json::Bool(settings.kernelReady));
  root.Set("kernelVirtualAlloc", Json::Bool(settings.kernelVirtualAlloc));
  return StringifyJson(root, 2);
}

Status SettingsFromJson(std::string_view json, Settings& out) {
  Json root;
  Status st = ParseJson(json, root);
  if (!st) return st;
  Settings settings;
  if (const Json* v = root.Find("theme"); v && v->AsString()) settings.theme = *v->AsString();
  if (const Json* v = root.Find("traceDirectory"); v && v->AsString()) settings.traceDirectory = *v->AsString();
  if (const Json* v = root.Find("timelinePixels"); v && v->AsUint()) settings.timelinePixels = static_cast<std::uint32_t>(*v->AsUint());
  if (const Json* v = root.Find("maxEvents"); v && v->AsUint()) settings.maxEvents = *v->AsUint();
  if (const Json* v = root.Find("regressionFraction"); v && v->AsNumber()) settings.regressionFraction = *v->AsNumber();
  if (const Json* v = root.Find("liveIntervalMs"); v && v->AsUint()) {
    settings.liveIntervalMs = static_cast<std::uint32_t>(*v->AsUint());
  }
  if (const Json* v = root.Find("kernelCswitch"); v && v->AsBool()) settings.kernelCswitch = *v->AsBool();
  if (const Json* v = root.Find("kernelReady"); v && v->AsBool()) settings.kernelReady = *v->AsBool();
  if (const Json* v = root.Find("kernelVirtualAlloc"); v && v->AsBool()) settings.kernelVirtualAlloc = *v->AsBool();
  st = ValidateSettings(settings);
  if (!st) return st;
  out = std::move(settings);
  return Status::Ok();
}

}  // namespace pulsescope
