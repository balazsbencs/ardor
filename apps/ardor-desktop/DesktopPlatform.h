#pragma once

namespace ardor {

enum class AudioInputPermission { Available, Pending, Denied };
AudioInputPermission requestAudioInputPermission();

} // namespace ardor
