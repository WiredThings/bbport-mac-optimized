// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Metal {
class Scheduler;
class SceneTargets;
enum class BlurCopy { Blit, Scale, Identity, Missing, Type, Samples, Format, Alias, Usage };
// Exact known fullscreen blur shader replacement: source texture 0, same game color format.
// Viewport/scissor registers can exceed the attachment; they do not determine image-copy size.
BlurCopy CopyWithoutMotionBlur(Scheduler&, SceneTargets&, const void* source, const void* target);
const char* BlurCopyName(BlurCopy);
inline bool BlurCopied(BlurCopy r) { return r <= BlurCopy::Identity; }
}
