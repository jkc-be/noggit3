// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <math/bounding_box.hpp>
#include <math/frustum.hpp>
#include <noggit/Log.h>
#include <noggit/Misc.h> // checkinside
#include <noggit/Model.h> // Model, etc.
#include <noggit/ModelInstance.h>
#include <noggit/WMOInstance.h>
#include <noggit/World.h>
#include <opengl/primitives.hpp>
#include <opengl/scoped.hpp>
#include <opengl/shader.hpp>

ModelInstance::ModelInstance(std::string const& filename)
  : noggit::moveable_object(math::vector_3d(0.f, 0.f, 0.f), math::degrees::vec3(), 1.f)
  , model (filename)
{
}

ModelInstance::ModelInstance(std::string const& filename, ENTRY_MDDF const*d)
  : noggit::moveable_object(d)
  , model (filename)
{
	uid = d->uniqueID;

  if (model->finishedLoading())
  {
    recalcExtents();
  }
  else
  {
    _need_recalc_extents = true;
  }
}

void ModelInstance::before_move(World* world)
{
  world->updateTilesModel(this, model_update::remove);
}
void ModelInstance::after_move(World* world)
{
  recalcExtents();
  world->updateTilesModel(this, model_update::add);
}

bool ModelInstance::is_a_duplicate_of(ModelInstance const& other)
{
  return !server_entry && !other.server_entry
      && model->filename == other.model->filename
      && misc::vec3d_equals(position(), other.position())
      && misc::deg_vec3d_equals(rotation(), other.rotation())
      && misc::float_equals(scale(), other.scale());
}

void ModelInstance::draw_box ( math::matrix_4x4 const& model_view
                             , math::matrix_4x4 const& projection
                             , bool is_current_selection
                             , bool only_one_box
                             )
{
  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  if (is_current_selection)
  {
    if(!only_one_box)
    {
      opengl::primitives::wire_box ( misc::transform_model_box_coords(model->header.collision_box_min)
                                   , misc::transform_model_box_coords(model->header.collision_box_max)
                                   ).draw ( model_view
                                          , projection
                                          , transform_matrix_transposed()
                                          , { 1.0f, 1.0f, 0.0f, 1.0f }
                                          );
      opengl::primitives::wire_box ( _extents[0]
                                   , _extents[1]
                                   ).draw ( model_view
                                          , projection
                                          , math::matrix_4x4(math::matrix_4x4::unit)
                                          , {0.0f, 1.0f, 0.0f, 1.0f}
                                          );
    }

    opengl::primitives::wire_box ( misc::transform_model_box_coords(model->header.bounding_box_min)
                                 , misc::transform_model_box_coords(model->header.bounding_box_max)
                                 ).draw ( model_view
                                        , projection
                                        , transform_matrix_transposed()
                                        , {1.0f, 1.0f, 1.0f, 1.0f}
                                        );
  }
  else
  {
    opengl::primitives::wire_box ( misc::transform_model_box_coords(model->header.bounding_box_min)
                                 , misc::transform_model_box_coords(model->header.bounding_box_max)
                                 ).draw ( model_view
                                        , projection
                                        , transform_matrix_transposed()
                                        , {0.5f, 0.5f, 0.5f, 1.0f}
                                        );
  }
}

void ModelInstance::update_transform_matrix()
{
  math::matrix_4x4 mat (math::matrix_4x4 (math::matrix_4x4::translation, position())
          * math::matrix_4x4 (math::matrix_4x4::rotation_yzx, rotation())
          * math::matrix_4x4 (math::matrix_4x4::scale, scale())
          );

  _transform_mat_inverted = mat.inverted();
  _transform_mat_transposed = mat.transposed();
}

void ModelInstance::intersect ( math::matrix_4x4 const& model_view
                              , math::ray const& ray
                              , selection_result* results
                              , int animtime
                              )
{
  math::ray subray (_transform_mat_inverted, ray);

  if ( !subray.intersect_bounds ( fixCoordSystem (model->header.bounding_box_min)
                                , fixCoordSystem (model->header.bounding_box_max)
                                )
     )
  {
    return;
  }

  for (auto&& result : model->intersect (model_view, subray, animtime))
  {
    //! \todo why is only sc important? these are relative to subray,
    //! so should be inverted by model_matrix?
    results->emplace_back (result * scale(), selected_model_type(this));
  }
}

void ModelInstance::resetDirection()
{
  auto dir = rotation();

  dir.x = 0_deg;
  dir.z = 0_deg;

  set_rotation(dir);
}

bool ModelInstance::isInsideRect(math::vector_3d rect[2]) const
{
  return misc::rectOverlap(_extents.data(), rect);
}

bool ModelInstance::is_visible( math::frustum const& frustum
                              , const float& cull_distance
                              , const math::vector_3d& camera
                              , display_mode display
                              )
{
  if (_need_recalc_extents)
  {
    recalcExtents();
  }

  float dist;

  _is_visible = false;

  if (display == display_mode::in_3D)
  {
    dist = (get_pos() - camera).length() - model->rad * scale();
  }
  else
  {
    dist = std::abs(get_pos().y - camera.y) - model->rad * scale();
  }

  if (dist >= cull_distance)
  {
    return false;
  }

  if (size_cat < 1.f && dist > 30.f)
  {
    return false;
  }
  else if (size_cat < 4.f && dist > 150.f)
  {
    return false;
  }
  else if (size_cat < 25.f && dist > 300.f)
  {
    return false;
  }

  _is_visible = frustum.intersectsSphere(get_pos(), model->rad * scale());

  return _is_visible;
}

bool ModelInstance::recalcExtents()
{
  if (!model->finishedLoading())
  {
    _need_recalc_extents = true;
    return false;
  }

  if (model->loading_failed())
  {
    _extents[0] = _extents[1] = position();
    _need_recalc_extents = false;
    return true;
  }

  update_transform_matrix();

  math::aabb const relative_to_model
    ( math::min ( model->header.collision_box_min
                , model->header.bounding_box_min
                )
    , math::max ( model->header.collision_box_max
                , model->header.bounding_box_max
                )
    );

  //! \todo If both boxes are {inf, -inf}, or well, if any min.c > max.c,
  //! the model is bad itself. We *could* detect that case and explicitly
  //! assume {-1, 1} then, to be nice to fuckported models.

  auto const corners_in_world (math::apply (misc::transform_model_box_coords, relative_to_model.all_corners()));

  auto const rotated_corners_in_world (_transform_mat_transposed.transposed() * corners_in_world);

  math::aabb const bounding_of_rotated_points (rotated_corners_in_world);

  _extents[0] = bounding_of_rotated_points.min;
  _extents[1] = bounding_of_rotated_points.max;

  size_cat = (bounding_of_rotated_points.max - bounding_of_rotated_points.min).length();

  _need_recalc_extents = false;

  // trigger model transform buffer update when
  // one instance has moved
  model->require_transform_buffer_update();

  return true;
}


std::vector<math::vector_3d> const& ModelInstance::extents()
{
  if (_need_recalc_extents && model->finishedLoading())
  {
    recalcExtents();
  }

  return _extents;
}


wmo_doodad_instance::wmo_doodad_instance(std::string const& filename, MPQFile* f)
  : ModelInstance (filename)
{
  float ff[4];

  f->read(ff, 12);
  set_position(math::vector_3d(ff[0], ff[2], -ff[1]));

  f->read(ff, 16);
  doodad_orientation = math::quaternion (-ff[0], -ff[2], ff[1], ff[3]);

  float scale;
  f->read(&scale, 4);
  set_scale(scale);

  union
  {
    uint32_t packed;
    struct
    {
      uint8_t b, g, r, a;
    }bgra;
  } color;

  f->read(&color.packed, 4);

  light_color = math::vector_3d(color.bgra.r / 255.f, color.bgra.g / 255.f, color.bgra.b / 255.f);
}

bool wmo_doodad_instance::update_transform_matrix_wmo(WMOInstance* wmo)
{
  if (!model->finishedLoading())
  {
    _need_matrix_update = true;
    return false;
  }

  world_pos = wmo->transform_matrix() * position();

  math::matrix_4x4 m2_mat
  (
    math::matrix_4x4(math::matrix_4x4::translation, position())
    * math::matrix_4x4 (math::matrix_4x4::rotation, doodad_orientation)
    * math::matrix_4x4 (math::matrix_4x4::scale, scale())
  );

  math::matrix_4x4 mat
  (
    wmo->transform_matrix()
    * m2_mat
  );

  _transform_mat_inverted = mat.inverted();
  _transform_mat_transposed = mat.transposed();

  // to compute the size category (used in culling)
  recalcExtents();

  _need_matrix_update = false;

  return true;
}
