
# #####
#
#  Copyright (c) Mercury Federal Systems, Inc., Arlington VA., 2009-2010
#
#    Mercury Federal Systems, Incorporated
#    1901 South Bell Street
#    Suite 402
#    Arlington, Virginia 22202
#    United States of America
#    Telephone 703-413-0781
#    FAX 703-413-0784
#
#  This file is part of OpenCPI (www.opencpi.org).
#     ____                   __________   ____
#    / __ \____  ___  ____  / ____/ __ \ /  _/ ____  _________ _
#   / / / / __ \/ _ \/ __ \/ /   / /_/ / / /  / __ \/ ___/ __ `/
#  / /_/ / /_/ /  __/ / / / /___/ ____/_/ / _/ /_/ / /  / /_/ /
#  \____/ .___/\___/_/ /_/\____/_/    /___/(_)____/_/   \__, /
#      /_/                                             /____/
#
#  OpenCPI is free software: you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License as published
#  by the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  OpenCPI is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with OpenCPI.  If not, see <http://www.gnu.org/licenses/>.
#
########################################################################### #


################################################################################
# metadata and generated files that are target-independent
ImplSuffix=$($(CapModel)ImplSuffix)
SkelSuffix=$($(CapModel)SkelSuffix)
SourceSuffix=$($(CapModel)SourceSuffix)
ImplXmlFiles=$(foreach w,$(Workers),$(or $(Worker_$w_xml),$(Worker).xml))
$(call OcpiDbgVar,ImplXmlFiles)

# Only workers need the implementation "header" file and the skeleton

# Allow this to be set to override this default
ifeq ($(origin ImplHeaderFiles),undefined)
ImplHeaderFiles=$(foreach w,$(Workers),$(call ImplHeaderFile,$w))
ImplHeaderFile=$(GeneratedDir)/$1$(ImplSuffix)
$(call OcpiDbgVar,ImplHeaderFiles)
# FIXME: HdlPlatform is bogus here
$(ImplHeaderFiles): $(GeneratedDir)/%$(ImplSuffix) : $$(Worker_%_xml) | $(GeneratedDir)
	$(AT)echo Generating the implementation header file: $@ from $< 
	$(AT)$(OcpiGen) -D $(GeneratedDir) $(and $(Package),-p $(Package)) \
	$(and $(HdlPlatform),-P $(HdlPlatform)) \
	 $(if $(Libraries),$(foreach l,$(Libraries),-l $l)) -i $< \

ifeq ($(origin SkelFiles),undefined)
SkelFiles=$(foreach w,$(Workers),$(GeneratedDir)/$w$(SkelSuffix))
endif

# Making the skeleton may also make a default OWD
skeleton:  $(ImplHeaderFiles) $(SkelFiles)
all: skeleton

$(SkelFiles): $(GeneratedDir)/%$(SkelSuffix) : $$(Worker_%_xml) | $(GeneratedDir)
	$(AT)echo Generating the implementation skeleton file: $@
	$(AT)$(OcpiGen) -D $(GeneratedDir) $(and $(Package),-p $(Package)) -s $<
endif
IncludeDirs:=$(OCPI_CDK_DIR)/include/$(Model) $(GeneratedDir) $(IncludeDirs)
ifeq ($(origin XmlIncludeDirsInternal),undefined)
ifeq ($(origin XmlIncludeDirs),undefined)
ifneq ($(wildcard ../specs),)
XmlIncludeDirsInternal=../specs
endif
endif
endif
override XmlIncludeDirs+=. $(XmlIncludeDirsInternal) $(OCPI_CDK_DIR)/lib/components
-include $(GeneratedDir)/*.deps

clean:: cleanfirst
	$(AT)rm -r -f $(GeneratedDir) \
             $(if $(filter all,$($(CapModel)Targets)),\
                  $(wildcard $(call WkrTargetDir,*)),\
                  $(foreach t,$($(CapModel)Targets),$(call WkrTargetDir,$t)))

################################################################################
# source files that are target-independent
# FIXME: this should not reference an Hdl variable
ifeq ($(findstring $(HdlMode),assembly container),)
ifeq ($(origin WorkerSourceFiles),undefined)
WorkerSourceFiles=$(foreach w,$(Workers),$(w)$(SourceSuffix))
# We must preserve the order of CompiledSourceFiles
#$(call OcpiDbgVar,CompiledSourceFiles)
#AuthoredSourceFiles:=$(strip $(SourceFiles) $(filter-out $(SourceFiles),$(WorkerSourceFiles)))
ifeq ($(origin ModelSpecificBuildHook),undefined)
$(call OcpiDbgVar,SourceSuffix)
$(call OcpiDbgVar,WorkerSourceFiles)
$(call OcpiDbgVar,AuthoredSourceFiles)
# This rule get's run a lot, since it is basically sees that the generated
# skeleton is newer than the source file.
$(WorkerSourceFiles): %$(SourceSuffix) : $(GeneratedDir)/%$(SkelSuffix)
	$(AT)if test ! -e $@; then \
		echo No source file exists. Copying skeleton \($<\) to $@. ; \
		cp $< $@;\
	fi
endif
endif
endif
skeleton: $(WorkerSourceFiles)
AuthoredSourceFiles=$(call Unique,$(SourceFiles) $(WorkerSourceFiles))
$(call OcpiDbgVar,AuthoredSourceFiles)

################################################################################
# Compilation of source to binary into target directories
# We assume all outputs are in the generated directory, so -M goes in that directory
ifndef WkrBinaryName
ifdef BinaryName
WkrBinaryName=$(BinaryName)
else
WkrBinaryName=$(word 1,$(Workers))
endif
endif
$(call OcpiDbgVar,WkrBinaryName)
$(call OcpiDbgVar,Workers)
$(call OcpiDbgVar,ModelSpecificBuildHook,Before all: )
all: $(ModelSpecificBuildHook) 

# Function to generate target dir from target: $(call WkrTargetDir,target)
WkrTargetDir=$(OutDir)target-$(1)

# Function to generate final binary from target: $(call WkrBinary,target)
WkrBinary=$(call WkrTargetDir,$(1))/$(WkrBinaryName)$(BF)

# Function to generate object file name from source: $(call WkrObject,src,target)
WkrObject=$(call WkrTargetDir,$(2))/$(basename $(notdir $(1)))$(OBJ)

# Function to make an object from source: $(call WkrMakeObject,src,target)
define WkrMakeObject
# A line is needed here for the "define" to work (no extra eval)
ObjectFiles_$(2) += $(call WkrTargetDir,$(2))/$(basename $(notdir $(1)))$(OBJ)
$(call WkrObject,$(1),$(2)): $(1) $(ImplHeaderFiles) | $(call WkrTargetDir,$(2))
	$(Compile_$(subst .,,$(suffix $(1))))

endef

# Function to make worker objects depend on impl headers and primitive libraries: 
# $(call WkrWorkerDep,worker,target)
define WkrWorkerDep

$(call WkrObject,$1,$2): TargetDir=$(OutDir)target-$2
$(call WkrObject,$1,$2): $(CapModel)Target=$2
$(call WkrObject,$1,$2): \
   $(call ImplHeaderFile,$1) \
   $(foreach l,$(call $(CapModel)LibrariesInternal,$2),$$(call LibraryRefFile,$l,$2))

endef

################################################################################
# Function to do stuff per target: $(eval $(call WkrDoTarget,target))
define WkrDoTarget
-include $(call WkrTargetDir,$1)/*.deps
# The target directory
$(call WkrTargetDir,$1): | $(OutDir) $(GeneratedDir)
	$(AT)mkdir $$@

# If object files are separate from the final binary,
# Make them individually, and then link them together
ifdef ToolSeparateObjects
$$(call OcpiDbgVar,CompiledSourceFiles)
$(foreach s,$(CompiledSourceFiles),$(call WkrMakeObject,$(s),$(1)))

$(call WkrBinary,$(1)): $(CapModel)Target=$1
$(call WkrBinary,$(1)): $$(ObjectFiles_$(1)) $$(call ArtifactXmlFile,$1) \
			| $(call WkrTargetDir,$(1))
	$(LinkBinary) $$(ObjectFiles_$(1)) $(OtherLibraries)
	$(AT)if test -f "$(ArtifactXmlFile)"; then \
		(cat $(ArtifactXmlFile); \
                 bash -c 'echo X$$$$4' `ls -l $(ArtifactXmlFile)`) >> $$@; \
	fi
endif
# Make sure we actuall make the final binary for this target
$(call OcpiDbg,Before all: WkrBinary is "$(call WkrBinary,$(1))")
all: $(call WkrBinary,$(1))

# If not an application, make the worker object files depend on the impl headers
$(foreach w,$(Workers),$(eval $(call WkrWorkerDep,$w,$1)))

endef

# Do all the targets
$(foreach t,$($(CapModel)Targets),$(eval $(call WkrDoTarget,$t)))

################################################################################
# Export support - what we put into the (export) library above us

ifdef LibDir
# The default for things in the target dir to export into the component library's
# export directory for this target
ifndef WkrExportNames
WkrExportNames=$(WkrBinaryName)$(BF)
endif

define DoLink
LibLinks+=$(LibDir)/$(1)/$(notdir $(2))
$(LibDir)/$(1)/$(notdir $(2)): $(OutDir)target-$(1)/$(2) | $(LibDir)/$(1)
	$(AT)$$(call MakeSymLink,$$^,$(LibDir)/$(1))

endef

define DoLinks

LibDirs+=$(LibDir)/$(1)
$(LibDir)/$(1): | $(LibDir)
$(foreach n,$(WkrExportNames),$(call DoLink,$(1),$(n)))
$$(call OcpiDbgVar,WkrExportNames,In Dolinks )
endef

$(call OcpiDbgVar,WkrExportNames)
$(foreach t,$($(CapModel)Targets),$(eval $(call DoLinks,$(t))))


$(call OcpiDbgVar,LibLinks,Before all:)
all: $(LibLinks)

$(LibDir) $(LibDirs):
	$(AT)mkdir $@

endif # LibDir to export into

################################################################################
# Cleanup.  Tricky/careful removal of skeletons that were copied up into the 
#           directory.
cleanfirst::
	$(AT)for s in $(AuthoredSourceFiles); do \
	   sk=$(GeneratedDir)/`echo $$s | sed s~$(SourceSuffix)$$~$(SkelSuffix)~`; \
	   if test -e $$s -a -e $$sk; then \
	      sed 's/GENERATED ON.*$$//' < $$s > $(GeneratedDir)/$$s; \
	      if (sed 's/GENERATED ON.*$$//' < $$sk | \
                  cmp -s - $(GeneratedDir)/$$s); then \
		echo Source file \($$s\) identical to skeleton file \($$sk\).  Removing it.; \
	        rm $$s; \
	      fi; \
	    fi; \
	done

