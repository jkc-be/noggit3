# Noggit / AzerothCore server handoff

This integration edits existing decorative (type 5), unit-scale gameobject templates.
It exports SQL; it does not connect to a database, start server software, or deploy anything.

## Client/editor machine

Launch **Noggit — Reforged + AzerothCore** from Applications, or run `noggit-reforged`.
The installed native Linux editor is built from https://github.com/wowdev/noggit3.
The client is `/home/janc/Games/WoW-Reforged`; editing output goes to
`/home/janc/Games/Noggit-Reforged/`. Client archives are read without modification.

Open a map (a Northshire bookmark is included). For a map's initial UID prompt,
choose **Get max UID**. Move the mouse over the desired ground location, then use
**Ctrl+Shift+G** (or **AzerothCore > Add server object at cursor**). Search by name, model path, or entry.
The picker combines the local template catalog with the client's display DBC and
only offers supported models that actually exist in the client.

Use Noggit's normal object controls to move, turn, copy/paste or delete the placement.
Server objects retain their layer during copy/paste. X/Z rotation must remain zero;
scale must remain one. WMO doodad sets must remain zero. The phase menu changes selected server objects' phase mask.
All phases are visible in the editor; the server applies phase visibility in game.

Server objects autosave every two seconds, and on normal editor exit, to
`server/map-<ID>.json`. They reload with the map. **Save server objects** saves explicitly.
This sidecar is the editable source of truth: back it up with the project. Do not copy
a project for independent use without assigning a new project UUID in each map JSON.

Terrain chunk copy/clear tools operate on map scenery and leave the server layer alone.
Normal map saves omit the server layer from ADT placements and references, preventing
an overlapping map copy. Static objects placed through the ordinary asset browser remain
map scenery. Never convert an existing static building by adding another at the same spot.
A client MPQ is unnecessary for supported server placements using assets all players have.

## Export

Use **AzerothCore > Export server objects to SQL**. It produces:

- The chosen `.sql`: the complete desired state for this map's project UUID.
- `*-remove-project.sql`: removes all spawns owned by this project (not an undo of only
  the latest export). Keep a database backup for exact rollback.

The tool validates finite world coordinates, upright rotation, unit scale and unique
placement keys. A model placed with unsupported tilt or scale stays in your editor
project, but SQL export fails until corrected.

The bundled `catalog.json` is an OFFLINE snapshot of the local AzerothCore base templates,
not a claim about the live database. To refresh it, obtain a `mysqldump` of the actual
server's gameobject_template table in the same 35-column schema. Ordinary mysqldump
INSERT statements with or without extended inserts are supported (no explicit column list).
Run `python3 catalog.py dump.sql catalog.json`, put the result in the installed
`integration/` directory, and restart Noggit. Custom client DBC changes also require
matching server DBC and extracted model data.

## Server machine only

1. Review the exported SQL and back up the world database, especially `gameobject` and
   `noggit_gameobject_link` if it already exists.
2. Stop worldserver using the server machine's normal procedure. The core maintains a
   cached GUID allocator and cached spawns; importing while it is running is unsupported.
3. Apply the SQL to the actual WORLD database with a MySQL 8 client. The import account
   needs CREATE ROUTINE plus the table privileges in the script. Do not use `--force`.
4. If it completes successfully, start worldserver normally. No C++ changes or rebuild
   are needed for supported placements.
5. Validate position, facing, visibility, collision and NPC routes with a normal player.

The SQL checks each referenced server template's display ID, type and size. It allocates
new spawn GUIDs with MySQL AUTO_INCREMENT, and stores stable project/object-to-GUID links
in `noggit_gameobject_link`. Reapplying updates existing GUIDs. Removing an object in the
editor deletes only that project's corresponding spawn on the next import.

The import is transactional and uses an advisory lock to serialize Noggit imports.
Ownership markers detect deleted or GM-rewritten managed spawns instead of silently
creating duplicates. Event, pool or addon attachments also cause an import rejection;
reconcile such customizations before using this exporter. Do not manage these same spawns
through GM commands or another editor. Keep both the JSON and ownership table backed up.
Apply the newest export: older exports intentionally restore their older desired layout.

The script does not create templates, edit DBCs, extract collision data, rebuild mmaps,
or manipulate character-database respawn records. Use non-despawning decorative templates.
The server must already have matching gameobject collision models. Placing a building
does not automatically make NPC navigation account for it; pathfinding changes are a
separate server-side integration task.

## Validation boundary

Native editor compilation and local client/editor tests are performed here. SQL generation
has automated coordinate/validation tests. The SQL import and live game behavior require
validation on the separate server; no server components were run on the client machine.
