###############################################################################
# THE LIBRARIES TO BUILD
###############################################################################
ACLOCAL_AMFLAGS                                             =   -I m4

# The list of header files that belong to the library (to be installed later)
HEADER_FILES                                                =   ../Includes/libBeaver/beaver_filter.h       \
                                                                ../Includes/libBeaver/beaver_readerfilter.h \
                                                                ../Includes/libBeaver/beaver_parrot.h       \
                                                                ../Includes/libBeaver/beaver_parser.h       \
                                                                ../Includes/libBeaver/beaver_writer.h       \
                                                                ../Includes/libBeaver/beaver.h

# The sources to add to the library and to add to the source distribution
SOURCE_FILES                                                =   $(HEADER_FILES)                          \
                                                                ../src/beaver_filter.c                   \
                                                                ../src/beaver_readerfilter.c             \
                                                                ../src/beaver_parrot.c                   \
                                                                ../src/beaver_parser.c                   \
                                                                ../src/beaver_writer.c


# The library names to build (note we are building static and shared libs)
if DEBUG_MODE
lib_LTLIBRARIES                                             =   libbeaver_dbg.la
libbeaver_dbg_ladir                                         =   $(includedir)/libBeaver
libbeaver_dbg_la_HEADERS                                    =   $(HEADER_FILES)
libbeaver_dbg_la_SOURCES                                    =   $(SOURCE_FILES)
else
lib_LTLIBRARIES                                             =   libbeaver.la
libbeaver_ladir                                             =   $(includedir)/libBeaver
libbeaver_la_HEADERS                                        =   $(HEADER_FILES)
libbeaver_la_SOURCES                                        =   $(SOURCE_FILES)
endif


CLEAN_FILES                                                 =   libbeaver.la                           \
                                                                libbeaver_dbg.la

EXTRA_DIST                                                  =   bootstrap

# Ensure the distribution of the doxygen configuration file
# Doxygen support
if HAVE_DOXYGEN
include doxygen.am
EXTRA_DIST                                                  +=  doxygen.ac                              \
                                                                doxygen.am                              \
                                                                $(DX_CONFIG)
endif
