if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `MediaPlayerLib.vcxproj`.
        MediaPlayerFunctions.mm
        MPChangePlaybackRateCommand.mm
        MPChangePlaybackRateCommandEvent.mm
        MPContentItem.mm
        MPFeedbackCommand.mm
        MPFeedbackCommandEvent.mm
        MPMediaEntity.mm
        MPMediaItem.mm
        MPMediaItemArtwork.mm
        MPMediaItemCollection.mm
        MPMediaLibrary.mm
        MPMediaPickerController.mm
        MPMediaPlaylist.mm
        MPMediaPredicate.mm
        MPMediaPropertyPredicate.mm
        MPMediaQuery.mm
        MPMediaQuerySection.mm
        MPMovieAccessLog.mm
        MPMovieAccessLogEvent.mm
        MPMovieErrorLog.mm
        MPMovieErrorLogEvent.mm
        MPMoviePlayerController.mm
        MPMoviePlayerViewController.mm
        MPMusicPlayerController.mm
        MPNowPlayingInfoCenter.mm
        MPPlayableContentManager.mm
        MPRatingCommand.mm
        MPRatingCommandEvent.mm
        MPRemoteCommand.mm
        MPRemoteCommandCenter.mm
        MPRemoteCommandEvent.mm
        MPSeekCommandEvent.mm
        MPSkipIntervalCommand.mm
        MPSkipIntervalCommandEvent.mm
        MPTimedMetadata.mm
        MPVolumeView.mm
        UIViewController.mm)

    add_library (MediaPlayer SHARED ${SOURCE_FILES})
    woc_framework (MediaPlayer)

    target_include_directories (MediaPlayer PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (MediaPlayer PRIVATE
        ${WINOBJC_DEFS}
        # From `MediaPlayerLib.vcxproj`.
        MEDIAPLAYER_IMPEXP=)

    target_compile_options (MediaPlayer PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (MediaPlayer PRIVATE
        ${WOCFX_LIBS}
        # From `MediaPlayer.vcxproj`.
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/MediaPlayer/dll/\
MediaPlayer.def")
endif (NOT CL_COMPILER)
