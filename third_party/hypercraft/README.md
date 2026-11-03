# Hypercraft terrain/content snapshot

This directory contains the minimal native C++ terrain and content snapshot
used by Proj4D's Low and High world modes. It was taken from Hypercraft commit
`d830abc` and deliberately excludes the client, renderer, browser, networking,
multiplayer, lighting, mobs, assets, saves, credentials, and certificates.

Keeping this source in-tree makes the public Proj4D build reproducible and
preserves byte-for-byte terrain parity without requiring access to another
repository. The source is distributed under Proj4D's repository license.
