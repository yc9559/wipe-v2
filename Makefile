# Makefile for Project WIPE-v2
# Author: Matt Yang

CC 			:= gcc
CXX 		:= g++
CFLAGS 		:= -std=c99
CXXFLAGS 	:= -std=c++11
LDFLAGS 	:= -Wl,--as-needed

SRC_DIR		:= ./source
INC_DIR		:= ./source
BUILD_DIR	:= ./build
DEP_DIR		:= $(BUILD_DIR)/dep

BIN_NAME	:= wipe
REL_FLAGS	:= -O3
REL_DEFINES	:= 
DBG_FLAGS	:= -O0 -g
DBG_DEFINES	:= 

EXT_LIB_INC	:= 
EXT_LIBS	:=

INC			:= $(shell find $(INC_DIR) -name '*.h')
INC 		+= $(shell find $(INC_DIR) -name '*.hpp')
SRC			:= $(shell find $(SRC_DIR) -name '*.c')
SRC 		+= $(shell find $(SRC_DIR) -name '*.cpp')

OBJS		:= $(foreach f,$(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SRC))),$(BUILD_DIR)/$(f))
INCLUDES	:= $(foreach f,$(sort $(dir $(INC))),-I$(f)) $(EXT_LIB_INC)
LIBS 		:= -lpthread $(EXT_LIBS)

# 生成.c/cpp对.h的依赖，由include插入到makefile, 不使用冒号用于延后展开 $@ 和 $(*F)
# http://maskray.me/blog/2011-08-11-generate-dependency-in-makefile
DEP_FLAGS	= -MM -MP -MT $@ -MF $(DEP_DIR)/$(*F).d

# gcc并不会自己生成目录
$(shell mkdir -p $(DEP_DIR) > /dev/null)
$(shell mkdir -p $(dir $(OBJS)) > /dev/null)

.PHONY: all release debug clean help

all: release

release: MODE_FLAG 	= $(REL_FLAGS)
release: DEFINES 	= $(REL_DEFINES)
release: $(OBJS)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(DEFINES) $(MODE_FLAG) -o $(BIN_NAME) $^ $(LIBS)
	@echo -e ' bin\t ./$(BIN_NAME)'
	@echo -e '\033[32m\033[1m build $@ done. \033[0m'

debug: MODE_FLAG 	= $(DBG_FLAGS)
debug: DEFINES 		= $(DBG_DEFINES)
debug: $(OBJS)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(DEFINES) $(MODE_FLAG) -o $(BIN_NAME) $^ $(LIBS)
	@echo -e ' bin\t ./$(BIN_NAME)'
	@echo -e '\033[32m\033[1m build $@ done. \033[0m'

$(BUILD_DIR)/%.o: %.c
	@echo -e ' cc\t $<'
	@$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) $(MODE_FLAG) $(DEP_FLAGS) $<
	@$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) $(MODE_FLAG) -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@echo -e ' cxx\t $<'
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) $(MODE_FLAG) $(DEP_FLAGS) $<
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(DEFINES) $(MODE_FLAG) -c $< -o $@

-include $(foreach f,$(notdir $(basename $(SRC))),$(DEP_DIR)/$(f).d)

clean:
	@rm -rf $(BUILD_DIR)
	@echo -e '\033[32m\033[1m clean done. \033[0m'

help:
	@echo -e 'Makefile for Project WIPE-v2'
	@echo -e 'Author: Matt Yang'
	@echo -e 'make release -j4 \tbuild for actual use'
	@echo -e 'make debug -j4 \t\tbuild for development'
	@echo -e 'make clean \t\tnecessary when switching between "release" and "debug"'
