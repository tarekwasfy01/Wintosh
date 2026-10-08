set (SOURCE_FILES
    # From `AppInsights_Win10-UAP.vcxproj`.
    src/core/BaseTelemetryContext.cpp
    src/core/channel/TelemetryChannel.cpp
    src/core/channel/utils/HttpHeaderField.cpp
    src/core/channel/utils/HttpRequest.cpp
    src/core/channel/utils/HttpResponse.cpp
    src/core/common/JsonWriter.cpp
    src/core/common/Serializer.cpp
    src/core/common/StreamWriter.cpp
    src/core/common/StringWriter.cpp
    src/core/common/Utils.cpp
    src/core/contracts/Application.cpp
    src/core/contracts/Base.cpp
    src/core/contracts/CrashData.cpp
    src/core/contracts/CrashDataBinary.cpp
    src/core/contracts/CrashDataHeaders.cpp
    src/core/contracts/CrashDataThread.cpp
    src/core/contracts/CrashDataThreadFrame.cpp
    src/core/contracts/Data.cpp
    src/core/contracts/DataPoint.cpp
    src/core/contracts/Device.cpp
    src/core/contracts/Domain.cpp
    src/core/contracts/Envelope.cpp
    src/core/contracts/EventData.cpp
    src/core/contracts/ExceptionData.cpp
    src/core/contracts/ExceptionDetails.cpp
    src/core/contracts/Internal.cpp
    src/core/contracts/Location.cpp
    src/core/contracts/MessageData.cpp
    src/core/contracts/MetricData.cpp
    src/core/contracts/Operation.cpp
    src/core/contracts/PageViewData.cpp
    src/core/contracts/PageViewPerfData.cpp
    src/core/contracts/RemoteDependencyData.cpp
    src/core/contracts/RequestData.cpp
    src/core/contracts/Session.cpp
    src/core/contracts/SessionStateData.cpp
    src/core/contracts/StackFrame.cpp
    src/core/contracts/User.cpp
    src/core/TelemetryClient.cpp
    src/core/TelemetryContextOther.cpp
    src/core/TelemetryContextWin32.cpp
    src/core/TelemetryContextWinStore.cpp
    # From `ApplicationInsights.vcxproj`.
    Projects/AppInsights_Win10-UAP/ApplicationInsights/SessionTracking.cpp
    Projects/AppInsights_Win10-UAP/ApplicationInsights/PageTracking.cpp
    Projects/AppInsights_Win10-UAP/ApplicationInsights/TelemetryClient.cpp)

add_library (AppInsights STATIC ${SOURCE_FILES})
add_prep_dep (AppInsights)

target_compile_definitions (AppInsights PRIVATE
    ${WINOBJC_DEFS}
    # From `AppInsights_Win10-UAP.vcxproj`.
    TELEMETRY_CLIENT_DLL_EXPORT
    # From `ApplicationInsights.vcxproj`.
    _WINRT_DLL)

target_compile_options (AppInsights PRIVATE
    # From `AppInsights_Win10-UAP.vcxproj`.
    /wd4251 # For `<DisableSpecificWarnings>`.
    ${COMPILE_AS_WINRT})

target_include_directories (AppInsights PUBLIC
    # From `ApplicationInsights.vcxproj`.
    # For `<ProjectReference>`.
    Projects/AppInsights_Win10-UAP/ApplicationInsights)
