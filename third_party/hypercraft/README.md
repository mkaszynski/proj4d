# Hypercraft terrain/content/texture snapshot

This directory contains the minimal native C++ terrain, content, and
deterministic 16x16 texture-atlas snapshot used by Proj4D. It was taken from
Hypercraft commit `d830abc`. Only the renderer-independent atlas generator and
its public source tiles are retained; the client, general renderer, browser,
networking, multiplayer, lighting, mobs, saves, credentials, and certificates
remain excluded.

Keeping this source in-tree makes the public Proj4D build reproducible and
preserves byte-for-byte terrain parity without requiring access to another
repository. The source is distributed under Proj4D's repository license.
