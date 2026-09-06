// This file is part of Noggit3, licensed under GNU GPL version 3.
#pragma once
#include <QJsonArray>
#include <QString>
#include <math/vector_3d.hpp>
class World;
class QWidget;
class QTimer;
namespace noggit::ui
{
  class server_object_layer
  {
  public:
    server_object_layer(World*, QWidget*);
    ~server_object_layer();
    void load();
    bool save();
    void add(math::vector_3d const&);
    void phase();
    void export_sql();
  private:
    void ensure_catalog();
    void spawn(QJsonObject const&);
    QJsonArray snapshot();
    void report(QString const&);
    World* _world;
    QWidget* _parent;
    QTimer* _timer;
    QString _path, _project;
    QJsonArray _catalog;
    QByteArray _last_saved;
    bool _ready = false;
    bool _error_reported = false;
  };
}
