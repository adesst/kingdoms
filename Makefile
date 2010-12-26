CXX      ?= g++
CXXFLAGS ?= -O2
CXXFLAGS += -Wall
LDFLAGS  ?= -lSDL -lSDL_image -lSDL_ttf

BINDIR = bin
TARGET = $(BINDIR)/main
SRCDIR = src
SRCFILES = color.cpp sdl-utils.cpp utils.cpp rect.cpp \
	   resource_configuration.cpp advance.cpp \
	   city_improvement.cpp unit.cpp \
	   city.cpp map.cpp fog_of_war.cpp \
	   government.cpp civ.cpp \
	   pompelmous.cpp \
	   astar.cpp map-astar.cpp ai-orders.cpp ai-objective.cpp \
	   ai-debug.cpp ai-exploration.cpp ai-expansion.cpp \
	   ai-defense.cpp ai-offense.cpp ai-commerce.cpp \
	   ai.cpp \
	   gui-utils.cpp main_window.cpp city_window.cpp \
	   diplomacy_window.cpp gui.cpp \
	   main.cpp
OBJS   = $(SRCS:.cpp=.o)
DEPS   = $(SRCS:.cpp=.dep)

SRCS = $(addprefix $(SRCDIR)/, $(SRCFILES))

.PHONY: clean all

all: $(BINDIR) $(TARGET)

$(BINDIR):
	mkdir -p $(BINDIR)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $(TARGET)

%.cpp.o: %.dep
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.dep: %.cpp
	@rm -f $@
	@$(CC) -MM $(CPPFLAGS) $< > $@.P
	@sed 's,\($(notdir $*)\)\.o[ :]*,$(dir $*)\1.o $@ : ,g' < $@.P > $@
	@rm -f $@.P

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -rf $(BINDIR)

-include $(DEPS)

