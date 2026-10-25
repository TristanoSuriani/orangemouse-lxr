// Shim: Dexed's in-memory logger needs JUCE; logging is dropped.
#pragma once
struct MemoryLogger {
  static MemoryLogger* getInstance() { static MemoryLogger l; return &l; }
  template <typename... A> void logFormat(const char*, A...) {}
};
