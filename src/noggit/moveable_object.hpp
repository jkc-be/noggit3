// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <math/matrix_4x4.hpp>
#include <math/quaternion.hpp>
#include <math/vector_3d.hpp>
#include <math/trig.hpp>

#include <optional>
#include <string>

class World;
struct ENTRY_MDDF;
struct ENTRY_MODF;

namespace noggit
{
  class gizmo;

  class moveable_object
  {
  public:
    // Editor-only server placement metadata. Never serialized into ADT files.
    std::string server_key;
    unsigned int server_entry = 0;
    unsigned int server_phase = 1;

    moveable_object(math::vector_3d const& pos, math::degrees::vec3 const& rotation);
    moveable_object(math::vector_3d const& pos, math::degrees::vec3 const& rotation, float scale);
    moveable_object(ENTRY_MDDF const* m2_entry);
    moveable_object(ENTRY_MODF const* wmo_entry);

    moveable_object(moveable_object const& other) = default;
    moveable_object& operator= (moveable_object const& other) = default;

    moveable_object(moveable_object&& other);
    moveable_object& operator= (moveable_object&& other);

    virtual void before_move(World*) {}
    virtual void after_move(World*) {}

    void set_position(math::vector_3d const& pos);
    void set_rotation(math::degrees::vec3 const& rotation);
    void set_rotation(math::quaternion const& rotation);
    void set_scale(float scale);

    void move(float dx, float dy, float dz, World* world);
    virtual void move(math::vector_3d const& pos_dt, World* world);
    virtual void rotate(math::degrees::vec3 const& rotation, World* world, bool local);
    virtual void rotate(math::quaternion const& quat, World* world, bool local);
    virtual void update_position(math::vector_3d const& pos, World* world);
    virtual void update_rotation(math::degrees::vec3 const& rotation, World* world);
    virtual void update_scale(float scale, World* world);

    math::vector_3d const& position() const { return _position; }
    math::degrees::vec3 rotation() const { return _rotation.ToEulerAngles(); }
    math::degrees::vec3 adt_rotation() const;
    math::quaternion const& quaternion() const { return _rotation; }
    float scale() const { return _scale; }

    bool can_scale() const { return _can_scale; }

    void link_to_gizmo(noggit::gizmo* gizmo);
    void update_gizmo_if_linked();
    void unlink_from_gizmo();

  private:
    std::optional<noggit::gizmo*> _linked_gizmo;

    math::vector_3d _position;
    math::quaternion _rotation;
    float _scale;
    bool _can_scale;
  };
}
