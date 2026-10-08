if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `AVFoundationLib.vcxproj`.
        AVAsset.mm
        AVAssetExportSession.mm
        AVAssetImageGenerator.mm
        AVAssetReader.mm
        AVAssetReaderAudioMixOutput.mm
        AVAssetReaderOutput.mm
        AVAssetReaderOutputMetadataAdaptor.mm
        AVAssetReaderSampleReferenceOutput.mm
        AVAssetReaderTrackOutput.mm
        AVAssetReaderVideoCompositionOutput.mm
        AVAssetResourceLoader.mm
        AVAssetResourceLoadingContentInformationRequest.mm
        AVAssetResourceLoadingDataRequest.mm
        AVAssetResourceLoadingRequest.mm
        AVAssetResourceRenewalRequest.mm
        AVAssetTrack.mm
        AVAssetTrackGroup.mm
        AVAssetTrackSegment.mm
        AVAssetWriter.mm
        AVAssetWriterInput.mm
        AVAssetWriterInputGroup.mm
        AVAssetWriterInputMetadataAdaptor.mm
        AVAssetWriterInputPassDescription.mm
        AVAssetWriterInputPixelBufferAdaptor.mm
        AVAsynchronousVideoCompositionRequest.mm
        AVAudioBuffer.mm
        AVAudioChannelLayout.mm
        AVAudioEngine.mm
        AVAudioEnvironmentDistanceAttenuationParameters.mm
        AVAudioEnvironmentNode.mm
        AVAudioEnvironmentReverbParameters.mm
        AVAudioFile.mm
        AVAudioFormat.mm
        AVAudioInputNode.mm
        AVAudioIONode.mm
        AVAudioMix.mm
        AVAudioMixerNode.mm
        AVAudioMixInputParameters.mm
        AVAudioNode.mm
        AVAudioOutputNode.mm
        AVAudioPCMBuffer.mm
        AVAudioPlayer.mm
        AVAudioPlayerNode.mm
        AVAudioRecorder.mm
        AVAudioSession.mm
        AVAudioSessionChannelDescription.mm
        AVAudioSessionDataSourceDescription.mm
        AVAudioSessionPortDescription.mm
        AVAudioSessionRouteDescription.mm
        AVAudioTime.mm
        AVAudioUnit.mm
        AVAudioUnitDelay.mm
        AVAudioUnitDistortion.mm
        AVAudioUnitEffect.mm
        AVAudioUnitEQ.mm
        AVAudioUnitEQFilterParameters.mm
        AVAudioUnitGenerator.mm
        AVAudioUnitMIDIInstrument.mm
        AVAudioUnitReverb.mm
        AVAudioUnitSampler.mm
        AVAudioUnitTimeEffect.mm
        AVAudioUnitTimePitch.mm
        AVAudioUnitVarispeed.mm
        AVCaptureAudioChannel.mm
        AVCaptureAudioDataOutput.mm
        AVCaptureAutoExposureBracketedStillImageSettings.mm
        AVCaptureBracketedStillImageSettings.mm
        AVCaptureConnection.mm
        AVCaptureDevice.mm
        AVCaptureDeviceFormat.mm
        AVCaptureDeviceInput.mm
        AVCaptureFileOutput.mm
        AVCaptureInput.mm
        AVCaptureInputPort.mm
        AVCaptureManualExposureBracketedStillImageSettings.mm
        AVCaptureMetadataOutput.mm
        AVCaptureMovieFileOutput.mm
        AVCaptureOutput.mm
        AVCaptureSession.mm
        AVCaptureStillImageOutput.mm
        AVCaptureVideoDataOutput.mm
        AVCaptureVideoPreviewLayer.mm
        AVComposition.mm
        AVCompositionTrack.mm
        AVCompositionTrackSegment.mm
        AVFrameRateRange.mm
        AVMediaSelectionGroup.mm
        AVMediaSelectionOption.mm
        AVMetadataFaceObject.mm
        AVMetadataItem.mm
        AVMetadataItemFilter.mm
        AVMetadataMachineReadableCodeObject.mm
        AVMetadataObject.mm
        AVMIDIPlayer.mm
        AVMutableAudioMix.mm
        AVMutableAudioMixInputParameters.mm
        AVMutableComposition.mm
        AVMutableCompositionTrack.mm
        AVMutableMetadataItem.mm
        AVMutableTimedMetadataGroup.mm
        AVMutableVideoComposition.mm
        AVMutableVideoCompositionInstruction.mm
        AVMutableVideoCompositionLayerInstruction.mm
        AVOutputSettingsAssistant.mm
        AVPlayer.mm
        AVPlayerItem.mm
        AVPlayerItemAccessLog.mm
        AVPlayerItemAccessLogEvent.mm
        AVPlayerItemErrorLog.mm
        AVPlayerItemErrorLogEvent.mm
        AVPlayerItemLegibleOutput.mm
        AVPlayerItemMetadataOutput.mm
        AVPlayerItemOutput.mm
        AVPlayerItemTrack.mm
        AVPlayerItemVideoOutput.mm
        AVPlayerLayer.mm
        AVPlayerMediaSelectionCriteria.mm
        AVQueuePlayer.mm
        AVSampleBufferDisplayLayer.mm
        AVSpeechSynthesisVoice.mm
        AVSpeechSynthesizer.mm
        AVSpeechUtterance.mm
        AVSynchronizedLayer.mm
        AVTextStyleRule.mm
        AVTimedMetadataGroup.mm
        AVURLAsset.mm
        AVVideoComposition.mm
        AVVideoCompositionCoreAnimationTool.mm
        AVVideoCompositionLayerInstruction.mm
        AVVideoCompositionRenderContext.mm
        AVFoundationConstants.mm
        AVMetadataGroup.mm
        AVVideoCompositionInstruction.mm)

    add_library (AVFoundation SHARED ${SOURCE_FILES})
    woc_framework (AVFoundation)

    target_include_directories (AVFoundation PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (AVFoundation PRIVATE
        ${WINOBJC_DEFS}
        # From `AVFoundationLib.vcxproj`.
        AVFOUNDATION_IMPEXP=)

    target_compile_options (AVFoundation PRIVATE
        ${WINOBJC_CLANG_OPTIONS}
        # From `AVFoundationLib.vcxproj`.
        "${OBJC_ARC}")

    target_link_libraries (AVFoundation PRIVATE
        ${WOCFX_LIBS}
        # From `AVFoundation.vcxproj`.
        UIKit
        QuartzCore
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/AVFoundation/dll/\
AVFoundation.def")
endif (NOT CL_COMPILER)
