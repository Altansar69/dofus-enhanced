#pragma once
#include "runtime.hpp"

namespace soul {
enum class CaptureState { Unknown, Missing, Active };
void InstallCapture();
void ResetCapture();
CaptureState ReadCapture(managed::Obj fight, managed::Obj cached);
int64_t CaptureParticipant(managed::Obj fight, managed::Obj cached);
}
