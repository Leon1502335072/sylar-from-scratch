# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.20

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Produce verbose output by default.
VERBOSE = 1

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Coroutines/sylar-from-scratch

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Coroutines/sylar-from-scratch

# Include any dependencies generated for this target.
include CMakeFiles/test_util.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/test_util.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/test_util.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/test_util.dir/flags.make

CMakeFiles/test_util.dir/tests/test_util.cpp.o: CMakeFiles/test_util.dir/flags.make
CMakeFiles/test_util.dir/tests/test_util.cpp.o: tests/test_util.cpp
CMakeFiles/test_util.dir/tests/test_util.cpp.o: CMakeFiles/test_util.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Coroutines/sylar-from-scratch/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/test_util.dir/tests/test_util.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) -D__FILE__=\"tests/test_util.cpp\" $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/test_util.dir/tests/test_util.cpp.o -MF CMakeFiles/test_util.dir/tests/test_util.cpp.o.d -o CMakeFiles/test_util.dir/tests/test_util.cpp.o -c /Coroutines/sylar-from-scratch/tests/test_util.cpp

CMakeFiles/test_util.dir/tests/test_util.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/test_util.dir/tests/test_util.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) -D__FILE__=\"tests/test_util.cpp\" $(CXX_INCLUDES) $(CXX_FLAGS) -E /Coroutines/sylar-from-scratch/tests/test_util.cpp > CMakeFiles/test_util.dir/tests/test_util.cpp.i

CMakeFiles/test_util.dir/tests/test_util.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/test_util.dir/tests/test_util.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) -D__FILE__=\"tests/test_util.cpp\" $(CXX_INCLUDES) $(CXX_FLAGS) -S /Coroutines/sylar-from-scratch/tests/test_util.cpp -o CMakeFiles/test_util.dir/tests/test_util.cpp.s

# Object files for target test_util
test_util_OBJECTS = \
"CMakeFiles/test_util.dir/tests/test_util.cpp.o"

# External object files for target test_util
test_util_EXTERNAL_OBJECTS =

bin/test_util: CMakeFiles/test_util.dir/tests/test_util.cpp.o
bin/test_util: CMakeFiles/test_util.dir/build.make
bin/test_util: lib/libsylar.so
bin/test_util: CMakeFiles/test_util.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Coroutines/sylar-from-scratch/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable bin/test_util"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/test_util.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/test_util.dir/build: bin/test_util
.PHONY : CMakeFiles/test_util.dir/build

CMakeFiles/test_util.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/test_util.dir/cmake_clean.cmake
.PHONY : CMakeFiles/test_util.dir/clean

CMakeFiles/test_util.dir/depend:
	cd /Coroutines/sylar-from-scratch && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Coroutines/sylar-from-scratch /Coroutines/sylar-from-scratch /Coroutines/sylar-from-scratch /Coroutines/sylar-from-scratch /Coroutines/sylar-from-scratch/CMakeFiles/test_util.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/test_util.dir/depend

