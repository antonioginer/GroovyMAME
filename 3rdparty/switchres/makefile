PLATFORM := $(shell uname)

MAIN = switchres_main
STANDALONE = switchres
TARGET_LIB = libswitchres
GRID = grid
SRC = monitor.cpp modeline.cpp switchres.cpp display.cpp custom_video.cpp log.cpp switchres_wrapper.cpp edid.cpp
OBJS = $(SRC:.cpp=.o)

CROSS_COMPILE ?=
CXX ?= g++
AR ?= ar
LDFLAGS = -shared
FINAL_CXX=$(CROSS_COMPILE)$(CXX)
FINAL_AR=$(CROSS_COMPILE)$(AR)
CPPFLAGS = -O3 -Wall -Wextra

PKG_CONFIG=pkg-config
INSTALL=install

DESTDIR ?=
PREFIX ?= /usr
INCDIR = $(DESTDIR)$(PREFIX)/include
LIBDIR = $(DESTDIR)$(PREFIX)/lib
BINDIR = $(DESTDIR)$(PREFIX)/bin
PKGDIR = $(LIBDIR)/pkgconfig

# Linux
ifeq  ($(PLATFORM),Linux)
SRC += display_linux.cpp

HAS_VALID_XRANDR := $(shell $(PKG_CONFIG) --libs xrandr; echo $$?)
ifeq ($(HAS_VALID_XRANDR),1)
    $(info Switchres needs xrandr. X support is disabled)
else
    $(info X support enabled)
    CPPFLAGS += -DSR_WITH_XRANDR
    SRC += custom_video_xrandr.cpp
endif

HAS_VALID_DRMKMS := $(shell $(PKG_CONFIG) --libs "libdrm >= 2.4.98"; echo $$?)
ifeq ($(HAS_VALID_DRMKMS),1)
    $(info Switchres needs libdrm >= 2.4.98. KMS support is disabled)
else
    $(info KMS support enabled)
    CPPFLAGS += -DSR_WITH_KMSDRM
    EXTRA_LIBS = libdrm
    SRC += custom_video_drmkms.cpp
endif

ifneq (,$(EXTRA_LIBS))
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(EXTRA_LIBS))
endif

CPPFLAGS += -fPIC
LIBS = -ldl
REMOVE = rm -f
STATIC_LIB_EXT = a
DYNAMIC_LIB_EXT = so

# Windows
else ifneq (,$(findstring NT,$(PLATFORM)))
SRC += display_windows.cpp custom_video_ati_family.cpp custom_video_ati.cpp custom_video_adl.cpp custom_video_pstrip.cpp resync_windows.cpp
CPPFLAGS += -static -static-libgcc -static-libstdc++
LIBS =
#REMOVE = del /f
REMOVE = rm -f
STATIC_LIB_EXT = lib
DYNAMIC_LIB_EXT = dll
endif

define SR_PKG_CONFIG
prefix=$(PREFIX)
exec_prefix=$${prefix}
includedir=$${prefix}/include
libdir=$${exec_prefix}/lib

Name: libswitchres
Description: A basic switchres implementation
Version: 2.00
Cflags: -I$${includedir}/switchres
Libs: -L$${libdir} -ldl -lswitchres
endef


%.o : %.cpp
	$(FINAL_CXX) -c $(CPPFLAGS) $< -o $@

all: $(SRC:.cpp=.o) $(MAIN).cpp $(TARGET_LIB) prepare_pkg_config
	@echo $(OSFLAG)
	$(FINAL_CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC:.cpp=.o) $(MAIN).cpp $(LIBS) -o $(STANDALONE)

$(TARGET_LIB): $(OBJS)
	$(FINAL_CXX) $(LDFLAGS) $(CPPFLAGS) -o $@.$(DYNAMIC_LIB_EXT) $^
	$(FINAL_CXX) -c $(CPPFLAGS) -DSR_WIN32_STATIC switchres_wrapper.cpp -o switchres_wrapper.o
	$(FINAL_AR) rcs $@.$(STATIC_LIB_EXT) $(^)

$(GRID):
	$(FINAL_CXX) grid.cpp -lSDL2 -o grid

clean:
	$(REMOVE) $(OBJS) $(STANDALONE) $(TARGET_LIB).*
	$(REMOVE) switchres.pc

prepare_pkg_config:
	$(file > switchres.pc,$(SR_PKG_CONFIG))

install:
	$(INSTALL) -Dm644 $(TARGET_LIB).$(DYNAMIC_LIB_EXT) $(LIBDIR)/$(TARGET_LIB).$(DYNAMIC_LIB_EXT)
	$(INSTALL) -Dm644 switchres_wrapper.h $(INCDIR)/switchres/switchres_wrapper.h
	$(INSTALL) -Dm644 switchres.h $(INCDIR)/switchres/switchres.h
	$(INSTALL) -Dm644 switchres.pc $(PKGDIR)/switchres.pc
