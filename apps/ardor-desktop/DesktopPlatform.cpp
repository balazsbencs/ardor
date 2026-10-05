#include "DesktopPlatform.h"

namespace ardor {

AudioInputPermission requestAudioInputPermission()
{
  // Other platforms report capture failures through the audio backend.
  return AudioInputPermission::Available;
}

} // namespace ardor
