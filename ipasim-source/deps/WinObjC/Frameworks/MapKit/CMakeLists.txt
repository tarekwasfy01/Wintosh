if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `MapKitLib.vcxproj`.
        MapKitConstants.mm
        MapKitFunctions.mm
        MKAnnotationView.mm
        MKCircle.mm
        MKCircleRenderer.mm
        MKCircleView.mm
        MKDirections.mm
        MKDirectionsRequest.mm
        MKDirectionsResponse.mm
        MKDistanceFormatter.mm
        MKETAResponse.mm
        MKGeodesicPolyline.mm
        MKLocalSearch.mm
        MKLocalSearchRequest.mm
        MKLocalSearchResponse.mm
        MKMapCamera.mm
        MKMapItem.mm
        MKMapSnapshot.mm
        MKMapSnapshotOptions.mm
        MKMapSnapshotter.mm
        MKMapView.mm
        MKMultiPoint.mm
        MKOverlayPathRenderer.mm
        MKOverlayPathView.mm
        MKOverlayRenderer.mm
        MKOverlayView.mm
        MKPinAnnotationView.mm
        MKPlacemark.mm
        MKPointAnnotation.mm
        MKPolygon.mm
        MKPolygonRenderer.mm
        MKPolygonView.mm
        MKPolyline.mm
        MKPolylineRenderer.mm
        MKPolylineView.mm
        MKReverseGeocoder.mm
        MKRoute.mm
        MKRouteStep.mm
        MKShape.mm
        MKTileOverlay.mm
        MKTileOverlayRenderer.mm
        MKUserLocation.mm
        MKUserTrackingBarButtonItem.mm)

    add_library (MapKit SHARED ${SOURCE_FILES})
    woc_framework (MapKit)

    target_include_directories (MapKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (MapKit PRIVATE
        ${WINOBJC_DEFS}
        # From `MapKitLib.vcxproj`.
        MAPKIT_IMPEXP=)

    target_compile_options (MapKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (MapKit PRIVATE
        ${WOCFX_LIBS}
        # From `MapKit.vcxproj`.
        CoreLocation
        UIKit
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/MapKit/dll/\
MapKit.def")
endif (NOT CL_COMPILER)
