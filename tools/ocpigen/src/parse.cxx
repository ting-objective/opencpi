/*
 *  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2011
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

#define __STDC_LIMIT_MACROS // wierd standards goof up
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "OcpiUtilMisc.h"
#include "OcpiUtilEzxml.h"
#include "OcpiUtilAssembly.h"
#include "wip.h"
#include "hdl-platform.h"

/*
 * Todo:
 *  property values in assembly instances?
 */

const char *propertyTypes[] = {
  "none", // for OCPI_none
#define OCPI_DATA_TYPE(sca,corba,letter,bits,run,pretty,store) #pretty,
OCPI_PROPERTY_DATA_TYPES
0};
#undef OCPI_DATA_TYPE

#if 0
const char *controlOperations[] = {
#define CONTROL_OP(x, c, t, s1, s2, s3)  #x,
OCPI_CONTROL_OPS
#undef CONTROL_OP
0};
#endif
const char *container = 0, *platform = 0, *device = 0, *load = 0, *os = 0, *os_version = 0, *assembly = 0, *attribute;

Clock *Worker::
addClock() {
  Clock *c = new Clock;
  c->ordinal = m_clocks.size();
  m_clocks.push_back(c);
  return c;
}

Clock *Worker::
addWciClockReset() {
  // If there is no control port, then we synthesize the clock as wci_clk
  for (ClocksIter ci = m_clocks.begin(); ci != m_clocks.end(); ci++)
    if (!strcasecmp("wci_Clk", (*ci)->name))
      return *ci;
  Clock *clock = addClock();
  clock->name = strdup("wci_Clk");
  clock->signal = strdup("wci_Clk");
  clock->reset = "wci_Reset_n";
  return clock;
}
// MyClock boolean simply says whether the clock is "homed" and "named" here.
// The clock attribute says that the clock is defined elsewhere
// The "elsewhere" is either a port that has its own clock or a global definition.
const char *Worker::
checkClock(Port *p) {
  const char *err;
  const char *clock = 0;
  if (p->implXml) {
    clock = ezxml_cattr(p->implXml, "Clock");
    if ((err = OE::getBoolean(p->implXml, "MyClock", &p->myClock)))
      return err;
  }
  if (!clock) {
    if (p->myClock || (p->type == WCIPort && !p->master)) {
      // If port has its own clock, or it is a WCI slave, establish a clock named and homed here
      p->myClock = true;
      p->clock = addClock();
      asprintf((char **)&p->clock->name, "%s_Clk", p->name); // fixme
      p->clock->port = p;
    } else if (m_ports[0]->type == WCIPort && !m_ports[0]->master) {
      // If no clock, and we have a WCI slave then assume the WCI's clock.
      p->clockPort = m_ports[0];
    } else if (m_noControl && !m_assembly &&
	       p->isOCP() && !(p->type == WCIPort && p->master))
      p->clock = addWciClockReset();
    else if (p->isOCP())
      // If no clock, and no wci port, we're hosed.
      return OU::esprintf("Interface %s has no clock declared, and there is no control interface",
			  p->name);
  } else {
    // Port refers to another clock by name
    for (unsigned i = 0; i < m_ports.size(); i++) {
      Port *op = m_ports[i];
      if (p != op && !strcasecmp(clock, op->name)) {
        if (p->myClock)
          // Can't refer to another port and also own the clock
          return OU::esprintf("Clock for interface \"%s\" refers to interface \"%s\","
			      " and also has MyClock=true?",
			      p->name, clock);
        p->clockPort = op;
        return 0;
      }
    }
    // We are not referring to another port.  It muts be a defined clock
    for (ClocksIter ci = m_clocks.begin(); ci != m_clocks.end(); ci++) {
      Clock *c = *ci;
      if (!strcasecmp(clock, c->name)) {
        p->clock = c;
        if (p->myClock)
          if (c->port)
            return OU::esprintf("Clock for interface \"%s\", \"%s\" is already owned by interface \"%s\"",
				p->name, clock, c->port->name);
          else
            c->port = p;
        return 0;
      }
    }
    return OU::esprintf("Clock attribute of \"%s\" matches no interface or clock", p->name);
  }
  return 0;
}

// Check for implementation attributes common to data interfaces, several of which
// are able to override protocol-determined values.
// Take care of the case of implementation-specific ports (via implname);
const char *Worker::
checkDataPort(ezxml_t impl, Port **dpp, WIPType type) {
  const char
    *err,
    *name = ezxml_cattr(impl, "Name"),
    *portImplName = ezxml_cattr(impl, "implName");
  unsigned i;
  Port *dp = 0;
  if (name && portImplName)
    return OU::esprintf("Both \"Name\" and \"ImplName\" attributes of %s element are present",
			impl->name);
  else if (name) {
    for (i = 0; i < m_ports.size(); i++) {
      dp = m_ports[i];
      if (dp && dp->name && !strcasecmp(dp->name, name))
	break;
    }
    if (i >= m_ports.size() || dp && !dp->isData)
      return OU::esprintf("Name attribute of Stream/MessageInterface \"%s\" "
			  "does not match a DataInterfaceSpec", name);
    if (ezxml_cattr(impl, "producer"))
      return OU::esprintf("The \"producer\" attribute is illegal for %s elements",
			  impl->name);

  } else if (portImplName) {
    for (i = 0; i < m_ports.size(); i++) {
      dp = m_ports[i];
      if (dp && dp->name && !strcmp(dp->name, portImplName))
	break;
    }
    if (i < m_ports.size())
      return OU::esprintf("ImplName attribute of Stream/MessageInterface \"%s\" "
			  "matches an existing port", portImplName);
    dp = new Port(portImplName, this, true, WDIPort, impl);
    if ((err = OE::getBoolean(impl, "Producer", &dp->u.wdi.isProducer)) ||
        (err = OE::getBoolean(impl, "Optional", &dp->u.wdi.isOptional)))
      return err;
    m_ports.push_back(dp);
  } else
    return OU::esprintf("Missing \"Name\" or \"ImplName\" attribute of %s element",
			impl->name);
  dp->type = type;
  bool dwFound;
  if ((err = OE::getNumber(impl, "DataWidth", &dp->dataWidth, &dwFound)) ||
      // Be careful not to clobber protocol-determined values (i.e. don't set default values)
      (err = OE::getNumber(impl, "NumberOfOpcodes", &dp->u.wdi.nOpcodes, NULL, 0, false)) ||
      (err = OE::getNumber(impl, "MaxMessageValues", &dp->protocol->m_maxMessageValues, NULL, 0, false)) ||
      (err = OE::getNumber(impl, "DataValueWidth", &dp->protocol->m_dataValueWidth, NULL, 0, false)) ||
      (err = OE::getBoolean(impl, "ZeroLengthMessages", &dp->protocol->m_zeroLengthMessages, true)) ||
      (err = OE::getBoolean(impl, "Continuous", &dp->u.wdi.continuous)) ||
      (err = OE::getBoolean(impl, "ImpreciseBurst", &dp->impreciseBurst)) ||
      (err = OE::getBoolean(impl, "PreciseBurst", &dp->preciseBurst)))
    return err;
  if (!dwFound) {
    if (m_defaultDataWidth >= 0)
      dp->dataWidth = (unsigned)m_defaultDataWidth;
    else
      dp->dataWidth = dp->protocol->m_dataValueWidth;  // or granularity?
  } else if (!dp->protocol->m_dataValueWidth && !dp->protocol->nOperations())
    dp->protocol->m_dataValueWidth = dp->dataWidth;
  if (dp->dataWidth >= dp->protocol->m_dataValueWidth) {
    if (dp->dataWidth % dp->protocol->m_dataValueWidth)
      return OU::esprintf("DataWidth (%zu) on port '%s' not a multiple of DataValueWidth (%zu)",
			  dp->dataWidth, dp->name, dp->protocol->m_dataValueWidth);
  } else if (dp->protocol->m_dataValueWidth % dp->dataWidth)
      return OU::esprintf("DataValueWidth (%zu) on port '%s' not a multiple of DataWidth (%zu)",
			  dp->protocol->m_dataValueWidth, dp->name, dp->dataWidth);
  if (!dp->impreciseBurst && !dp->preciseBurst)
    dp->impreciseBurst = true;
#if 0 // FIXME
  if (dp->impreciseBurst && dp->preciseBurst)
    return "Both ImpreciseBurst and PreciseBurst cannot be specified for WSI or WMI";
#endif
  dp->pattern = ezxml_cattr(impl, "Pattern");
  *dpp = dp;
  return 0;
}

#if 0
// Is this an include? If so give me the underlying XML
static const char *
tryThisInclude(ezxml_t x, const char *parent, ezxml_t *parsed, const char **child) {
  const char *eName = ezxml_name(x);
  if (!eName || strcasecmp(eName, "xi:include"))
    return 0;
  const char *err;
  if ((err = OE::checkAttrs(x, "href", (void*)0)))
    return err;
  const char *ifile = ezxml_cattr(x, "href");
  if (!ifile)
    return OU::esprintf("xi:include missing an href attribute in file \"%s\"", parent);
  ezxml_t i = 0;
  if ((err = parseFile(ifile, parent, element, &i, &ifile, optional)))
    return err;
  *parsed = i;
  *child = ifile;
}
#endif
// If the given element is xi:include, then parse it and return the parsed element.
// If not, *parsed is set to zero.
// If not optional then it MUST be the indicated element
// Also return the file name of the included file.
static const char *
tryInclude(ezxml_t x, const char *parent, const char *element, ezxml_t *parsed,
           const char **child, bool optional) {
  *parsed = 0;
  const char *eName = ezxml_name(x);
  if (!eName || strcasecmp(eName, "xi:include"))
    return 0;
  const char *err;
  if ((err = OE::checkAttrs(x, "href", (void*)0)))
    return err;
  const char *ifile = ezxml_cattr(x, "href");
  if (!ifile)
    return OU::esprintf("xi:include missing an href attribute in file \"%s\"", parent);
  if ((err = parseFile(ifile, parent, element, parsed, &ifile, optional)))
    return OU::esprintf("Error in %s: %s", ifile, err);
  *child = ifile;
  return NULL;
}

// If this element is either the given element or an include of the given element...
// optional means the included file can be something else.
// If success, set *parsed, and maybe *childFile.
static const char *
tryChildInclude(ezxml_t x, const char *parent, const char *element,
                ezxml_t *parsed, const char **childFile, bool optional = false) {
  *childFile = 0;
  const char *err = tryInclude(x, parent, element, parsed, childFile, optional);
  if (err || *parsed)
    return err;
  const char *eName = ezxml_name(x);
  if (!eName || strcasecmp(eName, element))
    return 0;
  *parsed = x;
  return 0;
}

// Find the single instance of a child, which might be xi:included
const char *
tryOneChildInclude(ezxml_t top, const char *parent, const char *element,
		   ezxml_t *parsed, const char **childFile, bool optional) {
  *parsed = 0;
  const char *err = 0;
  for (ezxml_t x = OE::ezxml_firstChild(top); x; x = OE::ezxml_nextChild(x)) {
    const char *eName = ezxml_name(x);
    if (eName)
      if (!strcasecmp(eName, element))
	if (*parsed)
	  return OU::esprintf("found duplicate %s element where only one was expected",
			      element);
	else {
	  if (childFile)
	    *childFile = parent;
	  *parsed = x;
	}
      else {
	const char *file;
	ezxml_t found;
	if ((err = tryInclude(x, parent, element, &found, &file, optional)))
	  return err;
	else if (found) {
	  if (*parsed)
	    return OU::esprintf("found duplicate %s element in file %s, "
				"included from file %s, where only one was expected",
				element, parent, file);
	  else {
	    *parsed = found;
	    if (childFile)
	      *childFile = file;
	  }
	}
      }
  }
  if (!*parsed && !optional)
    return OU::esprintf("no %s element found under %s, whether included via xi:include or not",
			element, ezxml_name(top));
  return err;
}


const char *Worker::
addProperty(ezxml_t prop, bool includeImpl)
{
  OU::Property *p = new OU::Property;
  m_ctl.properties.push_back(p);
  
  const char *err =
    p->parse(prop, m_ctl.readables, m_ctl.writables, m_ctl.sub32Bits,
	     includeImpl, (unsigned)(m_ctl.ordinal++));
  if (!err) {
    if (p->m_isVolatile)
      m_ctl.volatiles = true;
    if (p->m_isVolatile || p->m_isReadable && !p->m_isWritable)
      m_ctl.readbacks = true;
    if (!p->m_isParameter || p->m_isReadable)
      m_ctl.nRunProperties++;
  }
  return err;
}

struct PropInfo {
  Worker *worker;
  bool isImpl; // Are we in an implementation context?
  bool anyIsBad;
  bool top;    // Are we in a top layer mixed with other elements?
  const char *parent;
  PropInfo(Worker *worker, bool isImpl, bool anyIsBad, const char *parent)
    : worker(worker), isImpl(isImpl), anyIsBad(anyIsBad), top(true), parent(parent) {}
};

// process something that might be a property, either at spec time or at impl time
// This tries to keep properties in order no matter where they occur
static const char *
doMaybeProp(ezxml_t maybe, void *vpinfo) {
  PropInfo &pinfo = *(PropInfo*)vpinfo;
  Worker *w = pinfo.worker;
  ezxml_t props = 0;
  const char *childFile;
  const char *err;
  if (pinfo.top) {
    if ((err = tryChildInclude(maybe, pinfo.parent, "ControlInterface", &props, &childFile, true)))
      return err;
    if (props) {
      const char *parent = pinfo.parent;
      pinfo.parent = childFile;
      err = OE::ezxml_children(props, doMaybeProp, &pinfo);
      pinfo.parent = parent;
      return err;
    }
  }
  if (!props &&
      (err = tryChildInclude(maybe, pinfo.parent, "Properties", &props, &childFile, pinfo.top)))
    return err;
  if (props) {
    if (pinfo.anyIsBad)
      return "A Properties element is invalid in this context";
    bool save = pinfo.top;
    pinfo.top = false;
    const char *parent = pinfo.parent;
    pinfo.parent = childFile;
    err = OE::ezxml_children(props, doMaybeProp, &pinfo);
    pinfo.parent = parent;
    pinfo.top = save;
    return err;
  }
  const char *eName = ezxml_name(maybe);
  if (!eName)
    return 0;
  bool isSpec = !strcasecmp(eName, "SpecProperty");
  if (isSpec && !pinfo.isImpl)
    return "SpecProperty elements not allowed in component specification";
  if (!isSpec && strcasecmp(eName, "Property"))
    if (pinfo.top)
      return 0;
    else
      return OU::esprintf("Invalid child element '%s' of a 'Properties' element", eName);
  if (pinfo.anyIsBad)
    return "A Property or SpecProperty element are invalid in this context";
  const char *name = ezxml_cattr(maybe, "Name");
  if (!name)
    return "Property or SpecProperty has no \"Name\" attribute";
  OU::Property *p = NULL;
  for (PropertiesIter pi = w->m_ctl.properties.begin(); pi != w->m_ctl.properties.end(); pi++)
    if (!strcasecmp((*pi)->m_name.c_str(), name)) {
      p = *pi;
      break;
    }
  if (isSpec) {
    // FIXME mark a property as "impled" so we reject doing it more than once
    if (!p)
      return OU::esprintf("Existing property named \"%s\" not found", name);
    if (p->m_default && ezxml_cattr(maybe, "Default"))
      return OU::esprintf("Implementation property named \"%s\" cannot override "
			  "previous default value", name);
    // So simply add impl info to the existing property
    return p->parseImpl(maybe, w->m_ctl.readables, w->m_ctl.writables);
  } else if (p)
      return OU::esprintf("Property named \"%s\" conflicts with existing/previous property",
			  name);
  // All the spec attributes plus the impl attributes
  return w->addProperty(maybe, pinfo.isImpl);
}

const char *Worker::
doProperties(ezxml_t top, const char *parent, bool impl, bool anyIsBad) {
  PropInfo pi(this, impl, anyIsBad, parent);
  return OE::ezxml_children(top, doMaybeProp, &pi);
}

// parse an attribute value as a list separated by comma, space or tab
// and call a function with the given arg for each token found
static const char *
parseList(const char *list, const char * (*doit)(const char *tok, void *arg), void *arg) {
  const char *err = 0;
  if (list) {
    char
      *mylist = strdup(list),
      *base = mylist,
      *last = 0,
      *tok;
    for (base = mylist; (tok = strtok_r(base, ", \t", &last)); base = NULL)
      if ((err = doit(tok, arg)))
        break;
    free(mylist);
  }
  return err;
}

static const char *parseControlOp(const char *op, void *arg) {
  Worker *w = (Worker *)arg;
  unsigned n = 0;
  const char **p;
  for (p = OU::controlOpNames; *p; p++, n++)
    if (!strcasecmp(*p, op)) {
      w->m_ctl.controlOps |= 1 << n;
      break;
    }
  return
    *p ? NULL : "Invalid control operation name in ControlOperations attribute";
}

// Parse the generic implementation control aspects (for rcc and hdl and other)
const char *Worker::
parseImplControl(ezxml_t &xctl) {
  // Now we do the rest of the control interface
  const char *err;
  if ((xctl = ezxml_cchild(m_xml, "ControlInterface")) &&
      m_noControl)
    return "Worker has a ControlInterface element, but also has NoControl=true";
  // Allow overriding byte enables
  bool sub32;
  // either can set to true
  if ((err = OE::getBoolean(m_xml, "Sub32BitConfigProperties", &sub32)) ||
      !sub32 && xctl && (err = OE::getBoolean(xctl, "Sub32BitConfigProperties", &sub32)))
    return err;
  if (sub32)
    m_ctl.sub32Bits = true;
  // We take ops from either place as true
  if ((err = parseList(ezxml_cattr(m_xml, "ControlOperations"), parseControlOp, this)) ||
      xctl && (err = parseList(ezxml_cattr(xctl, "ControlOperations"), parseControlOp, this)))
    return err;
  if ((err = doProperties(m_xml, m_file.c_str(), true, false)))
    return err;
  // Now that we have all information about properties and we can actually
  // do the offset calculations
  for (PropertiesIter pi = m_ctl.properties.begin(); pi != m_ctl.properties.end(); pi++)
    (**pi).offset(m_ctl.offset, m_ctl.sizeOfConfigSpace);
  // Allow overriding sizeof config space, giving priority to controlinterface
  uint64_t sizeOfConfigSpace;
  bool haveSize = false;
  if (xctl && (err = OE::getNumber64(xctl, "SizeOfConfigSpace", &sizeOfConfigSpace, &haveSize, 0)))
    return err;
  if (!haveSize &&
      (err = OE::getNumber64(m_xml, "SizeOfConfigSpace", &sizeOfConfigSpace, &haveSize, 0)))
    return err;
  if (haveSize) {
    if (sizeOfConfigSpace < m_ctl.sizeOfConfigSpace)
      return "SizeOfConfigSpace attribute of ControlInterface smaller than properties indicate";
    m_ctl.sizeOfConfigSpace = sizeOfConfigSpace;
  }
  return 0;
}

// Parse the generic implementation local memories (for rcc and ocl and other)
  const char *Worker::
parseImplLocalMemory() {
  const char* err;
  for (ezxml_t x = ezxml_cchild(m_xml, "LocalMemory"); x; x = ezxml_next(x)) {
    LocalMemory* m = new LocalMemory();
    m_localMemories.push_back(m);
    if ((err = OE::checkAttrs(x, "Name", "SizeofLocalMemory", (void*)0)) )
      return err;
    m->name = ezxml_cattr(x, "Name");
    if (!m->name)
      return "Missing \"Name\" attribute on Local Memory element if OclWorker";
    if ((err = OE::getNumber(x, "SizeOfLocalMemory", &m->sizeOfLocalMemory, 0, 0)))
      return err;
  }
  return 0;
}

// Parse the control information about the component spec
  const char *Worker::
parseSpecControl(ezxml_t ps) {
  const char *err;
  if (ps &&
      ((err = OE::checkAttrs(ps, "SizeOfConfigSpace", "WritableConfigProperties",
			     "ReadableConfigProperties", "Sub32BitConfigProperties",
			     "Count", (void*)0)) ||
       (err = OE::getNumber64(ps, "SizeOfConfigSpace", &m_ctl.sizeOfConfigSpace, 0, 0)) ||
       (err = OE::getBoolean(ps, "WritableConfigProperties", &m_ctl.writables)) ||
       (err = OE::getBoolean(ps, "ReadableConfigProperties", &m_ctl.readables)) ||
       (err = OE::getBoolean(ps, "Sub32BitConfigProperties", &m_ctl.sub32Bits))))
    return err;
  return 0;
}

static const char *checkSuffix(const char *str, const char *suff, const char *last) {
  size_t nstr = last - str, nsuff = strlen(suff);
  const char *start = str + nstr - nsuff;
  return nstr > nsuff && !strncmp(suff, start, nsuff) ? start : str + nstr;
}

Protocol::
Protocol(Port &port)
  : m_port(port) {}

const char * Protocol::
parse(const char *file, ezxml_t prot)
{
  if (file) {
    // If we are being parsed from a protocol file, default the name.
    const char *start = strrchr(file, '/');
    if (start)
      start++;
    else
      start = file;
    const char *last = strrchr(file, '.');
    if (!last)
      last = file + strlen(file);
    last = checkSuffix(start, "_protocol", last);
    last = checkSuffix(start, "_prot", last);
    m_name.assign(start, last - start);
    std::string ofile = m_port.worker->m_file;
    m_port.worker->m_file = file;
    const char *err = OU::Protocol::parse(prot);
    m_port.worker->m_file = ofile;
    return err;
  }
  return prot ? OU::Protocol::parse(prot) : NULL;
}

const char *Protocol::
parseOperation(ezxml_t op) {
  const char *err, *ifile;
  ezxml_t iprot = 0;
  if ((err = tryInclude(op, m_port.worker->m_file.c_str(), "Protocol", &iprot, &ifile, false)))
    return err;
  // If it is an "include", basically recurse
  if (iprot) {
    std::string ofile = m_port.worker->m_file;
    m_port.worker->m_file = ifile;
    err = OU::Protocol::parse(iprot, false);
    m_port.worker->m_file = ofile;
    return err;
  }
  return OU::Protocol::parseOperation(op);
}

const char *Worker::
parsePort(ezxml_t x) {
  const char *err;
  std::string name;
  if ((err = OE::getRequiredString(x, name, "name")))
    return err;
  Port *p = new Port(strdup(name.c_str()), this, true, WDIPort, x);
  m_ports.push_back(p);
  if ((err = OE::checkAttrs(x, "Name", "Producer", "Count", "Optional", "Protocol", (void*)0)) ||
      (err = OE::getBoolean(x, "Producer", &p->u.wdi.isProducer)) ||
      (err = OE::getBoolean(x, "Optional", &p->u.wdi.isOptional)))
    return err;
  const char *protocol = ezxml_cattr(x, "protocol");
  for (unsigned i = 0; i < m_ports.size(); i++) {
    Port *pp = m_ports[i];
    if (pp != p && !strcasecmp(pp->name, p->name))
      return OU::esprintf("%s element Name attribute duplicates another interface name",
			  OE::ezxml_tag(x));
  }
  ezxml_t pSum;
  const char *protFile = 0;
  if ((err = tryOneChildInclude(x, m_file.c_str(), "ProtocolSummary", &pSum, &protFile, true)))
    return err;
  Protocol *prot = p->protocol = new Protocol(*p);
  if (pSum) {
    if (protocol || ezxml_cchild(x, "Protocol"))
      return "cannot have both Protocol and ProtocolSummary";
    if ((err = OE::checkAttrs(pSum, "DataValueWidth", "DataValueGranularity",
			      "DiverDataSizes", "MaxMessageValues", "NumberOfOpcodes",
			      "VariableMessageLength", "ZeroLengthMessages",
			      "MinMessageValues",  (void*)0)) ||
	(err = OE::getNumber(pSum, "NumberOfOpcodes", &p->u.wdi.nOpcodes, 0, 1)) ||
	(err = prot->parseSummary(pSum)))
      return err;
  } else {
    ezxml_t protx = NULL;
    // FIXME: default protocol name from file name
    if ((err = tryOneChildInclude(x, m_file.c_str(), "Protocol", &protx, &protFile, true)))
      return err;
    if (protocol) {
      if (protx)
	return "can't have both 'protocol' element (maybe xi:included) and 'protocol' attribute";
      if ((err = parseFile(protocol, m_file.c_str(), "protocol", &protx, &protFile, false)))
	return err;
    }
    if (protx) {
      if ((err = prot->parse(protFile, protx)))
	return err;
      // So if there is a protocol, nOpcodes is initialized from it.
      p->u.wdi.nOpcodes = p->protocol->nOperations();
    } else {
      // When there is no protocol at all, we force it to variable, bounded at 64k, diverse, zlm
      prot->m_diverseDataSizes = true;
      prot->m_variableMessageLength = true;
      prot->m_maxMessageValues = 64*1024;
      prot->m_zeroLengthMessages = false; // keep it simplest in this case
      p->u.wdi.nOpcodes = 1;
    }
  }
  return NULL;
}

const char *Worker::
parseSpec(const char *package) {
  const char *err;
  // xi:includes at this level are component specs, nothing else can be included
  ezxml_t spec = NULL;
  if ((err = tryOneChildInclude(m_xml, m_file.c_str(), "ComponentSpec", &spec,
				&m_specFile, true)))
    return err;
  const char *specAttr = ezxml_cattr(m_xml, "spec");
  if (specAttr) {
    if (spec)
      return "Can't have both ComponentSpec element (maybe xi:included) and a 'spec' attribute";
    if ((err = parseFile(specAttr, m_file.c_str(), "ComponentSpec", &spec, &m_specFile, true)))
      return err;
  } else if (!spec)
    return "missing componentspec element or spec attribute";
  if (!(m_specName = ezxml_cattr(spec, "Name")))
    return "Missing Name attribute for ComponentSpec";
  if (strchr(m_specName, '.')) {
    m_specName = strdup(m_specName);
  } else {
    std::string packageName;
    if (!package)
      package = ezxml_cattr(spec, "package");
    if (package)
      packageName = package;
    else {
      std::string packageFileDir;
      const char *base = m_specFile ? m_specFile : m_file.c_str();
      const char *cp = strrchr(base, '/');
      if (cp)
	packageFileDir.assign(base, cp + 1 - base);
      // FIXME: Fix this using the include path maybe?
      std::string packageFileName = packageFileDir + "package-name";
      if ((err = OU::file2String(packageName, packageFileName.c_str()))) {
	packageFileName = packageFileDir + "../package-name";
	if ((err = OU::file2String(packageName, packageFileName.c_str()))) {
	  packageFileName = packageFileDir + "../lib/package-name";
	  if ((err = OU::file2String(packageName, packageFileName.c_str())))
	    return OU::esprintf("Missing package-name file: %s", err);
	}
      }
      for (cp = packageName.c_str(); *cp && isspace(*cp); cp++)
	;
      const char *ep;
      for (ep = cp; *ep && !isspace(*ep); ep++)
	;
      packageName.resize(ep - packageName.c_str());
      package = cp;
    }
    asprintf((char **)&m_specName, "%s.%s", package, m_specName);
  }
  if ((err = OE::checkAttrs(spec, "Name", "NoControl", "package", (void*)0)) ||
      (err = OE::getBoolean(spec, "NoControl", &m_noControl)))
    return err;
  // Parse control port info
  ezxml_t ps;
  if ((err = tryOneChildInclude(spec, m_file.c_str(), "PropertySummary", &ps, NULL, true)))
    return err;
  if ((err = doProperties(spec, m_file.c_str(), false, ps != NULL)))
    return err;
  if (m_noControl) {
    if (ps)
      return "NoControl specified, PropertySummary cannot be specified";
    if ((err = doProperties(spec, m_file.c_str(), false, true)))
      return err;
  } else if ((err = parseSpecControl(ps)))
    return err;
  // Now parse the data aspects, allocating (data) ports.
  for (ezxml_t x = ezxml_cchild(spec, "DataInterfaceSpec"); x; x = ezxml_next(x))
    if ((err = parsePort(x)))
      return err;
  for (ezxml_t x = ezxml_cchild(spec, "Port"); x; x = ezxml_next(x))
    if ((err = parsePort(x)))
      return err;
  // FIXME: this should only be for HDL, but of source we don't know that yet.
  // FIXME: but parseing a spec is usually in the context of a model
  // FIXME: so there is perhaps the notion of a spec being restricted to a model?
  return Signal::parseSignals(spec, m_signals);
}

Signal::
Signal()
  : m_direction(IN), m_width(0), m_differential(false), m_pos("%sp"), m_neg("%sn"),
    m_type(NULL) {
}

const char *Signal::
parse(ezxml_t x) {
  const char *err;
  if ((err = OE::checkAttrs(x, "input", "inout", "bidirectional", "output",
			    "width", "differential", "type", (void*)0)))
    return err;
  const char *name;
  if ((name = ezxml_cattr(x, "Input")))
    m_direction = IN;
  else if ((name = ezxml_cattr(x, "Output")))
    m_direction = OUT;
  else if ((name = ezxml_cattr(x, "Inout")))
    m_direction = INOUT;
  else if ((name = ezxml_cattr(x, "bidirectional")))
    m_direction = INOUT;
  else
    return "Missing input, output, or inout attribute for signal element";
  if ((err = OE::getNumber(x, "Width", &m_width, 0, 0)) ||
      (err = OE::getBoolean(x, "differential", &m_differential)))
    return err;
  m_type = ezxml_cattr(x, "type");
  m_name = name;
  return NULL;
}

const char *Signal::
parseSignals(ezxml_t xml, Signals &signals) {
  const char *err = NULL;
  // process ad hoc signals
  for (ezxml_t xs = ezxml_cchild(xml, "Signal"); !err && xs; xs = ezxml_next(xs)) {
    Signal *s = new Signal;
    if (!(err = s->parse(xs)))
      if (!Signal::find(signals, s->m_name.c_str()))
	signals.push_back(s);
      else {
	err = OU::esprintf("Duplicate signal: '%s'", s->m_name.c_str());
	delete s;
      }
  }
  return err;
}

Signal *Signal::
find(Signals &signals, const char *name) {
  for (SignalsIter si = signals.begin(); si != signals.end(); si++)
    if ((*si)->m_name == name)
      return *si;
  return NULL;
}

void Signal::
deleteSignals(Signals &signals) {
  for (SignalsIter si = signals.begin(); si != signals.end(); si++)
    delete *si;
}
const char *Worker::
initImplPorts(ezxml_t xml, const char *element, const char *prefix, WIPType type) {
  const char *err;
  unsigned
    nTotal = OE::countChildren(xml, element),
    ordinal = 0;
  // Clocks depend on port names, so get those names in first pass(non-control ports)
  for (ezxml_t x = ezxml_cchild(xml, element); x; x = ezxml_next(x), ordinal++) {
    bool master = false;
    if ((err = OE::getBoolean(x, "master", &master)))
      return err;
    Port *p = new Port(ezxml_cattr(x, "Name"), this, false, type, x, 1, master);
    m_ports.push_back(p);
    if (!p->name)
      if (nTotal == 1)
        p->name = prefix;
      else
        asprintf((char **)&p->name, "%s%u", prefix, ordinal);
    if ((err = OE::getNumber(x, "count", &p->count)))
      return err;
#if 0
    if (type == CPPort || type == NOCPort)
      m_hasPlatformPorts = true;
#endif
  }
  return NULL;
}

const char *Worker::
parseHdlImpl(const char *package) {
  const char *err;
  ezxml_t xctl;
  size_t dw;
  bool dwFound;
  if (!strcasecmp(OE::ezxml_tag(m_xml),"hdldevice"))
    m_isDevice = true;
  if ((err = parseSpec(package)) ||
      (err = parseImplControl(xctl)) ||
      (err = OE::getNumber(m_xml, "datawidth", &dw, &dwFound)) ||
      (err = OE::getBoolean(m_xml, "outer", &m_outer)))
    return err;
  if (dwFound)
    m_defaultDataWidth = (int)dw; // override the -1 default if set
  // Parse the optional endian attribute.
  // If not specified, it will be defaulted later based on protocols
  const char *myendian = ezxml_cattr(m_xml, "endian");
  if (myendian) {
    static const char *endians[] = {ENDIANS, NULL};
    for (const char **ap = endians; *ap; ap++)
      if (!strcasecmp(myendian, *ap)) {
	m_endian = (Endian)(ap - endians);
	break;
      }
  }
  Port *wci;
  if (m_noControl) {
    wci = NULL;
  } else {
    // Insert the control port at the beginning of the port list since we want
    // To always process the control port first if we have one
    wci = new Port(ezxml_cattr(xctl, "Name"), this, false, WCIPort, xctl);
    m_ports.insert(m_ports.begin(), wci);
    // Finish HDL-specific control parsing
    m_ctl.controlOps |= 1 << OU::OpStart;
    if (m_language == VHDL)
      m_ctl.controlOps |= 1 << OU::OpStop;
    if (xctl) {
      if ((err = OE::checkAttrs(xctl, GENERIC_IMPL_CONTROL_ATTRS, "ResetWhileSuspended",
				"Clock", "MyClock", "Timeout", "Count", "Name", "Pattern",
				(void *)0)) ||
          (err = OE::getNumber(xctl, "Timeout", &wci->u.wci.timeout, 0, 0)) ||
          (err = OE::getNumber(xctl, "Count", &wci->count, 0, 0)) ||
          (err = OE::getBoolean(xctl, "RawProperties", &m_ctl.rawProperties)) ||
          (err = OE::getBoolean(xctl, "ResetWhileSuspended",
				&wci->u.wci.resetWhileSuspended)))
        return err;
      wci->pattern = ezxml_cattr(xctl, "Pattern");
    }
    const char *firstRaw = ezxml_cattr(m_xml, "FirstRawProperty");
    if ((err = OE::getBoolean(m_xml, "RawProperties", &m_ctl.rawProperties)))
      return err;
    if (firstRaw) {
      for (PropertiesIter pi = m_ctl.properties.begin(); pi != m_ctl.properties.end(); pi++)
	if (!strcasecmp((*pi)->m_name.c_str(), firstRaw))
	  m_ctl.firstRaw = *pi;
      if (!m_ctl.firstRaw)
	return OU::esprintf("FirstRawProperty: '%s' not found as a property", firstRaw);
      m_ctl.rawProperties = true;
    }
    bool raw = false;
    for (PropertiesIter pi = m_ctl.properties.begin(); pi != m_ctl.properties.end(); pi++) {
      OU::Property &p = **pi;
      // Determine when the raw properties start
      if (m_ctl.rawProperties &&
	  (!m_ctl.firstRaw ||
	   !strcasecmp(m_ctl.firstRaw->m_name.c_str(), p.m_name.c_str())))
	raw = true;
      if (raw) {
	if (p.m_isWritable)
	  m_ctl.rawWritables = true;
	if (p.m_isReadable)
	  m_ctl.rawReadables = true;
      } else {
	// These control attributes are only set for non-raw properties.
	if (p.m_isReadable)
	  m_ctl.nonRawReadables = true;
	if (p.m_isWritable)
	  m_ctl.nonRawWritables = true;
	if (p.m_isVolatile)
	  m_ctl.nonRawVolatiles = true;
	if (p.m_isVolatile || p.m_isReadable && !p.m_isWritable)
	  m_ctl.nonRawReadbacks = true;
	if (!p.m_isParameter || p.m_isReadable)
	  m_ctl.nNonRawRunProperties++;
	if (p.m_isSub32)
	  m_ctl.nonRawSub32Bits = true;
      }
    }
    if (!wci->count)
      wci->count = 1;
    // clock processing depends on the name so it must be defaulted here
    if (!wci->name)
      wci->name = "ctl";
    if (m_ctl.sub32Bits)
      m_needsEndian = true;
    
  }
  // Now we do clocks before interfaces since they may refer to clocks
#if 0
  m_nClocks = OE::countChildren(m_xml, "Clock");
  // add one to allow for adding the WCI clock later
  m_clocks = myCalloc(Clock, m_nClocks + 1 + m_ports.size());
#endif
  for (ezxml_t xc = ezxml_cchild(m_xml, "Clock"); xc; xc = ezxml_next(xc)) {
    if ((err = OE::checkAttrs(xc, "Name", "Signal", "Home", (void*)0)))
      return err;
    Clock *c = addClock();
    c->name = ezxml_cattr(xc, "Name");
    if (!c->name)
      return "Missing Name attribute in Clock subelement of HdlWorker";
    c->signal = ezxml_cattr(xc, "Signal");
  }
  // Now that we have clocks roughly set up, we process the wci clock
  //  if (wci && (err = checkClock(xctl, wci)))
  //    return err;
  // End of control interface/wci processing (except OCP signal config)
  size_t oldSize = m_ports.size(); // remember the base of extra ports
  // This ordering is repeated below
  if ((err = initImplPorts(m_xml, "MemoryInterface", "mem", WMemIPort)) ||
      (err = initImplPorts(m_xml, "TimeInterface", "wti", WTIPort)) ||
      (err = initImplPorts(m_xml, "timeservice", "time", TimePort)) ||
      (err = initImplPorts(m_xml, "CPMaster", "cp", CPPort)) ||
      (err = initImplPorts(m_xml, "uNOC", "noc", NOCPort)) ||
      (err = initImplPorts(m_xml, "Metadata", "metadata", MetadataPort)) ||
      (err = initImplPorts(m_xml, "Control", "wci", WCIPort)))
    return err;

  // Prepare to process data plane port implementation info
  // Now lets look at the implementation-specific data interface info
  Port *dp;
  for (ezxml_t s = ezxml_cchild(m_xml, "StreamInterface"); s; s = ezxml_next(s))
    if ((err = OE::checkAttrs(s, "Name", "Clock", "DataWidth", "PreciseBurst",
                              "ImpreciseBurst", "Continuous", "Abortable",
                              "EarlyRequest", "MyClock", "RegRequest", "Pattern",
                              "NumberOfOpcodes", "MaxMessageValues",
			      "datavaluewidth", "zerolengthmessages",
			      "implname", "producer", "optional", (void*)0)) ||
        (err = checkDataPort(s, &dp, WSIPort)) ||
        (err = OE::getBoolean(s, "Abortable", &dp->u.wsi.abortable)) ||
        (err = OE::getBoolean(s, "RegRequest", &dp->u.wsi.regRequest)) ||
        (err = OE::getBoolean(s, "EarlyRequest", &dp->u.wsi.earlyRequest)))
      return err;
  for (ezxml_t m = ezxml_cchild(m_xml, "MessageInterface"); m; m = ezxml_next(m))
    if ((err = OE::checkAttrs(m, "Name", "Clock", "MyClock", "DataWidth", "master",
                              "PreciseBurst", "MFlagWidth", "ImpreciseBurst",
                              "Continuous", "ByteWidth", "TalkBack",
                              "Bidirectional", "Pattern",
                              "NumberOfOpcodes", "MaxMessageValues",
			      "datavaluewidth", "zerolengthmessages",
                              (void*)0)) ||
        (err = checkDataPort(m, &dp, WMIPort)) ||
	(err = OE::getBoolean(m, "master", &dp->master)) ||
        (err = OE::getNumber(m, "ByteWidth", &dp->byteWidth, 0, dp->dataWidth)) ||
        (err = OE::getBoolean(m, "TalkBack", &dp->u.wmi.talkBack)) ||
        (err = OE::getBoolean(m, "Bidirectional", &dp->u.wdi.isBidirectional)) ||
        (err = OE::getNumber(m, "MFlagWidth", &dp->u.wmi.mflagWidth, 0, 0)))
      return err;
  // Final pass over all data ports for defaulting and checking
  for (unsigned i = 0; i < m_ports.size(); i++) {
    dp = m_ports[i];
    if ((err = checkClock(dp)))
      return err;
    switch (dp->type) {
    case WDIPort:
      // For data ports that have not been specified as stream or message,
      // default to imprecise stream clocked by the WSI, with data width implied from protocol.
      dp->type = WSIPort;
      dp->dataWidth = m_defaultDataWidth >= 0 ? m_defaultDataWidth : dp->protocol->m_dataValueWidth;
      dp->impreciseBurst = true;
      if (m_ports[0]->type == WCIPort)
	dp->clockPort = m_ports[0];
      else
	return "A data port that defaults to WSI must be in a worker with a WCI";
      // fall into
    case WSIPort:
    case WMIPort:
      {
	// If messages are always a multiple of datawidth and we don't have zlms, bytes are datawidth
	size_t granuleWidth =
	  dp->protocol->m_dataValueWidth * dp->protocol->m_dataValueGranularity;
	// If messages are always a multiple of datawidth and we don't have zlms, bytes are datawidth
	if (granuleWidth >= dp->dataWidth &&
	    (dp->dataWidth == 0 || (granuleWidth % dp->dataWidth) == 0) && 
	    !dp->protocol->m_zeroLengthMessages)
	  dp->byteWidth = dp->dataWidth;
	else
	  dp->byteWidth = dp->protocol->m_dataValueWidth;
	if (dp->byteWidth != 0 && dp->dataWidth % dp->byteWidth)
	  return "Specified ByteWidth does not divide evenly into specified DataWidth";
	// Check if this port requires endianness
	// Either the granule is smaller than or not a multiple of data path width
	if (granuleWidth < dp->dataWidth || dp->dataWidth && granuleWidth % dp->dataWidth)
	  m_needsEndian = true;
      }
      break;
    default:;
    }
  }
  // This is pretty lame, but there is no other heuristic now.
  // Presumably we could enumerate ports that imply we have some clock.
  if (m_ports.size() == 0)
    addWciClockReset();
  size_t nextPort = oldSize;
  for (ezxml_t m = ezxml_cchild(m_xml, "MemoryInterface"); m; m = ezxml_next(m), nextPort++) {
    Port *mp = m_ports[nextPort];
    bool memFound = false;
    if ((err = OE::checkAttrs(m, "Name", "Clock", "DataWidth", "PreciseBurst", "ImpreciseBurst",
                              "MemoryWords", "ByteWidth", "MaxBurstLength", "WriteDataFlowControl",
                              "ReadDataFlowControl", "Count", "Pattern", "master", "myclock", (void*)0)) ||
        (err = OE::getBoolean(m, "master", &mp->master)) ||
        (err = OE::getNumber(m, "Count", &mp->count, 0, 0)) ||
        (err = OE::getNumber64(m, "MemoryWords", &mp->u.wmemi.memoryWords, &memFound, 0)) ||
        (err = OE::getNumber(m, "DataWidth", &mp->dataWidth, 0, 8)) ||
        (err = OE::getNumber(m, "ByteWidth", &mp->byteWidth, 0, 8)) ||
        (err = OE::getNumber(m, "MaxBurstLength", &mp->u.wmemi.maxBurstLength, 0, 0)) ||
        (err = OE::getBoolean(m, "ImpreciseBurst", &mp->impreciseBurst)) ||
        (err = OE::getBoolean(m, "PreciseBurst", &mp->preciseBurst)) ||
        (err = OE::getBoolean(m, "WriteDataFlowControl", &mp->u.wmemi.writeDataFlowControl)) ||
        (err = OE::getBoolean(m, "ReadDataFlowControl", &mp->u.wmemi.readDataFlowControl)))
      return err;
    if (!memFound || !mp->u.wmemi.memoryWords)
      return "Missing \"MemoryWords\" attribute in MemoryInterface";
    if (!mp->preciseBurst && !mp->impreciseBurst) {
      if (mp->u.wmemi.maxBurstLength > 0)
        return "MaxBurstLength specified when no bursts are enabled";
      if (mp->u.wmemi.writeDataFlowControl || mp->u.wmemi.readDataFlowControl)
        return "Read or WriteDataFlowControl enabled when no bursts are enabled";
    }
    if (mp->byteWidth < 8 || mp->dataWidth % mp->byteWidth)
      return "Bytewidth < 8 or doesn't evenly divide into DataWidth";
    mp->pattern = ezxml_cattr(m, "Pattern");
  }
  bool foundWTI = false;
  for (ezxml_t m = ezxml_cchild(m_xml, "TimeInterface"); m; m = ezxml_next(m), nextPort++) {
    Port *mp = m_ports[nextPort];
    if (foundWTI)
      return "More than one WTI specified, which is not permitted";
    if ((err = OE::checkAttrs(m,
			      "Name", "Clock", "SecondsWidth", "FractionWidth", "AllowUnavailable",
			      "Pattern", "master", "myclock",
			      (void*)0)) ||
        (err = OE::getNumber(m, "SecondsWidth", &mp->u.wti.secondsWidth, 0, 32)) ||
        (err = OE::getNumber(m, "FractionWidth", &mp->u.wti.fractionWidth, 0, 0)) ||
        (err = OE::getBoolean(m, "master", &mp->master)) ||
        (err = OE::getBoolean(m, "AllowUnavailable", &mp->u.wti.allowUnavailable)))
      return err;
    mp->dataWidth = mp->u.wti.secondsWidth + mp->u.wti.fractionWidth;
    foundWTI = true;
    mp->pattern = ezxml_cattr(m, "Pattern");
  }
  // Now check that all clocks have a home and all ports have a clock
  for (ClocksIter ci = m_clocks.begin(); ci != m_clocks.end(); ci++) {
    Clock *c = *ci;
    if (c->port) {
#if 0
      if (c->signal)
        return OU::esprintf("Clock %s is owned by interface %s and has a signal name",
			    c->name, c->port->name);
      //asprintf((char **)&c->signal, "%s_Clk", c->port->fullNameIn);
#endif
    } else if (!c->signal)
      return OU::esprintf("Clock %s is owned by no port and has no signal name",
			  c->name);
  }
  // now make sure clockPort references are sorted out
  for (unsigned i = 0; i < m_ports.size(); i++) {
    Port *p = m_ports[i];
    if (p->clockPort)
      p->clock = p->clockPort->clock;
    if (p->count == 0)
      p->count = 1;
  }
  // process ad hoc signals
  if ((err = Signal::parseSignals(m_xml, m_signals)))
    return err;
  // Finalize endian default
  if (m_endian == NoEndian)
    m_endian = m_needsEndian ? Little : Neutral;
  return 0;
}

// This is an HDL file, and perhaps an assembly or a platform
const char *Worker::
parseHdl(const char *package) {
  const char *err;
  if (strcmp(m_implName, m_fileName.c_str()))
    return OU::esprintf("File name (%s) and implementation name in XML (%s) don't match",
			m_fileName.c_str(), m_implName);
  m_pattern = ezxml_cattr(m_xml, "Pattern");
  m_portPattern = ezxml_cattr(m_xml, "PortPattern");
  if (!m_pattern)
    m_pattern = "%s_";
  if (!m_portPattern)
    m_portPattern = "%s_%n";
  // Here is where there is a difference between a implementation and an assembly
  if (!strcasecmp(m_xml->name, "HdlImplementation") || !strcasecmp(m_xml->name, "HdlWorker") ||
      !strcasecmp(m_xml->name, "HdlPlatform") || !strcasecmp(m_xml->name, "HdlDevice") ||
      !strcasecmp(m_xml->name, "HdlConfig") || !strcasecmp(m_xml->name, "HdlContainer")) {
    if ((err = parseHdlImpl(package)))
      return OU::esprintf("in %s for %s: %s", m_xml->name, m_implName, err);
  } else if (!strcasecmp(m_xml->name, "HdlAssembly") ||
	     !strcasecmp(m_xml->name, "HdlPlatformAssembly") ||
	     !strcasecmp(m_xml->name, "HdlContainerAssembly")) {
    if ((err = parseHdlAssy()))
      return OU::esprintf("in %s for %s: %s", m_xml->name, m_implName, err);
  } else
    return "file contains neither an HdlImplementation nor an HdlAssembly nor an HdlPlatform";
  // Whether a worker or an assembly, we derive the external OCP signals, etc.
  if ((err = deriveOCP()))
    return OU::esprintf("in %s for %s: %s", m_xml->name, m_implName, err);
  unsigned wipN[NWIPTypes][2] = {{0}};
  for (unsigned i = 0; i < m_ports.size(); i++) {
    Port *p = m_ports[i];
    // Derive full names
    bool mIn = p->masterIn();
    // ordinal == -1 means insert "%u" into the name for using later
    if ((err = doPattern(p, -1, wipN[p->type][mIn], true, !mIn, p->fullNameIn)) ||
        (err = doPattern(p, -1, wipN[p->type][mIn], false, !mIn, p->fullNameOut)) ||
        (err = doPattern(p, -1, wipN[p->type][mIn], true, !mIn, p->typeNameIn, true)) ||
        (err = doPattern(p, -1, wipN[p->type][mIn], false, !mIn, p->typeNameOut, true)))
      return err;
    if (p->typeNameIn.length() > m_maxPortTypeName)
      m_maxPortTypeName = p->typeNameIn.length();
    if (p->typeNameOut.length() > m_maxPortTypeName)
      m_maxPortTypeName = p->typeNameOut.length();
    //    const char *pat = p->pattern ? p->pattern : w->pattern;
    
    if (p->clock && p->clock->port == p) {
      std::string sin;
      // ordinal == -2 means suppress ordinal
      if ((err = doPattern(p, p->count > 1 ? 0 : -2, wipN[p->type][mIn], true, !mIn, sin)))
        return err;
      asprintf((char **)&p->ocp.Clk.signal, "%s%s", sin.c_str(), "Clk");
      p->clock->signal = p->ocp.Clk.signal;
    }
    switch (p->type) {
    case WCIPort:
    case WSIPort:
    case WMIPort:
    case WMemIPort:
    case WTIPort:
      {
	OcpSignalDesc *osd = ocpSignals;
	for (OcpSignal *os = p->ocp.signals; osd->name; os++, osd++)
	  if (os->master == mIn && /* strcasecmp(osd->name, "Clk") && */ os->value)
	    asprintf((char **)&os->signal, "%s%s", p->fullNameIn.c_str(), osd->name);
	osd = ocpSignals;
	for (OcpSignal *os = p->ocp.signals; osd->name; os++, osd++)
	  if (os->master != mIn && /* strcasecmp(osd->name, "Clk") && */os->value)
	    asprintf((char **)&os->signal, "%s%s", p->fullNameOut.c_str(), osd->name);
      }
    default:;
    }
    wipN[p->type][mIn]++;
  }
  if (m_ports.size() > 32)
    return "worker has more than 32 ports";
  m_model = HdlModel;
  m_modelString = "hdl";
  return 0;
}

/*
 * What implementation-specific attributes does an RCC worker have?
 * And which are not available at runtime?
 * And if they are indeed available at runtime, do we really retreive them from the
 * container or just let the container use what it knows?
 */
const char *Worker::
parseRcc() {
  const char *err;
  if ((err = OE::checkAttrs(m_xml, IMPL_ATTRS, "ExternMethods", "StaticMethods", "Threaded",
			    "ControlOperations", "Language", (void*)0)) ||
      (err = OE::checkElements(m_xml, IMPL_ELEMS, "port", (void*)0)))
    return err;
  // We use the pattern value as the method naming for RCC
  // and its presence indicates "extern" methods.
  m_pattern = ezxml_cattr(m_xml, "ExternMethods");
  m_staticPattern = ezxml_cattr(m_xml, "StaticMethods");
  ezxml_t xctl;
  if ((err = parseSpec()) ||
      (err = parseImplControl(xctl)) ||
      (xctl && (err = OE::checkAttrs(xctl, GENERIC_IMPL_CONTROL_ATTRS, "Threaded", (void *)0))) ||
      (err = OE::getBoolean(m_xml, "Threaded", &m_isThreaded)))
    return err;
  if ((err = parseList(ezxml_cattr(m_xml, "ControlOperations"), parseControlOp, this)))
    return err;
  // Parse data port implementation metadata: maxlength, minbuffers.
  for (ezxml_t x = ezxml_cchild(m_xml, "Port"); x; x = ezxml_next(x)) {
    if ((err = OE::checkAttrs(x, "Name", "MinBuffers", "MinBufferCount", "BufferSize", (void*)0)))
      return err;
    const char *name = ezxml_cattr(x, "Name");
    if (!name)
      return "Missing \"Name\" attribute on Port element if RccWorker";
    Port *p = 0; // kill warning
    unsigned n;
    for (n = 0; n < m_ports.size(); n++) {
      p = m_ports[n];
      if (!strcasecmp(p->name, name))
        break;
    }
    if (n >= m_ports.size())
      return OU::esprintf("No DataInterface named \"%s\" from Port element", name);
    if ((err = OE::getNumber(x, "MinBuffers", &p->u.wdi.minBufferCount, 0, 0)) || // backward compat
        (err = OE::getNumber(x, "MinBufferCount", &p->u.wdi.minBufferCount, 0, p->u.wdi.minBufferCount)) ||
        (err = OE::getNumber(x, "Buffersize", &p->u.wdi.bufferSize, 0,
			     p->protocol ? p->protocol->m_defaultBufferSize : 0)))
      return err;
  }
  m_model = RccModel;
  m_modelString = "rcc";
  return 0;
}
/*
 * What implementation-specific attributes does an OCL worker have?
 * And which are not available at runtime?
 * And if they are indeed available at runtime, do we really retreive them from the
 * container or just let the container use what it knows?
 */
const char * Worker::
parseOcl() {
  const char *err;
  if ((err = OE::checkAttrs(m_xml, "Name", "ExternMethods", "StaticMethods", (void*)0)) ||
      (err = OE::checkElements(m_xml, IMPL_ELEMS, "port", (void*)0)))
    return err;
  // We use the pattern value as the method naming for OCL
  // and its presence indicates "extern" methods.
  m_pattern = ezxml_cattr(m_xml, "ExternMethods");
  m_staticPattern = ezxml_cattr(m_xml, "StaticMethods");
  ezxml_t xctl;
  if ((err = parseSpec()) ||
      (err = parseImplControl(xctl)) ||
      (err = parseImplLocalMemory()))
    return err;
  // Parse data port implementation metadata: maxlength, minbuffers.
  for (ezxml_t x = ezxml_cchild(m_xml, "Port"); x; x = ezxml_next(x)) {
    if ((err = OE::checkAttrs(x, "Name", "MinBuffers", "MinBufferCount", (void*)0)))
      return err;
    const char *name = ezxml_cattr(x, "Name");
    if (!name)
      return "Missing \"Name\" attribute on Port element if OclWorker";
    Port *p = 0; // kill warning
    unsigned n;
    for (n = 0; n < m_ports.size(); n++) {
      p = m_ports[n];
      if (!strcasecmp(p->name, name))
        break;
    }
    if (n >= m_ports.size())
      return OU::esprintf("No DataInterface named \"%s\" from Port element", name);
    if ((err = OE::getNumber(x, "MinBuffers", &p->u.wdi.minBufferCount, 0, 0)) || // backward compat
        (err = OE::getNumber(x, "MinBufferCount", &p->u.wdi.minBufferCount, 0, p->u.wdi.minBufferCount)))
      return err;
  }
  m_model = OclModel;
  m_modelString = "ocl";
  return 0;
}

// Get the filename and the name as required.
// Used when the name defaults from the filename
const char *
getNames(ezxml_t xml, const char *file, const char *tag, std::string &name, std::string &fileName) {
  const char *xname = ezxml_name(xml);
  if (!xname)
    xname = "";
  if (tag && strcasecmp(tag, xname))
    return OU::esprintf("Found xml element: '%s' when expecting %s", xname, tag);
  if (file) {
    const char *cp = strrchr(file, '/');
    cp = cp ? cp + 1 : file;
    const char *lp = strrchr(cp, '.');
    fileName.assign(cp, lp ? lp - cp : strlen(cp));
  }
  if (fileName.empty())
    return OE::getRequiredString(xml, name, "name", ezxml_name(xml));
  OE::getOptionalString(xml, name, "name");
  if (name.empty())
    name = fileName;
  return NULL;
}

// The factory, which decides which class to instantiate
// This will evolve as more things are based on derived classes
Worker *Worker::
create(const char *file, const char *parent, const char *package, const char *&err) {
  err = NULL;
  ezxml_t xml;
  const char *xfile;
  if ((err = parseFile(file, parent, NULL, &xml, &xfile)) )
    return NULL;
  const char *name = ezxml_name(xml);
  if (name) {
    size_t index = 0;
    if (!strcasecmp("HdlPlatform", name))
      return HdlPlatform::create(xml, xfile, err);
    if (!strcasecmp("HdlConfig", name))
      return HdlConfig::create(xml, xfile, index, err);
    if (!strcasecmp("HdlContainer", name))
      return HdlContainer::create(xml, xfile, err);
    if (!strcasecmp("HdlAssembly", name))
      return HdlAssembly::create(xml, xfile, index, err);
    Worker *w = new Worker(xml, xfile, NULL, err);
    if (!err) {
      if (!strcasecmp("RccImplementation", name) || !strcasecmp("RccWorker", name))
	err = w->parseRcc();
      else if (!strcasecmp("OclImplementation", name) || !strcasecmp("OclWorker", name))
	err = w->parseOcl();
      else if (!strcasecmp("HdlImplementation", name) || !strcasecmp("HdlWorker", name) ||
	       !strcasecmp("HdlDevice", name)) {
	if (!(err = OE::checkAttrs(xml, IMPL_ATTRS, GENERIC_IMPL_CONTROL_ATTRS, HDL_TOP_ATTRS,
				   HDL_IMPL_ATTRS, (void*)0)) &&
	    !(err = OE::checkElements(xml, IMPL_ELEMS, HDL_IMPL_ELEMS, (void*)0)))
	  err = w->parseHdl(package);
      } else if (!strcasecmp("HdlAssembly", name)) {
	if (!(err = OE::checkAttrs(xml, IMPL_ATTRS, HDL_TOP_ATTRS, (void*)0)) &&
	    !(err = OE::checkElements(xml, IMPL_ELEMS, HDL_IMPL_ELEMS, ASSY_ELEMS, (void*)0)))
	  err = w->parseHdl(package);
      } else if (!strcasecmp("RccAssembly", name))
	err = w->parseRccAssy();
      else if (!strcasecmp("OclAssembly", name))
	err = w->parseOclAssy();
    }
    if (err) {
      delete w;
      w = NULL;
    }
    return w;
  }
  err = OU::esprintf("\"%s\" is not a valid implemention type (RccWorker, HdlWorker, OclWorker, HdlAssembly, OclAssembly, ComponentAssembly, HdlPlatform)", xml->name);
  return NULL;
}

static unsigned nLibraries;
const char **libraries;
void
addLibrary(const char *lib) {
  for (const char **lp = libraries; lp && *lp; lp++) {
    if (!strcasecmp(lib, *lp))
      return;
  }
  libraries = (const char **)realloc(libraries, (nLibraries + 2) * sizeof(char *));
  libraries[nLibraries++] = lib;
  libraries[nLibraries] = 0;
}

// FIXME: use std::map
const char **mappedLibraries;
static const char **mappedDirs;
static unsigned nMaps;
// If just a library is added, it will be added to this list, but not have a file mapping
const char *
addLibMap(const char *map) {
  const char *cp = strchr(map, ':');
  const char *newLib = cp ? strndup(map, cp - map) : strdup(map);
  if (cp)
    cp++;
  for (const char **mp = mappedLibraries; mp && *mp; mp++)
    if (!strcasecmp(newLib, *mp)) {
      const char **dir = &mappedDirs[mp - mappedLibraries];
      if (cp) // if a new dir is associated with this library
	if ((*dir)[0]) { // if there is an existing dir for this library
	  if (strcmp(cp, *dir))
	    return OU::esprintf("Inconsistent library mapping for %s: %s vs. %s",
				newLib, cp, *dir);
	} else
	  *dir = cp;
      return NULL;
    }
  mappedLibraries = (const char **)realloc(mappedLibraries, (nMaps + 2) * sizeof(char *));
  mappedDirs = (const char **)realloc(mappedDirs, (nMaps + 2) * sizeof(char *));
  mappedLibraries[nMaps] = newLib;
  mappedDirs[nMaps++] = cp ? cp : "";
  mappedLibraries[nMaps] = 0;
  mappedDirs[nMaps] = 0;
  return NULL;
}
const char *
findLibMap(const char *file) {
  const char *cp = strrchr(file, '/');
  for (unsigned n = 0; n < nMaps; n++) {
    size_t len = strlen(mappedDirs[n]);
    if (len && cp && len == (size_t)(cp - file) && !strncmp(mappedDirs[n], file, len))
      return mappedLibraries[n];
  }
  return NULL;
}

Control::Control()
  : sizeOfConfigSpace(0),
    writables(false), nonRawWritables(false), rawWritables(false),
    readables(false), nonRawReadables(false), rawReadables(false),
    sub32Bits(false), nonRawSub32Bits(false), volatiles(false), nonRawVolatiles(false),
    readbacks(false), nonRawReadbacks(false), rawReadbacks(false),
    nRunProperties(0), nNonRawRunProperties(0),
    controlOps(0), offset(0), ordinal(0), rawProperties(false), firstRaw(NULL)
{
}
Worker::Worker(ezxml_t xml, const char *xfile, const char *parent, const char *&err)
  : Parsed(xml, xfile, parent, NULL, err),
    m_model(NoModel), m_modelString(NULL), m_isDevice(false), //m_hasPlatformPorts(false),
    m_noControl(false), m_specFile(0), m_implName(m_name.c_str()), m_specName(0),
    m_isThreaded(false), m_maxPortTypeName(0), m_endian(NoEndian),
    m_needsEndian(false), m_pattern(0), m_staticPattern(0), m_defaultDataWidth(-1),
    m_language(NoLanguage), m_assembly(NULL), m_library(NULL), m_outer(false)
{
  const char *name = ezxml_name(xml);
  // FIXME: make HdlWorker and RccWorker classes  etc.
  if (!err && name && !strncasecmp("hdl", name, 3)) {
    // Parse things that the base class should parse.
    const char *lang = ezxml_cattr(m_xml, "Language");
    if (!lang)
      if (!strcasecmp("HdlContainer", name))
	m_language = VHDL;
      else if (!strcasecmp("HdlAssembly", name))
	m_language = Verilog;
      else
	err = "Missing Language attribute for HDL worker element";
    else if (!strcasecmp(lang, "Verilog"))
      m_language = Verilog;
    else if (!strcasecmp(lang, "VHDL"))
      m_language = VHDL;
    else
      err = OU::esprintf("Language attribute \"%s\" is not \"Verilog\" or \"VHDL\""
			 " in HdlWorker xml file: '%s'", lang, xfile ? xfile : "");
    m_model = HdlModel;
    m_modelString = "hdl";
    m_library = ezxml_cattr(xml, "library");
    if (!m_library && xfile)
      m_library = findLibMap(xfile);
    if (!m_library && hdlAssy)
      m_library = m_implName;
    // Add the library to the global list
    if (m_library)
      addLibMap(m_library);
  }
}
Worker::~Worker() {
  deleteAssy();
}

const char *Worker::
emitAttribute(const char *attr) {
  if (!strcasecmp(attr, "language")) {
    printf(m_language == VHDL ? "VHDL" : "Verilog");
    return NULL;
  }
  return OU::esprintf("Unknown worker attribute: %s", attr);
}

Port::Port(const char *name, Worker *w, bool isData, WIPType type, ezxml_t xml, size_t count, bool master)
  : name(name), worker(w), count(count),
    //    isExternal(false),
    isData(isData), pattern(0), type(type),
    dataWidth(0), byteWidth(0), impreciseBurst(false), preciseBurst(false),
    clock(0), clockPort(0), myClock(false),values(0), master(master), protocol(0), implXml(xml)
{
  memset(&ocp, 0, sizeof(ocp));
  memset(&u, 0, sizeof(u));
}

Parsed::
Parsed(ezxml_t xml,        // The xml for this entity
       const char *file,   // The file with this as top level, possibly NULL
       const char *parent, // The file referencing this entity or file, possibly NULL
       const char *tag,    // The top level tag for this entity
       const char *&err)   // Errors detected during construction
  : m_file(file ? file : ""), m_parent(parent ? parent : ""), m_xml(xml) {
  ocpiAssert(xml);
  err = getNames(xml, file, tag, m_name, m_fileName);
}

Clock::
Clock() 
  : name(NULL), signal(NULL), port(NULL), assembly(false), ordinal(0) {
}
