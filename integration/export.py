#!/usr/bin/env python3
"""Export a Noggit server layer to reviewed AzerothCore SQL; never connects to MySQL."""
import json
import math
from pathlib import Path
import sys
import uuid

ZEROPOINT = 1600.0 / 3.0 * 32


def identity(value):
    return str(uuid.UUID(value))


def number(value):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError('Non-finite coordinate')
    return format(value, '.9g')


def transform(position, rotation):
    if len(position) != 3 or len(rotation) != 3:
        raise ValueError('Position and rotation must have three components')
    x, y, z = (float(v) for v in position)
    rx, ry, rz = (float(v) for v in rotation)
    if not all(math.isfinite(v) for v in (x,y,z,rx,ry,rz)):
        raise ValueError('Non-finite transform')
    if abs(rx) > .001 or abs(rz) > .001:
        raise ValueError('Server objects must be upright: set X/Z rotation to zero before export')
    # Noggit model vertices: (x,z,-y), world position: (-Y,Z,-X).
    # Internal editor yaw is ADT yaw - 90 degrees; convert the local X axis to server space.
    angle = math.radians(ry - 90) % math.tau
    return ZEROPOINT-z, ZEROPOINT-x, y, angle, math.sin(angle/2), math.cos(angle/2)


def render(root):
    if root.get('version') != 1:
        raise ValueError('Unsupported project format')
    project = identity(root['project'])
    map_id = root['map']
    if type(map_id) is not int or not 0 <= map_id <= 65535:
        raise ValueError('Invalid map ID')
    rows, keys = [], set()
    for obj in root['objects']:
        key = identity(obj['key'])
        if key in keys:
            raise ValueError('Duplicate object identity')
        keys.add(key)
        for field in ('entry', 'display', 'phase'):
            if type(obj[field]) is not int or not 0 < obj[field] <= 2147483647:
                raise ValueError('Invalid ' + field)
        if not math.isfinite(float(obj.get('scale', 1))) or abs(float(obj.get('scale', 1)) - 1) > .0001:
            raise ValueError('Per-spawn scaling is unsupported; reset scale to 1')
        if obj.get('doodadset', 0) != 0:
            raise ValueError('Custom WMO doodad sets are unsupported; restore doodad set 0')
        x,y,z,o,qz,qw = transform(obj['position'],obj['rotation'])
        if abs(x) > ZEROPOINT or abs(y) > ZEROPOINT or abs(z) > 100000:
            raise ValueError('Placement is outside supported world bounds')
        rows.append("('%s',%d,%d,%d,%s)" % (key,obj['entry'],obj['display'],obj['phase'],
                    ','.join(map(number,(x,y,z,o,qz,qw)))))
    inserts = ''
    if rows:
        inserts = 'INSERT INTO `noggit_stage` VALUES\n' + ',\n'.join(rows) + ';\n'
    proc = 'noggit_apply_' + project.replace('-','')
    return f'''-- Noggit -> AzerothCore decorative server objects. Generated; review before applying.
-- Apply to the WORLD database on the server machine with worldserver STOPPED.
-- Back up gameobject and noggit_gameobject_link first. Requires MySQL 8 and CREATE ROUTINE.
-- This is the complete desired state of project {project}, map {map_id}.
-- Reapplying updates existing GUIDs. Missing objects are deleted only from this project.
-- Do not use mysql --force. Do not mix GM edits with these managed spawns.
CREATE TABLE IF NOT EXISTS `noggit_gameobject_link` (
  `project` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `object_key` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`project`,`object_key`), UNIQUE KEY (`guid`)
) ENGINE=InnoDB;
DROP TEMPORARY TABLE IF EXISTS `noggit_stage`;
CREATE TEMPORARY TABLE `noggit_stage` (
  `object_key` CHAR(36) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
  `entry` INT UNSIGNED, `display` INT UNSIGNED, `phase` INT UNSIGNED,
  `x` DOUBLE, `y` DOUBLE, `z` DOUBLE, `o` DOUBLE, `qz` DOUBLE, `qw` DOUBLE
) ENGINE=InnoDB;
{inserts}
DROP PROCEDURE IF EXISTS `{proc}`;
DELIMITER $$
CREATE PROCEDURE `{proc}`()
BEGIN
  DECLARE acquired INT DEFAULT 0;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    IF acquired = 1 THEN DO RELEASE_LOCK('noggit_azerothcore_export'); END IF;
    RESIGNAL;
  END;
  SELECT GET_LOCK('noggit_azerothcore_export',0) INTO acquired;
  IF acquired IS NULL OR acquired <> 1 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Another Noggit export is running';
  END IF;
  START TRANSACTION;
  IF EXISTS (SELECT 1 FROM `noggit_stage` s LEFT JOIN `gameobject_template` t ON t.entry=s.entry
             WHERE t.entry IS NULL OR t.displayId<>s.display OR t.type<>5 OR ABS(t.size-1)>0.0001) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Missing or changed server template: refresh catalog first';
  END IF;
  IF EXISTS (SELECT 1 FROM `noggit_gameobject_link` l LEFT JOIN `gameobject` g ON g.guid=l.guid
             WHERE l.project='{project}' AND (g.guid IS NULL OR
               NOT (g.Comment <=> CONCAT('noggit:',l.project,':',l.object_key)))) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Managed spawn was changed/deleted outside Noggit; reconcile before import';
  END IF;
  IF EXISTS (SELECT 1 FROM `gameobject` g LEFT JOIN `noggit_gameobject_link` l ON l.guid=g.guid
             WHERE g.Comment LIKE 'noggit:{project}:%' AND l.guid IS NULL) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Unlinked project spawn found; restore ownership table before import';
  END IF;
  IF EXISTS (SELECT 1 FROM `noggit_gameobject_link` l
             WHERE l.project='{project}' AND (
               EXISTS (SELECT 1 FROM `game_event_gameobject` e WHERE e.guid=l.guid) OR
               EXISTS (SELECT 1 FROM `pool_gameobject` p WHERE p.guid=l.guid) OR
               EXISTS (SELECT 1 FROM `gameobject_addon` a WHERE a.guid=l.guid))) THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='Managed spawn has external event/pool/addon data; reconcile before import';
  END IF;
  DELETE g FROM `gameobject` g JOIN `noggit_gameobject_link` l ON l.guid=g.guid
    LEFT JOIN `noggit_stage` s ON s.object_key=l.object_key
    WHERE l.project='{project}' AND s.object_key IS NULL;
  DELETE l FROM `noggit_gameobject_link` l LEFT JOIN `noggit_stage` s ON s.object_key=l.object_key
    WHERE l.project='{project}' AND s.object_key IS NULL;
  UPDATE `gameobject` g JOIN `noggit_gameobject_link` l ON l.guid=g.guid
    JOIN `noggit_stage` s ON s.object_key=l.object_key
    SET g.id=s.entry,g.map={map_id},g.spawnMask=1,g.phaseMask=s.phase,
        g.position_x=s.x,g.position_y=s.y,g.position_z=s.z,g.orientation=s.o,
        g.rotation0=0,g.rotation1=0,g.rotation2=s.qz,g.rotation3=s.qw,
        g.zoneId=0,g.areaId=0,g.spawntimesecs=0,g.animprogress=255,g.state=1
    WHERE l.project='{project}';
  INSERT INTO `gameobject`
    (id,map,spawnMask,phaseMask,position_x,position_y,position_z,orientation,
     rotation0,rotation1,rotation2,rotation3,spawntimesecs,animprogress,state,Comment)
    SELECT s.entry,{map_id},1,s.phase,s.x,s.y,s.z,s.o,0,0,s.qz,s.qw,0,255,1,
           CONCAT('noggit:{project}:',s.object_key)
    FROM `noggit_stage` s LEFT JOIN `noggit_gameobject_link` l
      ON l.project='{project}' AND l.object_key=s.object_key WHERE l.guid IS NULL;
  INSERT INTO `noggit_gameobject_link` (project,object_key,guid)
    SELECT '{project}',s.object_key,g.guid FROM `noggit_stage` s
    JOIN `gameobject` g ON g.Comment=CONCAT('noggit:{project}:',s.object_key)
    LEFT JOIN `noggit_gameobject_link` l ON l.project='{project}' AND l.object_key=s.object_key
    WHERE l.guid IS NULL;
  COMMIT;
  DO RELEASE_LOCK('noggit_azerothcore_export');
END$$
DELIMITER ;
CALL `{proc}`();
DROP PROCEDURE `{proc}`;
DROP TEMPORARY TABLE `noggit_stage`;
'''


def main():
    root = json.loads(Path(sys.argv[1]).read_text())
    sql = render(root)
    destination = Path(sys.argv[2])
    tmp = destination.with_suffix(destination.suffix + '.tmp')
    tmp.write_text(sql)
    tmp.replace(destination)
    empty = dict(root, objects=[])
    destination.with_name(destination.stem + '-remove-project.sql').write_text(render(empty))
    print(f'Exported {len(root["objects"])} placements to {destination}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, IndexError) as exc:
        sys.exit(str(exc))
