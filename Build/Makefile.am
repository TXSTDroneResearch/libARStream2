###############################################################################
# THE LIBRARIES TO BUILD
###############################################################################
ACLOCAL_AMFLAGS                                             =   -I m4

# The list of header files that belong to the library (to be installed later)
HEADER_FILES                                                =   ../Includes/libARStream2/arstream2_error.h           \
                                                                ../Includes/libARStream2/arstream2_stream_receiver.h \
                                                                ../Includes/libARStream2/arstream2_rtp_receiver.h    \
                                                                ../Includes/libARStream2/arstream2_rtp_sender.h      \
                                                                ../Includes/libARStream2/arstream2_h264_sei.h        \
                                                                ../Includes/libARStream2/arstream2_h264_filter.h     \
                                                                ../Includes/libARStream2/arstream2_h264_parser.h     \
                                                                ../Includes/libARStream2/arstream2_h264_writer.h

# The sources to add to the library and to add to the source distribution
SOURCE_FILES                                                =   $(HEADER_FILES)                    \
                                                                ../src/arstream2_error.c           \
                                                                ../src/arstream2_stream_receiver.c \
                                                                ../src/arstream2_h264.h            \
                                                                ../src/arstream2_h264_parser.c     \
                                                                ../src/arstream2_h264_writer.c     \
                                                                ../src/arstream2_h264_sei.c        \
                                                                ../src/arstream2_h264_filter.c     \
                                                                ../src/arstream2_rtp.h             \
                                                                ../src/arstream2_rtp_receiver.c    \
                                                                ../src/arstream2_rtp_sender.c


# The library names to build (note we are building static and shared libs)
if DEBUG_MODE
lib_LTLIBRARIES                                             =   libarstream2_dbg.la
libarstream2_dbg_ladir                                         =   $(includedir)/libARStream2
libarstream2_dbg_la_HEADERS                                    =   $(HEADER_FILES)
libarstream2_dbg_la_SOURCES                                    =   $(SOURCE_FILES)
else
lib_LTLIBRARIES                                             =   libarstream2.la
libarstream2_ladir                                             =   $(includedir)/libARStream2
libarstream2_la_HEADERS                                        =   $(HEADER_FILES)
libarstream2_la_SOURCES                                        =   $(SOURCE_FILES)
endif


CLEAN_FILES                                                 =   libarstream2.la                           \
                                                                libarstream2_dbg.la

EXTRA_DIST                                                  =   bootstrap

# Ensure the distribution of the doxygen configuration file
# Doxygen support
if HAVE_DOXYGEN
include doxygen.am
EXTRA_DIST                                                  +=  doxygen.ac                              \
                                                                doxygen.am                              \
                                                                $(DX_CONFIG)
endif
