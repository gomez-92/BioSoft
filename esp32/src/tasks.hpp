#pragma once

namespace Tasks {
  constexpr const char* UpdateScreens         = "UPDATE_SCREENS";
  constexpr const char* ReSendStart           = "RESEND_START";
  constexpr const char* ReSendStop            = "RESEND_STOP";
  constexpr const char* ReSendReset           = "RESEND_RESET";
  constexpr const char* UpdateProgress        = "UPDATE_PROGRESS";
  // Una tarea por grupo PERIODICO de telemetria (ver topics.hpp). Los
  // grupos por evento -- targets, alerts, result -- no tienen tarea: se
  // publican cuando ocurren.
  constexpr const char* PublishMeasures       = "PUBLISH_MEASURES";
  constexpr const char* PublishCoils          = "PUBLISH_COILS";
  constexpr const char* PublishStatus         = "PUBLISH_STATUS";
  constexpr const char* ReSendConfig          = "RESEND_CONFIG";
};