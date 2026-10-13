#!/usr/bin/make -f
NAME = CrateChop

FILES_DSP = CrateChopPlugin.cpp ChopEngine.cpp
FILES_UI  = CrateChopUI.cpp

include ../../Makefile.plugins.mk

TARGETS += vst3
TARGETS += clap
ifeq ($(MACOS),true)
TARGETS += au
endif

all: $(TARGETS)
