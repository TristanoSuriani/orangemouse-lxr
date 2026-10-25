// Shim: Orange Mouse has no MTS-ESP microtuning; the master is never present.
#pragma once
struct MTSClient;
static inline bool MTS_HasMaster(const MTSClient*) { return false; }
static inline double MTS_NoteToFrequency(const MTSClient*, char, char) { return 0.0; }
