#include "rensga/core/audio.h"

#include <string.h>

#include "rensga/core/log.h"
#include "rensga/core/math.h"

namespace rensga {

namespace {
constexpr i32 kUnity = 32768;  // Q15 1.0

i32 toQ15(Scalar v, Scalar maxValue) {
  if (!(v > 0.0f)) return 0;  // also catches NaN
  if (v > maxValue) v = maxValue;
  return static_cast<i32>(v * 32768.0f + 0.5f);
}

Scalar clampPan(Scalar p) { return p < -1.0f ? -1.0f : (p > 1.0f ? 1.0f : p); }

u32 le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
u32 le16(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8); }

constexpr u32 kUnknownSize = 0xFFFFFFFFu;
}  // namespace

AudioMixer::AudioMixer() {}

bool AudioMixer::init(u32 sampleRate, IFileSystem* fs) {
  if (sampleRate != 22050 && sampleRate != 44100) {
    RLOG_ERROR("audio", "unsupported sample rate %u (use 22050 or 44100)", static_cast<unsigned>(sampleRate));
    return false;
  }
  rate_ = sampleRate;
  fs_ = fs;
  for (u32 i = 0; i < kMaxVoices; ++i) voices_[i] = Voice();
  music_.active = false;
  music_.handle = -1;
  serial_ = 0;
  stolen_ = 0;
  limit_ = kMaxVoices;
  stats_ = MixStats();
  master_ = kUnity;
  return true;
}

void AudioMixer::shutdown() {
  stopMusic();
  stopAll();
}

u32 AudioMixer::voiceId(u32 index) const { return (u32(voices_[index].generation) << 8) | (index + 1); }

AudioMixer::Voice* AudioMixer::lookup(u32 id) {
  u32 idx = (id & 0xFF);
  if (idx == 0 || idx > kMaxVoices) return nullptr;
  Voice& v = voices_[idx - 1];
  return (v.active && voiceId(idx - 1) == id) ? &v : nullptr;
}
const AudioMixer::Voice* AudioMixer::lookup(u32 id) const { return const_cast<AudioMixer*>(this)->lookup(id); }

void AudioMixer::computeGains(Voice& v) {
  // Constant-power pan; the trig runs once per change, never per sample.
  const Scalar angle = (clampPan(v.pan) + 1.0f) * (Mathf::PI * 0.25f);
  v.gainL = toQ15(v.volume * Mathf::Cos(angle), 2.0f);
  v.gainR = toQ15(v.volume * Mathf::Sin(angle), 2.0f);
}

u32 AudioMixer::play(const AudioClip* clip, Scalar volume, Scalar pan, bool loop, Scalar pitch, u8 priority) {
  if (!clip || !clip->samples || clip->frames == 0 || rate_ == 0 || (clip->channels != 1 && clip->channels != 2)) return 0;
  u32 slot = kMaxVoices;
  for (u32 i = 0; i < limit_; ++i)
    if (!voices_[i].active) { slot = i; break; }
  if (slot == kMaxVoices) {
    // Steal the least important voice (highest priority number), the oldest among equals. Never a more important one.
    u32 best = 0xFFFFFFFFu;
    int bestPrio = -1;
    for (u32 i = 0; i < limit_; ++i) {
      const Voice& o = voices_[i];
      const bool stealable = o.priority > priority || (o.priority == priority && !o.loop);
      if (!stealable) continue;
      if (int(o.priority) > bestPrio || (int(o.priority) == bestPrio && o.serial < best)) {
        bestPrio = o.priority;
        best = o.serial;
        slot = i;
      }
    }
    if (slot == kMaxVoices) return 0;
    ++stolen_;
  }
  Voice& v = voices_[slot];
  const u16 gen = static_cast<u16>(v.generation + 1);
  v = Voice();
  v.generation = gen;
  v.clip = clip;
  v.active = true;
  v.loop = loop;
  v.priority = priority;
  v.serial = ++serial_;
  v.volume = volume;
  v.pan = pan;
  setPitch(voiceId(slot), pitch);
  computeGains(v);
  return voiceId(slot);
}

void AudioMixer::stop(u32 id) {
  if (Voice* v = lookup(id)) v->active = false;
}
void AudioMixer::stopAll() {
  for (u32 i = 0; i < kMaxVoices; ++i) voices_[i].active = false;
}
bool AudioMixer::isPlaying(u32 id) const { return lookup(id) != nullptr; }
void AudioMixer::setVolume(u32 id, Scalar volume) {
  if (Voice* v = lookup(id)) { v->volume = volume; computeGains(*v); }
}
void AudioMixer::setPan(u32 id, Scalar pan) {
  if (Voice* v = lookup(id)) { v->pan = pan; computeGains(*v); }
}
void AudioMixer::setPitch(u32 id, Scalar pitch) {
  Voice* v = lookup(id);
  if (!v || !v->clip) return;
  if (!(pitch >= 0.25f)) pitch = 0.25f;
  if (pitch > 4.0f) pitch = 4.0f;
  v->pitch = pitch;
  const u64 base = (u64(v->clip->sampleRate) << 16) / rate_;
  v->step = static_cast<u32>(static_cast<Scalar>(base) * pitch);
  if (v->step == 0) v->step = 1;
}
u32 AudioMixer::activeVoices() const {
  u32 n = 0;
  for (u32 i = 0; i < kMaxVoices; ++i) n += voices_[i].active ? 1 : 0;
  return n;
}
void AudioMixer::setVoiceLimit(u32 voices) {
  if (voices < 1) voices = 1;
  if (voices > kMaxVoices) voices = kMaxVoices;
  for (u32 i = voices; i < kMaxVoices; ++i) voices_[i].active = false;
  limit_ = voices;
}
void AudioMixer::setMasterVolume(Scalar volume) { master_ = toQ15(volume, 2.0f); }

// ---- music ---------------------------------------------------------------------------------------

void AudioMixer::stopMusic() {
  if (music_.handle >= 0 && music_.fs) music_.fs->closeStream(music_.handle);
  music_.handle = -1;
  music_.active = false;
}

void AudioMixer::setMusicVolume(Scalar volume) {
  music_.volume = volume;
  music_.gainL = music_.gainR = toQ15(volume, 2.0f);
}

bool AudioMixer::refillMusic(u32 which) {
  Music& m = music_;
  const u32 bpf = 2 * m.channels;
  u32 got = 0;
  bool sought = false;
  while (got < kMusicChunkFrames && !m.eof) {
    u32 bytes = (kMusicChunkFrames - got) * bpf;
    if (bytes > m.dataLeft) bytes = m.dataLeft;
    u32 n = 0;
    if (bytes > 0) {
      i16* dst = m.buf[which] + got * 2;
      n = m.fs->readStream(m.handle, dst, bytes);
      n -= n % bpf;
      if (n > 0) {
        const u32 frames = n / bpf;
        if (m.channels == 1)  // expand mono to stereo in place, backwards
          for (u32 i = frames; i-- > 0;) { dst[i * 2 + 1] = dst[i]; dst[i * 2] = dst[i]; }
        got += frames;
        if (m.dataLeft != kUnknownSize) m.dataLeft -= n;
        sought = false;
        continue;
      }
    }
    // End of the data (or a short read at the end of the file).
    if (m.loop && !sought && m.dataBytes > 0 && m.fs->seekStream(m.handle, m.dataOffset)) {
      m.dataLeft = m.dataBytes;
      sought = true;
    } else {
      m.eof = true;
    }
  }
  m.frames[which] = got;
  m.filled[which] = got > 0;
  return got > 0;
}

bool AudioMixer::playMusic(const char* path, Scalar volume, bool loop) {
  stopMusic();
  if (!fs_ || rate_ == 0) return false;
  Music& m = music_;
  const i32 h = fs_->openStream(path);
  if (h < 0) {
    RLOG_WARN("audio", "music not found: %s", path);
    return false;
  }
  m.fs = fs_;
  m.handle = h;
  m.loop = loop;
  m.eof = false;
  m.srcRate = rate_;
  m.channels = 2;
  m.dataOffset = 0;
  m.dataBytes = kUnknownSize;
  u8 head[16];
  if (fs_->readStream(h, head, 12) == 12 && memcmp(head, "RIFF", 4) == 0 && memcmp(head + 8, "WAVE", 4) == 0) {
    u32 off = 12;
    bool haveFmt = false, haveData = false;
    for (int guard = 0; guard < 32 && !haveData; ++guard) {
      u8 ch[8];
      if (!fs_->seekStream(h, off) || fs_->readStream(h, ch, 8) != 8) break;
      const u32 size = le32(ch + 4);
      if (memcmp(ch, "fmt ", 4) == 0) {
        u8 f[16];
        if (size < 16 || fs_->readStream(h, f, 16) != 16) break;
        const u32 tag = le16(f), channels = le16(f + 2), bits = le16(f + 14);
        if (tag != 1 || bits != 16 || (channels != 1 && channels != 2)) {
          RLOG_ERROR("audio", "%s: only PCM16 mono/stereo WAV is supported", path);
          break;
        }
        m.channels = channels;
        m.srcRate = le32(f + 4);
        haveFmt = true;
      } else if (memcmp(ch, "data", 4) == 0) {
        m.dataOffset = off + 8;
        m.dataBytes = (size == 0) ? kUnknownSize : size;
        haveData = haveFmt;
        break;
      }
      off += 8 + size + (size & 1);
    }
    if (!haveData) {
      RLOG_ERROR("audio", "%s: bad WAV file", path);
      stopMusic();
      return false;
    }
  }
  if (!fs_->seekStream(h, m.dataOffset)) {
    stopMusic();
    return false;
  }
  m.dataLeft = m.dataBytes;
  if (m.srcRate == 0) { stopMusic(); return false; }
  m.step = static_cast<u32>((u64(m.srcRate) << 16) / rate_);
  m.frac = 0;
  m.cur = 0;
  m.pos = 0;
  m.underruns = 0;
  setMusicVolume(volume);
  m.filled[0] = m.filled[1] = false;
  m.frames[0] = m.frames[1] = 0;
  if (!refillMusic(0)) { stopMusic(); return false; }
  if (!m.eof) refillMusic(1);
  m.active = true;
  return true;
}

void AudioMixer::update() {
  Music& m = music_;
  if (!m.active || m.eof) return;
  for (u32 i = 0; i < 2 && !m.eof; ++i) {
    const u32 w = (m.cur + i) & 1;  // refill the buffer that plays next first
    if (!m.filled[w]) refillMusic(w);
  }
}

// ---- mixing --------------------------------------------------------------------------------------

void AudioMixer::mixVoice(Voice& v, i32* acc, u32 frames) {
  const AudioClip& c = *v.clip;
  u32 start = 0, end = c.frames;
  bool loop = v.loop;
  if (loop) {
    if (c.loopStart >= 0 && u32(c.loopStart) < c.frames) start = u32(c.loopStart);
    if (c.loopEnd > 0 && u32(c.loopEnd) <= c.frames) end = u32(c.loopEnd);
    if (end <= start) { start = 0; end = c.frames; }
  }
  const u32 len = end - start;
  const bool stereo = c.channels == 2;
  for (u32 i = 0; i < frames; ++i) {
    if (v.index >= end) {  // only reachable on a non-looping end (loops are wrapped below)
      v.active = false;
      return;
    }
    u32 n = v.index + 1;
    if (n >= end) n = loop ? start : v.index;
    const i32 f = static_cast<i32>(v.frac >> 1);  // 15 bits
    i32 l, r;
    if (stereo) {
      const i16* a = c.samples + v.index * 2;
      const i16* b = c.samples + n * 2;
      l = a[0] + (((b[0] - a[0]) * f) >> 15);
      r = a[1] + (((b[1] - a[1]) * f) >> 15);
    } else {
      const i32 a = c.samples[v.index], b = c.samples[n];
      l = r = a + (((b - a) * f) >> 15);
    }
    acc[i * 2] += (l * v.gainL) >> 15;
    acc[i * 2 + 1] += (r * v.gainR) >> 15;
    const u64 p = ((u64(v.index) << 16) | v.frac) + v.step;
    v.index = static_cast<u32>(p >> 16);
    v.frac = static_cast<u32>(p & 0xFFFF);
    if (loop) {
      while (v.index >= end) v.index -= len;
    }
  }
}

void AudioMixer::mixMusic(i32* acc, u32 frames) {
  Music& m = music_;
  for (u32 i = 0; i < frames; ++i) {
    if (m.pos >= m.frames[m.cur]) {
      m.filled[m.cur] = false;
      m.cur ^= 1;
      m.pos = 0;
      if (!m.filled[m.cur]) {
        if (m.eof) { stopMusic(); return; }
        ++m.underruns;  // main thread has not refilled yet: silence until it does
        return;
      }
    }
    const i16* s = m.buf[m.cur] + m.pos * 2;
    acc[i * 2] += (s[0] * m.gainL) >> 15;
    acc[i * 2 + 1] += (s[1] * m.gainR) >> 15;
    const u32 sum = m.frac + m.step;
    m.frac = sum & 0xFFFF;
    m.pos += sum >> 16;
  }
}

void AudioMixer::mix(i16* out, u32 frames) {
  const u64 t0 = clock_ ? clock_() : 0;
  i32 acc[kMixChunk * 2];
  u32 done = 0;
  while (done < frames) {
    const u32 n = (frames - done) < kMixChunk ? (frames - done) : kMixChunk;
    memset(acc, 0, sizeof(i32) * n * 2);
    for (u32 i = 0; i < kMaxVoices; ++i)
      if (voices_[i].active) mixVoice(voices_[i], acc, n);
    if (music_.active) mixMusic(acc, n);
    for (u32 i = 0; i < n * 2; ++i) {
      i64 s = (i64(acc[i]) * master_) >> 15;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      out[(done * 2) + i] = static_cast<i16>(s);
    }
    done += n;
  }
  if (clock_) {
    const u64 us = clock_() - t0;
    ++stats_.blocks;
    stats_.frames += frames;
    stats_.totalUs += us;
    if (us > stats_.maxUs) stats_.maxUs = us;
  }
}

}  // namespace rensga
