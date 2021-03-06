#ifndef OCPI_LIBRARY_MANAGER_H
#define OCPI_LIBRARY_MANAGER_H
/*
 * Notes:
 * Should a given library should be tagged as to whether its implementations should be combined with others.
 * Issue of stronger naming on specs - like class names or UUIDs
 *
 */
#include <map>
#include "ezxml.h"
#include "OcpiUtilMisc.h"
#include "OcpiUtilEzxml.h"
#include "OcpiUtilUUID.h"
#include "OcpiDriverManager.h"
#include "OcpiUtilImplementation.h"
#include "OcpiLibraryApi.h"

namespace OCPI {
  namespace Library {
    using OCPI::Util::Child;
    using OCPI::Util::Parent;
    using OCPI::Util::PValue;

    class Driver;   // base class of all library drivers

    // This structure describes the capabilities of a container
    // as it relates to looking at an artifact's metadata to see if 
    // implementations in the artifact can in fact run on that
    // container.  It is defined here in the library facility (and not
    // in the container facility) since it is indeed a toss-up, but
    // this allows thie library facility to not depend on containers.
    // Eventually we will extended to multiple supported aspects
    // (like multiple compiler versions or something).
    struct Capabilities {
      std::string m_model;
      std::string m_os;
      std::string m_osVersion;
      std::string m_platform;
      std::string m_runtime;
      std::string m_runtimeVersion;
    };

    class Implementation;
    struct Connection {
      Implementation *impl;
      OCPI::Util::Port *port;
    };
    class Artifact;
    // This object represents a potentially usable implementation in an artifact.
    // And in any artifact, there may be multiple static instances of the implementation,
    // that may have fixed connectivity with other implementations.
    class Implementation {
    public:
      Artifact &m_artifact;                 // FIXME this can be parent/child
      // This is the metadata description of the implementation, whether static instance or not
      OCPI::Util::Implementation &m_metadataImpl;
      ezxml_t m_staticInstance;             // static instances of this implementation
      OCPI::Util::Port::Mask m_externals, m_internals;
      Connection *m_connections;
      unsigned m_ordinal;                   // ordinal of worker within artifact
      Implementation(Artifact &art, OCPI::Util::Implementation &i, ezxml_t instance, unsigned ordinal);
      // Does this implementation satify the selection criteria?  and if so, what is the score?
      //      bool satisfiesSelection(const char *selection, unsigned &score);
      bool getValue(const std::string &symbol, OCPI::Util::ExprValue &val);
      void setConnection(OCPI::Util::Port &myPort, Implementation *otherImpl = NULL,
			 OCPI::Util::Port *otherPort = NULL);
    };

    // Note due to xml persistence we don't need strings in the map
    // but this multimap stuff is pretty ugly
    typedef std::multimap<const char *, Implementation *, OCPI::Util::ConstCharComp > WorkerMap;
    typedef std::pair< const char*, Implementation *> WorkerMapPair;
    typedef WorkerMap::const_iterator WorkerIter;
    typedef std::pair<WorkerIter,WorkerIter> WorkerRange;
    class Artifact : public OCPI::Util::Attributes {
    protected:
      ezxml_t m_xml;
      // A count and array of implementations found in the artifact, *not* static instances.
      unsigned m_nImplementations;
      OCPI::Util::Implementation *m_metaImplementations; // this array 
      // A map for implementations (*including* static instances) in this artifact
      // Used for artifact-by-artifact searches (FIXME: obsolete?)
      WorkerMap m_workers;      // Map from spec name to implementations
      // A count of static instances added to the worker map (m_workers)
      unsigned m_nWorkers;
      Artifact();
      virtual ~Artifact();
      Implementation *addImplementation(OCPI::Util::Implementation &metaImpl, ezxml_t staticInstance);
    public:
      void configure(ezxml_t x = NULL);
      //      bool evaluateWorkerSuitability( const OCPI::API::PValue *p, unsigned & score );
      // Can this artifact run on something with these capabilities?
      virtual bool meetsRequirements (const Capabilities &caps,
				      const char *impl,
				      const OCPI::API::PValue *props,
				      const char *selectCriteria,
				      const OCPI::API::Connection *conns,
				      const char *&inst,
				      unsigned & score
				      );
      inline ezxml_t xml() const { return m_xml; }
      virtual const std::string &name() const = 0;
      virtual Artifact *nextArtifact() = 0;
      static char *getMetadata(const char *name);
    };

    // This class is what is used when looking for implementations.
    // It is called back on implementations that are suitable.
    // It returns true if the search should stop.
    class ImplementationCallback {
    protected:
      virtual ~ImplementationCallback(){};
    public:
      // Return true if we found THE ONE.
      // Set accepted = true, if we actually accepted one
      virtual bool foundImplementation(const Implementation &i, unsigned score, bool &accepted) = 0;
    };

    // The manager/owner of all library drivers
    extern const char *library;
    class Manager : public OCPI::Driver::ManagerBase<Manager, Driver, library> {
      std::string m_libraryPath;
      WorkerMap m_implementations;
      friend class OCPI::API::LibraryManager;
      Artifact &getArtifactX(const char *url, const OCPI::API::PValue *props);
      Artifact &findArtifactX(const Capabilities &caps,
			      const char *impl,
			      const OCPI::API::PValue *props,
			      const char *selectCriteria,
			      const OCPI::API::Connection *conns,
			      const char *&inst);
      void setPath(const char *);
      // return true if present
      //      bool getPlatform(const char *name, unsigned &platform);
      //      unsigned addPlatform(const char *name);
    protected:
    public:
      Manager();
      std::string &getPath();
      // Ask the library manager to get this artifact.
      // It might be in a library, and if not it needs to be established,
      // in a "pseudo library" for artifacts that are specifically loaded.
      // The properties can be used to affect the establishment, or check 
      // against aspects of artifacts that are already established.
      static Artifact &getArtifact(const char *url,
				   const OCPI::API::PValue *props = 0);
      // Find an artifact for a worker on a container
      // First, container capabilities (in a form established by this
      // library facility), then worker info.
      static Artifact &findArtifact(const Capabilities &caps,
				    const char *specName,
				    const OCPI::API::PValue *props,
				    const char *selectCriteria, 
				    const OCPI::API::Connection *conns,
				    const char *&inst);
      // Inform the manager about an implementation
      void addImplementation(Implementation &imp);
    private:
      // Find (and callback with) implementations for specName and selectCriteria
      // Return true if any were found
      bool findImplementationsX(ImplementationCallback &icb, const char *specName,
				const char *selectCriteria);
    public:
      inline static bool findImplementations(ImplementationCallback &icb, const char *specName,
					     const char *selectCriteria) {
	return getSingleton().findImplementationsX(icb, specName, selectCriteria);
      }
      // Find one good implementation, return true one is found that satisfies the criteria
      bool findImplementation(const char *specName, const char *selectCriteria, const Implementation *&impl);
    };
    class Library;
    // This is the base class for all library drivers
    class Driver : public OCPI::Driver::DriverType<Manager,Driver> {
      //      virtual Library *findLibrary(const char *url) = 0;
    protected:
      Driver(const char *);
    public:
      virtual Library *firstLibrary() = 0;
      virtual Artifact *findArtifact(const char *url) = 0;
      // Return NULL if the artifact is not supported by the driver
      virtual Artifact *addArtifact(const char *url,
				    const OCPI::API::PValue *props) = 0;
      Artifact *findArtifact(const Capabilities &caps,
			     const char *impl,
			     const OCPI::API::PValue *props,
			     const char *selectCriteria,
			     const OCPI::API::Connection *conns,
			     const char *&inst);
    };
    // This is the template inherited by concrete library drivers
    template <class ConcDri, class ConcreteLibrary, const char *&name>
    class DriverBase :
      public OCPI::Driver::DriverBase
      <Manager, Driver, ConcDri, ConcreteLibrary, name>
    {
      inline Library *firstLibrary() {
	return
	  OCPI::Driver::
	  DriverBase<Manager,Driver,ConcDri,ConcreteLibrary, name>::
	  firstDevice();
      }
#if 0
      virtual Library *findLibrary(const char *url) {
	return Parent<ConcreteLibrary>::findChildByName(url);
      }
#endif
      virtual Artifact *findArtifact(const char *url) {
	for (ConcreteLibrary *l = Parent<ConcreteLibrary>::firstChild();
	     l; l = l->nextChild())
	  return l->findArtifact(url);
	return NULL;
      }
    };
    // This is the base class for all libraries for all drivers.
    class Library {
    protected:
      virtual ~Library();
    public:
      void findImplementations(ImplementationCallback &icb, const char *specName,
			       const char *selectCriteria);
      Artifact *
      findArtifact(const Capabilities &caps,
		   const char *specName,
		   const OCPI::API::PValue *props,
		   const char *selectCriteria,
		   const OCPI::API::Connection *conns,
		   const char *&inst);
      virtual Artifact *firstArtifact() = 0;
      virtual Library *nextLibrary() = 0;
    };
    // The template class inherited by all concrete libraries.
    template <class Dri, class Lib, class Art>
    class LibraryBase :
      public OCPI::Driver::DeviceBase<Dri,Lib>,
      public OCPI::Util::Parent<Art>,
      public Library
    {
    public:
      inline Artifact *firstArtifact() { return Parent<Art>::firstChild(); }
      inline Library *nextLibrary() {
	return OCPI::Driver::DeviceBase<Dri,Lib>::nextDevice();
      }
      inline Artifact *findArtifact(const char *url) {
	return Parent<Art>::findChildByName(url);
      }
    protected:
      LibraryBase<Dri, Lib, Art>(Lib &lib, const char *childName)
	: OCPI::Driver::DeviceBase<Dri, Lib>(childName, lib) {}
    };

    // This is the template inherited by concrete artifacts
    template <class Lib, class Art>
    class ArtifactBase :
      public OCPI::Util::Child<Lib, Art>,
      public Artifact
    {
    protected:
      ArtifactBase<Lib, Art>(Lib &lib, Art &art, const char *name)
	: OCPI::Util::Child<Lib,Art>(lib, art, name) {}
    public:
      inline Artifact *nextArtifact() {
	return OCPI::Util::Child<Lib,Art>::nextChild();
      }
      inline const std::string &name() const {
	return OCPI::Util::Child<Lib,Art>::name();
      }
    };
    template <class Dri>
    class RegisterLibraryDriver : public OCPI::Driver::Registration<Dri>{};
  }
}
#endif
