. ./generic_env.sh

# #### Location of the Xilinx tools ####################################### #

export OCPI_XILINX_TOOLS_DIR=/opt/Xilinx/14.6/LabTools
export OCPI_VIVADO_TOOLS_DIR=/home/jek1/mac/Xilinx/Vivado/2012.1

# #### Location of the Altera tools ####################################### #

export OCPI_ALTERA_TOOLS_DIR=/home/jim/Altera/13.1
export OCPI_ALTERA_KITS_DIR=/home/jim/Altera/11.1
# License not needed - this is runtime only
#export OCPI_ALTERA_LICENSE_FILE=/home/jim/mac/altera/1-9095JT_License.dat

#export OCPI_CFLAGS=-m64
#export OCPI_CXXFLAGS=-m64
#export OCPI_LDFLAGS=-m64

# #### Shared library build settings ###################################### #

export OCPI_SHARED_LIBRARIES_FLAGS="-m64 -m elf_x86_64"

# Set to 0 to build static libraries
export OCPI_BUILD_SHARED_LIBRARIES=1

# export OCPI_HAVE_OPENSPLICE=1
# export OCPI_OPENSPLICE_HOME=/opt/opencpi/prerequisites/opensplice/linux-x86_64

. ./generic_env_post.sh
