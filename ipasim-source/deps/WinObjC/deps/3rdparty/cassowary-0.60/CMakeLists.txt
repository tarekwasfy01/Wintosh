set (SOURCE_FILES
    # From `cassowary-0.60-Windows.vcxproj`
    c++/ClAbstractVariable.cc
    c++/ClConstraint.cc
    c++/ClDummyVariable.cc
    c++/ClFDVariable.cc
    c++/ClFloatVariable.cc
    c++/ClLinearExpression.cc
    c++/ClSimplexSolver.cc
    c++/ClSlackVariable.cc
    c++/ClSolver.cc
    c++/ClStrength.cc
    c++/ClSymbolicWeight.cc
    c++/ClTableau.cc
    c++/ClVariable.cc
    c++/ClLinearEquation.cc)

add_library (cassowary STATIC ${SOURCE_FILES})
add_prep_dep (cassowary)

target_compile_definitions (cassowary PRIVATE
    _MT _DLL
    # From `cassowary-0.60-Windows.vcxproj`
    $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>
    $<$<CONFIG:Debug>:HAVE_CONFIG_H>
    CL_NO_IO
    CL_NO_ID
    WIN32
    $<IF:$<CONFIG:Debug>,_DEBUG,NDEBUG>
    _WINDOWS
    _USRDLL
    CASSOWARY060WINDOWS_EXPORTS
    VERSION="0.60"
    $<$<CONFIG:Debug>:_CRT_SECURE_NO_WARNINGS>)
