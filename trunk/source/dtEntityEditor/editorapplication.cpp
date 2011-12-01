/*
* dtEntity Game and Simulation Engine
*
* This library is free software; you can redistribute it and/or modify it under
* the terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; either version 2.1 of the License, or (at your option)
* any later version.
*
* This library is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
* details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this library; if not, write to the Free Software Foundation, Inc.,
* 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*
* Martin Scheffler
*/

#include <dtEntityEditor/editorapplication.h>

#include <assert.h>
#include <dtEntity/applicationcomponent.h>
#include <dtEntity/basemessages.h>
#include <dtEntity/cameracomponent.h>
#include <dtEntity/componentpluginmanager.h>
#include <dtEntity/entity.h>
#include <dtEntity/entitymanager.h>
#include <dtEntity/initosgviewer.h>
#include <dtEntity/layerattachpointcomponent.h>
#include <dtEntity/mapcomponent.h>
#include <dtEntityEditor/editormainwindow.h>
#include <dtEntityQtWidgets/messages.h>
#include <dtEntityQtWidgets/osggraphicswindowqt.h>
#include <dtEntityQtWidgets/osgadapterwidget.h>
#include <dtEntityWrappers/wrappers.h>
#include <dtEntityWrappers/scriptcomponent.h>
#include <osgViewer/GraphicsWindow>
#include <osg/MatrixTransform>
#include <osgViewer/View>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <GL/gl.h>
#include <iostream>
#include <QtCore/QDir>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>

namespace dtEntityEditor
{

   ////////////////////////////////////////////////////////////////////////////////
   EditorApplication::EditorApplication(int argc, char *argv[])
      : mMainWindow(NULL)
      , mTimer(NULL)
      , mEntityManager(new dtEntity::EntityManager())
      , mStartOfFrameTick(osg::Timer::instance()->tick())
      , mTimeScale(1)
   {

      // default plugin dir
      mPluginPaths.push_back("dteplugins");

      osg::ArgumentParser arguments(&argc,argv);
      mViewer = new osgViewer::Viewer(arguments);
      mViewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);

      //osgViewer::View* v = new osgViewer::View();
      //mViewer->addView(v);
      //v->setUpViewInWindow(100,100,800,600);
      dtEntity::InitOSGViewer(argc, argv, mViewer, mEntityManager, false, false);

      dtEntity::MapSystem* ms;
      mEntityManager->GetEntitySystem(dtEntity::MapComponent::TYPE, ms);
      dtEntityQtWidgets::RegisterMessageTypes(ms->GetMessageFactory());
   }

   ////////////////////////////////////////////////////////////////////////////////
   EditorApplication::~EditorApplication()
   {
      if(mTimer)
      {
         mTimer->stop();
         mTimer->deleteLater();
      }
   }

   ////////////////////////////////////////////////////////////////////////////////
   dtEntity::EntityManager& EditorApplication::GetEntityManager() const
   {
      return *mEntityManager;
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::SetMainWindow(EditorMainWindow* mw)
   {

      assert(mMainWindow == NULL);
      mMainWindow = mw;
      
      connect(mMainWindow, SIGNAL(Closed(bool)), this, SLOT(ShutDownGame(bool)));
      connect(mMainWindow, SIGNAL(ViewResized(const QSize&)), this, SLOT(ViewResized(const QSize&)));
      connect(this, SIGNAL(SceneLoaded(const QString&)), mMainWindow, SLOT(OnSceneLoaded(const QString&)));


   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::SetAdditionalPluginPath(const QString &path, bool bBeforeDefaultPath)
   {
      if(bBeforeDefaultPath)
         mPluginPaths.insert(mPluginPaths.begin(), path.toStdString());
      else
         mPluginPaths.push_back(path.toStdString());
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::StartGame(const QString& sceneToLoad)
   {
      assert(mMainWindow != NULL);
      
      osgViewer::ViewerBase::Windows wins;
      mViewer->getWindows(wins);


      dtEntityQtWidgets::OSGGraphicsWindowQt* osgGraphWindow =
            dynamic_cast<dtEntityQtWidgets::OSGGraphicsWindowQt*>(wins.front());


      if(osgGraphWindow->thread() == thread())
      {
         mMainWindow->SetOSGWindow(osgGraphWindow);
      }
      else
      {
         QMetaObject::invokeMethod(mMainWindow, "SetOSGWindow",  Qt::BlockingQueuedConnection,
            Q_ARG(dtEntityQtWidgets::OSGGraphicsWindowQt*, osgGraphWindow));
      }

      try
      { 

         mTimer = new QTimer(this);
         mTimer->setInterval(10);
         connect(mTimer, SIGNAL(timeout()), this, SLOT(StepGame()), Qt::QueuedConnection);

         mTimer->start();

         connect(this, SIGNAL(ErrorOccurred(const QString&)),
                 mMainWindow, SLOT(OnDisplayError(const QString&)));

         dtEntity::MapSystem* mapSystem;
         mEntityManager->GetEntitySystem(dtEntity::MapComponent::TYPE, mapSystem);
         for(unsigned int i = 0; i < mPluginPaths.size(); ++i) 
         {
            LOG_DEBUG("Looking for plugins in directory " + mPluginPaths[i]);
            // load and start all entity systems in plugins
            mapSystem->GetPluginManager().LoadPluginsInDir(mPluginPaths[i]);
         }         
      }
      catch(const std::exception& e)
      {
         emit(ErrorOccurred(QString("Error starting application: %1").arg(e.what())));
         LOG_ERROR("Error starting application:" + std::string(e.what()));
      }
      catch(...)
      {
         emit(ErrorOccurred("Unknown error starting application"));
         LOG_ERROR("Unknown error starting application");
      }

      InitializeScripting();

      if(sceneToLoad != "")
      {
         LoadScene(sceneToLoad);
      }

   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::StepGame()
   {
      if(!mViewer->done())
      {
         mViewer->frame();
      }
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::ShutDownGame(bool)
   {
      mViewer->setDone(true);
      
      if(mTimer)
      {
         mTimer->stop();
         mTimer->deleteLater();
         mTimer = NULL;
      }
      
      QMetaObject::invokeMethod(mMainWindow, "ShutDown", Qt::QueuedConnection);
      QThread::currentThread()->quit();
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::ViewResized(const QSize& size)
   {
      osgViewer::ViewerBase::Windows wins;
      mViewer->getWindows(wins);
      wins.front()->resized(0, 0, size.width(), size.height());
      wins.front()->getEventQueue()->windowResize(0, 0, size.width(), size.height());

      osgViewer::ViewerBase::Cameras cams;
      mViewer->getCameras(cams);
      cams.front()->setViewport(new osg::Viewport(0, 0, size.width(), size.height()));
   }

   ////////////////////////////////////////////////////////////////////////////////
   QStringList EditorApplication::GetDataPaths() const
   {
      QStringList out;
      osgDB::FilePathList paths = osgDB::getDataFilePathList();
      for(osgDB::FilePathList::iterator i = paths.begin(); i != paths.end(); ++i)
      {
         out.push_back(i->c_str());
      }
      return out;
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::SetDataPaths(const QStringList& paths)
   {

      osgDB::FilePathList in;

      for(QStringList::const_iterator i = paths.begin(); i != paths.end(); ++i)
      {
         QString path = *i;
         if(!QFile::exists(path))
         {
            LOG_ERROR("Project assets folder does not exist: " + path.toStdString());
         }
         else
         {
            in.push_back(path.toStdString());
         }
      }

      osgDB::setDataFilePathList(in);

      QSettings settings;
      settings.setValue("DataPaths", paths);
      emit DataPathsChanged(paths);
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::LoadScene(const QString& path)
   {  
      
      dtEntity::MapSystem* mapSystem;
      GetEntityManager().GetEntitySystem(dtEntity::MapComponent::TYPE, mapSystem);

      if(mapSystem->GetCurrentScene().size() != 0)
      {
         mapSystem->UnloadScene();
      }

      mapSystem->LoadScene(path.toStdString());

      std::string cameramapname = "maps/default.dtemap";
      if(!mapSystem->GetLoadedMaps().empty())
      {
         cameramapname = mapSystem->GetLoadedMaps().front();
      }

      // create a main camera entity if it was not loaded from map
      dtEntity::ApplicationSystem* appsys;
      GetEntityManager().GetEntitySystem(dtEntity::ApplicationSystem::TYPE, appsys);

      dtEntity::Entity* entity;
      mEntityManager->CreateEntity(entity);

      unsigned int contextId = appsys->GetPrimaryWindow()->getState()->getContextID();
      dtEntity::CameraComponent* camcomp;
      entity->CreateComponent(camcomp);
      camcomp->SetContextId(contextId);
      camcomp->SetClearColor(osg::Vec4(0,0,0,1));
      camcomp->Finished();

      dtEntity::MapComponent* mapcomp;
      entity->CreateComponent(mapcomp);
      std::ostringstream os;
      os << "cam_" << contextId;
      mapcomp->SetEntityName(os.str());
      mapcomp->SetUniqueId(os.str());
      mapcomp->SetMapName("maps/camera.dtemap");
      mapcomp->Finished();
      GetEntityManager().AddToScene(entity->GetId());

      emit SceneLoaded(path);

   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::AddScene(const QString& name)
   {

      dtEntity::MapSystem* mapSystem;
      GetEntityManager().GetEntitySystem(dtEntity::MapComponent::TYPE, mapSystem);

      mapSystem->UnloadScene();
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::SaveScene(const QString& path)
   {
      dtEntity::MapSystem* mapSystem;
      GetEntityManager().GetEntitySystem(dtEntity::MapComponent::TYPE, mapSystem);
      mapSystem->SaveScene(path.toStdString(), false);
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::SaveAll(const QString& path)
   {
      dtEntity::MapSystem* mapSystem;
      GetEntityManager().GetEntitySystem(dtEntity::MapComponent::TYPE, mapSystem);
      mapSystem->SaveScene(path.toStdString(), true);
   }

   ////////////////////////////////////////////////////////////////////////////////
   void EditorApplication::InitializeScripting()
   {
      //DtScript needs to get key events for all views. Add Script component
      // as a keyboard / mouse event recipient

      dtEntityWrappers::ScriptSystem* ss;
      if(!GetEntityManager().GetEntitySystem(dtEntityWrappers::ScriptSystem::TYPE, ss))
      {
         ss = new dtEntityWrappers::ScriptSystem(GetEntityManager());
         GetEntityManager().AddEntitySystem(*ss);
      }
      
      std::string source = ""
          "include_once(\"Scripts/osgveclib.js\");\n"
          "include_once(\"Scripts/stdlib.js\");\n"
          "include_once(\"Scripts/editormotionmodel.js\");\n"
          "include_once(\"Scripts/manipulators.js\");\n";

      ss->ExecuteScript(source);

   }
}
