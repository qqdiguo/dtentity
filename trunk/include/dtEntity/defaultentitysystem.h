#pragma once

/* -*-c++-*-
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


#include <dtEntity/entity.h>
#include <dtEntity/entitysystem.h>
#include <dtEntity/dtentity_config.h>

#include <dtEntity/entitymanager.h>
#include <dtEntity/log.h>
#include <assert.h>

#if USE_BOOST_POOL
#include <boost/pool/object_pool.hpp>
#include <boost/pool/pool.hpp>
#endif

#if defined(_MSC_VER) && (_MSC_VER >=1500)
#   include <unordered_map>
#else
#   ifdef __APPLE__
#      include <ext/hash_map>
#   else
#      include <hash_map>
#   endif
#endif


namespace dtEntity
{

   ////////////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER) && (_MSC_VER >=1500)

   template<class T>
   class ComponentStoreMap
      : public std::tr1::unordered_map<EntityId, T*>
   {
   };
#elif defined(__GNUG__)
   template<class T>
   class ComponentStoreMap
      : public __gnu_cxx::hash_map<EntityId, T*>
   {
   };
#else
   template<class T>
   class ComponentStoreMap
      : public std::map<EntityId, T*>
   {
   };
#endif

   ////////////////////////////////////////////////////////////////////////////////
   template<class T>
   struct MemAllocPolicyNew
   {
      static T* Create()
      {
         return new T;
      }

      static void Destroy(T* t)
      {
         delete t;
      }

      static void DestroyAll(ComponentStoreMap<T>& components)
      {
         for(typename ComponentStoreMap<T>::iterator i = components.begin(); i != components.end(); ++i)
         {
            delete i->second;
         }
      }

   protected:
      ~MemAllocPolicyNew() {}
   };


   ////////////////////////////////////////////////////////////////////////////////
#if USE_BOOST_POOL
   template<class T>
   struct MemAllocPolicyBoostPool
   {
      static T* Create()
      {
         return mComponentPool->construct();
      }

      static void Destroy(T* t)
      {
         mComponentPool->destroy(t);
      }

      static void DestroyAll(ComponentStoreMap<T>& components)
      {
         delete mComponentPool;
      }

   protected:
      ~MemAllocPolicyBoostPool() {}

   private:
      boost::object_pool<T>* mComponentPool;
   };
#endif

   ////////////////////////////////////////////////////////////////////////////////
   /**
    * A simple base for an entity system that handles component allocation
    * and deletion.
    * Uses an unordered map for component storage
    */
   template<typename T, template<class> class MemAllocPolicy = MemAllocPolicyNew>
   class DefaultEntitySystem
      : public EntitySystem
      , public MemAllocPolicy<T>
   {
   public:

      typedef ComponentStoreMap<T> ComponentStore;

      DefaultEntitySystem(EntityManager& em, ComponentType baseType = StringId())
         : EntitySystem(em, baseType)
         , mComponentType(T::TYPE)
      {
      }

      ~DefaultEntitySystem()
      {
         MemAllocPolicy<T>::DestroyAll(mComponents);
      }

      virtual ComponentType GetComponentType() const;

      virtual bool HasComponent(EntityId eid) const;

      T* GetComponent(EntityId eid);
      const T* GetComponent(EntityId eid) const;

      virtual bool GetComponent(EntityId eid, Component*& c);
      virtual bool GetComponent(EntityId eid, const Component*& c) const;

      virtual bool CreateComponent(EntityId eid, Component*& component);
      virtual bool DeleteComponent(EntityId eid);

      virtual void GetEntitiesInSystem(std::list<EntityId>& toFill) const;

      unsigned int GetNumComponents() const;

      virtual GroupProperty GetComponentProperties() const;

      typename ComponentStore::iterator begin();
      typename ComponentStore::const_iterator begin() const;

      typename ComponentStore::iterator end();
      typename ComponentStore::const_iterator end() const;

   protected:

      ComponentStore mComponents;
      ComponentType mComponentType;
   };



   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      ComponentType DefaultEntitySystem<T, MemAllocPolicy>::GetComponentType() const
   {
      return mComponentType;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      bool DefaultEntitySystem<T, MemAllocPolicy>::HasComponent(EntityId eid) const
   {
      typename ComponentStore::const_iterator i = mComponents.find(eid);
      return(i != mComponents.end());
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      T* DefaultEntitySystem<T, MemAllocPolicy>::GetComponent(EntityId eid)
   {
      typename ComponentStore::iterator i = mComponents.find(eid);
      if(i != mComponents.end())
      {
         return i->second;
      }
      return NULL;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      const T* DefaultEntitySystem<T, MemAllocPolicy>::GetComponent(EntityId eid) const
   {
      typename ComponentStore::const_iterator i = mComponents.find(eid);
      if(i != mComponents.end())
      {
         return i->second;
      }
      return NULL;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      bool DefaultEntitySystem<T, MemAllocPolicy>::GetComponent(EntityId eid, Component*& c)
   {
      typename ComponentStore::iterator i = mComponents.find(eid);
      if(i != mComponents.end())
      {
         c = i->second;
         return true;
      }
      return false;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      bool DefaultEntitySystem<T, MemAllocPolicy>::GetComponent(EntityId eid, const Component*& c) const
   {
      typename ComponentStore::const_iterator i = mComponents.find(eid);
      if(i != mComponents.end())
      {
         c = i->second;
         return true;
      }
      return false;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      bool DefaultEntitySystem<T, MemAllocPolicy>::CreateComponent(EntityId eid, Component*& component)
   {
      if(HasComponent(eid))
      {
         LOG_ERROR("Could not create component: already exists!");
         return false;
      }

      T* t = MemAllocPolicy<T>::Create();

      if(t == NULL)
      {
         LOG_ERROR("Out of memory!");
         return false;
      }
      component = t;
      mComponents[eid] = t;

      Entity* e;
      bool found = GetEntityManager().GetEntity(eid, e);
      if(!found)
      {
         LOG_ERROR("Can't add component to entity: entity does not exist!");
         return false;
      }
      component->OnAddedToEntity(*e);
      return true;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      bool DefaultEntitySystem<T, MemAllocPolicy>::DeleteComponent(EntityId eid)
   {
      typename ComponentStore::iterator i = mComponents.find(eid);
      if(i == mComponents.end())
         return false;
      T* component = i->second;
      Entity* e;
      bool found = GetEntityManager().GetEntity(eid, e);
      assert(found);
      component->OnRemovedFromEntity(*e);
      mComponents.erase(i);
      MemAllocPolicy<T>::Destroy(component);
      return true;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      void DefaultEntitySystem<T, MemAllocPolicy>::GetEntitiesInSystem(std::list<EntityId>& toFill) const
   {
      typename ComponentStore::const_iterator i = mComponents.begin();
      for(;i != mComponents.end(); ++i)
      {
         toFill.push_back(i->first);
      }
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      unsigned int DefaultEntitySystem<T, MemAllocPolicy>::GetNumComponents() const
   {
      return mComponents.size();
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      GroupProperty DefaultEntitySystem<T, MemAllocPolicy>::GetComponentProperties() const
   {
      T t;
      return t;
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      typename ComponentStoreMap<T>::iterator DefaultEntitySystem<T, MemAllocPolicy>::begin()
   {
      return mComponents.begin();
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      typename ComponentStoreMap<T>::const_iterator DefaultEntitySystem<T, MemAllocPolicy>::begin() const
   {
      return mComponents.begin();
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      typename ComponentStoreMap<T>::iterator DefaultEntitySystem<T, MemAllocPolicy>::end()
   {
      return mComponents.end();
   }

   ////////////////////////////////////////////////////////////////////////////////
   template<typename T, template<class> class MemAllocPolicy>
      typename ComponentStoreMap<T>::const_iterator DefaultEntitySystem<T, MemAllocPolicy>::end() const
   {
      return mComponents.end();
   }
}

