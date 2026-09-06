// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/MapView.h>
#include <noggit/Misc.h>
#include <noggit/ModelInstance.h>
#include <noggit/settings.hpp>
#include <noggit/WMOInstance.h> // WMOInstance
#include <noggit/World.h>
#include <noggit/ui/asset_browser.hpp>
#include <noggit/ui/collapsible_widget.hpp>
#include <noggit/ui/HelperModels.h>
#include <noggit/ui/ModelImport.h>
#include <noggit/ui/ObjectEditor.h>
#include <noggit/ui/RotationEditor.h>
#include <noggit/ui/checkbox.hpp>
#include <util/qt/overload.hpp>

#include <QClipboard>
#include <QGuiApplication>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLineEdit>
#include <QPushButton>
#include <QtWidgets/QMessageBox>
#include <QUuid>
#include <QKeyEvent>


#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <sstream>

namespace noggit
{
  namespace ui
  {
    object_editor::object_editor ( MapView* mapView
                                 , World* world
                                 , QWidget* parent
                                 )
            : noggit_tool(parent)
            , modelImport (new model_import(this))
            , rotationEditor (new rotation_editor(mapView, world, &_use_median_pivot_point))
            , helper_models_widget(new helper_models(this))
            , asset_browser_widget(new asset_browser(mapView, this))
            , _copy_model_stats (true)
            , selected()
            , pasteMode(PASTE_ON_TERRAIN)
    {
      auto layout = new QFormLayout (this);
      // might seem counter-intuitive but that's what allow the widget
      // to resize when the children change size
      layout->setSizeConstraint(QLayout::SetFixedSize);

      collapsible_widget* copy_widget = new collapsible_widget("Copy options", true, this);
      // to avoid width changes when the widget expand/collapse
      // which can make part of the UI go out of screen
      copy_widget->setFixedWidth(200);
      auto copy_layout = new QFormLayout (copy_widget);
      copy_widget->setTitle("test");
      copy_widget->add_layout(copy_layout);

      auto rotation_group (new QGroupBox ("Random rotation", copy_widget));
      auto tilt_group (new QGroupBox ("Random tilt", copy_widget));
      auto scale_group (new QGroupBox ("Random scale", copy_widget));
      auto rotation_layout (new QGridLayout (rotation_group));
      auto tilt_layout (new QGridLayout(tilt_group));
      auto scale_layout (new QGridLayout(scale_group));

      rotation_group->setCheckable(true);
      rotation_group->setChecked(NoggitSettings.value ("model/random_rotation", false).toBool());
      tilt_group->setCheckable(true);
      tilt_group->setChecked(NoggitSettings.value ("model/random_tilt", false).toBool());
      scale_group->setCheckable(true);
      scale_group->setChecked(NoggitSettings.value ("model/random_size", false).toBool());

      QCheckBox *copyAttributesCheck = new QCheckBox("Copy rotation, tilt, and scale", this);

      QDoubleSpinBox *rotRangeStart = new QDoubleSpinBox(this);
      QDoubleSpinBox *rotRangeEnd = new QDoubleSpinBox(this);
      QDoubleSpinBox *tiltRangeStart = new QDoubleSpinBox(this);
      QDoubleSpinBox *tiltRangeEnd = new QDoubleSpinBox(this);
      QDoubleSpinBox *scaleRangeStart = new QDoubleSpinBox(this);
      QDoubleSpinBox *scaleRangeEnd = new QDoubleSpinBox(this);

      _filename = new QLabel (this);
      _filename->setWordWrap (true);

      rotRangeStart->setMaximumWidth(85);
      rotRangeEnd->setMaximumWidth(85);
      tiltRangeStart->setMaximumWidth(85);
      tiltRangeEnd->setMaximumWidth(85);
      scaleRangeStart->setMaximumWidth(85);
      scaleRangeEnd->setMaximumWidth(85);

      rotRangeStart->setDecimals(3);
      rotRangeEnd->setDecimals(3);
      tiltRangeStart->setDecimals(3);
      tiltRangeEnd->setDecimals(3);
      scaleRangeStart->setDecimals(3);
      scaleRangeEnd->setDecimals(3);

      rotRangeStart->setRange (-180.f, 180.f);
      rotRangeEnd->setRange (-180.f, 180.f);
      tiltRangeStart->setRange (-180.f, 180.f);
      tiltRangeEnd->setRange (-180.f, 180.f);
      scaleRangeStart->setRange (-180.f, 180.f);
      scaleRangeEnd->setRange (-180.f, 180.f);

      rotation_layout->addWidget(rotRangeStart, 0, 0);
      rotation_layout->addWidget(rotRangeEnd, 0 ,1);
      copy_layout->addRow(rotation_group);

      tilt_layout->addWidget(tiltRangeStart, 0, 0);
      tilt_layout->addWidget(tiltRangeEnd, 0, 1);
      copy_layout->addRow(tilt_group);

      scale_layout->addWidget(scaleRangeStart, 0, 0);
      scale_layout->addWidget(scaleRangeEnd, 0, 1);
      copy_layout->addRow(scale_group);

      copy_layout->addRow(copyAttributesCheck);

      QGroupBox *pasteBox = new QGroupBox("Paste Options", this);
      auto paste_layout = new QGridLayout (pasteBox);
      QRadioButton *terrainButton = new QRadioButton("Terrain");
      QRadioButton *selectionButton = new QRadioButton("Selection");
      QRadioButton *cameraButton = new QRadioButton("Camera");

      pasteModeGroup = new QButtonGroup(this);
      pasteModeGroup->addButton(terrainButton, 0);
      pasteModeGroup->addButton(selectionButton, 1);
      pasteModeGroup->addButton(cameraButton, 2);

      paste_layout->addWidget(terrainButton, 0, 0);
      paste_layout->addWidget(selectionButton, 0, 1);
      paste_layout->addWidget(cameraButton, 1, 0);

      auto object_movement_box (new QGroupBox("Single Selection Movement", this));
      auto object_movement_layout = new QFormLayout (object_movement_box);

      // single model selection
      auto object_movement_cb ( new checkbox ( "Mouse move follow\ncursor on the ground"
                                             , &_move_model_to_cursor_position
                                             , this
                                             )
                              );

      object_movement_layout->addRow(object_movement_cb);

      // multi model selection
      auto multi_select_movement_box(new QGroupBox("Multi Selection Movement", this));
      auto multi_select_movement_layout = new QFormLayout(multi_select_movement_box);

      auto multi_select_movement_cb ( new checkbox( "Mouse move snap\nmodels to the ground"
                                                  , &_snap_multi_selection_to_ground
                                                  , this
                                                  )
                                    );

      auto object_median_pivot_point (new checkbox ( "Rotate around pivot point"
                                                   , &_use_median_pivot_point
                                                   , this
                                                   )
                                     );


      multi_select_movement_layout->addRow(multi_select_movement_cb);
      multi_select_movement_layout->addRow(object_median_pivot_point);

      auto object_rot_box(new QGroupBox("Follow Ground Rotation", this));
      auto object_rot_layout = new QFormLayout(object_rot_box);

      auto object_rotateground_cb ( new checkbox ( "Rotate following cursor"
                                                 , &_rotate_along_ground
                                                 , this
                                                 )
                                  );
      auto object_rotategroundsmooth_cb ( new checkbox ( "Smooth follow rotation"
                                                       , &_rotate_along_ground_smooth
                                                       , this
                                                       )
                                        );

      auto object_rotategroundrandom_cb ( new checkbox ( "Random rot/tilt/scale\n on rotate"
                                                       , &_rotate_along_ground_random
                                                       , this
                                                       )
                                        );
      object_rot_layout->addRow(object_rotateground_cb);
      object_rot_layout->addRow(object_rotategroundsmooth_cb);
      object_rot_layout->addRow(object_rotategroundrandom_cb);

      QPushButton *rotEditorButton = new QPushButton("Pos/Rotation Editor", this);
      QPushButton *visToggleButton = new QPushButton("Toggle Hidden Models Visibility", this);
      QPushButton *clearListButton = new QPushButton("Clear Hidden Models List", this);

      QGroupBox *importBox = new QGroupBox(this);
      new QGridLayout (importBox);
      importBox->setTitle("Import");

      QPushButton* asset_browser_btn = new QPushButton("Asset Browser", this);
      QPushButton* toTxt = new QPushButton("To Text File", this);
      QPushButton* fromTxt = new QPushButton("From Text File", this);
      QPushButton* last_m2_from_wmv = new QPushButton("Last M2 from WMV", this);
      QPushButton* last_wmo_from_wmv = new QPushButton("Last WMO from WMV", this);
      QPushButton* helper_models_btn = new QPushButton("Helper Models", this);

      importBox->layout()->addWidget(asset_browser_btn);
      importBox->layout()->addWidget(toTxt);
      importBox->layout()->addWidget(fromTxt);
      importBox->layout()->addWidget(last_m2_from_wmv);
      importBox->layout()->addWidget(last_wmo_from_wmv);
      importBox->layout()->addWidget(helper_models_btn);

      QGroupBox* gizmo_gb = new QGroupBox("Gizmo", this);
      QFormLayout* gizmo_layout(new QFormLayout(gizmo_gb));

      QPushButton* toggle_gizmo_local_mode = new QPushButton("Toggle local/world coord mode", gizmo_gb);
      gizmo_layout->addWidget(toggle_gizmo_local_mode);


      layout->addRow(copy_widget);
      layout->addRow(pasteBox);
      layout->addRow(object_movement_box);
      layout->addRow(multi_select_movement_box);
      layout->addRow(object_rot_box);
      layout->addRow(rotEditorButton);
      layout->addRow(visToggleButton);
      layout->addRow(clearListButton);
      layout->addRow(importBox);
      layout->addRow(gizmo_gb);
      layout->addRow (_filename);

      connect (rotation_group, &QGroupBox::toggled, [&] (int s)
      {
        NoggitSettings.set_value ("model/random_rotation", s);
        NoggitSettings.values->sync();
      });

      connect (tilt_group, &QGroupBox::toggled, [&] (int s)
      {
        NoggitSettings.set_value ("model/random_tilt", s);
        NoggitSettings.values->sync();
      });

      connect (scale_group, &QGroupBox::toggled, [&] (int s)
      {
        NoggitSettings.set_value ("model/random_size", s);
        NoggitSettings.values->sync();
      });

      rotRangeStart->setValue(_paste_params.minRotation);
      rotRangeEnd->setValue(_paste_params.maxRotation);

      tiltRangeStart->setValue(_paste_params.minTilt);
      tiltRangeEnd->setValue(_paste_params.maxTilt);

      scaleRangeStart->setValue(_paste_params.minScale);
      scaleRangeEnd->setValue(_paste_params.maxScale);

      connect ( rotRangeStart, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [=] (double v)
                {
                  _paste_params.minRotation = v;
                }
      );

      connect ( rotRangeEnd, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [=] (double v)
                {
                  _paste_params.maxRotation = v;
                }
      );

      connect ( tiltRangeStart, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [=] (double v)
                {
                  _paste_params.minTilt = v;
                }
      );

      connect ( tiltRangeEnd, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [=] (double v)
                {
                  _paste_params.maxTilt = v;
                }
      );

      connect ( scaleRangeStart, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [=] (double v)
                {
                  _paste_params.minScale = v;
                }
      );

      connect ( scaleRangeEnd, qOverload<double> (&QDoubleSpinBox::valueChanged)
              , [=] (double v)
                {
                  _paste_params.maxScale = v;
                }
      );

      copyAttributesCheck->setChecked(_copy_model_stats);
      connect (copyAttributesCheck, &QCheckBox::stateChanged, [this] (int s)
      {
        _copy_model_stats = s;
      });

      pasteModeGroup->button(pasteMode)->setChecked(true);

      connect ( pasteModeGroup, qOverload<int> (&QButtonGroup::buttonClicked)
              , [&] (int id)
                {
                    pasteMode = id;
                }
      );

      connect(rotEditorButton, &QPushButton::clicked, [=]() {
          rotationEditor->show();
      });

      connect(visToggleButton, &QPushButton::clicked, [=]() {
          mapView->_draw_hidden_models.set
            (!mapView->_draw_hidden_models.get());
      });

      connect(clearListButton, &QPushButton::clicked, [=]() {
        ModelManager::clear_hidden_models();
        WMOManager::clear_hidden_wmos();
      });

      connect(asset_browser_btn, &QPushButton::clicked, [=]() {
          asset_browser_widget->setVisible(!asset_browser_widget->isVisible());
      });
      connect(toTxt, &QPushButton::clicked, [=]() {
          SaveObjecttoTXT (world);
      });

      connect(fromTxt, &QPushButton::clicked, [=]() {
          showImportModels();
      });

      connect( last_m2_from_wmv
             , &QPushButton::clicked
             , [=]() { import_last_model_from_wmv(eEntry_Model); }
             );

      connect( last_wmo_from_wmv
             , &QPushButton::clicked
             , [=]() { import_last_model_from_wmv(eEntry_WMO); }
             );

      connect( helper_models_btn
             , &QPushButton::clicked
             , [=]() { helper_models_widget->show(); }
             );

      connect( toggle_gizmo_local_mode
             , &QPushButton::clicked
             , [=]() { world->gizmo.toggle_local_space(); }
             );

      setMinimumWidth(sizeHint().width());

      auto mv_pos = mapView->pos();
      auto mv_size = mapView->size();

      // make sure the window doesn't show up halfway outside the screen
      modelImport->move(mv_pos.x() + (mv_size.width() / 2), mv_pos.y() + (mv_size.height() / 2));
    }

    object_editor::~object_editor()
    {
      for (auto& instance : _model_instance_created)
      {
        if (instance.index() == eEntry_Model)
        {
          ModelInstance* mi = std::get<selected_model_type>(instance);
          delete mi;
        }
        else if (instance.index() == eEntry_WMO)
        {
          WMOInstance* wi = std::get<selected_wmo_type>(instance);
          delete wi;
        }
      }
    }

    void object_editor::showImportModels()
    {
      modelImport->show();
    }

    void object_editor::pasteObject ( math::vector_3d cursor_pos
                                    , math::vector_3d camera_pos
                                    , World* world
                                    )
    {
      auto last_entry = world->get_last_selected_model();

      for (auto& selection : selected)
      {
        math::vector_3d pos;

        if (selection.index() == eEntry_MapChunk)
        {
          LogError << "Invalid selection" << std::endl;
          return;
        }

        math::vector_3d model_pos = selection.index() == eEntry_Model
          ? std::get<selected_model_type>(selection)->position()
          : std::get<selected_wmo_type>(selection)->position()
          ;

        switch (pasteMode)
        {
        case PASTE_ON_TERRAIN:
          pos = cursor_pos + model_pos;
          break;
        case PASTE_ON_SELECTION:
          if (last_entry)
          {
            math::vector_3d last_entry_pos = last_entry->index() == eEntry_Model
              ? std::get<selected_model_type>(last_entry.value())->position()
              : std::get<selected_wmo_type>(last_entry.value())->position()
              ;

            pos = last_entry_pos + model_pos;
          }
          else // paste to mouse cursor when there's no selected model
          {
            pos = cursor_pos + model_pos;
          }
          break;
        case PASTE_ON_CAMERA:
          pos = camera_pos + model_pos;
          break;
        default:
          LogDebug << "object_editor::pasteObject: unknown paste mode " << pasteMode << std::endl;
          break;
        }

        if (selection.index() == eEntry_Model)
        {
          float scale(1.f);
          math::degrees::vec3 rotation(0_deg, 0_deg, 0_deg);

          if (_copy_model_stats)
          {
            // copy rot size from original model. Dirty but woring
            scale = std::get<selected_model_type>(selection)->scale();
            rotation = std::get<selected_model_type>(selection)->rotation();
          }

          auto* placed = world->addM2( std::get<selected_model_type>(selection)->model->filename
                      , pos
                      , scale
                      , rotation
                      , &_paste_params
                      );
          auto* source = std::get<selected_model_type>(selection);
          placed->server_entry = source->server_entry;
          placed->server_phase = source->server_phase;
          if (placed->server_entry) placed->server_key = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        }
        else if (selection.index() == eEntry_WMO)
        {
          math::degrees::vec3 rotation(0.0_deg, 0.0_deg, 0.0_deg);
          if (_copy_model_stats)
          {
            // copy rot from original model. Dirty but working
            rotation = std::get<selected_wmo_type>(selection)->rotation();
          }

          auto* source = std::get<selected_wmo_type>(selection);
          auto* placed = world->addWMO(source->wmo->filename, pos, rotation);
          placed->server_entry = source->server_entry;
          placed->server_phase = source->server_phase;
          if (placed->server_entry) placed->server_key = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        }
      }
    }

    void object_editor::togglePasteMode()
    {
      pasteModeGroup->button ((pasteMode + 1) % PASTE_MODE_COUNT)->setChecked (true);
    }

    void object_editor::replace_selection(std::vector<selection_type> new_selection)
    {
      selected = new_selection;

      std::stringstream ss;

      if (selected.empty())
      {
        _filename->setText("");
        return;
      }

      if (selected.size() == 1)
      {
        ss << "Model: ";

        auto selectedObject = new_selection.front();
        if (selectedObject.index() == eEntry_Model)
        {
          ss << std::get<selected_model_type>(selectedObject)->model->filename;
        }
        else if (selectedObject.index() == eEntry_WMO)
        {
          ss << std::get<selected_wmo_type>(selectedObject)->wmo->filename;
        }
        else
        {
          ss << "Error";
          LogError << "The new selection wasn't a m2 or wmo" << std::endl;
        }
      }
      else
      {
        ss << "Multiple objects selected";
      }

      // to avoid duplicates
      std::unordered_set<std::string> files;
      std::stringstream clipboard_text;

      for (auto const& it : selected)
      {
        if (it.index() == eEntry_Model)
        {
          files.emplace(std::get<selected_model_type>(it)->model->filename);
        }
        else if (it.index() == eEntry_WMO)
        {
          files.emplace(std::get<selected_wmo_type>(it)->wmo->filename);
        }
      }

      for (std::string const& filename : files)
      {
        clipboard_text << filename << std::endl;
      }

      QGuiApplication::clipboard()->setText(QString::fromStdString(clipboard_text.str()));

      _filename->setText(ss.str().c_str());
    }

    void object_editor::copy(std::string const& filename)
    {
      if (!MPQFile::exists(filename))
      {
        QMessageBox::warning
          ( nullptr
          , "Warning"
          , QString::fromStdString(filename + " not found.")
          );

        return;
      }

      std::vector<selection_type> selected_model;

      if (misc::str_ends_with (filename, ".m2"))
      {
        ModelInstance* mi = new ModelInstance(filename);

        _model_instance_created.push_back(mi);

        selected_model.push_back(mi);
        replace_selection(selected_model);
      }
      else if (misc::str_ends_with (filename, ".wmo"))
      {
        WMOInstance* wi = new WMOInstance(filename);

        _model_instance_created.push_back(wi);

        selected_model.push_back(wi);
        replace_selection(selected_model);
      }
    }

    void object_editor::copy_current_selection(World* world)
    {
      auto const& current_selection = world->current_selection();
      auto const& pivot = world->multi_select_pivot();

      if (current_selection.empty())
      {
        return;
      }

      std::vector<selection_type> selected_model;

      for (auto& selection : current_selection)
      {
        if (selection.index() == eEntry_Model)
        {
          auto original = std::get<selected_model_type>(selection);
          auto clone = new ModelInstance(original->model->filename);

          clone->set_scale(original->scale());
          clone->server_entry = original->server_entry;
          clone->server_phase = original->server_phase;
          clone->set_position(pivot ? original->position() - pivot.value() : math::vector_3d());
          clone->set_rotation(original->rotation());

          selected_model.push_back(clone);
          _model_instance_created.push_back(clone);
        }
        else if (selection.index() == eEntry_WMO)
        {
          auto original = std::get<selected_wmo_type>(selection);
          auto clone = new WMOInstance(original->wmo->filename);
          clone->server_entry = original->server_entry;
          clone->server_phase = original->server_phase;
          clone->set_position(pivot ? original->position() - pivot.value() : math::vector_3d());
          clone->set_rotation(original->rotation());

          selected_model.push_back(clone);
          _model_instance_created.push_back(clone);
        }
      }
      replace_selection(selected_model);
    }

    void object_editor::SaveObjecttoTXT (World* world)
    {
      if (!world->has_selection())
      {
        return;
      }

      std::ofstream stream(NoggitSettings.value("project/import_file", "import.txt").toString().toStdString(), std::ios_base::app);
      for (auto& selection : world->current_selection())
      {
        if (selection.index() == eEntry_MapChunk)
        {
          continue;
        }

        std::string path;

        if (selection.index() == eEntry_WMO)
        {
          path = std::get<selected_wmo_type>(selection)->wmo->filename;
        }
        else if (selection.index() == eEntry_Model)
        {
          path = std::get<selected_model_type>(selection)->model->filename;
        }

        stream << path << std::endl;
      }
      stream.close();
      modelImport->buildModelList();
    }

    void object_editor::import_last_model_from_wmv(int type)
    {
      std::string wmv_log_file (NoggitSettings.value ("project/wmv_log_file").toString().toStdString());
      std::string last_model_found;
      std::string line;
      std::ifstream file(wmv_log_file.c_str());

      if (file.is_open())
      {
        while (!file.eof())
        {
          getline(file, line);
          std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) { return std::tolower(c); });
          std::regex regex( type == eEntry_Model
                          ? "([a-z]+\\\\([a-z0-9_ ]+\\\\)*[a-z0-9_ ]+\\.)(mdx|m2)"
                          : "([a-z]+\\\\([a-z0-9_ ]+\\\\)*[a-z0-9_ ]+\\.)(wmo)"
                          );

          std::smatch match;

          if (std::regex_search (line, match, regex))
          {
            last_model_found = match.str(0);
            size_t found = last_model_found.rfind(".mdx");
            if (found != std::string::npos)
            {
              last_model_found.replace(found, 4, ".m2");
            }
          }
        }
      }
      else
      {
        QMessageBox::warning
          ( nullptr
          , "Warning"
          , "The wmv log file could not be opened"
          );
      }

      if(last_model_found == "")
      {
        QMessageBox::warning
          ( nullptr
          , "Warning"
          , "No corresponding model found in the wmv log file."
          );
      }
      else
      {
        copy(last_model_found);
      }
    }

    QSize object_editor::sizeHint() const
    {
      return QSize(215, height());
    }

    void object_editor::tick(float, math::vector_3d const& cursor_pos, bool, World* world)
    {
      // reset speed when no movement key is pressed
      if(!_move_x_key && !_move_y_key && !_move_z_key && !_scale_key && !_rot_y_key)
      {
        _move_speed_factor = 0.001f;
      }
      else
      {
        if (_mod_ctrl_down && _mod_shift_down)
        {
          _move_speed_factor += 0.1f;
        }
        else if (_mod_shift_down)
        {
          _move_speed_factor += 0.01f;
        }
        else if (_mod_ctrl_down)
        {
          _move_speed_factor += 0.0005f;
        }
      }

      if (_scale_key)
      {
        world->scale_selected_models(_scale_key * _move_speed_factor / 50.f, World::m2_scaling_type::add);
        rotationEditor->updateValues(world);
      }
      if (_rot_y_key)
      {
        world->rotate_selected_models( math::degrees(0.f)
                                     , math::degrees(_rot_y_key * _move_speed_factor * 5.f)
                                     , math::degrees(0.f)
                                     , _use_median_pivot_point.get()
                                     );
        rotationEditor->updateValues(world);
      }
      if (_move_x_key || _move_y_key || _move_z_key)
      {
        world->move_selected_models(_move_x_key * _move_speed_factor, _move_y_key * _move_speed_factor, _move_z_key * _move_speed_factor);
        rotationEditor->updateValues(world);
      }

      if (_middle_mouse_button)
      {
        if (_mod_alt_down)
        {
          world->scale_selected_models(std::pow(2.f, _mouse_mov_y * 4.f), World::m2_scaling_type::mult);
        }
        else if (_mod_shift_down)
        {
          world->move_selected_models(0.f, _mouse_mov_y * 80.f, 0.f);
        }
        else
        {
          bool snapped = false;
          if (world->has_multiple_model_selected())
          {
            world->set_selected_models_pos(cursor_pos, false);

            if (_snap_multi_selection_to_ground.get())
            {
              world->snap_selected_models_to_the_ground();
              snapped = true;
            }
          }
          else
          {
            if (!_move_model_to_cursor_position.get())
            {
              math::vector_3d move(_mouse_mov_y, 0.f, -_mouse_mov_x);
              math::rotate(0.f, 0.f, &move.x, &move.z, -_camera_yaw);

              // todo: add a speed setting for it ? I doubt it's used much though
              world->move_selected_models(move * 150.f);
            }
            else
            {
              world->set_selected_models_pos(cursor_pos, false);
              snapped = true;
            }
          }

          if (snapped && _rotate_along_ground.get())
          {
            world->rotate_selected_models_to_ground_normal(_rotate_along_ground_smooth.get());

            if (_rotate_along_ground_random.get())
            {
              float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;

              if (NoggitSettings.value("model/random_rotation", false).toBool())
              {
                minY = _paste_params.minRotation;
                maxY = _paste_params.maxRotation;
              }

              if (NoggitSettings.value("model/random_tilt", false).toBool())
              {
                minX = _paste_params.minTilt;
                maxX = _paste_params.maxTilt;
                minZ = minX;
                maxZ = maxX;
              }

              world->rotate_selected_models_randomly(minX, maxX, minY, maxY, minZ, maxZ);

              if (NoggitSettings.value("model/random_size", false).toBool())
              {
                float min = _paste_params.minScale;
                float max = _paste_params.maxScale;

                world->scale_selected_models(misc::randfloat(min, max), World::m2_scaling_type::set);
              }
            }
          }
        }

        _mouse_mov_x = 0.f;
        _mouse_mov_y = 0.f;

        rotationEditor->updateValues(world);
      }

      if (_right_mouse_button)
      {
        if (_mod_ctrl_down)
        {
          world->rotate_selected_models( math::degrees(_mouse_rotation_dt)
                                       , math::degrees(0.f)
                                       , math::degrees(0.f)
                                       , _use_median_pivot_point.get()
                                       );
          rotationEditor->updateValues(world);
        }
        if (_mod_shift_down)
        {
          world->rotate_selected_models( math::degrees(0.f)
                                       , math::degrees(_mouse_rotation_dt)
                                       , math::degrees(0.f)
                                       , _use_median_pivot_point.get()
                                       );
          rotationEditor->updateValues(world);
        }
        if (_mod_alt_down)
        {
          world->rotate_selected_models( math::degrees(0.f)
                                       , math::degrees(0.f)
                                       , math::degrees(_mouse_rotation_dt)
                                       , _use_median_pivot_point.get()
                                       );
          rotationEditor->updateValues(world);
        }

        _mouse_rotation_dt = 0.f;
      }
    }

    void object_editor::reset_extra_states()
    {
      _mouse_mov_x = 0.f;
      _mouse_mov_y = 0.f;
      _mouse_rotation_dt = 0.f;

      _scale_key = 0;
      _rot_y_key = 0;
    }

    void object_editor::mouse_move_event(QLineF const& relative_movement)
    {
      _mouse_mov_x = -aspect_ratio() * relative_movement.dx() / static_cast<float>(_window_width);
      _mouse_mov_y = -relative_movement.dy() / static_cast<float>(_window_height);
      // todo: store x + y delta and rotate depending on the camera orientation ?
      _mouse_rotation_dt = (relative_movement.dx() + relative_movement.dy()) / mouse_sensibility * 5.f;
    }

    void object_editor::key_press_event(QKeyEvent* event)
    {
      noggit_tool::key_press_event(event);

      auto key = event->key();

      switch (key)
      {
        case Qt::Key_Plus: _scale_key = 1; break;
        case Qt::Key_Minus: _scale_key = -1; break;
      }

      if (event->modifiers() & Qt::KeypadModifier)
      {
        switch (key)
        {
          case Qt::Key_7: _rot_y_key = 1; break;
          case Qt::Key_9: _rot_y_key = -1; break;
          case Qt::Key_2: _move_x_key = 1; break;
          case Qt::Key_8: _move_x_key = -1; break;
          case Qt::Key_1: _move_y_key = -1; break;
          case Qt::Key_3: _move_y_key = 1; break;
          case Qt::Key_4: _move_z_key = 1; break;
          case Qt::Key_6: _move_z_key = -1; break;
        }
      }
    }
    void object_editor::key_release_event(QKeyEvent* event)
    {
      noggit_tool::key_release_event(event);

      auto key = event->key();

      switch (key)
      {
        case Qt::Key_Plus:
        case Qt::Key_Minus: _scale_key = 0; break;
      }

      if (event->modifiers() & Qt::KeypadModifier)
      {
        switch (key)
        {
          case Qt::Key_7:
          case Qt::Key_9: _rot_y_key = 0; break;
          case Qt::Key_2:
          case Qt::Key_8: _move_x_key = 0; break;
          case Qt::Key_1:
          case Qt::Key_3: _move_y_key = 0; break;
          case Qt::Key_4:
          case Qt::Key_6: _move_z_key = 0; break;
        }
      }
    }

  }
}
