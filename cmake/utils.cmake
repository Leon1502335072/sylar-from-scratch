# 重定义目标源码的__FILE__宏，使用相对路径的形式，避免暴露敏感信息

# This function will overwrite the standard predefined macro（宏） "__FILE__".
# "__FILE__" expands to the name of the current input file, but cmake
# input the absolute path of source file, any code using the macro 
# would expose sensitive information, such as MORDOR_THROW_EXCEPTION(x),
# so we'd better overwirte it with filename.
function(force_redefine_file_macro_for_sources targetname)
    # 获取文件相关属性 获取的属性存在source_files中，
    get_target_property(source_files "${targetname}" SOURCES)
    foreach(sourcefile ${source_files})
        # Get source file's current list of compile definitions.
        get_property(defs SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS)
        # Get the relative path of the source file in project directory
        get_filename_component(filepath "${sourcefile}" ABSOLUTE)
        string(REPLACE ${PROJECT_SOURCE_DIR}/ "" relpath ${filepath})
        list(APPEND defs "__FILE__=\"${relpath}\"")
        # Set the updated compile definitions on the source file.
        set_property(
            SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS ${defs}
            )
    endforeach()
endfunction()

# wrapper for add_executable 其实就是包装了一下add_executable 
# 包装后新的函数名就是：sylar_add_executable
# 生成的可执行文件：targetname 
# 所需要的源文件：srcs
# 所需的依赖库：depends
# 需要链接的库：libs
# sylar_add_executable(test_fiber "tests/test_fiber.cc" sylar "${LIBS}")
function(sylar_add_executable targetname srcs depends libs)
    add_executable(${targetname} ${srcs})
    add_dependencies(${targetname} ${depends}) # 该条指令可以让编译器先生成可执行文件所需的依赖库
    force_redefine_file_macro_for_sources(${targetname})
    target_link_libraries(${targetname} ${libs})
endfunction()
