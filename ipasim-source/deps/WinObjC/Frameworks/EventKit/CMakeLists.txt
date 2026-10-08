if (NOT CL_COMPILER)
    set (SOURCE_FILES
        # From `EventKitLib.vcxproj`.
        EKAlarm.mm
        EKCalendar.mm
        EKCalendarItem.mm
        EKEvent.mm
        EKEventStore.mm
        EKObject.mm
        EKParticipant.mm
        EKRecurrenceDayOfWeek.mm
        EKRecurrenceEnd.mm
        EKRecurrenceRule.mm
        EKReminder.mm
        EKSource.mm
        EKStructuredLocation.mm
        EventKitConstants.mm)

    add_library (EventKit SHARED ${SOURCE_FILES})
    woc_framework (EventKit)

    target_include_directories (EventKit PRIVATE ${WOCFX_INCLUDE_DIRS})

    target_compile_definitions (EventKit PRIVATE
        ${WINOBJC_DEFS}
        # From `EventKitLib.vcxproj`.
        EVENTKIT_IMPEXP=)

    target_compile_options (EventKit PRIVATE ${WINOBJC_CLANG_OPTIONS})

    target_link_libraries (EventKit PRIVATE
        ${WOCFX_LIBS}
        # From `EventKit.vcxproj`.
        "-Wl,/def:${CMAKE_CURRENT_SOURCE_DIR}/../../build/EventKit/dll/\
EventKit.def")
endif (NOT CL_COMPILER)
