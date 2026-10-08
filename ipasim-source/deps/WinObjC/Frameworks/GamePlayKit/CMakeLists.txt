if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `GamePlayKitLib.vcxproj`.
        GKAgent.mm
        GKAgent2D.mm
        GKARC4RandomSource.mm
        GKBehavior.mm
        GKCircleObstacle.mm
        GKComponent.mm
        GKComponentSystem.mm
        GKEntity.mm
        GKGaussianDistribution.mm
        GKGoal.mm
        GKGraph.mm
        GKGraphNode.mm
        GKGraphNode2D.mm
        GKGridGraph.mm
        GKGridGraphNode.mm
        GKLinearCongruentialRandomSource.mm
        GKMersenneTwisterRandomSource.mm
        GKMinmaxStrategist.mm
        GKNSPredicateRule.mm
        GKObstacle.mm
        GKObstacleGraph.mm
        GKPath.mm
        GKPolygonObstacle.mm
        GKRandomDistribution.mm
        GKRandomSource.mm
        GKRule.mm
        GKRuleSystem.mm
        GKShuffledDistribution.mm
        GKState.mm
        GKStateMachine.mm)

    add_library (GamePlayKit SHARED ${SOURCE_FILES})
    woc_framework (GamePlayKit)

    target_include_directories (GamePlayKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (GamePlayKit PRIVATE
        ${WINOBJC_DEFS}
        # From `GamePlayKitLib.vcxproj`.
        GAMEPLAYKIT_IMPEXP=)

    target_compile_options (GamePlayKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (GamePlayKit PRIVATE
        ${WOCFX_LIBS}
        # From `GamePlayKit.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/GamePlayKit/dll/\
GamePlayKit.def")
endif (NOT CL_COMPILER)
