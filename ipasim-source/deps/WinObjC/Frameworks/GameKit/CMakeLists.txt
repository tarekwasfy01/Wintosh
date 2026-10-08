if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `GameKitLib.vcxproj`.
        GKAchievement.mm
        GKAchievementChallenge.mm
        GKAchievementDescription.mm
        GKAchievementViewController.mm
        GKChallenge.mm
        GKChallengeEventHandler.mm
        GKFriendRequestComposeViewController.mm
        GKGameCenterViewController.mm
        GKInvite.mm
        GKLeaderboard.mm
        GKLeaderboardSet.mm
        GKLeaderboardViewController.mm
        GKLocalPlayer.mm
        GKMatch.mm
        GKMatchmaker.mm
        GKMatchmakerViewController.mm
        GKMatchRequest.mm
        GKNotificationBanner.mm
        GKPeerPickerController.mm
        GKPlayer.mm
        GKSavedGame.mm
        GKScore.mm
        GKScoreChallenge.mm
        GKSession.mm
        GKTurnBasedEventHandler.mm
        GKTurnBasedExchange.mm
        GKTurnBasedExchangeReply.mm
        GKTurnBasedMatch.mm
        GKTurnBasedMatchmakerViewController.mm
        GKTurnBasedParticipant.mm
        GKVoiceChat.mm
        GKVoiceChatService.mm
        GKConstants.mm)

    add_library (GameKit SHARED ${SOURCE_FILES})
    woc_framework (GameKit)

    target_include_directories (GameKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (GameKit PRIVATE
        ${WINOBJC_DEFS}
        # From `GameKitLib.vcxproj`.
        GAMEKIT_IMPEXP=)

    target_compile_options (GameKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (GameKit PRIVATE
        ${WOCFX_LIBS}
        # From `GameKit.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/GameKit/dll/\
GameKit.def")
endif (NOT CL_COMPILER)
