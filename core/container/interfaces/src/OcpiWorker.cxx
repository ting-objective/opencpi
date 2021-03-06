
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

#include "OcpiOsMisc.h"
#include "OcpiUtilAutoMutex.h"
#include "OcpiContainerApplication.h"
#include "OcpiContainerErrorCodes.h"
#include "OcpiWorker.h"
#include "OcpiContainerPort.h"
#include "OcpiContainerInterface.h"
#include "OcpiContainerMisc.h"
#include "OcpiContainerArtifact.h"
#include "OcpiUtilValue.h"

namespace OA = OCPI::API;
namespace OU = OCPI::Util;
namespace OM = OCPI::Metadata;
namespace OCPI {
  namespace Container {
    Controllable::Controllable()
      : m_state(EXISTS), m_controlMask(0) {
    }
    void Controllable::setControlOperations(const char *ops) {
      if (ops) {
#define CONTROL_OP(x, c, t, s1, s2, s3, s4)		\
	if (strstr(ops, #x))				\
	  m_controlMask |= 1 << OU::Op##c;
	OCPI_CONTROL_OPS
#undef CONTROL_OP
      }
    }

    // Due to class hierarchy issues..
    Worker::Worker(Artifact *art, ezxml_t impl, ezxml_t inst, const OA::PValue *) 
      : OM::Worker::Worker(impl),
	m_artifact(art), m_xml(impl), m_instXml(inst), m_workerMutex(true)
    {
      setControlOperations(ezxml_cattr(impl, "controlOperations"));
      if (impl)
	m_implTag = ezxml_cattr(impl, "name");
      if (inst)
	m_instTag = ezxml_cattr(inst, "name");
    }

    Worker::~Worker()
    {
      if (m_artifact)
	m_artifact->removeWorker(*this);
    }

    OA::Port &Worker::
    getPort(const char *name, const OA::PValue *props ) {
      Port *p = findPort(name);
      if (p)
        return *p;
      OM::Port *metaPort = findMetaPort(name);
      if (!metaPort)
        throw ApiError("no port found with name \"", name, "\"", NULL);
      return createPort(*metaPort, props);
    }
    OA::PropertyInfo & Worker::setupProperty(const char *name, 
					     volatile void *&m_writeVaddr,
					     const volatile void *&m_readVaddr) {
      OU::Property &prop = findProperty(name);
      prepareProperty(prop, m_writeVaddr, m_readVaddr);
      return prop;
    }
    OA::PropertyInfo & Worker::setupProperty(unsigned n, 
					     volatile void *&m_writeVaddr,
					     const volatile void *&m_readVaddr) {
      OU::Property &prop = property(n);
      prepareProperty(prop, m_writeVaddr, m_readVaddr);
      return prop;
    }
    // Internal used by others.
    void Worker::setPropertyValue(const OA::Property &prop, const OU::Value &v) {
      OA::PropertyInfo &info = prop.m_info;
      if (info.m_baseType == OA::OCPI_Struct ||
	  info.m_baseType == OA::OCPI_Type)
	throw OU::Error("Struct and Typedef properties are not settable");
      if (info.m_isSequence || prop.m_info.m_arrayRank > 0) {
	if (info.m_baseType == OA::OCPI_String) {
	  const char **sp = v.m_pString;
	  size_t offset = info.m_offset + (info.m_isSequence ? info.m_align : 0);
	  for (unsigned n = 0; n < v.m_nTotal; n++) {
	    size_t l = strlen(sp[n]);
	    setPropertyBytes(info, offset, (uint8_t*)sp[n], l + 1);
	    offset += OU::roundUp(info.m_stringLength + 1, 4);
	  }	  
	} else {
	  uint8_t *data = v.m_pUChar;
	  size_t nBytes = v.m_nTotal * (info.m_nBits/8);
	  if (nBytes)
	    setPropertyBytes(info,
			     info.m_offset + (info.m_isSequence ? info.m_align : 0),
			     data, nBytes);
	}
	if (info.m_isSequence)
	  setProperty32(info, (uint32_t)v.m_nElements);
      } else if (info.m_baseType == OA::OCPI_String) {
	size_t l = strlen(v.m_String) + 1; // amount to actually copy
	if (l > 4)
	  setPropertyBytes(info, info.m_offset + 4,
			   (uint8_t *)(v.m_String + 4), l - 4);
	setProperty32(info, *(uint32_t *)v.m_String);
      } else switch (info.m_nBits) {
	case 8:
	  setProperty8(info, v.m_UChar); break;
	case 16:
	  setProperty16(info, v.m_UShort); break;
	case 32:
	  setProperty32(info, v.m_ULong); break;
	case 64:
	  setProperty64(info, v.m_ULongLong); break;
	default:;
	}
#if 0
      switch (info.m_baseType) {
#define OCPI_DATA_TYPE(sca,corba,letter,bits,run,pretty,store)		 \
	case OA::OCPI_##pretty:					         \
	  if (info.m_isSequence)   	         		 \
	    set##pretty##SequenceProperty(prop, (const run*)(v.m_p##pretty), \
					  v.m_nElements);	\
	  else								 \
	    prop.set##pretty##Value(v.m_##pretty);			 \
          break;
      OCPI_PROPERTY_DATA_TYPES
#undef OCPI_DATA_TYPE
      case OA::OCPI_none: case OA::OCPI_Struct: case OA::OCPI_Type: case OA::OCPI_Enum:
      case OA::OCPI_scalar_type_limit:;
      }
#endif
    }
    void Worker::setProperty(const char *name, const char *value) {
      OA::Property prop(*this, name);
      OU::ValueType &vt = prop.m_info;
      OU::Value v(vt); // FIXME storage when not scalar
      if (vt.m_baseType == OA::OCPI_Struct)
	throw ApiError("No support yet for setting struct properties", NULL);
      const char *err = v.parse(value);
      if (err)
        throw ApiError("Error parsing property value:\"", value, "\"", NULL);
      setPropertyValue(prop, v);
    }
    bool Worker::getProperty(unsigned ordinal, std::string &name, std::string &value) {
      unsigned nProps;
      OU::Property *props = getProperties(nProps);
      if (ordinal >= nProps)
	return false;
      OU::Property &p = props[ordinal];
      if (p.m_baseType == OA::OCPI_Struct || p.m_baseType == OA::OCPI_Type)
	throw OU::Error("Struct and Typedef properties are unsupported");
      OU::Value v(p);
      OA::Property a(*this, p.m_name.c_str()); // FIXME clumsy because get methods take API props
      OA::PropertyInfo &info = a.m_info;
      if (info.m_isSequence || info.m_arrayRank > 0) {
	v.m_nTotal = info.m_nItems;
	if (info.m_isSequence) {
	  v.m_nElements = getProperty32(info);
	  if (v.m_nElements > info.m_sequenceLength)
	    throw OU::Error("Worker's %s property has invalid sequence length: %zu",
			    info.m_name.c_str(), v.m_nElements);
	  v.m_nTotal *= v.m_nElements;
	}
	if (info.m_baseType == OA::OCPI_String) {
	  size_t length = OU::roundUp(info.m_stringLength + 1, 4);
	  v.m_stringSpaceLength = v.m_nTotal * length;
	  v.m_stringNext = v.m_stringSpace = new char[v.m_stringSpaceLength];
	  char **sp = new char *[v.m_nTotal];
	  v.m_pString = (const char **)sp;
	  size_t offset = info.m_offset + (info.m_isSequence ? info.m_align : 0);
	  for (unsigned n = 0; n < v.m_nTotal; n++) {
	    sp[n] = v.m_stringNext;
	    getPropertyBytes(info, offset, (uint8_t *)v.m_stringNext, length);
	    v.m_stringNext += length;
	    offset += length;
	  }	  
	} else {
	  size_t nBytes = v.m_nTotal * (info.m_nBits/8);
	  uint8_t *data = new uint8_t[nBytes];
	  v.m_pUChar = data;
	  if (nBytes)
	    getPropertyBytes(info, info.m_offset + (info.m_isSequence ? info.m_align : 0), data, nBytes);
	}
      } else if (info.m_baseType == OA::OCPI_String) {
	// FIXME: a gross modularity violation
	v.m_stringSpace = new char[info.m_stringLength + 1];
	v.m_String = v.m_stringSpace;
	getPropertyBytes(info, info.m_offset, (uint8_t*)v.m_pString, info.m_stringLength + 1);
      } else switch (info.m_nBits) {
	case 8:
	  v.m_UChar = getProperty8(info); break;
	case 16:
	  v.m_UShort = getProperty16(info); break;
	case 32:
	  v.m_ULong = getProperty32(info); break;
	case 64:
	  v.m_ULongLong = getProperty64(info); break;
	default:;
	}
      
      v.unparse(value);
      name = p.m_name;
      return true;
    }
    void Worker::setProperty(unsigned ordinal, OCPI::Util::Value &value) {
      OA::Property prop(*this, ordinal);
      setPropertyValue(prop, value);
    }

    // batch setting with lots of error checking - all or nothing
    void Worker::setProperties(const OA::PValue *props) {
      if (props)
	for (const OA::PValue *p = props; p->name; p++) {
	  OA::Property prop(*this, p->name); // exception goes out
	  prop.checkTypeAlways(p->type, 1, true);
	  switch (prop.m_info.m_baseType) {
#define OCPI_DATA_TYPE(sca,corba,letter,bits,run,pretty,store)		\
	    case OA::OCPI_##pretty:				\
	      prop.set##pretty##Value(p->v##pretty);			\
	      break;
            OCPI_PROPERTY_DATA_TYPES
#undef OCPI_DATA_TYPE
            default:
	      ocpiAssert("unknown data type"==0);
	  }
	}
    }
    // batch setting with lots of error checking - all or nothing
    void Worker::setProperties(const char *props[][2]) {
      for (const char *(*p)[2] = props; (*p)[0]; p++)
	setProperty((*p)[0], (*p)[1]);
    }
    // Common top level implementation for control operations
    // Note that the m_controlMask does not apply at this level
    // since the container might want to know anyway, even if the
    // worker doesn't have an implementation
#define CONTROL_OP(x, c, t, s1, s2, s3, s4)	\
    void Worker::x() { controlOp(OU::Op##c); }
    OCPI_CONTROL_OPS
#undef CONTROL_OP

    struct ControlTransition {
      ControlState valid[4];
      ControlState next;
    } controlTransitions[] = {
#define CONTROL_OP(x, c, t, s1, s2, s3, s4)	\
      {{s1, s2, s3, s4}, t},
	OCPI_CONTROL_OPS
#undef CONTROL_OP
    };
    const char *controlStateNames[] = {
#define CONTROL_STATE(s) #s,
      OCPI_CONTROL_STATES
#undef CONTROL_STATE
      NULL
    };      

    void Worker::controlOp(OU::ControlOperation op) {
      OU::AutoMutex guard (m_workerMutex, true);
      ControlState cs = getControlState();
      ControlTransition ct = controlTransitions[op];
      // Special case starting and stopping after finished
      if (cs == FINISHED && (op == OU::OpStop || op == OU::OpStart))
	return;
      // If we are already in the desired state, just ignore it so that
      // Neither workers not containers need to deal with this
      if (ct.next == NONE || cs != ct.next) {
	if (cs == ct.valid[0] ||
	    (ct.valid[1] != NONE && cs == ct.valid[1]) ||
	    (ct.valid[2] != NONE && cs == ct.valid[2]) ||
	    (ct.valid[3] != NONE && cs == ct.valid[3])) {
	  controlOperation(op);
	  if (ct.next != NONE)
	    setControlState(ct.next);
	} else
	  throw
	    OU::Error("Control operation '%s' failed on worker '%s%s%s' in state: '%s'",
		      OU::controlOpNames[op], implTag().c_str(),
		      instTag().empty() ? "" : "/", instTag().c_str(),
			  controlStateNames[cs]);
      }
      Application &a = application();
      Container &c = a.container();
      c.start();
    }
    bool Worker::beforeStart() {
      return getControlState() == INITIALIZED;
    }
    bool Worker::isDone() {
      ControlState cs = getControlState();
      return cs == UNUSABLE || cs == FINISHED;
    }
    bool Worker::wait(OCPI::OS::Timer *timer) {
      while (!isDone()) {
	OS::sleep(1);
	if (timer && timer->expired())
	  return true;
      }
      return false;
    }

    //      application().container().start(); 
  }
  namespace API {
    Worker::~Worker(){}
  }
}
