// This file is part of Noggit3, licensed under GNU GPL version 3.
#include <noggit/ui/server_object_layer.hpp>
#include <noggit/World.h>
#include <noggit/DBCFile.h>
#include <noggit/MPQ.h>
#include <noggit/settings.hpp>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>
#include <stdexcept>

namespace
{
  QJsonObject read_json(QString const& path)
  {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error(f.errorString().toStdString());
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
      throw std::runtime_error("Invalid JSON project/catalog file");
    return doc.object();
  }
  QString new_key() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
  QString normalized(QString path)
  {
    path = path.toLower().replace('\\', '/');
    if (path.endsWith(".mdx")) path.chop(3), path += "m2";
    return path;
  }
}

namespace noggit::ui
{
  server_object_layer::server_object_layer(World* world, QWidget* parent)
    : _world(world), _parent(parent), _timer(new QTimer(parent))
  {
    QDir dir(QString::fromStdString(NoggitSettings.project_path()));
    dir.mkpath("server");
    _path = dir.filePath(QString("server/map-%1.json").arg(world->getMapID()));
    QObject::connect(_timer, &QTimer::timeout, parent, [this] { save(); });
  }
  server_object_layer::~server_object_layer() { delete _timer; }
  void server_object_layer::report(QString const& error)
  {
    if (!_error_reported)
    {
      _error_reported = true;
      QMessageBox::warning(_parent, "AzerothCore server objects", error);
    }
  }
  void server_object_layer::ensure_catalog()
  {
    if (!_catalog.isEmpty()) return;
    auto root = read_json(QCoreApplication::applicationDirPath() + "/integration/catalog.json");
    if (!MPQFile::exists("DBFilesClient/GameObjectDisplayInfo.dbc"))
      throw std::runtime_error("GameObjectDisplayInfo.dbc is missing from the client.");
    DBCFile dbc("DBFilesClient/GameObjectDisplayInfo.dbc");
    dbc.open();
    QMap<unsigned int, QString> models;
    for (auto const& rec : dbc)
      models[rec.getUInt(0)] = normalized(QString::fromUtf8(rec.getString(1)));
    for (auto value : root["templates"].toArray())
    {
      auto t = value.toObject();
      auto model = models.value(t["display"].toInt());
      // First version supports decorative generic objects with an upright, unit-scale model.
      if (t["type"].toInt() != 5 || std::abs(t["size"].toDouble() - 1.0) > 0.0001
          || (!model.endsWith(".m2") && !model.endsWith(".wmo"))
          || !MPQFile::exists(model.toStdString())) continue;
      t["model"] = model;
      _catalog.append(t);
    }
    if (_catalog.isEmpty()) throw std::runtime_error("No compatible decorative templates found in the catalog.");
  }
  void server_object_layer::spawn(QJsonObject const& row)
  {
    auto pos = row["position"].toArray();
    auto rot = row["rotation"].toArray();
    auto file = row["model"].toString().toStdString();
    math::vector_3d p(pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble());
    math::degrees::vec3 r(math::degrees(rot[0].toDouble()), math::degrees(rot[1].toDouble()), math::degrees(rot[2].toDouble()));
    noggit::moveable_object* obj;
    if (row["model"].toString().endsWith(".wmo"))
    {
      auto* wmo = _world->addWMO(file, p, r);
      wmo->change_doodadset(row["doodadset"].toInt(0));
      obj = wmo;
      _world->set_current_selection(wmo);
    }
    else
    {
      auto* m2 = _world->addM2(file, p, row["scale"].toDouble(1), r, nullptr);
      obj = m2;
      _world->set_current_selection(m2);
    }
    obj->server_entry = row["entry"].toInt();
    obj->server_key = row["key"].toString().toStdString();
    obj->server_phase = row["phase"].toInt(1);
  }
  void server_object_layer::load()
  {
    try
    {
      if (QFile::exists(_path))
      {
        auto root = read_json(_path);
        if (root["version"].toInt() != 1 || root["map"].toInt(-1) != int(_world->getMapID())
            || QUuid(root["project"].toString()).isNull() || !root["objects"].isArray())
          throw std::runtime_error("Invalid server layer header. File preserved; layer disabled.");
        _project = root["project"].toString();
        ensure_catalog();
        QSet<QString> keys;
        for (auto value : root["objects"].toArray())
        {
          auto row = value.toObject();
          auto key = row["key"].toString();
          if (QUuid(key).isNull() || keys.contains(key) || row["position"].toArray().size() != 3
              || row["rotation"].toArray().size() != 3 || row["phase"].toInt() < 1)
            throw std::runtime_error("Invalid server placement. Original file preserved; layer disabled.");
          for (auto field : {"position", "rotation"})
            for (auto component : row[field].toArray())
              if (!component.isDouble() || !std::isfinite(component.toDouble()))
                throw std::runtime_error("Invalid server transform. Original file preserved.");
          if (!row["scale"].isDouble() || !std::isfinite(row["scale"].toDouble()) || row["scale"].toDouble() <= 0)
            throw std::runtime_error("Invalid server scale. Original file preserved.");
          keys.insert(key);
          bool found = false;
          for (auto t : _catalog)
            if (t.toObject()["entry"] == row["entry"] && t.toObject()["model"] == row["model"]
                && t.toObject()["display"] == row["display"]) found = true;
          if (!found) throw std::runtime_error("A saved template/model is missing or changed. Original file preserved.");
        }
        for (auto row : root["objects"].toArray()) spawn(row.toObject());
        _world->reset_selection();
      }
      else _project = new_key();
      _ready = true;
      _timer->start(2000);
    }
    catch (std::exception const& e) { report(QString::fromUtf8(e.what())); }
  }
  QJsonArray server_object_layer::snapshot()
  {
    QJsonArray rows;
    QSet<QString> keys;
    auto collect = [&](noggit::moveable_object& obj, QString model)
    {
      if (!obj.server_entry) return;
      auto key = QString::fromStdString(obj.server_key);
      // Copy/paste creates a new placement identity instead of aliasing the source spawn.
      if (key.isEmpty() || keys.contains(key)) obj.server_key = (key = new_key()).toStdString();
      keys.insert(key);
      auto p = obj.position();
      auto r = obj.rotation();
      int display = 0;
      for (auto t : _catalog)
        if (t.toObject()["entry"].toInt() == int(obj.server_entry)) display = t.toObject()["display"].toInt();
      rows.append(QJsonObject{{"key", key}, {"entry", int(obj.server_entry)}, {"display", display},
        {"model", normalized(model)}, {"phase", int(obj.server_phase)}, {"scale", obj.scale()},
        {"position", QJsonArray{p.x,p.y,p.z}}, {"rotation", QJsonArray{r.x._,r.y._,r.z._}}});
    };
    _world->for_each_m2_instance([&](ModelInstance& obj) { collect(obj, QString::fromStdString(obj.model->filename)); });
    _world->for_each_wmo_instance([&](WMOInstance& obj) {
      collect(obj, QString::fromStdString(obj.wmo->filename));
      if (obj.server_entry) {
        auto row = rows.last().toObject();
        row["doodadset"] = int(obj.doodadset());
        rows[rows.size()-1] = row;
      }
    });
    return rows;
  }
  bool server_object_layer::save()
  {
    if (!_ready) return false;
    auto data = QJsonDocument(QJsonObject{{"version",1}, {"project",_project},
      {"map",int(_world->getMapID())}, {"objects",snapshot()}}).toJson();
    if (data == _last_saved) return true;
    QSaveFile f(_path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
    {
      report("Server placements could not be saved: " + f.errorString());
      return false;
    }
    _last_saved = data;
    _error_reported = false;
    return true;
  }
  void server_object_layer::add(math::vector_3d const& position)
  {
    if (!_ready) return;
    try { ensure_catalog(); }
    catch (std::exception const& e) { report(QString::fromUtf8(e.what())); return; }
    QDialog dialog(_parent);
    dialog.setWindowTitle("Add AzerothCore server object");
    dialog.resize(850,500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Search existing decorative templates. Placement is saved to the server layer.", &dialog));
    auto* search = new QLineEdit(&dialog);
    search->setPlaceholderText("House, tent, barrel, or template ID...");
    layout->addWidget(search);
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);
    auto fill = [&]
    {
      list->clear();
      for (int i = 0; i < _catalog.size(); ++i)
      {
        auto t = _catalog[i].toObject();
        auto text = QString("%1 — %2 — %3").arg(t["entry"].toInt()).arg(t["name"].toString(),t["model"].toString());
        if (!text.contains(search->text(), Qt::CaseInsensitive)) continue;
        auto* item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, i);
      }
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, fill);
    fill();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted || !list->currentItem()) return;
    auto row = _catalog[list->currentItem()->data(Qt::UserRole).toInt()].toObject();
    row["position"] = QJsonArray{position.x,position.y,position.z};
    row["rotation"] = QJsonArray{0,90,0}; // server orientation zero
    row["key"] = new_key();
    row["phase"] = 1;
    spawn(row);
    save();
  }
  void server_object_layer::phase()
  {
    bool ok;
    int phase = QInputDialog::getInt(_parent,"Server object phase","Phase mask",1,1,2147483647,1,&ok);
    if (!ok) return;
    for (auto selection : _world->current_selection())
    {
      if (selection.index() == eEntry_Model)
      { auto* p = std::get<selected_model_type>(selection); if (p->server_entry) p->server_phase = phase; }
      else if (selection.index() == eEntry_WMO)
      { auto* p = std::get<selected_wmo_type>(selection); if (p->server_entry) p->server_phase = phase; }
    }
    save();
  }
  void server_object_layer::export_sql()
  {
    if (!save()) return;
    auto output = QFileDialog::getSaveFileName(_parent,"Export AzerothCore SQL",QFileInfo(_path).path() + "/placements.sql","SQL (*.sql)");
    if (output.isEmpty()) return;
    QProcess process;
    process.start("python3", {QCoreApplication::applicationDirPath()+"/integration/export.py",_path,output});
    if (!process.waitForFinished(30000) || process.exitCode() != 0)
    {
      QMessageBox::warning(_parent,"Export failed",QString::fromUtf8(process.readAllStandardError()) + process.errorString());
      return;
    }
    QMessageBox::information(_parent,"SQL exported","Saved " + output + "\nApply on the server using the included SERVER-HANDOFF.md instructions.");
  }
}
