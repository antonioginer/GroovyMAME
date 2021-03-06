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
SED=sed

DESTDIR ?=
PREFIX ?= /usr
INCDIR = $(DESTDIR)$(PREFIX)/include
LIBDIR = $(DESTDIR)$(PREFIX)/lib
BINDIR = $(DESTDIR)$(PREFIX)/bin
PKGDIR = $(LIBDIR)/pkgconfig

# Linux
ifeq  ($(PLATFORM),Linux)
EXTRA_LIBS = libdrm
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags $(EXTRA_LIBS))
SRC += display_linux.cpp custom_video_xrandr.cpp custom_video_drmkms.cpp
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
REMOVE = del /f
STATIC_LIB_EXT = lib
DYNAMIC_LIB_EXT = dll
endif

%.o : %.cpp
	$(FINAL_CXX) -c $(CPPFLAGS) $< -o $@

all: $(SRC:.cpp=.o) $(MAIN).cpp $(TARGET_LIB)
	@echo $(OSFLAG)
	$(FINAL_CXX) $(CPPFLAGS) $(CXXFLAGS) $(SRC:.cpp=.o) $(MAIN).cpp $(LIBS) -o $(STANDALONE)

$(TARGET_LIB): $(OBJS)
	$(FINAL_CXX) $(LDFLAGS) $(CPPFLAGS) -o $@.$(DYNAMIC_LIB_EXT) $^
	$(FINAL_AR) rcs $@.$(STATIC_LIB_EXT) $(^)

$(GRID):
	$(FINAL_CXX) grid.cpp -lSDL2 -o grid

clean:
	$(REMOVE) $(OBJS) $(STANDALONE) $(TARGET_LIB).*

prepare_pkg_config:
	$(SED) -e "s+@prefix@+$(PREFIX)+g" \
	  -e"s+@libdir@+$(LIBDIR)+g" \
	  -e"s+@includedir@+$(INCDIR)+g" \
	  switchres.pc.in > switchres.pc

install: prepare_pkg_config
	$(INSTALL) -Dm644 $(TARGET_LIB).$(DYNAMIC_LIB_EXT) $(LIBDIR)/$(TARGET_LIB).$(DYNAMIC_LIB_EXT)
	$(INSTALL) -Dm644 switchres_wrapper.h $(INCDIR)/switchres/switchres_wrapper.h
	$(INSTALL) -Dm644 switchres.h $(INCDIR)/switchres/switchres.h
	$(INSTALL) -Dm644 switchres.pc $(PKGDIR)/switchres.pc
