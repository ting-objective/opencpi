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


/*
 * PCI transfer driver, which is built on the PIO XferServices.
 *
 * Endpoint format is:
 * ocpi-pci-pio:<busId>.<address>.<holeOffset>.<holeEnd>
 *
 * All values after busid are hex.
 * If holeOffset is zero there is no hole
 */


#define __STDC_FORMAT_MACROS
#define __STDC_LIMIT_MACROS
#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include "OcpiOsAssert.h"
#include "KernelDriver.h"
#include <OcpiPValue.h>
#include <OcpiUtilMisc.h>
#include <DtSharedMemoryInternal.h>
#include <DtPioXfer.h>

namespace OU = OCPI::Util;
namespace OS = OCPI::OS;
namespace DT = DataTransfer;
namespace OD = OCPI::Driver;
namespace DataTransfer {
  namespace PCI {

    class XferFactory;
    class Device : public DataTransfer::DeviceBase<XferFactory,Device> {
      Device(const char *name)
	: DataTransfer::DeviceBase<XferFactory,Device>(name, *this) {}
    };
    class XferServices;
    class SmemServices;

    const char *pci = "pci"; // name passed to inherited template class
    class XferFactory :
      public DriverBase<XferFactory, Device, PIOXferServices, pci> {
      friend class SmemServices;
      int       m_dmaFd; // if this is >= 0, then we have been there before
      bool      m_usingKernelDriver;
      uint64_t  m_dmaBase;
      unsigned  m_maxMBox, m_perMBox;
    public:
      XferFactory()
	: m_dmaFd(-1), m_usingKernelDriver(false), m_dmaBase(UINT64_MAX),
	  m_maxMBox(0)
      {}

      virtual
      ~XferFactory() {
	lock();
	if (m_dmaFd >= 0)
	  ::close(m_dmaFd); // FIXME need OS abstraction
      }
    private:
      void
      initDma(uint16_t maxCount) {
	if ((m_dmaFd = ::open(OCPI_DRIVER_MEM, O_RDWR | O_SYNC)) >= 0)
	  m_usingKernelDriver = true;
	else if ((m_dmaFd = open("/dev/mem", O_RDWR|O_SYNC )) < 0)
	  throw OU::Error("cant open /dev/mem for PCI (Use sudo or load the driver)");
	else {
	  m_usingKernelDriver = false;
	  const char *dma = getenv("OCPI_DMA_MEMORY");
	  if (!dma)
	    throw OU::Error("OCPI_DMA_MEMORY environment variable not set");
	  unsigned sizeM, pagesize = getpagesize();
	  uint64_t top;
	  if (sscanf(dma, "%uM$0x%" SCNx64, &sizeM, &m_dmaBase) != 2)
	    throw OU::Error("Bad format for OCPI_DMA_MEMORY environment variable: '%s'",
			    dma);
	  ocpiDebug("DMA Memory:  %uM at 0x%" PRIx64, sizeM, m_dmaBase);
	  unsigned dmaSize = sizeM * 1024 * 1024;
	  top = m_dmaBase + dmaSize;
	  if (m_dmaBase & (pagesize-1)) {
	    m_dmaBase += pagesize - 1;
	    m_dmaBase &= ~(pagesize - 1);
	    top &= ~(pagesize - 1);
	    dmaSize = (uint32_t)(top - m_dmaBase);
	    ocpiBad("DMA Memory is NOT page aligned.  Now %u at 0x%" PRIx64 "\n",
		    dmaSize, m_dmaBase);
	  }
	  m_maxMBox = maxCount;
	  m_perMBox = (dmaSize / maxCount) & ~(pagesize - 1);
	}
      }
    protected:
      // Getting a memory region happens on demand, but releasing them
      // only happens on shutdown
      void
      getDmaRegion(EndPoint &ep) {
	OU::SelfAutoMutex guard(this);
	if (m_dmaFd < 0)
	  initDma(ep.maxCount);
	ocpi_request_t request;
	memset(&request, 0, sizeof(request));
	request.needed = (ocpi_size_t)ep.size;
	request.cached = ocpi_uncached;
	if (m_usingKernelDriver) {
	  if (ioctl(m_dmaFd, OCPI_CMD_REQUEST, &request))
	    throw OU::Error("Can't allocate memory size %zu for DMA memory", ep.size);
	} else {
	  ocpiAssert(ep.maxCount == m_maxMBox);
	  // chop it up into equal parts assuming everyone has the same maxCount
	  if (ep.size > m_perMBox)
	    throw OU::Error("not enough memory to accomodate all endpoints");
	  request.actual = m_perMBox;
	  request.address = m_dmaBase + m_perMBox * ep.mailbox;
	}
	ep.address = request.address;
      }

      uint8_t *
      mapDmaRegion(EndPoint &ep, uint32_t offset, size_t size) {
	OU::SelfAutoMutex guard(this);
	if (m_dmaFd < 0)
	  initDma(ep.maxCount);

	void *vaddr =  mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
			    m_dmaFd, (off_t)(ep.address + offset));
	if (vaddr == MAP_FAILED)
	  throw OU::Error("mmap failed on DMA region %zu at 0x%" PRIx64,
			  size, ep.address + offset);
	ocpiDebug("For ep %p, offset 0x%" PRIx32 " size %zu vaddr is %p to %p",
		  &ep, offset, size, vaddr, (uint8_t *)vaddr + size);
	return (uint8_t*)vaddr;
      }
    public:
      const char *
      getProtocol() { return "ocpi-pci-pio"; }

      DT::XferServices *
      getXferServices(DT::SmemServices* source, DT::SmemServices* target) {
	return new PIOXferServices(source, target);
      }
      
      DT::EndPoint *
      createEndPoint(std::string& endpoint, bool local);

      // FIXME: provide ref to string as arg
      std::string 
      allocateEndpoint(const OU::PValue*, uint16_t mailBox, uint16_t maxMailBoxes) {
	std::string ep;
	
	OCPI::Util::formatString(ep, "ocpi-pci-pio:0.0.0.0;%zu.%" PRIu16 ".%" PRIu16,
				 m_SMBSize, mailBox, maxMailBoxes);
	return ep;
      }
    };
    RegisterTransferDriver<XferFactory> driver;

    class  EndPoint : public DT::EndPoint {
      friend class SmemServices;
    protected:
      uint32_t m_holeOffset, m_holeEnd;
      unsigned m_busId;
    public:
      EndPoint( std::string& ep, bool local)
        : DT::EndPoint(ep, 0, local) {
	if (sscanf(ep.c_str(), "ocpi-pci-pio:%x.%" SCNx64 ".%" SCNx32 ".%" SCNx32 ";",
		   &m_busId, &address, &m_holeOffset, &m_holeEnd) != 4)
	  throw OU::Error("Invalid format for PCI endpoint: %s", ep.c_str());
  
	ocpiDebug("PCI ep %p %s: bus_id = %d, address = 0x%" PRIx64
		  " size = 0x%zx hole 0x%" PRIx32 " end 0x%" PRIx32,
		  this, ep.c_str(), m_busId, address, size, m_holeOffset, m_holeEnd);
      };
      virtual ~EndPoint() {}

      DT::SmemServices & createSmemServices();

      // Get the address from the endpoint
      // FIXME: make this get address thing NOT generic...
      virtual const char* getAddress() {
	return 0;
      }
    };

    DT::EndPoint* XferFactory::
    createEndPoint(std::string& endpoint, bool local) {
      return new EndPoint(endpoint, local);
    }

    class SmemServices : public DT::SmemServices {
      EndPoint &m_pciEndPoint;
      uint8_t *m_vaddr, *m_vaddr1;
      XferFactory &m_driver;
      friend class EndPoint;
    protected:
      SmemServices(EndPoint& ep) 
	: DT::SmemServices(ep), m_pciEndPoint(ep), m_vaddr(NULL), m_vaddr1(NULL),
	  m_driver(XferFactory::getSingleton())
      {
	// For remote mappings all is deferred until mapping
	if (ep.local) {
	  m_driver.getDmaRegion(ep);
	  m_vaddr = m_driver.mapDmaRegion(ep, 0, ep.size);
	  // FIXME: somehow we shouldn't be reformatting the whole endpoint string?
	  OU::formatString(ep.end_point,
			   "ocpi-pci-pio:%u.0x%" PRIx64".0x%" PRIx32 ".0x%" PRIx32
			   ";%zu.%" PRIu16 ".%" PRIu16,
			   ep.m_busId, ep.address, ep.m_holeOffset, ep.m_holeEnd,
			   ep.size, ep.mailbox, ep.maxCount);
	  ocpiDebug("Finalized PCI ep %p: %s", &ep, ep.end_point.c_str());
	}
      }
    public:
      virtual ~SmemServices () {
      }
      // FIXME these should have defaults...
      int32_t attach(DT::EndPoint*) { return 0; }
      int32_t detach() { return 0; }
      int32_t unMap() { return 0; }
      void* map(DtOsDataTypes::Offset offset, size_t size ) {
	EndPoint &ep = m_pciEndPoint;
	OU::SelfAutoMutex guard (&m_driver);
	uint8_t *vaddr = 0;
	if (m_vaddr == NULL) {
	  if (ep.m_holeOffset) {
	    m_vaddr = m_driver.mapDmaRegion(ep, 0, ep.m_holeOffset);
	    m_vaddr1 = m_driver.mapDmaRegion(ep, ep.m_holeEnd, ep.size - ep.m_holeEnd);
	    ocpiDebug("pci ep %p has vaddr %p to %p, vaddr1 %p to %p",
		      &ep, m_vaddr, m_vaddr + ep.m_holeOffset, m_vaddr1,
		      m_vaddr1 + (ep.size - ep.m_holeEnd));
	  } else {
	    m_vaddr = m_driver.mapDmaRegion(ep, 0, ep.size);
	    ocpiDebug("pci ep %p has vaddr %p to %p, no vaddr1",
		      &ep, m_vaddr, m_vaddr + ep.size);
	  }
	}
	size_t top = offset + size;
	if (top <= ep.size) {
	  if (ep.m_holeOffset == 0 || offset < ep.m_holeOffset && top <= ep.m_holeOffset)
	    vaddr = m_vaddr + offset;
	  else if (ep.m_holeOffset && offset >= ep.m_holeEnd)
	    vaddr = m_vaddr1 + (offset - ep.m_holeEnd);
	}
	if (!vaddr)
	  throw OU::Error("Mapping offset %" DTOSDATATYPES_OFFSET_PRIx " size %zu out of range",
			  offset, size);
	ocpiDebug("PCI::SmemServices::map returning %s vaddr = %p base %p/%p offset 0x%"
		  DTOSDATATYPES_OFFSET_PRIx " size %zu, end 0x%p",
		  m_pciEndPoint.local ? "local" : "remote",
 		  vaddr, m_vaddr, m_vaddr1, offset, size, vaddr + size);
	return (void*)vaddr;
      }
    };
    
    DT::SmemServices & EndPoint::
    createSmemServices() {
      return *new SmemServices(*this);
    }
  }
}
