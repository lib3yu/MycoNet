# - utf-8 -

.PHONY: clean ctest-unit demo1 demo2 demo3

######################################
# target
######################################
TARGET := myconet
UNITEST_TARGET := ctest-unit
GTEST_TARGET := cpptest-unit

#######################################
# paths
#######################################
PROJ_BINDIR  := bin
PROJ_OBJDIR  := obj
PROJ_SRCDIR  := src
PROJ_CINCDIR := include
PROJ_CLIBDIR := lib
PROJ_TESTDIR := test

#######################################
# config variables
#######################################

MAKEFILE_NAME := Makefile

# CC  ?= gcc
CC_PREIFX := 
# CC_PREIFX := arm-linux-gnueabi-

CC  := $(CC_PREIFX)gcc
CXX := $(CC_PREIFX)g++
SZ  ?= $(CC_PREIFX)size

#######################################
# project header / sources files
#######################################
# C include folders
PROJ_CINCLUDES := 
PROJ_CINCLUDES += -I$(PROJ_CINCDIR)

UNITEST_CINCLUDES :=
UNITEST_CINCLUDES += $(PROJ_CINCLUDES)
UNITEST_CINCLUDES += -I3rd_party/unity

GTEST_CINCLUDES :=
GTEST_CINCLUDES += $(PROJ_CINCLUDES)
GTEST_CINCLUDES += -I/usr/include/gtest

# C sources file
PROJ_CSOURCE := 
# PROJ_CSOURCE += src/myconet.c

PROJ_CXXSOURCE := 
PROJ_CXXSOURCE += src/myconet.cpp
PROJ_CXXSOURCE += src/myconet2c.cpp

UNITEST_CSOURCE :=
UNITEST_CSOURCE += 3rd_party/unity/unity.c
UNITEST_CSOURCE += test/test-unit.c

DEMO1_CXXSOURCE := 
DEMO1_CXXSOURCE += test/demo1.cpp

DEMO2_CXXSOURCE := 
DEMO2_CXXSOURCE += test/demo2.cpp

DEMO3_CSOURCE :=
DEMO3_CSOURCE += test/demo3.c

GTEST_CXXSOURCE :=
GTEST_CXXSOURCE += test/gtest-myconet.cpp

# C definations
PROJ_CDEFINES := 

UNITEST_CDEFINES :=
UNITEST_CDEFINES += -DUNITY_INCLUDE_DOUBLE

#######################################
# CFLAGS
#######################################

COMMON_CFLAGS := 
# COMMON_CFLAGS += -Og -g
COMMON_CFLAGS += -O3 
COMMON_CFLAGS += -DNDEBUG
COMMON_CFLAGS += -Wall
COMMON_CFLAGS += -Wextra
COMMON_CFLAGS += -ffunction-sections
COMMON_CFLAGS += -fdata-sections
COMMON_CFLAGS += -pthread

CFLAGS := $(CFLAGS) -std=c17
CFLAGS := $(CFLAGS) -MMD
CFLAGS := $(CFLAGS) $(COMMON_CFLAGS)
CFLAGS := $(CFLAGS) $(PROJ_CDEFINES)
CFLAGS := $(CFLAGS) $(PROJ_CINCLUDES)

#######################################
# CXXFLAGS 
#######################################

CXXFLAGS := $(CXXFLAGS) -std=c++17
CXXFLAGS := $(CXXFLAGS) -fno-rtti
CXXFLAGS := $(CXXFLAGS) -fno-exceptions
CXXFLAGS := $(CXXFLAGS) -fno-threadsafe-statics
CXXFLAGS := $(CXXFLAGS) -MMD
CXXFLAGS := $(CXXFLAGS) $(COMMON_CFLAGS)
CXXFLAGS := $(CXXFLAGS) $(PROJ_CDEFINES)
CXXFLAGS := $(CXXFLAGS) $(PROJ_CINCLUDES)

#######################################
# LDFLAGS
#######################################
LDFLAGS := $(LDFLAGS) -L$(PROJ_CLIBDIR)
LDFLAGS := $(LDFLAGS) -Wl,--gc-sections
LDFLAGS := $(LDFLAGS) -lstdc++
LDFLAGS := $(LDFLAGS) -lpthread

#######################################
# build the application
#######################################

# replace all file name suffix .c into .o 
OBJECTS := 
OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(PROJ_CSOURCE:.c=.o)))
OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(PROJ_CXXSOURCE:.cpp=.o)))

UNITEST_OBJECTS := 
UNITEST_OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(UNITEST_CSOURCE:.c=.o)))
UNITEST_OBJECTS += $(OBJECTS)

DEMO1_OBJECTS := 
DEMO1_OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(DEMO1_CXXSOURCE:.cpp=.o)))

DEMO2_OBJECTS := 
DEMO2_OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(DEMO2_CXXSOURCE:.cpp=.o)))

DEMO3_OBJECTS :=
DEMO3_OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(DEMO3_CSOURCE:.c=.o)))

GTEST_OBJECTS :=
GTEST_OBJECTS += $(addprefix $(PROJ_OBJDIR)/,$(notdir $(GTEST_CXXSOURCE:.cpp=.o)))
GTEST_OBJECTS += $(OBJECTS)

# source files search path
vpath %.c $(sort $(dir $(PROJ_CSOURCE)))
vpath %.cpp $(sort $(dir $(PROJ_CXXSOURCE)))
vpath %.c $(sort $(dir $(UNITEST_CSOURCE)))
vpath %.cpp $(sort $(dir $(DEMO1_CXXSOURCE)))
vpath %.cpp $(sort $(dir $(DEMO2_CXXSOURCE)))
vpath %.c $(sort $(dir $(DEMO3_CSOURCE)))
vpath %.cpp $(sort $(dir $(GTEST_CXXSOURCE)))

all: $(PROJ_BINDIR)/$(TARGET)
demo1: $(PROJ_BINDIR)/demo1
demo2: $(PROJ_BINDIR)/demo2
demo3: $(PROJ_BINDIR)/demo3
ctest-unit: $(PROJ_BINDIR)/$(UNITEST_TARGET)
cpptest-unit: $(PROJ_BINDIR)/$(GTEST_TARGET)

$(PROJ_BINDIR)/demo3: $(DEMO3_OBJECTS) $(OBJECTS) $(MAKEFILE_NAME) | $(PROJ_BINDIR)
	$(CC) $(DEMO3_OBJECTS) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(PROJ_BINDIR)/$(GTEST_TARGET): $(GTEST_OBJECTS) $(MAKEFILE_NAME) | $(PROJ_BINDIR)
	$(CXX) $(GTEST_OBJECTS) $(LDFLAGS) -lgtest -lgtest_main -o $@
	$(SZ) $@

$(PROJ_BINDIR)/demo2: $(DEMO2_OBJECTS) $(OBJECTS) $(MAKEFILE_NAME) | $(PROJ_BINDIR)
	$(CC) $(DEMO2_OBJECTS) $(OBJECTS) $(LDFLAGS) -o $@ 
	$(SZ) $@

$(PROJ_BINDIR)/demo1: $(DEMO1_OBJECTS) $(OBJECTS) $(MAKEFILE_NAME) | $(PROJ_BINDIR)
	$(CC) $(DEMO1_OBJECTS) $(OBJECTS) $(LDFLAGS) -o $@ 
	$(SZ) $@

$(PROJ_BINDIR)/$(UNITEST_TARGET): $(UNITEST_OBJECTS) $(MAKEFILE_NAME) | $(PROJ_BINDIR)
	$(CC) $(UNITEST_OBJECTS) $(LDFLAGS) -o $@ 
	$(SZ) $@

$(PROJ_BINDIR)/$(TARGET): $(OBJECTS) $(MAKEFILE_NAME) | $(PROJ_BINDIR) 
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(PROJ_OBJDIR)/%.o: %.c $(MAKEFILE_NAME) | $(PROJ_OBJDIR) 
	$(CC) -c $(CFLAGS) $< -o $@

$(PROJ_OBJDIR)/%.o: %.cpp $(MAKEFILE_NAME) | $(PROJ_OBJDIR) 
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(PROJ_BINDIR):
	mkdir -p $@

$(PROJ_OBJDIR):
	mkdir -p $@

#######################################
# commands
#######################################

clean:
	-rm -fR $(PROJ_OBJDIR)/
	-rm -fR $(PROJ_BINDIR)/

-include $(wildcard $(PROJ_OBJDIR)/*.d)
# *** EOF ***