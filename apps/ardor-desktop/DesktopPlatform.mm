#include "DesktopPlatform.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>

namespace ardor {

AudioInputPermission requestAudioInputPermission()
{
  static std::atomic<bool> requested{false};
  const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusAuthorized) return AudioInputPermission::Available;
  if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) return AudioInputPermission::Denied;
  if (!requested.exchange(true)) {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) { (void)granted; }];
  }
  return AudioInputPermission::Pending;
}

} // namespace ardor
