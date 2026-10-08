if (CL_COMPILER)
    set (SOURCE_FILES
        # From `UIKitLib.vcxproj`.
        StarboardXaml/XamlCompositor.cpp
        StarboardXaml/DisplayTexture.cpp
        StarboardXaml/LayerCoordinator.cpp
        StarboardXaml/StoryboardManager.cpp
        StarboardXaml/StarboardXaml.cpp
        StarboardXaml/LayerAnimation.cpp
        StarboardXaml/LayerProxy.cpp)

    add_library (UIKit STATIC ${SOURCE_FILES})

    target_include_directories (UIKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_options (UIKit PRIVATE
        "/FU${CMAKE_CURRENT_SOURCE_DIR}/../UIKit.Xaml/prebuilt/\
${CMAKE_BUILD_TYPE}/UIKit.Xaml.winmd"
        /ZW)

    target_link_libraries (UIKit PRIVATE Logging)
else (CL_COMPILER)
    set (SOURCE_FILES
        # From `UIKitLib.vcxproj`.
        art.mm
        ForceInclusion.mm
        NSAttributedString+UIKitAdditions.mm
        NSIndexPath+UITableView.mm
        NSString+UIKitAdditions.mm
        NSValue+UIKitAdditions.mm
        NSLayoutConstraint.mm
        UIAccessibility.mm
        UIActionSheet.mm
        UIAlertView.mm
        UIAnimationNotification.mm
        UIAppearanceSetter.mm
        UIApplication.mm
        UIActivity.mm
        UIActivityViewController.mm
        UIApplicationMain.mm
        UIBarButtonItem.mm
        UIBarItem.mm
        UIBezierPath.mm
        UIButton.mm
        UIButtonContent.mm
        UIClassSwapper.mm
        UIColor.mm
        UICGColor.mm
        UIControl.mm
        UIDevice.mm
        UIDocument.mm
        UIEmptyView.mm
        UIEvent.mm
        UIFont.mm
        UIFontDescriptor.mm
        UIGeometry.mm
        UIGestureRecognizer.mm
        UIGraphicsFunctions.mm
        UIImage.mm
        UIImageCachedObject.mm
        UIImageNibPlaceholder.mm
        UIImageView.mm
        UILabel.mm
        UIButtonProxies.mm
        UILocalNotification.mm
        UIManagedDocument.mm
        UIMotionEffect.mm
        UINavigationBar.mm
        UINavigationController.mm
        UINavigationItem.mm
        UINavigationPane.mm
        UINib.mm
        UINibUnarchiver.mm
        UIPanGestureRecognizer.mm
        _UIDirectManipulationRecognizer.mm
        UIPageViewController.mm
        UIPasteboard.mm
        UIPickerView.mm
        UIPopoverPresentationController.mm
        UIPresentationController.mm
        UIProxyObject.mm
        UIProxyObjectPair.mm
        UIResponder.mm
        UIRuntimeEventConnection.mm
        UIRuntimeOutletConnection.mm
        UIScreen.mm
        UIScrollView.mm
        UISearchBar.mm
        UISegment.mm
        UISegmentedControl.mm
        UISlider.mm
        UIStoryboard.mm
        UIStoryboardModalSegueTemplate.mm
        UISwitch.mm
        UITabBar.mm
        UITabBarButton.mm
        UITabBarController.mm
        UITabBarItem.mm
        UITableView.mm
        UITableViewCell.mm
        UITableViewContentView.mm
        UITableViewController.mm
        UITableViewDataSource.mm
        UITableViewHeaderFooterView.mm
        UITableViewRow.mm
        UITableViewSection.mm
        UITabPane.mm
        UITapGestureRecognizer.mm
        XamlControls.mm
        XamlUtilities.mm
        UITextField.mm
        UITouch.mm
        UIUserNotificationSettings.mm
        UIView.mm
        UIViewController.mm
        UIWindow.mm
        _TableCellAnimationHelper.mm
        _UIGroupEdgeView.mm
        _UIPopupViewController.mm
        UIActivityIndicatorView.mm
        UIDatePicker.mm
        UIPinchGestureRecognizer.mm
        UIProgressView.mm
        UITableViewCellContentView.mm
        UITextView.mm
        UIToolbar.mm
        UIWebView.mm
        UISwipeGestureRecognizer.mm
        UICollectionView.mm
        UICollectionViewCell.mm
        UIGridLayoutSection.mm
        UICollectionViewController.mm
        UICollectionViewItemKey.mm
        UICollectionViewFlowLayout.mm
        UICollectionViewData.mm
        UIGridLayoutRow.mm
        UICollectionViewLayout.mm
        UIGridLayoutItem.mm
        UICollectionViewUpdateItem.mm
        UIGridLayoutInfo.mm
        UrlLauncher.mm
        NSLayoutManager.mm
        NSParagraphStyle.mm
        NSTextContainer.mm
        NSTextStorage.mm
        UIRuntimeOutletCollectionConnection.mm
        NSStringDrawingContext.mm
        UITextRange.mm
        NSBundle+UIKitAdditions.mm
        NSDataAsset.mm
        NSFileProviderExtension.mm
        NSLayoutAnchor.mm
        NSLayoutDimension.mm
        NSLayoutXAxisAnchor.mm
        NSLayoutYAxisAnchor.mm
        NSMutableParagraphStyle.mm
        NSObject+UIKitAdditions.mm
        NSShadow.mm
        NSTextAttachment.mm
        NSTextTab.mm
        UIAcceleration.mm
        UIAccelerometer.mm
        UIAccessibilityCustomAction.mm
        UIAccessibilityElement.mm
        UIActivityItemProvider.mm
        UIAlertAction.mm
        UIAlertController.mm
        UIApplicationShortcutIcon.mm
        UIApplicationShortcutItem.mm
        UIAttachmentBehavior.mm
        UIBarButtonItemGroup.mm
        UIBlurEffect.mm
        UICollectionReusableView.mm
        UICollectionViewFlowLayoutInvalidationContext.mm
        UICollectionViewFocusUpdateContext.mm
        UICollectionViewLayoutAttributes.mm
        UICollectionViewLayoutInvalidationContext.mm
        UICollectionViewTransitionLayout.mm
        UICollisionBehavior.mm
        UIDictationPhrase.mm
        UIDocumentInteractionController.mm
        UIDocumentMenuViewController.mm
        UIDocumentPickerExtensionViewController.mm
        UIDocumentPickerViewController.mm
        UIDynamicAnimator.mm
        UIDynamicBehavior.mm
        UIDynamicItemBehavior.mm
        UIDynamicItemGroup.mm
        UIFieldBehavior.mm
        UIFocusAnimationCoordinator.mm
        UIFocusGuide.mm
        UIFocusUpdateContext.mm
        UIGraphics.mm
        UIGravityBehavior.mm
        UIImageAsset.mm
        UIImagePickerController.mm
        UIImagePickerControllerDelegate.mm
        UIInputView.mm
        UIInputViewController.mm
        UIInterpolatingMotionEffect.mm
        UIKeyCommand.mm
        UILayoutGuide.mm
        UILexicon.mm
        UILexiconEntry.mm
        UILocalizedIndexedCollation.mm
        UILongPressGestureRecognizer.mm
        UIMarkupTextPrintFormatter.mm
        UIMenuController.mm
        UIMenuItem.mm
        UIMotionEffectGroup.mm
        UIMutableApplicationShortcutItem.mm
        UIMutableUserNotificationAction.mm
        UIMutableUserNotificationCategory.mm
        UIPageControl.mm
        UIPercentDrivenInteractiveTransition.mm
        UIPopoverBackgroundView.mm
        UIPopoverController.mm
        WYPopoverController.mm
        WYStoryboardPopoverSegue.m
        UIPress.mm
        UIPressesEvent.mm
        UIPreviewAction.mm
        UIPreviewActionGroup.mm
        UIPrinter.mm
        UIPrinterPickerController.mm
        UIPrintFormatter.mm
        UIPrintInfo.mm
        UIPrintInteractionController.mm
        UIPrintPageRenderer.mm
        UIPrintPaper.mm
        UIPushBehavior.mm
        UIReferenceLibraryViewController.mm
        UIRefreshControl.mm
        UIRegion.mm
        UIRotationGestureRecognizer.mm
        UIScreenEdgePanGestureRecognizer.mm
        UIScreenMode.mm
        UISearchContainerViewController.mm
        UISearchController.mm
        UISearchDisplayController.mm
        UISimpleTextPrintFormatter.mm
        UISnapBehavior.mm
        UISplitViewController.mm
        UIStackView.mm
        UIStateRestoring.mm
        UIStepper.mm
        UIStoryboardPopoverSegue.mm
        UIStoryboardSegue.mm
        UITableViewFocusUpdateContext.mm
        UITableViewRowAction.mm
        UITextChecker.mm
        UITextInput.mm
        UITextInputAssistantItem.mm
        UITextInputMode.mm
        UITextInputStringTokenizer.mm
        UITextPosition.mm
        UITextSelectionRect.mm
        UITraitCollection.mm
        UIUserNotificationAction.mm
        UIUserNotificationCategory.mm
        UIVibrancyEffect.mm
        UIVideoEditorController.mm
        UIViewControllerContextTransitioning.mm
        UIViewPrintFormatter.mm
        UIVisualEffect.mm
        UIVisualEffectView.mm
        UICTFont.mm
        UICTFontDescriptor.mm
        _UIGestureCoordinator.mm
        StarboardXaml/XamlCompositor.mm
        StarboardXaml/DisplayProperties.mm
        StarboardXaml/DisplayTexture.mm
        StarboardXaml/LayerAnimation.mm
        StarboardXaml/LayerProxy.mm
        StarboardXaml/LayerTransaction.mm
        ../Starboard/Quaternion.mm
        StarboardXaml/StringConversion.mm
        StarboardXaml/ApplicationMain.mm
        StarboardXaml/UWPBackgroundTask.mm
        # From `UIKit.vcxproj`.
        NSCoder+UIKitAdditions.mm
        NSMutableAttributedString+UIKitAdditions.mm)

    add_objcuwp_libs ()

    add_library (UIKit SHARED ${SOURCE_FILES})
    woc_framework (UIKit)

    target_include_directories (UIKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (UIKit PRIVATE
        ${WINOBJC_DEFS}
        # From `UIKitLib.vcxproj`.
        STARBOARD_PORT=1
        UIKIT_IMPEXP=
        GAMEKIT_IMPEXP=
        SOCIAL_IMPEXP=)

    target_compile_options (UIKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    # See #9.
    add_library (UIKit.Xaml SHARED IMPORTED)
    set_target_properties (UIKit.Xaml PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/../UIKit.Xaml/prebuilt/\
${CMAKE_BUILD_TYPE}/UIKit.Xaml.dll"
        IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/../UIKit.Xaml/prebuilt/\
${CMAKE_BUILD_TYPE}/UIKit.Xaml.lib")

    target_link_libraries (UIKit PRIVATE
        ${WOCFX_LIBS}
        woc-UIKit
        # From `UIKit.vcxproj`.
        CoreText
        # TODO: AutoLayout
        CoreGraphics
        ImageIO
        QuartzCore
        CoreData
        UIKit.Xaml
        libxml2
        RTObjCInterop
        # TODO: DWrite.lib;Windowscodecs.lib
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/UIKit/dll/\
UIKit.def")

    # From `UIKitLib.vcxproj`.
    set_source_files_properties (
        UIActionSheet.mm
        UIApplication.mm
        UIButton.mm
        UILabel.mm
        UIButtonProxies.mm
        UIScrollView.mm
        UISlider.mm
        XamlUtilities.mm
        UITextField.mm
        UIWindow.mm
        _UIPopupViewController.mm
        UIActivityIndicatorView.mm
        UIProgressView.mm
        UICollectionView.mm
        UICollectionViewCell.mm
        UIGridLayoutSection.mm
        UICollectionViewController.mm
        UICollectionViewItemKey.mm
        UICollectionViewFlowLayout.mm
        UICollectionViewData.mm
        UIGridLayoutRow.mm
        UICollectionViewLayout.mm
        UICollectionViewUpdateItem.mm
        UIGridLayoutInfo.mm
        UICollectionReusableView.mm
        UICollectionViewLayoutAttributes.mm
        WYPopoverController.mm
        WYStoryboardPopoverSegue.m
        UWPBackgroundTask.mm
        PROPERTIES COMPILE_OPTIONS "${OBJC_ARC}")
endif (CL_COMPILER)
