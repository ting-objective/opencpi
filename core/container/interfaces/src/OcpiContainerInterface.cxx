
/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2010
 *
 *    Mercury Federal Systems, Incorporated
 *    1901 South Bell Street
 *    Suite 402
 *    Arlington, Virginia 22202
 *    United States of America
 *    Telephone 703-413-0781
 *    FAX 703-413-0784
 *
 *  This file is part of OpenCPI (www.opencpi.org).
 *     ____                   __________   ____
 *    / __ \____  ___  ____  / ____/ __ \ /  _/ ____  _________ _
 *   / / / / __ \/ _ \/ __ \/ /   / /_/ / / /  / __ \/ ___/ __ `/
 *  / /_/ / /_/ /  __/ / / / /___/ ____/_/ / _/ /_/ / /  / /_/ /
 *  \____/ .___/\___/_/ /_/\____/_/    /___/(_)____/_/   \__, /
 *      /_/                                             /____/
 *
 *  OpenCPI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenCPI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <signal.h>
#include <OcpiOsAssert.h>
#include <OcpiOsMisc.h>
#include "OcpiUtilCppMacros.h"
#include <OcpiPortMetaData.h>
#include <OcpiLibraryManager.h>
#include <OcpiContainerManager.h>
#include <OcpiContainerInterface.h>
#include <OcpiContainerPort.h>
#include <OcpiContainerApplication.h>
#include <OcpiContainerArtifact.h>
#include "OcpiContainerErrorCodes.h"



namespace OA = OCPI::API;
namespace OU = OCPI::Util;
namespace OS = OCPI::OS;
namespace OL = OCPI::Library;



namespace OCPI {
  namespace Container {

#if 0
    static uint32_t mkUID() {
      static uint32_t id = 1;
      return id++ + getpid();
    }
#endif

    unsigned Container::s_nContainers = 0;
    Container::Container(const char *name, const ezxml_t config, const OCPI::Util::PValue *params)
      throw ( OU::EmbeddedException )
      : //m_ourUID(mkUID()),
      OCPI::Time::Emit("Container", name ),
      m_enabled(false), m_ownThread(true), m_thread(NULL),
      m_transport(*new OCPI::DataTransport::Transport(&Manager::getTransportGlobal(params), false, this))
    {
      OU::SelfAutoMutex guard (this);
      m_ordinal = s_nContainers++;
      if (m_ordinal >= s_maxContainer) {
	Container **old = s_containers;
	s_containers = new Container *[s_maxContainer + 10];
	if (old) {
	  memcpy(s_containers, old, s_maxContainer * sizeof(Container *));
	  delete [] old;
	}
	s_maxContainer += 10;
      }
      s_containers[m_ordinal] = this;
      (void)config; // nothing to parse (yet)
      // FIXME:  this should really be in a baseclass inherited by software containers
      // It works because stuff can be overriden and no threads are created until
      // "start", which is 
      OU::findBool(params, "ownthread", m_ownThread);
      if (getenv("OCPI_NO_THREADS"))
	m_ownThread = false;
      m_os = OCPI_CPP_STRINGIFY(OCPI_OS) + strlen("OCPI");
      m_osVersion = OCPI_CPP_STRINGIFY(OCPI_OS_VERSION);
      m_platform = OCPI_CPP_STRINGIFY(OCPI_PLATFORM);
#if 0
      m_runtime = 0;
      m_runtimeVersion = 0;
#endif
    }

    bool Container::supportsImplementation(OU::Implementation &i) {
      ocpiDebug("supports: %u %s/%s %s/%s %s/%s", m_ordinal,
		m_model.c_str(), i.model().c_str(),
		m_os.c_str(), i.attributes().m_os.c_str(),
		m_platform.c_str(), i.attributes().m_platform.c_str());
      return
	m_model == i.model() &&
	m_os == i.attributes().m_os &&
	m_platform == i.attributes().m_platform;
    }

    Artifact & Container::
    loadArtifact(const char *url, const OA::PValue *artifactParams) {
      // First check if it is loaded on in this container
      // FIXME: canonicalize the URL here?
      Artifact *art = findLoadedArtifact(url);

      if (art)
	return *art;
      // If it is not loaded, let's get it from the library system,
      // and load it ourselves.
      return createArtifact(OL::Manager::getArtifact(url, artifactParams),
			    artifactParams);
    }
    Artifact & Container::
    loadArtifact(OL::Artifact &libArt, const OA::PValue *artifactParams) {
      // First check if it is loaded on in this container
      // FIXME: canonicalize the URL here?
      Artifact *art = findLoadedArtifact(libArt);

      if (art)
	return *art;
      // If it is not loaded, let's get it from the library system,
      // and load it ourselves.
      return createArtifact(libArt, artifactParams);
    }

    // Ultimately there would be a set of "base class" generic properties
    // and the derived class would merge them.
    // FIXME: define base class properties for all apps
    OCPI::Util::PValue *Container::getProperties() {
      return 0;
    }
    OCPI::Util::PValue *Container::getProperty(const char *) {
      return 0;
    }
    // This is for the derived class's destructor to call
    void Container::shutdown() {
      stop(NULL);
      if (m_thread)
	m_thread->join();
    }
    Container::~Container() {
      if (m_thread)
	delete m_thread;
      s_containers[m_ordinal] = 0;
      delete &m_transport;
    }

    //    bool m_start;

    void Container::start(DataTransfer::EventManager* event_manager)
      throw()
    {
      (void)event_manager;
      m_enabled = true;
    }

    void Container::stop(DataTransfer::EventManager* event_manager)
      throw()
    {
      (void)event_manager;
      m_enabled = false;
    }

    Container::DispatchRetCode Container::dispatch(DataTransfer::EventManager*)
    {
      return Container::DispatchNoMore;
    }
    bool Container::run(uint32_t usecs, bool verbose) {
      (void)usecs; (void)verbose;
      if (m_ownThread)
	throw OU::EmbeddedException( OU::CONTAINER_HAS_OWN_THREAD,
				     "Can't use container->run when container has own thread",
				     OU::ApplicationRecoverable);
      return runInternal();
    }

    bool Container::runInternal(uint32_t usecs, bool verbose) {
      if (!m_enabled)
	return false;
      DataTransfer::EventManager *em = getEventManager();
      switch (dispatch(em)) {
      case DispatchNoMore:
	// All done, exit from dispatch thread.
	return false;
	  
      case MoreWorkNeeded:
	// No-op. To prevent blocking the CPU, yield.
	OCPI::OS::sleep (0);
	return true;

      case Stopped:
	// Exit from dispatch thread, it will be restarted.
	return false;
	break;

      case Spin:
	/*
	 * If we have an event manager, ask it to go to sleep and wait for
	 * an event.  If we are not event driven, the event manager will
	 * tell us that it is spinning.  In that case, yield to give other
	 * threads a chance to run.
	 */
	if (em &&
	    em->waitForEvent(usecs) == DataTransfer::EventTimeout && verbose)
	  printf("Timeout after %u usecs waiting for event\n", usecs);
	OCPI::OS::sleep (0);
      }
      return true;
    }
    // This will be called inside a separate thread for this container.
    void Container::thread() {
      while (m_enabled && runInternal())
	;
    }
    void Container::stop() {
      stop(getEventManager());
      m_enabled = false;
    }
    void runContainer(void*arg) {

      // Disable signals on these background threads.
      // Any non-exception/non-inline signal handling should be in the main program.
      // (signals originating outside the program)
      // FIXME: we need a good theory here about how signals in container threads
      // should be handled and/or controlled.  For now we just know that container
      // threads don't know what the control app wants from signals so it stays away
      // FIXME: use OcpiOs for this
      sigset_t set;
      sigemptyset(&set);
      static int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGKILL, SIGTERM, SIGSTOP, SIGTSTP,
			   SIGCHLD, 0};
      for (int *sp = sigs; *sp; sp++)
	sigaddset(&set, *sp);
      ocpiCheck(pthread_sigmask(SIG_BLOCK, &set, NULL) == 0);
      try {
	((Container *)arg)->thread();
      } catch (const std::string s) {
	std::cerr << "Container background thread exception: " << s << std::endl;
	throw;
      } catch (...) {
	std::cerr << "Container background thread unknown exception" << std::endl;
	throw;
      }
    }
    void Container::start() {
      if (!m_enabled) {
	m_enabled = true;
	if (!m_thread && m_ownThread && needThread()) {
	  m_thread = new OCPI::OS::ThreadManager;
	  m_thread->start(runContainer, (void*)this);
	}
	start(getEventManager());
      }
    }
    Container **Container::s_containers;
    unsigned Container::s_maxContainer;
    Container &Container::nthContainer(unsigned n) {
      if (!s_containers[n])
	throw OU::Error("Missing container %u", n);
      if (n >= s_maxContainer)
	throw OU::Error("Invalid container %u", n);
      return *s_containers[n];
    }
  }
  namespace API {
    Container::~Container(){}
  }
}
