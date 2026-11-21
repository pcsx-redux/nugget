# XM player

A player for FastTracker 2 modules (.xm) on the SPU. Convert a song with the `xmconv` tool from PCSX-Redux, which writes two files: the song (patterns, instruments and sample headers, kept in main RAM while playing) and the samples (SPU ADPCM, uploaded by `XM_Load` and freeable afterwards). `xmplayer.h` documents the song file format and the API.

Each XM channel plays on the SPU voice of the same number, so a song can use up to 24 channels; `xmconv` drops channels that are never used and refuses songs that still need more. Volume and panning envelopes, fadeout and auto-vibrato are run by the player once per tick, since the SPU's ADSR cannot express them. Ping-pong loops are unfolded into forward loops by the converter. Samples that would need more than the SPU's 4x pitch range, or that do not fit SPU RAM with `-f`, are resampled at conversion.

Multi-retrig (Rxy) is not implemented yet.

The demo plays `song.xmp` and `song.smp`, polled from timer 1 the same way as the modplayer demo.
