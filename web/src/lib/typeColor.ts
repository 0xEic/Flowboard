// SPDX-License-Identifier: MIT

// Stable color for a port's type tag.
// FNV-1a 32-bit hash → HSL hue. Saturation/lightness fixed to look
// reasonable against the dark slate-900 canvas background.
export function colorForTypeTag(tag: string): string {
  let h = 2166136261; // FNV offset basis
  for (let i = 0; i < tag.length; i++) {
    h ^= tag.charCodeAt(i);
    h = Math.imul(h, 16777619) >>> 0;
  }
  return `hsl(${h % 360} 60% 55%)`;
}
