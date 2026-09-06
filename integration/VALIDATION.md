# Local validation — 2026-09-06

Upstream: wowdev/noggit3 at 59e58add868608339fe292970196985d4ce050ff.

- Built the modified editor with GCC 13 / Qt 5.15 on Ubuntu 24.04 in rootless Podman.
- Native executable loads against Fedora 44's Qt5 libraries; bundled bzip2 supplies the
  Ubuntu SONAME. Native Wayland startup succeeded. Containers are not needed to run it.
- Opened Northshire Valley from the supplied 3.3.5a client with its Reforged patches in
  a separate Xvfb/Mesa test session. Source client was mounted read-only during this test.
- Opened the live DBC-backed object picker, added decorative template 180033, moved it
  through the position editor, and copied/pasted it. JSON contained separate stable UUIDs.
- Saved an ADT while the server object was present. The saved tile contained no reference
  to doghouse.m2, confirming exclusion from the map's model list. Chunk reference writing
  uses the same filtered lists.
- Exported through the actual menu/dialog to SQL, including project-removal SQL.
- Closed and reopened the project over the original client maps with normal UID checking;
  both placement UUIDs, positions, rotations, phase masks and display IDs survived unchanged.
- Verified Ctrl+Shift+G opens the picker.
- Seven Python tests pass: axis/rotation conversion, invalid values, export ownership and
  transaction guards, duplicate identities, scoped removal, and dump parsing/escaping.
- Source whitespace check, launcher syntax, and desktop entry validation completed.

During testing, corrected upstream 64-bit DBC header initialization, Linux MPQ filename
case lookup, and returning the actual allocated model UID after collision resolution.
A test using disabled UID checking and a partly saved map produced upstream UID warnings;
the installation keeps normal UID checking enabled. Use Get max UID on first map entry.

Limitations: SQL was generated and reviewed, not imported into a running MySQL/game server.
Server persistence, collision, pathfinding and in-game model orientation still need live
server validation. Interactive templates, per-spawn scale/tilt, and custom WMO doodad sets
are outside this version. WMO support is implemented but the interactive placement smoke
test used an M2 object; individual Reforged assets may need further compatibility checks.
