// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/AsyncLoader.h>
#include <noggit/DBC.h>
#include <noggit/Log.h>
#include <noggit/MPQ.h>
#include <noggit/MapView.h>
#include <noggit/Model.h>
#include <noggit/ModelManager.h> // ModelManager::report()
#include <noggit/TextureManager.h> // TextureManager::report()
#include <noggit/WMO.h> // WMOManager::report()
#include <noggit/errorHandling.h>
#include <noggit/liquid_layer.hpp>
#include <noggit/settings.hpp>
#include <noggit/ui/main_window.hpp>
#include <opengl/context.hpp>
#include <util/exception_to_string.hpp>

#include <filesystem>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <list>
#include <string>
#include <thread>
#include <vector>

#include <QtCore/QTimer>
#include <QtGui/QOffscreenSurface>
#include <QtOpenGL/QGLFormat>
#include <QtCore/QDir>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>

#include "revision.h"

class Noggit
{
public:
  Noggit (int argc, char *argv[]);

private:
  void initPath(char *argv[]);
  void loadMPQs();

  std::unique_ptr<noggit::ui::main_window> main_window;

  std::filesystem::path wowpath;

  bool fullscreen;
  bool doAntiAliasing;
};

void Noggit::initPath(char *argv[])
{
  try
  {
    std::filesystem::path startupPath(argv[0]);
    startupPath.remove_filename();

    if (startupPath.is_relative())
    {
      std::filesystem::current_path(std::filesystem::current_path() / startupPath);
    }
    else
    {
      std::filesystem::current_path(startupPath);
    }
  }
  catch (const std::filesystem::filesystem_error& ex)
  {
    LogError << ex.what() << std::endl;
  }
}

namespace
{
  std::string archive_case(std::string const& requested)
  {
    std::filesystem::path p(requested);
    if (!std::filesystem::exists(p.parent_path())) return requested;
    auto name = QString::fromStdString(p.filename().string());
    for (auto const& item : std::filesystem::directory_iterator(p.parent_path()))
      if (QString::fromStdString(item.path().filename().string()).compare(name, Qt::CaseInsensitive) == 0)
        return item.path().string();
    return requested;
  }
}

void Noggit::loadMPQs()
{
  // load project folder listfile first, todo: make that async
  {
    auto const prefix(std::filesystem::path(NoggitSettings.project_path()));
    auto const prefix_size(prefix.string().length());

    if (std::filesystem::exists(prefix))
    {
      for (auto const& entry_abs : std::filesystem::recursive_directory_iterator(prefix))
      {
        gListfile.emplace(
          noggit::mpq::normalized_filename
          (entry_abs.path().string().substr(prefix_size))
        );
      }
    }
  }

  std::vector<std::string> archiveNames;
  archiveNames.push_back("common.MPQ");
  archiveNames.push_back("common-2.MPQ");
  archiveNames.push_back("expansion.MPQ");
  archiveNames.push_back("lichking.MPQ");
  archiveNames.push_back("patch.MPQ");
  archiveNames.push_back("patch-{number}.MPQ");
  archiveNames.push_back("patch-{character}.MPQ");

  //archiveNames.push_back( "{locale}/backup-{locale}.MPQ" );
  //archiveNames.push_back( "{locale}/base-{locale}.MPQ" );
  archiveNames.push_back("{locale}/locale-{locale}.MPQ");
  //archiveNames.push_back( "{locale}/speech-{locale}.MPQ" );
  archiveNames.push_back("{locale}/expansion-locale-{locale}.MPQ");
  //archiveNames.push_back( "{locale}/expansion-speech-{locale}.MPQ" );
  archiveNames.push_back("{locale}/lichking-locale-{locale}.MPQ");
  //archiveNames.push_back( "{locale}/lichking-speech-{locale}.MPQ" );
  archiveNames.push_back("{locale}/patch-{locale}.MPQ");
  archiveNames.push_back("{locale}/patch-{locale}-{number}.MPQ");
  archiveNames.push_back("{locale}/patch-{locale}-{character}.MPQ");

  archiveNames.push_back("development.MPQ");

  const char * locales[] = { "enGB", "enUS", "deDE", "koKR", "frFR", "zhCN", "zhTW", "esES", "esMX", "ruRU" };
  const char * locale("****");

  // Find locale, take first one.
  for (int i(0); i < 10; ++i)
  {
    if (std::filesystem::exists (wowpath / "Data" / locales[i]))
    {
      locale = locales[i];
      NOGGIT_LOG << "Locale: " << locale << std::endl;
      break;
    }
  }
  if (!strcmp(locale, "****"))
  {
    LogError << "Could not find locale directory. Be sure, that there is one containing the file \"realmlist.wtf\"." << std::endl;
    //return -1;
  }


  //! \todo  This may be done faster. Maybe.
  for (size_t i(0); i < archiveNames.size(); ++i)
  {
    std::string path((wowpath / "Data" / archiveNames[i]).string());

    path = misc::replace(path, "{locale}", std::string(locale));

    if (path.find("{number}") != std::string::npos)
    {
      for (char j = '2'; j <= '9'; j++)
      {
        std::string file = archive_case(misc::replace(path, "{number}", std::string(1, j)));

        if (std::filesystem::exists(file))
        {
          MPQArchive::loadMPQ(AsyncLoader::instance, file, true);
        }
      }
    }
    else if (path.find("{character}") != std::string::npos)
    {
      for (char c = 'a'; c <= 'z'; c++)
      {
        std::string file = archive_case(misc::replace(path, "{character}", std::string(1, c)));

        if (std::filesystem::exists(file))
        {
          MPQArchive::loadMPQ(AsyncLoader::instance, file, true);
        }
      }
    }
    else if (std::filesystem::exists(archive_case(path)))
    {
      MPQArchive::loadMPQ(AsyncLoader::instance, archive_case(path), true);
    }
  }
}

namespace
{
  bool is_valid_game_path (const QDir& path)
  {
    if (!path.exists ())
    {
      LogError << "Path \"" << qPrintable (path.absolutePath ())
        << "\" does not exist." << std::endl;
      return false;
    }

    QStringList locales;
    locales << "enGB" << "enUS" << "deDE" << "koKR" << "frFR"
      << "zhCN" << "zhTW" << "esES" << "esMX" << "ruRU";
    QString found_locale ("****");

    for (auto const& locale : locales)
    {
      if (path.exists (("Data/" + locale)))
      {
        found_locale = locale;
        break;
      }
    }

    if (found_locale == "****")
    {
      LogError << "Path \"" << qPrintable (path.absolutePath ())
        << "\" does not contain a locale directory "
        << "(invalid installation or no installation at all)."
        << std::endl;
      return false;
    }

    return true;
  }
}

namespace
{
  std::atomic_bool success = false;

  void opengl_context_creation_stuck_failsafe()
  {
    for (int i = 0; i < 50; ++i)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (success.load())
      {
        return;
      }
    }

    LogError << "OpenGL Context creation failed (timeout), closing..." << std::endl;

    std::terminate();
  }
}

Noggit::Noggit(int argc, char *argv[])
  : fullscreen(false)
  , doAntiAliasing(true)
{
  InitLogging();
  assert (argc >= 1); (void) argc;
  initPath(argv);

  NOGGIT_LOG << "Noggit Studio - " << STRPRODUCTVER << std::endl;

#ifdef USE_BINDLESS_TEXTURES
  NOGGIT_LOG << "Bindless textures enabled." << std::endl;
#endif

  AsyncLoader::setup(NoggitSettings.value("async_thread_count", 3).toInt());

  doAntiAliasing = NoggitSettings.value("antialiasing", false).toBool();
  fullscreen = NoggitSettings.value("fullscreen", false).toBool();

  srand(::time(nullptr));
  QDir path (NoggitSettings.value ("project/game_path").toString());

  while (!is_valid_game_path (path))
  {
    auto const new_path (QFileDialog::getExistingDirectory (nullptr, "Open WoW Directory", "/", QFileDialog::ShowDirsOnly));
    if (new_path.isEmpty())
    {
      LogError << "Could not auto-detect game path "
        << "and user canceled the dialog." << std::endl;
      throw std::runtime_error ("Could not auto-detect game path and user canceled the dialog.");
    }
    path = QDir (new_path);
  }

  // save the new game path if it was changed
  NoggitSettings.set_value("project/game_path", path.absolutePath());
  NoggitSettings.sync();

  wowpath = path.absolutePath().toStdString();

  NOGGIT_LOG << "Game path: " << wowpath << std::endl;

  std::string project_path = NoggitSettings.value ("project/path", path.absolutePath()).toString().toStdString();
  NoggitSettings.set_value ("project/path", QString::fromStdString (project_path));

  NOGGIT_LOG << "Project path: " << project_path << std::endl;

  NoggitSettings.set_value ("project/game_path", path.absolutePath());
  NoggitSettings.set_value ("project/path", QString::fromStdString(project_path));

  loadMPQs(); // listfiles are not available straight away! They are async! Do not rely on anything at this point!
  OpenDBs();

  if (!QGLFormat::hasOpenGL())
  {
    throw std::runtime_error ("Your system does not support OpenGL. Sorry, this application can't run without it.");
  }

  QSurfaceFormat format;

  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(4, 1);
  format.setProfile(QSurfaceFormat::CoreProfile);

  format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);

  bool vsync = NoggitSettings.value("vsync", true).toBool();

  format.setSwapInterval(vsync ? 1 : 0);

  if (doAntiAliasing)
  {
    format.setSamples (4);
  }

  // context creation seems to get stuck sometimes, this ensure the app is killed
  // otherwise it's wasting cpu resources and is annoying when developping
  auto failsafe = std::async(&opengl_context_creation_stuck_failsafe);

  QSurfaceFormat::setDefaultFormat (format);

  QOpenGLContext context;
  context.create();
  QOffscreenSurface surface;
  surface.create();
  context.makeCurrent (&surface);

  success = true;

  opengl::context::scoped_setter const _ (::gl, &context);

  LogDebug << "GL: Version: " << gl.getString (GL_VERSION) << std::endl;
  LogDebug << "GL: Vendor: " << gl.getString (GL_VENDOR) << std::endl;
  LogDebug << "GL: Renderer: " << gl.getString (GL_RENDERER) << std::endl;

  // ensure all MPQs are loaded fully so the listfile is complete
  AsyncLoader::instance->wait_queue_empty();

  main_window = std::make_unique<noggit::ui::main_window>();
  if (fullscreen)
  {
    main_window->showFullScreen();
  }
  else
  {
    main_window->showMaximized();
  }
}

namespace
{
  void noggit_terminate_handler()
  {
    std::string const reason
      {util::exception_to_string (std::current_exception())};

    if (qApp)
    {
      QMessageBox::critical ( nullptr
                            , "std::terminate"
                            , QString::fromStdString (reason)
                            , QMessageBox::Close
                            , QMessageBox::Close
                            );
    }

    LogError << "std::terminate: " << reason << std::endl;
  }

  struct application_with_exception_printer_on_notify : QApplication
  {
    using QApplication::QApplication;

    virtual bool notify (QObject* object, QEvent* event) override
    {
      try
      {
        return QApplication::notify (object, event);
      }
      catch (...)
      {
        std::terminate();
      }
    }
  };
}

int main(int argc, char *argv[])
{
  noggit::RegisterErrorHandlers();
  std::set_terminate (noggit_terminate_handler);

  QApplication qapp (argc, argv);
  qapp.setApplicationName ("Noggit");
  qapp.setOrganizationName ("Noggit");

  Noggit app (argc, argv);

  return qapp.exec();
}
