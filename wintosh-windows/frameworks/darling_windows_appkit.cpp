/* Minimal Win32-backed AppKit process entry point; GPL-3.0-only. */
#include "darling_windows_appkit.h"
#include "../abi/darling_windows_objc.h"
#include "../abi/darling_windows_dispatch.h"
#include "darling_windows_corefoundation.h"
#include "darling_windows_coregraphics.h"
#include "darling_windows_preferences.h"
#include "../loader/darling_windows_macho.h"
#include "../platform/darling_windows_stdio.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" {
HANDLE darling_windows_dispatch_duplicate_descriptor_handle(int descriptor);
id NSFontAttributeName = nullptr;
id NSForegroundColorAttributeName = nullptr;
id NSApplicationWillFinishLaunchingNotification = nullptr;
id NSApplicationDidFinishLaunchingNotification = nullptr;
id NSApplicationWillTerminateNotification = nullptr;
id NSApplicationWillBecomeActiveNotification = nullptr;
id NSApplicationDidBecomeActiveNotification = nullptr;
id NSApplicationWillResignActiveNotification = nullptr;
id NSApplicationDidResignActiveNotification = nullptr;
id NSApplicationWillHideNotification = nullptr;
id NSApplicationDidHideNotification = nullptr;
id NSApplicationWillUnhideNotification = nullptr;
id NSApplicationDidUnhideNotification = nullptr;
id NSBundleDidLoadNotification = nullptr;
id NSLoadedClasses = nullptr;
std::int64_t NSExecutableErrorMinimum = 3584;
std::int64_t NSExecutableNotLoadableError = 3584;
std::int64_t NSExecutableArchitectureMismatchError = 3585;
std::int64_t NSExecutableRuntimeMismatchError = 3586;
std::int64_t NSExecutableLoadError = 3587;
std::int64_t NSExecutableLinkError = 3588;
std::int64_t NSExecutableErrorMaximum = 3839;
std::int64_t NSBundleExecutableArchitectureI386 = 7;
std::int64_t NSBundleExecutableArchitectureX86_64 = 0x01000007;
std::int64_t NSBundleExecutableArchitectureARM64 = 0x0100000c;
std::int64_t NSBundleExecutableArchitecturePPC = 18;
std::int64_t NSBundleExecutableArchitecturePPC64 = 0x01000012;
id NSWindowWillCloseNotification = nullptr;
id NSWindowDidResizeNotification = nullptr;
id NSWindowDidBecomeKeyNotification = nullptr;
id NSWindowDidResignKeyNotification = nullptr;
id NSWindowDidBecomeMainNotification = nullptr;
id NSWindowDidResignMainNotification = nullptr;
id NSCocoaErrorDomain = nullptr;
id NSLocalizedDescriptionKey = nullptr;
id NSFileType = nullptr;
id NSFileSize = nullptr;
id NSFileModificationDate = nullptr;
id NSFileCreationDate = nullptr;
id NSFilePosixPermissions = nullptr;
id NSFileSystemSize = nullptr;
id NSFileSystemFreeSize = nullptr;
id NSFileTypeRegular = nullptr;
id NSFileTypeDirectory = nullptr;
id NSFileTypeSymbolicLink = nullptr;
id NSFileTypeUnknown = nullptr;
}

namespace {
constexpr wchar_t kWindowClass[] = L"Wintosh.AppKitBootstrap";
constexpr UINT kApplicationStopMessage = WM_APP + 0x3a1;
constexpr UINT kApplicationTerminateMessage = WM_APP + 0x3a2;
constexpr UINT kApplicationEventMessage = WM_APP + 0x3a3;
constexpr std::uint64_t kEventLeftMouseDown = 1;
constexpr std::uint64_t kEventLeftMouseUp = 2;
constexpr std::uint64_t kEventRightMouseDown = 3;
constexpr std::uint64_t kEventRightMouseUp = 4;
constexpr std::uint64_t kEventMouseMoved = 5;
constexpr std::uint64_t kEventLeftMouseDragged = 6;
constexpr std::uint64_t kEventRightMouseDragged = 7;
constexpr std::uint64_t kEventKeyDown = 10;
constexpr std::uint64_t kEventKeyUp = 11;
constexpr std::uint64_t kEventFlagsChanged = 12;
constexpr std::uint64_t kEventScrollWheel = 22;
constexpr wchar_t kButtonOriginalProcProperty[] = L"Wintosh.ButtonOriginalProc";
const char g_view_menu_key = 0;
std::once_flag g_window_class_once;
std::once_flag g_appkit_classes_once;
bool g_window_class_registered = false;
std::atomic_bool g_application_running{false};
std::atomic<DWORD> g_application_thread_id{0};
std::atomic<DWORD> g_appkit_main_thread_id{0};
std::atomic_bool g_application_termination_pending{false};
std::atomic_bool g_application_termination_notified{false};
std::atomic_bool g_application_active{false};
std::atomic_bool g_application_active_transition{false};
std::atomic_bool g_application_hidden_transition{false};
bool g_application_launched = false;
bool g_application_hidden = false;
thread_local bool g_loading_nib = false;
thread_local id g_nib_owner = nullptr;
struct AppKitNibLoadContext final {
	bool has_visible_windows{};
	bool retain_graph_for_lifetime{};
	std::vector<id> visible_windows;
	std::vector<id> top_level_objects;
	std::vector<id> decoded_objects;
};
thread_local AppKitNibLoadContext* g_current_nib_load_context = nullptr;
int g_application_exit_code = 0;
id g_shared_application = nullptr;
id g_application_delegate = nullptr;
id g_key_appkit_window = nullptr;
id g_main_appkit_window = nullptr;
std::unordered_set<id> g_application_hidden_windows;
std::mutex g_process_info_mutex;
id g_process_info_singleton = nullptr;
std::vector<std::string> g_process_arguments;
thread_local id g_current_graphics_context = nullptr;
thread_local darling_windows_CGContextRef g_current_cg_context = nullptr;
thread_local COLORREF g_current_text_color = RGB(0, 0, 0);
thread_local double g_current_text_alpha = 1.0;
const char g_next_responder_key = 0;
struct AppKitWindowState final {
	HWND handle{};
	HMENU native_menu{};
	id title{};
	bool visible{};
	DarlingObjcRect content_rect{};
	std::uint64_t style_mask{};
	DarlingObjcPoint min_size{};
	DarlingObjcPoint max_size{10000.0, 10000.0};
	bool accepts_mouse_moved_events{};
	id content_view{};
	id first_responder{};
	id delegate{};
};
struct AppKitViewState final {
	DarlingObjcRect frame{};
	DarlingObjcRect bounds{};
	bool has_custom_bounds{};
	std::uint64_t autoresizing_mask{};
	id window{};
	id superview{};
	id next_key_view{};
	id title{};
	id key_equivalent{};
	std::uint64_t key_equivalent_modifier_mask{1ull << 20};
	id placeholder{};
	id image{};
	id target{};
	SEL action{};
	id clip_view{};
	std::uint64_t button_type{};
	std::int64_t button_state{};
	bool button_allows_mixed_state{};
	DarlingObjcRange selected_range{};
	double progress_min_value{};
	double progress_max_value{100.0};
	double progress_value{};
	std::uint64_t progress_style{};
	bool progress_indeterminate{};
	bool progress_animating{};
	bool native_progress_marquee{};
	double slider_min_value{};
	double slider_max_value{1.0};
	double slider_value{};
	bool slider_vertical{};
	bool slider_continuous{true};
	bool native_slider_vertical{};
	bool slider_pending_change{};
	double stepper_min_value{};
	double stepper_max_value{59.0};
	double stepper_value{};
	double stepper_increment{1.0};
	bool stepper_value_wraps{true};
	bool stepper_autorepeat{true};
	bool stepper_mouse_pressed{};
	int stepper_mouse_direction{};
	std::vector<std::wstring> popup_titles;
	std::vector<id> popup_items;
	std::int64_t popup_selected_index{-1};
	bool popup_pulls_down{};
	bool has_vertical_scroller{};
	bool has_horizontal_scroller{};
	bool autohides_scrollers{};
	HWND vertical_scroller{};
	HWND horizontal_scroller{};
	HWND control{};
	bool enabled{true};
	bool editable{true};
	bool hidden{};
	std::vector<id> subviews;
};
struct AppKitMouseTrackingState final {
	id view{};
	std::int64_t button{};
	std::int64_t click_count{};
};
struct ViewCoordinateOrigin final {
	id window{};
	double scale_x{1.0};
	double scale_y{1.0};
	double translate_x{};
	double translate_y{};
	bool valid{};
};
struct ViewCoordinateStep final {
	id view{};
	id superview{};
	DarlingObjcRect frame{};
	DarlingObjcRect bounds{};
};
std::mutex g_windows_mutex;
std::unordered_map<id, AppKitWindowState> g_windows;
std::unordered_map<id, AppKitViewState> g_views;
std::unordered_map<id, id> g_popup_item_owners;
std::unordered_map<HWND, id> g_controls;
std::unordered_map<HWND, id> g_scrollbar_controls;
std::unordered_map<HWND, AppKitMouseTrackingState> g_mouse_tracking;
struct AppKitImageState final {
	darling_windows_CGImageRef image{};
	DarlingObjcPoint size{};
	std::vector<id> representations;
};
std::mutex g_images_mutex;
std::unordered_map<id, AppKitImageState> g_images;
struct AppKitNibConnectorState final {
	id source{};
	id destination{};
	id label{};
	bool control{};
};
std::mutex g_nib_connectors_mutex;
std::unordered_map<id, AppKitNibConnectorState> g_nib_connectors;
struct AppKitFontState final {
	HFONT handle{};
	std::wstring name;
	double point_size{};
};
struct AppKitAttributedStringState final {
	id string{};
	id attributes{};
};
struct AppKitNotificationState final {
	id name{};
	id object{};
	id user_info{};
};
struct AppKitEventState final {
	std::uint64_t type{};
	DarlingObjcPoint location{};
	std::uint64_t modifier_flags{};
	double timestamp{};
	id window{};
	std::int64_t button_number{-1};
	std::int64_t click_count{};
	std::uint16_t key_code{};
	bool repeat{};
	id characters{};
	id characters_ignoring_modifiers{};
	double delta_x{};
	double delta_y{};
	double scrolling_delta_x{};
	double scrolling_delta_y{};
	bool precise_scrolling_deltas{};
	UINT keyboard_virtual_key{};
	bool keyboard_handled{};
};
struct AppKitQueuedEvent final {
	id event{};
	std::uint64_t type{};
	double timestamp{};
};
struct AppKitTimerState final {
	std::mutex mutex;
	std::condition_variable condition;
	id timer{};
	id target{};
	id user_info{};
	SEL selector{};
	double interval{};
	double fire_date{};
	double last_fire_date{};
	std::uint64_t fire_date_generation{};
	darling_windows_CFRunLoopRef run_loop{};
	DWORD owner_thread{};
	bool repeats{};
	bool valid{true};
	bool scheduled{};
	bool callback_pending{};
	bool appkit_loop{};
};
thread_local id g_current_appkit_event = nullptr;
struct AppKitCurrentEventScope final {
	id previous{};
	explicit AppKitCurrentEventScope(id event) : previous(g_current_appkit_event)
	{
		g_current_appkit_event = event == nullptr ? nullptr : objc_retain(event);
	}
	~AppKitCurrentEventScope()
	{
	id current = g_current_appkit_event;
		g_current_appkit_event = previous;
		if (current != nullptr) objc_release(current);
	}
};
struct AppKitNotificationObserver final {
	id observer{};
	SEL selector{};
	id object_filter{};
	bool has_name{};
	bool has_object_filter{};
	std::string name;
	~AppKitNotificationObserver()
	{
		objc_destroyWeak(&observer);
		objc_destroyWeak(&object_filter);
	}
};
struct AppKitMenuState final {
	id title{};
	std::vector<id> items;
	bool autoenables_items{true};
};
struct AppKitMenuItemState final {
	id title{};
	id target{};
	id key_equivalent{};
	id submenu{};
	SEL action{};
	std::int64_t state{};
	std::int64_t tag{};
	std::uint64_t modifier_mask{1ull << 20};
	bool enabled{true};
	bool separator{};
};
struct AppKitAlertState final {
	id message_text{};
	id informative_text{};
	std::vector<id> buttons;
	std::uint64_t style{};
};
struct AppKitURLState final {
	id path{};
};
struct AppKitUTTypeState final {
	id identifier{};
	std::wstring preferred_extension;
};
struct AppKitNSErrorState final {
	id domain{};
	id localized_description{};
	id user_info{};
	std::int64_t code{};
};
struct AppKitFileHandleState final {
	HANDLE handle{INVALID_HANDLE_VALUE};
	int darwin_descriptor{-1};
	bool close_on_dealloc{true};
	bool close_native_handle{true};
	std::mutex mutex;
	~AppKitFileHandleState()
	{
		if (close_native_handle && handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
		if (close_on_dealloc && darwin_descriptor >= 0)
			darling_windows_close(darwin_descriptor);
	}
};
struct AppKitFilePanelState final {
	std::wstring title;
	std::wstring name;
	std::vector<std::wstring> allowed_file_types;
	std::vector<std::wstring> paths;
	bool allows_multiple{};
	bool can_choose_files{true};
	bool can_choose_directories{};
};
struct AppKitAutoreleasePoolState final {
	void* token{};
};
struct AppKitUserDefaultsState final {
	darling_windows_preferences_ref store{};
	darling_windows_preferences_ref argument_store{};
	darling_windows_preferences_ref managed_store{};
	std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
		volatile_domains;
	std::vector<std::string> volatile_domain_order;
};
std::mutex g_fonts_mutex;
std::unordered_map<id, AppKitFontState> g_fonts;
thread_local id g_current_font = nullptr;
std::mutex g_attributed_strings_mutex;
std::unordered_map<id, AppKitAttributedStringState> g_attributed_strings;
std::mutex g_notifications_mutex;
std::unordered_map<id, AppKitNotificationState> g_notifications;
std::mutex g_events_mutex;
std::unordered_map<id, AppKitEventState> g_events;
std::mutex g_appkit_event_queue_mutex;
std::deque<AppKitQueuedEvent> g_appkit_event_queue;
std::atomic<bool> g_appkit_event_polling{false};
std::mutex g_appkit_timer_mutex;
std::unordered_map<id, std::shared_ptr<AppKitTimerState>> g_appkit_timers;
std::mutex g_appkit_callback_mutex;
std::deque<std::function<void()>> g_appkit_callbacks;
std::mutex g_user_defaults_mutex;
std::unordered_map<id, AppKitUserDefaultsState> g_user_defaults;
id g_standard_user_defaults = nullptr;
std::mutex g_menus_mutex;
std::unordered_map<id, AppKitMenuState> g_menus;
std::unordered_map<id, AppKitMenuItemState> g_menu_items;
std::mutex g_alerts_mutex;
std::unordered_map<id, AppKitAlertState> g_alerts;
std::mutex g_urls_mutex;
std::unordered_map<id, AppKitURLState> g_urls;
std::mutex g_ut_types_mutex;
std::unordered_map<id, AppKitUTTypeState> g_ut_types;
std::mutex g_nserrors_mutex;
std::unordered_map<id, AppKitNSErrorState> g_nserrors;
std::mutex g_file_handles_mutex;
std::unordered_map<id, std::shared_ptr<AppKitFileHandleState>> g_file_handles;
std::mutex g_file_panels_mutex;
std::unordered_map<id, AppKitFilePanelState> g_file_panels;
std::unordered_map<UINT, id> g_menu_commands;
std::unordered_map<HMENU, id> g_native_menu_objects;
UINT g_next_menu_command = 0x4000;
id g_application_main_menu = nullptr;
std::mutex g_loaded_nib_objects_mutex;
std::vector<id> g_loaded_nib_objects;
std::mutex g_autorelease_pools_mutex;
std::unordered_map<id, AppKitAutoreleasePoolState> g_autorelease_pools;
std::mutex g_notification_center_mutex;
id g_default_notification_center = nullptr;
std::vector<std::shared_ptr<AppKitNotificationObserver>> g_notification_observers;
void NSNotificationCenterDispatch(id notification);
id NSNotificationFactory(Class cls, SEL selector, id name, id object);
id NSNotificationFactoryWithUserInfo(Class cls, SEL selector, id name,
	id object, id user_info);
void InvalidateAppKitWindow(id window_object)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(window_object);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	if (handle != nullptr) InvalidateRect(handle, nullptr, FALSE);
}

void InvalidateAppKitView(id view)
{
	id window_object = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(view);
		if (found != g_views.end()) window_object = found->second.window;
	}
	if (window_object != nullptr) InvalidateAppKitWindow(window_object);
}

std::mutex g_constant_string_mutex;
std::unordered_map<id, std::string> g_constant_string_utf8;
std::mutex g_mutable_string_mutex;
std::unordered_map<id, std::string> g_mutable_strings;
std::mutex g_array_mutex;
std::unordered_map<id, std::vector<id>> g_arrays;
std::mutex g_dictionary_mutex;
std::unordered_map<id, std::vector<std::pair<id, id>>> g_dictionaries;
std::mutex g_data_mutex;
std::unordered_map<id, std::vector<std::uint8_t>> g_data;
std::mutex g_bundle_mutex;
std::unordered_map<id, darling_windows_CFBundleRef> g_bundles;
id g_main_bundle_object = nullptr;
struct AppKitNibState final {
	id name{};
	id bundle{};
	id data{};
};
std::mutex g_nib_instances_mutex;
std::unordered_map<id, AppKitNibState> g_nib_instances;
std::mutex g_nib_array_graphs_mutex;
std::unordered_map<id, std::vector<id>> g_nib_array_graphs;
struct ObjcNumberValue final {
	double real{};
	std::int64_t integer{};
	std::uint64_t unsigned_integer{};
	bool integral{};
	bool unsigned_value{};
	bool boolean{};
};
std::mutex g_number_mutex;
std::unordered_map<id, ObjcNumberValue> g_numbers;
std::mutex g_color_mutex;
std::unordered_map<id, darling_windows_CGColor> g_nscolors;
std::unordered_set<id> g_grayscale_nscolors;
enum class BezierCommandKind { Move, Line, Curve, Close, Rect, Oval };
struct BezierCommand final {
	BezierCommandKind kind{};
	DarlingObjcPoint points[3]{};
	DarlingObjcRect rect{};
};
struct BezierPathState final {
	std::vector<BezierCommand> commands;
	double line_width{1.0};
	std::uint64_t line_cap_style{darling_windows_kCGLineCapButt};
	std::uint64_t line_join_style{darling_windows_kCGLineJoinMiter};
};
std::mutex g_bezier_mutex;
std::unordered_map<id, BezierPathState> g_bezier_paths;
std::mutex g_date_mutex;
std::unordered_map<id, double> g_dates;
std::mutex g_nsrunloop_mutex;
std::unordered_map<darling_windows_CFRunLoopRef, id> g_nsrunloop_objects;
std::unordered_map<id, darling_windows_CFRunLoopRef> g_nsrunloop_refs;
std::once_flag g_null_once;
id g_null_object = nullptr;

id NSViewInitWithFrame(id self, SEL selector, DarlingObjcRect frame);
id NSViewInitWithCoder(id self, SEL selector, id coder);
DarlingObjcRect NSKeyedArchiveDecodeRectForKey(id key);
void NSViewSetFrame(id self, SEL selector, DarlingObjcRect frame);
void AttachViewTree(id view, id window);
void PositionNativeControl(id view);
void RepositionViewTree(id view);
using HiddenStateSnapshot = std::vector<std::pair<id, bool>>;
void CaptureHiddenSubtreeLocked(id view, HiddenStateSnapshot& snapshot);
void NotifyHiddenStateChanges(const HiddenStateSnapshot& before,
	const HiddenStateSnapshot& after);
void DispatchControlAction(HWND control);
bool DispatchNativeSliderMessage(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam);
bool DispatchNativeStepperMessage(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam);
void StepNSStepper(HWND control, int delta);
bool DispatchAppKitMenuCommand(UINT command);
bool DispatchAppKitMenuKey(WPARAM key);
void ValidateAppKitMenu(HMENU menu);
void RefreshAppKitMenus();
void ApplyAppKitMenuToWindow(id window);
bool NSWindowShouldClose(id window);
void NSWindowNotifyDelegate(id window, const char* selector);
bool NSApplicationShouldTerminateAfterLastWindowClosed();
void NSApplicationTerminate(id self, SEL selector, id sender);
void NSApplicationNotifyWillTerminate(id application);
bool EnqueueAppKitCallback(std::function<void()> callback);
bool DispatchOneAppKitCallback();
id NSTimerWithTimeInterval(Class cls, SEL selector, double interval, id target,
	SEL timer_selector, id user_info, bool repeats);
id NSTimerScheduledWithTimeInterval(Class cls, SEL selector, double interval,
	id target, SEL timer_selector, id user_info, bool repeats);
void NSTimerInvalidate(id self, SEL selector);
void NSTimerFire(id self, SEL selector);
bool NSTimerIsValid(id self, SEL selector);
double NSTimerTimeInterval(id self, SEL selector);
id NSTimerFireDate(id self, SEL selector);
void NSTimerSetFireDate(id self, SEL selector, id date);
id NSTimerUserInfo(id self, SEL selector);
void NSTimerFinalize(id object);
void NSApplicationSetActiveState(bool active);
void NSApplicationDispatchLifecycleEvent(id notification_name,
	const char* delegate_selector_name);
bool DispatchAppKitMouseEvent(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam);
bool DispatchAppKitKeyEvent(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam);
bool DispatchAppKitScrollEvent(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam);
void ClearAppKitMouseTracking(HWND window);
id TakeMatchingAppKitEvent(std::uint64_t event_mask, bool dequeue);
void DispatchAppKitEventToApplication(id application, id event);
void ClearAppKitMouseTracking(HWND window);
void NSApplicationSendEvent(id self, SEL selector, id event);
void PopupNativeAppMenu(id menu, HWND owner, POINT point);
id FindAppKitViewAtPoint(id view, double x, double y, unsigned depth = 0);
bool ScrollAppKitViewAtPoint(id root, double x, double y, int wheel_delta,
	bool horizontal = false);
void PositionScrollViewScrollbars(id view);
void RefreshAncestorScrollView(id view);
bool DispatchScrollBarMessage(HWND window, UINT message, WPARAM wparam,
	LPARAM lparam);
void UpdateTextFieldValue(HWND control);
DarlingObjcRect NSViewBounds(id self, SEL selector);
DarlingObjcRect NSViewFrame(id self, SEL selector);
DarlingObjcRect NSViewVisibleRect(id self, SEL selector);
bool NSViewScrollRectToVisible(id self, SEL selector, DarlingObjcRect rect);
void NSViewSetBounds(id self, SEL selector, DarlingObjcRect bounds);
void NSViewSetAutoresizingMask(id self, SEL selector, std::uint64_t mask);
void NSViewResizeSubviewsWithOldSize(id self, SEL selector,
	DarlingObjcPoint old_size);
void NSViewResizeWithOldSuperviewSize(id self, SEL selector,
	DarlingObjcPoint old_size);
void NSViewFinalize(id object);
id NSWindowContentView(id self, SEL selector);
void NSWindowSetTitle(id self, SEL selector, id title);
void NSWindowSetFrame(id self, SEL selector, DarlingObjcRect frame, bool display);
void NSWindowSetMinSize(id self, SEL selector, DarlingObjcPoint size);
void NSWindowSetMaxSize(id self, SEL selector, DarlingObjcPoint size);
id NSNibConnectorInitWithCoder(id self, SEL selector, id coder);
void NSNibConnectorEstablishConnection(id self, SEL selector);
void NSNibConnectorFinalize(id object);
void NSMenuItemSetTarget(id self, SEL selector, id target);
void NSMenuItemSetAction(id self, SEL selector, SEL action);
void NSMenuItemSetTitle(id self, SEL selector, id title);
void NSMenuItemSetKeyEquivalent(id self, SEL selector, id value);
void NSMenuItemSetSubmenu(id self, SEL selector, id submenu);
void NSButtonSetTarget(id self, SEL selector, id target);
void NSButtonSetAction(id self, SEL selector, SEL action);
bool NSButtonIsEnabled(id self, SEL selector);
std::uint64_t NSEventType(id self, SEL selector);
double NSEventTimestamp(id self, SEL selector);
std::uint64_t NSEventModifierFlags(id self, SEL selector);
id NSEventCharactersIgnoringModifiers(id self, SEL selector);
id NSApplicationSharedApplication(Class requested_class, SEL selector);
void NSApplicationSetMainMenu(id self, SEL selector, id menu);
bool NSBundleLoadNibNamedClass(Class cls, SEL selector, id nib_name,
	id owner);
bool NSBundleLoadNibNamedInstance(id bundle, SEL selector, id nib_name,
	id owner, id* top_level_objects);
id NSNibInitWithNameBundle(id self, SEL selector, id nib_name, id bundle);
id NSNibInitWithDataBundle(id self, SEL selector, id nib_data, id bundle);
bool NSNibInstantiateWithOwner(id self, SEL selector, id owner,
	id* top_level_objects);
void NSNibFinalize(id object);
void NSMenuAddItem(id self, SEL selector, id item);
ViewCoordinateOrigin NSViewOriginInWindow(id view);
void DrawViewTree(id view, darling_windows_CGContextRef context,
	DarlingObjcRect dirty_window_rect, std::size_t depth = 0);
void NSViewSetNeedsDisplay(id self, SEL selector, bool needs_display);
void NSViewSetNeedsDisplayInRect(id self, SEL selector,
	DarlingObjcRect dirty_rect);
void NSViewAddSubview(id self, SEL selector, id child);
void NSViewRemoveFromSuperview(id self, SEL selector);
void NSViewSetHidden(id self, SEL selector, bool hidden);
bool NSViewIsHiddenOrHasHiddenAncestor(id self, SEL selector);
void NSViewSetNextKeyView(id self, SEL selector, id next_view);
void NSPopUpButtonSyncItemTitle(id item, id title);
LRESULT CALLBACK AppKitControlWindowProc(HWND control, UINT message,
	WPARAM wparam, LPARAM lparam);
std::wstring Utf8ToWide(const char* utf8);
std::string WideToUtf8(std::wstring_view wide);
const char* NSStringUTF8String(id self, SEL selector);
std::uint64_t NSStringLength(id self, SEL selector);
bool ObjcObjectsEqual(id left, id right);
id CreateDynamicNSString(Class cls, const char* utf8);
id CreateMutableNSString(Class cls, const char* utf8);
id ObjcObjectFromCF(darling_windows_CFTypeRef value, unsigned depth = 0);
id CreateNSArray(Class cls, const id* values, std::uint64_t count);
id CreateNSDictionary(Class cls, const id* values, const id* keys,
	std::uint64_t count);
id NSMenuItemTitle(id self, SEL selector);
id NSButtonTitle(id self, SEL selector);
id NSButtonCreateWithTitleTargetAction(Class cls, SEL selector, id title,
	id target, SEL action);
bool IsViewKindOf(id object, const char* class_name);
id NSStringStringWithUTF8String(Class cls, SEL selector, const char* utf8);
id NSMutableStringStringWithString(Class cls, SEL selector, id value);
void NSMutableStringReplaceCharactersInRange(id self, SEL selector,
	DarlingObjcRange range, id replacement);
id NSDictionaryObjectForKey(id self, SEL selector, id key);
void NSFontSet(id self, SEL selector);
void NSColorApply(id self, bool fill, bool stroke);
id NSGraphicsContextCurrentContext(Class cls, SEL selector);
id NSBundleMainBundle(Class cls, SEL selector);
id NSBundleWithURL(Class cls, SEL selector, id url);
id NSBundleInitWithURL(id self, SEL selector, id url);
Class NSBundlePrincipalClass(id self, SEL selector);
Class NSBundleClassNamed(id self, SEL selector, id name);
bool NSBundleLoad(id self, SEL selector);
bool NSBundlePreflightAndReturnError(id self, SEL selector, id* error);
bool NSBundleLoadAndReturnError(id self, SEL selector, id* error);
void NSBundlePostDidLoad(id self, const std::unordered_set<std::string>& prior_classes);
id NSErrorCreate(Class cls, id domain, std::int64_t code, id user_info,
	id localized_description);
id NSURLCreateFileURL(Class cls, id path);
id NSBundleBundleURL(id self, SEL selector);
id NSBundleExecutableURL(id self, SEL selector);
id NSBundleResourceURL(id self, SEL selector);
id NSBundleExecutableArchitectures(id self, SEL selector);
id NSBundleURLForResource(id self, SEL selector, id resource_name,
	id resource_type, id subdirectory);
id NSBundlePathForResourceType(id self, SEL selector, id resource_name,
	id resource_type);
void NSImageFinalize(id object);
bool ParseKeyedSizeString(const char* text, double& width, double& height);
id NSImageInitWithContentsOfFile(id self, SEL selector, id path);
id NSImageInitWithData(id self, SEL selector, id data);
id NSImageInitWithCoder(id self, SEL selector, id coder);
id NSBitmapImageRepInitWithData(id self, SEL selector, id data);
id NSBitmapImageRepInitWithCoder(id self, SEL selector, id coder);
std::uint64_t NSBitmapImageRepPixelsWide(id self, SEL selector);
std::uint64_t NSBitmapImageRepPixelsHigh(id self, SEL selector);
DarlingObjcPoint NSBitmapImageRepSize(id self, SEL selector);
void NSBitmapImageRepSetSize(id self, SEL selector, DarlingObjcPoint size);
darling_windows_CGImageRef NSBitmapImageRepCGImage(id self, SEL selector);
void NSBitmapImageRepDraw(id self, SEL selector);
id NSImageRepresentations(id self, SEL selector);
id NSImageWithContentsOfFile(Class cls, SEL selector, id path);
id NSImageNamed(Class cls, SEL selector, id name);
DarlingObjcPoint NSImageSize(id self, SEL selector);
bool NSImageIsValid(id self, SEL selector);
void NSImageDrawAtPoint(id self, SEL selector, DarlingObjcPoint point);
void NSImageDrawInRect(id self, SEL selector, DarlingObjcRect rect);
id NSImageViewImage(id self, SEL selector);
void NSImageViewSetImage(id self, SEL selector, id image);
id NSImageViewInitWithCoder(id self, SEL selector, id coder);
void NSImageViewDrawRect(id self, SEL selector, DarlingObjcRect rect);

void PaintAppKitWindow(HWND window)
{
	PAINTSTRUCT paint{};
	HDC device = BeginPaint(window, &paint);
	if (device == nullptr) return;
	RECT client{};
	if (!GetClientRect(window, &client)) {
		EndPaint(window, &paint);
		return;
	}
	const int width = client.right - client.left;
	const int height = client.bottom - client.top;
	if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
		static_cast<std::uint64_t>(width) *
		static_cast<std::uint64_t>(height) > 64ull * 1024 * 1024) {
		EndPaint(window, &paint);
		return;
	}
	id window_object = reinterpret_cast<id>(GetWindowLongPtrW(window,
		GWLP_USERDATA));
	id content_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(window_object);
		if (found != g_windows.end()) content_view = found->second.content_view;
	}
	darling_windows_CGColorSpaceRef color_space =
		CGColorSpaceCreateDeviceRGB();
	darling_windows_CGContextRef context = color_space == nullptr ? nullptr :
		CGBitmapContextCreate(nullptr, static_cast<std::size_t>(width),
			static_cast<std::size_t>(height), 8,
			static_cast<std::size_t>(width) * 4, color_space,
			darling_windows_kCGImageAlphaPremultipliedFirst |
				darling_windows_kCGBitmapByteOrder32Little);
	if (context != nullptr) {
		const DarlingObjcRect dirty_window_rect{
			static_cast<double>(paint.rcPaint.left),
			static_cast<double>(height - paint.rcPaint.bottom),
			static_cast<double>(paint.rcPaint.right - paint.rcPaint.left),
			static_cast<double>(paint.rcPaint.bottom - paint.rcPaint.top)};
		CGContextSetRGBFillColor(context, 1.0, 1.0, 1.0, 1.0);
		CGContextFillRect(context, {{dirty_window_rect.x,
			dirty_window_rect.y}, {dirty_window_rect.width,
			dirty_window_rect.height}});
		CGContextClipToRect(context, {{dirty_window_rect.x,
			dirty_window_rect.y}, {dirty_window_rect.width,
			dirty_window_rect.height}});
		g_current_cg_context = context;
		g_current_text_color = RGB(0, 0, 0);
		g_current_text_alpha = 1.0;
		g_current_graphics_context = NSGraphicsContextCurrentContext(
			objc_getClass("NSGraphicsContext"), nullptr);
		if (content_view != nullptr)
			DrawViewTree(content_view, context, dirty_window_rect);
		g_current_cg_context = nullptr;
		if (g_current_font != nullptr) {
			id current_font = g_current_font;
			g_current_font = nullptr;
			objc_release(current_font);
		}
		BITMAPINFO bitmap{};
		bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
		bitmap.bmiHeader.biWidth = width;
		bitmap.bmiHeader.biHeight = -height;
		bitmap.bmiHeader.biPlanes = 1;
		bitmap.bmiHeader.biBitCount = 32;
		bitmap.bmiHeader.biCompression = BI_RGB;
		StretchDIBits(device, paint.rcPaint.left, paint.rcPaint.top,
			paint.rcPaint.right - paint.rcPaint.left,
			paint.rcPaint.bottom - paint.rcPaint.top,
			paint.rcPaint.left, paint.rcPaint.top,
			paint.rcPaint.right - paint.rcPaint.left,
			paint.rcPaint.bottom - paint.rcPaint.top,
			CGBitmapContextGetData(context), &bitmap, DIB_RGB_COLORS, SRCCOPY);
		CGContextRelease(context);
	}
	if (color_space != nullptr) CGColorSpaceRelease(color_space);
	EndPaint(window, &paint);
}

id NSGraphicsContextCurrentContext(Class cls, SEL)
{
	if (g_current_cg_context == nullptr) return nullptr;
	if (g_current_graphics_context == nullptr && cls != nullptr)
		g_current_graphics_context = class_createInstance(cls, 0);
	return g_current_graphics_context;
}

void* NSGraphicsContextCGContext(id self, SEL)
{
	return self == g_current_graphics_context ? g_current_cg_context : nullptr;
}

void NSGraphicsContextSaveGraphicsState(Class, SEL)
{
	CGContextSaveGState(g_current_cg_context);
}

void NSGraphicsContextRestoreGraphicsState(Class, SEL)
{
	CGContextRestoreGState(g_current_cg_context);
}

void NSStringDrawAtPoint(id self, SEL, darling_windows_CGPoint point)
{
	if (self == nullptr || g_current_cg_context == nullptr ||
		!std::isfinite(point.x) || !std::isfinite(point.y)) return;
	const char* utf8 = NSStringUTF8String(self, nullptr);
	if (utf8 == nullptr || *utf8 == '\0') return;
	const auto text = Utf8ToWide(utf8);
	if (text.empty() || text.size() > static_cast<std::size_t>(INT_MAX)) return;
	const std::size_t width = CGBitmapContextGetWidth(g_current_cg_context);
	const std::size_t height = CGBitmapContextGetHeight(g_current_cg_context);
	const std::size_t stride = CGBitmapContextGetBytesPerRow(g_current_cg_context);
	auto* pixels = static_cast<std::uint8_t*>(
		CGBitmapContextGetData(g_current_cg_context));
	if (pixels == nullptr || width == 0 || height == 0 || width > INT_MAX ||
		height > INT_MAX || stride != width * 4 ||
		height > (std::numeric_limits<std::size_t>::max)() / stride) return;
	const auto transformed = darling_windows_CGPointApplyAffineTransform(point,
		CGContextGetCTM(g_current_cg_context));
	if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y)) return;
	const auto x = std::llround(transformed.x);
	if (x < INT_MIN || x > INT_MAX) return;
	BITMAPINFO bitmap{};
	bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
	bitmap.bmiHeader.biWidth = static_cast<LONG>(width);
	bitmap.bmiHeader.biHeight = -static_cast<LONG>(height);
	bitmap.bmiHeader.biPlanes = 1;
	bitmap.bmiHeader.biBitCount = 32;
	bitmap.bmiHeader.biCompression = BI_RGB;
	HDC memory = CreateCompatibleDC(nullptr);
	if (memory == nullptr) return;
	void* dib_pixels = nullptr;
	HBITMAP dib = CreateDIBSection(memory, &bitmap, DIB_RGB_COLORS,
		&dib_pixels, nullptr, 0);
	if (dib == nullptr || dib_pixels == nullptr) {
		if (dib != nullptr) DeleteObject(dib);
		DeleteDC(memory);
		return;
	}
	const std::size_t byte_count = stride * height;
	std::vector<std::uint8_t> alpha;
	try {
		alpha.resize(width * height);
	} catch (...) {
		DeleteObject(dib);
		DeleteDC(memory);
		return;
	}
	std::memcpy(dib_pixels, pixels, byte_count);
	for (std::size_t row = 0; row < height; ++row)
		for (std::size_t column = 0; column < width; ++column)
			alpha[row * width + column] = pixels[row * stride + column * 4 + 3];
	const HGDIOBJ previous_bitmap = SelectObject(memory, dib);
	std::unique_lock font_lock(g_fonts_mutex);
	HGDIOBJ font = GetStockObject(DEFAULT_GUI_FONT);
	const auto selected_font = g_fonts.find(g_current_font);
	if (selected_font != g_fonts.end()) font = selected_font->second.handle;
	const HGDIOBJ previous_font = font == nullptr ? nullptr : SelectObject(memory, font);
	SetBkMode(memory, TRANSPARENT);
	SetTextColor(memory, g_current_text_color);
	TEXTMETRICW metrics{};
	bool drawn = false;
	if (GetTextMetricsW(memory, &metrics)) {
		const auto top = static_cast<long long>(height) -
			std::llround(transformed.y) - metrics.tmAscent;
		if (top >= INT_MIN && top <= INT_MAX)
			drawn = TextOutW(memory, static_cast<int>(x), static_cast<int>(top),
				text.data(), static_cast<int>(text.size())) != FALSE;
	}
	if (drawn) {
		const auto* rendered = static_cast<const std::uint8_t*>(dib_pixels);
		const auto blend_channel = [](std::uint8_t background,
			std::uint8_t foreground, double opacity) {
			return static_cast<std::uint8_t>(std::lround(background +
				(static_cast<double>(foreground) - background) * opacity));
		};
		for (std::size_t row = 0; row < height; ++row) {
			for (std::size_t column = 0; column < width; ++column) {
				const std::size_t offset = row * stride + column * 4;
				pixels[offset] = blend_channel(pixels[offset], rendered[offset],
					g_current_text_alpha);
				pixels[offset + 1] = blend_channel(pixels[offset + 1],
					rendered[offset + 1], g_current_text_alpha);
				pixels[offset + 2] = blend_channel(pixels[offset + 2],
					rendered[offset + 2], g_current_text_alpha);
				pixels[offset + 3] = alpha[row * width + column];
			}
		}
	}
	if (previous_font != nullptr && previous_font != HGDI_ERROR)
		SelectObject(memory, previous_font);
	if (previous_bitmap != nullptr && previous_bitmap != HGDI_ERROR)
		SelectObject(memory, previous_bitmap);
	DeleteObject(dib);
	DeleteDC(memory);
}

void NSStringDrawAtPointWithAttributes(id self, SEL,
	darling_windows_CGPoint point, id attributes)
{
	if (attributes != nullptr) {
		if (NSFontAttributeName != nullptr) {
			const id font = NSDictionaryObjectForKey(attributes, nullptr,
				NSFontAttributeName);
			if (font != nullptr) NSFontSet(font, nullptr);
		}
		if (NSForegroundColorAttributeName != nullptr) {
			const id color = NSDictionaryObjectForKey(attributes, nullptr,
				NSForegroundColorAttributeName);
			if (color != nullptr) NSColorApply(color, true, false);
		}
	}
	NSStringDrawAtPoint(self, nullptr, point);
}

void NSFontFinalize(id object)
{
	std::lock_guard lock(g_fonts_mutex);
	const auto found = g_fonts.find(object);
	if (found == g_fonts.end()) return;
	if (found->second.handle != nullptr) DeleteObject(found->second.handle);
	g_fonts.erase(found);
}

id NSFontCreate(Class cls, const wchar_t* name, double point_size, bool bold)
{
	if (cls == nullptr || name == nullptr || !std::isfinite(point_size) ||
		point_size <= 0.0 || point_size > 512.0) return nullptr;
	HDC device = GetDC(nullptr);
	const int dpi = device == nullptr ? 96 : GetDeviceCaps(device, LOGPIXELSY);
	if (device != nullptr) ReleaseDC(nullptr, device);
	const auto pixel_height = std::llround(point_size *
		static_cast<double>(dpi > 0 ? dpi : 96) / 72.0);
	if (pixel_height <= 0 || pixel_height > INT_MAX) return nullptr;
	const int height = -static_cast<int>(pixel_height);
	HFONT font = CreateFontW(height, 0, 0, 0,
		bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, name);
	if (font == nullptr) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) {
		DeleteObject(font);
		return nullptr;
	}
	try {
		std::lock_guard lock(g_fonts_mutex);
		g_fonts.emplace(object, AppKitFontState{font, name, point_size});
	} catch (...) {
		DeleteObject(font);
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSFontFinalize)) {
		NSFontFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSFontSystemFontOfSize(Class cls, SEL, double point_size)
{
	return objc_autorelease(NSFontCreate(cls, L"Segoe UI", point_size, false));
}

id NSFontBoldSystemFontOfSize(Class cls, SEL, double point_size)
{
	return objc_autorelease(NSFontCreate(cls, L"Segoe UI", point_size, true));
}

id NSFontWithNameSize(Class cls, SEL, id name_object, double point_size)
{
	const char* utf8 = NSStringUTF8String(name_object, nullptr);
	if (utf8 == nullptr) return nullptr;
	const auto name = Utf8ToWide(utf8);
	if (name.empty() || name.size() >= LF_FACESIZE) return nullptr;
	return objc_autorelease(NSFontCreate(cls, name.c_str(), point_size, false));
}

void NSFontSet(id self, SEL)
{
	{
		std::lock_guard lock(g_fonts_mutex);
		if (!g_fonts.contains(self)) return;
	}
	if (g_current_font == self) return;
	objc_retain(self);
	id previous = g_current_font;
	g_current_font = self;
	if (previous != nullptr) objc_release(previous);
}

void NSAttributedStringFinalize(id object)
{
	AppKitAttributedStringState state{};
	{
		std::lock_guard lock(g_attributed_strings_mutex);
		const auto found = g_attributed_strings.find(object);
		if (found == g_attributed_strings.end()) return;
		state = found->second;
		g_attributed_strings.erase(found);
	}
	if (state.string != nullptr) objc_release(state.string);
	if (state.attributes != nullptr) objc_release(state.attributes);
}

id NSAttributedStringInit(id self, SEL, id string, id attributes)
{
	if (self == nullptr || string == nullptr ||
		NSStringUTF8String(string, nullptr) == nullptr) return nullptr;
	AppKitAttributedStringState replacement{
		objc_retain(string), attributes == nullptr ? nullptr : objc_retain(attributes)};
	bool needs_finalizer = false;
	AppKitAttributedStringState previous{};
	try {
		std::lock_guard lock(g_attributed_strings_mutex);
		auto found = g_attributed_strings.find(self);
		if (found == g_attributed_strings.end()) {
			g_attributed_strings.emplace(self, replacement);
			needs_finalizer = true;
		} else {
			previous = std::exchange(found->second, replacement);
		}
	} catch (...) {
		objc_release(replacement.string);
		if (replacement.attributes != nullptr)
			objc_release(replacement.attributes);
		return nullptr;
	}
	if (previous.string != nullptr) objc_release(previous.string);
	if (previous.attributes != nullptr) objc_release(previous.attributes);
	if (needs_finalizer && !darling_objc_register_object_finalizer(self,
		&NSAttributedStringFinalize)) {
		NSAttributedStringFinalize(self);
		return nullptr;
	}
	return self;
}

id NSAttributedStringFactory(Class cls, SEL selector, id string, id attributes)
{
	if (cls == nullptr) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	object = NSAttributedStringInit(object, selector, string, attributes);
	return object == nullptr ? nullptr : objc_autorelease(object);
}

id NSMutableAttributedStringFactory(Class cls, SEL selector, id string,
	id attributes)
{
	return NSAttributedStringFactory(cls, selector, string, attributes);
}

id NSMutableAttributedStringInit(id self, SEL selector, id string,
	id attributes)
{
	const char* utf8 = NSStringUTF8String(string, nullptr);
	if (utf8 == nullptr) return nullptr;
	id mutable_string = CreateMutableNSString(
		objc_getClass("NSMutableString"), utf8);
	if (mutable_string == nullptr) return nullptr;
	id result = NSAttributedStringInit(self, selector, mutable_string,
		attributes);
	objc_release(mutable_string);
	return result;
}

id NSMutableAttributedStringMutableString(id self, SEL)
{
	std::lock_guard lock(g_attributed_strings_mutex);
	const auto found = g_attributed_strings.find(self);
	if (found == g_attributed_strings.end()) return nullptr;
	return objc_retainAutorelease(found->second.string);
}

void NSMutableAttributedStringReplaceCharactersInRange(id self, SEL,
	DarlingObjcRange range, id replacement)
{
	id string = nullptr;
	{
		std::lock_guard lock(g_attributed_strings_mutex);
		const auto found = g_attributed_strings.find(self);
		if (found == g_attributed_strings.end()) return;
		string = objc_retain(found->second.string);
	}
	NSMutableStringReplaceCharactersInRange(string, nullptr, range, replacement);
	objc_release(string);
}

void NSMutableAttributedStringSetAttributes(id self, SEL, id attributes,
	DarlingObjcRange range)
{
	id string = nullptr;
	{
		std::lock_guard lock(g_attributed_strings_mutex);
		const auto found = g_attributed_strings.find(self);
		if (found == g_attributed_strings.end()) return;
		string = objc_retain(found->second.string);
	}
	const auto length = NSStringLength(string, nullptr);
	if (range.location != 0 || range.length != length) {
		objc_release(string);
		return;
	}
	id replacement = attributes == nullptr ? nullptr : objc_retain(attributes);
	id previous = nullptr;
	bool applied = false;
	{
		std::lock_guard lock(g_attributed_strings_mutex);
		const auto found = g_attributed_strings.find(self);
		if (found != g_attributed_strings.end() &&
			found->second.string == string) {
			previous = std::exchange(found->second.attributes, replacement);
			applied = true;
		}
	}
	objc_release(string);
	if (!applied) {
		if (replacement != nullptr) objc_release(replacement);
		return;
	}
	if (previous != nullptr) objc_release(previous);
}

id NSAttributedStringValue(id self, SEL)
{
	std::lock_guard lock(g_attributed_strings_mutex);
	const auto found = g_attributed_strings.find(self);
	return found == g_attributed_strings.end() ? nullptr :
		objc_retainAutorelease(found->second.string);
}

std::uint64_t NSAttributedStringLength(id self, SEL)
{
	id string = nullptr;
	{
		std::lock_guard lock(g_attributed_strings_mutex);
		const auto found = g_attributed_strings.find(self);
		if (found == g_attributed_strings.end()) return 0;
		string = objc_retain(found->second.string);
	}
	const std::uint64_t length = NSStringLength(string, nullptr);
	objc_release(string);
	return length;
}

void NSAttributedStringDrawAtPoint(id self, SEL, darling_windows_CGPoint point)
{
	AppKitAttributedStringState state{};
	{
		std::lock_guard lock(g_attributed_strings_mutex);
		const auto found = g_attributed_strings.find(self);
		if (found == g_attributed_strings.end()) return;
		state = {objc_retain(found->second.string),
			found->second.attributes == nullptr ? nullptr :
				objc_retain(found->second.attributes)};
	}
	NSStringDrawAtPointWithAttributes(state.string, nullptr, point,
		state.attributes);
	objc_release(state.string);
	if (state.attributes != nullptr) objc_release(state.attributes);
}

void NSNumberFinalize(id object)
{
	std::lock_guard lock(g_number_mutex);
	g_numbers.erase(object);
}

void NSColorFinalize(id object)
{
	std::lock_guard lock(g_color_mutex);
	g_nscolors.erase(object);
	g_grayscale_nscolors.erase(object);
}

id CreateNSColor(Class cls, darling_windows_CGColor color,
	bool grayscale = false)
{
	if (cls == nullptr || !std::isfinite(color.red) ||
		!std::isfinite(color.green) || !std::isfinite(color.blue) ||
		!std::isfinite(color.alpha)) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	try {
		std::lock_guard lock(g_color_mutex);
		g_nscolors.emplace(object, color);
		if (grayscale) g_grayscale_nscolors.insert(object);
	} catch (...) {
		{
			std::lock_guard lock(g_color_mutex);
			g_nscolors.erase(object);
			g_grayscale_nscolors.erase(object);
		}
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSColorFinalize)) {
		NSColorFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSColorNamed(Class cls, SEL selector)
{
	const char* name = sel_getName(selector);
	if (name == nullptr) return nullptr;
	darling_windows_CGColor color{0.0, 0.0, 0.0, 1.0};
	bool grayscale = false;
	if (std::strcmp(name, "whiteColor") == 0) color = {1.0, 1.0, 1.0, 1.0};
	else if (std::strcmp(name, "redColor") == 0) color = {1.0, 0.0, 0.0, 1.0};
	else if (std::strcmp(name, "greenColor") == 0) color = {0.0, 1.0, 0.0, 1.0};
	else if (std::strcmp(name, "blueColor") == 0) color = {0.0, 0.0, 1.0, 1.0};
	else if (std::strcmp(name, "grayColor") == 0) color = {0.5, 0.5, 0.5, 1.0};
	else if (std::strcmp(name, "lightGrayColor") == 0) color = {0.75, 0.75, 0.75, 1.0};
	else if (std::strcmp(name, "darkGrayColor") == 0) color = {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0, 1.0};
	else if (std::strcmp(name, "yellowColor") == 0) color = {1.0, 1.0, 0.0, 1.0};
	else if (std::strcmp(name, "cyanColor") == 0) color = {0.0, 1.0, 1.0, 1.0};
	else if (std::strcmp(name, "magentaColor") == 0) color = {1.0, 0.0, 1.0, 1.0};
	else if (std::strcmp(name, "orangeColor") == 0) color = {1.0, 0.5, 0.0, 1.0};
	else if (std::strcmp(name, "purpleColor") == 0) color = {0.5, 0.0, 0.5, 1.0};
	else if (std::strcmp(name, "brownColor") == 0) color = {0.6, 0.4, 0.2, 1.0};
	else if (std::strcmp(name, "clearColor") == 0) color = {0.0, 0.0, 0.0, 0.0};
	grayscale = std::strcmp(name, "blackColor") == 0 ||
		std::strcmp(name, "whiteColor") == 0 ||
		std::strcmp(name, "grayColor") == 0 ||
		std::strcmp(name, "lightGrayColor") == 0 ||
		std::strcmp(name, "darkGrayColor") == 0 ||
		std::strcmp(name, "clearColor") == 0;
	return objc_autorelease(CreateNSColor(cls, color, grayscale));
}

id NSColorWithComponents(Class cls, SEL, double red, double green,
	double blue, double alpha)
{
	if (!std::isfinite(red) || !std::isfinite(green) ||
		!std::isfinite(blue) || !std::isfinite(alpha)) return nullptr;
	return objc_autorelease(CreateNSColor(cls, darling_windows_CGColor{
		std::clamp(red, 0.0, 1.0), std::clamp(green, 0.0, 1.0),
		std::clamp(blue, 0.0, 1.0), std::clamp(alpha, 0.0, 1.0)}));
}

id NSColorWithWhite(Class cls, SEL, double white, double alpha)
{
	if (!std::isfinite(white) || !std::isfinite(alpha)) return nullptr;
	white = std::clamp(white, 0.0, 1.0);
	alpha = std::clamp(alpha, 0.0, 1.0);
	return objc_autorelease(CreateNSColor(cls,
		darling_windows_CGColor{white, white, white, alpha}, true));
}

id NSColorWithHSB(Class cls, SEL, double hue, double saturation,
	double brightness, double alpha)
{
	if (!std::isfinite(hue) || !std::isfinite(saturation) ||
		!std::isfinite(brightness) || !std::isfinite(alpha)) return nullptr;
	hue = std::clamp(hue, 0.0, 1.0);
	saturation = std::clamp(saturation, 0.0, 1.0);
	brightness = std::clamp(brightness, 0.0, 1.0);
	alpha = std::clamp(alpha, 0.0, 1.0);
	const double sector = hue * 6.0;
	const int index = static_cast<int>(sector) % 6;
	const double fraction = sector - std::floor(sector);
	const double p = brightness * (1.0 - saturation);
	const double q = brightness * (1.0 - fraction * saturation);
	const double t = brightness * (1.0 - (1.0 - fraction) * saturation);
	darling_windows_CGColor color{};
	switch (index) {
	case 0: color = {brightness, t, p, alpha}; break;
	case 1: color = {q, brightness, p, alpha}; break;
	case 2: color = {p, brightness, t, alpha}; break;
	case 3: color = {p, q, brightness, alpha}; break;
	case 4: color = {t, p, brightness, alpha}; break;
	default: color = {brightness, p, q, alpha}; break;
	}
	return objc_autorelease(CreateNSColor(cls, color));
}

id NSColorWithCMYK(Class cls, SEL, double cyan, double magenta, double yellow,
	double black, double alpha)
{
	if (!std::isfinite(cyan) || !std::isfinite(magenta) ||
		!std::isfinite(yellow) || !std::isfinite(black) ||
		!std::isfinite(alpha)) return nullptr;
	cyan = std::clamp(cyan, 0.0, 1.0);
	magenta = std::clamp(magenta, 0.0, 1.0);
	yellow = std::clamp(yellow, 0.0, 1.0);
	black = std::clamp(black, 0.0, 1.0);
	alpha = std::clamp(alpha, 0.0, 1.0);
	const double remaining = 1.0 - black;
	return objc_autorelease(CreateNSColor(cls, {
		(1.0 - cyan) * remaining,
		(1.0 - magenta) * remaining,
		(1.0 - yellow) * remaining,
		alpha}));
}

void NSColorApply(id self, bool fill, bool stroke)
{
	if (g_current_cg_context == nullptr) return;
	darling_windows_CGColor color{};
	{
		std::lock_guard lock(g_color_mutex);
		const auto found = g_nscolors.find(self);
		if (found == g_nscolors.end()) return;
		color = found->second;
	}
	if (fill) CGContextSetRGBFillColor(g_current_cg_context, color.red,
		color.green, color.blue, color.alpha);
	if (fill) g_current_text_color = RGB(
		static_cast<BYTE>(std::lround(std::clamp(color.red, 0.0, 1.0) * 255.0)),
		static_cast<BYTE>(std::lround(std::clamp(color.green, 0.0, 1.0) * 255.0)),
		static_cast<BYTE>(std::lround(std::clamp(color.blue, 0.0, 1.0) * 255.0)));
	if (fill) g_current_text_alpha = std::clamp(color.alpha, 0.0, 1.0);
	if (stroke) CGContextSetRGBStrokeColor(g_current_cg_context, color.red,
		color.green, color.blue, color.alpha);
}

bool NSColorReadComponents(id self, darling_windows_CGColor& color)
{
	std::lock_guard lock(g_color_mutex);
	const auto found = g_nscolors.find(self);
	if (found == g_nscolors.end()) return false;
	color = found->second;
	return true;
}

id NSColorWithAlphaComponent(id self, SEL, double alpha)
{
	if (!std::isfinite(alpha)) return nullptr;
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return nullptr;
	bool grayscale = false;
	{
		std::lock_guard lock(g_color_mutex);
		grayscale = g_grayscale_nscolors.contains(self);
	}
	color.alpha = std::clamp(alpha, 0.0, 1.0);
	return objc_autorelease(CreateNSColor(object_getClass(self), color,
		grayscale));
}

id NSColorBlendedWithFraction(id self, SEL, double fraction, id other_color)
{
	if (!std::isfinite(fraction)) return nullptr;
	darling_windows_CGColor first{};
	darling_windows_CGColor second{};
	if (!NSColorReadComponents(self, first) ||
		!NSColorReadComponents(other_color, second)) return nullptr;
	bool both_grayscale = false;
	{
		std::lock_guard lock(g_color_mutex);
		both_grayscale = g_grayscale_nscolors.contains(self) &&
			g_grayscale_nscolors.contains(other_color);
	}
	const double inverse_fraction = 1.0 - fraction;
	first.red = inverse_fraction * first.red + fraction * second.red;
	first.green = inverse_fraction * first.green + fraction * second.green;
	first.blue = inverse_fraction * first.blue + fraction * second.blue;
	first.alpha = inverse_fraction * first.alpha + fraction * second.alpha;
	return objc_autorelease(CreateNSColor(object_getClass(self), first,
		both_grayscale));
}

struct NSHSBComponents final { double hue; double saturation; double brightness; };
struct NSCMYKComponents final { double cyan; double magenta; double yellow; double black; };

NSHSBComponents NSColorRGBToHSB(const darling_windows_CGColor& color)
{
	const double maximum = (std::max)({color.red, color.green, color.blue});
	const double minimum = (std::min)({color.red, color.green, color.blue});
	const double delta = maximum - minimum;
	double hue = 0.0;
	if (delta > 0.0) {
		if (maximum == color.red)
			hue = std::fmod((color.green - color.blue) / delta, 6.0);
		else if (maximum == color.green)
			hue = (color.blue - color.red) / delta + 2.0;
		else
			hue = (color.red - color.green) / delta + 4.0;
		hue /= 6.0;
		if (hue < 0.0) hue += 1.0;
	}
	return {hue, maximum == 0.0 ? 0.0 : delta / maximum, maximum};
}

NSCMYKComponents NSColorRGBToCMYK(const darling_windows_CGColor& color)
{
	const double maximum = (std::max)({color.red, color.green, color.blue});
	const double black = 1.0 - maximum;
	const double remaining = 1.0 - black;
	if (remaining <= 0.0) return {0.0, 0.0, 0.0, 1.0};
	return {(1.0 - color.red - black) / remaining,
		(1.0 - color.green - black) / remaining,
		(1.0 - color.blue - black) / remaining, black};
}

double NSColorComponent(id self, SEL selector)
{
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return 0.0;
	const char* name = sel_getName(selector);
	if (name == nullptr) return 0.0;
	if (std::strcmp(name, "redComponent") == 0) return color.red;
	if (std::strcmp(name, "greenComponent") == 0) return color.green;
	if (std::strcmp(name, "blueComponent") == 0) return color.blue;
	if (std::strcmp(name, "alphaComponent") == 0) return color.alpha;
	if (std::strcmp(name, "hueComponent") == 0)
		return NSColorRGBToHSB(color).hue;
	if (std::strcmp(name, "saturationComponent") == 0)
		return NSColorRGBToHSB(color).saturation;
	if (std::strcmp(name, "brightnessComponent") == 0)
		return NSColorRGBToHSB(color).brightness;
	if (std::strcmp(name, "whiteComponent") == 0) {
		std::lock_guard lock(g_color_mutex);
		return g_grayscale_nscolors.contains(self) ? color.red : 0.0;
	}
	return 0.0;
}

bool NSColorGetComponents(id self, SEL, double* red, double* green,
	double* blue, double* alpha)
{
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return false;
	if (red != nullptr) *red = color.red;
	if (green != nullptr) *green = color.green;
	if (blue != nullptr) *blue = color.blue;
	if (alpha != nullptr) *alpha = color.alpha;
	return true;
}

bool NSColorGetWhite(id self, SEL, double* white, double* alpha)
{
	darling_windows_CGColor color{};
	{
		std::lock_guard lock(g_color_mutex);
		const auto found = g_nscolors.find(self);
		if (found == g_nscolors.end() ||
			!g_grayscale_nscolors.contains(self)) return false;
		color = found->second;
	}
	if (white != nullptr) *white = color.red;
	if (alpha != nullptr) *alpha = color.alpha;
	return true;
}

bool NSColorGetHSB(id self, SEL, double* hue, double* saturation,
	double* brightness, double* alpha)
{
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return false;
	const auto components = NSColorRGBToHSB(color);
	if (hue != nullptr) *hue = components.hue;
	if (saturation != nullptr) *saturation = components.saturation;
	if (brightness != nullptr) *brightness = components.brightness;
	if (alpha != nullptr) *alpha = color.alpha;
	return true;
}

bool NSColorGetCMYK(id self, SEL, double* cyan, double* magenta,
	double* yellow, double* black, double* alpha)
{
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return false;
	const auto components = NSColorRGBToCMYK(color);
	if (cyan != nullptr) *cyan = components.cyan;
	if (magenta != nullptr) *magenta = components.magenta;
	if (yellow != nullptr) *yellow = components.yellow;
	if (black != nullptr) *black = components.black;
	if (alpha != nullptr) *alpha = color.alpha;
	return true;
}

std::uint64_t NSColorNumberOfComponents(id self, SEL)
{
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return 0;
	std::lock_guard lock(g_color_mutex);
	return g_grayscale_nscolors.contains(self) ? 2u : 4u;
}

void NSColorGetComponentArray(id self, SEL, double* components)
{
	if (components == nullptr) return;
	darling_windows_CGColor color{};
	if (!NSColorReadComponents(self, color)) return;
	bool grayscale = false;
	{
		std::lock_guard lock(g_color_mutex);
		grayscale = g_grayscale_nscolors.contains(self);
	}
	if (grayscale) {
		components[0] = color.red;
		components[1] = color.alpha;
		return;
	}
	components[0] = color.red;
	components[1] = color.green;
	components[2] = color.blue;
	components[3] = color.alpha;
}

void NSColorSet(id self, SEL) { NSColorApply(self, true, true); }
void NSColorSetFill(id self, SEL) { NSColorApply(self, true, false); }
void NSColorSetStroke(id self, SEL) { NSColorApply(self, false, true); }

void NSBezierPathFinalize(id object)
{
	std::lock_guard lock(g_bezier_mutex);
	g_bezier_paths.erase(object);
}

id NSBezierPathCreate(Class cls)
{
	if (cls == nullptr) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	try {
		std::lock_guard lock(g_bezier_mutex);
		g_bezier_paths.emplace(object, BezierPathState{});
	} catch (...) {
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSBezierPathFinalize)) {
		NSBezierPathFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSBezierPathFactory(Class cls, SEL)
{
	return objc_autorelease(NSBezierPathCreate(cls));
}

id NSBezierPathRectFactory(Class cls, SEL, DarlingObjcRect rect,
	BezierCommandKind kind)
{
	id object = NSBezierPathCreate(cls);
	if (object == nullptr) return nullptr;
	try {
		std::lock_guard lock(g_bezier_mutex);
		g_bezier_paths[object].commands.push_back(
			BezierCommand{kind, {}, rect});
	} catch (...) {
		objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

id NSBezierPathRectPathFactory(Class cls, SEL selector, DarlingObjcRect rect)
{
	const char* name = sel_getName(selector);
	return NSBezierPathRectFactory(cls, selector, rect,
		name != nullptr && std::strcmp(name, "bezierPathWithOvalInRect:") == 0 ?
			BezierCommandKind::Oval : BezierCommandKind::Rect);
}

std::vector<BezierCommand> NSBezierPathRoundedRectCommands(
	DarlingObjcRect rect, double x_radius, double y_radius)
{
	if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
		!std::isfinite(rect.width) || !std::isfinite(rect.height) ||
		!std::isfinite(x_radius) || !std::isfinite(y_radius)) return {};
	const double far_x = rect.x + rect.width;
	const double far_y = rect.y + rect.height;
	if (!std::isfinite(far_x) || !std::isfinite(far_y)) return {};
	const double x0 = std::min(rect.x, far_x);
	const double y0 = std::min(rect.y, far_y);
	const double x1 = std::max(rect.x, far_x);
	const double y1 = std::max(rect.y, far_y);
	const double width = x1 - x0;
	const double height = y1 - y0;
	if (!std::isfinite(width) || !std::isfinite(height)) return {};
	const double rx = std::clamp(x_radius, 0.0, width / 2.0);
	const double ry = std::clamp(y_radius, 0.0, height / 2.0);
	if (rx == 0.0 || ry == 0.0) {
		return {BezierCommand{BezierCommandKind::Rect, {},
			DarlingObjcRect{x0, y0, width, height}}};
	}
	constexpr double kappa = 0.5522847498307936;
	std::vector<BezierCommand> commands;
	commands.reserve(10);
	const auto point = [](double x, double y) { return DarlingObjcPoint{x, y}; };
	commands.push_back({BezierCommandKind::Move,
		{point(x0 + rx, y0)}});
	commands.push_back({BezierCommandKind::Line,
		{point(x1 - rx, y0)}});
	commands.push_back({BezierCommandKind::Curve,
		{point(x1, y0 + ry), point(x1 - rx + kappa * rx, y0),
			point(x1, y0 + ry - kappa * ry)}});
	commands.push_back({BezierCommandKind::Line,
		{point(x1, y1 - ry)}});
	commands.push_back({BezierCommandKind::Curve,
		{point(x1 - rx, y1), point(x1, y1 - ry + kappa * ry),
			point(x1 - rx + kappa * rx, y1)}});
	commands.push_back({BezierCommandKind::Line,
		{point(x0 + rx, y1)}});
	commands.push_back({BezierCommandKind::Curve,
		{point(x0, y1 - ry), point(x0 + rx - kappa * rx, y1),
			point(x0, y1 - ry + kappa * ry)}});
	commands.push_back({BezierCommandKind::Line,
		{point(x0, y0 + ry)}});
	commands.push_back({BezierCommandKind::Curve,
		{point(x0 + rx, y0), point(x0, y0 + ry - kappa * ry),
			point(x0 + rx - kappa * rx, y0)}});
	commands.push_back({BezierCommandKind::Close});
	return commands;
}

id NSBezierPathRoundedRectFactory(Class cls, SEL, DarlingObjcRect rect,
	double x_radius, double y_radius)
{
	auto commands = NSBezierPathRoundedRectCommands(rect, x_radius, y_radius);
	if (commands.empty()) return nullptr;
	id object = NSBezierPathCreate(cls);
	if (object == nullptr) return nullptr;
	try {
		std::lock_guard lock(g_bezier_mutex);
		g_bezier_paths[object].commands = std::move(commands);
	} catch (...) {
		objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

void NSBezierPathAppendRoundedRect(id self, SEL, DarlingObjcRect rect,
	double x_radius, double y_radius)
{
	auto commands = NSBezierPathRoundedRectCommands(rect, x_radius, y_radius);
	if (commands.empty()) return;
	try {
		std::lock_guard lock(g_bezier_mutex);
		auto& target = g_bezier_paths[self].commands;
		target.insert(target.end(), commands.begin(), commands.end());
	} catch (...) {
	}
}

void NSBezierPathAppend(id self, BezierCommand command)
{
	try {
		std::lock_guard lock(g_bezier_mutex);
		g_bezier_paths[self].commands.push_back(command);
	} catch (...) {
	}
}

void NSBezierPathMoveToPoint(id self, SEL, DarlingObjcPoint point)
{
	NSBezierPathAppend(self, BezierCommand{BezierCommandKind::Move, {point}});
}

void NSBezierPathLineToPoint(id self, SEL, DarlingObjcPoint point)
{
	NSBezierPathAppend(self, BezierCommand{BezierCommandKind::Line, {point}});
}

void NSBezierPathCurveToPoint(id self, SEL, DarlingObjcPoint end,
	DarlingObjcPoint control1, DarlingObjcPoint control2)
{
	NSBezierPathAppend(self, BezierCommand{BezierCommandKind::Curve,
		{end, control1, control2}});
}

void NSBezierPathClose(id self, SEL)
{
	NSBezierPathAppend(self, BezierCommand{BezierCommandKind::Close});
}

void NSBezierPathAppendRect(id self, SEL, DarlingObjcRect rect)
{
	NSBezierPathAppend(self, BezierCommand{BezierCommandKind::Rect, {}, rect});
}

void NSBezierPathAppendOval(id self, SEL, DarlingObjcRect rect)
{
	NSBezierPathAppend(self, BezierCommand{BezierCommandKind::Oval, {}, rect});
}

void NSBezierPathAppendPath(id self, SEL, id other)
{
	std::vector<BezierCommand> commands;
	{
		std::lock_guard lock(g_bezier_mutex);
		const auto found = g_bezier_paths.find(other);
		if (found == g_bezier_paths.end()) return;
		try { commands = found->second.commands; }
		catch (...) { return; }
	}
	try {
		std::lock_guard lock(g_bezier_mutex);
		auto& target = g_bezier_paths[self].commands;
		target.insert(target.end(), commands.begin(), commands.end());
	} catch (...) {
	}
}

void NSBezierPathRemoveAllPoints(id self, SEL)
{
	std::lock_guard lock(g_bezier_mutex);
	const auto found = g_bezier_paths.find(self);
	if (found != g_bezier_paths.end()) found->second.commands.clear();
}

void NSBezierPathSetLineWidth(id self, SEL, double width)
{
	if (!std::isfinite(width) || width < 0.0) return;
	std::lock_guard lock(g_bezier_mutex);
	g_bezier_paths[self].line_width = width;
}

double NSBezierPathLineWidth(id self, SEL)
{
	std::lock_guard lock(g_bezier_mutex);
	const auto found = g_bezier_paths.find(self);
	return found == g_bezier_paths.end() ? 1.0 : found->second.line_width;
}

void NSBezierPathSetLineCapStyle(id self, SEL, std::uint64_t style)
{
	if (style > darling_windows_kCGLineCapSquare) return;
	std::lock_guard lock(g_bezier_mutex);
	g_bezier_paths[self].line_cap_style = style;
}

std::uint64_t NSBezierPathLineCapStyle(id self, SEL)
{
	std::lock_guard lock(g_bezier_mutex);
	const auto found = g_bezier_paths.find(self);
	return found == g_bezier_paths.end() ? darling_windows_kCGLineCapButt :
		found->second.line_cap_style;
}

void NSBezierPathSetLineJoinStyle(id self, SEL, std::uint64_t style)
{
	if (style > darling_windows_kCGLineJoinBevel) return;
	std::lock_guard lock(g_bezier_mutex);
	g_bezier_paths[self].line_join_style = style;
}

std::uint64_t NSBezierPathLineJoinStyle(id self, SEL)
{
	std::lock_guard lock(g_bezier_mutex);
	const auto found = g_bezier_paths.find(self);
	return found == g_bezier_paths.end() ? darling_windows_kCGLineJoinMiter :
		found->second.line_join_style;
}

void NSBezierPathRender(id self, bool fill, bool even_odd = false)
{
	if (g_current_cg_context == nullptr) return;
	BezierPathState path;
	{
		std::lock_guard lock(g_bezier_mutex);
		const auto found = g_bezier_paths.find(self);
		if (found == g_bezier_paths.end()) return;
		try { path = found->second; }
		catch (...) { return; }
	}
	CGContextBeginPath(g_current_cg_context);
	for (const BezierCommand& command : path.commands) {
		switch (command.kind) {
		case BezierCommandKind::Move:
			CGContextMoveToPoint(g_current_cg_context, command.points[0].x,
				command.points[0].y);
			break;
		case BezierCommandKind::Line:
			CGContextAddLineToPoint(g_current_cg_context, command.points[0].x,
				command.points[0].y);
			break;
		case BezierCommandKind::Curve:
			CGContextAddCurveToPoint(g_current_cg_context,
				command.points[1].x, command.points[1].y,
				command.points[2].x, command.points[2].y,
				command.points[0].x, command.points[0].y);
			break;
		case BezierCommandKind::Close:
			CGContextClosePath(g_current_cg_context);
			break;
		case BezierCommandKind::Rect:
			CGContextAddRect(g_current_cg_context, {{command.rect.x,
				command.rect.y}, {command.rect.width, command.rect.height}});
			break;
		case BezierCommandKind::Oval:
			CGContextAddEllipseInRect(g_current_cg_context, {{command.rect.x,
				command.rect.y}, {command.rect.width, command.rect.height}});
			break;
		}
	}
	if (fill) {
		if (even_odd) CGContextEOFillPath(g_current_cg_context);
		else CGContextFillPath(g_current_cg_context);
	} else {
		CGContextSaveGState(g_current_cg_context);
		CGContextSetLineWidth(g_current_cg_context, path.line_width);
		CGContextSetLineCap(g_current_cg_context,
			static_cast<darling_windows_CGLineCap>(path.line_cap_style));
		CGContextSetLineJoin(g_current_cg_context,
			static_cast<darling_windows_CGLineJoin>(path.line_join_style));
		CGContextStrokePath(g_current_cg_context);
		CGContextRestoreGState(g_current_cg_context);
	}
}

void NSBezierPathFill(id self, SEL) { NSBezierPathRender(self, true); }
void NSBezierPathEvenOddFill(id self, SEL) { NSBezierPathRender(self, true, true); }
void NSBezierPathStroke(id self, SEL) { NSBezierPathRender(self, false); }

struct BezierBounds final {
	double min_x{(std::numeric_limits<double>::infinity)()};
	double min_y{(std::numeric_limits<double>::infinity)()};
	double max_x{-(std::numeric_limits<double>::infinity)()};
	double max_y{-(std::numeric_limits<double>::infinity)()};
	void Include(DarlingObjcPoint point)
	{
		if (!std::isfinite(point.x) || !std::isfinite(point.y)) return;
		min_x = (std::min)(min_x, point.x);
		min_y = (std::min)(min_y, point.y);
		max_x = (std::max)(max_x, point.x);
		max_y = (std::max)(max_y, point.y);
	}
	void Include(DarlingObjcRect rect)
	{
		Include(DarlingObjcPoint{rect.x, rect.y});
		Include(DarlingObjcPoint{rect.x + rect.width,
			rect.y + rect.height});
	}
	[[nodiscard]] bool IsEmpty() const
	{
		return !std::isfinite(min_x) || !std::isfinite(min_y) ||
			!std::isfinite(max_x) || !std::isfinite(max_y);
	}
	[[nodiscard]] DarlingObjcRect Rect() const
	{
		return IsEmpty() ? DarlingObjcRect{} : DarlingObjcRect{min_x, min_y,
			max_x - min_x, max_y - min_y};
	}
};

double CubicCoordinate(double p0, double p1, double p2, double p3, double t)
{
	const double inverse = 1.0 - t;
	return inverse * inverse * inverse * p0 +
		3.0 * inverse * inverse * t * p1 +
		3.0 * inverse * t * t * p2 + t * t * t * p3;
}

void IncludeCubicExtrema(double p0, double p1, double p2, double p3,
	auto&& include)
{
	const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
	const double b = 2.0 * (p0 - 2.0 * p1 + p2);
	const double c = p1 - p0;
	if (std::abs(a) <= std::numeric_limits<double>::epsilon()) {
		if (std::abs(b) <= std::numeric_limits<double>::epsilon()) return;
		const double t = -c / b;
		if (t > 0.0 && t < 1.0) include(t);
		return;
	}
	const double discriminant = b * b - 4.0 * a * c;
	if (discriminant < 0.0 || !std::isfinite(discriminant)) return;
	const double root = std::sqrt(discriminant);
	const double first = (-b - root) / (2.0 * a);
	const double second = (-b + root) / (2.0 * a);
	if (first > 0.0 && first < 1.0) include(first);
	if (second > 0.0 && second < 1.0 && second != first) include(second);
}

BezierPathState NSBezierPathSnapshot(id self)
{
	try {
		std::lock_guard lock(g_bezier_mutex);
		const auto found = g_bezier_paths.find(self);
		return found == g_bezier_paths.end() ? BezierPathState{} : found->second;
	} catch (...) {
		return {};
	}
}

DarlingObjcRect NSBezierPathBounds(id self, SEL)
{
	const BezierPathState path = NSBezierPathSnapshot(self);
	BezierBounds bounds;
	DarlingObjcPoint current{};
	bool has_current = false;
	for (const BezierCommand& command : path.commands) {
		switch (command.kind) {
		case BezierCommandKind::Move:
			current = command.points[0];
			has_current = true;
			bounds.Include(current);
			break;
		case BezierCommandKind::Line:
			bounds.Include(command.points[0]);
			current = command.points[0];
			has_current = true;
			break;
		case BezierCommandKind::Curve: {
			const DarlingObjcPoint end = command.points[0];
			const DarlingObjcPoint control1 = command.points[1];
			const DarlingObjcPoint control2 = command.points[2];
			if (!has_current) {
				bounds.Include(end);
				bounds.Include(control1);
				bounds.Include(control2);
			} else {
				bounds.Include(current);
				bounds.Include(end);
				const auto include_both = [&](double t) {
					bounds.Include(DarlingObjcPoint{CubicCoordinate(current.x, control1.x,
						control2.x, end.x, t), CubicCoordinate(current.y,
						control1.y, control2.y, end.y, t)});
				};
				IncludeCubicExtrema(current.x, control1.x, control2.x, end.x,
					include_both);
				IncludeCubicExtrema(current.y, control1.y, control2.y, end.y,
					include_both);
			}
			current = end;
			has_current = true;
			break;
		}
		case BezierCommandKind::Close:
			break;
		case BezierCommandKind::Rect:
		case BezierCommandKind::Oval:
			bounds.Include(command.rect);
			break;
		}
	}
	return bounds.Rect();
}

DarlingObjcRect NSBezierPathControlPointBounds(id self, SEL)
{
	const BezierPathState path = NSBezierPathSnapshot(self);
	BezierBounds bounds;
	for (const BezierCommand& command : path.commands) {
		switch (command.kind) {
		case BezierCommandKind::Move:
		case BezierCommandKind::Line:
			bounds.Include(command.points[0]);
			break;
		case BezierCommandKind::Curve:
			bounds.Include(command.points[0]);
			bounds.Include(command.points[1]);
			bounds.Include(command.points[2]);
			break;
		case BezierCommandKind::Rect:
		case BezierCommandKind::Oval:
			bounds.Include(command.rect);
			break;
		case BezierCommandKind::Close:
			break;
		}
	}
	return bounds.Rect();
}

bool NSBezierPathIsEmpty(id self, SEL)
{
	std::lock_guard lock(g_bezier_mutex);
	const auto found = g_bezier_paths.find(self);
	return found == g_bezier_paths.end() || found->second.commands.empty();
}

id CreateNSNumber(Class cls, double value, bool boolean = false)
{
	if (cls == nullptr || !std::isfinite(value)) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	try {
		std::lock_guard lock(g_number_mutex);
		g_numbers.emplace(object, ObjcNumberValue{value, 0, 0, false, false,
			boolean});
	} catch (...) {
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSNumberFinalize)) {
		NSNumberFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id CreateIntegerNSNumber(Class cls, std::int64_t value)
{
	id object = CreateNSNumber(cls, static_cast<double>(value));
	if (object == nullptr) return nullptr;
	std::lock_guard lock(g_number_mutex);
	auto found = g_numbers.find(object);
	if (found != g_numbers.end()) {
		found->second.integer = value;
		found->second.integral = true;
	}
	return object;
}

id CreateUnsignedNSNumber(Class cls, std::uint64_t value)
{
	id object = CreateNSNumber(cls, static_cast<double>(value));
	if (object == nullptr) return nullptr;
	std::lock_guard lock(g_number_mutex);
	auto found = g_numbers.find(object);
	if (found != g_numbers.end()) {
		found->second.unsigned_integer = value;
		found->second.integral = true;
		found->second.unsigned_value = true;
	}
	return object;
}

id NSNumberWithInt(Class cls, SEL, int value)
{
	return objc_autorelease(CreateIntegerNSNumber(cls, value));
}

id NSNumberWithInteger(Class cls, SEL, std::int64_t value)
{
	return objc_autorelease(CreateIntegerNSNumber(cls, value));
}

id NSNumberWithUnsignedInteger(Class cls, SEL, std::uint64_t value)
{
	return objc_autorelease(CreateUnsignedNSNumber(cls, value));
}

id NSNumberWithDouble(Class cls, SEL, double value)
{
	return objc_autorelease(CreateNSNumber(cls, value));
}

id NSNumberWithBool(Class cls, SEL, bool value)
{
	return objc_autorelease(CreateNSNumber(cls, value ? 1.0 : 0.0, true));
}

double NSNumberGetDouble(id self)
{
	std::lock_guard lock(g_number_mutex);
	auto found = g_numbers.find(self);
	if (found == g_numbers.end()) return 0.0;
	if (!found->second.integral) return found->second.real;
	return found->second.unsigned_value ?
		static_cast<double>(found->second.unsigned_integer) :
		static_cast<double>(found->second.integer);
}

int NSNumberIntValue(id self, SEL) { return static_cast<int>(NSNumberGetDouble(self)); }
std::int64_t NSNumberIntegerValue(id self, SEL) {
	ObjcNumberValue stored{};
	{
		std::lock_guard lock(g_number_mutex);
		auto found = g_numbers.find(self);
		if (found == g_numbers.end()) return 0;
		stored = found->second;
	}
	if (stored.integral && !stored.unsigned_value) return stored.integer;
	return static_cast<std::int64_t>(NSNumberGetDouble(self));
}
std::uint64_t NSNumberUnsignedIntegerValue(id self, SEL) {
	ObjcNumberValue stored{};
	{
		std::lock_guard lock(g_number_mutex);
		auto found = g_numbers.find(self);
		if (found == g_numbers.end()) return 0;
		stored = found->second;
	}
	if (stored.integral && stored.unsigned_value) return stored.unsigned_integer;
	return static_cast<std::uint64_t>(std::max(0.0, NSNumberGetDouble(self)));
}
double NSNumberDoubleValue(id self, SEL) { return NSNumberGetDouble(self); }
bool NSNumberBoolValue(id self, SEL) { return NSNumberGetDouble(self) != 0.0; }

id NSNumberStringValue(id self, SEL)
{
	char buffer[64]{};
	ObjcNumberValue stored{};
	{
		std::lock_guard lock(g_number_mutex);
		auto found = g_numbers.find(self);
		if (found == g_numbers.end()) return nullptr;
		stored = found->second;
	}
	int written = 0;
	if (stored.integral && stored.unsigned_value)
		written = std::snprintf(buffer, sizeof(buffer), "%llu",
			static_cast<unsigned long long>(stored.unsigned_integer));
	else if (stored.integral)
		written = std::snprintf(buffer, sizeof(buffer), "%lld",
			static_cast<long long>(stored.integer));
	else
		written = std::snprintf(buffer, sizeof(buffer), "%.17g", stored.real);
	return written > 0 && static_cast<std::size_t>(written) < sizeof(buffer) ?
		objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), buffer)) :
		nullptr;
}

bool NSNumberIsEqual(id self, SEL, id other)
{
	if (self == other) return true;
	const Class cls = object_getClass(other);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	if (name == nullptr || std::strcmp(name, "NSNumber") != 0) return false;
	ObjcNumberValue left{};
	ObjcNumberValue right{};
	{
		std::lock_guard lock(g_number_mutex);
		auto left_value = g_numbers.find(self);
		auto right_value = g_numbers.find(other);
		if (left_value == g_numbers.end() || right_value == g_numbers.end())
			return false;
		left = left_value->second;
		right = right_value->second;
	}
	if (left.integral && right.integral &&
		left.unsigned_value == right.unsigned_value)
		return left.unsigned_value ?
			left.unsigned_integer == right.unsigned_integer :
			left.integer == right.integer;
	const double left_number = left.integral ? (left.unsigned_value ?
		static_cast<double>(left.unsigned_integer) :
		static_cast<double>(left.integer)) : left.real;
	const double right_number = right.integral ? (right.unsigned_value ?
		static_cast<double>(right.unsigned_integer) :
		static_cast<double>(right.integer)) : right.real;
	return left_number == right_number;
}

void NSDateFinalize(id object)
{
	std::lock_guard lock(g_date_mutex);
	g_dates.erase(object);
}

id CreateNSDate(Class cls, double absolute_time)
{
	if (cls == nullptr || !std::isfinite(absolute_time)) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	try {
		std::lock_guard lock(g_date_mutex);
		g_dates.emplace(object, absolute_time);
	} catch (...) {
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSDateFinalize)) {
		NSDateFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSDateWithReferenceTime(Class cls, SEL, double absolute_time)
{
	return objc_autorelease(CreateNSDate(cls, absolute_time));
}

double NSDateNowReferenceTime()
{
	constexpr double unix_to_reference_epoch = 978307200.0;
	const auto now = std::chrono::system_clock::now().time_since_epoch();
	return std::chrono::duration<double>(now).count() - unix_to_reference_epoch;
}

id NSDateNow(Class cls, SEL)
{
	return objc_autorelease(CreateNSDate(cls, NSDateNowReferenceTime()));
}

id NSDateDistantFuture(Class cls, SEL)
{
	return objc_autorelease(CreateNSDate(cls, 1.0e12));
}

id NSDateDistantPast(Class cls, SEL)
{
	return objc_autorelease(CreateNSDate(cls, -1.0e12));
}

double NSDateClassReferenceTime(Class, SEL)
{
	return NSDateNowReferenceTime();
}

id NSDateWithTimeIntervalSinceNow(Class cls, SEL, double interval)
{
	if (!std::isfinite(interval)) return nullptr;
	return objc_autorelease(CreateNSDate(cls, NSDateNowReferenceTime() + interval));
}

id NSDateWithTimeIntervalSince1970(Class cls, SEL, double unix_time)
{
	if (!std::isfinite(unix_time)) return nullptr;
	return objc_autorelease(CreateNSDate(cls, unix_time - 978307200.0));
}

double NSDateReferenceTime(id self, SEL selector);

id NSDateWithTimeIntervalSinceDate(Class cls, SEL, double interval, id other)
{
	if (!std::isfinite(interval) || other == nullptr) return nullptr;
	return objc_autorelease(CreateNSDate(cls,
		NSDateReferenceTime(other, nullptr) + interval));
}

id NSDateInitWithReferenceTime(id self, SEL, double absolute_time)
{
	if (self == nullptr || !std::isfinite(absolute_time)) return nullptr;
	try {
		std::lock_guard lock(g_date_mutex);
		g_dates[self] = absolute_time;
	} catch (...) {
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(self, &NSDateFinalize))
		return nullptr;
	return self;
}

id NSDateInitWithTimeIntervalSinceNow(id self, SEL, double interval)
{
	if (!std::isfinite(interval)) return nullptr;
	return NSDateInitWithReferenceTime(self, nullptr,
		NSDateNowReferenceTime() + interval);
}

id NSDateInitWithTimeIntervalSince1970(id self, SEL, double unix_time)
{
	if (!std::isfinite(unix_time)) return nullptr;
	return NSDateInitWithReferenceTime(self, nullptr,
		unix_time - 978307200.0);
}

id NSDateInitWithTimeIntervalSinceDate(id self, SEL, double interval, id other)
{
	if (!std::isfinite(interval) || other == nullptr) return nullptr;
	return NSDateInitWithReferenceTime(self, nullptr,
		NSDateReferenceTime(other, nullptr) + interval);
}

double NSDateReferenceTime(id self, SEL)
{
	std::lock_guard lock(g_date_mutex);
	auto found = g_dates.find(self);
	return found == g_dates.end() ? 0.0 : found->second;
}

double NSDateTimeIntervalSinceNow(id self, SEL)
{
	return NSDateReferenceTime(self, nullptr) - NSDateNowReferenceTime();
}

double NSDateTimeIntervalSince1970(id self, SEL)
{
	return NSDateReferenceTime(self, nullptr) + 978307200.0;
}

double NSDateTimeIntervalSinceDate(id self, SEL, id other)
{
	return NSDateReferenceTime(self, nullptr) - NSDateReferenceTime(other, nullptr);
}

id NSDateEarlierDate(id self, SEL, id other)
{
	if (other == nullptr) return objc_retainAutorelease(self);
	return objc_retainAutorelease(NSDateReferenceTime(self, nullptr) <=
		NSDateReferenceTime(other, nullptr) ? self : other);
}

id NSDateLaterDate(id self, SEL, id other)
{
	if (other == nullptr) return objc_retainAutorelease(self);
	return objc_retainAutorelease(NSDateReferenceTime(self, nullptr) >=
		NSDateReferenceTime(other, nullptr) ? self : other);
}

id NSDateByAddingTimeInterval(id self, SEL, double seconds)
{
	if (!std::isfinite(seconds)) return nullptr;
	return objc_autorelease(CreateNSDate(object_getClass(self),
		NSDateReferenceTime(self, nullptr) + seconds));
}

bool NSDateIsEqual(id self, SEL, id other)
{
	if (self == other) return true;
	if (other == nullptr) return false;
	const Class cls = object_getClass(other);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	if (name == nullptr || std::strcmp(name, "NSDate") != 0) return false;
	return NSDateReferenceTime(self, nullptr) ==
		NSDateReferenceTime(other, nullptr);
}

std::int64_t NSDateCompare(id self, SEL, id other)
{
	const double left = NSDateReferenceTime(self, nullptr);
	const double right = NSDateReferenceTime(other, nullptr);
	return left < right ? -1 : left > right ? 1 : 0;
}

id NSRunLoopObjectForRef(Class cls, darling_windows_CFRunLoopRef loop)
{
	if (cls == nullptr || loop == nullptr) return nullptr;
	std::lock_guard lock(g_nsrunloop_mutex);
	const auto found = g_nsrunloop_objects.find(loop);
	if (found != g_nsrunloop_objects.end())
		return objc_retainAutorelease(found->second);
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	try {
		g_nsrunloop_objects.emplace(loop, objc_retain(object));
		g_nsrunloop_refs.emplace(object, loop);
	} catch (...) {
		g_nsrunloop_objects.erase(loop);
		g_nsrunloop_refs.erase(object);
		objc_release(object);
		objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

id NSRunLoopCurrent(Class cls, SEL)
{
	return NSRunLoopObjectForRef(cls, darling_windows_CFRunLoopGetCurrent());
}

id NSRunLoopMain(Class cls, SEL)
{
	return NSRunLoopObjectForRef(cls, darling_windows_CFRunLoopGetMain());
}

id NSRunLoopDefaultMode(Class, SEL)
{
	return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
		"kCFRunLoopDefaultMode");
}

id NSRunLoopCommonModes(Class, SEL)
{
	return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
		"kCFRunLoopCommonModes");
}

darling_windows_CFRunLoopRef NSRunLoopGetCFRunLoop(id self, SEL)
{
	std::lock_guard lock(g_nsrunloop_mutex);
	const auto found = g_nsrunloop_refs.find(self);
	return found == g_nsrunloop_refs.end() ? nullptr : found->second;
}

bool NSRunLoopRunMode(id self, SEL, id mode, id limit_date)
{
	const auto loop = NSRunLoopGetCFRunLoop(self, nullptr);
	const char* mode_name = mode == nullptr ? "kCFRunLoopDefaultMode" :
		NSStringUTF8String(mode, nullptr);
	if (loop == nullptr || mode_name == nullptr) return false;
	if (!darling_windows_CFRunLoopContainsPendingBlockForMode(loop, mode_name))
		return false;
	const double limit = NSDateReferenceTime(limit_date, nullptr);
	const double interval = (std::max)(0.0, limit - NSDateNowReferenceTime());
	return darling_windows_CFRunLoopRunInModeForLoop(loop, mode_name,
		interval, true) >= 0;
}

void NSRunLoopRunUntilDate(id self, SEL, id limit_date)
{
	const auto loop = NSRunLoopGetCFRunLoop(self, nullptr);
	if (loop == nullptr) return;
	const double limit = NSDateReferenceTime(limit_date, nullptr);
	for (;;) {
		const double remaining = limit - NSDateNowReferenceTime();
		if (!std::isfinite(remaining) || remaining <= 0.0) return;
		const int result = darling_windows_CFRunLoopRunInModeForLoop(loop,
			"kCFRunLoopDefaultMode", remaining, false);
		if (result != 1) return;
	}
}

void NSRunLoopRun(id self, SEL)
{
	const auto loop = NSRunLoopGetCFRunLoop(self, nullptr);
	if (loop == nullptr) return;
	while (darling_windows_CFRunLoopRunInModeForLoop(loop,
		"kCFRunLoopDefaultMode", 315360000.0, false) == 1) { }
}

std::shared_ptr<AppKitTimerState> NSTimerState(id timer)
{
	std::lock_guard lock(g_appkit_timer_mutex);
	const auto found = g_appkit_timers.find(timer);
	return found == g_appkit_timers.end() ? nullptr : found->second;
}

void NSTimerInvalidateState(id timer, bool release_scheduled_ownership)
{
	std::shared_ptr<AppKitTimerState> state;
	{
		std::lock_guard lock(g_appkit_timer_mutex);
		const auto found = g_appkit_timers.find(timer);
		if (found == g_appkit_timers.end()) return;
		state = found->second;
	}
	id target = nullptr;
	id user_info = nullptr;
	bool release_timer = false;
	{
		std::lock_guard lock(state->mutex);
		if (!state->valid) return;
		state->valid = false;
		state->callback_pending = false;
		target = std::exchange(state->target, nullptr);
		user_info = std::exchange(state->user_info, nullptr);
		release_timer = release_scheduled_ownership && state->scheduled;
		state->scheduled = false;
	}
	state->condition.notify_all();
	if (target != nullptr) objc_release(target);
	if (user_info != nullptr) objc_release(user_info);
	if (release_timer) objc_release(timer);
}

void NSTimerFinalize(id object)
{
	NSTimerInvalidateState(object, false);
	std::lock_guard lock(g_appkit_timer_mutex);
	g_appkit_timers.erase(object);
}

void NSTimerDeliver(const std::shared_ptr<AppKitTimerState>& state,
	bool scheduled_fire, double scheduled_date = 0.0,
	std::uint64_t scheduled_generation = 0)
{
	id timer = nullptr;
	id target = nullptr;
	id user_info = nullptr;
	SEL selector = nullptr;
	bool repeats = false;
	{
		std::lock_guard lock(state->mutex);
		if (!state->valid) return;
		if (scheduled_fire && state->fire_date_generation !=
			scheduled_generation) {
			state->callback_pending = false;
			state->condition.notify_all();
			return;
		}
		timer = state->timer;
		target = state->target;
		user_info = state->user_info;
		selector = state->selector;
		repeats = state->repeats;
		if (scheduled_fire) state->last_fire_date = scheduled_date;
		else if (!repeats) state->last_fire_date = NSDateNowReferenceTime();
		if (timer != nullptr) objc_retain(timer);
		if (target != nullptr) objc_retain(target);
		if (user_info != nullptr) objc_retain(user_info);
	}
	if (target != nullptr && selector != nullptr)
		darling_objc_msgSend_void1_object(target, selector, timer);
	if (!repeats) NSTimerInvalidateState(timer, true);
	{
		std::lock_guard lock(state->mutex);
		if (state->valid && repeats && scheduled_fire &&
			state->fire_date_generation == scheduled_generation) {
			const double now = NSDateNowReferenceTime();
			state->fire_date = scheduled_date + state->interval;
			do { state->fire_date += state->interval; }
			while (state->fire_date <= now && std::isfinite(state->fire_date));
			++state->fire_date_generation;
		}
		state->callback_pending = false;
	}
	if (target != nullptr) objc_release(target);
	if (user_info != nullptr) objc_release(user_info);
	if (timer != nullptr) objc_release(timer);
	state->condition.notify_all();
}

struct NSTimerDeliveryContext final {
	std::shared_ptr<AppKitTimerState> state;
	double due{};
	std::uint64_t generation{};
};

void NSTimerCFRunLoopDeliver(void* context)
{
	std::unique_ptr<NSTimerDeliveryContext> delivery(
		static_cast<NSTimerDeliveryContext*>(context));
	if (delivery != nullptr)
		NSTimerDeliver(delivery->state, true, delivery->due,
			delivery->generation);
}

bool EnqueueNSTimerDelivery(const std::shared_ptr<AppKitTimerState>& state,
	double due, std::uint64_t generation)
{
	darling_windows_CFRunLoopRef run_loop = nullptr;
	bool appkit_loop = false;
	{
		std::lock_guard lock(state->mutex);
		run_loop = state->run_loop;
		appkit_loop = state->appkit_loop;
	}
	if (appkit_loop)
		return EnqueueAppKitCallback([state, due, generation] {
			NSTimerDeliver(state, true, due, generation);
		});
	auto* delivery = new (std::nothrow) NSTimerDeliveryContext{
		state, due, generation};
	if (delivery == nullptr) return false;
	if (!darling_windows_CFRunLoopPerformBlock(run_loop,
		&NSTimerCFRunLoopDeliver, delivery)) {
		delete delivery;
		return false;
	}
	return true;
}

id NSTimerWithTimeInterval(Class cls, SEL, double interval, id target,
	SEL timer_selector, id user_info, bool repeats)
{
	if (cls == nullptr || target == nullptr || timer_selector == nullptr ||
		!std::isfinite(interval) || interval < 0.0 || interval > 1.0e9 ||
		(repeats && interval == 0.0)) return nullptr;
	id timer = class_createInstance(cls, 0);
	if (timer == nullptr) return nullptr;
	std::shared_ptr<AppKitTimerState> state;
	try {
		state = std::make_shared<AppKitTimerState>();
		state->timer = timer;
		state->target = objc_retain(target);
		state->user_info = user_info == nullptr ? nullptr : objc_retain(user_info);
		state->selector = timer_selector;
		state->interval = interval;
		state->repeats = repeats;
		state->fire_date = NSDateNowReferenceTime() + interval;
		state->last_fire_date = state->fire_date;
		{
			std::lock_guard lock(g_appkit_timer_mutex);
			g_appkit_timers.emplace(timer, std::move(state));
		}
	} catch (...) {
		if (state != nullptr) {
			if (state->target != nullptr) objc_release(state->target);
			if (state->user_info != nullptr) objc_release(state->user_info);
		}
		objc_release(timer);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(timer, &NSTimerFinalize)) {
		NSTimerInvalidateState(timer, false);
		NSTimerFinalize(timer);
		objc_release(timer);
		return nullptr;
	}
	return objc_autorelease(timer);
}

id NSTimerScheduledWithTimeInterval(Class cls, SEL selector, double interval,
	id target, SEL timer_selector, id user_info, bool repeats)
{
	id timer = NSTimerWithTimeInterval(cls, nullptr, interval, target,
		timer_selector, user_info, repeats);
	if (timer == nullptr) return nullptr;
	auto state = NSTimerState(timer);
	if (state == nullptr) return nullptr;
	{
		std::lock_guard lock(state->mutex);
		if (!state->valid || state->scheduled) return nullptr;
		state->scheduled = true;
		state->owner_thread = GetCurrentThreadId();
		state->run_loop = darling_windows_CFRunLoopGetCurrent();
		state->appkit_loop = state->owner_thread ==
			g_appkit_main_thread_id.load(std::memory_order_acquire) ||
			state->owner_thread == g_application_thread_id.load(
				std::memory_order_acquire);
		objc_retain(timer);
	}
	try {
		std::thread([state] {
			std::unique_lock lock(state->mutex);
			for (;;) {
				if (!state->valid) return;
				if (state->callback_pending) {
					state->condition.wait(lock, [&] {
						return !state->valid || !state->callback_pending;
					});
					continue;
				}
				const std::uint64_t generation = state->fire_date_generation;
			const double due = state->fire_date;
			const double delay = (std::min)(86400.0, (std::max)(0.0,
				due - NSDateNowReferenceTime()));
				if (state->condition.wait_for(lock,
					std::chrono::duration<double>(delay), [&] {
						return !state->valid || state->callback_pending ||
							state->fire_date_generation != generation;
					})) continue;
				if (NSDateNowReferenceTime() < due) continue;
				state->callback_pending = true;
				lock.unlock();
				if (!EnqueueNSTimerDelivery(state, due, generation)) {
					bool one_shot = false;
					id timer = nullptr;
					{
						std::lock_guard retry_lock(state->mutex);
						state->callback_pending = false;
						one_shot = !state->repeats;
						timer = one_shot ? state->timer : nullptr;
						if (!one_shot) {
							state->fire_date = NSDateNowReferenceTime() +
								state->interval;
							++state->fire_date_generation;
						}
					}
					if (timer != nullptr)
						NSTimerInvalidateState(timer, true);
				}
				lock.lock();
				if (!state->repeats) return;
			}
		}).detach();
	} catch (...) {
		NSTimerInvalidateState(timer, true);
		return nullptr;
	}
	return timer;
}

void NSTimerInvalidate(id self, SEL)
{
	NSTimerInvalidateState(self, true);
}

void NSTimerFire(id self, SEL)
{
	if (auto state = NSTimerState(self)) NSTimerDeliver(state, false);
}

bool NSTimerIsValid(id self, SEL)
{
	auto state = NSTimerState(self);
	if (state == nullptr) return false;
	std::lock_guard lock(state->mutex);
	return state->valid;
}

double NSTimerTimeInterval(id self, SEL)
{
	auto state = NSTimerState(self);
	if (state == nullptr) return 0.0;
	std::lock_guard lock(state->mutex);
	return state->interval;
}

id NSTimerFireDate(id self, SEL)
{
	auto state = NSTimerState(self);
	if (state == nullptr) return nullptr;
	double fire_date = 0.0;
	{
		std::lock_guard lock(state->mutex);
		fire_date = state->valid ? state->fire_date : state->last_fire_date;
	}
	return objc_autorelease(CreateNSDate(objc_getClass("NSDate"), fire_date));
}

void NSTimerSetFireDate(id self, SEL, id date)
{
	auto state = NSTimerState(self);
	if (state == nullptr || date == nullptr || class_getInstanceMethod(
		object_getClass(date), sel_registerName("timeIntervalSinceReferenceDate")) ==
		nullptr) return;
	const double fire_date = NSDateReferenceTime(date, nullptr);
	if (!std::isfinite(fire_date)) return;
	{
		std::lock_guard lock(state->mutex);
		if (!state->valid) return;
		state->fire_date = fire_date;
		++state->fire_date_generation;
	}
	state->condition.notify_all();
}

id NSTimerUserInfo(id self, SEL)
{
	auto state = NSTimerState(self);
	if (state == nullptr) return nullptr;
	std::lock_guard lock(state->mutex);
	return state->user_info == nullptr ? nullptr :
		objc_retainAutorelease(state->user_info);
}

id NSNullSingleton(Class cls, SEL)
{
	std::call_once(g_null_once,
		[cls] { g_null_object = class_createInstance(cls, 0); });
	return g_null_object;
}

darling_windows_CFStringRef NSStringToCFString(id string)
{
	if (string == nullptr) return nullptr;
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		string, sel_registerName("UTF8String")));
	return utf8 == nullptr ? nullptr :
		darling_windows_CFStringCreateWithCString(utf8);
}

id NSStringFromCFString(darling_windows_CFStringRef string)
{
	if (string == nullptr) return nullptr;
	const char* utf8 = darling_windows_CFStringGetCStringPtr(string);
	if (utf8 != nullptr)
		return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), utf8));
	const auto max_size = darling_windows_CFStringGetMaximumSizeForEncoding(
		darling_windows_CFStringGetLength(string), 0x08000100);
	if (max_size < 0 || max_size > 16 * 1024 * 1024) return nullptr;
	std::string buffer(static_cast<std::size_t>(max_size) + 1, '\0');
	if (!darling_windows_CFStringGetCString(string, buffer.data(),
		static_cast<darling_windows_CFIndex>(buffer.size()))) return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		buffer.c_str()));
}

void NSBundleFinalize(id object)
{
	darling_windows_CFBundleRef bundle = nullptr;
	{
		std::lock_guard lock(g_bundle_mutex);
		auto found = g_bundles.find(object);
		if (found == g_bundles.end()) return;
		bundle = found->second;
		g_bundles.erase(found);
	}
	darling_windows_CFRelease(bundle);
}

bool NSBundleAttach(id object, darling_windows_CFBundleRef bundle)
{
	if (object == nullptr || bundle == nullptr) return false;
	try {
		std::lock_guard lock(g_bundle_mutex);
		if (!g_bundles.emplace(object, bundle).second) {
			darling_windows_CFRelease(bundle);
			return false;
		}
	} catch (...) {
		darling_windows_CFRelease(bundle);
		return false;
	}
	if (darling_objc_register_object_finalizer(object, &NSBundleFinalize))
		return true;
	NSBundleFinalize(object);
	return false;
}

darling_windows_CFBundleRef NSBundleGetCFBundle(id object)
{
	std::lock_guard lock(g_bundle_mutex);
	auto found = g_bundles.find(object);
	return found == g_bundles.end() ? nullptr : found->second;
}

id NSBundleCreateObject(Class cls, darling_windows_CFBundleRef bundle)
{
	if (cls == nullptr || bundle == nullptr) {
		darling_windows_CFRelease(bundle);
		return nullptr;
	}
	id object = class_createInstance(cls, 0);
	if (object == nullptr || !NSBundleAttach(object, bundle)) {
		if (object != nullptr) objc_release(object);
		else darling_windows_CFRelease(bundle);
		return nullptr;
	}
	return object;
}

darling_windows_CFURLRef NSBundleURLForPath(id path)
{
	const char* utf8 = path == nullptr ? nullptr :
		static_cast<const char*>(darling_objc_msgSend_pointer0(path,
			sel_registerName("UTF8String")));
	return utf8 == nullptr ? nullptr :
		darling_windows_CFURLCreateWithFileSystemPath(utf8);
}

darling_windows_CFURLRef NSBundleCFURLForNSURL(id url)
{
	if (url == nullptr) return nullptr;
	id path = darling_objc_msgSend_object0(url, sel_registerName("path"));
	return NSBundleURLForPath(path);
}

id NSBundleMainBundle(Class cls, SEL)
{
	std::lock_guard lock(g_bundle_mutex);
	if (g_main_bundle_object == nullptr) {
		auto bundle = darling_windows_CFBundleGetMainBundle();
		if (bundle == nullptr) return nullptr;
		darling_windows_CFRetain(bundle);
		id object = class_createInstance(cls, 0);
		if (object == nullptr || !g_bundles.emplace(object, bundle).second) {
			if (object != nullptr) objc_release(object);
			darling_windows_CFRelease(bundle);
			return nullptr;
		}
		if (!darling_objc_register_object_finalizer(object, &NSBundleFinalize)) {
			g_bundles.erase(object);
			objc_release(object);
			darling_windows_CFRelease(bundle);
			return nullptr;
		}
		g_main_bundle_object = object;
	}
	return objc_retainAutorelease(g_main_bundle_object);
}

id NSBundleWithPath(Class cls, SEL, id path)
{
	auto url = NSBundleURLForPath(path);
	if (url == nullptr) return nullptr;
	auto bundle = darling_windows_CFBundleCreate(nullptr, url);
	darling_windows_CFRelease(url);
	return objc_autorelease(NSBundleCreateObject(cls, bundle));
}

id NSBundleWithURL(Class cls, SEL, id url)
{
	auto cf_url = NSBundleCFURLForNSURL(url);
	if (cf_url == nullptr) return nullptr;
	auto bundle = darling_windows_CFBundleCreate(nullptr, cf_url);
	darling_windows_CFRelease(cf_url);
	return objc_autorelease(NSBundleCreateObject(cls, bundle));
}

id NSBundleWithIdentifier(Class cls, SEL, id identifier)
{
	auto cf_identifier = NSStringToCFString(identifier);
	if (cf_identifier == nullptr) return nullptr;
	auto bundle = darling_windows_CFBundleGetBundleWithIdentifier(cf_identifier);
	if (bundle != nullptr) darling_windows_CFRetain(bundle);
	darling_windows_CFRelease(cf_identifier);
	return objc_autorelease(NSBundleCreateObject(cls, bundle));
}

id NSBundleInitWithPath(id self, SEL, id path)
{
	auto url = NSBundleURLForPath(path);
	if (url == nullptr) return nullptr;
	auto bundle = darling_windows_CFBundleCreate(nullptr, url);
	darling_windows_CFRelease(url);
	return NSBundleAttach(self, bundle) ? self : nullptr;
}

id NSBundleInitWithURL(id self, SEL, id url)
{
	auto cf_url = NSBundleCFURLForNSURL(url);
	if (cf_url == nullptr) return nullptr;
	auto bundle = darling_windows_CFBundleCreate(nullptr, cf_url);
	darling_windows_CFRelease(cf_url);
	return NSBundleAttach(self, bundle) ? self : nullptr;
}

id NSBundleBundlePath(id self, SEL)
{
	auto bundle = NSBundleGetCFBundle(self);
	auto url = darling_windows_CFBundleCopyBundleURL(bundle);
	auto path = darling_windows_CFURLCopyFileSystemPath(url);
	id result = NSStringFromCFString(path);
	darling_windows_CFRelease(path);
	darling_windows_CFRelease(url);
	return result;
}

id NSBundleURLObject(darling_windows_CFURLRef url)
{
	auto path = darling_windows_CFURLCopyFileSystemPath(url);
	id path_string = NSStringFromCFString(path);
	id result = path_string == nullptr ? nullptr : NSURLCreateFileURL(
		objc_getClass("NSURL"), path_string);
	darling_windows_CFRelease(path);
	darling_windows_CFRelease(url);
	return result;
}

id NSBundleBundleURL(id self, SEL)
{
	return NSBundleURLObject(darling_windows_CFBundleCopyBundleURL(
		NSBundleGetCFBundle(self)));
}

id NSBundleExecutableURL(id self, SEL)
{
	return NSBundleURLObject(darling_windows_CFBundleCopyExecutableURL(
		NSBundleGetCFBundle(self)));
}

id NSBundleResourceURL(id self, SEL)
{
	return NSBundleURLObject(darling_windows_CFBundleCopyResourcesDirectoryURL(
		NSBundleGetCFBundle(self)));
}

bool ReadBundleExecutableArchitectures(const std::filesystem::path& path,
	std::vector<std::int32_t>& architectures)
{
	using darling::windows_host::FAT_MAGIC_64_BE;
	using darling::windows_host::FAT_MAGIC_BE;
	using darling::windows_host::MH_MAGIC;
	using darling::windows_host::MH_MAGIC_64;
	using darling::windows_host::MachHeader32;
	std::ifstream input(path, std::ios::binary);
	if (!input) return false;
	std::error_code error;
	const auto file_bytes = std::filesystem::file_size(path, error);
	if (error || file_bytes < sizeof(MachHeader32) ||
		file_bytes > static_cast<std::uintmax_t>(
			(std::numeric_limits<std::streamoff>::max)())) return false;
	auto read_at = [&input](std::uint64_t offset, void* output,
		std::size_t bytes) {
		input.clear();
		input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		return input.good() && input.read(static_cast<char*>(output),
			static_cast<std::streamsize>(bytes)).good();
	};
	auto big_endian_32 = [](const std::uint8_t* bytes) {
		return (static_cast<std::uint32_t>(bytes[0]) << 24) |
			(static_cast<std::uint32_t>(bytes[1]) << 16) |
			(static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
	};
	auto big_endian_64 = [&big_endian_32](const std::uint8_t* bytes) {
		return (static_cast<std::uint64_t>(big_endian_32(bytes)) << 32) |
			big_endian_32(bytes + 4);
	};
	std::uint32_t raw_magic = 0;
	if (!read_at(0, &raw_magic, sizeof(raw_magic))) return false;
	if (raw_magic != FAT_MAGIC_BE && raw_magic != FAT_MAGIC_64_BE) {
		MachHeader32 header{};
		if (!read_at(0, &header, sizeof(header)) ||
			(header.magic != MH_MAGIC && header.magic != MH_MAGIC_64)) return false;
		architectures.push_back(header.cpu_type);
		return true;
	}
	std::array<std::uint8_t, 8> fat_header{};
	if (!read_at(0, fat_header.data(), fat_header.size())) return false;
	const std::uint32_t count = big_endian_32(fat_header.data() + 4);
	if (count == 0 || count > 128) return false;
	const std::size_t architecture_bytes = raw_magic == FAT_MAGIC_64_BE ? 32 : 20;
	const std::uint64_t table_bytes = static_cast<std::uint64_t>(count) * architecture_bytes;
	if (table_bytes > file_bytes - fat_header.size()) return false;
	std::vector<std::uint8_t> table(static_cast<std::size_t>(table_bytes));
	if (!read_at(fat_header.size(), table.data(), table.size())) return false;
	for (std::uint32_t index = 0; index < count; ++index) {
		const auto* record = table.data() +
			static_cast<std::size_t>(index) * architecture_bytes;
		const auto cpu_type = static_cast<std::int32_t>(big_endian_32(record));
		const std::uint64_t offset = raw_magic == FAT_MAGIC_64_BE ?
			big_endian_64(record + 8) : big_endian_32(record + 8);
		const std::uint64_t slice_bytes = raw_magic == FAT_MAGIC_64_BE ?
			big_endian_64(record + 16) : big_endian_32(record + 12);
		if (offset > file_bytes || slice_bytes > file_bytes - offset ||
			slice_bytes < sizeof(MachHeader32)) return false;
		MachHeader32 slice_header{};
		if (!read_at(offset, &slice_header, sizeof(slice_header)) ||
			(slice_header.magic != MH_MAGIC && slice_header.magic != MH_MAGIC_64) ||
			slice_header.cpu_type != cpu_type) return false;
		if (std::find(architectures.begin(), architectures.end(), cpu_type) ==
			architectures.end()) architectures.push_back(cpu_type);
	}
	return !architectures.empty();
}

bool NSBundleReadExecutableArchitectureIds(id self,
	std::vector<std::int32_t>& architectures)
{
	const auto url = darling_windows_CFBundleCopyExecutableURL(
		NSBundleGetCFBundle(self));
	if (url == nullptr) return false;
	std::array<unsigned char, 32768> path_bytes{};
	const bool has_path = darling_windows_CFURLGetFileSystemRepresentation(
		url, true, path_bytes.data(), static_cast<darling_windows_CFIndex>(path_bytes.size()));
	darling_windows_CFRelease(url);
	if (!has_path) return false;
	try {
		const auto* utf8_path = reinterpret_cast<const char8_t*>(path_bytes.data());
		const std::u8string encoded_path(utf8_path, std::strlen(
			reinterpret_cast<const char*>(path_bytes.data())));
		return ReadBundleExecutableArchitectures(std::filesystem::path(encoded_path),
			architectures);
	} catch (...) { return false; }
}

std::int32_t CurrentHostMachOCPUType()
{
#if defined(_M_ARM64) || defined(__aarch64__)
	return darling::windows_host::CPU_TYPE_ARM64;
#elif defined(_WIN64)
	return darling::windows_host::CPU_TYPE_X86_64;
#else
	return darling::windows_host::CPU_TYPE_X86;
#endif
}

id NSBundleExecutableArchitectures(id self, SEL)
{
	try {
		std::vector<std::int32_t> architectures;
		if (!NSBundleReadExecutableArchitectureIds(self, architectures))
			return nullptr;
		std::vector<id> values;
		values.reserve(architectures.size());
		for (const auto architecture : architectures) {
			id number = CreateIntegerNSNumber(objc_getClass("NSNumber"), architecture);
			if (number == nullptr) {
				for (id value : values) objc_release(value);
				return nullptr;
			}
			values.push_back(number);
		}
		id result = CreateNSArray(objc_getClass("NSArray"), values.data(),
			static_cast<std::uint64_t>(values.size()));
		for (id value : values) objc_release(value);
		return objc_autorelease(result);
	} catch (...) { return nullptr; }
}

id NSBundleExecutablePath(id self, SEL)
{
	auto bundle = NSBundleGetCFBundle(self);
	auto url = darling_windows_CFBundleCopyExecutableURL(bundle);
	auto path = darling_windows_CFURLCopyFileSystemPath(url);
	id result = NSStringFromCFString(path);
	darling_windows_CFRelease(path);
	darling_windows_CFRelease(url);
	return result;
}

id NSBundleResourcePath(id self, SEL)
{
	auto bundle = NSBundleGetCFBundle(self);
	auto url = darling_windows_CFBundleCopyResourcesDirectoryURL(bundle);
	auto path = darling_windows_CFURLCopyFileSystemPath(url);
	id result = NSStringFromCFString(path);
	darling_windows_CFRelease(path);
	darling_windows_CFRelease(url);
	return result;
}

id NSBundleIdentifier(id self, SEL)
{
	return NSStringFromCFString(darling_windows_CFBundleGetIdentifier(
		NSBundleGetCFBundle(self)));
}

id NSBundleObjectForInfoDictionaryKey(id self, SEL, id key)
{
	auto cf_key = NSStringToCFString(key);
	if (cf_key == nullptr) return nullptr;
	auto value = darling_windows_CFBundleGetValueForInfoDictionaryKey(
		NSBundleGetCFBundle(self), cf_key);
	id result = objc_autorelease(ObjcObjectFromCF(value));
	darling_windows_CFRelease(cf_key);
	return result;
}

Class NSBundleClassNamed(id self, SEL, id name)
{
	if (self == nullptr || !NSBundleLoad(self, nullptr)) return nullptr;
	const char* class_name = NSStringUTF8String(name, nullptr);
	return class_name == nullptr || class_name[0] == '\0' ? nullptr :
		objc_getClass(class_name);
}

Class NSBundlePrincipalClass(id self, SEL)
{
	if (self == nullptr || !NSBundleLoad(self, nullptr)) return nullptr;
	id key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSPrincipalClass"));
	if (key == nullptr) return nullptr;
	return NSBundleClassNamed(self, nullptr,
		NSBundleObjectForInfoDictionaryKey(self, nullptr, key));
}

id NSBundleInfoDictionary(id self, SEL)
{
	return objc_autorelease(ObjcObjectFromCF(
		darling_windows_CFBundleGetInfoDictionary(NSBundleGetCFBundle(self))));
}

id NSBundlePathForResourceType(id self, SEL, id resource_name, id resource_type)
{
	auto name = NSStringToCFString(resource_name);
	auto type = NSStringToCFString(resource_type);
	auto url = darling_windows_CFBundleCopyResourceURL(
		NSBundleGetCFBundle(self), name, type, nullptr);
	auto path = darling_windows_CFURLCopyFileSystemPath(url);
	id result = NSStringFromCFString(path);
	darling_windows_CFRelease(path);
	darling_windows_CFRelease(url);
	darling_windows_CFRelease(name);
	darling_windows_CFRelease(type);
	return result;
}

id NSBundlePathForResourceTypeInDirectory(id self, SEL, id resource_name,
	id resource_type, id directory)
{
	auto name = NSStringToCFString(resource_name);
	auto type = NSStringToCFString(resource_type);
	auto subdirectory = NSStringToCFString(directory);
	auto url = darling_windows_CFBundleCopyResourceURL(
		NSBundleGetCFBundle(self), name, type, subdirectory);
	auto path = darling_windows_CFURLCopyFileSystemPath(url);
	id result = NSStringFromCFString(path);
	darling_windows_CFRelease(path);
	darling_windows_CFRelease(url);
	darling_windows_CFRelease(name);
	darling_windows_CFRelease(type);
	darling_windows_CFRelease(subdirectory);
	return result;
}

id NSBundleURLForResource(id self, SEL, id resource_name, id resource_type,
	id subdirectory)
{
	auto name = NSStringToCFString(resource_name);
	auto type = NSStringToCFString(resource_type);
	auto directory = NSStringToCFString(subdirectory);
	auto url = darling_windows_CFBundleCopyResourceURL(
		NSBundleGetCFBundle(self), name, type, directory);
	darling_windows_CFRelease(name);
	darling_windows_CFRelease(type);
	darling_windows_CFRelease(directory);
	return NSBundleURLObject(url);
}

void NSImageFinalize(id object)
{
	darling_windows_CGImageRef image = nullptr;
	std::vector<id> representations;
	{
		std::lock_guard lock(g_images_mutex);
		const auto found = g_images.find(object);
		if (found == g_images.end()) return;
		image = found->second.image;
		representations = std::move(found->second.representations);
		g_images.erase(found);
	}
	if (image != nullptr) CGImageRelease(image);
	for (id representation : representations)
		if (representation != nullptr) objc_release(representation);
}

bool NSImageAttach(id object, darling_windows_CGImageRef image,
	const std::vector<id>* representations = nullptr)
{
	if (object == nullptr || image == nullptr) return false;
	const DarlingObjcPoint size{
		static_cast<double>(CGImageGetWidth(image)),
		static_cast<double>(CGImageGetHeight(image))};
	std::vector<id> retained_representations;
	try {
		if (representations != nullptr) {
			retained_representations.reserve(representations->size());
			for (id representation : *representations) {
				if (representation == nullptr) continue;
				id retained = objc_retain(representation);
				try { retained_representations.push_back(retained); }
				catch (...) { objc_release(retained); throw; }
			}
		}
		bool inserted = false;
		{
			std::lock_guard lock(g_images_mutex);
			auto result = g_images.emplace(object,
				AppKitImageState{image, size, {}});
			inserted = result.second;
			if (inserted)
				result.first->second.representations =
					std::move(retained_representations);
		}
		if (!inserted) {
			for (id representation : retained_representations)
				if (representation != nullptr) objc_release(representation);
			return false;
		}
	} catch (...) {
		for (id representation : retained_representations)
			if (representation != nullptr) objc_release(representation);
		return false;
	}
	if (darling_objc_register_object_finalizer(object, &NSImageFinalize))
		return true;
	std::vector<id> failed_representations;
	{
		std::lock_guard lock(g_images_mutex);
		const auto found = g_images.find(object);
		if (found != g_images.end()) {
			failed_representations = std::move(found->second.representations);
			g_images.erase(found);
		}
	}
	for (id representation : failed_representations)
		if (representation != nullptr) objc_release(representation);
	return false;
}

darling_windows_CGImageRef NSImageCreateCGImageFromData(id data)
{
	std::vector<std::uint8_t> bytes;
	try {
		std::lock_guard lock(g_data_mutex);
		const auto found = g_data.find(data);
		if (found != g_data.end()) bytes = found->second;
	} catch (...) { return nullptr; }
	if (bytes.empty()) return nullptr;
	auto cf_data = CFDataCreate(nullptr, bytes.data(),
		static_cast<darling_windows_CFIndex>(bytes.size()));
	auto source = cf_data == nullptr ? nullptr :
		CGImageSourceCreateWithData(cf_data, nullptr);
	auto image = source == nullptr ? nullptr :
		CGImageSourceCreateImageAtIndex(source, 0, nullptr);
	if (source != nullptr) CFRelease(source);
	if (cf_data != nullptr) CFRelease(cf_data);
	return image;
}

darling_windows_CGImageRef NSImageCreateCGImageFromICNSFile(const char* path)
{
	if (path == nullptr || *path == '\0') return nullptr;
	try {
		std::filesystem::path file_path(Utf8ToWide(path));
		std::ifstream stream(file_path, std::ios::binary | std::ios::ate);
		if (!stream) return nullptr;
		const auto end = stream.tellg();
		if (end < 8 || static_cast<std::uint64_t>(end) >
			128ull * 1024 * 1024) return nullptr;
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
		stream.seekg(0, std::ios::beg);
		if (!stream.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()))) return nullptr;
		const auto read_be32 = [&bytes](std::size_t offset) -> std::uint32_t {
			return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
				(static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
				(static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
				static_cast<std::uint32_t>(bytes[offset + 3]);
		};
		if (std::memcmp(bytes.data(), "icns", 4) != 0) return nullptr;
		const auto declared_size = static_cast<std::size_t>(read_be32(4));
		if (declared_size < 8 || declared_size > bytes.size()) return nullptr;
		constexpr std::uint8_t png_signature[]{
			0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
		darling_windows_CGImageRef best = nullptr;
		std::uint64_t best_area = 0;
		std::size_t offset = 8;
		for (; offset + 8 <= declared_size;) {
			const auto chunk_size = static_cast<std::size_t>(read_be32(offset + 4));
			if (chunk_size < 8 || chunk_size > declared_size - offset) {
				if (best != nullptr) CGImageRelease(best);
				return nullptr;
			}
			const auto payload = offset + 8;
			const auto payload_size = chunk_size - 8;
			if (payload_size >= sizeof(png_signature) &&
				std::memcmp(bytes.data() + payload, png_signature,
					sizeof(png_signature)) == 0) {
				auto data = CFDataCreate(nullptr, bytes.data() + payload,
					static_cast<darling_windows_CFIndex>(payload_size));
				auto source = data == nullptr ? nullptr :
					CGImageSourceCreateWithData(data, nullptr);
				auto image = source == nullptr ? nullptr :
					CGImageSourceCreateImageAtIndex(source, 0, nullptr);
				if (source != nullptr) CFRelease(source);
				if (data != nullptr) CFRelease(data);
				if (image != nullptr) {
					const auto area = static_cast<std::uint64_t>(
						CGImageGetWidth(image)) * CGImageGetHeight(image);
					if (area > best_area) {
						if (best != nullptr) CGImageRelease(best);
						best = image;
						best_area = area;
					} else CGImageRelease(image);
				}
			}
			offset += chunk_size;
		}
		if (offset != declared_size) {
			if (best != nullptr) CGImageRelease(best);
			return nullptr;
		}
		return best;
	} catch (...) { return nullptr; }
}

id NSImageInitWithContentsOfFile(id self, SEL, id path)
{
	const char* utf8_path = path == nullptr ? nullptr : NSStringUTF8String(path, nullptr);
	if (self == nullptr || utf8_path == nullptr || *utf8_path == '\0') {
		if (self != nullptr) objc_release(self);
		return nullptr;
	}
	auto url = darling_windows_CFURLCreateWithFileSystemPath(utf8_path);
	auto source = url == nullptr ? nullptr : CGImageSourceCreateWithURL(url, nullptr);
	auto image = source == nullptr ? nullptr :
		CGImageSourceCreateImageAtIndex(source, 0, nullptr);
	if (source != nullptr) CFRelease(source);
	if (url != nullptr) CFRelease(url);
	if (image == nullptr) image = NSImageCreateCGImageFromICNSFile(utf8_path);
	if (image == nullptr || !NSImageAttach(self, image)) {
		if (image != nullptr) CGImageRelease(image);
		objc_release(self);
		return nullptr;
	}
	return self;
}

id NSImageInitWithData(id self, SEL, id data)
{
	std::vector<std::uint8_t> bytes;
	try {
		std::lock_guard lock(g_data_mutex);
		const auto found = g_data.find(data);
		if (found != g_data.end()) bytes = found->second;
	} catch (...) {}
	if (self == nullptr || bytes.empty()) {
		if (self != nullptr) objc_release(self);
		return nullptr;
	}
	auto cf_data = CFDataCreate(nullptr, bytes.data(),
		static_cast<darling_windows_CFIndex>(bytes.size()));
	auto source = cf_data == nullptr ? nullptr :
		CGImageSourceCreateWithData(cf_data, nullptr);
	auto image = source == nullptr ? nullptr :
		CGImageSourceCreateImageAtIndex(source, 0, nullptr);
	if (source != nullptr) CFRelease(source);
	if (cf_data != nullptr) CFRelease(cf_data);
	if (image == nullptr || !NSImageAttach(self, image)) {
		if (image != nullptr) CGImageRelease(image);
		objc_release(self);
		return nullptr;
	}
	return self;
}

id NSBitmapImageRepInitWithData(id self, SEL, id data)
{
	if (self == nullptr) return nullptr;
	auto image = NSImageCreateCGImageFromData(data);
	if (image == nullptr || !NSImageAttach(self, image)) {
		if (image != nullptr) CGImageRelease(image);
		objc_release(self);
		return nullptr;
	}
	return self;
}

id NSBitmapImageRepInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr) return nullptr;
	for (const char* field : {"NSData", "NSBitmapData", "NSImageData"}) {
		id key = CreateDynamicNSString(objc_getClass("NSString"), field);
		if (key == nullptr) continue;
		id data = darling_objc_msgSend_object1(coder,
			sel_registerName("decodeObjectForKey:"), key);
		objc_release(key);
		if (data == nullptr) continue;
		auto image = NSImageCreateCGImageFromData(data);
		objc_release(data);
		if (image == nullptr) return nullptr;
		if (!NSImageAttach(self, image)) {
			CGImageRelease(image);
			return nullptr;
		}
		return self;
	}
	return nullptr;
}

std::uint64_t NSBitmapImageRepPixelsWide(id self, SEL)
{
	std::lock_guard lock(g_images_mutex);
	const auto found = g_images.find(self);
	return found == g_images.end() ? 0 :
		static_cast<std::uint64_t>(CGImageGetWidth(found->second.image));
}

std::uint64_t NSBitmapImageRepPixelsHigh(id self, SEL)
{
	std::lock_guard lock(g_images_mutex);
	const auto found = g_images.find(self);
	return found == g_images.end() ? 0 :
		static_cast<std::uint64_t>(CGImageGetHeight(found->second.image));
}

DarlingObjcPoint NSBitmapImageRepSize(id self, SEL)
{
	std::lock_guard lock(g_images_mutex);
	const auto found = g_images.find(self);
	return found == g_images.end() ? DarlingObjcPoint{} : found->second.size;
}

void NSBitmapImageRepSetSize(id self, SEL, DarlingObjcPoint size)
{
	if (!std::isfinite(size.x) || !std::isfinite(size.y) ||
		size.x <= 0.0 || size.y <= 0.0) return;
	std::lock_guard lock(g_images_mutex);
	const auto found = g_images.find(self);
	if (found != g_images.end()) found->second.size = size;
}

darling_windows_CGImageRef NSBitmapImageRepCGImage(id self, SEL)
{
	std::lock_guard lock(g_images_mutex);
	const auto found = g_images.find(self);
	return found == g_images.end() ? nullptr : found->second.image;
}

void NSBitmapImageRepDraw(id self, SEL)
{
	if (g_current_cg_context == nullptr) return;
	darling_windows_CGImageRef image = nullptr;
	DarlingObjcPoint size{};
	{
		std::lock_guard lock(g_images_mutex);
		const auto found = g_images.find(self);
		if (found == g_images.end()) return;
		image = found->second.image;
		size = found->second.size;
		CGImageRetain(image);
	}
	CGContextDrawImage(g_current_cg_context,
		darling_windows_CGRect{{0.0, 0.0}, {size.x, size.y}}, image);
	CGImageRelease(image);
}

id NSImageRepresentations(id self, SEL)
{
	std::vector<id> representations;
	darling_windows_CGImageRef image = nullptr;
	DarlingObjcPoint image_size{};
	try {
		std::lock_guard lock(g_images_mutex);
		const auto found = g_images.find(self);
		if (found != g_images.end()) {
			representations.reserve(std::max<std::size_t>(1,
				found->second.representations.size()));
			for (id representation : found->second.representations) {
				id retained = objc_retain(representation);
				try { representations.push_back(retained); }
				catch (...) { objc_release(retained); throw; }
			}
			if (representations.empty()) {
				image = found->second.image;
				image_size = found->second.size;
				CGImageRetain(image);
			}
		}
	} catch (...) {
		if (image != nullptr) CGImageRelease(image);
		for (id representation : representations) objc_release(representation);
		return nullptr;
	}
	if (image != nullptr) {
		Class bitmap_class = objc_getClass("NSBitmapImageRep");
		id bitmap = bitmap_class == nullptr ? nullptr :
			class_createInstance(bitmap_class, 0);
		if (bitmap != nullptr && NSImageAttach(bitmap, image)) {
			NSBitmapImageRepSetSize(bitmap, nullptr, image_size);
			bool stored = false;
			{
				std::lock_guard lock(g_images_mutex);
				const auto found = g_images.find(self);
				if (found != g_images.end() && found->second.image != nullptr &&
					found->second.representations.empty()) {
					id retained = objc_retain(bitmap);
					try {
						found->second.representations.push_back(retained);
						stored = true;
					} catch (...) { objc_release(retained); }
				}
			}
			if (stored) representations.push_back(bitmap);
			else objc_release(bitmap);
		} else {
			if (bitmap != nullptr) objc_release(bitmap);
			CGImageRelease(image);
		}
	}
	id result = CreateNSArray(objc_getClass("NSArray"), representations.data(),
		representations.size());
	for (id representation : representations) objc_release(representation);
	return objc_autorelease(result);
}

id NSImageInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr) return nullptr;
	for (const char* field : {"NSReps", "NSImageReps"}) {
		id key = CreateDynamicNSString(objc_getClass("NSString"), field);
		if (key == nullptr) continue;
		id representations = darling_objc_msgSend_object1(coder,
			sel_registerName("decodeObjectForKey:"), key);
		objc_release(key);
		if (representations == nullptr) continue;
		std::vector<id> reps;
		try {
			std::lock_guard lock(g_array_mutex);
			const auto found = g_arrays.find(representations);
			if (found != g_arrays.end()) {
				reps.reserve(found->second.size());
				for (id rep : found->second) {
					if (rep == nullptr) continue;
					id retained = objc_retain(rep);
					try { reps.push_back(retained); }
					catch (...) { objc_release(retained); throw; }
				}
			}
		} catch (...) {
			for (id rep : reps) objc_release(rep);
			reps.clear();
		}
		objc_release(representations);
		darling_windows_CGImageRef image = nullptr;
		for (id rep : reps) {
			std::lock_guard lock(g_images_mutex);
			const auto found = g_images.find(rep);
			if (found != g_images.end()) {
				image = found->second.image;
				CGImageRetain(image);
				break;
			}
		}
		if (image == nullptr) {
			for (id rep : reps) objc_release(rep);
			continue;
		}
	if (!NSImageAttach(self, image, &reps)) {
			CGImageRelease(image);
			for (id rep : reps) objc_release(rep);
			return nullptr;
		}
		for (id rep : reps) objc_release(rep);
		return self;
	}
	// AppKit archives use several historical keys for the bitmap payload. Decode
	// only NSData-backed representations; vector and cached display reps need
	// their own rendering implementations.
	for (const char* field : {"NSImageData", "NSData", "NSBitmapData",
		"NSImageRepData"}) {
		id key = CreateDynamicNSString(objc_getClass("NSString"), field);
		if (key == nullptr) continue;
		id data = darling_objc_msgSend_object1(coder,
			sel_registerName("decodeObjectForKey:"), key);
		objc_release(key);
		if (data == nullptr) continue;
		bool has_bytes = false;
		{
			std::lock_guard lock(g_data_mutex);
			const auto found = g_data.find(data);
			has_bytes = found != g_data.end() && !found->second.empty();
		}
		if (has_bytes) {
			std::vector<std::uint8_t> bytes;
			try {
				std::lock_guard lock(g_data_mutex);
				const auto found = g_data.find(data);
				if (found != g_data.end()) bytes = found->second;
			} catch (...) {}
			objc_release(data);
			if (bytes.empty()) return nullptr;
			auto cf_data = CFDataCreate(nullptr, bytes.data(),
				static_cast<darling_windows_CFIndex>(bytes.size()));
			auto source = cf_data == nullptr ? nullptr :
				CGImageSourceCreateWithData(cf_data, nullptr);
			auto image = source == nullptr ? nullptr :
				CGImageSourceCreateImageAtIndex(source, 0, nullptr);
			if (source != nullptr) CFRelease(source);
			if (cf_data != nullptr) CFRelease(cf_data);
			if (image == nullptr || !NSImageAttach(self, image)) {
				if (image != nullptr) CGImageRelease(image);
				return nullptr;
			}
			id result = self;
			// Preserve the archived logical size when available; otherwise size is
			// the decoded pixel extent established by NSImageAttach.
			id size_key = CreateDynamicNSString(objc_getClass("NSString"), "NSSize");
			id encoded_size = size_key == nullptr ? nullptr :
				darling_objc_msgSend_object1(coder,
					sel_registerName("decodeObjectForKey:"), size_key);
			if (size_key != nullptr) objc_release(size_key);
			const char* size_text = NSStringUTF8String(encoded_size, nullptr);
			double width = 0.0;
			double height = 0.0;
			if (ParseKeyedSizeString(size_text, width, height) &&
				std::isfinite(width) && std::isfinite(height) &&
				width > 0.0 && height > 0.0) {
				std::lock_guard lock(g_images_mutex);
				const auto found = g_images.find(result);
				if (found != g_images.end())
					found->second.size = DarlingObjcPoint{width, height};
			}
			if (encoded_size != nullptr) objc_release(encoded_size);
			return result;
		}
		objc_release(data);
	}
	return nullptr;
}

id NSImageWithContentsOfFile(Class cls, SEL, id path)
{
	if (cls == nullptr) return nullptr;
	id image = class_createInstance(cls, 0);
	if (image == nullptr) return nullptr;
	id initialized = NSImageInitWithContentsOfFile(image, nullptr, path);
	return objc_autorelease(initialized);
}

id NSImageNamed(Class cls, SEL, id name)
{
	const char* name_utf8 = name == nullptr ? nullptr : NSStringUTF8String(name, nullptr);
	if (cls == nullptr || name_utf8 == nullptr || *name_utf8 == '\0') return nullptr;
	std::string resource_name(name_utf8);
	std::string supplied_extension;
	const auto dot = resource_name.find_last_of('.');
	const auto slash = resource_name.find_last_of("/\\");
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		supplied_extension = resource_name.substr(dot + 1);
		resource_name.resize(dot);
	}
	const std::array<const char*, 8> extensions{
		"png", "jpg", "jpeg", "tif", "tiff", "bmp", "gif", "icns"};
	id bundle = NSBundleMainBundle(objc_getClass("NSBundle"), nullptr);
	if (bundle == nullptr) return nullptr;
	for (const char* extension : extensions) {
		if (!supplied_extension.empty() && _stricmp(
			supplied_extension.c_str(), extension) != 0) continue;
		id resource = CreateDynamicNSString(objc_getClass("NSString"),
			resource_name.c_str());
		id type = CreateDynamicNSString(objc_getClass("NSString"), extension);
		if (resource == nullptr || type == nullptr) {
			if (resource != nullptr) objc_release(resource);
			if (type != nullptr) objc_release(type);
			break;
		}
		id path = NSBundlePathForResourceType(bundle, nullptr, resource, type);
		objc_release(resource);
		objc_release(type);
		if (path != nullptr) {
			id image = NSImageWithContentsOfFile(cls, nullptr, path);
			objc_release(path);
			if (image != nullptr) return image;
		}
	}
	return nullptr;
}

DarlingObjcPoint NSImageSize(id self, SEL)
{
	std::lock_guard lock(g_images_mutex);
	const auto found = g_images.find(self);
	return found == g_images.end() ? DarlingObjcPoint{} : found->second.size;
}

bool NSImageIsValid(id self, SEL)
{
	std::lock_guard lock(g_images_mutex);
	return g_images.find(self) != g_images.end();
}

void NSImageDrawInRect(id self, SEL, DarlingObjcRect rect)
{
	if (g_current_cg_context == nullptr || !std::isfinite(rect.x) ||
		!std::isfinite(rect.y) || !std::isfinite(rect.width) ||
		!std::isfinite(rect.height) || rect.width <= 0.0 || rect.height <= 0.0)
		return;
	darling_windows_CGImageRef image = nullptr;
	{
		std::lock_guard lock(g_images_mutex);
		const auto found = g_images.find(self);
		if (found != g_images.end()) {
			image = found->second.image;
			CGImageRetain(image);
		}
	}
	if (image == nullptr) return;
	CGContextDrawImage(g_current_cg_context,
		darling_windows_CGRect{{rect.x, rect.y}, {rect.width, rect.height}}, image);
	CGImageRelease(image);
}

void NSImageDrawAtPoint(id self, SEL, DarlingObjcPoint point)
{
	if (!std::isfinite(point.x) || !std::isfinite(point.y)) return;
	const auto size = NSImageSize(self, nullptr);
	NSImageDrawInRect(self, nullptr,
		DarlingObjcRect{point.x, point.y, size.x, size.y});
}

id NSImageViewImage(id self, SEL)
{
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.image == nullptr ? nullptr :
		objc_autorelease(objc_retain(found->second.image));
}

void NSImageViewSetImage(id self, SEL, id image)
{
	if (image != nullptr) objc_retain(image);
	id previous = nullptr;
	id window = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) {
			if (image != nullptr) objc_release(image);
			return;
		}
		previous = std::exchange(found->second.image, image);
		window = found->second.window;
	}
	if (previous != nullptr) objc_release(previous);
	if (window != nullptr) InvalidateAppKitWindow(window);
}

id NSImageViewInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr ||
		NSViewInitWithCoder(self, nullptr, coder) == nullptr) return nullptr;
	for (const char* field : {"NSImage", "NSImageObject", "NSContents"}) {
		id key = CreateDynamicNSString(objc_getClass("NSString"), field);
		if (key == nullptr) continue;
		id image = darling_objc_msgSend_object1(coder,
			sel_registerName("decodeObjectForKey:"), key);
		objc_release(key);
		if (image == nullptr) continue;
		NSImageViewSetImage(self, nullptr, image);
		objc_release(image);
		break;
	}
	return self;
}

void NSImageViewDrawRect(id self, SEL, DarlingObjcRect)
{
	if (g_current_cg_context == nullptr) return;
	id image_object = nullptr;
	DarlingObjcRect bounds{};
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		image_object = found->second.image == nullptr ? nullptr :
			objc_retain(found->second.image);
		bounds = found->second.bounds;
	}
	if (image_object == nullptr) return;
	darling_windows_CGImageRef image = nullptr;
	{
		std::lock_guard lock(g_images_mutex);
		const auto found = g_images.find(image_object);
		if (found != g_images.end()) {
			image = found->second.image;
			CGImageRetain(image);
		}
	}
	if (image != nullptr) {
		CGContextDrawImage(g_current_cg_context,
			darling_windows_CGRect{{bounds.x, bounds.y},
				{bounds.width, bounds.height}}, image);
		CGImageRelease(image);
	}
	objc_release(image_object);
}

std::unordered_set<std::string> NSBundleRegisteredClassNames()
{
	std::unordered_set<std::string> names;
	int count = objc_getClassList(nullptr, 0);
	for (int attempt = 0; attempt < 4 && count > 0 && count <= 1'000'000;
		++attempt) {
		std::vector<Class> classes(static_cast<std::size_t>(count));
		const int actual = objc_getClassList(classes.data(), count);
		if (actual > count) { count = actual; continue; }
		for (int index = 0; index < actual; ++index) {
			const char* name = class_getName(classes[static_cast<std::size_t>(index)]);
			if (name != nullptr) names.emplace(name);
		}
		break;
	}
	return names;
}

void NSBundlePostDidLoad(id bundle, const std::unordered_set<std::string>& prior_classes)
{
	if (bundle == nullptr || NSBundleDidLoadNotification == nullptr ||
		NSLoadedClasses == nullptr) return;
	try {
		const int count = objc_getClassList(nullptr, 0);
		if (count < 0 || count > 1'000'000) return;
		std::vector<Class> classes(static_cast<std::size_t>(count));
		const int actual = objc_getClassList(classes.data(), count);
		if (actual < 0 || actual > count) return;
		std::vector<id> names;
		names.reserve(static_cast<std::size_t>(actual));
		for (int index = 0; index < actual; ++index) {
			const char* name = class_getName(classes[static_cast<std::size_t>(index)]);
			if (name == nullptr || prior_classes.contains(name)) continue;
			id value = CreateDynamicNSString(objc_getClass("NSString"), name);
			if (value == nullptr) continue;
			names.push_back(value);
		}
		id loaded_classes = CreateNSArray(objc_getClass("NSArray"), names.data(),
			static_cast<std::uint64_t>(names.size()));
		for (id name : names) objc_release(name);
		if (loaded_classes == nullptr) return;
		const id values[]{loaded_classes};
		const id keys[]{NSLoadedClasses};
		id user_info = CreateNSDictionary(objc_getClass("NSDictionary"), values,
			keys, 1);
		objc_release(loaded_classes);
		if (user_info == nullptr) return;
		id notification = NSNotificationFactoryWithUserInfo(
			objc_getClass("NSNotification"), nullptr,
			NSBundleDidLoadNotification, bundle, user_info);
		objc_release(user_info);
		if (notification != nullptr) NSNotificationCenterDispatch(notification);
	} catch (...) { }
}

bool NSBundleLoad(id self, SEL)
{
	try {
		const auto bundle = NSBundleGetCFBundle(self);
		if (self == nullptr || bundle == nullptr) return false;
		const bool was_loaded = CFBundleIsExecutableLoaded(bundle);
		auto prior_classes = was_loaded ? std::unordered_set<std::string>{} :
			NSBundleRegisteredClassNames();
		if (!CFBundleLoadExecutable(bundle)) return false;
		if (!was_loaded) NSBundlePostDidLoad(self, prior_classes);
		return true;
	} catch (...) { return false; }
}

bool NSBundleIsLoaded(id self, SEL)
{
	return CFBundleIsExecutableLoaded(NSBundleGetCFBundle(self));
}

void NSBundleSetLoadError(id* error_output, std::int64_t code,
	const char* message)
{
	if (error_output == nullptr) return;
	*error_output = nullptr;
	if (NSCocoaErrorDomain == nullptr) return;
	id description = CreateDynamicNSString(objc_getClass("NSString"),
		message);
	if (description == nullptr) return;
	*error_output = NSErrorCreate(objc_getClass("NSError"),
		NSCocoaErrorDomain, code, nullptr, description);
	objc_release(description);
}

bool NSBundlePreflightAndReturnError(id self, SEL, id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	const auto bundle = NSBundleGetCFBundle(self);
	if (bundle != nullptr && CFBundleIsExecutableLoaded(bundle)) return true;
	std::vector<std::int32_t> architectures;
	if (bundle == nullptr || !NSBundleReadExecutableArchitectureIds(self, architectures)) {
		NSBundleSetLoadError(error_output, NSExecutableNotLoadableError,
			"The bundle does not contain a readable Mach-O executable.");
		return false;
	}
	if (std::find(architectures.begin(), architectures.end(),
		CurrentHostMachOCPUType()) == architectures.end()) {
		NSBundleSetLoadError(error_output, NSExecutableArchitectureMismatchError,
			"The bundle executable has no slice for this Wintosh host architecture.");
		return false;
	}
	if (CFBundlePreflightExecutable(bundle, nullptr)) return true;
	NSBundleSetLoadError(error_output, NSExecutableLoadError,
		"The bundle executable or one of its dependencies cannot be preflighted.");
	return false;
}

bool NSBundleLoadAndReturnError(id self, SEL, id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	const auto bundle = NSBundleGetCFBundle(self);
	if (bundle != nullptr && CFBundleIsExecutableLoaded(bundle)) return true;
	std::vector<std::int32_t> architectures;
	if (bundle == nullptr || !NSBundleReadExecutableArchitectureIds(self, architectures)) {
		NSBundleSetLoadError(error_output, NSExecutableNotLoadableError,
			"The bundle does not contain a readable Mach-O executable.");
		return false;
	}
	if (std::find(architectures.begin(), architectures.end(),
		CurrentHostMachOCPUType()) == architectures.end()) {
		NSBundleSetLoadError(error_output, NSExecutableArchitectureMismatchError,
			"The bundle executable has no slice for this Wintosh host architecture.");
		return false;
	}
	if (NSBundleLoad(self, nullptr)) return true;
	NSBundleSetLoadError(error_output, NSExecutableLoadError,
		"The bundle executable or one of its dependencies failed to load.");
	return false;
}

void NSBundleUnload(id self, SEL)
{
	CFBundleUnloadExecutable(NSBundleGetCFBundle(self));
}

id NSBundleLocalizedString(id self, SEL, id key, id value, id table)
{
	auto cf_key = NSStringToCFString(key);
	auto cf_value = NSStringToCFString(value);
	auto cf_table = NSStringToCFString(table);
	auto localized = darling_windows_CFBundleCopyLocalizedString(
		NSBundleGetCFBundle(self), cf_key, cf_value, cf_table);
	id result = NSStringFromCFString(localized);
	darling_windows_CFRelease(localized);
	darling_windows_CFRelease(cf_key);
	darling_windows_CFRelease(cf_value);
	darling_windows_CFRelease(cf_table);
	return result;
}

void NSDataFinalize(id object)
{
	std::lock_guard lock(g_data_mutex);
	g_data.erase(object);
}

bool InitializeDataStorage(id object, const void* bytes, std::uint64_t length)
{
	if (object == nullptr || (bytes == nullptr && length != 0) ||
		length > 256ull * 1024 * 1024) return false;
	std::vector<std::uint8_t> data;
	try {
		if (length != 0) {
			const auto* source = static_cast<const std::uint8_t*>(bytes);
			data.assign(source, source + static_cast<std::size_t>(length));
		}
	} catch (...) {
		return false;
	}
	bool needs_finalizer = false;
	try {
		std::lock_guard lock(g_data_mutex);
		auto found = g_data.find(object);
		if (found == g_data.end()) {
			g_data.emplace(object, std::move(data));
			needs_finalizer = true;
		} else {
			found->second = std::move(data);
		}
	} catch (...) {
		return false;
	}
	if (needs_finalizer && !darling_objc_register_object_finalizer(object,
		&NSDataFinalize)) {
		NSDataFinalize(object);
		return false;
	}
	return true;
}

id CreateNSData(Class cls, const void* bytes, std::uint64_t length)
{
	if (cls == nullptr || (bytes == nullptr && length != 0)) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	if (!InitializeDataStorage(object, bytes, length)) {
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSDataWithBytesLength(Class cls, SEL, const void* bytes,
	std::uint64_t length)
{
	return objc_autorelease(CreateNSData(cls, bytes, length));
}

id NSDataInitWithBytesLength(id self, SEL, const void* bytes,
	std::uint64_t length)
{
	return InitializeDataStorage(self, bytes, length) ? self : nullptr;
}

const void* NSDataBytes(id self, SEL)
{
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	return found == g_data.end() || found->second.empty() ? nullptr :
		found->second.data();
}

std::uint64_t NSDataLength(id self, SEL)
{
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	return found == g_data.end() ? 0 : found->second.size();
}

id NSDataSubdataWithRange(id self, SEL, DarlingObjcRange range)
{
	std::vector<std::uint8_t> copy;
	{
		std::lock_guard lock(g_data_mutex);
		auto found = g_data.find(self);
		if (found == g_data.end() || range.location > found->second.size() ||
			range.length > found->second.size() - range.location) return nullptr;
		try {
			const auto first = found->second.begin() +
				static_cast<std::ptrdiff_t>(range.location);
			copy.assign(first, first + static_cast<std::ptrdiff_t>(range.length));
		} catch (...) { return nullptr; }
	}
	return objc_autorelease(CreateNSData(objc_getClass("NSData"),
		copy.data(), copy.size()));
}

void NSDataGetBytesLength(id self, SEL, void* destination,
	std::uint64_t length)
{
	if (destination == nullptr && length != 0) return;
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	if (found == g_data.end() || length > found->second.size()) return;
	if (length != 0) std::memcpy(destination, found->second.data(),
		static_cast<std::size_t>(length));
}

id NSMutableDataData(Class cls, SEL)
{
	return objc_autorelease(CreateNSData(cls, nullptr, 0));
}

id NSMutableDataDataWithLength(Class cls, SEL, std::uint64_t length)
{
	if (length > 256ull * 1024 * 1024) return nullptr;
	id object = CreateNSData(cls, nullptr, 0);
	if (object == nullptr) return nullptr;
	bool resized = true;
	{
		std::lock_guard lock(g_data_mutex);
		try {
			g_data[object].resize(static_cast<std::size_t>(length));
		} catch (...) {
			resized = false;
		}
	}
	if (!resized) { objc_release(object); return nullptr; }
	return objc_autorelease(object);
}

bool IsMutableNSData(id object)
{
	const Class cls = object_getClass(object);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	return name != nullptr && std::strcmp(name, "NSMutableData") == 0;
}

void* NSMutableDataMutableBytes(id self, SEL)
{
	if (!IsMutableNSData(self)) return nullptr;
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	return found == g_data.end() || found->second.empty() ? nullptr :
		found->second.data();
}

void NSMutableDataSetLength(id self, SEL, std::uint64_t length)
{
	if (!IsMutableNSData(self) || length > 256ull * 1024 * 1024) return;
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	if (found == g_data.end()) return;
	try { found->second.resize(static_cast<std::size_t>(length)); }
	catch (...) {}
}

void NSMutableDataAppendBytesLength(id self, SEL, const void* bytes,
	std::uint64_t length)
{
	if (!IsMutableNSData(self) || (bytes == nullptr && length != 0) ||
		length > 256ull * 1024 * 1024) return;
	std::vector<std::uint8_t> copy;
	try {
		if (length != 0) {
			const auto* source = static_cast<const std::uint8_t*>(bytes);
			copy.assign(source, source + static_cast<std::size_t>(length));
		}
	} catch (...) { return; }
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	if (found == g_data.end() || length > 256ull * 1024 * 1024 - found->second.size())
		return;
	try {
		found->second.insert(found->second.end(), copy.begin(), copy.end());
	} catch (...) {}
}

void NSMutableDataReplaceBytesInRange(id self, SEL, DarlingObjcRange range,
	const void* bytes, std::uint64_t length)
{
	if (!IsMutableNSData(self) || (bytes == nullptr && length != 0) ||
		length > 256ull * 1024 * 1024) return;
	std::vector<std::uint8_t> replacement;
	try {
		if (length != 0) {
			const auto* source = static_cast<const std::uint8_t*>(bytes);
			replacement.assign(source, source + static_cast<std::size_t>(length));
		}
	} catch (...) { return; }
	std::lock_guard lock(g_data_mutex);
	auto found = g_data.find(self);
	if (found == g_data.end() || range.location > found->second.size() ||
		range.length > found->second.size() - range.location) return;
	const std::uint64_t final_size = found->second.size() - range.length + length;
	if (final_size > 256ull * 1024 * 1024) return;
	try {
		std::vector<std::uint8_t> changed = found->second;
		const auto begin = changed.begin() +
			static_cast<std::ptrdiff_t>(range.location);
		const auto end = begin + static_cast<std::ptrdiff_t>(range.length);
		changed.erase(begin, end);
		const auto insertion_point = changed.begin() +
			static_cast<std::ptrdiff_t>(range.location);
		changed.insert(insertion_point, replacement.begin(), replacement.end());
		found->second.swap(changed);
	} catch (...) {}
}

void NSArrayFinalize(id object)
{
	std::vector<id> elements;
	std::vector<id> nib_graph;
	{
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(object);
		if (found != g_arrays.end()) {
			elements = std::move(found->second);
			g_arrays.erase(found);
		}
	}
	{
		std::lock_guard lock(g_nib_array_graphs_mutex);
		auto found = g_nib_array_graphs.find(object);
		if (found != g_nib_array_graphs.end()) {
			nib_graph = std::move(found->second);
			g_nib_array_graphs.erase(found);
		}
	}
	for (id element : elements) objc_release(element);
	for (id object_in_graph : nib_graph) objc_release(object_in_graph);
}

bool NSNibArrayAttachGraph(id array, std::vector<id>& graph)
{
	if (array == nullptr) return false;
	try {
		std::lock_guard lock(g_nib_array_graphs_mutex);
		auto [found, inserted] = g_nib_array_graphs.try_emplace(array);
		if (!inserted) return false;
		found->second.swap(graph);
		return true;
	} catch (...) { return false; }
}

bool InitializeEmptyNSArray(id object)
{
	{
		std::lock_guard lock(g_array_mutex);
		if (!g_arrays.emplace(object, std::vector<id>{}).second) return true;
	}
	if (darling_objc_register_object_finalizer(object, &NSArrayFinalize))
		return true;
	NSArrayFinalize(object);
	return false;
}

id CreateNSArray(Class cls, const id* values, std::uint64_t count)
{
	if (cls == nullptr || (values == nullptr && count != 0) || count > 1'000'000 ||
		count > (std::numeric_limits<std::size_t>::max)()) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	std::vector<id> elements;
	try {
		elements.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			id value = values[index];
			if (value == nullptr) {
				for (id element : elements) objc_release(element);
				objc_release(object);
				return nullptr;
			}
			elements.push_back(objc_retain(value));
		}
		{
			std::lock_guard lock(g_array_mutex);
			g_arrays.emplace(object, std::move(elements));
		}
	} catch (...) {
		for (id element : elements) objc_release(element);
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSArrayFinalize)) {
		NSArrayFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSArrayArray(Class cls, SEL)
{
	return objc_autorelease(CreateNSArray(cls, nullptr, 0));
}

id NSArrayArrayWithObjectsCount(Class cls, SEL, const id* values,
	std::uint64_t count)
{
	return objc_autorelease(CreateNSArray(cls, values, count));
}

id NSArrayArrayWithObject(Class cls, SEL, id value)
{
	return value == nullptr ? nullptr : objc_autorelease(CreateNSArray(cls,
		&value, 1));
}

id NSArrayInitWithObjectsCount(id self, SEL, const id* values,
	std::uint64_t count)
{
	if (self == nullptr || (values == nullptr && count != 0) || count > 1'000'000)
		return nullptr;
	std::vector<id> elements;
	try {
		elements.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			if (values[index] == nullptr) throw std::invalid_argument("nil array element");
			elements.push_back(objc_retain(values[index]));
		}
	} catch (...) {
		for (id element : elements) objc_release(element);
		return nullptr;
	}
	std::vector<id> old;
	bool needs_finalizer = false;
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end()) {
			g_arrays.emplace(self, std::move(elements));
			needs_finalizer = true;
		} else {
			old.swap(found->second);
			found->second = std::move(elements);
		}
	} catch (...) {
		for (id element : elements) objc_release(element);
		return nullptr;
	}
	for (id element : old) objc_release(element);
	if (needs_finalizer && !darling_objc_register_object_finalizer(self,
		&NSArrayFinalize)) {
		NSArrayFinalize(self);
		return nullptr;
	}
	return self;
}

id CreateNSSet(Class cls, const id* values, std::uint64_t count)
{
	if (cls == nullptr || (count != 0 && values == nullptr) || count > 1'000'000)
		return nullptr;
	std::vector<id> unique;
	try {
		unique.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			id value = values[index];
			if (value == nullptr) throw std::invalid_argument("nil set element");
			if (std::none_of(unique.begin(), unique.end(),
				[value](id existing) { return ObjcObjectsEqual(existing, value); }))
				unique.push_back(value);
		}
	} catch (...) {
		return nullptr;
	}
	return CreateNSArray(cls, unique.data(), unique.size());
}

id NSSetSet(Class cls, SEL)
{
	return objc_autorelease(CreateNSSet(cls, nullptr, 0));
}

id NSSetSetWithObjectsCount(Class cls, SEL, const id* values,
	std::uint64_t count)
{
	return objc_autorelease(CreateNSSet(cls, values, count));
}

id NSSetSetWithObject(Class cls, SEL, id value)
{
	return value == nullptr ? nullptr : objc_autorelease(CreateNSSet(cls,
		&value, 1));
}

id NSSetMember(id self, SEL, id value)
{
	if (value == nullptr) return nullptr;
	std::vector<id> elements;
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end()) return nullptr;
		elements.reserve(found->second.size());
		for (id element : found->second) elements.push_back(objc_retain(element));
	} catch (...) {
		for (id element : elements) objc_release(element);
		return nullptr;
	}
	id result = nullptr;
	for (id element : elements) {
		if (ObjcObjectsEqual(element, value)) {
			result = objc_retainAutorelease(element);
			break;
		}
	}
	for (id element : elements) objc_release(element);
	return result;
}

id NSSetAllObjects(id self, SEL)
{
	std::vector<id> elements;
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end()) return nullptr;
		elements.reserve(found->second.size());
		for (id element : found->second) elements.push_back(objc_retain(element));
	} catch (...) {
		for (id element : elements) objc_release(element);
		return nullptr;
	}
	id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
		elements.data(), elements.size()));
	for (id element : elements) objc_release(element);
	return result;
}

bool IsMutableNSSet(id object)
{
	const Class cls = object_getClass(object);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	return name != nullptr && std::strcmp(name, "NSMutableSet") == 0;
}

void NSMutableSetAddObject(id self, SEL, id value)
{
	if (!IsMutableNSSet(self) || value == nullptr || NSSetMember(self, nullptr,
		value) != nullptr) return;
	id retained = objc_retain(value);
	bool inserted = false;
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found != g_arrays.end() && found->second.size() < 1'000'000) {
			found->second.push_back(retained);
			inserted = true;
		}
	} catch (...) {}
	if (!inserted) objc_release(retained);
}

void NSMutableSetRemoveObject(id self, SEL, id value)
{
	if (!IsMutableNSSet(self) || value == nullptr) return;
	id member = NSSetMember(self, nullptr, value);
	if (member == nullptr) return;
	id removed = nullptr;
	{
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found != g_arrays.end()) {
			auto item = std::find(found->second.begin(), found->second.end(), member);
			if (item != found->second.end()) {
				removed = *item;
				found->second.erase(item);
			}
		}
	}
	objc_release(removed);
}

void NSMutableSetRemoveAllObjects(id self, SEL)
{
	if (!IsMutableNSSet(self)) return;
	std::vector<id> removed;
	{
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end()) return;
		removed.swap(found->second);
	}
	for (id element : removed) objc_release(element);
}

std::uint64_t NSArrayCount(id self, SEL)
{
	std::lock_guard lock(g_array_mutex);
	auto found = g_arrays.find(self);
	return found == g_arrays.end() ? 0 : found->second.size();
}

id NSArrayObjectAtIndex(id self, SEL, std::uint64_t index)
{
	std::lock_guard lock(g_array_mutex);
	auto found = g_arrays.find(self);
	if (found == g_arrays.end() || index >= found->second.size()) return nullptr;
	return objc_retainAutorelease(found->second[static_cast<std::size_t>(index)]);
}

struct NSRunLoopSelectorInvocation final {
	id target{};
	SEL selector{};
	id argument{};
	~NSRunLoopSelectorInvocation()
	{
		if (target != nullptr) objc_release(target);
		if (argument != nullptr) objc_release(argument);
	}
};

void NSRunLoopInvokeSelector(void* context)
{
	std::unique_ptr<NSRunLoopSelectorInvocation> invocation(
		static_cast<NSRunLoopSelectorInvocation*>(context));
	if (invocation == nullptr) return;
	try {
		darling_objc_msgSend_void1_object(invocation->target,
			invocation->selector, invocation->argument);
	} catch (...) {
		// Guest exceptions must not unwind through the native run loop.
	}
}

void NSRunLoopPerformSelector(id self, SEL, SEL selector, id target,
	id argument, std::uint64_t order, id modes)
{
	const auto loop = NSRunLoopGetCFRunLoop(self, nullptr);
	const auto mode_count = NSArrayCount(modes, nullptr);
	if (loop == nullptr || selector == nullptr || target == nullptr ||
		mode_count == 0 || mode_count > 256) return;
	std::vector<const char*> mode_names;
	try {
		mode_names.reserve(static_cast<std::size_t>(mode_count));
		for (std::uint64_t index = 0; index < mode_count; ++index) {
			id mode = NSArrayObjectAtIndex(modes, nullptr, index);
			const char* name = mode == nullptr ? nullptr : NSStringUTF8String(mode, nullptr);
			if (name == nullptr || *name == '\0') return;
			mode_names.push_back(name);
		}
	} catch (...) {
		return;
	}
	auto invocation = std::unique_ptr<NSRunLoopSelectorInvocation>(
		new (std::nothrow) NSRunLoopSelectorInvocation{
			objc_retain(target), selector,
			argument == nullptr ? nullptr : objc_retain(argument)});
	if (invocation == nullptr) return;
	if (!darling_windows_CFRunLoopPerformBlockInModesWithOrder(loop,
		mode_names.data(), mode_names.size(), order,
		&NSRunLoopInvokeSelector, invocation.get())) return;
	invocation.release();
	darling_windows_CFRunLoopWakeUp(loop);
	const DWORD application_thread = g_application_thread_id.load(std::memory_order_acquire);
	if (application_thread != 0)
		PostThreadMessageW(application_thread, kApplicationEventMessage, 0, 0);
}

bool NSArrayContainsObject(id self, SEL, id value)
{
	if (value == nullptr) return false;
	std::vector<id> elements;
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end()) return false;
		elements.reserve(found->second.size());
		for (id element : found->second) elements.push_back(objc_retain(element));
	} catch (...) {
		for (id element : elements) objc_release(element);
		return false;
	}
	const SEL is_equal = sel_registerName("isEqual:");
	bool contains = false;
	for (id element : elements) {
		if (element == value || darling_objc_msgSend_bool_object1(element,
			is_equal, value)) {
			contains = true;
			break;
		}
	}
	for (id element : elements) objc_release(element);
	return contains;
}

bool IsMutableNSArray(id object)
{
	const Class cls = object_getClass(object);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	return name != nullptr && std::strcmp(name, "NSMutableArray") == 0;
}

void NSMutableArrayAddObject(id self, SEL, id value)
{
	if (!IsMutableNSArray(self) || value == nullptr) return;
	id retained = objc_retain(value);
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end() || found->second.size() >= 1'000'000)
			throw std::length_error("NSMutableArray limit");
		found->second.push_back(retained);
	} catch (...) {
		objc_release(retained);
	}
}

void NSMutableArrayInsertObjectAtIndex(id self, SEL, id value,
	std::uint64_t index)
{
	if (!IsMutableNSArray(self) || value == nullptr) return;
	id retained = objc_retain(value);
	try {
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end() || index > found->second.size() ||
			found->second.size() >= 1'000'000)
			throw std::out_of_range("NSMutableArray index");
		found->second.insert(found->second.begin() +
			static_cast<std::ptrdiff_t>(index), retained);
	} catch (...) {
		objc_release(retained);
	}
}

void NSMutableArrayRemoveObjectAtIndex(id self, SEL, std::uint64_t index)
{
	if (!IsMutableNSArray(self)) return;
	id removed = nullptr;
	{
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end() || index >= found->second.size()) return;
		auto position = found->second.begin() + static_cast<std::ptrdiff_t>(index);
		removed = *position;
		found->second.erase(position);
	}
	objc_release(removed);
}

void NSMutableArrayReplaceObjectAtIndex(id self, SEL, std::uint64_t index,
	id value)
{
	if (!IsMutableNSArray(self) || value == nullptr) return;
	id retained = objc_retain(value);
	id removed = nullptr;
	bool replaced = false;
	{
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found != g_arrays.end() && index < found->second.size()) {
			removed = found->second[static_cast<std::size_t>(index)];
			found->second[static_cast<std::size_t>(index)] = retained;
			replaced = true;
		}
	}
	if (replaced) objc_release(removed);
	else objc_release(retained);
}

void NSMutableArrayRemoveAllObjects(id self, SEL)
{
	if (!IsMutableNSArray(self)) return;
	std::vector<id> removed;
	{
		std::lock_guard lock(g_array_mutex);
		auto found = g_arrays.find(self);
		if (found == g_arrays.end()) return;
		removed.swap(found->second);
	}
	for (id element : removed) objc_release(element);
}

void NSDictionaryFinalize(id object)
{
	std::vector<std::pair<id, id>> entries;
	{
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(object);
		if (found == g_dictionaries.end()) return;
		entries = std::move(found->second);
		g_dictionaries.erase(found);
	}
	for (const auto& [key, value] : entries) {
		objc_release(key);
		objc_release(value);
	}
}

bool InitializeEmptyNSDictionary(id object)
{
	{
		std::lock_guard lock(g_dictionary_mutex);
		if (!g_dictionaries.emplace(object,
			std::vector<std::pair<id, id>>{}).second) return true;
	}
	if (darling_objc_register_object_finalizer(object, &NSDictionaryFinalize))
		return true;
	NSDictionaryFinalize(object);
	return false;
}

bool ObjcObjectsEqual(id left, id right)
{
	return left == right || (left != nullptr && right != nullptr &&
		darling_objc_msgSend_bool_object1(left, sel_registerName("isEqual:"),
			right));
}

id CreateNSDictionary(Class cls, const id* values, const id* keys,
	std::uint64_t count)
{
	if (cls == nullptr || (count != 0 && (values == nullptr || keys == nullptr)) ||
		count > 1'000'000) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	std::vector<std::pair<id, id>> entries;
	try {
		entries.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			id value = values[index];
			id key = keys[index];
			if (value == nullptr || key == nullptr) {
				for (const auto& [stored_key, stored_value] : entries) {
					objc_release(stored_key);
					objc_release(stored_value);
				}
				objc_release(object);
				return nullptr;
			}
			auto duplicate = std::find_if(entries.begin(), entries.end(),
				[key](const auto& entry) { return ObjcObjectsEqual(entry.first, key); });
			if (duplicate != entries.end()) {
				id retained = objc_retain(value);
				id old_value = std::exchange(duplicate->second, retained);
				objc_release(old_value);
			} else {
				entries.emplace_back(objc_retain(key), objc_retain(value));
			}
		}
		{
			std::lock_guard lock(g_dictionary_mutex);
			g_dictionaries.emplace(object, std::move(entries));
		}
	} catch (...) {
		for (const auto& [key, value] : entries) {
			objc_release(key);
			objc_release(value);
		}
		objc_release(object);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(object, &NSDictionaryFinalize)) {
		NSDictionaryFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

id NSDictionaryDictionary(Class cls, SEL)
{
	return objc_autorelease(CreateNSDictionary(cls, nullptr, nullptr, 0));
}

id NSDictionaryDictionaryWithObjectsKeysCount(Class cls, SEL,
	const id* values, const id* keys, std::uint64_t count)
{
	return objc_autorelease(CreateNSDictionary(cls, values, keys, count));
}

id NSDictionaryInitWithObjectsKeysCount(id self, SEL, const id* values,
	const id* keys, std::uint64_t count)
{
	if (self == nullptr || (count != 0 && (values == nullptr || keys == nullptr)) ||
		count > 1'000'000) return nullptr;
	std::vector<std::pair<id, id>> entries;
	try {
		entries.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			id value = values[index];
			id key = keys[index];
			if (value == nullptr || key == nullptr)
				throw std::invalid_argument("nil dictionary entry");
			auto duplicate = std::find_if(entries.begin(), entries.end(),
				[key](const auto& entry) { return ObjcObjectsEqual(entry.first, key); });
			if (duplicate == entries.end())
				entries.emplace_back(objc_retain(key), objc_retain(value));
			else {
				id retained_value = objc_retain(value);
				id old_value = std::exchange(duplicate->second, retained_value);
				objc_release(old_value);
			}
		}
	} catch (...) {
		for (const auto& [key, value] : entries) {
			objc_release(key);
			objc_release(value);
		}
		return nullptr;
	}
	std::vector<std::pair<id, id>> old;
	bool needs_finalizer = false;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) {
			g_dictionaries.emplace(self, std::move(entries));
			needs_finalizer = true;
		} else {
			old.swap(found->second);
			found->second = std::move(entries);
		}
	} catch (...) {
		for (const auto& [key, value] : entries) {
			objc_release(key);
			objc_release(value);
		}
		return nullptr;
	}
	for (const auto& [key, value] : old) {
		objc_release(key);
		objc_release(value);
	}
	if (needs_finalizer && !darling_objc_register_object_finalizer(self,
		&NSDictionaryFinalize)) {
		NSDictionaryFinalize(self);
		return nullptr;
	}
	return self;
}

std::uint64_t NSDictionaryCount(id self, SEL)
{
	std::lock_guard lock(g_dictionary_mutex);
	auto found = g_dictionaries.find(self);
	return found == g_dictionaries.end() ? 0 : found->second.size();
}

id NSDictionaryObjectForKey(id self, SEL, id key)
{
	if (key == nullptr) return nullptr;
	std::vector<std::pair<id, id>> entries;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) return nullptr;
		entries.reserve(found->second.size());
		for (const auto& [stored_key, value] : found->second)
			entries.emplace_back(objc_retain(stored_key), objc_retain(value));
	} catch (...) {
		for (const auto& [stored_key, value] : entries) {
			objc_release(stored_key);
			objc_release(value);
		}
		return nullptr;
	}
	for (const auto& [stored_key, value] : entries) {
		const bool matches = ObjcObjectsEqual(stored_key, key);
		if (matches) {
			id result = objc_retainAutorelease(value);
			for (const auto& [release_key, release_value] : entries) {
				objc_release(release_key);
				objc_release(release_value);
			}
			return result;
		}
	}
	for (const auto& [stored_key, value] : entries) {
		objc_release(stored_key);
		objc_release(value);
	}
	return nullptr;
}

id NSDictionaryAllKeys(id self, SEL)
{
	std::vector<id> keys;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) return nullptr;
		keys.reserve(found->second.size());
		for (const auto& entry : found->second) keys.push_back(objc_retain(entry.first));
	} catch (...) {
		for (id key : keys) objc_release(key);
		return nullptr;
	}
	id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
		keys.data(), keys.size()));
	for (id key : keys) objc_release(key);
	return result;
}

id NSDictionaryAllValues(id self, SEL)
{
	std::vector<id> values;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) return nullptr;
		values.reserve(found->second.size());
		for (const auto& entry : found->second)
			values.push_back(objc_retain(entry.second));
	} catch (...) {
		for (id value : values) objc_release(value);
		return nullptr;
	}
	id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
		values.data(), values.size()));
	for (id value : values) objc_release(value);
	return result;
}

void NSMutableDictionarySetObjectForKey(id self, SEL, id value, id key)
{
	if (value == nullptr || key == nullptr) return;
	const Class cls = object_getClass(self);
	const char* class_name = cls == nullptr ? nullptr : class_getName(cls);
	if (class_name == nullptr || std::strcmp(class_name, "NSMutableDictionary") != 0)
		return;
	id retained_key = objc_retain(key);
	id retained_value = objc_retain(value);
	id old_value = nullptr;
	std::vector<std::pair<id, id>> snapshot;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) throw std::runtime_error("missing dictionary");
		snapshot.reserve(found->second.size());
		for (const auto& [stored_key, stored_value] : found->second)
			snapshot.emplace_back(objc_retain(stored_key), objc_retain(stored_value));
	} catch (...) {
		for (const auto& [stored_key, stored_value] : snapshot) {
			objc_release(stored_key);
			objc_release(stored_value);
		}
		objc_release(retained_key);
		objc_release(retained_value);
		return;
	}
	id matching_key = nullptr;
	for (const auto& [stored_key, stored_value] : snapshot) {
		(void)stored_value;
		if (ObjcObjectsEqual(stored_key, key)) {
			matching_key = stored_key;
			break;
		}
	}
	bool inserted = false;
	bool updated = false;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found != g_dictionaries.end()) {
			auto entry = matching_key == nullptr ? found->second.end() :
				std::find_if(found->second.begin(), found->second.end(),
					[matching_key](const auto& candidate) {
						return candidate.first == matching_key;
					});
			if (entry != found->second.end()) {
				old_value = entry->second;
				entry->second = retained_value;
				updated = true;
			} else if (found->second.size() < 1'000'000) {
				found->second.emplace_back(retained_key, retained_value);
				inserted = true;
			}
		}
	} catch (...) {}
	for (const auto& [stored_key, stored_value] : snapshot) {
		objc_release(stored_key);
		objc_release(stored_value);
	}
	if (updated) {
		objc_release(retained_key);
		objc_release(old_value);
	} else if (!inserted) {
		objc_release(retained_key);
		objc_release(retained_value);
	}
}

void NSMutableDictionaryRemoveObjectForKey(id self, SEL, id key)
{
	if (key == nullptr) return;
	const Class cls = object_getClass(self);
	const char* class_name = cls == nullptr ? nullptr : class_getName(cls);
	if (class_name == nullptr || std::strcmp(class_name, "NSMutableDictionary") != 0)
		return;
	id old_key = nullptr;
	id old_value = nullptr;
	std::vector<id> snapshot;
	try {
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) return;
		snapshot.reserve(found->second.size());
		for (const auto& entry : found->second)
			snapshot.push_back(objc_retain(entry.first));
	} catch (...) {
		for (id stored_key : snapshot) objc_release(stored_key);
		return;
	}
	id matching_key = nullptr;
	for (id stored_key : snapshot) {
		if (ObjcObjectsEqual(stored_key, key)) {
			matching_key = stored_key;
			break;
		}
	}
	{
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found != g_dictionaries.end() && matching_key != nullptr) {
			auto entry = std::find_if(found->second.begin(), found->second.end(),
				[matching_key](const auto& candidate) {
					return candidate.first == matching_key;
				});
			if (entry != found->second.end()) {
				old_key = entry->first;
				old_value = entry->second;
				found->second.erase(entry);
			}
		}
	}
	for (id stored_key : snapshot) objc_release(stored_key);
	objc_release(old_key);
	objc_release(old_value);
}

void NSMutableDictionaryRemoveAllObjects(id self, SEL)
{
	const Class cls = object_getClass(self);
	const char* class_name = cls == nullptr ? nullptr : class_getName(cls);
	if (class_name == nullptr || std::strcmp(class_name, "NSMutableDictionary") != 0)
		return;
	std::vector<std::pair<id, id>> removed;
	{
		std::lock_guard lock(g_dictionary_mutex);
		auto found = g_dictionaries.find(self);
		if (found == g_dictionaries.end()) return;
		removed.swap(found->second);
	}
	for (const auto& [key, value] : removed) {
		objc_release(key);
		objc_release(value);
	}
}

struct MachOConstantString final {
	void* isa;
	std::uint32_t flags;
	std::uint32_t padding;
	const void* contents;
	std::uint64_t length;
};
static_assert(sizeof(MachOConstantString) == 32);

constexpr std::size_t kDynamicStringLengthOffset = 2 * sizeof(std::uintptr_t);
constexpr std::size_t kDynamicStringBytesOffset =
	kDynamicStringLengthOffset + sizeof(std::uint64_t);

bool IsSupportedNSStringClass(Class cls)
{
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	return name != nullptr && (std::strcmp(name, "NSString") == 0 ||
		std::strcmp(name, "NSConstantString") == 0 ||
		std::strcmp(name, "NSMutableString") == 0);
}

bool IsMutableNSString(id object)
{
	const Class cls = object_getClass(object);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	return name != nullptr && std::strcmp(name, "NSMutableString") == 0;
}

bool IsDynamicNSString(id object)
{
	return IsSupportedNSStringClass(object_getClass(object));
}

bool ConvertWideToUtf8(std::wstring_view source, std::string& result)
{
	if (source.size() > static_cast<std::size_t>(INT_MAX)) return false;
	const int source_length = static_cast<int>(source.size());
	const int required = source_length == 0 ? 0 : WideCharToMultiByte(CP_UTF8,
		WC_ERR_INVALID_CHARS, source.data(), source_length, nullptr, 0,
		nullptr, nullptr);
	if (required == 0 && source_length != 0) return false;
	result.resize(static_cast<std::size_t>(required));
	return required == 0 || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		source.data(), source_length, result.data(), required, nullptr,
		nullptr) == required;
}

std::string WideToUtf8(std::wstring_view wide)
{
	std::string result;
	if (!ConvertWideToUtf8(wide, result))
		throw std::runtime_error("invalid UTF-16 path");
	return result;
}

bool UTF8Substring(const char* source, DarlingObjcRange range,
	std::string& result)
{
	if (source == nullptr) return false;
	const std::wstring wide = Utf8ToWide(source);
	if (wide.empty() && source[0] != '\0') return false;
	if (range.location > wide.size() || range.length > wide.size() - range.location)
		return false;
	const auto begin = static_cast<std::size_t>(range.location);
	const auto end = begin + static_cast<std::size_t>(range.length);
	if ((begin > 0 && begin < wide.size() && wide[begin] >= 0xdc00 &&
		wide[begin] <= 0xdfff) || (end > 0 && end < wide.size() &&
		wide[end] >= 0xdc00 && wide[end] <= 0xdfff))
		return false;
	return ConvertWideToUtf8(std::wstring_view(wide).substr(begin, end - begin),
		result);
}

bool UTF8ByDeletingRange(std::string_view source, DarlingObjcRange range,
	std::string& result)
{
	std::string terminated(source);
	const std::wstring wide = Utf8ToWide(terminated.c_str());
	if (wide.empty() && !source.empty()) return false;
	if (range.location > wide.size() || range.length > wide.size() - range.location)
		return false;
	const auto begin = static_cast<std::size_t>(range.location);
	const auto end = begin + static_cast<std::size_t>(range.length);
	if ((begin > 0 && begin < wide.size() && wide[begin] >= 0xdc00 &&
		wide[begin] <= 0xdfff) || (end > 0 && end < wide.size() &&
		wide[end] >= 0xdc00 && wide[end] <= 0xdfff))
		return false;
	std::wstring changed = wide;
	changed.erase(begin, end - begin);
	return ConvertWideToUtf8(changed, result);
}

bool UTF8ByReplacingRange(std::string_view source, DarlingObjcRange range,
	std::string_view replacement, std::string& result)
{
	std::string terminated_source(source);
	std::string terminated_replacement(replacement);
	const std::wstring wide = Utf8ToWide(terminated_source.c_str());
	const std::wstring wide_replacement = Utf8ToWide(terminated_replacement.c_str());
	if ((wide.empty() && !source.empty()) ||
		(wide_replacement.empty() && !replacement.empty()) ||
		range.location > wide.size() || range.length > wide.size() - range.location)
		return false;
	const auto begin = static_cast<std::size_t>(range.location);
	const auto end = begin + static_cast<std::size_t>(range.length);
	if ((begin > 0 && begin < wide.size() && wide[begin] >= 0xdc00 &&
		wide[begin] <= 0xdfff) || (end > 0 && end < wide.size() &&
		wide[end] >= 0xdc00 && wide[end] <= 0xdfff))
		return false;
	std::wstring changed = wide;
	changed.replace(begin, end - begin, wide_replacement);
	if (changed.size() > 16 * 1024 * 1024) return false;
	return ConvertWideToUtf8(changed, result);
}

const char* DynamicNSStringBytes(id object)
{
	return reinterpret_cast<const char*>(object) + kDynamicStringBytesOffset;
}

id CreateDynamicNSString(Class cls, const char* utf8)
{
	const char* class_name = cls == nullptr ? nullptr : class_getName(cls);
	if (class_name == nullptr ||
		(std::strcmp(class_name, "NSString") != 0 &&
			std::strcmp(class_name, "NSConstantString") != 0) || utf8 == nullptr)
		return nullptr;
	const std::size_t length = std::strlen(utf8);
	if (length > 16 * 1024 * 1024 || length >
		(std::numeric_limits<std::size_t>::max)() - sizeof(std::uint64_t) - 1)
		return nullptr;
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
		nullptr, 0) == 0)
		return nullptr;
	id object = class_createInstance(cls, sizeof(std::uint64_t) + length + 1);
	if (object == nullptr) return nullptr;
	const auto stored_length = static_cast<std::uint64_t>(length);
	std::memcpy(reinterpret_cast<std::uint8_t*>(object) +
		kDynamicStringLengthOffset, &stored_length, sizeof(stored_length));
	std::memcpy(reinterpret_cast<std::uint8_t*>(object) +
		kDynamicStringBytesOffset, utf8, length + 1);
	return object;
}

void NSMutableStringFinalize(id object);

void NSMutableStringFinalize(id object)
{
	std::lock_guard lock(g_mutable_string_mutex);
	g_mutable_strings.erase(object);
}

id CreateMutableNSString(Class cls, const char* utf8)
{
	if (cls == nullptr || std::strcmp(class_getName(cls), "NSMutableString") != 0 ||
		utf8 == nullptr)
		return nullptr;
	const std::size_t length = std::strlen(utf8);
	if (length > 16 * 1024 * 1024 ||
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
			nullptr, 0) == 0)
		return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	{
		std::lock_guard lock(g_mutable_string_mutex);
		g_mutable_strings.emplace(object, utf8);
	}
	if (!darling_objc_register_object_finalizer(object,
		&NSMutableStringFinalize)) {
		NSMutableStringFinalize(object);
		objc_release(object);
		return nullptr;
	}
	return object;
}

extern "C" {
id NSApp = nullptr;
}

LRESULT CALLBACK AppKitBootstrapWindowProc(HWND window, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	if (message == WM_PAINT) {
		PaintAppKitWindow(window);
		return 0;
	}
	if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
		message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN ||
		message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
		message == WM_MOUSEMOVE) {
		if (DispatchAppKitMouseEvent(window, message, wparam, lparam)) return 0;
	}
	if (message == WM_CAPTURECHANGED &&
		reinterpret_cast<HWND>(lparam) != window)
		ClearAppKitMouseTracking(window);
	if (message == WM_CANCELMODE) ClearAppKitMouseTracking(window);
	if (message == WM_CONTEXTMENU) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		id root = nullptr;
		{
			std::lock_guard lock(g_windows_mutex);
			const auto found = g_windows.find(object);
			if (found != g_windows.end()) root = found->second.content_view;
		}
		POINT screen{static_cast<short>(LOWORD(lparam)),
			static_cast<short>(HIWORD(lparam))};
		if (screen.x == -1 && screen.y == -1) GetCursorPos(&screen);
		POINT local = screen;
		if (ScreenToClient(window, &local)) {
			RECT client{};
			GetClientRect(window, &client);
			id view = FindAppKitViewAtPoint(root, local.x,
				client.bottom - local.y);
			id menu = view == nullptr ? nullptr :
				objc_getAssociatedObject(view, &g_view_menu_key);
			if (menu != nullptr) {
				PopupNativeAppMenu(menu, window, screen);
				return 0;
			}
		}
	}
	if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
		if (DispatchAppKitScrollEvent(window, message, wparam, lparam)) return 0;
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		id root = nullptr;
		{
			std::lock_guard lock(g_windows_mutex);
			const auto found = g_windows.find(object);
			if (found != g_windows.end()) root = found->second.content_view;
		}
		POINT local{static_cast<short>(LOWORD(lparam)),
			static_cast<short>(HIWORD(lparam))};
		if (ScreenToClient(window, &local)) {
			RECT client{};
			GetClientRect(window, &client);
			if (ScrollAppKitViewAtPoint(root, local.x,
				client.bottom - local.y, GET_WHEEL_DELTA_WPARAM(wparam),
				message == WM_MOUSEHWHEEL)) return 0;
		}
	}
	if (message == WM_CLOSE) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		if (object != nullptr && !NSWindowShouldClose(object)) return 0;
		DestroyWindow(window);
		return 0;
	}
	if (message == WM_GETMINMAXINFO) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		AppKitWindowState state{};
		bool found_state = false;
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			const auto found = g_windows.find(object);
			if (found != g_windows.end()) { state = found->second; found_state = true; }
		}
		if (found_state) {
			auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
			const DWORD style = GetWindowLongPtrW(window, GWL_STYLE);
			const DWORD extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
			if (state.min_size.x > 0.0 || state.min_size.y > 0.0) {
				RECT frame{0, 0, static_cast<LONG>(std::ceil(state.min_size.x)),
					static_cast<LONG>(std::ceil(state.min_size.y))};
				if (AdjustWindowRectEx(&frame, style, GetMenu(window) != nullptr,
					extended_style)) {
					limits->ptMinTrackSize.x = std::max<LONG>(
						limits->ptMinTrackSize.x, frame.right - frame.left);
					limits->ptMinTrackSize.y = std::max<LONG>(
						limits->ptMinTrackSize.y, frame.bottom - frame.top);
				}
			}
			if (state.max_size.x > 0.0 && state.max_size.x < 10000.0) {
				RECT frame{0, 0, static_cast<LONG>(std::floor(state.max_size.x)), 0};
				if (AdjustWindowRectEx(&frame, style, GetMenu(window) != nullptr,
					extended_style)) limits->ptMaxTrackSize.x =
					frame.right - frame.left;
			}
			if (state.max_size.y > 0.0 && state.max_size.y < 10000.0) {
				RECT frame{0, 0, 0, static_cast<LONG>(std::floor(state.max_size.y))};
				if (AdjustWindowRectEx(&frame, style, GetMenu(window) != nullptr,
					extended_style)) limits->ptMaxTrackSize.y =
					frame.bottom - frame.top;
			}
		}
	}
	if (message == WM_SIZE) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		if (object != nullptr) NSWindowNotifyDelegate(object, "windowDidResize:");
	}
	if (message == WM_ACTIVATEAPP)
		NSApplicationSetActiveState(wparam != FALSE);
	if (message == WM_INITMENU || message == WM_INITMENUPOPUP) {
		ValidateAppKitMenu(reinterpret_cast<HMENU>(wparam));
	}
	if ((message == WM_KEYDOWN || message == WM_KEYUP ||
		message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
		DispatchAppKitKeyEvent(window, message, wparam, lparam)) return 0;
	if (message == WM_COMMAND && lparam != 0) {
		const HWND control = reinterpret_cast<HWND>(lparam);
		if (HIWORD(wparam) == CBN_SELCHANGE) {
			const LRESULT selection = SendMessageW(control, CB_GETCURSEL, 0, 0);
			bool is_popup = false;
			{
				std::lock_guard lock(g_windows_mutex);
				const auto found = g_controls.find(control);
				if (found != g_controls.end() &&
					IsViewKindOf(found->second, "NSPopUpButton")) {
					is_popup = true;
					const auto state = g_views.find(found->second);
					if (state != g_views.end())
						state->second.popup_selected_index = selection == CB_ERR ? -1 :
							static_cast<std::int64_t>(selection);
				}
			}
			if (is_popup) DispatchControlAction(control);
			return 0;
		}
		if (HIWORD(wparam) == BN_CLICKED) {
			DispatchControlAction(control);
			return 0;
		}
		if (HIWORD(wparam) == EN_CHANGE) {
			UpdateTextFieldValue(control);
			return 0;
		}
	}
	if ((message == WM_VSCROLL || message == WM_HSCROLL) &&
		(DispatchNativeSliderMessage(window, message, wparam, lparam) ||
			DispatchScrollBarMessage(window, message, wparam, lparam))) return 0;
		if (message == WM_NOTIFY && DispatchNativeStepperMessage(window, message,
			wparam, lparam)) return 1;
	if (message == WM_COMMAND && lparam == 0 &&
		DispatchAppKitMenuCommand(LOWORD(wparam))) return 0;
	if (message == WM_DESTROY) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		if (object != nullptr) {
			HMENU menu = nullptr;
			bool another_window_is_open = false;
			bool was_key_window = false;
			bool was_main_window = false;
			{
				std::lock_guard<std::mutex> lock(g_windows_mutex);
				const auto found = g_windows.find(object);
				if (found != g_windows.end()) {
					found->second.handle = nullptr;
					found->second.visible = false;
					menu = std::exchange(found->second.native_menu, nullptr);
				}
				if (g_key_appkit_window == object) {
					g_key_appkit_window = nullptr;
					was_key_window = true;
				}
				if (g_main_appkit_window == object) {
					g_main_appkit_window = nullptr;
					was_main_window = true;
				}
				g_application_hidden_windows.erase(object);
				for (const auto& [candidate, state] : g_windows) {
					if (candidate != object && state.handle != nullptr &&
						IsWindow(state.handle)) {
						another_window_is_open = true;
						break;
					}
				}
			}
			if (menu != nullptr) DestroyMenu(menu);
			if (was_key_window)
				NSWindowNotifyDelegate(object, "windowDidResignKey:");
			if (was_main_window)
				NSWindowNotifyDelegate(object, "windowDidResignMain:");
			NSWindowNotifyDelegate(object, "windowWillClose:");
			if (!another_window_is_open &&
				g_application_running.load(std::memory_order_acquire) &&
				NSApplicationShouldTerminateAfterLastWindowClosed()) {
				id application = g_shared_application;
				if (application != nullptr)
					NSApplicationTerminate(application,
						sel_registerName("terminate:"), object);
			}
			return 0;
		}
		return 0;
	}
	if (message == WM_NCCREATE) {
		const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
		SetWindowLongPtrW(window, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(create->lpCreateParams));
	}
	if (message == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		id previous_key_window = nullptr;
		id previous_main_window = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			if (g_windows.find(object) != g_windows.end()) {
				previous_key_window = g_key_appkit_window;
				previous_main_window = g_main_appkit_window;
				g_key_appkit_window = object;
				g_main_appkit_window = object;
			}
		}
		if (previous_key_window != nullptr && previous_key_window != object)
			NSWindowNotifyDelegate(previous_key_window, "windowDidResignKey:");
		if (previous_main_window != nullptr && previous_main_window != object)
			NSWindowNotifyDelegate(previous_main_window, "windowDidResignMain:");
		if (previous_key_window != object)
			NSWindowNotifyDelegate(object, "windowDidBecomeKey:");
		if (previous_main_window != object)
			NSWindowNotifyDelegate(object, "windowDidBecomeMain:");
	} else if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) {
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		bool was_key_window = false;
		bool was_main_window = false;
		{
			std::lock_guard lock(g_windows_mutex);
			if (g_key_appkit_window == object) {
				g_key_appkit_window = nullptr;
				was_key_window = true;
			}
			if (g_main_appkit_window == object) {
				g_main_appkit_window = nullptr;
				was_main_window = true;
			}
		}
		if (was_key_window)
			NSWindowNotifyDelegate(object, "windowDidResignKey:");
		if (was_main_window)
			NSWindowNotifyDelegate(object, "windowDidResignMain:");
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterBootstrapWindowClass()
{
	std::call_once(g_window_class_once, [] {
		WNDCLASSEXW window_class{};
		window_class.cbSize = sizeof(window_class);
		window_class.lpfnWndProc = AppKitBootstrapWindowProc;
		window_class.hInstance = GetModuleHandleW(nullptr);
		window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		window_class.style = CS_DBLCLKS;
		window_class.lpszClassName = kWindowClass;
		SetLastError(ERROR_SUCCESS);
		g_window_class_registered = RegisterClassExW(&window_class) != 0 ||
			GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	});
	return g_window_class_registered;
}

id ObjcObjectFromCF(darling_windows_CFTypeRef value, unsigned depth)
{
	if (value == nullptr || depth > 32) return nullptr;
	const auto type = darling_windows_CFGetTypeID(value);
	if (type == darling_windows_CFStringGetTypeID())
		return objc_retain(NSStringFromCFString(
			static_cast<darling_windows_CFStringRef>(value)));
	if (type == darling_windows_CFBooleanGetTypeID())
		return CreateNSNumber(objc_getClass("NSNumber"),
			darling_windows_CFBooleanIsTrue(
				static_cast<darling_windows_CFBooleanRef>(value)) ? 1.0 : 0.0,
			true);
	if (type == darling_windows_CFDateGetTypeID())
		return CreateNSDate(objc_getClass("NSDate"),
			darling_windows_CFDateGetAbsoluteTime(
				static_cast<darling_windows_CFDateRef>(value)));
	if (type == darling_windows_CFNullGetTypeID())
		return objc_retain(NSNullSingleton(objc_getClass("NSNull"), nullptr));
	if (type == darling_windows_CFNumberGetTypeID()) {
		double real = 0;
		if (!darling_windows_CFNumberGetDouble(value, &real) || !std::isfinite(real))
			return nullptr;
		std::int64_t integer = 0;
		if (darling_windows_CFNumberGetInteger(value, &integer) &&
			static_cast<double>(integer) == real)
			return CreateIntegerNSNumber(objc_getClass("NSNumber"), integer);
		return CreateNSNumber(objc_getClass("NSNumber"), real);
	}
	if (type == darling_windows_CFDataGetTypeID()) {
		const auto data = static_cast<darling_windows_CFDataRef>(value);
		const auto length = darling_windows_CFDataGetLength(data);
		return length < 0 || length > 256ll * 1024 * 1024 ? nullptr :
			CreateNSData(objc_getClass("NSData"),
				darling_windows_CFDataGetBytePtr(data),
				static_cast<std::uint64_t>(length));
	}
	if (type == darling_windows_CFArrayGetTypeID()) {
		const auto array = static_cast<darling_windows_CFArrayRef>(value);
		const auto count = darling_windows_CFArrayGetCount(array);
		if (count < 0 || count > 100'000) return nullptr;
		std::vector<id> elements;
		try {
			elements.reserve(static_cast<std::size_t>(count));
			for (darling_windows_CFIndex index = 0; index < count; ++index) {
				id element = ObjcObjectFromCF(
					darling_windows_CFArrayGetValueAtIndex(array, index), depth + 1);
				if (element == nullptr) throw std::runtime_error("unsupported CF array value");
				elements.push_back(element);
			}
		} catch (...) {
			for (id element : elements) objc_release(element);
			return nullptr;
		}
		id result = CreateNSArray(objc_getClass("NSArray"), elements.data(),
			elements.size());
		for (id element : elements) objc_release(element);
		return result;
	}
	if (type == darling_windows_CFDictionaryGetTypeID()) {
		const auto dictionary = static_cast<darling_windows_CFDictionaryRef>(value);
		const auto count = darling_windows_CFDictionaryGetCount(dictionary);
		if (count < 0 || count > 100'000) return nullptr;
		std::vector<const void*> cf_keys(static_cast<std::size_t>(count));
		std::vector<const void*> cf_values(static_cast<std::size_t>(count));
		darling_windows_CFDictionaryGetKeysAndValues(dictionary,
			cf_keys.data(), cf_values.data());
		std::vector<id> keys;
		std::vector<id> values;
		try {
			keys.reserve(static_cast<std::size_t>(count));
			values.reserve(static_cast<std::size_t>(count));
			for (darling_windows_CFIndex index = 0; index < count; ++index) {
				id key = ObjcObjectFromCF(cf_keys[static_cast<std::size_t>(index)],
					depth + 1);
				id mapped = ObjcObjectFromCF(cf_values[static_cast<std::size_t>(index)],
					depth + 1);
				if (key == nullptr || mapped == nullptr) {
					if (key != nullptr) objc_release(key);
					if (mapped != nullptr) objc_release(mapped);
					throw std::runtime_error("unsupported CF dictionary value");
				}
				keys.push_back(key);
				values.push_back(mapped);
			}
		} catch (...) {
			for (id key : keys) objc_release(key);
			for (id mapped : values) objc_release(mapped);
			return nullptr;
		}
		id result = CreateNSDictionary(objc_getClass("NSDictionary"),
			values.data(), keys.data(), count);
		for (id key : keys) objc_release(key);
		for (id mapped : values) objc_release(mapped);
		return result;
	}
	return nullptr;
}

darling_windows_CFTypeRef KeyedArchiveField(
	darling_windows_CFDictionaryRef dictionary, const char* key_name)
{
	if (dictionary == nullptr || key_name == nullptr) return nullptr;
	const auto key = darling_windows_CFStringCreateWithCString(key_name);
	if (key == nullptr) return nullptr;
	const auto value = darling_windows_CFDictionaryGetValue(dictionary, key);
	darling_windows_CFRelease(key);
	return value;
}

bool KeyedArchiveString(darling_windows_CFTypeRef value, std::string& result)
{
	if (value == nullptr || darling_windows_CFGetTypeID(value) !=
		darling_windows_CFStringGetTypeID()) return false;
	try {
		std::vector<char> buffer(65536);
		if (!darling_windows_CFStringGetCString(
			static_cast<darling_windows_CFStringRef>(value), buffer.data(),
				static_cast<darling_windows_CFIndex>(buffer.size()))) return false;
		result.assign(buffer.data());
		return true;
	} catch (...) { return false; }
}

class KeyedArchiveObjectDecoder;
thread_local KeyedArchiveObjectDecoder* g_active_keyed_archive_decoder{};
bool ParseKeyedRectString(const char* text, DarlingObjcRect& rect);
bool ParseKeyedSizeString(const char* text, double& width, double& height);

id NSKeyedUnarchiverDecodeObjectForKey(id, SEL, id key);
bool NSKeyedUnarchiverContainsValueForKey(id, SEL, id key);
bool NSKeyedUnarchiverDecodeBoolForKey(id, SEL, id key);
int NSKeyedUnarchiverDecodeIntForKey(id, SEL, id key);
std::int64_t NSKeyedUnarchiverDecodeIntegerForKey(id, SEL, id key);
double NSKeyedUnarchiverDecodeDoubleForKey(id, SEL, id key);
float NSKeyedUnarchiverDecodeFloatForKey(id, SEL, id key);
std::int32_t NSKeyedUnarchiverDecodeInt32ForKey(id, SEL, id key);
std::int64_t NSKeyedUnarchiverDecodeInt64ForKey(id, SEL, id key);
std::uint32_t NSKeyedUnarchiverDecodeUInt32ForKey(id, SEL, id key);
std::uint64_t NSKeyedUnarchiverDecodeUInt64ForKey(id, SEL, id key);
id NSKeyedUnarchiverDecodeObjectOfClassForKey(id, SEL, Class expected, id key);

class KeyedArchiveObjectDecoder final {
public:
	explicit KeyedArchiveObjectDecoder(darling_windows_CFArrayRef objects,
		id unarchiver = nullptr) : objects_(objects), unarchiver_(unarchiver) {}
	~KeyedArchiveObjectDecoder()
	{
		for (const auto& [index, object] : decoded_) {
			(void)index;
			if (object != nullptr) objc_release(object);
		}
	}
	void SetCurrentContext(darling_windows_CFDictionaryRef context)
	{
		current_context_ = context;
	}
	darling_windows_CFDictionaryRef CurrentContext() const
	{
		return current_context_;
	}
	bool ContainsKey(id key) const
	{
		if (current_context_ == nullptr || key == nullptr) return false;
		const char* utf8 = NSStringUTF8String(key, nullptr);
		if (utf8 == nullptr) return false;
		const auto encoded = KeyedArchiveField(current_context_, utf8);
		return encoded != nullptr && !(darling_windows_CFGetTypeID(encoded) ==
			darling_windows_CFPropertyListUIDGetTypeID() && [&] {
				std::uint64_t index = 0;
				return darling_windows_CFPropertyListUIDGetValue(encoded, &index) && index == 0;
			}());
	}
	id DecodeForKey(id key)
	{
		if (current_context_ == nullptr || key == nullptr) return nullptr;
		const char* utf8 = NSStringUTF8String(key, nullptr);
		return utf8 == nullptr ? nullptr : Decode(KeyedArchiveField(current_context_, utf8));
	}

	id Decode(darling_windows_CFTypeRef value, unsigned depth = 0)
	{
		if (value == nullptr || depth > 128) return nullptr;
		if (darling_windows_CFGetTypeID(value) ==
			darling_windows_CFPropertyListUIDGetTypeID()) {
			std::uint64_t index = 0;
			if (!darling_windows_CFPropertyListUIDGetValue(value, &index))
				return nullptr;
			return DecodeIndex(index, depth + 1);
		}
		const auto type = darling_windows_CFGetTypeID(value);
		if (type == darling_windows_CFStringGetTypeID() ||
			type == darling_windows_CFNumberGetTypeID() ||
			type == darling_windows_CFBooleanGetTypeID() ||
			type == darling_windows_CFNullGetTypeID() ||
			type == darling_windows_CFDateGetTypeID() ||
			type == darling_windows_CFDataGetTypeID())
			return ObjcObjectFromCF(value, depth + 1);
		if (type == darling_windows_CFArrayGetTypeID())
			return DecodeArray(static_cast<darling_windows_CFArrayRef>(value),
				false, depth + 1);
		if (type == darling_windows_CFDictionaryGetTypeID())
			return DecodeDictionary(
				static_cast<darling_windows_CFDictionaryRef>(value), depth + 1);
		return nullptr;
	}
	void EstablishConnections()
	{
		const SEL establish = sel_registerName("establishConnection");
		for (const auto& [index, object] : decoded_) {
			(void)index;
			if (object == nullptr || !class_respondsToSelector(
				object_getClass(object), establish)) continue;
			const Class cls = object_getClass(object);
			const char* name = cls == nullptr ? nullptr : class_getName(cls);
			if (name != nullptr && (std::strcmp(name, "NSNibOutletConnector") == 0 ||
				std::strcmp(name, "NSNibControlConnector") == 0))
				darling_objc_msgSend_void0(object, establish);
		}
	}
	void PreserveDecodedObjects(std::vector<id>& lifetime_roots)
	{
		std::vector<id> retained;
		std::unordered_set<id> seen;
		try {
			retained.reserve(decoded_.size());
			seen.reserve(decoded_.size());
			for (const auto& [index, object] : decoded_) {
				(void)index;
				if (object != nullptr && seen.insert(object).second)
					retained.push_back(objc_retain(object));
			}
			lifetime_roots.reserve(lifetime_roots.size() + retained.size());
			for (id object : retained) lifetime_roots.push_back(object);
			retained.clear();
		} catch (...) {
			for (id object : retained) objc_release(object);
		}
	}
	void AwakeObjects()
	{
		const SEL awake = sel_registerName("awakeFromNib");
		std::vector<id> objects;
		std::unordered_set<id> seen;
		id owner_to_awake = nullptr;
		try {
			objects.reserve(decoded_.size());
			seen.reserve(decoded_.size());
			for (const auto& [index, object] : decoded_) {
				(void)index;
				if (object == nullptr || !class_respondsToSelector(
					object_getClass(object), awake)) continue;
				if (object == g_nib_owner) {
					if (owner_to_awake == nullptr)
						owner_to_awake = objc_retain(object);
					continue;
				}
				if (seen.insert(object).second)
					objects.push_back(objc_retain(object));
			}
			if (owner_to_awake == nullptr && g_nib_owner != nullptr &&
				class_respondsToSelector(object_getClass(g_nib_owner), awake))
				owner_to_awake = objc_retain(g_nib_owner);
		} catch (...) {
			for (id object : objects) objc_release(object);
			if (owner_to_awake != nullptr) objc_release(owner_to_awake);
			return;
		}
		for (id object : objects) {
			darling_objc_msgSend_void0(object, awake);
			objc_release(object);
		}
		if (owner_to_awake != nullptr) {
			darling_objc_msgSend_void0(owner_to_awake, awake);
			objc_release(owner_to_awake);
		}
	}

private:
	id DecodeIndex(std::uint64_t index, unsigned depth)
	{
		if (index == 0) return nullptr; // NSKeyedArchiver's reserved $null slot.
		if (index >= static_cast<std::uint64_t>(
			darling_windows_CFArrayGetCount(objects_))) return nullptr;
		const auto cached = decoded_.find(index);
		if (cached != decoded_.end())
			return cached->second == nullptr ? nullptr : objc_retain(cached->second);
		if (!active_.insert(index).second) return nullptr;
		const auto value = darling_windows_CFArrayGetValueAtIndex(
			objects_, static_cast<darling_windows_CFIndex>(index));
		if (value != nullptr && darling_windows_CFGetTypeID(value) ==
			darling_windows_CFDictionaryGetTypeID()) {
			std::string class_name;
			const auto dictionary = static_cast<darling_windows_CFDictionaryRef>(value);
			if (ClassName(dictionary, class_name)) {
				id container = nullptr;
				if (IsArrayClass(class_name))
					container = DecodeCyclicArray(index, dictionary, class_name, depth + 1);
				else if (IsDictionaryClass(class_name))
					container = DecodeCyclicDictionary(index, dictionary,
						class_name, depth + 1);
				else if (class_name == "NSIBObjectData")
					container = DecodeIBObjectData(index, dictionary, depth + 1);
				else if (class_name == "NSCustomObject")
					container = DecodeCustomObject(index, dictionary);
				else if (!IsFoundationArchiveClass(class_name))
					container = class_name == "NSCustomView" ?
						DecodeCustomView(index, dictionary) : DecodeCyclicCustomObject(index,
							dictionary, class_name);
				if (container != nullptr || IsArrayClass(class_name) ||
					IsDictionaryClass(class_name) || !IsFoundationArchiveClass(class_name)) {
					active_.erase(index);
					return container;
				}
			}
		}
		id result = Decode(value, depth + 1);
		active_.erase(index);
		if (result == nullptr) return nullptr;
		if (decoded_.find(index) != decoded_.end()) return result;
		id retained = objc_retain(result);
		try { decoded_.emplace(index, retained); }
		catch (...) {
			objc_release(retained);
			objc_release(result);
			return nullptr;
		}
		return result;
	}

	bool ClassName(darling_windows_CFDictionaryRef archive_object,
		std::string& name)
	{
		const auto class_reference = KeyedArchiveField(archive_object, "$class");
		std::uint64_t index = 0;
		if (class_reference == nullptr || !darling_windows_CFPropertyListUIDGetValue(
			class_reference, &index) || index >= static_cast<std::uint64_t>(
				darling_windows_CFArrayGetCount(objects_))) return false;
		const auto class_record = static_cast<darling_windows_CFDictionaryRef>(
			darling_windows_CFArrayGetValueAtIndex(objects_,
				static_cast<darling_windows_CFIndex>(index)));
		if (class_record == nullptr || darling_windows_CFGetTypeID(class_record) !=
			darling_windows_CFDictionaryGetTypeID()) return false;
		return KeyedArchiveString(KeyedArchiveField(class_record, "$classname"),
			name);
	}

	static bool IsArrayClass(const std::string& name)
	{
		return name == "NSArray" || name == "NSMutableArray" ||
			name == "__NSArrayI" || name == "__NSArrayM" ||
			name == "__NSFrozenArrayM";
	}
	static bool IsDictionaryClass(const std::string& name)
	{
		return name == "NSDictionary" || name == "NSMutableDictionary" ||
			name == "__NSDictionaryI" || name == "__NSDictionaryM" ||
			name == "__NSFrozenDictionaryM";
	}
	static bool IsFoundationArchiveClass(const std::string& name)
	{
		return name == "NSString" || name == "NSMutableString" ||
			name == "__NSCFString" || name == "__NSCFConstantString" ||
			name == "NSNumber" || name == "__NSCFNumber" ||
			name == "__NSCFBoolean" || name == "NSData" ||
			name == "NSMutableData" || name == "__NSCFData" ||
			name == "NSDate" || name == "__NSDate" || name == "NSNull" ||
			name == "__NSCFNull" || IsArrayClass(name) || IsDictionaryClass(name);
	}
	bool CachePlaceholder(std::uint64_t index, id object)
	{
		try { return decoded_.emplace(index, objc_retain(object)).second; }
		catch (...) { return false; }
	}
	void DiscardPlaceholder(std::uint64_t index, id object)
	{
		const auto found = decoded_.find(index);
		if (found != decoded_.end()) {
			id cached = found->second;
			decoded_.erase(found);
			if (cached != nullptr) objc_release(cached);
		}
		objc_release(object);
	}
	darling_windows_CFArrayRef ArchiveArrayElements(
		darling_windows_CFTypeRef encoded)
	{
		if (encoded == nullptr) return nullptr;
		darling_windows_CFTypeRef value = encoded;
		if (darling_windows_CFGetTypeID(value) ==
			darling_windows_CFPropertyListUIDGetTypeID()) {
			std::uint64_t index = 0;
			if (!darling_windows_CFPropertyListUIDGetValue(value, &index) ||
				index >= static_cast<std::uint64_t>(
					darling_windows_CFArrayGetCount(objects_))) return nullptr;
			value = darling_windows_CFArrayGetValueAtIndex(objects_,
				static_cast<darling_windows_CFIndex>(index));
		}
		if (value == nullptr) return nullptr;
		if (darling_windows_CFGetTypeID(value) ==
			darling_windows_CFDictionaryGetTypeID()) {
			std::string class_name;
			const auto dictionary =
				static_cast<darling_windows_CFDictionaryRef>(value);
			if (!ClassName(dictionary, class_name) || !IsArrayClass(class_name))
				return nullptr;
			value = KeyedArchiveField(dictionary, "NS.objects");
		}
		return value != nullptr && darling_windows_CFGetTypeID(value) ==
			darling_windows_CFArrayGetTypeID() ?
			static_cast<darling_windows_CFArrayRef>(value) : nullptr;
	}
	void SeedNibProxyReferences(darling_windows_CFDictionaryRef dictionary,
		unsigned depth)
	{
		const auto keys = ArchiveArrayElements(
			KeyedArchiveField(dictionary, "NSOidsKeys"));
		const auto values = ArchiveArrayElements(
			KeyedArchiveField(dictionary, "NSOidsValues"));
		if (keys == nullptr || values == nullptr) return;
		const auto count = darling_windows_CFArrayGetCount(keys);
		if (count < 0 || count > 100'000 ||
			darling_windows_CFArrayGetCount(values) != count) return;
		for (darling_windows_CFIndex offset = 0; offset < count; ++offset) {
			std::uint64_t object_index = 0;
			const auto object_reference = darling_windows_CFArrayGetValueAtIndex(
				keys, offset);
			if (object_reference == nullptr ||
				!darling_windows_CFPropertyListUIDGetValue(object_reference,
					&object_index) || object_index == 0 ||
				object_index >= static_cast<std::uint64_t>(
					darling_windows_CFArrayGetCount(objects_)) ||
				decoded_.find(object_index) != decoded_.end()) continue;
			id encoded_oid = Decode(darling_windows_CFArrayGetValueAtIndex(
				values, offset), depth + 1);
			if (encoded_oid == nullptr) continue;
			const int oid = darling_objc_msgSend_int0(encoded_oid,
				sel_registerName("intValue"));
			objc_release(encoded_oid);
			id replacement = oid == -1 ? g_nib_owner :
				oid == -2 ? NSNullSingleton(objc_getClass("NSNull"), nullptr) :
				oid == -3 ? NSApp : nullptr;
			if (replacement != nullptr) CachePlaceholder(object_index, replacement);
		}
	}
	void CollectNibTopLevelObjects(
		darling_windows_CFDictionaryRef dictionary, unsigned depth)
	{
		auto* load_context = g_current_nib_load_context;
		if (load_context == nullptr) return;
		const auto object_keys = ArchiveArrayElements(
			KeyedArchiveField(dictionary, "NSObjectsKeys"));
		const auto object_values = ArchiveArrayElements(
			KeyedArchiveField(dictionary, "NSObjectsValues"));
		const auto oid_keys = ArchiveArrayElements(
			KeyedArchiveField(dictionary, "NSOidsKeys"));
		const auto oid_values = ArchiveArrayElements(
			KeyedArchiveField(dictionary, "NSOidsValues"));
		if (object_keys == nullptr || object_values == nullptr ||
			oid_keys == nullptr || oid_values == nullptr) return;
		const auto object_count = darling_windows_CFArrayGetCount(object_keys);
		const auto oid_count = darling_windows_CFArrayGetCount(oid_keys);
		if (object_count < 0 || object_count > 100'000 ||
			darling_windows_CFArrayGetCount(object_values) != object_count ||
			oid_count < 0 || oid_count > 100'000 ||
			darling_windows_CFArrayGetCount(oid_values) != oid_count) return;
		id root = Decode(KeyedArchiveField(dictionary, "NSRoot"), depth + 1);
		std::unordered_map<std::uint64_t, int> object_ids;
		std::unordered_set<id> seen;
		try {
			object_ids.reserve(static_cast<std::size_t>(oid_count));
			seen.reserve(static_cast<std::size_t>(object_count));
			for (darling_windows_CFIndex offset = 0; offset < oid_count; ++offset) {
				std::uint64_t object_index = 0;
				const auto reference = darling_windows_CFArrayGetValueAtIndex(
					oid_keys, offset);
				if (reference == nullptr ||
					!darling_windows_CFPropertyListUIDGetValue(reference,
						&object_index)) continue;
				id value = Decode(darling_windows_CFArrayGetValueAtIndex(
					oid_values, offset), depth + 1);
				if (value == nullptr) continue;
				const int oid = darling_objc_msgSend_int0(value,
					sel_registerName("intValue"));
				objc_release(value);
				object_ids.emplace(object_index, oid);
			}
			load_context->top_level_objects.reserve(
				load_context->top_level_objects.size() +
				static_cast<std::size_t>(object_count));
			for (darling_windows_CFIndex offset = 0;
				offset < object_count; ++offset) {
				std::uint64_t object_index = 0;
				const auto object_reference = darling_windows_CFArrayGetValueAtIndex(
					object_keys, offset);
				if (object_reference == nullptr ||
					!darling_windows_CFPropertyListUIDGetValue(object_reference,
						&object_index)) continue;
				const auto oid = object_ids.find(object_index);
				if (oid == object_ids.end() || oid->second < 0) continue;
				id object = Decode(object_reference, depth + 1);
				id parent = Decode(darling_windows_CFArrayGetValueAtIndex(
					object_values, offset), depth + 1);
				const bool top_level = object != nullptr &&
					object != g_nib_owner && object != root &&
					(parent == g_nib_owner || parent == root);
				if (top_level && seen.insert(object).second)
					load_context->top_level_objects.push_back(objc_retain(object));
				if (object != nullptr) objc_release(object);
				if (parent != nullptr) objc_release(parent);
			}
		} catch (...) {
			for (id object : load_context->top_level_objects)
				objc_release(object);
			load_context->top_level_objects.clear();
		}
		if (root != nullptr) objc_release(root);
	}
	id DecodeIBObjectData(std::uint64_t index,
		darling_windows_CFDictionaryRef dictionary, unsigned depth)
	{
		// NSIBObjectData is the root container in classic compiled AppKit NIBs.
		// Its parallel maps are bookkeeping; materialize the root, all object
		// values, connectors, and explicitly visible windows from the archive.
		id placeholder = objc_retain(NSNullSingleton(objc_getClass("NSNull"),
			nullptr));
		if (placeholder == nullptr || !CachePlaceholder(index, placeholder)) {
			if (placeholder != nullptr) objc_release(placeholder);
			return nullptr;
		}
		SeedNibProxyReferences(dictionary, depth + 1);
		CollectNibTopLevelObjects(dictionary, depth + 1);
		for (const char* key : {"NSRoot", "NSObjectsValues",
			"NSConnections", "NSVisibleWindows"}) {
			const auto encoded = KeyedArchiveField(dictionary, key);
			if (encoded == nullptr) continue;
			id decoded = Decode(encoded, depth + 1);
			if (decoded != nullptr) {
				if (std::strcmp(key, "NSVisibleWindows") == 0 &&
					g_current_nib_load_context != nullptr) {
					auto& load_context = *g_current_nib_load_context;
					try {
						std::lock_guard lock(g_array_mutex);
						const auto found = g_arrays.find(decoded);
						if (found != g_arrays.end()) {
							load_context.has_visible_windows = true;
							load_context.visible_windows.reserve(
								found->second.size());
							for (id window : found->second)
								if (window != nullptr)
									load_context.visible_windows.push_back(
										objc_retain(window));
						}
					} catch (...) {
						for (id window : load_context.visible_windows)
							objc_release(window);
						load_context.visible_windows.clear();
						load_context.has_visible_windows = false;
					}
				}
				objc_release(decoded);
				continue;
			}
		darling_windows_CFTypeRef array_object = encoded;
			if (darling_windows_CFGetTypeID(array_object) ==
				darling_windows_CFPropertyListUIDGetTypeID()) {
				std::uint64_t array_index = 0;
				if (!darling_windows_CFPropertyListUIDGetValue(array_object,
					&array_index) || array_index >= static_cast<std::uint64_t>(
						darling_windows_CFArrayGetCount(objects_))) continue;
				array_object = darling_windows_CFArrayGetValueAtIndex(objects_,
					static_cast<darling_windows_CFIndex>(array_index));
			}
			if (array_object != nullptr && darling_windows_CFGetTypeID(array_object) ==
				darling_windows_CFDictionaryGetTypeID()) {
				std::string array_class;
				const auto array_dictionary =
					static_cast<darling_windows_CFDictionaryRef>(array_object);
				if (ClassName(array_dictionary, array_class) && IsArrayClass(array_class))
					array_object = KeyedArchiveField(array_dictionary, "NS.objects");
			}
			if (array_object != nullptr && darling_windows_CFGetTypeID(array_object) ==
				darling_windows_CFArrayGetTypeID()) {
				const auto array = static_cast<darling_windows_CFArrayRef>(array_object);
				const auto count = darling_windows_CFArrayGetCount(array);
				if (count < 0 || count > 100'000) continue;
				for (darling_windows_CFIndex member = 0; member < count; ++member) {
					id value = Decode(darling_windows_CFArrayGetValueAtIndex(array,
						member), depth + 1);
					if (value != nullptr) objc_release(value);
				}
			} else {
				id value = Decode(encoded, depth + 1);
				if (value != nullptr) objc_release(value);
			}
		}
		return placeholder;
	}
	id DecodeCustomObject(std::uint64_t index,
		darling_windows_CFDictionaryRef dictionary)
	{
		id encoded_class = Decode(KeyedArchiveField(dictionary, "NSClassName"));
		const char* class_name = NSStringUTF8String(encoded_class, nullptr);
		const std::string name = class_name == nullptr ? std::string{} :
			std::string(class_name);
		if (encoded_class != nullptr) objc_release(encoded_class);
		if (name == "FirstResponder") {
			id null_object = objc_retain(NSNullSingleton(objc_getClass("NSNull"),
				nullptr));
			if (!CachePlaceholder(index, null_object)) {
				objc_release(null_object);
				return nullptr;
			}
			return null_object;
		}
		if (name == "NSApplication") {
			id application = NSApplicationSharedApplication(
				objc_getClass("NSApplication"), sel_registerName("sharedApplication"));
			if (application == nullptr || !CachePlaceholder(index, application))
				return nullptr;
			return objc_retain(application);
		}
		const Class custom_class = name.empty() ? nullptr : objc_getClass(name.c_str());
		const Class root_class = objc_getClass("NSObject");
		bool is_object_class = false;
		for (Class current = custom_class; current != nullptr;
			current = class_getSuperclass(current))
			if (current == root_class) { is_object_class = true; break; }
		if (!is_object_class) return nullptr;
		id instance = class_createInstance(custom_class, 0);
		if (instance == nullptr || !CachePlaceholder(index, instance)) {
			if (instance != nullptr) objc_release(instance);
			return nullptr;
		}
		const auto previous_context = current_context_;
		current_context_ = dictionary;
		KeyedArchiveObjectDecoder* previous_decoder = g_active_keyed_archive_decoder;
		g_active_keyed_archive_decoder = this;
		id initialized = nullptr;
		try { initialized = darling_objc_msgSend_object0(instance,
			sel_registerName("init")); }
		catch (...) { initialized = nullptr; }
		g_active_keyed_archive_decoder = previous_decoder;
		current_context_ = previous_context;
		if (initialized != instance) {
			if (initialized != nullptr) objc_release(initialized);
			DiscardPlaceholder(index, instance);
			return nullptr;
		}
		return instance;
	}
	id DecodeCyclicArray(std::uint64_t index,
		darling_windows_CFDictionaryRef dictionary, const std::string& class_name,
		unsigned depth)
	{
		const auto refs_value = KeyedArchiveField(dictionary, "NS.objects");
		if (refs_value == nullptr || darling_windows_CFGetTypeID(refs_value) !=
			darling_windows_CFArrayGetTypeID()) return nullptr;
		const bool mutable_value = class_name == "NSMutableArray" ||
			class_name == "__NSArrayM";
		id result = CreateNSArray(objc_getClass(mutable_value ?
			"NSMutableArray" : "NSArray"), nullptr, 0);
		if (result == nullptr || !CachePlaceholder(index, result)) {
			if (result != nullptr) objc_release(result);
			return nullptr;
		}
		const auto refs = static_cast<darling_windows_CFArrayRef>(refs_value);
		const auto count = darling_windows_CFArrayGetCount(refs);
		bool valid = count >= 0 && count <= 100'000;
		for (darling_windows_CFIndex i = 0; valid && i < count; ++i) {
			id child = Decode(darling_windows_CFArrayGetValueAtIndex(refs, i), depth + 1);
			if (child == nullptr) { valid = false; break; }
			try {
				std::lock_guard lock(g_array_mutex);
				auto found = g_arrays.find(result);
				if (found == g_arrays.end() || found->second.size() >= 100'000)
					valid = false;
				else {
					id retained = objc_retain(child);
					try { found->second.push_back(retained); }
					catch (...) { objc_release(retained); throw; }
				}
			} catch (...) { valid = false; }
			objc_release(child);
		}
		if (!valid) { DiscardPlaceholder(index, result); return nullptr; }
		return result;
	}
	id DecodeCyclicDictionary(std::uint64_t index,
		darling_windows_CFDictionaryRef dictionary, const std::string& class_name,
		unsigned depth)
	{
		const auto key_value = KeyedArchiveField(dictionary, "NS.keys");
		const auto object_value = KeyedArchiveField(dictionary, "NS.objects");
		if (key_value == nullptr || object_value == nullptr ||
			darling_windows_CFGetTypeID(key_value) != darling_windows_CFArrayGetTypeID() ||
			darling_windows_CFGetTypeID(object_value) != darling_windows_CFArrayGetTypeID())
			return nullptr;
		const bool mutable_value = class_name == "NSMutableDictionary" ||
			class_name == "__NSDictionaryM";
		id result = CreateNSDictionary(objc_getClass(mutable_value ?
			"NSMutableDictionary" : "NSDictionary"), nullptr, nullptr, 0);
		if (result == nullptr || !CachePlaceholder(index, result)) {
			if (result != nullptr) objc_release(result);
			return nullptr;
		}
		const auto keys = static_cast<darling_windows_CFArrayRef>(key_value);
		const auto values = static_cast<darling_windows_CFArrayRef>(object_value);
		const auto count = darling_windows_CFArrayGetCount(keys);
		bool valid = count >= 0 && count <= 100'000 &&
			darling_windows_CFArrayGetCount(values) == count;
		for (darling_windows_CFIndex i = 0; valid && i < count; ++i) {
			id key = Decode(darling_windows_CFArrayGetValueAtIndex(keys, i), depth + 1);
			id value = Decode(darling_windows_CFArrayGetValueAtIndex(values, i), depth + 1);
			if (key == nullptr || value == nullptr) {
				if (key != nullptr) objc_release(key);
				if (value != nullptr) objc_release(value);
				valid = false;
				break;
			}
			try {
				std::lock_guard lock(g_dictionary_mutex);
				auto found = g_dictionaries.find(result);
				if (found == g_dictionaries.end() || found->second.size() >= 100'000)
					valid = false;
				else {
					id retained_key = objc_retain(key);
					id retained_value = objc_retain(value);
					try { found->second.emplace_back(retained_key, retained_value); }
					catch (...) {
						objc_release(retained_key);
						objc_release(retained_value);
						throw;
					}
				}
			} catch (...) { valid = false; }
			objc_release(key);
			objc_release(value);
		}
		if (!valid) { DiscardPlaceholder(index, result); return nullptr; }
		return result;
	}
	id DecodeCyclicCustomObject(std::uint64_t index,
		darling_windows_CFDictionaryRef dictionary, const std::string& class_name)
	{
		// Interface Builder archives encode windows, menus, and views as private
		// template classes. Materialize them as the requested public AppKit class.
		const bool window_template = class_name == "NSWindowTemplate";
		const bool menu_template = class_name == "NSMenuTemplate";
		const bool view_template = class_name == "NSViewTemplate" ||
			class_name == "NSTextViewTemplate";
		Class custom_class = objc_getClass(window_template ? "NSWindow" :
			menu_template ? "NSMenu" : view_template ? "NSView" :
			class_name.c_str());
		if (window_template) {
			id encoded_window_class = Decode(
				KeyedArchiveField(dictionary, "NSWindowClass"));
			const char* window_class_name = NSStringUTF8String(encoded_window_class,
				nullptr);
			const Class requested_class = window_class_name == nullptr ? nullptr :
				objc_getClass(window_class_name);
			if (encoded_window_class != nullptr) objc_release(encoded_window_class);
			const Class window_base = objc_getClass("NSWindow");
			for (Class current = requested_class; current != nullptr;
				current = class_getSuperclass(current)) {
				if (current == window_base) {
					custom_class = requested_class;
					break;
				}
			}
		}
		if (menu_template) {
			id encoded_menu_class = Decode(
				KeyedArchiveField(dictionary, "NSMenuClass"));
			const char* menu_class_name = NSStringUTF8String(encoded_menu_class,
				nullptr);
			const Class requested_class = menu_class_name == nullptr ? nullptr :
				objc_getClass(menu_class_name);
			if (encoded_menu_class != nullptr) objc_release(encoded_menu_class);
			const Class menu_base = objc_getClass("NSMenu");
			for (Class current = requested_class; current != nullptr;
				current = class_getSuperclass(current)) {
				if (current == menu_base) {
					custom_class = requested_class;
					break;
				}
			}
		}
		if (view_template) {
			id encoded_view_class = Decode(
				KeyedArchiveField(dictionary, "NSClassName"));
			const char* view_class_name = NSStringUTF8String(encoded_view_class,
				nullptr);
			const Class requested_class = view_class_name == nullptr ? nullptr :
				objc_getClass(view_class_name);
			if (encoded_view_class != nullptr) objc_release(encoded_view_class);
			const Class view_base = objc_getClass("NSView");
			bool is_view_class = false;
			for (Class current = requested_class; current != nullptr;
				current = class_getSuperclass(current)) {
				if (current == view_base) {
					is_view_class = true;
					break;
				}
			}
			if (!is_view_class) return nullptr;
			custom_class = requested_class;
		}
		const SEL init_with_coder = sel_registerName("initWithCoder:");
		if (custom_class == nullptr || !class_respondsToSelector(custom_class,
			init_with_coder)) return nullptr;
		id instance = class_createInstance(custom_class, 0);
		if (instance == nullptr || !CachePlaceholder(index, instance)) {
			if (instance != nullptr) objc_release(instance);
			return nullptr;
		}
		const auto previous_context = current_context_;
		current_context_ = dictionary;
		KeyedArchiveObjectDecoder* previous_decoder = g_active_keyed_archive_decoder;
		g_active_keyed_archive_decoder = this;
		id initialized = nullptr;
		try { initialized = darling_objc_msgSend_object1(instance,
			init_with_coder, unarchiver_); }
		catch (...) { initialized = nullptr; }
		g_active_keyed_archive_decoder = previous_decoder;
		current_context_ = previous_context;
		if (initialized != instance) {
			if (initialized != nullptr) objc_release(initialized);
			DiscardPlaceholder(index, instance);
			return nullptr;
		}
		return instance;
	}
	id DecodeCustomView(std::uint64_t index,
		darling_windows_CFDictionaryRef dictionary)
	{
		id encoded_class = Decode(KeyedArchiveField(dictionary, "NSClassName"));
		const char* class_name = NSStringUTF8String(encoded_class, nullptr);
		const Class custom_class = class_name == nullptr ? nullptr :
			objc_getClass(class_name);
		if (encoded_class != nullptr) objc_release(encoded_class);
		const Class view_class = objc_getClass("NSView");
		bool is_view = false;
		for (Class current = custom_class; current != nullptr;
			current = class_getSuperclass(current))
			if (current == view_class) { is_view = true; break; }
		const SEL init_with_frame = sel_registerName("initWithFrame:");
		if (!is_view || !class_respondsToSelector(custom_class, init_with_frame))
			return nullptr;

		DarlingObjcRect frame{};
		id encoded_frame = Decode(KeyedArchiveField(dictionary, "NSFrame"));
		const char* frame_text = NSStringUTF8String(encoded_frame, nullptr);
		const bool has_frame = ParseKeyedRectString(frame_text, frame);
		if (encoded_frame != nullptr) objc_release(encoded_frame);
		if (!has_frame) {
			id encoded_size = Decode(KeyedArchiveField(dictionary, "NSFrameSize"));
			const char* size_text = NSStringUTF8String(encoded_size, nullptr);
			double width = 0.0;
			double height = 0.0;
			const bool has_size = ParseKeyedSizeString(size_text, width, height);
			if (encoded_size != nullptr) objc_release(encoded_size);
			if (!has_size) return nullptr;
			frame = DarlingObjcRect{0.0, 0.0, width, height};
		}
		id allocated = class_createInstance(custom_class, 0);
		if (allocated == nullptr) return nullptr;
		id view = darling_objc_msgSend_object_rect1(allocated, init_with_frame,
			frame);
		if (view == nullptr) {
			objc_release(allocated);
			return nullptr;
		}
		NSViewInitWithFrame(view, init_with_frame, frame);
		if (!CachePlaceholder(index, view)) {
			objc_release(view);
			return nullptr;
		}
		const auto bounds_value = KeyedArchiveField(dictionary, "NSBounds");
		if (bounds_value != nullptr) {
			id encoded_bounds = Decode(bounds_value);
			const char* bounds_text = NSStringUTF8String(encoded_bounds, nullptr);
			DarlingObjcRect bounds{};
			if (ParseKeyedRectString(bounds_text, bounds))
				NSViewSetBounds(view, sel_registerName("setBounds:"), bounds);
			if (encoded_bounds != nullptr) objc_release(encoded_bounds);
		}
		if (const auto mask_value = KeyedArchiveField(dictionary,
			"NSAutoresizingMask"); mask_value != nullptr) {
			std::int64_t mask = 0;
			darling_windows_CFNumberGetInteger(mask_value, &mask);
			NSViewSetAutoresizingMask(view, nullptr,
				static_cast<std::uint64_t>(mask));
		}
		const auto subviews_value = KeyedArchiveField(dictionary, "NSSubviews");
		if (subviews_value != nullptr) {
			id subviews = Decode(subviews_value);
			std::vector<id> children;
			try {
				std::lock_guard lock(g_array_mutex);
				const auto found = g_arrays.find(subviews);
				if (found != g_arrays.end()) {
					children.reserve(found->second.size());
					for (id child : found->second) children.push_back(objc_retain(child));
				}
			} catch (...) {
				for (id child : children) objc_release(child);
				children.clear();
			}
			if (subviews != nullptr) objc_release(subviews);
			for (id child : children) {
				if (child != view)
					NSViewAddSubview(view, sel_registerName("addSubview:"), child);
				objc_release(child);
			}
		}
		std::int64_t flags = 0;
		if (const auto flags_value = KeyedArchiveField(dictionary, "NSvFlags");
			flags_value != nullptr)
			darling_windows_CFNumberGetInteger(flags_value, &flags);
		NSViewSetHidden(view, sel_registerName("setHidden:"),
			(static_cast<std::uint32_t>(flags) & 0x80000000u) != 0);
		const auto next_view_value = KeyedArchiveField(dictionary, "NSNextKeyView");
		if (next_view_value != nullptr) {
			id next_view = Decode(next_view_value);
			if (next_view != nullptr) {
				NSViewSetNextKeyView(view, sel_registerName("setNextKeyView:"), next_view);
				objc_release(next_view);
			}
		}
		return view;
	}

	id DecodeArray(darling_windows_CFArrayRef references, bool mutable_value,
		unsigned depth)
	{
		const auto count = darling_windows_CFArrayGetCount(references);
		if (count < 0 || count > 100'000) return nullptr;
		std::vector<id> values;
		try {
			values.reserve(static_cast<std::size_t>(count));
			for (darling_windows_CFIndex index = 0; index < count; ++index) {
				id value = Decode(darling_windows_CFArrayGetValueAtIndex(
					references, index), depth + 1);
				if (value == nullptr) throw std::runtime_error(
					"null or unsupported keyed archive array member");
				values.push_back(value);
			}
		} catch (...) {
			for (id value : values) objc_release(value);
			return nullptr;
		}
		const Class cls = objc_getClass(mutable_value ? "NSMutableArray" : "NSArray");
		id result = CreateNSArray(cls, values.data(), values.size());
		for (id value : values) objc_release(value);
		return result;
	}

	id DecodeDictionary(darling_windows_CFDictionaryRef dictionary,
		unsigned depth)
	{
		std::string class_name;
		if (ClassName(dictionary, class_name)) {
			const auto field = [dictionary](const char* key) {
				return KeyedArchiveField(dictionary, key);
			};
			const auto is_class = [&class_name](
				std::initializer_list<const char*> names) {
				for (const char* name : names)
					if (std::strcmp(class_name.c_str(), name) == 0) return true;
				return false;
			};
			if (is_class({"NSString", "NSMutableString", "__NSCFString",
				"__NSCFConstantString"})) {
				id decoded = Decode(field("NS.string"), depth + 1);
				if (decoded == nullptr || class_name != "NSMutableString")
					return decoded;
				const char* utf8 = NSStringUTF8String(decoded, nullptr);
				id mutable_string = utf8 == nullptr ? nullptr : CreateMutableNSString(
					objc_getClass("NSMutableString"), utf8);
				objc_release(decoded);
				return mutable_string;
			}
			if (is_class({"NSNumber", "__NSCFNumber", "__NSCFBoolean"})) {
				for (const char* key : {"NS.boolval", "NS.intval", "NS.integer",
					"NS.dblval", "NS.floatval", "NS.real"}) {
					if (const auto encoded = field(key); encoded != nullptr)
						return Decode(encoded, depth + 1);
				}
				return nullptr;
			}
			if (is_class({"NSData", "NSMutableData", "__NSCFData"})) {
				const auto encoded = field("NS.data");
				if (encoded != nullptr && darling_windows_CFGetTypeID(encoded) ==
					darling_windows_CFDataGetTypeID()) {
					const auto data = static_cast<darling_windows_CFDataRef>(encoded);
					const auto length = darling_windows_CFDataGetLength(data);
					if (length < 0 || length > 256ll * 1024 * 1024) return nullptr;
					const bool mutable_value = class_name == "NSMutableData";
					return CreateNSData(objc_getClass(mutable_value ?
						"NSMutableData" : "NSData"),
						darling_windows_CFDataGetBytePtr(data),
						static_cast<std::uint64_t>(length));
				}
				return Decode(encoded, depth + 1);
			}
			if (is_class({"NSDate", "__NSDate"})) {
				double absolute_time = 0.0;
				const auto encoded = field("NS.time");
				if (encoded == nullptr || !darling_windows_CFNumberGetDouble(
					encoded, &absolute_time) || !std::isfinite(absolute_time))
					return nullptr;
				return CreateNSDate(objc_getClass("NSDate"), absolute_time);
			}
			if (is_class({"NSNull", "__NSCFNull"}))
				return objc_retain(NSNullSingleton(objc_getClass("NSNull"), nullptr));
			if (is_class({"NSArray", "NSMutableArray", "__NSArrayI",
				"__NSArrayM", "__NSFrozenArrayM"})) {
				const auto encoded_references = field("NS.objects");
				if (encoded_references == nullptr || darling_windows_CFGetTypeID(
					encoded_references) != darling_windows_CFArrayGetTypeID())
					return nullptr;
				const auto references = static_cast<darling_windows_CFArrayRef>(
					encoded_references);
				return DecodeArray(references,
					class_name == "NSMutableArray" || class_name == "__NSArrayM",
					depth + 1);
			}
			if (is_class({"NSDictionary", "NSMutableDictionary", "__NSDictionaryI",
				"__NSDictionaryM", "__NSFrozenDictionaryM"})) {
				const auto keys = static_cast<darling_windows_CFArrayRef>(
					field("NS.keys"));
				const auto values = static_cast<darling_windows_CFArrayRef>(
					field("NS.objects"));
				if (keys == nullptr || values == nullptr ||
					darling_windows_CFGetTypeID(keys) != darling_windows_CFArrayGetTypeID() ||
					darling_windows_CFGetTypeID(values) != darling_windows_CFArrayGetTypeID())
					return nullptr;
				const auto count = darling_windows_CFArrayGetCount(keys);
				if (count < 0 || count > 100'000 ||
					darling_windows_CFArrayGetCount(values) != count) return nullptr;
				std::vector<id> decoded_keys;
				std::vector<id> decoded_values;
				try {
					decoded_keys.reserve(static_cast<std::size_t>(count));
					decoded_values.reserve(static_cast<std::size_t>(count));
					for (darling_windows_CFIndex index = 0; index < count; ++index) {
						id key = Decode(darling_windows_CFArrayGetValueAtIndex(keys,
							index), depth + 1);
						id value = Decode(darling_windows_CFArrayGetValueAtIndex(values,
							index), depth + 1);
						if (key == nullptr || value == nullptr) {
							if (key != nullptr) objc_release(key);
							if (value != nullptr) objc_release(value);
						throw std::runtime_error(
							"null or unsupported keyed archive dictionary member");
						}
						decoded_keys.push_back(key);
						decoded_values.push_back(value);
					}
				} catch (...) {
					for (id value : decoded_keys) objc_release(value);
					for (id value : decoded_values) objc_release(value);
					return nullptr;
				}
				const bool mutable_value = class_name == "NSMutableDictionary" ||
					class_name == "__NSDictionaryM";
				const Class cls = objc_getClass(mutable_value ?
					"NSMutableDictionary" : "NSDictionary");
				id result = CreateNSDictionary(cls, decoded_values.data(),
					decoded_keys.data(), decoded_keys.size());
				for (id value : decoded_keys) objc_release(value);
				for (id value : decoded_values) objc_release(value);
				return result;
			}
			const Class custom_class = objc_getClass(class_name.c_str());
			const SEL init_with_coder = sel_registerName("initWithCoder:");
			if (custom_class == nullptr || !class_respondsToSelector(custom_class,
				init_with_coder)) return nullptr;
			id instance = class_createInstance(custom_class, 0);
			if (instance == nullptr) return nullptr;
			const auto previous_context = current_context_;
			current_context_ = dictionary;
			KeyedArchiveObjectDecoder* previous_decoder = g_active_keyed_archive_decoder;
			g_active_keyed_archive_decoder = this;
			id initialized = nullptr;
			try { initialized = darling_objc_msgSend_object1(instance,
				init_with_coder, unarchiver_); }
			catch (...) { initialized = nullptr; }
			g_active_keyed_archive_decoder = previous_decoder;
			current_context_ = previous_context;
			if (initialized == nullptr) {
				objc_release(instance);
				return nullptr;
			}
			return initialized;
		}

		const auto count = darling_windows_CFDictionaryGetCount(dictionary);
		if (count < 0 || count > 100'000) return nullptr;
		std::vector<const void*> cf_keys(static_cast<std::size_t>(count));
		std::vector<const void*> cf_values(static_cast<std::size_t>(count));
		darling_windows_CFDictionaryGetKeysAndValues(dictionary,
			cf_keys.data(), cf_values.data());
		std::vector<id> keys;
		std::vector<id> values;
		try {
			keys.reserve(static_cast<std::size_t>(count));
			values.reserve(static_cast<std::size_t>(count));
			for (darling_windows_CFIndex index = 0; index < count; ++index) {
				id key = Decode(static_cast<darling_windows_CFTypeRef>(
					cf_keys[static_cast<std::size_t>(index)]), depth + 1);
				id value = Decode(static_cast<darling_windows_CFTypeRef>(
					cf_values[static_cast<std::size_t>(index)]), depth + 1);
				if (key == nullptr || value == nullptr) {
					if (key != nullptr) objc_release(key);
					if (value != nullptr) objc_release(value);
					throw std::runtime_error("unsupported property-list dictionary member");
				}
				keys.push_back(key);
				values.push_back(value);
			}
		} catch (...) {
			for (id key : keys) objc_release(key);
			for (id value : values) objc_release(value);
			return nullptr;
		}
		id result = CreateNSDictionary(objc_getClass("NSDictionary"),
			values.data(), keys.data(), keys.size());
		for (id key : keys) objc_release(key);
		for (id value : values) objc_release(value);
		return result;
	}

	darling_windows_CFArrayRef objects_{};
	darling_windows_CFDictionaryRef current_context_{};
	id unarchiver_{};
	std::unordered_map<std::uint64_t, id> decoded_;
	std::unordered_set<std::uint64_t> active_;
};

id NSKeyedUnarchiverDecodeObjectForKey(id, SEL, id key)
{
	return g_active_keyed_archive_decoder == nullptr ? nullptr :
		objc_autorelease(g_active_keyed_archive_decoder->DecodeForKey(key));
}
bool NSKeyedUnarchiverContainsValueForKey(id, SEL, id key)
{
	return g_active_keyed_archive_decoder != nullptr &&
		g_active_keyed_archive_decoder->ContainsKey(key);
}
bool NSKeyedUnarchiverDecodeBoolForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return false;
	const bool result = darling_objc_msgSend_bool0(value,
		sel_registerName("boolValue"));
	objc_release(value);
	return result;
}
int NSKeyedUnarchiverDecodeIntForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return 0;
	const int result = darling_objc_msgSend_int0(value, sel_registerName("intValue"));
	objc_release(value);
	return result;
}
std::int64_t NSKeyedUnarchiverDecodeIntegerForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return 0;
	const auto result = darling_objc_msgSend_int64_0(value,
		sel_registerName("integerValue"));
	objc_release(value);
	return result;
}
double NSKeyedUnarchiverDecodeDoubleForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return 0.0;
	const double result = darling_objc_msgSend_double0(value,
		sel_registerName("doubleValue"));
	objc_release(value);
	return result;
}
float NSKeyedUnarchiverDecodeFloatForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return 0.0f;
	const float result = darling_objc_msgSend_float0(value,
		sel_registerName("floatValue"));
	objc_release(value);
	return result;
}
std::int32_t NSKeyedUnarchiverDecodeInt32ForKey(id, SEL, id key)
{
	return static_cast<std::int32_t>(NSKeyedUnarchiverDecodeIntForKey(nullptr,
		nullptr, key));
}
std::int64_t NSKeyedUnarchiverDecodeInt64ForKey(id, SEL, id key)
{
	return NSKeyedUnarchiverDecodeIntegerForKey(nullptr, nullptr, key);
}
std::uint32_t NSKeyedUnarchiverDecodeUInt32ForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return 0;
	const auto result = darling_objc_msgSend_uint0(value,
		sel_registerName("unsignedIntegerValue"));
	objc_release(value);
	return result;
}
std::uint64_t NSKeyedUnarchiverDecodeUInt64ForKey(id, SEL, id key)
{
	id value = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return 0;
	const auto result = darling_objc_msgSend_uint64_0(value,
		sel_registerName("unsignedIntegerValue"));
	objc_release(value);
	return result;
}
id NSKeyedUnarchiverDecodeObjectOfClassForKey(id, SEL, Class expected, id key)
{
	id decoded = NSKeyedUnarchiverDecodeObjectForKey(nullptr, nullptr, key);
	if (decoded == nullptr || expected == nullptr) return decoded;
	for (Class actual = object_getClass(decoded); actual != nullptr;
		actual = class_getSuperclass(actual)) {
		if (actual == expected) return decoded;
	}
	return nullptr;
}

bool NSKeyedUnarchiverAllowsKeyedCoding(id, SEL)
{
	return true;
}

bool ParseKeyedRectString(const char* text, DarlingObjcRect& rect)
{
	if (text == nullptr) return false;
	const char* cursor = text;
	double values[4]{};
	for (double& value : values) {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
			*cursor == '\n' || *cursor == '{' || *cursor == '}' || *cursor == ',')
			++cursor;
		char* end = nullptr;
		value = std::strtod(cursor, &end);
		if (end == cursor || !std::isfinite(value)) return false;
		cursor = end;
	}
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
		*cursor == '\n' || *cursor == '{' || *cursor == '}' || *cursor == ',')
		++cursor;
	if (*cursor != '\0') return false;
	rect = DarlingObjcRect{values[0], values[1], values[2], values[3]};
	return true;
}

bool ParseKeyedSizeString(const char* text, double& width, double& height)
{
	if (text == nullptr) return false;
	const char* cursor = text;
	double values[2]{};
	for (double& value : values) {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
			*cursor == '\n' || *cursor == '{' || *cursor == '}' || *cursor == ',')
			++cursor;
		char* end = nullptr;
		value = std::strtod(cursor, &end);
		if (end == cursor || !std::isfinite(value)) return false;
		cursor = end;
	}
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
		*cursor == '\n' || *cursor == '{' || *cursor == '}' || *cursor == ',')
		++cursor;
	if (*cursor != '\0') return false;
	width = values[0];
	height = values[1];
	return true;
}

DarlingObjcRect NSKeyedArchiveDecodeRectForKey(id key)
{
	DarlingObjcRect result{};
	if (g_active_keyed_archive_decoder == nullptr) return result;
	id value = g_active_keyed_archive_decoder->DecodeForKey(key);
	if (value == nullptr) return result;
	const char* text = NSStringUTF8String(value, nullptr);
	ParseKeyedRectString(text, result);
	objc_release(value);
	return result;
}

id NSNibConnectorInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr ||
		g_active_keyed_archive_decoder == nullptr) return nullptr;
	auto decode_field = [](const char* name) -> id {
		id key = CreateDynamicNSString(objc_getClass("NSString"), name);
		if (key == nullptr) return nullptr;
		id value = g_active_keyed_archive_decoder->DecodeForKey(key);
		objc_release(key);
		return value;
	};
	id source = decode_field("NSSource");
	id destination = decode_field("NSDestination");
	id label = decode_field("NSLabel");
	const Class cls = object_getClass(self);
	const char* class_name = cls == nullptr ? nullptr : class_getName(cls);
	AppKitNibConnectorState state{};
	state.source = source;
	state.destination = destination;
	state.label = label;
	state.control = class_name != nullptr &&
		std::strcmp(class_name, "NSNibControlConnector") == 0;
	try {
		std::lock_guard lock(g_nib_connectors_mutex);
		g_nib_connectors.emplace(self, state);
	} catch (...) {
		if (source != nullptr) objc_release(source);
		if (destination != nullptr) objc_release(destination);
		if (label != nullptr) objc_release(label);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(self, &NSNibConnectorFinalize)) {
		NSNibConnectorFinalize(self);
		return nullptr;
	}
	return self;
}

void NSNibConnectorEstablishConnection(id self, SEL)
{
	AppKitNibConnectorState state{};
	{
		std::lock_guard lock(g_nib_connectors_mutex);
		const auto found = g_nib_connectors.find(self);
		if (found == g_nib_connectors.end()) return;
		state = found->second;
		if (state.source != nullptr) objc_retain(state.source);
		if (state.destination != nullptr) objc_retain(state.destination);
		if (state.label != nullptr) objc_retain(state.label);
	}
	const char* label = NSStringUTF8String(state.label, nullptr);
	id destination = state.destination;
	for (Class current = destination == nullptr ? nullptr :
		object_getClass(destination); current != nullptr;
		current = class_getSuperclass(current)) {
		const char* name = class_getName(current);
		if (name != nullptr && std::strcmp(name, "NSNull") == 0) {
			destination = nullptr;
			break;
		}
	}
	if (state.source != nullptr && label != nullptr && label[0] != '\0') {
		if (state.control) {
			std::string action_name(label);
			if (action_name.find(':') == std::string::npos)
				action_name.push_back(':');
			const SEL action = sel_registerName(action_name.c_str());
			Class source_class = object_getClass(state.source);
			bool menu_item = false;
			for (Class current = source_class; current != nullptr;
				current = class_getSuperclass(current)) {
				const char* name = class_getName(current);
				if (name != nullptr && std::strcmp(name, "NSMenuItem") == 0) {
					menu_item = true;
					break;
				}
			}
			if (menu_item) {
				NSMenuItemSetTarget(state.source,
					sel_registerName("setTarget:"), destination);
				NSMenuItemSetAction(state.source,
					sel_registerName("setAction:"), action);
			} else if (IsViewKindOf(state.source, "NSControl")) {
				NSButtonSetTarget(state.source,
					sel_registerName("setTarget:"), destination);
				NSButtonSetAction(state.source,
					sel_registerName("setAction:"), action);
			}
		} else {
			std::string setter("set");
			setter.append(label);
			if (setter.back() != ':') setter.push_back(':');
			if (setter.size() > 3 && setter[3] >= 'a' && setter[3] <= 'z')
				setter[3] = static_cast<char>(setter[3] - ('a' - 'A'));
			const SEL outlet_setter = sel_registerName(setter.c_str());
			if (class_getInstanceMethod(object_getClass(state.source),
				outlet_setter) != nullptr)
				darling_objc_msgSend_object1(state.source, outlet_setter,
					state.destination);
			else {
				std::array<std::string, 2> ivar_names{
					std::string(label), std::string("_") + label};
				for (const auto& ivar_name : ivar_names) {
					const Ivar ivar = class_getInstanceVariable(
						object_getClass(state.source), ivar_name.c_str());
					const char* encoding = ivar == nullptr ? nullptr :
						ivar_getTypeEncoding(ivar);
					if (encoding == nullptr || encoding[0] != '@' ||
						encoding[1] == '?') continue;
					if (object_getIvar(state.source, ivar) == state.destination)
						break;
					object_setIvar(state.source, ivar, state.destination);
					break;
				}
			}
		}
	}
	if (state.source != nullptr) objc_release(state.source);
	if (state.destination != nullptr) objc_release(state.destination);
	if (state.label != nullptr) objc_release(state.label);
}

void NSNibConnectorFinalize(id object)
{
	AppKitNibConnectorState state{};
	{
		std::lock_guard lock(g_nib_connectors_mutex);
		const auto found = g_nib_connectors.find(object);
		if (found == g_nib_connectors.end()) return;
		state = found->second;
		g_nib_connectors.erase(found);
	}
	if (state.source != nullptr) objc_release(state.source);
	if (state.destination != nullptr) objc_release(state.destination);
	if (state.label != nullptr) objc_release(state.label);
}

id NSKeyedUnarchiverUnarchiveObjectWithData(Class, SEL, id data)
{
	if (data == nullptr) return nullptr;
	const Class data_class = object_getClass(data);
	const char* data_class_name = data_class == nullptr ? nullptr :
		class_getName(data_class);
	if (data_class_name == nullptr ||
		(std::strcmp(data_class_name, "NSData") != 0 &&
		 std::strcmp(data_class_name, "NSMutableData") != 0 &&
		 std::strcmp(data_class_name, "__NSCFData") != 0)) return nullptr;
	std::vector<std::uint8_t> archive_bytes;
	try {
		std::lock_guard lock(g_data_mutex);
		const auto found = g_data.find(data);
		if (found == g_data.end() || found->second.empty() ||
			found->second.size() > 256ull * 1024 * 1024) return nullptr;
		archive_bytes = found->second;
	} catch (...) { return nullptr; }
	const auto archive = darling_windows_CFPropertyListCreateFromBinary(
		archive_bytes.data(), static_cast<darling_windows_CFIndex>(archive_bytes.size()));
	if (archive == nullptr || darling_windows_CFGetTypeID(archive) !=
		darling_windows_CFDictionaryGetTypeID()) {
		darling_windows_CFRelease(archive);
		return nullptr;
	}
	id result = nullptr;
	try {
		const auto archive_dict = static_cast<darling_windows_CFDictionaryRef>(archive);
		std::string archiver_name;
		const auto archiver = KeyedArchiveField(archive_dict, "$archiver");
		const auto objects = static_cast<darling_windows_CFArrayRef>(
			KeyedArchiveField(archive_dict, "$objects"));
		const auto top = static_cast<darling_windows_CFDictionaryRef>(
			KeyedArchiveField(archive_dict, "$top"));
		if (KeyedArchiveString(archiver, archiver_name) &&
			archiver_name == "NSKeyedArchiver" && objects != nullptr && top != nullptr &&
			darling_windows_CFGetTypeID(objects) == darling_windows_CFArrayGetTypeID() &&
			darling_windows_CFGetTypeID(top) == darling_windows_CFDictionaryGetTypeID()) {
			const auto root = KeyedArchiveField(top, "root");
			if (root != nullptr) {
				id unarchiver = class_createInstance(objc_getClass("NSKeyedUnarchiver"), 0);
				if (unarchiver == nullptr) throw std::bad_alloc();
				try {
					KeyedArchiveObjectDecoder decoder(objects, unarchiver);
					result = decoder.Decode(root);
					if (result != nullptr) {
						decoder.EstablishConnections();
						if (g_loading_nib) {
							decoder.AwakeObjects();
							auto* context = g_current_nib_load_context;
							if (context != nullptr &&
								!context->retain_graph_for_lifetime) {
								decoder.PreserveDecodedObjects(
									context->decoded_objects);
							} else {
								std::lock_guard lock(g_loaded_nib_objects_mutex);
								decoder.PreserveDecodedObjects(
									g_loaded_nib_objects);
							}
						}
					}
				} catch (...) {
					objc_release(unarchiver);
					throw;
				}
				objc_release(unarchiver);
			}
		}
	} catch (...) { result = nullptr; }
	darling_windows_CFRelease(archive);
	return result == nullptr ? nullptr : objc_autorelease(result);
}

darling_windows_CFTypeRef CFObjectFromObjc(id object, unsigned depth = 0)
{
	if (object == nullptr || depth > 32) return nullptr;
	const Class cls = object_getClass(object);
	const char* name = cls == nullptr ? nullptr : class_getName(cls);
	if (IsSupportedNSStringClass(cls))
		return NSStringToCFString(object);
	if (name != nullptr && std::strcmp(name, "NSNumber") == 0) {
		ObjcNumberValue number{};
		{
			std::lock_guard lock(g_number_mutex);
			const auto found = g_numbers.find(object);
			if (found == g_numbers.end()) return nullptr;
			number = found->second;
		}
		if (number.boolean)
			return darling_windows_CFBooleanGetValue(number.real != 0.0);
		if (number.integral && !number.unsigned_value)
			return darling_windows_CFNumberCreateInteger(number.integer);
		if (number.integral && number.unsigned_value &&
			number.unsigned_integer <= static_cast<std::uint64_t>(INT64_MAX))
			return darling_windows_CFNumberCreateInteger(
				static_cast<std::int64_t>(number.unsigned_integer));
		return darling_windows_CFNumberCreateDouble(number.real);
	}
	if (name != nullptr && std::strcmp(name, "NSDate") == 0) {
		std::lock_guard lock(g_date_mutex);
		const auto found = g_dates.find(object);
		return found == g_dates.end() ? nullptr :
			darling_windows_CFDateCreate(found->second);
	}
	if (name != nullptr && (std::strcmp(name, "NSData") == 0 ||
		std::strcmp(name, "NSMutableData") == 0)) {
		std::vector<std::uint8_t> bytes;
		try {
			std::lock_guard lock(g_data_mutex);
			const auto found = g_data.find(object);
			if (found == g_data.end()) return nullptr;
			bytes = found->second;
		} catch (...) {
			return nullptr;
		}
		return darling_windows_CFDataCreate(bytes.data(),
			static_cast<darling_windows_CFIndex>(bytes.size()));
	}
	if (name != nullptr && (std::strcmp(name, "NSArray") == 0 ||
		std::strcmp(name, "NSMutableArray") == 0)) {
		std::vector<id> elements;
		try {
			std::lock_guard lock(g_array_mutex);
			const auto found = g_arrays.find(object);
			if (found == g_arrays.end() || found->second.size() > 100'000)
				return nullptr;
			elements = found->second;
			for (id element : elements) objc_retain(element);
		} catch (...) {
			return nullptr;
		}
		std::vector<const void*> converted;
		try {
			converted.reserve(elements.size());
			for (id element : elements) {
				auto value = CFObjectFromObjc(element, depth + 1);
				if (value == nullptr) throw std::runtime_error("unsupported defaults array value");
				converted.push_back(value);
			}
		} catch (...) {
			for (const void* value : converted) darling_windows_CFRelease(value);
			for (id element : elements) objc_release(element);
			return nullptr;
		}
		auto result = darling_windows_CFArrayCreate(converted.data(),
			static_cast<darling_windows_CFIndex>(converted.size()));
		for (const void* value : converted) darling_windows_CFRelease(value);
		for (id element : elements) objc_release(element);
		return result;
	}
	if (name != nullptr && (std::strcmp(name, "NSDictionary") == 0 ||
		std::strcmp(name, "NSMutableDictionary") == 0)) {
		std::vector<std::pair<id, id>> entries;
		try {
			std::lock_guard lock(g_dictionary_mutex);
			const auto found = g_dictionaries.find(object);
			if (found == g_dictionaries.end() || found->second.size() > 100'000)
				return nullptr;
			entries = found->second;
			for (const auto& [key, value] : entries) {
				objc_retain(key);
				objc_retain(value);
			}
		} catch (...) {
			return nullptr;
		}
		std::vector<const void*> keys;
		std::vector<const void*> values;
		try {
			keys.reserve(entries.size());
			values.reserve(entries.size());
			for (const auto& [key, value] : entries) {
				auto converted_key = CFObjectFromObjc(key, depth + 1);
				auto converted_value = CFObjectFromObjc(value, depth + 1);
				if (converted_key == nullptr || converted_value == nullptr ||
					darling_windows_CFGetTypeID(converted_key) !=
						darling_windows_CFStringGetTypeID()) {
					if (converted_key != nullptr) darling_windows_CFRelease(converted_key);
					if (converted_value != nullptr) darling_windows_CFRelease(converted_value);
					throw std::runtime_error("unsupported defaults dictionary value");
				}
				keys.push_back(converted_key);
				values.push_back(converted_value);
			}
		} catch (...) {
			for (const void* value : keys) darling_windows_CFRelease(value);
			for (const void* value : values) darling_windows_CFRelease(value);
			for (const auto& [key, value] : entries) {
				objc_release(key);
				objc_release(value);
			}
			return nullptr;
		}
		auto result = darling_windows_CFDictionaryCreate(keys.data(), values.data(),
			static_cast<darling_windows_CFIndex>(entries.size()));
		for (const void* value : keys) darling_windows_CFRelease(value);
		for (const void* value : values) darling_windows_CFRelease(value);
		for (const auto& [key, value] : entries) {
			objc_release(key);
			objc_release(value);
		}
		return result;
	}
	return nullptr;
}

bool ObjcPropertyListXml(id object, std::string& xml)
{
	auto value = CFObjectFromObjc(object);
	if (value == nullptr) return false;
	auto serialized = darling_windows_CFPropertyListCreateXML(value);
	darling_windows_CFRelease(value);
	if (serialized == nullptr) return false;
	try {
		std::vector<char> buffer(16 * 1024 * 1024);
		const bool converted = darling_windows_CFStringGetCString(serialized,
			buffer.data(), static_cast<darling_windows_CFIndex>(buffer.size()));
		if (converted) xml.assign(buffer.data());
		darling_windows_CFRelease(serialized);
		return converted && !xml.empty();
	} catch (...) {
		darling_windows_CFRelease(serialized);
		return false;
	}
}

id ObjcObjectFromPropertyListXml(const char* xml)
{
	auto value = darling_windows_CFPropertyListCreateFromXML(xml);
	if (value == nullptr) return nullptr;
	id object = ObjcObjectFromCF(value);
	darling_windows_CFRelease(value);
	return object;
}

id NSObjectAlloc(Class cls, SEL)
{
	return class_createInstance(cls, 0);
}

id NSObjectInit(id self, SEL)
{
	if (self == nullptr) return nullptr;
	const char* name = class_getName(object_getClass(self));
	if (name != nullptr && (std::strcmp(name, "NSArray") == 0 ||
		std::strcmp(name, "NSMutableArray") == 0 ||
		std::strcmp(name, "NSSet") == 0 ||
		std::strcmp(name, "NSMutableSet") == 0))
		return InitializeEmptyNSArray(self) ? self : nullptr;
	if (name != nullptr && (std::strcmp(name, "NSDictionary") == 0 ||
		std::strcmp(name, "NSMutableDictionary") == 0))
		return InitializeEmptyNSDictionary(self) ? self : nullptr;
	if (name != nullptr && (std::strcmp(name, "NSData") == 0 ||
		std::strcmp(name, "NSMutableData") == 0))
		return InitializeDataStorage(self, nullptr, 0) ? self : nullptr;
	return self;
}

id NSObjectInitWithCoder(id self, SEL, id)
{
	return self;
}

void NSAutoreleasePoolFinalize(id object)
{
	void* token = nullptr;
	{
		std::lock_guard lock(g_autorelease_pools_mutex);
		const auto found = g_autorelease_pools.find(object);
		if (found == g_autorelease_pools.end()) return;
		token = found->second.token;
		g_autorelease_pools.erase(found);
	}
	if (token != nullptr) objc_autoreleasePoolPop(token);
}

id NSAutoreleasePoolInit(id self, SEL)
{
	if (self == nullptr) return nullptr;
	{
		std::lock_guard lock(g_autorelease_pools_mutex);
		if (g_autorelease_pools.find(self) != g_autorelease_pools.end())
			return self;
	}
	void* token = nullptr;
	try {
		token = objc_autoreleasePoolPush();
		if (token == nullptr) return nullptr;
		{
			std::lock_guard lock(g_autorelease_pools_mutex);
			g_autorelease_pools.emplace(self, AppKitAutoreleasePoolState{token});
		}
	} catch (...) {
		if (token != nullptr) objc_autoreleasePoolPop(token);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(self, &NSAutoreleasePoolFinalize)) {
		NSAutoreleasePoolFinalize(self);
		return nullptr;
	}
	return self;
}

id NSAutoreleasePoolNew(Class cls, SEL)
{
	if (cls == nullptr) return nullptr;
	id pool = class_createInstance(cls, 0);
	if (pool == nullptr) return nullptr;
	pool = NSAutoreleasePoolInit(pool, nullptr);
	return pool;
}

void NSAutoreleasePoolDrain(id self, SEL)
{
	if (self != nullptr) objc_release(self);
}

id NSObjectRetain(id self, SEL)
{
	return objc_retain(self);
}

void NSObjectRelease(id self, SEL)
{
	objc_release(self);
}

id NSObjectAllocWithZone(Class cls, SEL, void*)
{
	return class_createInstance(cls, 0);
}

id NSObjectNew(Class cls, SEL)
{
	return NSObjectInit(class_createInstance(cls, 0), nullptr);
}

id NSObjectInstanceClass(id self, SEL)
{
	return reinterpret_cast<id>(object_getClass(self));
}

id NSObjectClassMethod(Class cls, SEL)
{
	return reinterpret_cast<id>(cls);
}

bool NSObjectIsKindOfClass(id self, SEL, Class expected)
{
	for (Class cls = object_getClass(self); cls != nullptr;
		cls = class_getSuperclass(cls))
		if (cls == expected) return true;
	return false;
}

bool NSObjectIsMemberOfClass(id self, SEL, Class expected)
{
	return self != nullptr && object_getClass(self) == expected;
}

bool NSObjectRespondsToSelector(id self, SEL, SEL expected)
{
	return self != nullptr && expected != nullptr &&
		class_respondsToSelector(object_getClass(self), expected);
}

bool NSObjectConformsToProtocol(id self, SEL, Protocol protocol)
{
	return self != nullptr && protocol != nullptr &&
		class_conformsToProtocol(object_getClass(self), protocol);
}

bool NSObjectIsEqual(id self, SEL, id other)
{
	return self == other;
}

std::uint64_t NSObjectHash(id self, SEL)
{
	return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(self));
}

id NSObjectDescription(id self, SEL)
{
	const char* name = class_getName(object_getClass(self));
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		name == nullptr ? "NSObject" : name));
}

id NSObjectClassDescription(Class cls, SEL)
{
	const char* name = class_getName(cls);
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		name == nullptr ? "NSObject" : name));
}

id NSObjectCopyWithZone(id self, SEL, void*)
{
	return objc_retain(self);
}

id NSObjectAutorelease(id self, SEL)
{
	return objc_autorelease(self);
}

const char* NSStringUTF8String(id self, SEL)
{
	if (!darling_objc_is_constant_string(self)) {
		if (IsMutableNSString(self)) {
			std::lock_guard lock(g_mutable_string_mutex);
			const auto found = g_mutable_strings.find(self);
			return found == g_mutable_strings.end() ? nullptr : found->second.c_str();
		}
		return IsDynamicNSString(self) ? DynamicNSStringBytes(self) : nullptr;
	}
	const auto* literal = reinterpret_cast<const MachOConstantString*>(self);
	if (literal->contents == nullptr || literal->length > 16 * 1024 * 1024)
		return nullptr;
	std::lock_guard lock(g_constant_string_mutex);
	auto found = g_constant_string_utf8.find(self);
	if (found != g_constant_string_utf8.end()) return found->second.c_str();
	std::string converted;
	if (literal->flags == 0x07d0) {
		if (literal->length > static_cast<std::uint64_t>(INT_MAX / 2))
			return nullptr;
		const auto* wide = static_cast<const wchar_t*>(literal->contents);
		const int source_length = static_cast<int>(literal->length);
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wide, source_length, nullptr, 0, nullptr, nullptr);
		if (required == 0 && source_length != 0) return nullptr;
		converted.resize(static_cast<std::size_t>(required));
		if (required != 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			wide, source_length, converted.data(), required, nullptr, nullptr) == 0)
			return nullptr;
	} else {
		converted.assign(static_cast<const char*>(literal->contents),
			static_cast<std::size_t>(literal->length));
	}
	return g_constant_string_utf8.emplace(self, std::move(converted)).first->second.c_str();
}

std::uint64_t NSStringLength(id self, SEL)
{
	if (!darling_objc_is_constant_string(self)) {
		if (IsMutableNSString(self)) {
			std::lock_guard lock(g_mutable_string_mutex);
			const auto found = g_mutable_strings.find(self);
			if (found == g_mutable_strings.end()) return 0;
			const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
				found->second.c_str(), -1, nullptr, 0);
			return length > 0 ? static_cast<std::uint64_t>(length - 1) : 0;
		}
		if (!IsDynamicNSString(self)) return 0;
		const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			DynamicNSStringBytes(self), -1, nullptr, 0);
		return length > 0 ? length - 1 : 0;
	}
	const auto* literal = reinterpret_cast<const MachOConstantString*>(self);
	if (literal->flags == 0x07d0)
		return literal->length;
	const char* utf8 = NSStringUTF8String(self, nullptr);
	if (utf8 == nullptr) return 0;
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		utf8, -1, nullptr, 0);
	return length > 0 ? length - 1 : 0;
}

id NSStringSubstringWithRange(id self, SEL, DarlingObjcRange range)
{
	const char* source = NSStringUTF8String(self, nullptr);
	std::string substring;
	if (!UTF8Substring(source, range, substring)) return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		substring.c_str()));
}

id NSStringSubstringFromIndex(id self, SEL, std::uint64_t index)
{
	const char* source = NSStringUTF8String(self, nullptr);
	if (source == nullptr) return nullptr;
	const std::wstring wide = Utf8ToWide(source);
	std::string substring;
	if (!UTF8Substring(source, {index,
		index <= wide.size() ? wide.size() - static_cast<std::size_t>(index) : 0},
		substring))
		return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		substring.c_str()));
}

id NSStringSubstringToIndex(id self, SEL, std::uint64_t index)
{
	std::string substring;
	if (!UTF8Substring(NSStringUTF8String(self, nullptr), {0, index}, substring))
		return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		substring.c_str()));
}

id NSStringInitWithUTF8String(id self, SEL, const char* utf8)
{
	if (utf8 == nullptr) {
		objc_release(self);
		return nullptr;
	}
	const Class cls = object_getClass(self);
	id result = CreateDynamicNSString(cls, utf8);
	objc_release(self);
	return result;
}

id NSProcessInfoSingleton(Class cls, SEL)
{
	if (cls == nullptr) return nullptr;
	std::lock_guard lock(g_process_info_mutex);
	if (g_process_info_singleton == nullptr)
		g_process_info_singleton = class_createInstance(cls, 0);
	return g_process_info_singleton == nullptr ? nullptr :
		objc_retainAutorelease(g_process_info_singleton);
}

id NSProcessInfoArguments(id, SEL)
{
	std::vector<std::string> arguments;
	try {
		std::lock_guard lock(g_process_info_mutex);
		arguments = g_process_arguments;
	} catch (...) {
		return nullptr;
	}
	std::vector<id> values;
	try {
		values.reserve(arguments.size());
		for (const auto& argument : arguments) {
			id value = CreateDynamicNSString(objc_getClass("NSString"),
				argument.c_str());
			if (value == nullptr) throw std::bad_alloc();
			values.push_back(value);
		}
		id result = CreateNSArray(objc_getClass("NSArray"), values.data(),
			values.size());
		for (id value : values) objc_release(value);
		return objc_autorelease(result);
	} catch (...) {
		for (id value : values) objc_release(value);
		return nullptr;
	}
}

id NSProcessInfoEnvironment(id, SEL)
{
	std::vector<id> keys;
	std::vector<id> values;
	try {
		for (char** entry = darling_windows_environ; entry != nullptr && *entry != nullptr; ++entry) {
			const std::string_view item(*entry);
			const auto separator = item.find('=');
			if (separator == std::string_view::npos || separator == 0) continue;
			const std::string key(item.substr(0, separator));
			const std::string value(item.substr(separator + 1));
			id key_object = CreateDynamicNSString(objc_getClass("NSString"),
				key.c_str());
			id value_object = CreateDynamicNSString(objc_getClass("NSString"),
				value.c_str());
			if (key_object == nullptr || value_object == nullptr) {
				if (key_object != nullptr) objc_release(key_object);
				if (value_object != nullptr) objc_release(value_object);
				throw std::bad_alloc();
			}
		try {
			keys.push_back(key_object);
			values.push_back(value_object);
		} catch (...) {
			if (keys.size() > values.size()) keys.pop_back();
			objc_release(key_object);
			objc_release(value_object);
			throw;
		}
		}
		id result = CreateNSDictionary(objc_getClass("NSDictionary"),
			values.data(), keys.data(), values.size());
		for (id key : keys) objc_release(key);
		for (id value : values) objc_release(value);
		return objc_autorelease(result);
	} catch (...) {
		for (id key : keys) objc_release(key);
		for (id value : values) objc_release(value);
		return nullptr;
	}
}

id NSProcessInfoProcessName(id, SEL)
{
	std::string executable;
	try {
		std::lock_guard lock(g_process_info_mutex);
		if (!g_process_arguments.empty()) executable = g_process_arguments.front();
	} catch (...) {
		return nullptr;
	}
	if (executable.empty()) {
		wchar_t module[MAX_PATH]{};
		const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
		if (length != 0 && length < MAX_PATH)
			ConvertWideToUtf8(std::wstring_view(module, length), executable);
	}
	const auto separator = executable.find_last_of("/\\");
	if (separator != std::string::npos) executable.erase(0, separator + 1);
	if (executable.size() >= 4 &&
		(executable.compare(executable.size() - 4, 4, ".exe") == 0 ||
		 executable.compare(executable.size() - 4, 4, ".EXE") == 0))
		executable.resize(executable.size() - 4);
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		executable.c_str()));
}

id NSProcessInfoHostName(id, SEL)
{
	wchar_t name[32768]{};
	DWORD length = static_cast<DWORD>(std::size(name));
	if (!GetComputerNameW(name, &length)) return nullptr;
	std::string utf8;
	if (!ConvertWideToUtf8(std::wstring_view(name, length), utf8)) return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		utf8.c_str()));
}

int NSProcessInfoProcessIdentifier(id, SEL)
{
	return static_cast<int>(GetCurrentProcessId());
}

std::uint64_t NSProcessInfoProcessorCount(id, SEL)
{
	std::uint64_t count = 0;
	const WORD groups = GetActiveProcessorGroupCount();
	for (WORD group = 0; group < groups; ++group)
		count += GetMaximumProcessorCount(group);
	return count;
}

std::uint64_t NSProcessInfoActiveProcessorCount(id, SEL)
{
	return static_cast<std::uint64_t>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
}

double NSProcessInfoSystemUptime(id, SEL)
{
	ULONGLONG unbiased_time = 0;
	if (QueryUnbiasedInterruptTime(&unbiased_time))
		return static_cast<double>(unbiased_time) / 10'000'000.0;
	return static_cast<double>(GetTickCount64()) / 1000.0;
}

std::string NSProcessInfoGuestUserName()
{
	if (const char* user = darling_windows_getenv("USER"); user != nullptr && *user != '\0')
		return user;
	if (const char* login = darling_windows_getenv("LOGNAME"); login != nullptr && *login != '\0')
		return login;
	const char* home = darling_windows_getenv("CFFIXED_USER_HOME");
	if (home == nullptr || *home == '\0') home = darling_windows_getenv("HOME");
	if (home != nullptr && *home != '\0') {
		std::string_view path(home);
		while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
			path.remove_suffix(1);
		const auto separator = path.find_last_of("/\\");
		const auto account = separator == std::string_view::npos ? path : path.substr(separator + 1);
		if (!account.empty()) return std::string(account);
	}
	return "Default";
}

id NSProcessInfoUserName(id, SEL)
{
	const auto user = NSProcessInfoGuestUserName();
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), user.c_str()));
}

id NSProcessInfoFullUserName(id, SEL)
{
	const char* configured = darling_windows_getenv("WINTOSH_FULL_USER_NAME");
	const std::string name = configured != nullptr && *configured != '\0' ?
		configured : NSProcessInfoGuestUserName();
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), name.c_str()));
}

std::uint64_t NSProcessInfoPhysicalMemory(id, SEL)
{
	MEMORYSTATUSEX memory{};
	memory.dwLength = sizeof(memory);
	return GlobalMemoryStatusEx(&memory) ? memory.ullTotalPhys : 0;
}

DarlingObjcOperatingSystemVersion NSProcessInfoOperatingSystemVersion(id, SEL)
{
	DarlingObjcOperatingSystemVersion version{14, 0, 0};
	const char* configured = darling_windows_getenv("WINTOSH_MACOS_VERSION");
	if (configured == nullptr) return version;
	std::array<std::int64_t*, 3> fields{
		&version.major_version, &version.minor_version, &version.patch_version};
	const std::string_view text(configured);
	std::size_t position = 0;
	for (std::size_t index = 0; index < fields.size(); ++index) {
		const auto separator = text.find('.', position);
		const auto end = separator == std::string_view::npos ? text.size() : separator;
		if (end == position) return {14, 0, 0};
		const auto parsed = std::from_chars(text.data() + position,
			text.data() + end, *fields[index]);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + end ||
			*fields[index] < 0 || *fields[index] > 10000) return {14, 0, 0};
		if (index == fields.size() - 1 && separator != std::string_view::npos)
			return {14, 0, 0};
		if (separator == std::string_view::npos) {
			for (std::size_t remaining = index + 1; remaining < fields.size(); ++remaining)
				*fields[remaining] = 0;
			return version;
		}
		position = separator + 1;
	}
	if (position >= text.size()) return {14, 0, 0};
	return version;
}

id NSProcessInfoOperatingSystemVersionString(id self, SEL selector)
{
	const auto version = NSProcessInfoOperatingSystemVersion(self, selector);
	const char* build = darling_windows_getenv("WINTOSH_DARWIN_OSVERSION");
	std::array<char, 160> text{};
	const int length = std::snprintf(text.data(), text.size(),
		"Version %lld.%lld.%lld (Build %s)",
		static_cast<long long>(version.major_version),
		static_cast<long long>(version.minor_version),
		static_cast<long long>(version.patch_version), build == nullptr ? "23A344" : build);
	if (length <= 0 || static_cast<std::size_t>(length) >= text.size()) return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), text.data()));
}

int NSProcessInfoOperatingSystem(id, SEL)
{
	return 5; // NSMACHOperatingSystem
}

id NSProcessInfoOperatingSystemName(id, SEL)
{
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSMACHOperatingSystem"));
}

bool NSProcessInfoIsOperatingSystemAtLeastVersion(id, SEL,
	DarlingObjcOperatingSystemVersion requested)
{
	const auto actual = NSProcessInfoOperatingSystemVersion(nullptr, nullptr);
	if (actual.major_version != requested.major_version)
		return actual.major_version > requested.major_version;
	if (actual.minor_version != requested.minor_version)
		return actual.minor_version > requested.minor_version;
	return actual.patch_version >= requested.patch_version;
}

std::string NSUserDefaultsApplicationDomain()
{
	const auto bundle = darling_windows_CFBundleGetMainBundle();
	const auto identifier = bundle == nullptr ? nullptr :
		darling_windows_CFBundleGetIdentifier(bundle);
	if (identifier != nullptr) {
		if (const char* utf8 = darling_windows_CFStringGetCStringPtr(identifier);
			utf8 != nullptr && *utf8 != '\0') return utf8;
		char buffer[1024]{};
		if (darling_windows_CFStringGetCString(identifier, buffer,
			static_cast<darling_windows_CFIndex>(sizeof(buffer))) && buffer[0] != '\0')
			return buffer;
	}
	return "org.wintosh.application";
}

void NSUserDefaultsFinalize(id object)
{
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(object);
		if (found == g_user_defaults.end()) return;
		state = found->second;
		g_user_defaults.erase(found);
		if (g_standard_user_defaults == object) g_standard_user_defaults = nullptr;
	}
	if (state.argument_store != nullptr)
		darling_windows_preferences_close(state.argument_store);
	if (state.managed_store != nullptr)
		darling_windows_preferences_close(state.managed_store);
	if (state.store != nullptr) darling_windows_preferences_close(state.store);
}

id NSUserDefaultsInitWithDomain(id self, const char* suite)
{
	if (self == nullptr) return nullptr;
	{
		std::lock_guard lock(g_user_defaults_mutex);
		if (g_user_defaults.find(self) != g_user_defaults.end()) return self;
	}
	const auto domain = NSUserDefaultsApplicationDomain();
	AppKitUserDefaultsState state{};
	state.store = suite == nullptr ?
		darling_windows_preferences_open(domain.c_str()) :
		darling_windows_preferences_open_suite(domain.c_str(), suite);
	if (state.store == nullptr) return nullptr;
	std::vector<std::string> arguments;
	std::vector<const char*> argument_pointers;
	try {
		{
			std::lock_guard lock(g_process_info_mutex);
			arguments = g_process_arguments;
		}
		argument_pointers.reserve(arguments.size());
		for (const auto& argument : arguments)
			argument_pointers.push_back(argument.c_str());
		state.argument_store = darling_windows_preferences_open_arguments(
			domain.c_str(), static_cast<int>(argument_pointers.size()),
			argument_pointers.data());
		state.managed_store = darling_windows_preferences_open_managed(domain.c_str());
		std::lock_guard lock(g_user_defaults_mutex);
		g_user_defaults.emplace(self, state);
	} catch (...) {
		if (state.argument_store != nullptr)
			darling_windows_preferences_close(state.argument_store);
		if (state.managed_store != nullptr)
			darling_windows_preferences_close(state.managed_store);
		darling_windows_preferences_close(state.store);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(self, &NSUserDefaultsFinalize)) {
		NSUserDefaultsFinalize(self);
		return nullptr;
	}
	return self;
}

id NSUserDefaultsInit(id self, SEL)
{
	return NSUserDefaultsInitWithDomain(self, nullptr);
}

id NSUserDefaultsInitWithSuiteName(id self, SEL, id suite_name)
{
	const char* suite = suite_name == nullptr ? nullptr :
		NSStringUTF8String(suite_name, nullptr);
	return suite == nullptr || *suite == '\0' ? nullptr :
		NSUserDefaultsInitWithDomain(self, suite);
}

id NSUserDefaultsStandard(Class cls, SEL)
{
	{
		std::lock_guard lock(g_user_defaults_mutex);
		if (g_standard_user_defaults != nullptr)
			return objc_retainAutorelease(g_standard_user_defaults);
	}
	id candidate = class_createInstance(cls, 0);
	if (candidate == nullptr) return nullptr;
	candidate = NSUserDefaultsInitWithDomain(candidate, nullptr);
	if (candidate == nullptr) return nullptr;
	id selected = nullptr;
	{
		std::lock_guard lock(g_user_defaults_mutex);
		if (g_standard_user_defaults == nullptr) {
			g_standard_user_defaults = candidate;
			selected = candidate;
		} else {
			selected = g_standard_user_defaults;
		}
	}
	if (selected != candidate) objc_release(candidate);
	return objc_retainAutorelease(selected);
}

id NSUserDefaultsObjectForStore(darling_windows_preferences_ref store,
	const char* key)
{
	if (store == nullptr) return nullptr;
	try {
		std::vector<char> buffer(1024 * 1024);
		if (darling_windows_preferences_get_string(store, key, buffer.data(),
			buffer.size()))
			return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
				buffer.data()));
	} catch (...) {
		return nullptr;
	}
	std::int64_t integer = 0;
	if (darling_windows_preferences_get_integer(store, key, &integer))
		return objc_autorelease(CreateIntegerNSNumber(objc_getClass("NSNumber"), integer));
	bool boolean = false;
	if (darling_windows_preferences_get_boolean(store, key, &boolean))
		return objc_autorelease(CreateNSNumber(objc_getClass("NSNumber"),
			boolean ? 1.0 : 0.0, true));
	double real = 0.0;
	if (darling_windows_preferences_get_double(store, key, &real))
		return objc_autorelease(CreateNSNumber(objc_getClass("NSNumber"), real));
	try {
		std::vector<char> plist(16 * 1024 * 1024);
		if (darling_windows_preferences_get_property_list_xml(store, key,
			plist.data(), plist.size())) {
			if (id value = ObjcObjectFromPropertyListXml(plist.data()); value != nullptr)
				return objc_autorelease(value);
		}
	} catch (...) {
		return nullptr;
	}
	return nullptr;
}

id NSUserDefaultsVolatileObjectForKey(const AppKitUserDefaultsState& state,
	const char* key)
{
	for (auto domain = state.volatile_domain_order.rbegin();
		domain != state.volatile_domain_order.rend(); ++domain) {
		const auto found_domain = state.volatile_domains.find(*domain);
		if (found_domain == state.volatile_domains.end()) continue;
		const auto found_value = found_domain->second.find(key);
		if (found_value == found_domain->second.end()) continue;
		if (id value = ObjcObjectFromPropertyListXml(found_value->second.c_str());
			value != nullptr) return objc_autorelease(value);
	}
	return nullptr;
}

id NSUserDefaultsObjectForKey(id self, SEL, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return nullptr;
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(self);
		if (found == g_user_defaults.end()) return nullptr;
		state = found->second;
	}
	if (id value = NSUserDefaultsObjectForStore(state.argument_store, key);
		value != nullptr) return value;
	if (id value = NSUserDefaultsObjectForStore(state.managed_store, key);
		value != nullptr) return value;
	if (id value = NSUserDefaultsVolatileObjectForKey(state, key); value != nullptr)
		return value;
	if (id value = NSUserDefaultsObjectForStore(state.store, key); value != nullptr)
		return value;
	return nullptr;
}

id NSUserDefaultsVolatileDomainForName(id self, SEL, id name_object)
{
	const char* name = name_object == nullptr ? nullptr :
		NSStringUTF8String(name_object, nullptr);
	if (name == nullptr) return nullptr;
	std::unordered_map<std::string, std::string> values;
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto state = g_user_defaults.find(self);
		if (state == g_user_defaults.end()) return nullptr;
		const auto domain = state->second.volatile_domains.find(name);
		if (domain == state->second.volatile_domains.end()) return nullptr;
		values = domain->second;
	}
	std::vector<id> keys;
	std::vector<id> objects;
	try {
		keys.reserve(values.size());
		objects.reserve(values.size());
		for (const auto& [key, xml] : values) {
			id key_object = CreateDynamicNSString(objc_getClass("NSString"), key.c_str());
			id value_object = ObjcObjectFromPropertyListXml(xml.c_str());
			if (key_object == nullptr || value_object == nullptr) {
				if (key_object != nullptr) objc_release(key_object);
				if (value_object != nullptr) objc_release(value_object);
				throw std::runtime_error("invalid volatile preference value");
			}
			keys.push_back(key_object);
			objects.push_back(value_object);
		}
	} catch (...) {
		for (id key : keys) objc_release(key);
		for (id value : objects) objc_release(value);
		return nullptr;
	}
	id result = CreateNSDictionary(objc_getClass("NSDictionary"), objects.data(),
		keys.data(), values.size());
	for (id key : keys) objc_release(key);
	for (id value : objects) objc_release(value);
	return objc_autorelease(result);
}

void NSUserDefaultsSetVolatileDomain(id self, SEL, id dictionary, id name_object)
{
	const char* name = name_object == nullptr ? nullptr :
		NSStringUTF8String(name_object, nullptr);
	if (name == nullptr || *name == '\0') return;
	std::vector<std::pair<id, id>> entries;
	{
		std::lock_guard lock(g_dictionary_mutex);
		const auto found = g_dictionaries.find(dictionary);
		if (found == g_dictionaries.end() || found->second.size() > 100'000) return;
		try {
			entries = found->second;
			for (const auto& [key, value] : entries) {
				objc_retain(key);
				objc_retain(value);
			}
		} catch (...) {
			for (const auto& [key, value] : entries) {
				objc_release(key);
				objc_release(value);
			}
			return;
		}
	}
	std::unordered_map<std::string, std::string> values;
	bool valid = true;
	try {
		for (const auto& [key_object, value_object] : entries) {
			const auto key_class = object_getClass(key_object);
			const char* key = IsSupportedNSStringClass(key_class) ?
				NSStringUTF8String(key_object, nullptr) : nullptr;
			std::string xml;
			if (key == nullptr || !ObjcPropertyListXml(value_object, xml)) {
				valid = false;
				break;
			}
			values[key] = std::move(xml);
		}
	} catch (...) {
		valid = false;
	}
	for (const auto& [key, value] : entries) {
		objc_release(key);
		objc_release(value);
	}
	if (!valid) return;
	std::lock_guard lock(g_user_defaults_mutex);
	const auto state = g_user_defaults.find(self);
	if (state != g_user_defaults.end()) {
		if (state->second.volatile_domains.find(name) ==
			state->second.volatile_domains.end())
			state->second.volatile_domain_order.emplace_back(name);
		state->second.volatile_domains[name] = std::move(values);
	}
}

void NSUserDefaultsRemoveVolatileDomain(id self, SEL, id name_object)
{
	const char* name = name_object == nullptr ? nullptr :
		NSStringUTF8String(name_object, nullptr);
	if (name == nullptr) return;
	std::lock_guard lock(g_user_defaults_mutex);
	const auto state = g_user_defaults.find(self);
	if (state != g_user_defaults.end()) {
		state->second.volatile_domains.erase(name);
		auto& order = state->second.volatile_domain_order;
		order.erase(std::remove(order.begin(), order.end(), name), order.end());
	}
}

id NSUserDefaultsVolatileDomainNames(id self, SEL)
{
	std::vector<std::string> names;
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto state = g_user_defaults.find(self);
		if (state == g_user_defaults.end()) return nullptr;
		for (const auto& [name, values] : state->second.volatile_domains)
			names.push_back(name);
	}
	std::vector<id> objects;
	try {
		objects.reserve(names.size());
		for (const auto& name : names) {
			id object = CreateDynamicNSString(objc_getClass("NSString"), name.c_str());
			if (object == nullptr) throw std::runtime_error("volatile domain name allocation failed");
			objects.push_back(object);
		}
	} catch (...) {
		for (id object : objects) objc_release(object);
		return nullptr;
	}
	id result = CreateNSArray(objc_getClass("NSArray"), objects.data(), objects.size());
	for (id object : objects) objc_release(object);
	return objc_autorelease(result);
}

id NSUserDefaultsStringForKey(id self, SEL selector, id key)
{
	id value = NSUserDefaultsObjectForKey(self, selector, key);
	const Class cls = value == nullptr ? nullptr : object_getClass(value);
	return IsSupportedNSStringClass(cls) ?
		value : nullptr;
}

std::int64_t NSUserDefaultsIntegerForKey(id self, SEL, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return 0;
	if (id object = NSUserDefaultsObjectForKey(self, nullptr, key_object);
		object != nullptr && object_getClass(object) == objc_getClass("NSNumber")) {
		std::lock_guard lock(g_number_mutex);
		const auto found = g_numbers.find(object);
		if (found != g_numbers.end()) return found->second.integral ?
			(found->second.unsigned_value ? static_cast<std::int64_t>(
				found->second.unsigned_integer) : found->second.integer) :
			static_cast<std::int64_t>(found->second.real);
	}
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(self);
		if (found == g_user_defaults.end()) return 0;
		state = found->second;
	}
	std::int64_t value = 0;
	if (state.argument_store != nullptr && darling_windows_preferences_get_integer(
		state.argument_store, key, &value)) return value;
	if (state.managed_store != nullptr && darling_windows_preferences_get_integer(
		state.managed_store, key, &value)) return value;
	if (state.store != nullptr && darling_windows_preferences_get_integer(
		state.store, key, &value)) return value;
	bool boolean = false;
	if (state.argument_store != nullptr && darling_windows_preferences_get_boolean(
		state.argument_store, key, &boolean)) return boolean ? 1 : 0;
	if (state.managed_store != nullptr && darling_windows_preferences_get_boolean(
		state.managed_store, key, &boolean)) return boolean ? 1 : 0;
	if (state.store != nullptr && darling_windows_preferences_get_boolean(
		state.store, key, &boolean)) return boolean ? 1 : 0;
	double real = 0.0;
	if (state.argument_store != nullptr && darling_windows_preferences_get_double(
		state.argument_store, key, &real) && real >= static_cast<double>(INT64_MIN) &&
		real < static_cast<double>(INT64_MAX)) return static_cast<std::int64_t>(real);
	if (state.managed_store != nullptr && darling_windows_preferences_get_double(
		state.managed_store, key, &real) && real >= static_cast<double>(INT64_MIN) &&
		real < static_cast<double>(INT64_MAX)) return static_cast<std::int64_t>(real);
	if (state.store != nullptr && darling_windows_preferences_get_double(
		state.store, key, &real) && real >= static_cast<double>(INT64_MIN) &&
		real < static_cast<double>(INT64_MAX)) return static_cast<std::int64_t>(real);
	return 0;
}

bool NSUserDefaultsBoolForKey(id self, SEL, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return false;
	if (id object = NSUserDefaultsObjectForKey(self, nullptr, key_object);
		object != nullptr && object_getClass(object) == objc_getClass("NSNumber")) {
		std::lock_guard lock(g_number_mutex);
		const auto found = g_numbers.find(object);
		if (found != g_numbers.end()) return found->second.real != 0.0;
	}
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(self);
		if (found == g_user_defaults.end()) return false;
		state = found->second;
	}
	bool value = false;
	if (state.argument_store != nullptr && darling_windows_preferences_get_boolean(
		state.argument_store, key, &value)) return value;
	if (state.managed_store != nullptr && darling_windows_preferences_get_boolean(
		state.managed_store, key, &value)) return value;
	if (state.store != nullptr && darling_windows_preferences_get_boolean(
		state.store, key, &value)) return value;
	std::int64_t integer = 0;
	if (state.argument_store != nullptr && darling_windows_preferences_get_integer(
		state.argument_store, key, &integer)) return integer != 0;
	if (state.managed_store != nullptr && darling_windows_preferences_get_integer(
		state.managed_store, key, &integer)) return integer != 0;
	if (state.store != nullptr && darling_windows_preferences_get_integer(
		state.store, key, &integer)) return integer != 0;
	double real = 0.0;
	if (state.argument_store != nullptr && darling_windows_preferences_get_double(
		state.argument_store, key, &real)) return real != 0.0;
	if (state.managed_store != nullptr && darling_windows_preferences_get_double(
		state.managed_store, key, &real)) return real != 0.0;
	if (state.store != nullptr && darling_windows_preferences_get_double(
		state.store, key, &real)) return real != 0.0;
	return false;
}

double NSUserDefaultsDoubleForKey(id self, SEL, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return 0.0;
	if (id object = NSUserDefaultsObjectForKey(self, nullptr, key_object);
		object != nullptr && object_getClass(object) == objc_getClass("NSNumber")) {
		std::lock_guard lock(g_number_mutex);
		const auto found = g_numbers.find(object);
		if (found != g_numbers.end()) return found->second.real;
	}
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(self);
		if (found == g_user_defaults.end()) return 0.0;
		state = found->second;
	}
	double value = 0.0;
	for (auto store : {state.argument_store, state.managed_store, state.store})
		if (store != nullptr && darling_windows_preferences_get_double(
			store, key, &value)) return value;
	return 0.0;
}

void NSUserDefaultsSetObject(id self, SEL, id object, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr || object == nullptr) return;
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(self);
		if (found == g_user_defaults.end()) return;
		state = found->second;
	}
	const Class value_class = object_getClass(object);
	const char* class_name = value_class == nullptr ? nullptr : class_getName(value_class);
	if (IsSupportedNSStringClass(value_class)) {
		const char* value = NSStringUTF8String(object, nullptr);
		if (value != nullptr && state.store != nullptr)
			darling_windows_preferences_set_string(state.store, key, value);
		return;
	}
	if (class_name == nullptr) return;
	if (std::strcmp(class_name, "NSNumber") != 0) {
		std::string xml;
		if (state.store != nullptr && ObjcPropertyListXml(object, xml))
			darling_windows_preferences_set_property_list_xml(state.store, key,
				xml.c_str());
		return;
	}
	ObjcNumberValue number{};
	{
		std::lock_guard lock(g_number_mutex);
		const auto found = g_numbers.find(object);
		if (found == g_numbers.end()) return;
		number = found->second;
	}
	if (state.store == nullptr) return;
	if (number.boolean)
		darling_windows_preferences_set_boolean(state.store, key,
			number.real != 0.0);
	else if (number.integral && !number.unsigned_value)
		darling_windows_preferences_set_integer(state.store, key, number.integer);
	else if (number.integral && number.unsigned_value &&
		number.unsigned_integer <= static_cast<std::uint64_t>(INT64_MAX))
		darling_windows_preferences_set_integer(state.store, key,
			static_cast<std::int64_t>(number.unsigned_integer));
	else if (!number.integral)
		darling_windows_preferences_set_double(state.store, key, number.real);
}

void NSUserDefaultsSetInteger(id self, SEL, std::int64_t value, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return;
	std::lock_guard lock(g_user_defaults_mutex);
	const auto found = g_user_defaults.find(self);
	if (found != g_user_defaults.end() && found->second.store != nullptr)
		darling_windows_preferences_set_integer(found->second.store, key, value);
}

void NSUserDefaultsSetBool(id self, SEL, bool value, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return;
	std::lock_guard lock(g_user_defaults_mutex);
	const auto found = g_user_defaults.find(self);
	if (found != g_user_defaults.end() && found->second.store != nullptr)
		darling_windows_preferences_set_boolean(found->second.store, key, value);
}

void NSUserDefaultsSetDouble(id self, SEL, double value, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr || !std::isfinite(value)) return;
	std::lock_guard lock(g_user_defaults_mutex);
	const auto found = g_user_defaults.find(self);
	if (found != g_user_defaults.end() && found->second.store != nullptr)
		darling_windows_preferences_set_double(found->second.store, key, value);
}

void NSUserDefaultsRemoveObject(id self, SEL, id key_object)
{
	const char* key = key_object == nullptr ? nullptr :
		NSStringUTF8String(key_object, nullptr);
	if (key == nullptr) return;
	std::lock_guard lock(g_user_defaults_mutex);
	const auto found = g_user_defaults.find(self);
	if (found != g_user_defaults.end() && found->second.store != nullptr)
		darling_windows_preferences_remove(found->second.store, key);
}

bool NSUserDefaultsSynchronize(id self, SEL)
{
	std::lock_guard lock(g_user_defaults_mutex);
	const auto found = g_user_defaults.find(self);
	return found != g_user_defaults.end() && found->second.store != nullptr &&
		darling_windows_preferences_synchronize(found->second.store);
}

void NSUserDefaultsRegisterDefaults(id self, SEL, id dictionary)
{
	AppKitUserDefaultsState state{};
	{
		std::lock_guard lock(g_user_defaults_mutex);
		const auto found = g_user_defaults.find(self);
		if (found == g_user_defaults.end() || found->second.store == nullptr) return;
		state = found->second;
	}
	std::vector<std::pair<id, id>> entries;
	{
		std::lock_guard lock(g_dictionary_mutex);
		const auto found = g_dictionaries.find(dictionary);
		if (found == g_dictionaries.end()) return;
		try {
			entries = found->second;
			for (const auto& entry : entries) {
				objc_retain(entry.first);
				objc_retain(entry.second);
			}
		} catch (...) {
			for (const auto& entry : entries) {
				objc_release(entry.first);
				objc_release(entry.second);
			}
			return;
		}
	}
	for (const auto& [key_object, value_object] : entries) {
		const char* key = NSStringUTF8String(key_object, nullptr);
		const Class value_class = object_getClass(value_object);
		const char* value_class_name = value_class == nullptr ? nullptr :
			class_getName(value_class);
		if (key != nullptr && IsSupportedNSStringClass(value_class)) {
			const char* value = NSStringUTF8String(value_object, nullptr);
			if (value != nullptr)
				darling_windows_preferences_register_string(state.store, key, value);
		} else if (key != nullptr && value_class_name != nullptr &&
			std::strcmp(value_class_name, "NSNumber") == 0) {
			ObjcNumberValue number{};
			{
				std::lock_guard lock(g_number_mutex);
				const auto found = g_numbers.find(value_object);
				if (found != g_numbers.end()) number = found->second;
			}
			if (number.boolean)
				darling_windows_preferences_register_boolean(state.store, key,
					number.real != 0.0);
			else if (number.integral && !number.unsigned_value)
				darling_windows_preferences_register_integer(state.store, key,
					number.integer);
			else if (number.integral && number.unsigned_value &&
				number.unsigned_integer <= static_cast<std::uint64_t>(INT64_MAX))
				darling_windows_preferences_register_integer(state.store, key,
					static_cast<std::int64_t>(number.unsigned_integer));
			else if (!number.integral)
				darling_windows_preferences_register_double(state.store, key,
					number.real);
		} else if (key != nullptr) {
			std::string xml;
			if (ObjcPropertyListXml(value_object, xml))
				darling_windows_preferences_register_property_list_xml(state.store,
					key, xml.c_str());
		}
		objc_release(key_object);
		objc_release(value_object);
	}
}

id NSStringStringWithUTF8String(Class cls, SEL, const char* utf8)
{
	return objc_autorelease(CreateDynamicNSString(cls, utf8));
}

id NSStringInitWithString(id self, SEL, id other)
{
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		other, sel_registerName("UTF8String")));
	return NSStringInitWithUTF8String(self, nullptr, utf8);
}

id NSStringStringWithString(Class cls, SEL, id other)
{
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		other, sel_registerName("UTF8String")));
	return objc_autorelease(CreateDynamicNSString(cls, utf8));
}

bool NSStringIsEqualToString(id self, SEL, id other)
{
	const char* left = NSStringUTF8String(self, nullptr);
	const char* right = static_cast<const char*>(darling_objc_msgSend_pointer0(
		other, sel_registerName("UTF8String")));
	return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

std::uint64_t NSStringHash(id self, SEL)
{
	const char* value = NSStringUTF8String(self, nullptr);
	if (value == nullptr) return 0;
	std::uint64_t hash = 14695981039346656037ull;
	for (const auto* byte = reinterpret_cast<const unsigned char*>(value);
		*byte != 0; ++byte) {
		hash ^= *byte;
		hash *= 1099511628211ull;
	}
	return hash;
}

id NSStringCopyWithZone(id self, SEL, void*)
{
	if (IsMutableNSString(self))
		return CreateDynamicNSString(objc_getClass("NSString"),
			NSStringUTF8String(self, nullptr));
	return objc_retain(self);
}

id NSStringDescription(id self, SEL)
{
	return objc_retainAutorelease(self);
}

id NSStringMutableCopyWithZone(id self, SEL, void*)
{
	return CreateMutableNSString(objc_getClass("NSMutableString"),
		NSStringUTF8String(self, nullptr));
}

id NSMutableStringInitWithUTF8String(id self, SEL, const char* utf8)
{
	id replacement = CreateMutableNSString(objc_getClass("NSMutableString"), utf8);
	objc_release(self);
	return replacement;
}

id NSMutableStringInitWithString(id self, SEL, id value)
{
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	return NSMutableStringInitWithUTF8String(self, nullptr, utf8);
}

id NSMutableStringStringWithUTF8String(Class, SEL, const char* utf8)
{
	return objc_autorelease(CreateMutableNSString(objc_getClass("NSMutableString"),
		utf8));
}

id NSMutableStringStringWithString(Class, SEL, id value)
{
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	return objc_autorelease(CreateMutableNSString(
		objc_getClass("NSMutableString"), utf8));
}

id NSMutableStringString(Class, SEL)
{
	return objc_autorelease(CreateMutableNSString(
		objc_getClass("NSMutableString"), ""));
}

void NSMutableStringAppendString(id self, SEL, id suffix)
{
	const char* bytes = static_cast<const char*>(darling_objc_msgSend_pointer0(
		suffix, sel_registerName("UTF8String")));
	if (bytes == nullptr) return;
	const std::string addition(bytes);
	if (addition.size() > 16 * 1024 * 1024) return;
	std::lock_guard lock(g_mutable_string_mutex);
	const auto found = g_mutable_strings.find(self);
	if (found == g_mutable_strings.end() ||
		addition.size() > 16 * 1024 * 1024 - found->second.size())
		return;
	found->second.append(addition);
}

void NSMutableStringSetString(id self, SEL, id value)
{
	const char* bytes = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	if (bytes == nullptr) return;
	std::string replacement(bytes);
	if (replacement.size() > 16 * 1024 * 1024 ||
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, replacement.c_str(),
			-1, nullptr, 0) == 0)
		return;
	std::lock_guard lock(g_mutable_string_mutex);
	const auto found = g_mutable_strings.find(self);
	if (found != g_mutable_strings.end()) found->second.swap(replacement);
}

void NSMutableStringDeleteCharactersInRange(id self, SEL,
	DarlingObjcRange range)
{
	std::lock_guard lock(g_mutable_string_mutex);
	const auto found = g_mutable_strings.find(self);
	if (found == g_mutable_strings.end()) return;
	std::string replacement;
	if (UTF8ByDeletingRange(found->second, range, replacement))
		found->second.swap(replacement);
}

void NSMutableStringReplaceCharactersInRange(id self, SEL,
	DarlingObjcRange range, id replacement)
{
	const char* replacement_bytes = static_cast<const char*>(
		darling_objc_msgSend_pointer0(replacement, sel_registerName("UTF8String")));
	if (replacement_bytes == nullptr) return;
	const std::string replacement_copy(replacement_bytes);
	std::lock_guard lock(g_mutable_string_mutex);
	const auto found = g_mutable_strings.find(self);
	if (found == g_mutable_strings.end()) return;
	std::string changed;
	if (UTF8ByReplacingRange(found->second, range, replacement_copy, changed))
		found->second.swap(changed);
}

void NSMutableStringInsertString(id self, SEL, id value,
	std::uint64_t index)
{
	NSMutableStringReplaceCharactersInRange(self, nullptr, {index, 0}, value);
}

id NSStringStringByAppendingString(id self, SEL, id suffix)
{
	const char* left = NSStringUTF8String(self, nullptr);
	const char* right = static_cast<const char*>(darling_objc_msgSend_pointer0(
		suffix, sel_registerName("UTF8String")));
	if (left == nullptr || right == nullptr) return nullptr;
	std::string combined(left);
	const std::size_t suffix_length = std::strlen(right);
	if (suffix_length > 16 * 1024 * 1024 - combined.size()) return nullptr;
	combined.append(right, suffix_length);
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		combined.c_str()));
}

bool NSStringHasPrefix(id self, SEL, id prefix)
{
	const char* value = NSStringUTF8String(self, nullptr);
	const char* candidate = static_cast<const char*>(darling_objc_msgSend_pointer0(
		prefix, sel_registerName("UTF8String")));
	if (value == nullptr || candidate == nullptr) return false;
	const std::size_t length = std::strlen(candidate);
	return std::strncmp(value, candidate, length) == 0;
}

bool NSStringHasSuffix(id self, SEL, id suffix)
{
	const char* value = NSStringUTF8String(self, nullptr);
	const char* candidate = static_cast<const char*>(darling_objc_msgSend_pointer0(
		suffix, sel_registerName("UTF8String")));
	if (value == nullptr || candidate == nullptr) return false;
	const std::size_t value_length = std::strlen(value);
	const std::size_t suffix_length = std::strlen(candidate);
	return suffix_length <= value_length &&
		std::memcmp(value + value_length - suffix_length, candidate,
			suffix_length) == 0;
}

id NSApplicationSharedApplication(Class requested_class, SEL)
{
	if (g_shared_application == nullptr) {
		const Class base_class = objc_getClass("NSApplication");
		const Class cls = requested_class == nullptr ? base_class : requested_class;
		if (cls == nullptr || base_class == nullptr) return nullptr;
		bool is_application = false;
		Class current = cls;
		for (unsigned depth = 0; current != nullptr && depth < 256; ++depth) {
			if (current == base_class) { is_application = true; break; }
			current = class_getSuperclass(current);
		}
		if (!is_application) return nullptr;
		id application = class_createInstance(cls, 0);
		if (application == nullptr) return nullptr;
		application = darling_objc_msgSend_object0(application,
			sel_registerName("init"));
		if (application == nullptr) return nullptr;
		g_shared_application = application;
	}
	return g_shared_application;
}

void NSApplicationSetDelegate(id, SEL, id delegate)
{
	if (delegate == g_application_delegate) return;
	if (delegate != nullptr) objc_retain(delegate);
	if (g_application_delegate != nullptr) objc_release(g_application_delegate);
	g_application_delegate = delegate;
}

id NSApplicationMainMenu(id, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	return g_application_main_menu == nullptr ? nullptr :
		objc_retainAutorelease(g_application_main_menu);
}

void NSApplicationSetMainMenu(id, SEL, id menu)
{
	if (menu != nullptr) objc_retain(menu);
	id previous = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		previous = std::exchange(g_application_main_menu, menu);
	}
	if (previous != nullptr) objc_release(previous);
	RefreshAppKitMenus();
}

id NSApplicationDelegate(id, SEL)
{
	return g_application_delegate;
}

id ResolveResponderActionTarget(id first_responder, SEL action)
{
	if (first_responder == nullptr || action == nullptr) return nullptr;
	id current = objc_retain(first_responder);
	std::vector<id> visited;
	while (current != nullptr && visited.size() < 64) {
		if (std::find(visited.begin(), visited.end(), current) != visited.end()) {
			objc_release(current);
			return nullptr;
		}
		visited.push_back(current);
		if (class_getInstanceMethod(object_getClass(current), action) != nullptr) {
			return current;
		}
		const SEL next = sel_registerName("nextResponder");
		id following = class_getInstanceMethod(object_getClass(current), next) == nullptr
			? nullptr : darling_objc_msgSend_object0(current, next);
		if (following != nullptr) objc_retain(following);
		objc_release(current);
		current = following;
	}
	if (current != nullptr) objc_release(current);
	return nullptr;
}

id ResolveApplicationActionTarget(SEL action)
{
	if (action == nullptr) return nullptr;
	id key_window = nullptr;
	id main_window = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		if (g_key_appkit_window != nullptr)
			key_window = objc_retain(g_key_appkit_window);
		if (g_main_appkit_window != nullptr)
			main_window = objc_retain(g_main_appkit_window);
	}
	const auto window_target = [action](id window) -> id {
		if (window == nullptr) return nullptr;
		id first_responder = nullptr;
		id delegate = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			const auto found = g_windows.find(window);
			if (found != g_windows.end()) {
				if (found->second.first_responder != nullptr)
					first_responder = objc_retain(found->second.first_responder);
				if (found->second.delegate != nullptr)
					delegate = objc_retain(found->second.delegate);
			}
		}
		id target = ResolveResponderActionTarget(first_responder, action);
		if (first_responder != nullptr) objc_release(first_responder);
		if (target == nullptr && delegate != nullptr &&
			class_getInstanceMethod(object_getClass(delegate), action) != nullptr)
			target = delegate;
		else if (delegate != nullptr)
			objc_release(delegate);
		return target;
	};
	id target = window_target(key_window);
	if (target == nullptr && main_window != key_window)
		target = window_target(main_window);
	if (key_window != nullptr) objc_release(key_window);
	if (main_window != nullptr) objc_release(main_window);
	if (target != nullptr) return target;
	if (NSApp != nullptr &&
		class_getInstanceMethod(object_getClass(NSApp), action) != nullptr)
		return objc_retain(NSApp);
	if (g_application_delegate != nullptr &&
		class_getInstanceMethod(object_getClass(g_application_delegate), action) != nullptr)
		return objc_retain(g_application_delegate);
	return nullptr;
}

bool NSApplicationSendAction(id, SEL, SEL action, id target, id sender)
{
	if (action == nullptr) return false;
	if (target != nullptr) {
		if (class_getInstanceMethod(object_getClass(target), action) == nullptr)
			return false;
		darling_objc_msgSend_void1_object(target, action, sender);
		return true;
	}
	id resolved = ResolveApplicationActionTarget(action);
	if (resolved == nullptr) return false;
	darling_objc_msgSend_void1_object(resolved, action, sender);
	objc_release(resolved);
	return true;
}

id NSApplicationTargetForAction(id, SEL, SEL action, id target, id)
{
	if (action == nullptr) return nullptr;
	if (target != nullptr) {
		return class_getInstanceMethod(object_getClass(target), action) == nullptr
			? nullptr : objc_retainAutorelease(target);
	}
	id key_window = nullptr;
	id first_responder = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		if (g_key_appkit_window != nullptr) {
			key_window = objc_retain(g_key_appkit_window);
			const auto found = g_windows.find(g_key_appkit_window);
			if (found != g_windows.end() && found->second.first_responder != nullptr)
				first_responder = objc_retain(found->second.first_responder);
		}
	}
	id resolved = ResolveResponderActionTarget(first_responder, action);
	if (first_responder != nullptr) objc_release(first_responder);
	if (key_window != nullptr) objc_release(key_window);
	return resolved == nullptr ? nullptr : objc_autoreleaseReturnValue(resolved);
}

id NSApplicationTargetForActionSimple(id self, SEL selector, SEL action)
{
	return NSApplicationTargetForAction(self, selector, action, nullptr, nullptr);
}

bool NSResponderTryToPerform(id self, SEL, SEL action, id object)
{
	if (self == nullptr || action == nullptr) return false;
	id target = ResolveResponderActionTarget(self, action);
	if (target != nullptr) {
		darling_objc_msgSend_void1_object(target, action, object);
		objc_release(target);
		return true;
	}
	id delegate = nullptr;
	if (self == g_shared_application || self == NSApp) {
		if (g_application_delegate != nullptr)
			delegate = objc_retain(g_application_delegate);
	} else {
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end() && found->second.delegate != nullptr)
			delegate = objc_retain(found->second.delegate);
	}
	if (delegate == nullptr) return false;
	if (class_getInstanceMethod(object_getClass(delegate), action) == nullptr) {
		objc_release(delegate);
		return false;
	}
	darling_objc_msgSend_void1_object(delegate, action, object);
	objc_release(delegate);
	return true;
}

bool NSResponderPerformKeyEquivalent(id, SEL, id)
{
	return false;
}

bool NSViewPerformKeyEquivalent(id self, SEL, id event)
{
	if (NSViewIsHiddenOrHasHiddenAncestor(self, nullptr)) return false;
	std::vector<id> subviews;
	try {
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return false;
		subviews = found->second.subviews;
		for (id subview : subviews) objc_retain(subview);
	} catch (...) {
		for (id subview : subviews) objc_release(subview);
		return false;
	}
	const SEL perform = sel_registerName("performKeyEquivalent:");
	for (auto subview = subviews.rbegin(); subview != subviews.rend(); ++subview) {
		if (darling_objc_msgSend_bool_object1(*subview, perform, event)) {
			for (id retained : subviews) objc_release(retained);
			return true;
		}
	}
	for (id retained : subviews) objc_release(retained);
	return false;
}

bool NSWindowPerformKeyEquivalent(id self, SEL, id event)
{
	id content_view = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end() && found->second.content_view != nullptr)
			content_view = objc_retain(found->second.content_view);
	}
	if (content_view == nullptr) return false;
	const bool handled = darling_objc_msgSend_bool_object1(content_view,
		sel_registerName("performKeyEquivalent:"), event);
	objc_release(content_view);
	return handled;
}

id NSApplicationKeyWindow(id, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	return g_key_appkit_window == nullptr ? nullptr :
		objc_retainAutorelease(g_key_appkit_window);
}

id NSApplicationMainWindow(id, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	return g_main_appkit_window == nullptr ? nullptr :
		objc_retainAutorelease(g_main_appkit_window);
}

bool HasVisibleAppKitWindow()
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	for (const auto& entry : g_windows) {
		if (entry.second.visible && entry.second.handle != nullptr) return true;
	}
	return false;
}

HWND CreateAppKitWindow(id object, const wchar_t* title,
	int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int width = 640,
	int height = 480, DWORD style = WS_OVERLAPPEDWINDOW)
{
	if (!RegisterBootstrapWindowClass()) return nullptr;
	return CreateWindowExW(0, kWindowClass, title, style,
		x, y, width, height, nullptr, nullptr,
		GetModuleHandleW(nullptr), object);
}

int ClampWindowCoordinate(double value, int low, int high)
{
	if (!std::isfinite(value)) return low;
	return static_cast<int>(std::clamp(std::round(value),
		static_cast<double>(low), static_cast<double>(high)));
}

DWORD Win32WindowStyle(std::uint64_t style_mask)
{
	constexpr std::uint64_t titled = 1ull << 0;
	constexpr std::uint64_t closable = 1ull << 1;
	constexpr std::uint64_t miniaturizable = 1ull << 2;
	constexpr std::uint64_t resizable = 1ull << 3;
	DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	if ((style_mask & titled) != 0) style |= WS_CAPTION;
	else style |= WS_POPUP;
	if ((style_mask & closable) != 0) style |= WS_SYSMENU;
	if ((style_mask & resizable) != 0) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
	if ((style_mask & miniaturizable) != 0) style |= WS_MINIMIZEBOX;
	return style;
}

struct NSWindowFrameInsets final {
	int left{};
	int top{};
	int right{};
	int bottom{};
};

bool QueryNSWindowFrameInsets(std::uint64_t style_mask,
	NSWindowFrameInsets& insets)
{
	RECT frame{0, 0, 1, 1};
	if (!AdjustWindowRectEx(&frame, Win32WindowStyle(style_mask), FALSE, 0))
		return false;
	insets = {-frame.left, -frame.top, frame.right - 1, frame.bottom - 1};
	return true;
}

DarlingObjcRect NSWindowFrameRectForContentRect(Class, SEL,
	DarlingObjcRect content_rect, std::uint64_t style_mask)
{
	NSWindowFrameInsets insets{};
	if (!QueryNSWindowFrameInsets(style_mask, insets)) return content_rect;
	return {content_rect.x - insets.left, content_rect.y - insets.bottom,
		content_rect.width + insets.left + insets.right,
		content_rect.height + insets.top + insets.bottom};
}

DarlingObjcRect NSWindowContentRectForFrameRect(Class, SEL,
	DarlingObjcRect frame_rect, std::uint64_t style_mask)
{
	NSWindowFrameInsets insets{};
	if (!QueryNSWindowFrameInsets(style_mask, insets)) return frame_rect;
	return {frame_rect.x + insets.left, frame_rect.y + insets.bottom,
		frame_rect.width - insets.left - insets.right,
		frame_rect.height - insets.top - insets.bottom};
}

id NSWindowInitWithContentRect(id self, SEL, DarlingObjcRect content_rect,
	std::uint64_t style_mask, std::uint64_t, std::uint64_t)
{
	const int screen_height = GetSystemMetrics(SM_CYSCREEN);
	const int x = ClampWindowCoordinate(content_rect.x, -16000, 16000);
	const int y = ClampWindowCoordinate(screen_height - content_rect.y -
		content_rect.height, -16000, 16000);
	const int width = std::max(1, ClampWindowCoordinate(content_rect.width,
		1, 10000));
	const int height = std::max(1, ClampWindowCoordinate(content_rect.height,
		1, 10000));
	DWORD style = Win32WindowStyle(style_mask);
	RECT frame{0, 0, width, height};
	if (AdjustWindowRectEx(&frame, style, FALSE, 0)) {
		const int outer_x = ClampWindowCoordinate(x + frame.left, -16000, 16000);
		const int outer_y = ClampWindowCoordinate(y + frame.top, -16000, 16000);
		const int outer_width = frame.right - frame.left;
		const int outer_height = frame.bottom - frame.top;
		HWND handle = CreateAppKitWindow(self, L"Untitled", outer_x, outer_y,
			outer_width, outer_height, style);
		if (handle == nullptr) return nullptr;
		const Class view_class = objc_getClass("NSView");
		id root_view = view_class == nullptr ? nullptr :
			class_createInstance(view_class, 0);
		if (root_view != nullptr) {
			NSViewInitWithFrame(root_view, sel_registerName("initWithFrame:"),
				DarlingObjcRect{0.0, 0.0, static_cast<double>(width),
					static_cast<double>(height)});
		}
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			AppKitWindowState& state = g_windows[self];
			state.handle = handle;
			state.content_rect = content_rect;
			state.style_mask = style_mask;
			state.content_view = root_view;
			state.first_responder = self;
			if (root_view != nullptr)
				g_views[root_view].window = self;
		}
		ApplyAppKitMenuToWindow(self);
		return self;
	}
	return nullptr;
}

std::wstring Utf8ToWide(const char* utf8)
{
	if (utf8 == nullptr) return {};
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8,
		-1, nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
		result.data(), length) != length) return {};
	result.resize(static_cast<std::size_t>(length - 1));
	return result;
}

std::wstring NativeTitle(id title)
{
	if (title == nullptr) return {};
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		title, sel_registerName("UTF8String")));
	return Utf8ToWide(utf8);
}

void NSMenuFinalize(id object)
{
	AppKitMenuState state{};
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menus.find(object);
		if (found == g_menus.end()) return;
		state = std::move(found->second);
		g_menus.erase(found);
	}
	if (state.title != nullptr) objc_release(state.title);
	for (id item : state.items) objc_release(item);
}

void NSAlertFinalize(id object)
{
	AppKitAlertState state{};
	{
		std::lock_guard lock(g_alerts_mutex);
		const auto found = g_alerts.find(object);
		if (found == g_alerts.end()) return;
		state = std::move(found->second);
		g_alerts.erase(found);
	}
	if (state.message_text != nullptr) objc_release(state.message_text);
	if (state.informative_text != nullptr) objc_release(state.informative_text);
	for (id button : state.buttons) objc_release(button);
}

id NSAlertInit(id self, SEL)
{
	if (self == nullptr) return nullptr;
	try {
		bool inserted = false;
		{
			std::lock_guard lock(g_alerts_mutex);
			inserted = g_alerts.try_emplace(self).second;
		}
		if (!inserted) return self;
		if (!darling_objc_register_object_finalizer(self, &NSAlertFinalize)) {
			NSAlertFinalize(self);
			return nullptr;
		}
		return self;
	} catch (...) {
		NSAlertFinalize(self);
		return nullptr;
	}
}

id NSAlertMessageText(id self, SEL)
{
	std::lock_guard lock(g_alerts_mutex);
	const auto found = g_alerts.find(self);
	return found == g_alerts.end() ? nullptr : found->second.message_text;
}

void NSAlertSetMessageText(id self, SEL, id value)
{
	if (value != nullptr) objc_retain(value);
	id previous = nullptr;
	bool found_alert = false;
	{
		std::lock_guard lock(g_alerts_mutex);
		const auto found = g_alerts.find(self);
		if (found != g_alerts.end()) {
			found_alert = true;
			previous = std::exchange(found->second.message_text, value);
		}
	}
	if (!found_alert && value != nullptr) objc_release(value);
	if (previous != nullptr) objc_release(previous);
}

id NSAlertInformativeText(id self, SEL)
{
	std::lock_guard lock(g_alerts_mutex);
	const auto found = g_alerts.find(self);
	return found == g_alerts.end() ? nullptr : found->second.informative_text;
}

void NSAlertSetInformativeText(id self, SEL, id value)
{
	if (value != nullptr) objc_retain(value);
	id previous = nullptr;
	bool found_alert = false;
	{
		std::lock_guard lock(g_alerts_mutex);
		const auto found = g_alerts.find(self);
		if (found != g_alerts.end()) {
			found_alert = true;
			previous = std::exchange(found->second.informative_text, value);
		}
	}
	if (!found_alert && value != nullptr) objc_release(value);
	if (previous != nullptr) objc_release(previous);
}

id NSAlertAddButtonWithTitle(id self, SEL, id title)
{
	const Class button_class = objc_getClass("NSButton");
	if (self == nullptr || button_class == nullptr || title == nullptr) return nullptr;
	{
		std::lock_guard lock(g_alerts_mutex);
		const auto found = g_alerts.find(self);
		if (found == g_alerts.end() || found->second.buttons.size() >= 3)
			return nullptr;
	}
	id button = NSButtonCreateWithTitleTargetAction(button_class,
		sel_registerName("buttonWithTitle:target:action:"), title, nullptr, nullptr);
	if (button == nullptr) return nullptr;
	objc_retain(button);
	try {
		std::lock_guard lock(g_alerts_mutex);
		const auto found = g_alerts.find(self);
		if (found == g_alerts.end()) {
			objc_release(button);
			return nullptr;
		}
		found->second.buttons.push_back(button);
		return button;
	} catch (...) {
		objc_release(button);
		return nullptr;
	}
}

std::uint64_t NSAlertStyle(id self, SEL)
{
	std::lock_guard lock(g_alerts_mutex);
	const auto found = g_alerts.find(self);
	return found == g_alerts.end() ? 0 : found->second.style;
}

void NSAlertSetStyle(id self, SEL, std::uint64_t style)
{
	std::lock_guard lock(g_alerts_mutex);
	const auto found = g_alerts.find(self);
	if (found != g_alerts.end()) found->second.style = style;
}

std::int64_t NSAlertRunModal(id self, SEL)
{
	id message_object = nullptr;
	id informative_object = nullptr;
	std::vector<id> button_objects;
	std::uint64_t style{};
	try {
		std::lock_guard lock(g_alerts_mutex);
		const auto found = g_alerts.find(self);
		if (found == g_alerts.end()) return -1001;
		message_object = found->second.message_text == nullptr ? nullptr :
			objc_retain(found->second.message_text);
		informative_object = found->second.informative_text == nullptr ? nullptr :
			objc_retain(found->second.informative_text);
		style = found->second.style;
		button_objects.reserve(found->second.buttons.size());
		for (id button : found->second.buttons)
			button_objects.push_back(objc_retain(button));
	} catch (...) {
		if (message_object != nullptr) objc_release(message_object);
		if (informative_object != nullptr) objc_release(informative_object);
		for (id button : button_objects) objc_release(button);
		return -1001;
	}
	const char* message_text = NSStringUTF8String(message_object, nullptr);
	const char* informative_text = NSStringUTF8String(informative_object, nullptr);
	std::wstring title = message_text == nullptr ? L"Application Alert" :
		Utf8ToWide(message_text);
	std::wstring content = informative_text == nullptr ? L"" :
		Utf8ToWide(informative_text);
	std::vector<std::wstring> button_titles;
	try {
		button_titles.reserve(button_objects.size());
		for (id button : button_objects) {
			const id button_title = NSButtonTitle(button, nullptr);
			const char* text = NSStringUTF8String(button_title, nullptr);
			button_titles.push_back(text == nullptr ? L"OK" : Utf8ToWide(text));
		}
	} catch (...) {
		button_titles.clear();
	}
	if (button_titles.empty()) button_titles.emplace_back(L"OK");
	std::vector<TASKDIALOG_BUTTON> buttons;
	try {
		buttons.reserve(button_titles.size());
		for (std::size_t index = 0; index < button_titles.size(); ++index)
			buttons.push_back(TASKDIALOG_BUTTON{
				1000 + static_cast<int>(index), button_titles[index].c_str()});
	} catch (...) {
		buttons.clear();
	}
	int selected = 0;
	HRESULT result = E_OUTOFMEMORY;
	if (!buttons.empty()) {
		TASKDIALOGCONFIG config{};
		config.cbSize = sizeof(config);
		config.hwndParent = GetActiveWindow();
		config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
		config.pszWindowTitle = L"Wintosh";
		config.pszMainInstruction = title.c_str();
		config.pszContent = content.c_str();
		config.cButtons = static_cast<UINT>(buttons.size());
		config.pButtons = buttons.data();
		config.nDefaultButton = buttons.front().nButtonID;
		config.pszMainIcon = style == 2 ? TD_ERROR_ICON :
			(style == 0 ? TD_WARNING_ICON : TD_INFORMATION_ICON);
		result = TaskDialogIndirect(&config, &selected, nullptr, nullptr);
	}
	if (FAILED(result)) {
		UINT flags = MB_OK;
		if (style == 2) flags |= MB_ICONERROR;
		else if (style == 0) flags |= MB_ICONWARNING;
		else flags |= MB_ICONINFORMATION;
		(void)MessageBoxW(GetActiveWindow(), content.empty() ? title.c_str() :
			content.c_str(), title.c_str(), flags);
		selected = 1000;
	}
	if (message_object != nullptr) objc_release(message_object);
	if (informative_object != nullptr) objc_release(informative_object);
	for (id button : button_objects) objc_release(button);
	return selected >= 1000 ? 1000 + (selected - 1000) : -1001;
}

void NSURLFinalize(id object)
{
	id path = nullptr;
	{
		std::lock_guard lock(g_urls_mutex);
		const auto found = g_urls.find(object);
		if (found == g_urls.end()) return;
		path = std::exchange(found->second.path, nullptr);
		g_urls.erase(found);
	}
	if (path != nullptr) objc_release(path);
}

id NSURLInitWithFileSystemPath(id self, SEL, id path)
{
	if (self == nullptr || path == nullptr) return nullptr;
	const char* utf8 = NSStringUTF8String(path, nullptr);
	if (utf8 == nullptr) return nullptr;
	id retained_path = objc_retain(path);
	id previous_path = nullptr;
	try {
		{
			std::lock_guard lock(g_urls_mutex);
			auto [found, inserted] = g_urls.try_emplace(self,
				AppKitURLState{retained_path});
			if (!inserted)
				previous_path = std::exchange(found->second.path, retained_path);
		}
		if (previous_path != nullptr) objc_release(previous_path);
		if (!darling_objc_register_object_finalizer(self, &NSURLFinalize)) {
			NSURLFinalize(self);
			return nullptr;
		}
		return self;
	} catch (...) {
		objc_release(retained_path);
		return nullptr;
	}
}

id NSURLCreateFileURL(Class cls, id path)
{
	if (cls == nullptr || path == nullptr) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	if (NSURLInitWithFileSystemPath(object, nullptr, path) == nullptr) {
		objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

id NSURLCreateFileURLFromWidePath(std::wstring_view path)
{
	try {
		const std::string utf8 = WideToUtf8(path);
		id string = CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
		if (string == nullptr) return nullptr;
		id result = NSURLCreateFileURL(objc_getClass("NSURL"), string);
		objc_release(string);
		return result;
	} catch (...) { return nullptr; }
}

id NSURLFileURLWithPath(Class cls, SEL, id path)
{
	return NSURLCreateFileURL(cls, path);
}

id NSURLPath(id self, SEL)
{
	std::lock_guard lock(g_urls_mutex);
	const auto found = g_urls.find(self);
	return found == g_urls.end() ? nullptr : found->second.path;
}

id NSURLAbsoluteString(id self, SEL)
{
	id path = NSURLPath(self, nullptr);
	const char* utf8 = NSStringUTF8String(path, nullptr);
	if (utf8 == nullptr) return nullptr;
	try {
		static constexpr char hex[] = "0123456789ABCDEF";
		std::string url = "file:///";
		for (const unsigned char character : std::string_view(utf8)) {
			const unsigned char value = character == '\\' ? '/' : character;
			const bool unreserved = (value >= 'a' && value <= 'z') ||
				(value >= 'A' && value <= 'Z') ||
				(value >= '0' && value <= '9') || value == '/' || value == ':' ||
				value == '-' || value == '.' || value == '_' || value == '~';
			if (unreserved) url.push_back(static_cast<char>(value));
			else {
				url.push_back('%');
				url.push_back(hex[value >> 4]);
				url.push_back(hex[value & 0x0f]);
			}
		}
		return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
			url.c_str());
	} catch (...) { return nullptr; }
}

bool NSURLIsFileURL(id self, SEL)
{
	return NSURLPath(self, nullptr) != nullptr;
}

bool NSURLCopyWidePath(id self, std::wstring& path)
{
	id path_object = nullptr;
	{
		std::lock_guard lock(g_urls_mutex);
		const auto found = g_urls.find(self);
		if (found == g_urls.end() || found->second.path == nullptr) return false;
		path_object = objc_retain(found->second.path);
	}
	const char* utf8 = NSStringUTF8String(path_object, nullptr);
	try {
		path = utf8 == nullptr ? std::wstring{} : Utf8ToWide(utf8);
	} catch (...) {
		objc_release(path_object);
		return false;
	}
	objc_release(path_object);
	return utf8 != nullptr;
}

id NSURLByAppendingPathComponent(id self, SEL, id component)
{
	std::wstring base;
	const char* utf8 = NSStringUTF8String(component, nullptr);
	if (utf8 == nullptr || !NSURLCopyWidePath(self, base)) return nullptr;
	try {
		const std::filesystem::path combined = std::filesystem::path(base) /
			std::filesystem::path(Utf8ToWide(utf8));
		return NSURLCreateFileURLFromWidePath(combined.native());
	} catch (...) { return nullptr; }
}

id NSURLByDeletingLastPathComponent(id self, SEL)
{
	std::wstring path;
	if (!NSURLCopyWidePath(self, path)) return nullptr;
	try {
		return NSURLCreateFileURLFromWidePath(
			std::filesystem::path(path).parent_path().native());
	} catch (...) { return nullptr; }
}

id NSURLLastPathComponent(id self, SEL)
{
	std::wstring path;
	if (!NSURLCopyWidePath(self, path)) return nullptr;
	try {
		const std::wstring component = std::filesystem::path(path).filename().native();
		const std::string utf8 = WideToUtf8(component);
		return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
			utf8.c_str());
	} catch (...) { return nullptr; }
}

id NSURLPathExtension(id self, SEL)
{
	std::wstring path;
	if (!NSURLCopyWidePath(self, path)) return nullptr;
	try {
		std::wstring extension = std::filesystem::path(path).extension().native();
		if (!extension.empty() && extension.front() == L'.') extension.erase(0, 1);
		const std::string utf8 = WideToUtf8(extension);
		return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
			utf8.c_str());
	} catch (...) { return nullptr; }
}

id NSURLByDeletingPathExtension(id self, SEL)
{
	std::wstring path;
	if (!NSURLCopyWidePath(self, path)) return nullptr;
	try {
		std::filesystem::path without_extension(path);
		without_extension.replace_extension();
		return NSURLCreateFileURLFromWidePath(without_extension.native());
	} catch (...) { return nullptr; }
}

id NSURLStandardizedFileURL(id self, SEL)
{
	std::wstring path;
	if (!NSURLCopyWidePath(self, path)) return nullptr;
	try {
		return NSURLCreateFileURLFromWidePath(
			std::filesystem::path(path).lexically_normal().native());
	} catch (...) { return nullptr; }
}

std::wstring_view UTTypeExtensionForIdentifier(std::string_view identifier)
{
	static constexpr std::pair<std::string_view, std::wstring_view> known[] = {
		{"public.plain-text", L"txt"}, {"public.utf8-plain-text", L"txt"},
		{"public.json", L"json"}, {"public.xml", L"xml"},
		{"public.html", L"html"}, {"public.jpeg", L"jpg"},
		{"public.png", L"png"}, {"com.compuserve.gif", L"gif"},
		{"public.tiff", L"tif"}, {"com.adobe.pdf", L"pdf"},
		{"public.zip-archive", L"zip"}, {"public.csv", L"csv"},
		{"public.rtf", L"rtf"}, {"public.mpeg-4", L"mp4"},
		{"public.mp3", L"mp3"}, {"public.wav-audio", L"wav"},
		{"org.openxmlformats.wordprocessingml.document", L"docx"},
		{"org.openxmlformats.spreadsheetml.sheet", L"xlsx"},
		{"org.openxmlformats.presentationml.presentation", L"pptx"},
		{"com.microsoft.word.doc", L"doc"},
		{"com.microsoft.excel.xls", L"xls"},
		{"com.microsoft.powerpoint.ppt", L"ppt"}
	};
	for (const auto& [name, extension] : known)
		if (identifier == name) return extension;
	return {};
}

std::string_view UTTypeIdentifierForExtension(std::wstring_view extension)
{
	static constexpr std::pair<std::string_view, std::wstring_view> known[] = {
		{"public.plain-text", L"txt"}, {"public.json", L"json"},
		{"public.xml", L"xml"}, {"public.html", L"html"},
		{"public.jpeg", L"jpg"}, {"public.png", L"png"},
		{"com.compuserve.gif", L"gif"}, {"public.tiff", L"tif"},
		{"com.adobe.pdf", L"pdf"}, {"public.zip-archive", L"zip"},
		{"public.csv", L"csv"}, {"public.rtf", L"rtf"},
		{"public.mpeg-4", L"mp4"}, {"public.mp3", L"mp3"},
		{"public.wav-audio", L"wav"},
		{"org.openxmlformats.wordprocessingml.document", L"docx"},
		{"org.openxmlformats.spreadsheetml.sheet", L"xlsx"},
		{"org.openxmlformats.presentationml.presentation", L"pptx"},
		{"com.microsoft.word.doc", L"doc"},
		{"com.microsoft.excel.xls", L"xls"},
		{"com.microsoft.powerpoint.ppt", L"ppt"}
	};
	for (const auto& [identifier, known_extension] : known)
		if (extension == known_extension) return identifier;
	return {};
}

void UTTypeFinalize(id object)
{
	id identifier = nullptr;
	{
		std::lock_guard lock(g_ut_types_mutex);
		const auto found = g_ut_types.find(object);
		if (found == g_ut_types.end()) return;
		identifier = std::exchange(found->second.identifier, nullptr);
		g_ut_types.erase(found);
	}
	if (identifier != nullptr) objc_release(identifier);
}

id UTTypeCreate(Class cls, id identifier, std::wstring extension)
{
	if (cls == nullptr || identifier == nullptr) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	id retained_identifier = objc_retain(identifier);
	try {
		{
			std::lock_guard lock(g_ut_types_mutex);
			g_ut_types.emplace(object,
				AppKitUTTypeState{retained_identifier, std::move(extension)});
		}
		if (!darling_objc_register_object_finalizer(object, &UTTypeFinalize)) {
			UTTypeFinalize(object);
			objc_release(object);
			return nullptr;
		}
		return objc_autorelease(object);
	} catch (...) {
		objc_release(retained_identifier);
		objc_release(object);
		return nullptr;
	}
}

id UTTypeTypeWithIdentifier(Class cls, SEL, id identifier)
{
	const char* utf8 = NSStringUTF8String(identifier, nullptr);
	if (utf8 == nullptr) return nullptr;
	try {
		return UTTypeCreate(cls, identifier, std::wstring(
			UTTypeExtensionForIdentifier(utf8)));
	} catch (...) { return nullptr; }
}

id UTTypeTypeWithIdentifierConformingToType(Class cls, SEL, id identifier, id)
{
	return UTTypeTypeWithIdentifier(cls, nullptr, identifier);
}

id UTTypeTypeWithFilenameExtension(Class cls, SEL, id extension_object)
{
	const char* utf8 = NSStringUTF8String(extension_object, nullptr);
	if (utf8 == nullptr) return nullptr;
	try {
		std::wstring extension = Utf8ToWide(utf8);
		if (!extension.empty() && extension.front() == L'.') extension.erase(0, 1);
		const auto known_identifier = UTTypeIdentifierForExtension(extension);
		std::string identifier;
		if (known_identifier.empty()) identifier = "dyn." + std::string(utf8);
		else identifier.assign(known_identifier);
		id identifier_object = CreateDynamicNSString(objc_getClass("NSString"),
			identifier.c_str());
		if (identifier_object == nullptr) return nullptr;
		id result = UTTypeCreate(cls, identifier_object, std::move(extension));
		objc_release(identifier_object);
		return result;
	} catch (...) { return nullptr; }
}

id UTTypeTypeWithFilenameExtensionConformingToType(Class cls, SEL,
	id extension, id)
{
	return UTTypeTypeWithFilenameExtension(cls, nullptr, extension);
}

id UTTypeIdentifier(id self, SEL)
{
	std::lock_guard lock(g_ut_types_mutex);
	const auto found = g_ut_types.find(self);
	return found == g_ut_types.end() ? nullptr : found->second.identifier;
}

id UTTypePreferredFilenameExtension(id self, SEL)
{
	std::wstring extension;
	{
		std::lock_guard lock(g_ut_types_mutex);
		const auto found = g_ut_types.find(self);
		if (found == g_ut_types.end() || found->second.preferred_extension.empty())
			return nullptr;
		extension = found->second.preferred_extension;
	}
	try {
		const std::string utf8 = WideToUtf8(extension);
		return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
			utf8.c_str());
	} catch (...) { return nullptr; }
}

bool UTTypeConformsTo(id self, SEL, id other)
{
	const char* target = NSStringUTF8String(UTTypeIdentifier(other, nullptr), nullptr);
	if (target == nullptr) return false;
	if (std::strcmp(target, "public.item") == 0 ||
		std::strcmp(target, "public.data") == 0) return true;
	const char* source = NSStringUTF8String(UTTypeIdentifier(self, nullptr), nullptr);
	return source != nullptr && std::strcmp(source, target) == 0;
}

void NSErrorFinalize(id object)
{
	AppKitNSErrorState state{};
	{
		std::lock_guard lock(g_nserrors_mutex);
		const auto found = g_nserrors.find(object);
		if (found == g_nserrors.end()) return;
		state = std::exchange(found->second, {});
		g_nserrors.erase(found);
	}
	if (state.domain != nullptr) objc_release(state.domain);
	if (state.localized_description != nullptr)
		objc_release(state.localized_description);
	if (state.user_info != nullptr) objc_release(state.user_info);
}

id NSErrorCreate(Class cls, id domain, std::int64_t code, id user_info,
	id localized_description)
{
	if (cls == nullptr || domain == nullptr || localized_description == nullptr)
		return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	AppKitNSErrorState state{objc_retain(domain),
		objc_retain(localized_description),
		user_info == nullptr ? nullptr : objc_retain(user_info), code};
	try {
		{
			std::lock_guard lock(g_nserrors_mutex);
			g_nserrors.emplace(object, state);
		}
		if (!darling_objc_register_object_finalizer(object, &NSErrorFinalize)) {
			NSErrorFinalize(object);
			objc_release(object);
			return nullptr;
		}
		return objc_autorelease(object);
	} catch (...) {
		if (state.domain != nullptr) objc_release(state.domain);
		if (state.localized_description != nullptr)
			objc_release(state.localized_description);
		if (state.user_info != nullptr) objc_release(state.user_info);
		objc_release(object);
		return nullptr;
	}
}

id NSErrorErrorWithDomainCodeUserInfo(Class cls, SEL, id domain,
	std::int64_t code, id user_info)
{
	if (domain == nullptr) return nullptr;
	std::string description = "Error " + std::to_string(code);
	if (user_info != nullptr) {
		id key = NSLocalizedDescriptionKey;
		if (key == nullptr) key = NSStringStringWithUTF8String(
			objc_getClass("NSString"), nullptr, "NSLocalizedDescription");
		if (key != nullptr) {
			id value = static_cast<id>(darling_objc_msgSend_object_pointer1(
				user_info, sel_registerName("objectForKey:"), key));
			if (value != nullptr) {
				const char* utf8 = NSStringUTF8String(value, nullptr);
				if (utf8 != nullptr) description = utf8;
			}
		}
	}
	id description_object = CreateDynamicNSString(objc_getClass("NSString"),
		description.c_str());
	if (description_object == nullptr) return nullptr;
	id result = NSErrorCreate(cls, domain, code, user_info, description_object);
	objc_release(description_object);
	return result;
}

id NSErrorInitWithDomainCodeUserInfo(id self, SEL, id domain,
	std::int64_t code, id user_info)
{
	if (self == nullptr || domain == nullptr) return nullptr;
	std::string description = "Error " + std::to_string(code);
	if (user_info != nullptr && NSLocalizedDescriptionKey != nullptr) {
		id value = static_cast<id>(darling_objc_msgSend_object_pointer1(
			user_info, sel_registerName("objectForKey:"),
			NSLocalizedDescriptionKey));
		const char* utf8 = NSStringUTF8String(value, nullptr);
		if (utf8 != nullptr) description = utf8;
	}
	id description_object = CreateDynamicNSString(objc_getClass("NSString"),
		description.c_str());
	if (description_object == nullptr) return nullptr;
	AppKitNSErrorState state{objc_retain(domain),
		objc_retain(description_object),
		user_info == nullptr ? nullptr : objc_retain(user_info), code};
	objc_release(description_object);
	bool attached = false;
	try {
		AppKitNSErrorState previous{};
		{
			std::lock_guard lock(g_nserrors_mutex);
			auto [found, inserted] = g_nserrors.try_emplace(self, state);
			if (!inserted) previous = std::exchange(found->second, state);
			attached = true;
		}
		if (previous.domain != nullptr) objc_release(previous.domain);
		if (previous.localized_description != nullptr)
			objc_release(previous.localized_description);
		if (previous.user_info != nullptr) objc_release(previous.user_info);
		if (!darling_objc_register_object_finalizer(self, &NSErrorFinalize)) {
			NSErrorFinalize(self);
			return nullptr;
		}
		return self;
	} catch (...) {
		if (attached) NSErrorFinalize(self);
		else {
			if (state.domain != nullptr) objc_release(state.domain);
			if (state.localized_description != nullptr)
				objc_release(state.localized_description);
			if (state.user_info != nullptr) objc_release(state.user_info);
		}
		return nullptr;
	}
}

id NSErrorDomain(id self, SEL)
{
	std::lock_guard lock(g_nserrors_mutex);
	const auto found = g_nserrors.find(self);
	return found == g_nserrors.end() ? nullptr : found->second.domain;
}

std::int64_t NSErrorCode(id self, SEL)
{
	std::lock_guard lock(g_nserrors_mutex);
	const auto found = g_nserrors.find(self);
	return found == g_nserrors.end() ? 0 : found->second.code;
}

id NSErrorUserInfo(id self, SEL)
{
	std::lock_guard lock(g_nserrors_mutex);
	const auto found = g_nserrors.find(self);
	return found == g_nserrors.end() ? nullptr : found->second.user_info;
}

id NSErrorLocalizedDescription(id self, SEL)
{
	std::lock_guard lock(g_nserrors_mutex);
	const auto found = g_nserrors.find(self);
	return found == g_nserrors.end() ? nullptr :
		found->second.localized_description;
}

void NSErrorSetOut(id* output, const std::error_code& error)
{
	if (output == nullptr) return;
	if (!error) { *output = nullptr; return; }
	try {
		const std::string message = error.message();
		id description = CreateDynamicNSString(objc_getClass("NSString"),
			message.c_str());
		if (description == nullptr) { *output = nullptr; return; }
		*output = NSErrorCreate(objc_getClass("NSError"), NSCocoaErrorDomain,
			error.value(), nullptr, description);
		objc_release(description);
	} catch (...) { *output = nullptr; }
}

bool NSFileManagerPath(id path_object, std::filesystem::path& path)
{
	const char* utf8 = NSStringUTF8String(path_object, nullptr);
	if (utf8 == nullptr) return false;
	try {
		path = std::filesystem::path(Utf8ToWide(utf8));
		return !path.empty();
	} catch (...) { return false; }
}

bool NSFileManagerCreateDirectory(id, SEL, id, bool, id, id*);
bool NSFileManagerRemoveItem(id, SEL, id, id*);
bool NSFileManagerCopyItem(id, SEL, id, id, id*);
bool NSFileManagerMoveItem(id, SEL, id, id, id*);

id NSFileManagerPathStringFromURL(id url)
{
	std::wstring path;
	if (!NSURLCopyWidePath(url, path)) return nullptr;
	try {
		const std::string utf8 = WideToUtf8(path);
		return CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
	} catch (...) { return nullptr; }
}

id NSFileManagerContentsOfDirectoryAtURL(id, SEL, id url, id, std::uint64_t,
	id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::wstring wide_directory;
	if (!NSURLCopyWidePath(url, wide_directory)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return nullptr;
	}
	const std::filesystem::path directory(wide_directory);
	std::vector<id> urls;
	std::error_code error;
	try {
		for (std::filesystem::directory_iterator iterator(directory, error), end;
			!error && iterator != end; iterator.increment(error)) {
			id child = NSURLCreateFileURLFromWidePath(iterator->path().native());
			if (child == nullptr) throw std::bad_alloc();
			urls.push_back(child);
		}
		if (error) {
			for (id child : urls) objc_release(child);
			NSErrorSetOut(error_output, error);
			return nullptr;
		}
		id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
			urls.data(), urls.size()));
		for (id child : urls) objc_release(child);
		return result;
	} catch (...) {
		for (id child : urls) objc_release(child);
		NSErrorSetOut(error_output, std::make_error_code(std::errc::not_enough_memory));
		return nullptr;
	}
}

bool NSFileManagerCreateDirectoryAtURL(id self, SEL, id url,
	bool create_intermediates, id attributes, id* error_output)
{
	id path = NSFileManagerPathStringFromURL(url);
	if (path == nullptr) {
		if (error_output != nullptr) {
			NSErrorSetOut(error_output,
				std::make_error_code(std::errc::invalid_argument));
		}
		return false;
	}
	const bool result = NSFileManagerCreateDirectory(self, nullptr, path,
		create_intermediates, attributes, error_output);
	objc_release(path);
	return result;
}

bool NSFileManagerRemoveItemAtURL(id self, SEL, id url, id* error_output)
{
	id path = NSFileManagerPathStringFromURL(url);
	if (path == nullptr) {
		if (error_output != nullptr)
			NSErrorSetOut(error_output,
				std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	const bool result = NSFileManagerRemoveItem(self, nullptr, path, error_output);
	objc_release(path);
	return result;
}

bool NSFileManagerCopyItemAtURL(id self, SEL, id source_url, id destination_url,
	id* error_output)
{
	id source = NSFileManagerPathStringFromURL(source_url);
	id destination = NSFileManagerPathStringFromURL(destination_url);
	if (source == nullptr || destination == nullptr) {
		if (source != nullptr) objc_release(source);
		if (destination != nullptr) objc_release(destination);
		if (error_output != nullptr)
			NSErrorSetOut(error_output,
				std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	const bool result = NSFileManagerCopyItem(self, nullptr, source, destination,
		error_output);
	objc_release(source);
	objc_release(destination);
	return result;
}

bool NSFileManagerMoveItemAtURL(id self, SEL, id source_url, id destination_url,
	id* error_output)
{
	id source = NSFileManagerPathStringFromURL(source_url);
	id destination = NSFileManagerPathStringFromURL(destination_url);
	if (source == nullptr || destination == nullptr) {
		if (source != nullptr) objc_release(source);
		if (destination != nullptr) objc_release(destination);
		if (error_output != nullptr)
			NSErrorSetOut(error_output,
				std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	const bool result = NSFileManagerMoveItem(self, nullptr, source, destination,
		error_output);
	objc_release(source);
	objc_release(destination);
	return result;
}

bool NSFileManagerDirectoryPath(std::uint64_t directory, std::uint64_t domain,
	std::filesystem::path& path)
{
	if (domain == 1 && directory == 15) {
		PWSTR downloads = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT,
		nullptr, &downloads)) || downloads == nullptr) return false;
		path = std::filesystem::path(downloads);
		CoTaskMemFree(downloads);
		return true;
	}
	int folder{};
	std::wstring suffix;
	if (domain == 1) {
		switch (directory) {
		case 9: folder = CSIDL_PERSONAL; break;
		case 12: folder = CSIDL_DESKTOPDIRECTORY; break;
		case 13: folder = CSIDL_LOCAL_APPDATA; suffix = L"Caches"; break;
		case 14: folder = CSIDL_APPDATA; suffix = L"Application Support"; break;
		case 17: folder = CSIDL_MYVIDEO; break;
		case 18: folder = CSIDL_MYMUSIC; break;
		case 19: folder = CSIDL_MYPICTURES; break;
		default: return false;
		}
	} else if (domain == 2) {
		if (directory == 9) folder = CSIDL_COMMON_DOCUMENTS;
		else if (directory == 13) {
			folder = CSIDL_COMMON_APPDATA;
			suffix = L"Caches";
		} else if (directory == 14) {
			folder = CSIDL_COMMON_APPDATA;
			suffix = L"Application Support";
		} else return false;
	} else return false;
	wchar_t base[MAX_PATH]{};
	if (FAILED(SHGetFolderPathW(nullptr, folder, nullptr,
		SHGFP_TYPE_CURRENT, base))) return false;
	path = std::filesystem::path(base);
	if (!suffix.empty()) path /= suffix;
	return true;
}

id NSFileManagerURLsForDirectory(id, SEL, std::uint64_t directory,
	std::uint64_t domains)
{
	std::vector<id> urls;
	try {
		for (const std::uint64_t domain : {1ull, 2ull}) {
			if ((domains & domain) == 0) continue;
			std::filesystem::path path;
			if (!NSFileManagerDirectoryPath(directory, domain, path)) continue;
			id url = NSURLCreateFileURLFromWidePath(path.native());
			if (url == nullptr) throw std::bad_alloc();
			urls.push_back(url);
		}
		id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
			urls.data(), urls.size()));
		for (id url : urls) objc_release(url);
		return result;
	} catch (...) {
		for (id url : urls) objc_release(url);
		return nullptr;
	}
}

id NSFileManagerURLForDirectory(id, SEL, std::uint64_t directory,
	std::uint64_t domain, id, bool create, id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path path;
	if (!NSFileManagerDirectoryPath(directory, domain, path)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::not_supported));
		return nullptr;
	}
	if (create) {
		std::error_code error;
		(void)std::filesystem::create_directories(path, error);
		if (error) {
			NSErrorSetOut(error_output, error);
			return nullptr;
		}
	}
	return NSURLCreateFileURLFromWidePath(path.native());
}

id NSFileManagerDefaultManager(Class, SEL)
{
	static std::once_flag once;
	static id manager = nullptr;
	std::call_once(once, [] {
		const Class cls = objc_getClass("NSFileManager");
		if (cls != nullptr) manager = class_createInstance(cls, 0);
	});
	return manager;
}

bool NSFileManagerFileExistsAtPath(id, SEL, id path_object)
{
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) return false;
	std::error_code error;
	return std::filesystem::exists(path, error) && !error;
}

bool NSFileManagerFileExistsAtPathIsDirectory(id, SEL, id path_object,
	bool* is_directory)
{
	if (is_directory != nullptr) *is_directory = false;
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) return false;
	std::error_code error;
	const bool exists = std::filesystem::exists(path, error);
	if (error || !exists) return false;
	if (is_directory != nullptr)
		*is_directory = std::filesystem::is_directory(path, error);
	return !error;
}

id NSFileManagerContentsOfDirectoryAtPath(id, SEL, id path_object,
	id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return nullptr;
	}
	std::error_code error;
	std::vector<id> names;
	try {
		for (std::filesystem::directory_iterator iterator(path, error), end;
			!error && iterator != end; iterator.increment(error)) {
			const std::wstring name = iterator->path().filename().native();
			const std::string utf8 = WideToUtf8(name);
			id item = CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
			if (item == nullptr) throw std::bad_alloc();
			names.push_back(item);
		}
		if (error) {
			for (id name : names) objc_release(name);
			NSErrorSetOut(error_output, error);
			return nullptr;
		}
		id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
			names.data(), names.size()));
		for (id name : names) objc_release(name);
		return result;
	} catch (...) {
		for (id name : names) objc_release(name);
		NSErrorSetOut(error_output, std::make_error_code(std::errc::not_enough_memory));
		return nullptr;
	}
}

id NSFileManagerContentsAtPath(id, SEL, id path_object)
{
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) return nullptr;
	try {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) return nullptr;
		const auto end = file.tellg();
		if (end < 0 || end > 256ll * 1024 * 1024) return nullptr;
		std::vector<unsigned char> bytes(static_cast<std::size_t>(end));
		file.seekg(0, std::ios::beg);
		if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()))) return nullptr;
		return objc_autorelease(CreateNSData(objc_getClass("NSData"),
			bytes.data(), bytes.size()));
	} catch (...) { return nullptr; }
}

bool NSFileManagerCreateDirectory(id, SEL, id path_object,
	bool create_intermediates, id, id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	std::error_code error;
	const bool already_directory = std::filesystem::is_directory(path, error);
	if (!error && already_directory) return true;
	error.clear();
	if (create_intermediates) (void)std::filesystem::create_directories(path, error);
	else (void)std::filesystem::create_directory(path, error);
	if (!error) return true;
	NSErrorSetOut(error_output, error);
	return false;
}

bool NSFileManagerRemoveItem(id, SEL, id path_object, id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	std::error_code error;
	const auto removed = std::filesystem::remove_all(path, error);
	if (!error && removed != 0) return true;
	if (!error) error = std::make_error_code(std::errc::no_such_file_or_directory);
	NSErrorSetOut(error_output, error);
	return false;
}

bool NSFileManagerCopyItem(id, SEL, id source_object, id destination_object,
	id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path source;
	std::filesystem::path destination;
	if (!NSFileManagerPath(source_object, source) ||
		!NSFileManagerPath(destination_object, destination)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	std::error_code error;
	std::filesystem::copy(source, destination,
		std::filesystem::copy_options::recursive |
		std::filesystem::copy_options::copy_symlinks, error);
	if (!error) return true;
	NSErrorSetOut(error_output, error);
	return false;
}

bool NSFileManagerMoveItem(id, SEL, id source_object, id destination_object,
	id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path source;
	std::filesystem::path destination;
	if (!NSFileManagerPath(source_object, source) ||
		!NSFileManagerPath(destination_object, destination)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return false;
	}
	std::error_code error;
	std::filesystem::rename(source, destination, error);
	if (!error) return true;
	NSErrorSetOut(error_output, error);
	return false;
}

bool NSFileManagerCreateFile(id, SEL, id path_object, id contents, id)
{
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) return false;
	if (contents != nullptr) {
		const Class contents_class = object_getClass(contents);
		const char* contents_class_name = contents_class == nullptr ? nullptr :
			class_getName(contents_class);
		if (contents_class_name == nullptr ||
			(std::strcmp(contents_class_name, "NSData") != 0 &&
			 std::strcmp(contents_class_name, "NSMutableData") != 0)) return false;
	}
	const auto length = contents == nullptr ? 0 : NSDataLength(contents, nullptr);
	const auto* bytes = contents == nullptr ? nullptr :
		static_cast<const unsigned char*>(NSDataBytes(contents, nullptr));
	if (length > 256ull * 1024 * 1024 || (length != 0 && bytes == nullptr))
		return false;
	try {
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) return false;
		if (length != 0) file.write(reinterpret_cast<const char*>(bytes),
			static_cast<std::streamsize>(length));
		return static_cast<bool>(file);
	} catch (...) { return false; }
}

double NSFileManagerFileTimeAsNSDate(FILETIME value)
{
	const std::uint64_t ticks = (static_cast<std::uint64_t>(value.dwHighDateTime)
		<< 32) | value.dwLowDateTime;
	constexpr std::uint64_t windows_to_unix_ticks = 116'444'736'000'000'000ull;
	constexpr double unix_to_cocoa_seconds = 978'307'200.0;
	return (static_cast<double>(ticks) -
		static_cast<double>(windows_to_unix_ticks)) / 10'000'000.0 -
		unix_to_cocoa_seconds;
}

id NSFileManagerCreateAttributeDictionary(
	const std::vector<id>& keys, std::vector<id>& values)
{
	if (keys.size() != values.size()) return nullptr;
	id dictionary = CreateNSDictionary(objc_getClass("NSDictionary"),
		values.data(), keys.data(), values.size());
	for (id value : values) if (value != nullptr) objc_release(value);
	values.clear();
	return objc_autorelease(dictionary);
}

id NSFileManagerAttributesOfItemAtPath(id, SEL, id path_object,
	id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return nullptr;
	}
	WIN32_FILE_ATTRIBUTE_DATA data{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
		const DWORD error = GetLastError();
		NSErrorSetOut(error_output,
			std::error_code(static_cast<int>(error), std::system_category()));
		return nullptr;
	}
	const bool is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	const bool is_link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	id type = is_link ? NSFileTypeSymbolicLink :
		(is_directory ? NSFileTypeDirectory : NSFileTypeRegular);
	const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
		data.nFileSizeLow;
	const std::uint64_t permissions = is_directory ? 0777ull :
		((data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0 ? 0444ull : 0666ull);
	std::vector<id> keys;
	std::vector<id> values;
	try {
		keys = {NSFileType, NSFileSize, NSFileModificationDate,
			NSFileCreationDate, NSFilePosixPermissions};
		values.reserve(keys.size());
		values.push_back(type == nullptr ? nullptr : objc_retain(type));
		values.push_back(CreateUnsignedNSNumber(objc_getClass("NSNumber"), size));
		values.push_back(CreateNSDate(objc_getClass("NSDate"),
			NSFileManagerFileTimeAsNSDate(data.ftLastWriteTime)));
		values.push_back(CreateNSDate(objc_getClass("NSDate"),
			NSFileManagerFileTimeAsNSDate(data.ftCreationTime)));
		values.push_back(CreateUnsignedNSNumber(objc_getClass("NSNumber"),
			permissions));
		if (std::find(values.begin(), values.end(), nullptr) != values.end())
			throw std::bad_alloc();
		return NSFileManagerCreateAttributeDictionary(keys, values);
	} catch (...) {
		for (id value : values) if (value != nullptr) objc_release(value);
		NSErrorSetOut(error_output,
			std::make_error_code(std::errc::not_enough_memory));
		return nullptr;
	}
}

id NSFileManagerAttributesOfItemAtURL(id self, SEL, id url,
	id* error_output)
{
	id path = NSFileManagerPathStringFromURL(url);
	if (path == nullptr) {
		if (error_output != nullptr)
			NSErrorSetOut(error_output,
				std::make_error_code(std::errc::invalid_argument));
		return nullptr;
	}
	id result = NSFileManagerAttributesOfItemAtPath(self, nullptr, path,
		error_output);
	objc_release(path);
	return result;
}

id NSFileManagerAttributesOfFileSystemForPath(id, SEL, id path_object,
	id* error_output)
{
	if (error_output != nullptr) *error_output = nullptr;
	std::filesystem::path path;
	if (!NSFileManagerPath(path_object, path)) {
		NSErrorSetOut(error_output, std::make_error_code(std::errc::invalid_argument));
		return nullptr;
	}
	std::error_code path_error;
	const bool is_directory = std::filesystem::is_directory(path, path_error);
	if (path_error) {
		NSErrorSetOut(error_output, path_error);
		return nullptr;
	}
	if (!is_directory) {
		path = path.parent_path();
		if (path.empty()) path = std::filesystem::current_path(path_error);
		if (path_error) {
			NSErrorSetOut(error_output, path_error);
			return nullptr;
		}
	}
	ULARGE_INTEGER total{};
	ULARGE_INTEGER free{};
	if (!GetDiskFreeSpaceExW(path.c_str(), nullptr, &total, &free)) {
		const DWORD error = GetLastError();
		NSErrorSetOut(error_output,
			std::error_code(static_cast<int>(error), std::system_category()));
		return nullptr;
	}
	std::vector<id> keys{NSFileSystemSize, NSFileSystemFreeSize};
	std::vector<id> values;
	try {
		values.push_back(CreateUnsignedNSNumber(objc_getClass("NSNumber"),
			total.QuadPart));
		values.push_back(CreateUnsignedNSNumber(objc_getClass("NSNumber"),
			free.QuadPart));
		if (std::find(values.begin(), values.end(), nullptr) != values.end())
			throw std::bad_alloc();
		return NSFileManagerCreateAttributeDictionary(keys, values);
	} catch (...) {
		for (id value : values) if (value != nullptr) objc_release(value);
		NSErrorSetOut(error_output,
			std::make_error_code(std::errc::not_enough_memory));
		return nullptr;
	}
}

void NSFileHandleFinalize(id object)
{
	std::shared_ptr<AppKitFileHandleState> state;
	{
		std::lock_guard lock(g_file_handles_mutex);
		const auto found = g_file_handles.find(object);
		if (found == g_file_handles.end()) return;
		state = std::move(found->second);
		g_file_handles.erase(found);
	}
}

bool NSFileHandleAttach(id object,
	std::shared_ptr<AppKitFileHandleState> state)
{
	if (object == nullptr || !state) return false;
	try {
		{
			std::lock_guard lock(g_file_handles_mutex);
			if (!g_file_handles.emplace(object, std::move(state)).second)
				return false;
		}
		if (darling_objc_register_object_finalizer(object,
			&NSFileHandleFinalize)) return true;
		NSFileHandleFinalize(object);
		return false;
	} catch (...) { return false; }
}

std::shared_ptr<AppKitFileHandleState> NSFileHandleGetState(id object)
{
	std::lock_guard lock(g_file_handles_mutex);
	const auto found = g_file_handles.find(object);
	return found == g_file_handles.end() ? nullptr : found->second;
}

id NSFileHandleCreate(Class cls, HANDLE handle, bool close_on_dealloc)
{
	if (cls == nullptr || handle == INVALID_HANDLE_VALUE || handle == nullptr)
		return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) {
		if (close_on_dealloc) CloseHandle(handle);
		return nullptr;
	}
	std::shared_ptr<AppKitFileHandleState> state;
	try { state = std::make_shared<AppKitFileHandleState>(); }
	catch (...) {
		objc_release(object);
		if (close_on_dealloc) CloseHandle(handle);
		return nullptr;
	}
	state->handle = handle;
	state->close_on_dealloc = close_on_dealloc;
	if (!NSFileHandleAttach(object, state)) {
		objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

id NSFileHandleCreateForPath(Class cls, id path_object, DWORD access)
{
	const char* utf8 = NSStringUTF8String(path_object, nullptr);
	if (utf8 == nullptr) return nullptr;
	try {
		const std::wstring path = Utf8ToWide(utf8);
		HANDLE handle = CreateFileW(path.c_str(), access,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) return nullptr;
		return NSFileHandleCreate(cls, handle, true);
	} catch (...) { return nullptr; }
}

id NSFileHandleForReadingAtPath(Class cls, SEL, id path)
{
	return NSFileHandleCreateForPath(cls, path, GENERIC_READ);
}

id NSFileHandleForWritingAtPath(Class cls, SEL, id path)
{
	return NSFileHandleCreateForPath(cls, path, GENERIC_WRITE);
}

id NSFileHandleForUpdatingAtPath(Class cls, SEL, id path)
{
	return NSFileHandleCreateForPath(cls, path, GENERIC_READ | GENERIC_WRITE);
}

HANDLE NSFileHandleDuplicateDarwinDescriptor(int descriptor,
	bool& close_native_handle)
{
	if (descriptor < 0) return INVALID_HANDLE_VALUE;
	if (descriptor <= 2) {
		close_native_handle = false;
		const DWORD standard = descriptor == 0 ? STD_INPUT_HANDLE :
			(descriptor == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
		HANDLE handle = GetStdHandle(standard);
		return handle == nullptr ? INVALID_HANDLE_VALUE : handle;
	}
	close_native_handle = true;
	HANDLE duplicate = darling_windows_dispatch_duplicate_descriptor_handle(
		descriptor);
	return duplicate == nullptr ? INVALID_HANDLE_VALUE : duplicate;
}

id NSFileHandleWithFileDescriptor(Class cls, SEL, int descriptor,
	bool close_on_dealloc)
{
	bool close_native_handle = false;
	const HANDLE handle = NSFileHandleDuplicateDarwinDescriptor(descriptor,
		close_native_handle);
	if (handle == INVALID_HANDLE_VALUE) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) {
		if (close_native_handle) CloseHandle(handle);
		return nullptr;
	}
	std::shared_ptr<AppKitFileHandleState> state;
	try {
		state = std::make_shared<AppKitFileHandleState>();
	} catch (...) {
		objc_release(object);
		if (close_native_handle) CloseHandle(handle);
		return nullptr;
	}
	state->handle = handle;
	state->darwin_descriptor = descriptor;
	state->close_on_dealloc = close_on_dealloc;
	state->close_native_handle = close_native_handle;
	if (!NSFileHandleAttach(object, state)) {
		state->close_on_dealloc = false;
		objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

id NSFileHandleWithFileDescriptorDefault(Class cls, SEL, int descriptor)
{
	return NSFileHandleWithFileDescriptor(cls, nullptr, descriptor, true);
}

id NSFileHandleInitWithFileDescriptor(id self, SEL, int descriptor,
	bool close_on_dealloc)
{
	if (self == nullptr) return nullptr;
	bool close_native_handle = false;
	const HANDLE handle = NSFileHandleDuplicateDarwinDescriptor(descriptor,
		close_native_handle);
	if (handle == INVALID_HANDLE_VALUE) return nullptr;
	try {
		auto state = std::make_shared<AppKitFileHandleState>();
		state->handle = handle;
		state->darwin_descriptor = descriptor;
		state->close_on_dealloc = close_on_dealloc;
		state->close_native_handle = close_native_handle;
		if (NSFileHandleAttach(self, state)) return self;
		state->close_on_dealloc = false;
		return nullptr;
	} catch (...) {
		if (close_native_handle) CloseHandle(handle);
		return nullptr;
	}
}

id NSFileHandleForStandardHandle(Class cls, DWORD standard_handle)
{
	HANDLE handle = GetStdHandle(standard_handle);
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr) return nullptr;
	try {
		auto state = std::make_shared<AppKitFileHandleState>();
		state->handle = handle;
		state->close_on_dealloc = false;
		if (!NSFileHandleAttach(object, std::move(state))) {
			objc_release(object);
			return nullptr;
		}
		return objc_autorelease(object);
	} catch (...) { objc_release(object); return nullptr; }
}

id NSFileHandleStandardInput(Class cls, SEL)
{
	return NSFileHandleForStandardHandle(cls, STD_INPUT_HANDLE);
}

id NSFileHandleStandardOutput(Class cls, SEL)
{
	return NSFileHandleForStandardHandle(cls, STD_OUTPUT_HANDLE);
}

id NSFileHandleStandardError(Class cls, SEL)
{
	return NSFileHandleForStandardHandle(cls, STD_ERROR_HANDLE);
}

id NSFileHandleNullDevice(Class cls, SEL)
{
	HANDLE handle = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	return handle == INVALID_HANDLE_VALUE ? nullptr :
		NSFileHandleCreate(cls, handle, true);
}

id NSFileHandleReadDataOfLength(id self, SEL, std::uint64_t requested)
{
	if (requested > 256ull * 1024 * 1024) return nullptr;
	auto state = NSFileHandleGetState(self);
	if (!state) return nullptr;
	std::vector<unsigned char> bytes;
	try { bytes.resize(static_cast<std::size_t>(requested)); }
	catch (...) { return nullptr; }
	DWORD total_read = 0;
	std::lock_guard lock(state->mutex);
	while (total_read < requested) {
		const DWORD amount = static_cast<DWORD>(std::min<std::uint64_t>(
			requested - total_read, MAXDWORD));
		DWORD read = 0;
		if (!ReadFile(state->handle, bytes.data() + total_read, amount,
			&read, nullptr)) return nullptr;
		if (read == 0) break;
		total_read += read;
	}
	return objc_autorelease(CreateNSData(objc_getClass("NSData"), bytes.data(),
		total_read));
}

id NSFileHandleReadDataToEndOfFile(id self, SEL)
{
	auto state = NSFileHandleGetState(self);
	if (!state) return nullptr;
	std::vector<unsigned char> bytes;
	std::array<unsigned char, 64 * 1024> chunk{};
	std::lock_guard lock(state->mutex);
	for (;;) {
		DWORD read = 0;
		if (!ReadFile(state->handle, chunk.data(),
			static_cast<DWORD>(chunk.size()), &read, nullptr)) return nullptr;
		if (read == 0) break;
		if (read > 256ull * 1024 * 1024 - bytes.size()) return nullptr;
		try { bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read); }
		catch (...) { return nullptr; }
	}
	return objc_autorelease(CreateNSData(objc_getClass("NSData"), bytes.data(),
		bytes.size()));
}

id NSFileHandleAvailableData(id self, SEL)
{
	auto state = NSFileHandleGetState(self);
	if (!state) return nullptr;
	std::vector<unsigned char> bytes;
	std::lock_guard lock(state->mutex);
	DWORD requested = 0;
	if (GetFileType(state->handle) == FILE_TYPE_PIPE) {
		if (!PeekNamedPipe(state->handle, nullptr, 0, nullptr, &requested, nullptr))
			return nullptr;
		requested = std::min<DWORD>(requested, 256u * 1024 * 1024);
	} else {
		LARGE_INTEGER current{};
		LARGE_INTEGER zero{};
		LARGE_INTEGER size{};
		if (!SetFilePointerEx(state->handle, zero, &current, FILE_CURRENT) ||
			!GetFileSizeEx(state->handle, &size)) return nullptr;
		if (size.QuadPart > current.QuadPart)
			requested = static_cast<DWORD>(std::min<LONGLONG>(
				size.QuadPart - current.QuadPart, 256 * 1024 * 1024));
	}
	try { bytes.resize(requested); }
	catch (...) { return nullptr; }
	DWORD total = 0;
	while (total < requested) {
		DWORD read = 0;
		if (!ReadFile(state->handle, bytes.data() + total, requested - total,
			&read, nullptr)) return nullptr;
		if (read == 0) break;
		total += read;
	}
	return objc_autorelease(CreateNSData(objc_getClass("NSData"), bytes.data(), total));
}

void NSFileHandleWriteData(id self, SEL, id data)
{
	auto state = NSFileHandleGetState(self);
	if (!state || data == nullptr) return;
	const auto length = NSDataLength(data, nullptr);
	const auto* bytes = static_cast<const unsigned char*>(NSDataBytes(data, nullptr));
	if (length > 256ull * 1024 * 1024 || (length != 0 && bytes == nullptr)) return;
	std::lock_guard lock(state->mutex);
	std::uint64_t written = 0;
	while (written < length) {
		const DWORD amount = static_cast<DWORD>(std::min<std::uint64_t>(
			length - written, MAXDWORD));
		DWORD count = 0;
		if (!WriteFile(state->handle, bytes + written, amount, &count, nullptr) ||
			count == 0) return;
		written += count;
	}
}

std::uint64_t NSFileHandleOffsetInFile(id self, SEL)
{
	auto state = NSFileHandleGetState(self);
	if (!state) return (std::numeric_limits<std::uint64_t>::max)();
	LARGE_INTEGER zero{};
	LARGE_INTEGER position{};
	std::lock_guard lock(state->mutex);
	return SetFilePointerEx(state->handle, zero, &position, FILE_CURRENT) ?
		static_cast<std::uint64_t>(position.QuadPart) :
		(std::numeric_limits<std::uint64_t>::max)();
}

std::uint64_t NSFileHandleSeekToEndOfFile(id self, SEL)
{
	auto state = NSFileHandleGetState(self);
	if (!state) return (std::numeric_limits<std::uint64_t>::max)();
	LARGE_INTEGER zero{};
	LARGE_INTEGER position{};
	std::lock_guard lock(state->mutex);
	return SetFilePointerEx(state->handle, zero, &position, FILE_END) ?
		static_cast<std::uint64_t>(position.QuadPart) :
		(std::numeric_limits<std::uint64_t>::max)();
}

void NSFileHandleSeekToFileOffset(id self, SEL, std::uint64_t offset)
{
	auto state = NSFileHandleGetState(self);
	if (!state || offset > static_cast<std::uint64_t>(LLONG_MAX)) return;
	LARGE_INTEGER position{};
	position.QuadPart = static_cast<LONGLONG>(offset);
	std::lock_guard lock(state->mutex);
	(void)SetFilePointerEx(state->handle, position, nullptr, FILE_BEGIN);
}

void NSFileHandleTruncateFileAtOffset(id self, SEL, std::uint64_t offset)
{
	auto state = NSFileHandleGetState(self);
	if (!state || offset > static_cast<std::uint64_t>(LLONG_MAX)) return;
	LARGE_INTEGER position{};
	position.QuadPart = static_cast<LONGLONG>(offset);
	std::lock_guard lock(state->mutex);
	if (SetFilePointerEx(state->handle, position, nullptr, FILE_BEGIN))
		(void)SetEndOfFile(state->handle);
}

void NSFileHandleSynchronizeFile(id self, SEL)
{
	auto state = NSFileHandleGetState(self);
	if (!state) return;
	std::lock_guard lock(state->mutex);
	(void)FlushFileBuffers(state->handle);
}

void NSFileHandleCloseFile(id self, SEL)
{
	auto state = NSFileHandleGetState(self);
	if (!state) return;
	std::lock_guard lock(state->mutex);
	if (state->darwin_descriptor >= 0) {
		(void)darling_windows_close(state->darwin_descriptor);
		state->darwin_descriptor = -1;
	}
	if (state->close_native_handle && state->handle != INVALID_HANDLE_VALUE)
		(void)CloseHandle(state->handle);
	state->handle = INVALID_HANDLE_VALUE;
}

void NSFilePanelFinalize(id object)
{
	std::lock_guard lock(g_file_panels_mutex);
	g_file_panels.erase(object);
}

id NSFilePanelInit(id self, SEL)
{
	if (self == nullptr) return nullptr;
	try {
		{
			std::lock_guard lock(g_file_panels_mutex);
			g_file_panels.try_emplace(self);
		}
		if (!darling_objc_register_object_finalizer(self, &NSFilePanelFinalize)) {
			NSFilePanelFinalize(self);
			return nullptr;
		}
		return self;
	} catch (...) { return nullptr; }
}

id NSFilePanelFactory(Class cls, SEL)
{
	if (cls == nullptr) return nullptr;
	id object = class_createInstance(cls, 0);
	if (object == nullptr || NSFilePanelInit(object, nullptr) == nullptr) {
		if (object != nullptr) objc_release(object);
		return nullptr;
	}
	return objc_autorelease(object);
}

void NSFilePanelSetTitle(id self, SEL, id value)
{
	const char* utf8 = NSStringUTF8String(value, nullptr);
	try {
		const std::wstring title = utf8 == nullptr ? L"" : Utf8ToWide(utf8);
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found != g_file_panels.end()) found->second.title = title;
	} catch (...) {}
}

void NSFilePanelSetAllowedFileTypes(id self, SEL, id value)
{
	std::vector<std::wstring> file_types;
	const auto count = value == nullptr ? 0 : NSArrayCount(value, nullptr);
	if (count > 256) return;
	try {
		file_types.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			id item = NSArrayObjectAtIndex(value, nullptr, index);
			const char* utf8 = NSStringUTF8String(item, nullptr);
			if (utf8 == nullptr) return;
			std::wstring extension = Utf8ToWide(utf8);
			if (extension.empty() || extension.find_first_of(L"\\/:;\0") !=
				std::wstring::npos) continue;
			if (extension.front() == L'.') extension.erase(extension.begin());
			if (!extension.empty()) file_types.push_back(std::move(extension));
		}
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found != g_file_panels.end())
			found->second.allowed_file_types = std::move(file_types);
	} catch (...) {}
}

void NSFilePanelSetAllowedContentTypes(id self, SEL, id value)
{
	std::vector<std::wstring> extensions;
	const auto count = value == nullptr ? 0 : NSArrayCount(value, nullptr);
	if (count > 256) return;
	const SEL preferred_extension_selector =
		sel_registerName("preferredFilenameExtension");
	try {
		extensions.reserve(static_cast<std::size_t>(count));
		for (std::uint64_t index = 0; index < count; ++index) {
			id type = NSArrayObjectAtIndex(value, nullptr, index);
			if (type == nullptr ||
				!objc_opt_respondsToSelector(type, preferred_extension_selector))
				continue;
			auto extension_object = static_cast<id>(darling_objc_msgSend_pointer0(
				type, preferred_extension_selector));
			const char* utf8 = NSStringUTF8String(extension_object, nullptr);
			if (utf8 == nullptr) continue;
			std::wstring extension = Utf8ToWide(utf8);
			if (!extension.empty() && extension.find_first_of(L"\\/:;") ==
				std::wstring::npos) extensions.push_back(std::move(extension));
		}
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found != g_file_panels.end())
			found->second.allowed_file_types = std::move(extensions);
	} catch (...) {}
}

id NSFilePanelAllowedContentTypes(id self, SEL)
{
	std::vector<std::wstring> extensions;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return nullptr;
		extensions = found->second.allowed_file_types;
	}
	std::vector<id> types;
	try {
		types.reserve(extensions.size());
		for (const auto& extension : extensions) {
			const std::string utf8 = WideToUtf8(extension);
			id extension_object = CreateDynamicNSString(objc_getClass("NSString"),
				utf8.c_str());
			if (extension_object == nullptr) throw std::bad_alloc();
			id type = UTTypeTypeWithFilenameExtension(
				objc_getClass("UTType"), nullptr, extension_object);
			objc_release(extension_object);
			if (type == nullptr) throw std::bad_alloc();
			types.push_back(type);
		}
		id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
			types.data(), types.size()));
		for (id type : types) objc_release(type);
		return result;
	} catch (...) {
		for (id type : types) objc_release(type);
		return nullptr;
	}
}

std::vector<wchar_t> NSFilePanelFilter(const AppKitFilePanelState& state)
{
	if (state.allowed_file_types.empty())
		return {L'A', L'l', L'l', L' ', L'f', L'i', L'l', L'e', L's', L'\0',
			L'*', L'.', L'*', L'\0', L'\0'};
	std::wstring pattern;
	for (const auto& extension : state.allowed_file_types) {
		if (!pattern.empty()) pattern.push_back(L';');
		pattern += L"*.";
		pattern += extension;
	}
	std::vector<wchar_t> filter;
	const std::wstring description = L"Allowed files";
	filter.insert(filter.end(), description.begin(), description.end());
	filter.push_back(L'\0');
	filter.insert(filter.end(), pattern.begin(), pattern.end());
	filter.push_back(L'\0');
	filter.push_back(L'\0');
	return filter;
}

void NSOpenPanelSetAllowsMultipleSelection(id self, SEL, bool value)
{
	std::lock_guard lock(g_file_panels_mutex);
	const auto found = g_file_panels.find(self);
	if (found != g_file_panels.end()) found->second.allows_multiple = value;
}

void NSOpenPanelSetCanChooseFiles(id self, SEL, bool value)
{
	std::lock_guard lock(g_file_panels_mutex);
	const auto found = g_file_panels.find(self);
	if (found != g_file_panels.end()) found->second.can_choose_files = value;
}

void NSOpenPanelSetCanChooseDirectories(id self, SEL, bool value)
{
	std::lock_guard lock(g_file_panels_mutex);
	const auto found = g_file_panels.find(self);
	if (found != g_file_panels.end()) found->second.can_choose_directories = value;
}

void NSSavePanelSetNameFieldStringValue(id self, SEL, id value)
{
	const char* utf8 = NSStringUTF8String(value, nullptr);
	try {
		const std::wstring name = utf8 == nullptr ? L"" : Utf8ToWide(utf8);
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found != g_file_panels.end()) found->second.name = name;
	} catch (...) {}
}

id NSSavePanelNameFieldStringValue(id self, SEL)
{
	std::wstring name;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return nullptr;
		name = found->second.name;
	}
	try {
		const std::string utf8 = WideToUtf8(name);
		return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
			utf8.c_str());
	} catch (...) { return nullptr; }
}

std::int64_t NSFilePanelRunModal(id self, SEL)
{
	AppKitFilePanelState state;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return 0;
		found->second.paths.clear();
		state = found->second;
	}
	if (state.can_choose_directories && !state.can_choose_files) {
		wchar_t display_name[MAX_PATH]{};
		BROWSEINFOW browser{};
		browser.hwndOwner = GetActiveWindow();
		browser.pszDisplayName = display_name;
		browser.lpszTitle = state.title.empty() ? L"Choose a folder" :
			state.title.c_str();
		browser.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&browser);
		if (selected == nullptr) return 0;
		wchar_t path[MAX_PATH]{};
		const bool got_path = SHGetPathFromIDListW(selected, path) != FALSE;
		CoTaskMemFree(selected);
		if (!got_path) return 0;
		try {
			std::lock_guard lock(g_file_panels_mutex);
			const auto found = g_file_panels.find(self);
			if (found == g_file_panels.end()) return 0;
			found->second.paths.assign(1, path);
			return 1;
		} catch (...) { return 0; }
	}
	std::vector<wchar_t> buffer(65536, L'\0');
	const auto filter = NSFilePanelFilter(state);
	std::wstring title = state.title.empty() ? L"Wintosh" : state.title;
	OPENFILENAMEW config{};
	config.lStructSize = sizeof(config);
	config.hwndOwner = GetActiveWindow();
	config.lpstrTitle = title.c_str();
	config.lpstrFile = buffer.data();
	config.nMaxFile = static_cast<DWORD>(buffer.size());
	config.lpstrFilter = filter.data();
	config.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
		OFN_HIDEREADONLY;
	if (state.can_choose_files) {
		config.Flags |= OFN_FILEMUSTEXIST;
		if (state.allows_multiple) config.Flags |= OFN_ALLOWMULTISELECT;
	} else {
		// The Win32 common file dialog cannot select directories.
		return 0;
	}
	if (!GetOpenFileNameW(&config)) return 0;
	std::vector<std::wstring> paths;
	try {
		const wchar_t* first = buffer.data();
		const std::wstring first_value(first);
		const wchar_t* next = first + first_value.size() + 1;
		if (*next == L'\0') {
			paths.push_back(first_value);
		} else {
			for (const wchar_t* item = next; *item != L'\0';
				item += std::wcslen(item) + 1) {
				std::wstring path = first_value;
				if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
					path.push_back(L'\\');
				path += item;
				paths.push_back(std::move(path));
			}
		}
	} catch (...) { return 0; }
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return 0;
		found->second.paths = std::move(paths);
	}
	return 1;
}

std::int64_t NSSavePanelRunModal(id self, SEL)
{
	AppKitFilePanelState state;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return 0;
		found->second.paths.clear();
		state = found->second;
	}
	std::vector<wchar_t> buffer(32768, L'\0');
	const auto filter = NSFilePanelFilter(state);
	if (!state.name.empty()) {
		const auto count = std::min(state.name.size(), buffer.size() - 1);
		std::copy_n(state.name.data(), count, buffer.data());
	}
	std::wstring title = state.title.empty() ? L"Wintosh" : state.title;
	OPENFILENAMEW config{};
	config.lStructSize = sizeof(config);
	config.hwndOwner = GetActiveWindow();
	config.lpstrTitle = title.c_str();
	config.lpstrFile = buffer.data();
	config.nMaxFile = static_cast<DWORD>(buffer.size());
	config.lpstrFilter = filter.data();
	config.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	if (!GetSaveFileNameW(&config)) return 0;
	try {
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return 0;
		found->second.paths.assign(1, buffer.data());
		found->second.name = std::filesystem::path(buffer.data()).filename().wstring();
	} catch (...) { return 0; }
	return 1;
}

id NSFilePanelURL(id self, SEL)
{
	std::wstring path;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end() || found->second.paths.empty()) return nullptr;
		path = found->second.paths.front();
	}
	try {
		const std::string utf8 = WideToUtf8(path);
		id string = CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
		if (string == nullptr) return nullptr;
		id url = NSURLCreateFileURL(objc_getClass("NSURL"), string);
		objc_release(string);
		return url;
	} catch (...) { return nullptr; }
}

id NSFilePanelURLs(id self, SEL)
{
	std::vector<std::wstring> paths;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return nullptr;
		paths = found->second.paths;
	}
	std::vector<id> urls;
	try {
		urls.reserve(paths.size());
		for (const auto& path : paths) {
			const std::string utf8 = WideToUtf8(path);
			id string = CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
			if (string == nullptr) throw std::bad_alloc();
			id url = NSURLCreateFileURL(objc_getClass("NSURL"), string);
			objc_release(string);
			if (url == nullptr) throw std::bad_alloc();
			urls.push_back(url);
		}
		id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
			urls.data(), urls.size()));
		for (id url : urls) objc_release(url);
		return result;
	} catch (...) {
		for (id url : urls) objc_release(url);
		return nullptr;
	}
}

id NSFilePanelFilenames(id self, SEL)
{
	std::vector<std::wstring> paths;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end()) return nullptr;
		paths = found->second.paths;
	}
	std::vector<id> filenames;
	try {
		filenames.reserve(paths.size());
		for (const auto& path : paths) {
			const std::string utf8 = WideToUtf8(path);
			id name = CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
			if (name == nullptr) throw std::bad_alloc();
			filenames.push_back(name);
		}
		id result = objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
			filenames.data(), filenames.size()));
		for (id filename : filenames) objc_release(filename);
		return result;
	} catch (...) {
		for (id filename : filenames) objc_release(filename);
		return nullptr;
	}
}

id NSFilePanelFilename(id self, SEL)
{
	std::wstring path;
	{
		std::lock_guard lock(g_file_panels_mutex);
		const auto found = g_file_panels.find(self);
		if (found == g_file_panels.end() || found->second.paths.empty()) return nullptr;
		path = found->second.paths.front();
	}
	try {
		const std::string utf8 = WideToUtf8(path);
		return NSStringStringWithUTF8String(objc_getClass("NSString"), nullptr,
			utf8.c_str());
	} catch (...) { return nullptr; }
}

void NSMenuItemFinalize(id object)
{
	AppKitMenuItemState state{};
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(object);
		if (found == g_menu_items.end()) return;
		state = std::exchange(found->second, {});
		g_menu_items.erase(found);
	}
	if (state.title != nullptr) objc_release(state.title);
	if (state.target != nullptr) objc_release(state.target);
	if (state.key_equivalent != nullptr) objc_release(state.key_equivalent);
	if (state.submenu != nullptr) objc_release(state.submenu);
}

id NSMenuInitWithTitle(id self, SEL, id title)
{
	if (self == nullptr) return nullptr;
	AppKitMenuState replacement{title == nullptr ? nullptr : objc_retain(title), {}};
	bool inserted = false;
	AppKitMenuState previous{};
	try {
		std::lock_guard lock(g_menus_mutex);
		auto found = g_menus.find(self);
		if (found == g_menus.end()) {
			g_menus.emplace(self, replacement);
			inserted = true;
		} else previous = std::exchange(found->second, replacement);
	} catch (...) {
		if (replacement.title != nullptr) objc_release(replacement.title);
		return nullptr;
	}
	if (previous.title != nullptr) objc_release(previous.title);
	if (inserted && !darling_objc_register_object_finalizer(self, &NSMenuFinalize)) {
		NSMenuFinalize(self);
		return nullptr;
	}
	return self;
}

id NSMenuInit(id self, SEL)
{
	id title = CreateDynamicNSString(objc_getClass("NSString"), "");
	if (title == nullptr) return nullptr;
	id result = NSMenuInitWithTitle(self, nullptr, title);
	objc_release(title);
	return result;
}

id NSMenuInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr ||
		g_active_keyed_archive_decoder == nullptr) return nullptr;
	auto decode_field = [](const char* name) -> id {
		id key = CreateDynamicNSString(objc_getClass("NSString"), name);
		if (key == nullptr) return nullptr;
		id value = g_active_keyed_archive_decoder->DecodeForKey(key);
		objc_release(key);
		return value;
	};
	id title = decode_field("NSTitle");
	if (title == nullptr)
		title = CreateDynamicNSString(objc_getClass("NSString"), "");
	if (title == nullptr) return nullptr;
	id initialized = NSMenuInitWithTitle(self,
		sel_registerName("initWithTitle:"), title);
	objc_release(title);
	if (initialized == nullptr) return nullptr;
	id items = decode_field("NSMenuItems");
	std::vector<id> snapshot;
	try {
		std::lock_guard lock(g_array_mutex);
		const auto found = g_arrays.find(items);
		if (found != g_arrays.end()) {
			snapshot.reserve(found->second.size());
			for (id item : found->second) snapshot.push_back(objc_retain(item));
		}
	} catch (...) {
		for (id item : snapshot) objc_release(item);
		snapshot.clear();
	}
	if (items != nullptr) objc_release(items);
	for (id item : snapshot) {
		NSMenuAddItem(initialized, sel_registerName("addItem:"), item);
		objc_release(item);
	}
	id name = decode_field("NSName");
	const char* name_text = NSStringUTF8String(name, nullptr);
	if (name_text != nullptr && std::strcmp(name_text, "_NSMainMenu") == 0 &&
		NSApp != nullptr)
		NSApplicationSetMainMenu(NSApp, sel_registerName("setMainMenu:"),
			initialized);
	if (name != nullptr) objc_release(name);
	return initialized;
}

id NSMenuItemInitWithTitle(id self, SEL, id title, SEL action, id key_equivalent)
{
	if (self == nullptr) return nullptr;
	AppKitMenuItemState replacement{};
	replacement.title = title == nullptr ? nullptr : objc_retain(title);
	replacement.key_equivalent = key_equivalent == nullptr ? nullptr :
		objc_retain(key_equivalent);
	replacement.action = action;
	bool inserted = false;
	AppKitMenuItemState previous{};
	try {
		std::lock_guard lock(g_menus_mutex);
		auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) {
			g_menu_items.emplace(self, replacement);
			inserted = true;
		} else previous = std::exchange(found->second, replacement);
	} catch (...) {
		if (replacement.title != nullptr) objc_release(replacement.title);
		if (replacement.key_equivalent != nullptr) objc_release(replacement.key_equivalent);
		return nullptr;
	}
	if (previous.title != nullptr) objc_release(previous.title);
	if (previous.target != nullptr) objc_release(previous.target);
	if (previous.key_equivalent != nullptr) objc_release(previous.key_equivalent);
	if (previous.submenu != nullptr) objc_release(previous.submenu);
	if (inserted && !darling_objc_register_object_finalizer(self,
		&NSMenuItemFinalize)) {
		NSMenuItemFinalize(self);
		return nullptr;
	}
	return self;
}

id NSMenuItemInit(id self, SEL)
{
	id title = CreateDynamicNSString(objc_getClass("NSString"), "");
	if (title == nullptr) return nullptr;
	id result = NSMenuItemInitWithTitle(self, nullptr, title, nullptr, nullptr);
	objc_release(title);
	return result;
}

id NSMenuItemInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr ||
		g_active_keyed_archive_decoder == nullptr) return nullptr;
	if (NSMenuItemInit(self, sel_registerName("init")) == nullptr)
		return nullptr;
	auto decode_field = [](const char* name) -> id {
		id key = CreateDynamicNSString(objc_getClass("NSString"), name);
		if (key == nullptr) return nullptr;
		id value = g_active_keyed_archive_decoder->DecodeForKey(key);
		objc_release(key);
		return value;
	};
	id title = decode_field("NSTitle");
	if (title != nullptr) {
		NSMenuItemSetTitle(self, sel_registerName("setTitle:"), title);
		objc_release(title);
	}
	id action_name = decode_field("NSAction");
	const char* action_text = NSStringUTF8String(action_name, nullptr);
	if (action_text != nullptr && action_text[0] != '\0')
		NSMenuItemSetAction(self, sel_registerName("setAction:"),
			sel_registerName(action_text));
	if (action_name != nullptr) objc_release(action_name);
	id key_equivalent = decode_field("NSKeyEquiv");
	if (key_equivalent != nullptr) {
		NSMenuItemSetKeyEquivalent(self, sel_registerName("setKeyEquivalent:"),
			key_equivalent);
		objc_release(key_equivalent);
	}
	id target = decode_field("NSTarget");
	if (target != nullptr) {
		NSMenuItemSetTarget(self, sel_registerName("setTarget:"), target);
		objc_release(target);
	}
	id submenu = decode_field("NSSubmenu");
	if (submenu != nullptr) {
		NSMenuItemSetSubmenu(self, sel_registerName("setSubmenu:"), submenu);
		objc_release(submenu);
	}
	return self;
}

id NSMenuTitle(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menus.find(self);
	return found == g_menus.end() || found->second.title == nullptr ? nullptr :
		objc_retainAutorelease(found->second.title);
}

void NSMenuSetTitle(id self, SEL, id title)
{
	if (title != nullptr) objc_retain(title);
	id previous = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menus.find(self);
		if (found == g_menus.end()) {
			if (title != nullptr) objc_release(title);
			return;
		}
		previous = std::exchange(found->second.title, title);
	}
	if (previous != nullptr) objc_release(previous);
	RefreshAppKitMenus();
}

bool NSMenuAutoenablesItems(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menus.find(self);
	return found != g_menus.end() && found->second.autoenables_items;
}

void NSMenuSetAutoenablesItems(id self, SEL, bool enabled)
{
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menus.find(self);
		if (found == g_menus.end()) return;
		found->second.autoenables_items = enabled;
	}
	RefreshAppKitMenus();
}

void NSMenuAddItem(id self, SEL, id item)
{
	if (item == nullptr) return;
	objc_retain(item);
	bool inserted = false;
	try {
		std::lock_guard lock(g_menus_mutex);
		const auto menu = g_menus.find(self);
		if (menu != g_menus.end() && g_menu_items.find(item) != g_menu_items.end()) {
			menu->second.items.push_back(item);
			inserted = true;
		}
	} catch (...) {
		inserted = false;
	}
	if (!inserted) { objc_release(item); return; }
	RefreshAppKitMenus();
}

void NSMenuInsertItemAtIndex(id self, SEL, id item, std::int64_t index)
{
	if (item == nullptr || index < 0) return;
	objc_retain(item);
	bool inserted = false;
	try {
		std::lock_guard lock(g_menus_mutex);
		const auto menu = g_menus.find(self);
		if (menu != g_menus.end() && g_menu_items.find(item) != g_menu_items.end() &&
			static_cast<std::uint64_t>(index) <= menu->second.items.size()) {
			menu->second.items.insert(menu->second.items.begin() + index, item);
			inserted = true;
		}
	} catch (...) {
		inserted = false;
	}
	if (!inserted) { objc_release(item); return; }
	RefreshAppKitMenus();
}

void NSMenuRemoveItem(id self, SEL, id item)
{
	id removed = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto menu = g_menus.find(self);
		if (menu == g_menus.end()) return;
		const auto found = std::find(menu->second.items.begin(),
			menu->second.items.end(), item);
		if (found == menu->second.items.end()) return;
		removed = *found;
		menu->second.items.erase(found);
	}
	objc_release(removed);
	RefreshAppKitMenus();
}

std::int64_t NSMenuNumberOfItems(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menus.find(self);
	return found == g_menus.end() ? 0 :
		static_cast<std::int64_t>(found->second.items.size());
}

id NSMenuItemAtIndex(id self, SEL, std::int64_t index)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menus.find(self);
	return found == g_menus.end() || index < 0 ||
		static_cast<std::uint64_t>(index) >= found->second.items.size() ? nullptr :
		objc_retainAutorelease(found->second.items[static_cast<std::size_t>(index)]);
}

id NSMenuItemArray(id self, SEL)
{
	std::vector<id> items;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menus.find(self);
		if (found == g_menus.end()) return nullptr;
		items = found->second.items;
	}
	return objc_autorelease(CreateNSArray(objc_getClass("NSArray"), items.data(),
		items.size()));
}

id NSMenuAddItemWithTitle(id self, SEL, id title, SEL action, id key_equivalent)
{
	const Class cls = objc_getClass("NSMenuItem");
	if (cls == nullptr) return nullptr;
	id item = class_createInstance(cls, 0);
	if (item == nullptr || NSMenuItemInitWithTitle(item, nullptr, title, action,
		key_equivalent) == nullptr) return nullptr;
	NSMenuAddItem(self, nullptr, item);
	return objc_autorelease(item);
}

id NSMenuMenuWithTitle(Class cls, SEL, id title)
{
	if (cls == nullptr) return nullptr;
	id menu = class_createInstance(cls, 0);
	if (menu == nullptr || NSMenuInitWithTitle(menu, nullptr, title) == nullptr)
		return nullptr;
	return objc_autorelease(menu);
}

std::int64_t NSMenuIndexOfItem(id self, SEL, id item)
{
	std::lock_guard lock(g_menus_mutex);
	const auto menu = g_menus.find(self);
	if (menu == g_menus.end()) return -1;
	const auto found = std::find(menu->second.items.begin(),
		menu->second.items.end(), item);
	return found == menu->second.items.end() ? -1 :
		static_cast<std::int64_t>(found - menu->second.items.begin());
}

id NSMenuItemWithTitle(id self, SEL, id title)
{
	std::vector<id> items;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto menu = g_menus.find(self);
		if (menu == g_menus.end()) return nullptr;
		items = menu->second.items;
		for (id item : items) objc_retain(item);
	}
	id result = nullptr;
	for (id item : items) {
		id candidate = NSMenuItemTitle(item, nullptr);
		if (candidate != nullptr && ObjcObjectsEqual(candidate, title) &&
			result == nullptr) result = objc_retain(item);
		if (candidate != nullptr) objc_release(candidate);
		objc_release(item);
	}
	return result == nullptr ? nullptr : objc_autorelease(result);
}

id NSMenuItemSeparatorItem(Class cls, SEL)
{
	if (cls == nullptr) return nullptr;
	id item = class_createInstance(cls, 0);
	if (item == nullptr) return nullptr;
	id title = CreateDynamicNSString(objc_getClass("NSString"), "");
	if (title == nullptr || NSMenuItemInitWithTitle(item, nullptr, title, nullptr,
		nullptr) == nullptr) {
		if (title != nullptr) objc_release(title);
		return nullptr;
	}
	objc_release(title);
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(item);
		if (found != g_menu_items.end()) found->second.separator = true;
	}
	return objc_autorelease(item);
}

id NSMenuItemWithTitleFactory(Class cls, SEL, id title, SEL action,
	id key_equivalent)
{
	if (cls == nullptr) return nullptr;
	id item = class_createInstance(cls, 0);
	if (item == nullptr || NSMenuItemInitWithTitle(item, nullptr, title, action,
		key_equivalent) == nullptr) return nullptr;
	return objc_autorelease(item);
}

id NSMenuItemTitle(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() || found->second.title == nullptr ? nullptr :
		objc_retainAutorelease(found->second.title);
}

void NSMenuItemSetTitle(id self, SEL, id title)
{
	if (title != nullptr) objc_retain(title);
	id previous = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) {
			if (title != nullptr) objc_release(title);
			return;
		}
		previous = std::exchange(found->second.title, title);
	}
	if (previous != nullptr) objc_release(previous);
	NSPopUpButtonSyncItemTitle(self, title);
	RefreshAppKitMenus();
}

id NSMenuItemTarget(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() || found->second.target == nullptr ? nullptr :
		objc_retainAutorelease(found->second.target);
}

void NSMenuItemSetTarget(id self, SEL, id target)
{
	if (target != nullptr) objc_retain(target);
	id previous = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) {
			if (target != nullptr) objc_release(target);
			return;
		}
		previous = std::exchange(found->second.target, target);
	}
	if (previous != nullptr) objc_release(previous);
}

SEL NSMenuItemAction(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() ? nullptr : found->second.action;
}

void NSMenuItemSetAction(id self, SEL, SEL action)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	if (found != g_menu_items.end()) found->second.action = action;
}

bool NSMenuItemIsEnabled(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found != g_menu_items.end() && found->second.enabled;
}

void NSMenuItemSetEnabled(id self, SEL, bool enabled)
{
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) return;
		found->second.enabled = enabled;
	}
	RefreshAppKitMenus();
}

id NSMenuItemSubmenu(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() || found->second.submenu == nullptr ? nullptr :
		objc_retainAutorelease(found->second.submenu);
}

id NSMenuItemKeyEquivalent(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() || found->second.key_equivalent == nullptr ?
		nullptr : objc_retainAutorelease(found->second.key_equivalent);
}

void NSMenuItemSetKeyEquivalent(id self, SEL, id key_equivalent)
{
	if (key_equivalent != nullptr) objc_retain(key_equivalent);
	id previous = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) {
			if (key_equivalent != nullptr) objc_release(key_equivalent);
			return;
		}
		previous = std::exchange(found->second.key_equivalent, key_equivalent);
	}
	if (previous != nullptr) objc_release(previous);
	RefreshAppKitMenus();
}

std::uint64_t NSMenuItemKeyEquivalentModifierMask(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() ? (1ull << 20) :
		found->second.modifier_mask;
}

void NSMenuItemSetKeyEquivalentModifierMask(id self, SEL, std::uint64_t mask)
{
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) return;
		found->second.modifier_mask = mask;
	}
	RefreshAppKitMenus();
}

void NSMenuItemSetSubmenu(id self, SEL, id submenu)
{
	if (submenu != nullptr) objc_retain(submenu);
	id previous = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) {
			if (submenu != nullptr) objc_release(submenu);
			return;
		}
		previous = std::exchange(found->second.submenu, submenu);
	}
	if (previous != nullptr) objc_release(previous);
	RefreshAppKitMenus();
}

bool NSMenuItemIsSeparator(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found != g_menu_items.end() && found->second.separator;
}

void NSMenuItemSetState(id self, SEL, std::int64_t state)
{
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(self);
		if (found == g_menu_items.end()) return;
		found->second.state = state;
	}
	RefreshAppKitMenus();
}

std::int64_t NSMenuItemState(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() ? 0 : found->second.state;
}

void NSMenuItemSetTag(id self, SEL, std::int64_t tag)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	if (found != g_menu_items.end()) found->second.tag = tag;
}

std::int64_t NSMenuItemTag(id self, SEL)
{
	std::lock_guard lock(g_menus_mutex);
	const auto found = g_menu_items.find(self);
	return found == g_menu_items.end() ? 0 : found->second.tag;
}

HMENU BuildNativeAppMenu(id menu, unsigned depth = 0)
{
	if (menu == nullptr || depth > 16) return nullptr;
	std::vector<id> items;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menus.find(menu);
		if (found == g_menus.end()) return nullptr;
		items = found->second.items;
		for (id item : items) objc_retain(item);
	}
	HMENU native = depth == 0 ? CreateMenu() : CreatePopupMenu();
	if (native == nullptr) {
		for (id item : items) objc_release(item);
		return nullptr;
	}
	{
		std::lock_guard lock(g_menus_mutex);
		g_native_menu_objects[native] = menu;
	}
	for (id item : items) {
		AppKitMenuItemState state{};
		{
			std::lock_guard lock(g_menus_mutex);
			const auto found = g_menu_items.find(item);
			if (found != g_menu_items.end()) state = found->second;
			if (state.title != nullptr) objc_retain(state.title);
			if (state.key_equivalent != nullptr) objc_retain(state.key_equivalent);
			if (state.submenu != nullptr) objc_retain(state.submenu);
		}
		if (state.separator) {
			AppendMenuW(native, MF_SEPARATOR, 0, nullptr);
		} else {
			std::wstring title = NativeTitle(state.title);
			if (state.key_equivalent != nullptr) {
				const char* key = NSStringUTF8String(state.key_equivalent, nullptr);
				if (key != nullptr && key[0] != '\0' && key[1] == '\0') {
					std::wstring shortcut;
					if ((state.modifier_mask & (1ull << 20)) != 0 ||
						(state.modifier_mask & (1ull << 18)) != 0)
						shortcut += L"Ctrl+";
					if ((state.modifier_mask & (1ull << 19)) != 0)
						shortcut += L"Alt+";
					if ((state.modifier_mask & (1ull << 17)) != 0)
						shortcut += L"Shift+";
					shortcut.push_back(static_cast<wchar_t>(
						std::toupper(static_cast<unsigned char>(key[0]))));
					title += L"\t" + shortcut;
				}
			}
			HMENU submenu = state.submenu == nullptr ? nullptr :
				BuildNativeAppMenu(state.submenu, depth + 1);
			UINT flags = MF_STRING | (state.enabled ? MF_ENABLED : MF_GRAYED);
			if (state.state != 0) flags |= MF_CHECKED;
			UINT_PTR command = 0;
			if (submenu != nullptr) {
				flags |= MF_POPUP;
				command = reinterpret_cast<UINT_PTR>(submenu);
			} else {
				std::lock_guard lock(g_menus_mutex);
				if (g_next_menu_command < 0xefff) ++g_next_menu_command;
				else g_next_menu_command = 0x4000;
				command = g_next_menu_command;
				g_menu_commands[static_cast<UINT>(command)] = item;
			}
			if (!AppendMenuW(native, flags, command, title.c_str()) && submenu != nullptr)
				DestroyMenu(submenu);
		}
		if (state.title != nullptr) objc_release(state.title);
		if (state.key_equivalent != nullptr) objc_release(state.key_equivalent);
		if (state.submenu != nullptr) objc_release(state.submenu);
		objc_release(item);
	}
	return native;
}

void RefreshAppKitMenus()
{
	std::vector<std::pair<id, HWND>> windows;
	{
		std::lock_guard lock(g_windows_mutex);
		for (const auto& [object, state] : g_windows)
			if (state.handle != nullptr) windows.emplace_back(object, state.handle);
	}
	{
		std::lock_guard lock(g_menus_mutex);
		g_menu_commands.clear();
		g_native_menu_objects.clear();
	}
	for (const auto& [object, handle] : windows) ApplyAppKitMenuToWindow(object);
}

void ApplyAppKitMenuToWindow(id window)
{
	HWND handle = nullptr;
	HMENU previous = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_windows.find(window);
		if (found == g_windows.end() || found->second.handle == nullptr) return;
		handle = found->second.handle;
		previous = found->second.native_menu;
	}
	id main_menu = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		main_menu = g_application_main_menu;
		if (main_menu != nullptr) objc_retain(main_menu);
	}
	HMENU native = BuildNativeAppMenu(main_menu);
	if (main_menu != nullptr) objc_release(main_menu);
	if (!SetMenu(handle, native)) {
		if (native != nullptr) DestroyMenu(native);
		return;
	}
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_windows.find(window);
		if (found != g_windows.end() && found->second.handle == handle)
			found->second.native_menu = native;
	}
	if (previous != nullptr && previous != native) DestroyMenu(previous);
	DrawMenuBar(handle);
}

id ResolveAppKitMenuActionTarget(id item, SEL action)
{
	id explicit_target = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(item);
		if (found != g_menu_items.end() && found->second.target != nullptr)
			explicit_target = objc_retain(found->second.target);
	}
	if (explicit_target != nullptr) {
		if (class_getInstanceMethod(object_getClass(explicit_target), action) != nullptr)
			return explicit_target;
		objc_release(explicit_target);
		return nullptr;
	}
	return ResolveApplicationActionTarget(action);
}

bool DispatchAppKitMenuItem(id item)
{
	AppKitMenuItemState state{};
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(item);
		if (found == g_menu_items.end()) return false;
		state = found->second;
	}
	if (state.enabled && state.action != nullptr) {
		NSApplicationSendAction(g_shared_application, nullptr, state.action,
			state.target, item);
	}
	return true;
}

bool DispatchAppKitMenuCommand(UINT command)
{
	id item = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_commands.find(command);
		if (found == g_menu_commands.end()) return false;
		item = objc_retain(found->second);
	}
	const bool dispatched = DispatchAppKitMenuItem(item);
	objc_release(item);
	return dispatched;
}

void UnregisterNativeAppMenu(HMENU menu)
{
	if (menu == nullptr) return;
	const int count = GetMenuItemCount(menu);
	for (int index = 0; index < count; ++index) {
		const UINT command = GetMenuItemID(menu, index);
		if (command == static_cast<UINT>(-1))
			UnregisterNativeAppMenu(GetSubMenu(menu, index));
		else {
			std::lock_guard lock(g_menus_mutex);
			g_menu_commands.erase(command);
		}
	}
	std::lock_guard lock(g_menus_mutex);
	g_native_menu_objects.erase(menu);
}

id FindAppKitViewAtPoint(id view, double x, double y, unsigned depth)
{
	if (view == nullptr || depth > 64) return nullptr;
	DarlingObjcRect bounds{};
	std::vector<id> subviews;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(view);
		if (found == g_views.end()) return nullptr;
		if (found->second.hidden) return nullptr;
		bounds = found->second.bounds;
		if (x < bounds.x || y < bounds.y || x >= bounds.x + bounds.width ||
			y >= bounds.y + bounds.height) return nullptr;
		subviews = found->second.subviews;
	}
	for (auto child = subviews.rbegin(); child != subviews.rend(); ++child) {
		DarlingObjcRect frame{};
		DarlingObjcRect child_bounds{};
		{
			std::lock_guard lock(g_windows_mutex);
			const auto found = g_views.find(*child);
			if (found == g_views.end()) continue;
			frame = found->second.frame;
			child_bounds = found->second.bounds;
		}
		const double child_x = x - frame.x + child_bounds.x;
		const double child_y = y - frame.y + child_bounds.y;
		if (id result = FindAppKitViewAtPoint(*child, child_x, child_y,
			depth + 1); result != nullptr) return result;
	}
	return view;
}

bool ScrollAppKitViewAtPoint(id root, double x, double y, int wheel_delta,
	bool horizontal)
{
	if (root == nullptr || wheel_delta == 0) return false;
	id hit = FindAppKitViewAtPoint(root, x, y);
	id clip_view = nullptr;
	DarlingObjcRect bounds{};
	bool has_horizontal_scroller = false;
	{
		std::lock_guard lock(g_windows_mutex);
		for (id current = hit; current != nullptr;) {
			const auto found = g_views.find(current);
			if (found == g_views.end()) break;
			if (IsViewKindOf(current, "NSClipView")) {
				clip_view = current;
				bounds = found->second.bounds;
				const auto scroll_view = g_views.find(found->second.superview);
				has_horizontal_scroller = scroll_view != g_views.end() &&
					scroll_view->second.has_horizontal_scroller;
				break;
			}
			current = found->second.superview;
		}
	}
	if (clip_view == nullptr) return false;
	if (horizontal && !has_horizontal_scroller) return false;
	id document_view = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto clip = g_views.find(clip_view);
		if (clip != g_views.end() && !clip->second.subviews.empty())
			document_view = clip->second.subviews.front();
	}
	if (document_view == nullptr) return false;
	if (horizontal) {
		bounds.x += static_cast<double>(wheel_delta) * 40.0 /
			static_cast<double>(WHEEL_DELTA);
	} else {
		const bool flipped = darling_objc_msgSend_bool0(document_view,
			sel_registerName("isFlipped"));
		const double direction = flipped ? -1.0 : 1.0;
		bounds.y += direction * static_cast<double>(wheel_delta) * 40.0 /
			static_cast<double>(WHEEL_DELTA);
	}
	NSViewSetBounds(clip_view, nullptr, bounds);
	return true;
}

void PopupNativeAppMenu(id menu, HWND owner, POINT point)
{
	if (menu == nullptr) return;
	if (owner == nullptr) owner = GetForegroundWindow();
	if (owner == nullptr) return;
	id retained_menu = objc_retain(menu);
	HMENU native = BuildNativeAppMenu(retained_menu, 1);
	if (native == nullptr) { objc_release(retained_menu); return; }
	ValidateAppKitMenu(native);
	SetForegroundWindow(owner);
	const UINT command = TrackPopupMenu(native,
		TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD |
			TPM_NONOTIFY, point.x, point.y, 0, owner, nullptr);
	if (command != 0) DispatchAppKitMenuCommand(command);
	UnregisterNativeAppMenu(native);
	DestroyMenu(native);
	objc_release(retained_menu);
}

void NSMenuPopUpContextMenu(Class, SEL, id menu, id, id view)
{
	POINT point{};
	GetCursorPos(&point);
	HWND owner = nullptr;
	if (view != nullptr && IsViewKindOf(view, "NSView")) {
		id window_object = darling_objc_msgSend_object0(view,
			sel_registerName("window"));
		if (window_object != nullptr) {
			std::lock_guard lock(g_windows_mutex);
			const auto found = g_windows.find(window_object);
			if (found != g_windows.end()) owner = found->second.handle;
		}
	}
	PopupNativeAppMenu(menu, owner, point);
}

void NSMenuPopUpContextMenuWithFont(Class cls, SEL selector, id menu,
	id event, id view, id)
{
	NSMenuPopUpContextMenu(cls, selector, menu, event, view);
}

bool MenuKeyMatches(id item, std::string_view key, std::uint64_t modifiers)
{
	AppKitMenuItemState state{};
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menu_items.find(item);
		if (found == g_menu_items.end()) return false;
		state = found->second;
		if (state.key_equivalent != nullptr) objc_retain(state.key_equivalent);
	}
	if (state.key_equivalent == nullptr) return false;
	if (!state.enabled || state.action == nullptr) {
		objc_release(state.key_equivalent);
		return false;
	}
	const char* text = NSStringUTF8String(state.key_equivalent, nullptr);
	constexpr std::uint64_t shift_mask = 1ull << 17;
	constexpr std::uint64_t control_mask = 1ull << 18;
	constexpr std::uint64_t option_mask = 1ull << 19;
	constexpr std::uint64_t command_mask = 1ull << 20;
	constexpr std::uint64_t physical_control = 1ull << 30;
	const auto required = state.modifier_mask &
		(shift_mask | control_mask | option_mask | command_mask);
	const bool needs_control = (required & (control_mask | command_mask)) != 0;
	const bool has_control = (modifiers & physical_control) != 0;
	const std::uint64_t required_non_control = required &
		(shift_mask | option_mask);
	const bool ascii_match = text != nullptr && key.size() == 1 &&
		static_cast<unsigned char>(text[0]) < 0x80 && text[0] == key[0];
	const bool match = text != nullptr &&
		((ascii_match) || (key.size() > 1 && key == text)) &&
		((required & (control_mask | command_mask)) !=
			(control_mask | command_mask)) && needs_control == has_control &&
		(modifiers & (shift_mask | option_mask)) == required_non_control;
	objc_release(state.key_equivalent);
	return match;
}

bool FindMenuKeyEquivalent(id menu, std::string_view key, std::uint64_t modifiers,
	std::vector<id>& visited)
{
	if (menu == nullptr || visited.size() >= 32 ||
		std::find(visited.begin(), visited.end(), menu) != visited.end()) return false;
	visited.push_back(menu);
	std::vector<id> items;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto found = g_menus.find(menu);
		if (found == g_menus.end()) { visited.pop_back(); return false; }
		items = found->second.items;
		for (id item : items) objc_retain(item);
	}
	for (id item : items) {
		id submenu = nullptr;
		{
			std::lock_guard lock(g_menus_mutex);
			const auto found = g_menu_items.find(item);
			if (found != g_menu_items.end() && found->second.submenu != nullptr)
				submenu = objc_retain(found->second.submenu);
		}
		bool matched = submenu != nullptr ?
			FindMenuKeyEquivalent(submenu, key, modifiers, visited) :
			MenuKeyMatches(item, key, modifiers);
		if (submenu != nullptr) objc_release(submenu);
		if (matched) {
			if (submenu == nullptr) DispatchAppKitMenuItem(item);
			for (id remaining : items) objc_release(remaining);
			visited.pop_back();
			return true;
		}
		objc_release(item);
	}
	visited.pop_back();
	return false;
}

bool DispatchAppKitMenuKey(WPARAM key)
{
	std::string equivalent;
	const auto append_codepoint = [](std::uint32_t codepoint, std::string& out) {
		wchar_t wide[2]{};
		int length = 1;
		if (codepoint <= 0xffff) wide[0] = static_cast<wchar_t>(codepoint);
		else {
			codepoint -= 0x10000;
			wide[0] = static_cast<wchar_t>(0xd800 + (codepoint >> 10));
			wide[1] = static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff));
			length = 2;
		}
		return ConvertWideToUtf8(std::wstring_view(wide, length), out);
	};
	switch (key) {
	case VK_ESCAPE: append_codepoint(0x1b, equivalent); break;
	case VK_RETURN: case VK_SEPARATOR: append_codepoint(0x0d, equivalent); break;
	case VK_TAB: append_codepoint(0x09, equivalent); break;
	case VK_BACK: append_codepoint(0x08, equivalent); break;
	case VK_DELETE: append_codepoint(0x7f, equivalent); break;
	case VK_UP: append_codepoint(0xf700, equivalent); break;
	case VK_DOWN: append_codepoint(0xf701, equivalent); break;
	case VK_LEFT: append_codepoint(0xf702, equivalent); break;
	case VK_RIGHT: append_codepoint(0xf703, equivalent); break;
	case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5: case VK_F6:
	case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11: case VK_F12:
	case VK_F13: case VK_F14: case VK_F15: case VK_F16: case VK_F17: case VK_F18:
	case VK_F19: case VK_F20: case VK_F21: case VK_F22: case VK_F23: case VK_F24:
		append_codepoint(0xf704 + static_cast<std::uint32_t>(key - VK_F1), equivalent);
		break;
	case VK_HOME: append_codepoint(0xf729, equivalent); break;
	case VK_END: append_codepoint(0xf72b, equivalent); break;
	case VK_PRIOR: append_codepoint(0xf72c, equivalent); break;
	case VK_NEXT: append_codepoint(0xf72d, equivalent); break;
	case VK_INSERT: append_codepoint(0xf727, equivalent); break;
	default: {
		BYTE keyboard_state[256]{};
		if (!GetKeyboardState(keyboard_state)) return false;
		keyboard_state[VK_CONTROL] = 0;
		keyboard_state[VK_LCONTROL] = 0;
		keyboard_state[VK_RCONTROL] = 0;
		wchar_t translated[8]{};
		const HKL layout = GetKeyboardLayout(0);
		const UINT scan = MapVirtualKeyExW(static_cast<UINT>(key), MAPVK_VK_TO_VSC,
			layout);
		const int length = ToUnicodeEx(static_cast<UINT>(key), scan,
			keyboard_state, translated, static_cast<int>(std::size(translated)), 0,
			layout);
		if (length <= 0 || !ConvertWideToUtf8(
			std::wstring_view(translated, static_cast<std::size_t>(length)),
			equivalent)) return false;
		break;
	}
	}
	if (equivalent.empty()) return false;
	std::uint64_t modifiers = 0;
	if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= 1ull << 17;
	if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= 1ull << 30;
	if ((GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= 1ull << 19;
	id menu = nullptr;
	{
		std::lock_guard lock(g_menus_mutex);
		menu = g_application_main_menu;
		if (menu != nullptr) objc_retain(menu);
	}
	std::vector<id> visited;
	const bool handled = FindMenuKeyEquivalent(menu, equivalent,
		modifiers, visited);
	if (menu != nullptr) objc_release(menu);
	return handled;
}

void ValidateAppKitMenu(HMENU native_menu)
{
	id menu = nullptr;
	bool autoenables = false;
	std::vector<id> items;
	{
		std::lock_guard lock(g_menus_mutex);
		const auto native = g_native_menu_objects.find(native_menu);
		if (native == g_native_menu_objects.end()) return;
		menu = objc_retain(native->second);
		const auto found = g_menus.find(menu);
		if (found == g_menus.end()) { objc_release(menu); return; }
		autoenables = found->second.autoenables_items;
		items = found->second.items;
		for (id item : items) objc_retain(item);
	}
	if (autoenables) {
		const int count = GetMenuItemCount(native_menu);
		for (int index = 0; index < count; ++index) {
			const UINT command = GetMenuItemID(native_menu, index);
			if (command == static_cast<UINT>(-1)) continue;
			id item = nullptr;
			{
				std::lock_guard lock(g_menus_mutex);
				const auto found = g_menu_commands.find(command);
				if (found != g_menu_commands.end()) item = objc_retain(found->second);
			}
			if (item == nullptr) continue;
			AppKitMenuItemState state{};
			{
				std::lock_guard lock(g_menus_mutex);
				const auto found = g_menu_items.find(item);
				if (found != g_menu_items.end()) state = found->second;
			}
			id target = state.action == nullptr ? nullptr :
				ResolveAppKitMenuActionTarget(item, state.action);
			bool enabled = target != nullptr;
			if (enabled && class_getInstanceMethod(object_getClass(target),
				sel_registerName("validateMenuItem:")) != nullptr)
				enabled = darling_objc_msgSend_bool_object1(target,
					sel_registerName("validateMenuItem:"), item);
			if (target != nullptr) objc_release(target);
			{
				std::lock_guard lock(g_menus_mutex);
				const auto found = g_menu_items.find(item);
				if (found != g_menu_items.end()) found->second.enabled = enabled;
			}
			EnableMenuItem(native_menu, command, MF_BYCOMMAND |
				(enabled ? MF_ENABLED : MF_GRAYED));
			objc_release(item);
		}
	}
	for (id item : items) objc_release(item);
	objc_release(menu);
}

bool IsViewKindOf(id object, const char* class_name)
{
	if (object == nullptr || class_name == nullptr) return false;
	const Class expected = objc_getClass(class_name);
	for (Class current = object_getClass(object); current != nullptr;
		current = class_getSuperclass(current)) {
		if (current == expected) return true;
	}
	return false;
}

void PositionNativeControl(id view)
{
	const bool is_popup = IsViewKindOf(view, "NSPopUpButton");
	const bool is_button = IsViewKindOf(view, "NSButton") && !is_popup;
	const bool is_text_field = IsViewKindOf(view, "NSTextField");
	const bool is_text_view = IsViewKindOf(view, "NSTextView");
	const bool is_progress = IsViewKindOf(view, "NSProgressIndicator");
	const bool is_slider = IsViewKindOf(view, "NSSlider");
	const bool is_stepper = IsViewKindOf(view, "NSStepper");
	if (!is_button && !is_text_field && !is_text_view && !is_progress &&
		!is_slider && !is_stepper && !is_popup) return;
	id window_object = nullptr;
	id title = nullptr;
	id placeholder = nullptr;
	HWND parent = nullptr;
	HWND control = nullptr;
	bool enabled = true;
	bool editable = true;
	bool hidden = false;
	std::uint64_t button_type = 0;
	std::int64_t button_state = 0;
	double progress_min = 0.0;
	double progress_max = 100.0;
	double progress_value = 0.0;
	std::uint64_t progress_style = 0;
	bool progress_indeterminate = false;
	bool progress_animating = false;
	bool native_progress_marquee = false;
	double slider_min = 0.0;
	double slider_max = 1.0;
	double slider_value = 0.0;
	bool slider_vertical = false;
	double stepper_min = 0.0;
	double stepper_max = 59.0;
	double stepper_value = 0.0;
	std::vector<std::wstring> popup_titles;
	std::int64_t popup_selected_index = -1;
	HWND recreate_control = nullptr;
	DarlingObjcRect frame{};
	id superview = nullptr;
	double window_x = 0.0;
	double window_y = 0.0;
	double window_width = 0.0;
	double window_height = 0.0;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto view_found = g_views.find(view);
		if (view_found == g_views.end()) return;
		window_object = view_found->second.window;
		frame = view_found->second.frame;
		title = view_found->second.title;
		placeholder = view_found->second.placeholder;
		control = view_found->second.control;
		enabled = view_found->second.enabled;
		editable = view_found->second.editable;
		hidden = view_found->second.hidden;
		button_type = view_found->second.button_type;
		button_state = view_found->second.button_state;
		progress_min = view_found->second.progress_min_value;
		progress_max = view_found->second.progress_max_value;
		progress_value = view_found->second.progress_value;
		progress_style = view_found->second.progress_style;
		progress_indeterminate = view_found->second.progress_indeterminate;
		progress_animating = view_found->second.progress_animating;
		slider_min = view_found->second.slider_min_value;
		slider_max = view_found->second.slider_max_value;
		slider_value = view_found->second.slider_value;
		slider_vertical = view_found->second.slider_vertical;
		stepper_min = view_found->second.stepper_min_value;
		stepper_max = view_found->second.stepper_max_value;
		stepper_value = view_found->second.stepper_value;
		popup_titles = view_found->second.popup_titles;
		popup_selected_index = view_found->second.popup_selected_index;
		native_progress_marquee = is_progress && progress_animating &&
			(progress_style == 1 || progress_indeterminate);
		superview = view_found->second.superview;
		if (((is_progress && view_found->second.control != nullptr &&
			view_found->second.native_progress_marquee != native_progress_marquee) ||
			(is_slider && view_found->second.control != nullptr &&
			view_found->second.native_slider_vertical != slider_vertical))) {
			recreate_control = view_found->second.control;
			g_controls.erase(recreate_control);
			view_found->second.control = nullptr;
			control = nullptr;
		}
		const auto window_found = g_windows.find(window_object);
		if (window_found == g_windows.end()) return;
		parent = window_found->second.handle;
		id ancestor = superview;
		std::size_t depth = 0;
		while (ancestor != nullptr && depth++ <= g_views.size()) {
			const auto ancestor_found = g_views.find(ancestor);
			if (ancestor_found == g_views.end()) return;
			hidden = hidden || ancestor_found->second.hidden;
			ancestor = ancestor_found->second.superview;
		}
		if (ancestor != nullptr) return;
	}
	if (recreate_control != nullptr) DestroyWindow(recreate_control);
	if (parent == nullptr) return;
	if (superview != nullptr) {
		const ViewCoordinateOrigin transform = NSViewOriginInWindow(superview);
		if (!transform.valid || transform.window != window_object) return;
		const double x0 = transform.scale_x * frame.x + transform.translate_x;
		const double x1 = transform.scale_x * (frame.x + frame.width) +
			transform.translate_x;
		const double y0 = transform.scale_y * frame.y + transform.translate_y;
		const double y1 = transform.scale_y * (frame.y + frame.height) +
			transform.translate_y;
		window_x = std::min(x0, x1);
		window_y = std::min(y0, y1);
		window_width = std::abs(x1 - x0);
		window_height = std::abs(y1 - y0);
	} else {
		window_x = frame.x;
		window_y = frame.y;
		window_width = frame.width;
		window_height = frame.height;
	}
	if (!std::isfinite(window_x) || !std::isfinite(window_y) ||
		!std::isfinite(window_width) || !std::isfinite(window_height)) return;
	RECT client{};
	if (!GetClientRect(parent, &client)) return;
	const int width = std::max(1, ClampWindowCoordinate(window_width, 1, 10000));
	const int height = std::max(1, ClampWindowCoordinate(window_height, 1, 10000));
	const int x = ClampWindowCoordinate(window_x, -16000, 16000);
	const int y = ClampWindowCoordinate(client.bottom - window_y - window_height,
		-16000, 16000);
	const DWORD native_button_type = button_type == 3 || button_type == 6 ?
		BS_3STATE : button_type == 4 ? BS_RADIOBUTTON :
		(button_type == 1 || button_type == 2) ?
			(BS_CHECKBOX | BS_PUSHLIKE) : BS_PUSHBUTTON;
	if (control == nullptr) {
		if (is_progress || is_slider || is_stepper) {
			INITCOMMONCONTROLSEX controls{sizeof(controls),
				static_cast<DWORD>(is_progress ? ICC_PROGRESS_CLASS : is_slider ?
					ICC_BAR_CLASSES : ICC_UPDOWN_CLASS)};
			InitCommonControlsEx(&controls);
		}
		const std::wstring native_title = NativeTitle(title);
		const DWORD control_style = WS_CHILD | (is_button || is_text_field ||
			is_text_view || is_slider || is_stepper || is_popup ? WS_TABSTOP : 0) |
			(is_button ? native_button_type : is_popup ?
				(CBS_DROPDOWNLIST | WS_VSCROLL) : is_progress ? PBS_SMOOTH |
				(native_progress_marquee ? PBS_MARQUEE : 0) : is_slider ?
				(TBS_AUTOTICKS | (slider_vertical ? TBS_VERT : TBS_HORZ)) :
				is_stepper ? (UDS_ARROWKEYS | UDS_NOTHOUSANDS) : WS_BORDER |
				(is_text_view ? ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL :
					ES_AUTOHSCROLL) | (editable ? 0 : ES_READONLY)) |
			(hidden ? 0 : WS_VISIBLE);
		const wchar_t* class_name = is_button ? L"BUTTON" : is_popup ?
			L"COMBOBOX" : is_progress ?
			PROGRESS_CLASSW : is_slider ? TRACKBAR_CLASSW : is_stepper ?
			UPDOWN_CLASSW : L"EDIT";
		const wchar_t* default_title = is_button ? L"Button" : L"";
		control = CreateWindowExW(0, class_name,
			native_title.empty() ? default_title : native_title.c_str(),
			control_style, x, y, width, height, parent,
			nullptr, GetModuleHandleW(nullptr), nullptr);
		if (control == nullptr) return;
		SetLastError(ERROR_SUCCESS);
		const auto original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
			control, GWLP_WNDPROC,
			reinterpret_cast<LONG_PTR>(&AppKitControlWindowProc)));
		if (original_proc == nullptr && GetLastError() != ERROR_SUCCESS) {
			DestroyWindow(control);
			return;
		}
		if (is_text_field && placeholder != nullptr) {
			const std::wstring cue = NativeTitle(placeholder);
			SendMessageW(control, 0x1501, TRUE,
				reinterpret_cast<LPARAM>(cue.c_str()));
		}
		if (!SetPropW(control, kButtonOriginalProcProperty,
			reinterpret_cast<HANDLE>(original_proc))) {
			SetWindowLongPtrW(control, GWLP_WNDPROC,
				reinterpret_cast<LONG_PTR>(original_proc));
			DestroyWindow(control);
			return;
		}
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto view_found = g_views.find(view);
		if (view_found == g_views.end()) {
			DestroyWindow(control);
			return;
		}
		view_found->second.control = control;
		if (is_progress)
			view_found->second.native_progress_marquee = native_progress_marquee;
		if (is_slider)
			view_found->second.native_slider_vertical = slider_vertical;
		g_controls[control] = view;
	} else {
		if (is_button) {
			const LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
			SetWindowLongPtrW(control, GWL_STYLE,
				(style & ~static_cast<LONG_PTR>(BS_TYPEMASK | BS_PUSHLIKE)) |
					native_button_type);
		}
		SetWindowPos(control, nullptr, x, y, width, height,
			SWP_NOZORDER | SWP_NOACTIVATE);
		if (!is_popup) {
			const std::wstring native_title = NativeTitle(title);
			const wchar_t* desired_title = is_button && native_title.empty() ?
				L"Button" : native_title.c_str();
			const int existing_length = GetWindowTextLengthW(control);
			std::vector<wchar_t> existing(static_cast<std::size_t>(
				std::max(existing_length, 0)) + 1);
			GetWindowTextW(control, existing.data(),
				static_cast<int>(existing.size()));
			if (std::wstring_view(existing.data()) != desired_title)
				SetWindowTextW(control, desired_title);
		}
		if (is_text_field) {
			const std::wstring cue = NativeTitle(placeholder);
			SendMessageW(control, 0x1501, TRUE,
				reinterpret_cast<LPARAM>(cue.c_str()));
			SendMessageW(control, EM_SETREADONLY, editable ? FALSE : TRUE, 0);
		}
	}
	if (is_button) SendMessageW(control, BM_SETCHECK,
		button_state < 0 ? BST_INDETERMINATE :
			button_state == 0 ? BST_UNCHECKED : BST_CHECKED, 0);
	if (is_progress) {
		SendMessageW(control, PBM_SETMARQUEE,
			native_progress_marquee ? TRUE : FALSE, 30);
		SendMessageW(control, PBM_SETRANGE32, 0, 10000);
		const double span = progress_max - progress_min;
		const double fraction = span > 0.0 ?
			std::clamp((progress_value - progress_min) / span, 0.0, 1.0) : 0.0;
		SendMessageW(control, PBM_SETPOS,
			static_cast<WPARAM>(std::lround(fraction * 10000.0)), 0);
	}
	if (is_slider) {
		SendMessageW(control, TBM_SETRANGE, TRUE, MAKELONG(0, 10000));
		const double span = slider_max - slider_min;
		const double fraction = span > 0.0 ?
			std::clamp((slider_value - slider_min) / span, 0.0, 1.0) : 0.0;
		SendMessageW(control, TBM_SETPOS, TRUE,
			static_cast<LPARAM>(std::lround(fraction * 10000.0)));
	}
	if (is_stepper) {
		SendMessageW(control, UDM_SETRANGE32, 0, 10000);
		const double span = stepper_max - stepper_min;
		const double fraction = span > 0.0 ?
			std::clamp((stepper_value - stepper_min) / span, 0.0, 1.0) : 0.0;
		SendMessageW(control, UDM_SETPOS32, 0,
			static_cast<LPARAM>(std::lround(fraction * 10000.0)));
	}
	if (is_popup) {
		SendMessageW(control, CB_RESETCONTENT, 0, 0);
		for (const std::wstring& item : popup_titles)
			SendMessageW(control, CB_ADDSTRING, 0,
				reinterpret_cast<LPARAM>(item.c_str()));
		const LRESULT selected = popup_selected_index >= 0 &&
			static_cast<std::uint64_t>(popup_selected_index) < popup_titles.size() ?
			static_cast<LRESULT>(popup_selected_index) : CB_ERR;
		SendMessageW(control, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
	}
	EnableWindow(control, enabled ? TRUE : FALSE);
	id clipping_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		id ancestor = superview;
		std::size_t depth = 0;
		while (ancestor != nullptr && depth++ <= g_views.size()) {
			if (IsViewKindOf(ancestor, "NSClipView")) {
				clipping_view = ancestor;
				break;
			}
			const auto found = g_views.find(ancestor);
			ancestor = found == g_views.end() ? nullptr : found->second.superview;
		}
	}
	if (clipping_view == nullptr) {
		SetWindowRgn(control, nullptr, TRUE);
	} else {
		const ViewCoordinateOrigin clip_origin =
			NSViewOriginInWindow(clipping_view);
		const DarlingObjcRect clip_bounds = NSViewBounds(clipping_view, nullptr);
		if (clip_origin.valid) {
			const double clip_x0 = clip_origin.scale_x * clip_bounds.x +
				clip_origin.translate_x;
			const double clip_x1 = clip_origin.scale_x *
				(clip_bounds.x + clip_bounds.width) + clip_origin.translate_x;
			const double clip_y0 = clip_origin.scale_y * clip_bounds.y +
				clip_origin.translate_y;
			const double clip_y1 = clip_origin.scale_y *
				(clip_bounds.y + clip_bounds.height) + clip_origin.translate_y;
			const int left = ClampWindowCoordinate((std::max)(
				static_cast<double>(x), (std::min)(clip_x0, clip_x1)),
				0, client.right);
			const int right = ClampWindowCoordinate((std::min)(
				static_cast<double>(x + width), (std::max)(clip_x0, clip_x1)),
				0, client.right);
			const int top = ClampWindowCoordinate((std::max)(
				static_cast<double>(y), client.bottom - (std::max)(clip_y0, clip_y1)),
				0, client.bottom);
			const int bottom = ClampWindowCoordinate((std::min)(
				static_cast<double>(y + height), client.bottom -
					(std::min)(clip_y0, clip_y1)), 0, client.bottom);
			HRGN region = right <= left || bottom <= top ?
				CreateRectRgn(0, 0, 0, 0) :
				CreateRectRgn(left - x, top - y, right - x, bottom - y);
			if (region != nullptr && SetWindowRgn(control, region, TRUE) == 0)
				DeleteObject(region);
		}
	}
	ShowWindow(control, hidden ? SW_HIDE : SW_SHOWNA);
}

void AttachViewTree(id view, id window)
{
	if (view == nullptr || window == nullptr) return;
	std::vector<id> children;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(view);
		if (found == g_views.end()) return;
		found->second.window = window;
		children = found->second.subviews;
	}
	PositionNativeControl(view);
	PositionScrollViewScrollbars(view);
	for (id child : children) AttachViewTree(child, window);
}

void RepositionViewTree(id view)
{
	if (view == nullptr) return;
	std::vector<id> children;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(view);
		if (found == g_views.end()) return;
		children = found->second.subviews;
	}
	PositionNativeControl(view);
	PositionScrollViewScrollbars(view);
	for (id child : children) RepositionViewTree(child);
}

void RefreshAncestorScrollView(id view)
{
	id scroll_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto initial = g_views.find(view);
		id ancestor = initial == g_views.end() ? nullptr :
			initial->second.superview;
		std::size_t depth = 0;
		while (ancestor != nullptr && depth++ <= g_views.size()) {
			const auto found = g_views.find(ancestor);
			if (found == g_views.end()) break;
			if (IsViewKindOf(ancestor, "NSScrollView")) {
				scroll_view = ancestor;
				break;
			}
			ancestor = found->second.superview;
		}
	}
	if (scroll_view != nullptr) {
		if (IsViewKindOf(view, "NSClipView"))
			PositionScrollViewScrollbars(scroll_view);
		else
			NSViewSetFrame(scroll_view, nullptr, NSViewFrame(scroll_view, nullptr));
	}
}

void DetachViewTree(id view)
{
	std::vector<id> children;
	HWND control = nullptr;
	HWND vertical_scroller = nullptr;
	HWND horizontal_scroller = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(view);
		if (found == g_views.end()) return;
		found->second.window = nullptr;
		control = found->second.control;
		found->second.control = nullptr;
		vertical_scroller = found->second.vertical_scroller;
		horizontal_scroller = found->second.horizontal_scroller;
		found->second.vertical_scroller = nullptr;
		found->second.horizontal_scroller = nullptr;
		if (control != nullptr) g_controls.erase(control);
		if (vertical_scroller != nullptr)
			g_scrollbar_controls.erase(vertical_scroller);
		if (horizontal_scroller != nullptr)
			g_scrollbar_controls.erase(horizontal_scroller);
		children = found->second.subviews;
	}
	if (control != nullptr) DestroyWindow(control);
	if (vertical_scroller != nullptr) DestroyWindow(vertical_scroller);
	if (horizontal_scroller != nullptr) DestroyWindow(horizontal_scroller);
	for (id child : children) DetachViewTree(child);
}

void CaptureHiddenSubtreeLocked(id view, bool hidden,
	HiddenStateSnapshot& snapshot, std::size_t depth)
{
	if (view == nullptr || depth > g_views.size()) return;
	const auto found = g_views.find(view);
	if (found == g_views.end()) return;
	hidden = hidden || found->second.hidden;
	snapshot.emplace_back(view, hidden);
	for (id child : found->second.subviews)
		CaptureHiddenSubtreeLocked(child, hidden, snapshot, depth + 1);
}

void CaptureHiddenSubtreeLocked(id view, HiddenStateSnapshot& snapshot)
{
	snapshot.reserve(g_views.size());
	const auto found = g_views.find(view);
	if (found == g_views.end()) return;
	bool ancestor_hidden = false;
	id ancestor = found->second.superview;
	std::size_t depth = 0;
	while (ancestor != nullptr && depth++ <= g_views.size()) {
		const auto ancestor_found = g_views.find(ancestor);
		if (ancestor_found == g_views.end()) break;
		ancestor_hidden = ancestor_hidden || ancestor_found->second.hidden;
		ancestor = ancestor_found->second.superview;
	}
	CaptureHiddenSubtreeLocked(view, ancestor_hidden, snapshot, 0);
}

bool IsInViewSubtreeLocked(id candidate, id root)
{
	for (std::size_t depth = 0; candidate != nullptr && depth <= g_views.size();
		++depth) {
		if (candidate == root) return true;
		const auto found = g_views.find(candidate);
		if (found == g_views.end()) return false;
		candidate = found->second.superview;
	}
	return false;
}

bool IsNativeFocusableControl(id view)
{
	return IsViewKindOf(view, "NSButton") ||
		IsViewKindOf(view, "NSTextField") ||
		IsViewKindOf(view, "NSTextView") ||
		IsViewKindOf(view, "NSSlider") ||
		IsViewKindOf(view, "NSStepper");
}

id FindFocusableControlLocked(id view, bool ancestor_hidden = false,
	std::size_t depth = 0)
{
	if (view == nullptr || depth > g_views.size()) return nullptr;
	const auto found = g_views.find(view);
	if (found == g_views.end()) return nullptr;
	const AppKitViewState& state = found->second;
	const bool hidden = ancestor_hidden || state.hidden;
	if (!hidden && state.control != nullptr && state.enabled &&
		IsNativeFocusableControl(view)) return view;
	for (id child : state.subviews) {
		if (id candidate = FindFocusableControlLocked(child, hidden, depth + 1))
			return candidate;
	}
	return nullptr;
}

void CollectFocusableControlsLocked(id view, bool ancestor_hidden,
	std::vector<id>& buttons, std::size_t depth = 0)
{
	if (view == nullptr || depth > g_views.size()) return;
	const auto found = g_views.find(view);
	if (found == g_views.end()) return;
	const AppKitViewState& state = found->second;
	const bool hidden = ancestor_hidden || state.hidden;
	if (!hidden && state.control != nullptr && state.enabled &&
		IsNativeFocusableControl(view)) buttons.push_back(view);
	for (id child : state.subviews)
		CollectFocusableControlsLocked(child, hidden, buttons, depth + 1);
}

bool FocusAdjacentKeyView(HWND current_control, bool reverse)
{
	HWND window_handle = GetParent(current_control);
	std::vector<id> buttons;
	id window_object = nullptr;
	HWND target = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		for (const auto& [object, state] : g_windows) {
			if (state.handle == window_handle) {
				window_object = object;
				CollectFocusableControlsLocked(state.content_view, false, buttons);
				break;
			}
		}
		if (buttons.empty()) return false;
		const auto source_found = g_controls.find(current_control);
		if (source_found == g_controls.end()) return false;
		const id source = source_found->second;
		id destination = nullptr;
		if (!reverse) {
			std::vector<id> visited;
			const auto source_state = g_views.find(source);
			id linked = source_state == g_views.end() ? nullptr :
				source_state->second.next_key_view;
			while (linked != nullptr && visited.size() < g_views.size()) {
				if (std::find(visited.begin(), visited.end(), linked) != visited.end())
					break;
				visited.push_back(linked);
				if (std::find(buttons.begin(), buttons.end(), linked) != buttons.end()) {
					destination = linked;
					break;
				}
				const auto linked_state = g_views.find(linked);
				linked = linked_state == g_views.end() ? nullptr :
					linked_state->second.next_key_view;
			}
		} else {
			const auto source_position = std::find(buttons.begin(), buttons.end(), source);
			if (source_position != buttons.end()) {
				for (std::size_t offset = 1; offset < buttons.size(); ++offset) {
					const std::size_t index = (static_cast<std::size_t>(
						source_position - buttons.begin()) + buttons.size() - offset) %
						buttons.size();
					const auto candidate = g_views.find(buttons[index]);
					if (candidate != g_views.end() &&
						candidate->second.next_key_view == source) {
						destination = buttons[index];
						break;
					}
				}
			}
		}
		if (destination == nullptr) {
			const auto current = std::find(buttons.begin(), buttons.end(), source);
			std::size_t index = current == buttons.end() ?
				(reverse ? 0 : buttons.size() - 1) :
				static_cast<std::size_t>(current - buttons.begin());
			index = reverse ? (index + buttons.size() - 1) % buttons.size() :
				(index + 1) % buttons.size();
			destination = buttons[index];
		}
		const auto target_view = g_views.find(destination);
		if (target_view == g_views.end()) return false;
		target = target_view->second.control;
		if (target == nullptr) return false;
		const auto window = g_windows.find(window_object);
		if (window == g_windows.end()) return false;
		window->second.first_responder = destination;
	}
	if (target != nullptr) SetFocus(target);
	return target != nullptr;
}

void NSWindowSelectKeyView(id self, bool reverse)
{
	std::vector<id> key_views;
	id destination = nullptr;
	HWND target = nullptr;
	HWND current_control = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto window = g_windows.find(self);
		if (window == g_windows.end()) return;
		CollectFocusableControlsLocked(window->second.content_view, false,
			key_views);
		if (key_views.empty()) return;
		const auto current = std::find(key_views.begin(), key_views.end(),
			window->second.first_responder);
		if (current != key_views.end()) {
			const auto view = g_views.find(*current);
			if (view != g_views.end()) current_control = view->second.control;
		} else {
			destination = reverse ? key_views.back() : key_views.front();
			const auto view = g_views.find(destination);
			if (view == g_views.end()) return;
			target = view->second.control;
			window->second.first_responder = destination;
		}
	}
	if (current_control != nullptr) {
		FocusAdjacentKeyView(current_control, reverse);
	} else if (target != nullptr) {
		SetFocus(target);
	}
}

void NSWindowSelectNextKeyView(id self, SEL, id)
{
	NSWindowSelectKeyView(self, false);
}

void NSWindowSelectPreviousKeyView(id self, SEL, id)
{
	NSWindowSelectKeyView(self, true);
}

LRESULT CALLBACK AppKitControlWindowProc(HWND control, UINT message,
	WPARAM wparam, LPARAM lparam)
{
	const auto original = reinterpret_cast<WNDPROC>(GetPropW(control,
		kButtonOriginalProcProperty));
	if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
		message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
		id view = nullptr;
		bool is_stepper = false;
		bool autorepeat = true;
		bool was_pressed = false;
		int pressed_direction = 0;
		{
			std::lock_guard lock(g_windows_mutex);
			const auto control_view = g_controls.find(control);
			if (control_view != g_controls.end()) {
				view = control_view->second;
				const auto state = g_views.find(view);
				is_stepper = IsViewKindOf(view, "NSStepper");
				if (state != g_views.end()) {
					autorepeat = state->second.stepper_autorepeat;
					was_pressed = state->second.stepper_mouse_pressed;
					pressed_direction = state->second.stepper_mouse_direction;
				}
			}
		}
		if (is_stepper && !autorepeat && message == WM_LBUTTONDOWN) {
			RECT bounds{};
			GetClientRect(control, &bounds);
			const int direction = static_cast<short>(HIWORD(lparam)) <
				(bounds.top + bounds.bottom) / 2 ? 1 : -1;
			{
				std::lock_guard lock(g_windows_mutex);
				const auto state = g_views.find(view);
				if (state != g_views.end()) {
					state->second.stepper_mouse_pressed = true;
					state->second.stepper_mouse_direction = direction;
				}
			}
			SetCapture(control);
			return original == nullptr ? 0 : CallWindowProcW(original, control,
				message, wparam, lparam);
		}
		if (is_stepper && message == WM_LBUTTONUP && was_pressed) {
			if (original != nullptr)
				CallWindowProcW(original, control, message, wparam, lparam);
			{
				std::lock_guard lock(g_windows_mutex);
				const auto state = g_views.find(view);
				if (state != g_views.end())
					state->second.stepper_mouse_pressed = false;
			}
			if (GetCapture() == control) ReleaseCapture();
			RECT bounds{};
			GetClientRect(control, &bounds);
			const int y = static_cast<short>(HIWORD(lparam));
			const int release_direction = y <
				(bounds.top + bounds.bottom) / 2 ? 1 : -1;
			const POINT release_point{static_cast<short>(LOWORD(lparam)), y};
			if (PtInRect(&bounds, release_point) &&
				release_direction == pressed_direction)
				StepNSStepper(control, pressed_direction);
			return 0;
		}
		if (is_stepper && !autorepeat &&
			(message == WM_CAPTURECHANGED || message == WM_CANCELMODE)) {
			std::lock_guard lock(g_windows_mutex);
			const auto state = g_views.find(view);
			if (state != g_views.end()) state->second.stepper_mouse_pressed = false;
			return 0;
		}
	}
	if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
		const HWND window = GetParent(control);
		if (DispatchAppKitScrollEvent(window, message, wparam, lparam)) return 0;
		const auto object = reinterpret_cast<id>(GetWindowLongPtrW(window,
			GWLP_USERDATA));
		id root = nullptr;
		{
			std::lock_guard lock(g_windows_mutex);
			const auto found = g_windows.find(object);
			if (found != g_windows.end()) root = found->second.content_view;
		}
		POINT local{};
		if (GetCursorPos(&local) && ScreenToClient(window, &local)) {
			RECT client{};
			GetClientRect(window, &client);
			if (ScrollAppKitViewAtPoint(root, local.x,
				client.bottom - local.y, GET_WHEEL_DELTA_WPARAM(wparam),
				message == WM_MOUSEHWHEEL)) return 0;
		}
	}
	if (message == WM_CONTEXTMENU) {
		id view = nullptr;
		{
			std::lock_guard lock(g_windows_mutex);
			const auto found = g_controls.find(control);
			if (found != g_controls.end()) view = found->second;
		}
		id menu = view == nullptr ? nullptr :
			objc_getAssociatedObject(view, &g_view_menu_key);
		if (menu != nullptr) {
			POINT screen{static_cast<short>(LOWORD(lparam)),
				static_cast<short>(HIWORD(lparam))};
			if (screen.x == -1 && screen.y == -1) GetCursorPos(&screen);
			PopupNativeAppMenu(menu, GetParent(control), screen);
			return 0;
		}
	}
	if ((message == WM_KEYDOWN || message == WM_KEYUP ||
		message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
		DispatchAppKitKeyEvent(control, message, wparam, lparam)) return 0;
	if (message == WM_GETDLGCODE && original != nullptr)
		return CallWindowProcW(original, control, message, wparam, lparam) |
			DLGC_WANTTAB;
	if (message == WM_KEYDOWN && wparam == VK_TAB) {
		const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		if (FocusAdjacentKeyView(control, reverse)) return 0;
	}
	if (message == WM_KEYDOWN && wparam == VK_RETURN) {
		id view = nullptr;
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			const auto found = g_controls.find(control);
			if (found != g_controls.end()) view = found->second;
		}
		if (view != nullptr && IsViewKindOf(view, "NSTextField")) {
			DispatchControlAction(control);
			return 0;
		}
	}
	if (message == WM_NCDESTROY)
		RemovePropW(control, kButtonOriginalProcProperty);
	return original == nullptr ? DefWindowProcW(control, message, wparam, lparam) :
		CallWindowProcW(original, control, message, wparam, lparam);
}

void NotifyHiddenStateChanges(const HiddenStateSnapshot& before,
	const HiddenStateSnapshot& after)
{
	std::vector<std::pair<id, SEL>> callbacks;
	callbacks.reserve(after.size());
	for (const auto& [view, hidden] : after) {
		const auto previous = std::find_if(before.begin(), before.end(),
			[view](const auto& entry) { return entry.first == view; });
		if (previous == before.end() || previous->second == hidden) continue;
		const SEL callback = sel_registerName(hidden ? "viewDidHide" : "viewDidUnhide");
		if (class_getInstanceMethod(object_getClass(view), callback) == nullptr)
			continue;
		objc_retain(view);
		callbacks.emplace_back(view, callback);
	}
	for (const auto& [view, callback] : callbacks) {
		darling_objc_msgSend_void0(view, callback);
		objc_release(view);
	}
}

void DispatchControlAction(HWND control)
{
	id target = nullptr;
	id sender = nullptr;
	SEL action = nullptr;
	std::vector<std::pair<HWND, std::int64_t>> button_updates;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto control_found = g_controls.find(control);
		if (control_found == g_controls.end()) return;
		sender = control_found->second;
		const auto view_found = g_views.find(sender);
		if (view_found == g_views.end()) return;
		objc_retain(sender);
		const auto window_found = g_windows.find(view_found->second.window);
		if (window_found != g_windows.end())
			window_found->second.first_responder = sender;
		target = view_found->second.target;
		if (target != nullptr) objc_retain(target);
		action = view_found->second.action;
		if (IsViewKindOf(sender, "NSButton")) {
			auto& button = view_found->second;
			if (button.button_type == 4) {
				button.button_state = 1;
				button_updates.emplace_back(control, 1);
				const id parent = button.superview;
				if (parent != nullptr) {
					const auto parent_view = g_views.find(parent);
					if (parent_view != g_views.end()) {
						for (id sibling : parent_view->second.subviews) {
							if (sibling == sender) continue;
							const auto sibling_view = g_views.find(sibling);
							if (sibling_view == g_views.end() ||
								sibling_view->second.button_type != 4) continue;
							sibling_view->second.button_state = 0;
							if (sibling_view->second.control != nullptr)
								button_updates.emplace_back(sibling_view->second.control, 0);
						}
					}
				}
			} else if (button.button_type == 1 || button.button_type == 2 ||
				button.button_type == 3 || button.button_type == 6) {
				if (button.button_allows_mixed_state) {
					button.button_state = button.button_state == 0 ? 1 :
						button.button_state == 1 ? -1 : 0;
				} else {
					button.button_state = button.button_state == 1 ? 0 : 1;
				}
				button_updates.emplace_back(control, button.button_state);
			}
		}
	}
	for (const auto& [button_control, state] : button_updates)
		SendMessageW(button_control, BM_SETCHECK,
			state < 0 ? BST_INDETERMINATE :
				state == 0 ? BST_UNCHECKED : BST_CHECKED, 0);
	if (action != nullptr)
		NSApplicationSendAction(g_shared_application, nullptr, action, target, sender);
	if (target != nullptr) objc_release(target);
	objc_release(sender);
}

void UpdateTextFieldValue(HWND control)
{
	const int length = GetWindowTextLengthW(control);
	if (length < 0 || length > 16 * 1024 * 1024) return;
	std::vector<wchar_t> wide(static_cast<std::size_t>(length) + 1);
	const int copied = GetWindowTextW(control, wide.data(), length + 1);
	if (copied < 0 || copied > length) return;
	std::string utf8;
	if (!ConvertWideToUtf8(std::wstring_view(wide.data(),
		static_cast<std::size_t>(copied)), utf8)) return;
	id value = CreateDynamicNSString(objc_getClass("NSString"), utf8.c_str());
	if (value == nullptr) return;
	id previous = nullptr;
	bool accepted = false;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto control_found = g_controls.find(control);
		if (control_found != g_controls.end() &&
			(IsViewKindOf(control_found->second, "NSTextField") ||
				IsViewKindOf(control_found->second, "NSTextView"))) {
			AppKitViewState& state = g_views[control_found->second];
			previous = state.title;
			state.title = value;
			accepted = true;
		}
	}
	if (!accepted) objc_release(value);
	if (previous != nullptr) objc_release(previous);
}

id NSWindowNextResponder(id, SEL)
{
	return g_shared_application;
}

id NSResponderNextResponder(id self, SEL)
{
	return objc_getAssociatedObject(self, &g_next_responder_key);
}

void NSResponderSetNextResponder(id self, SEL, id responder)
{
	objc_setAssociatedObject(self, &g_next_responder_key, responder,
		OBJC_ASSOCIATION_ASSIGN);
}

bool NSResponderAcceptsFirstResponder(id, SEL)
{
	return false;
}

bool NSWindowAcceptsFirstResponder(id, SEL)
{
	return true;
}

bool NSControlAcceptsFirstResponder(id, SEL)
{
	return true;
}

void NSViewFinalize(id object)
{
	HWND focus_handle = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto view = g_views.find(object);
		if (view != g_views.end() && view->second.window != nullptr) {
			const auto window = g_windows.find(view->second.window);
			if (window != g_windows.end() && window->second.first_responder != nullptr &&
				IsInViewSubtreeLocked(window->second.first_responder, object)) {
				window->second.first_responder = view->second.window;
				focus_handle = window->second.handle;
			}
		}
	}
	DetachViewTree(object);
	if (focus_handle != nullptr) SetFocus(focus_handle);
	AppKitViewState state{};
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(object);
		if (found == g_views.end()) return;
		state = std::move(found->second);
		if (state.superview != nullptr) {
			const auto parent = g_views.find(state.superview);
			if (parent != g_views.end()) {
				auto& siblings = parent->second.subviews;
				siblings.erase(std::remove(siblings.begin(), siblings.end(), object),
					siblings.end());
			}
		}
		for (id child : state.subviews) {
			const auto child_state = g_views.find(child);
			if (child_state != g_views.end() &&
				child_state->second.superview == object)
				child_state->second.superview = nullptr;
		}
		for (auto& [view_object, view_state] : g_views) {
			if (view_object != object && view_state.next_key_view == object)
				view_state.next_key_view = nullptr;
		}
		for (id item : state.popup_items) g_popup_item_owners.erase(item);
		g_views.erase(found);
	}
	if (state.title != nullptr) objc_release(state.title);
	if (state.placeholder != nullptr) objc_release(state.placeholder);
	if (state.image != nullptr) objc_release(state.image);
	if (state.target != nullptr) objc_release(state.target);
	for (id child : state.subviews) objc_release(child);
	for (id item : state.popup_items) objc_release(item);
}

id NSViewInitWithFrame(id self, SEL, DarlingObjcRect frame)
{
	if (self == nullptr) return nullptr;
	bool inserted = false;
	try {
		std::lock_guard lock(g_windows_mutex);
		auto [found, was_inserted] = g_views.try_emplace(self);
		inserted = was_inserted;
		found->second.frame = frame;
		found->second.bounds = DarlingObjcRect{0.0, 0.0, frame.width,
			frame.height};
	} catch (...) { return nullptr; }
	if (inserted && !darling_objc_register_object_finalizer(self,
		&NSViewFinalize)) {
		NSViewFinalize(self);
		return nullptr;
	}
	return self;
}

id NSViewInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr) return nullptr;
	id key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSFrame"));
	DarlingObjcRect frame = NSKeyedArchiveDecodeRectForKey(key);
	if (frame.width <= 0.0 || frame.height <= 0.0) {
		id size_key = objc_autorelease(CreateDynamicNSString(
			objc_getClass("NSString"), "NSFrameSize"));
		id encoded_size = g_active_keyed_archive_decoder == nullptr ? nullptr :
			g_active_keyed_archive_decoder->DecodeForKey(size_key);
		const char* size_text = NSStringUTF8String(encoded_size, nullptr);
		double width = 0.0;
		double height = 0.0;
		const bool has_size = ParseKeyedSizeString(size_text, width, height);
		if (encoded_size != nullptr) objc_release(encoded_size);
		if (has_size) frame = DarlingObjcRect{0.0, 0.0, width, height};
	}
	if (NSViewInitWithFrame(self, sel_registerName("initWithFrame:"), frame) == nullptr)
		return nullptr;
	key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSBounds"));
	if (g_active_keyed_archive_decoder != nullptr &&
		g_active_keyed_archive_decoder->ContainsKey(key))
		NSViewSetBounds(self, sel_registerName("setBounds:"),
			NSKeyedArchiveDecodeRectForKey(key));
	key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSAutoresizingMask"));
	if (g_active_keyed_archive_decoder != nullptr &&
		g_active_keyed_archive_decoder->ContainsKey(key)) {
		id encoded_mask = g_active_keyed_archive_decoder->DecodeForKey(key);
		std::int64_t mask = 0;
		if (encoded_mask != nullptr) {
			darling_windows_CFNumberGetInteger(encoded_mask, &mask);
			objc_release(encoded_mask);
		}
		NSViewSetAutoresizingMask(self, nullptr,
			static_cast<std::uint64_t>(mask));
	}
	key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSSubviews"));
	id subviews = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	std::vector<id> children;
	if (subviews != nullptr) {
		try {
			std::lock_guard lock(g_array_mutex);
			const auto found = g_arrays.find(subviews);
			if (found != g_arrays.end()) {
				children.reserve(found->second.size());
				for (id child : found->second) children.push_back(objc_retain(child));
			}
		} catch (...) {
			for (id child : children) objc_release(child);
			children.clear();
		}
		objc_release(subviews);
	}
	for (id child : children) {
		if (child != self) NSViewAddSubview(self, sel_registerName("addSubview:"), child);
		objc_release(child);
	}
	key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSvFlags"));
	const auto flags = static_cast<std::uint32_t>(
		NSKeyedUnarchiverDecodeIntForKey(coder, nullptr, key));
	NSViewSetHidden(self, sel_registerName("setHidden:"),
		(flags & 0x80000000u) != 0);
	key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSNextKeyView"));
	id next_key_view = g_active_keyed_archive_decoder == nullptr ? nullptr :
		g_active_keyed_archive_decoder->DecodeForKey(key);
	if (next_key_view != nullptr) {
		NSViewSetNextKeyView(self, sel_registerName("setNextKeyView:"),
			next_key_view);
		objc_release(next_key_view);
	}
	return self;
}

id NSViewMenu(id self, SEL)
{
	return objc_getAssociatedObject(self, &g_view_menu_key);
}

void NSViewSetMenu(id self, SEL, id menu)
{
	objc_setAssociatedObject(self, &g_view_menu_key, menu,
		OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

void NSViewDrawRect(id, SEL, DarlingObjcRect)
{
}

DarlingObjcRect NSViewFrame(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? DarlingObjcRect{} : found->second.frame;
}

DarlingObjcPoint NSViewFrameOrigin(id self, SEL)
{
	const DarlingObjcRect frame = NSViewFrame(self, nullptr);
	return DarlingObjcPoint{frame.x, frame.y};
}

DarlingObjcPoint NSViewFrameSize(id self, SEL)
{
	const DarlingObjcRect frame = NSViewFrame(self, nullptr);
	return DarlingObjcPoint{frame.width, frame.height};
}

void NSViewSetFrameOrigin(id self, SEL, DarlingObjcPoint origin)
{
	DarlingObjcRect frame = NSViewFrame(self, nullptr);
	frame.x = origin.x;
	frame.y = origin.y;
	NSViewSetFrame(self, sel_registerName("setFrame:"), frame);
}

void NSViewSetFrameSize(id self, SEL, DarlingObjcPoint size)
{
	DarlingObjcRect frame = NSViewFrame(self, nullptr);
	frame.width = size.x;
	frame.height = size.y;
	NSViewSetFrame(self, sel_registerName("setFrame:"), frame);
}

DarlingObjcRect NSViewBounds(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? DarlingObjcRect{} : found->second.bounds;
}

bool NSViewIsFlipped(id, SEL)
{
	return false;
}

ViewCoordinateOrigin NSViewOriginInWindow(id view)
{
	ViewCoordinateOrigin origin{};
	if (view == nullptr) return origin;
	std::vector<ViewCoordinateStep> steps;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto initial = g_views.find(view);
		if (initial == g_views.end() || initial->second.window == nullptr) return origin;
		origin.window = initial->second.window;
		id current = view;
		std::size_t depth = 0;
		while (current != nullptr && depth++ <= g_views.size()) {
			const auto found = g_views.find(current);
			if (found == g_views.end() || found->second.window != origin.window)
				return {};
			steps.push_back(ViewCoordinateStep{current, found->second.superview,
				found->second.frame, found->second.bounds});
			current = found->second.superview;
		}
		if (current != nullptr) return {};
	}
	const SEL is_flipped = sel_registerName("isFlipped");
	for (const ViewCoordinateStep& step : steps) {
		const bool child_flipped = darling_objc_msgSend_bool0(step.view,
			is_flipped);
		const bool parent_flipped = step.superview != nullptr &&
			darling_objc_msgSend_bool0(step.superview, is_flipped);
		const double scale_x = step.bounds.width != 0.0 ?
			step.frame.width / step.bounds.width : 1.0;
		const double scale_magnitude_y = step.bounds.height != 0.0 ?
			step.frame.height / step.bounds.height : 1.0;
		if (!std::isfinite(scale_x) || !std::isfinite(scale_magnitude_y)) return {};
		const double scale_y = child_flipped == parent_flipped ?
			scale_magnitude_y : -scale_magnitude_y;
		const double translate_x = step.frame.x - step.bounds.x * scale_x;
		const double translate_y = scale_y >= 0.0 ?
			step.frame.y - step.bounds.y * scale_y :
			step.frame.y + step.frame.height + step.bounds.y * scale_magnitude_y;
		origin.translate_x = scale_x * origin.translate_x + translate_x;
		origin.translate_y = scale_y * origin.translate_y + translate_y;
		origin.scale_x *= scale_x;
		origin.scale_y *= scale_y;
		if (!std::isfinite(origin.scale_x) || !std::isfinite(origin.scale_y) ||
			!std::isfinite(origin.translate_x) ||
			!std::isfinite(origin.translate_y)) return {};
	}
	origin.valid = true;
	return origin;
}

void DrawViewTree(id view, darling_windows_CGContextRef context,
	DarlingObjcRect dirty_window_rect, std::size_t depth)
{
	if (view == nullptr || context == nullptr || depth >= 256) return;
	DarlingObjcRect bounds{};
	std::vector<id> subviews;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(view);
		if (found == g_views.end() || found->second.hidden) return;
		bounds = found->second.bounds;
		try { subviews = found->second.subviews; }
		catch (...) { return; }
	}
	const ViewCoordinateOrigin origin = NSViewOriginInWindow(view);
	if (!origin.valid || origin.scale_x == 0.0 || origin.scale_y == 0.0)
		return;
	const double local_x0 = (dirty_window_rect.x - origin.translate_x) /
		origin.scale_x;
	const double local_x1 = (dirty_window_rect.x + dirty_window_rect.width -
		origin.translate_x) / origin.scale_x;
	const double local_y0 = (dirty_window_rect.y - origin.translate_y) /
		origin.scale_y;
	const double local_y1 = (dirty_window_rect.y + dirty_window_rect.height -
		origin.translate_y) / origin.scale_y;
	const double dirty_x0 = (std::max)(bounds.x, (std::min)(local_x0, local_x1));
	const double dirty_y0 = (std::max)(bounds.y, (std::min)(local_y0, local_y1));
	const double dirty_x1 = (std::min)(bounds.x + bounds.width,
		(std::max)(local_x0, local_x1));
	const double dirty_y1 = (std::min)(bounds.y + bounds.height,
		(std::max)(local_y0, local_y1));
	if (!(dirty_x1 > dirty_x0 && dirty_y1 > dirty_y0)) return;
	const DarlingObjcRect local_dirty_rect{dirty_x0, dirty_y0,
		dirty_x1 - dirty_x0, dirty_y1 - dirty_y0};
	const COLORREF saved_text_color = g_current_text_color;
	const double saved_text_alpha = g_current_text_alpha;
	CGContextSaveGState(context);
	CGContextSetCTM(context, {origin.scale_x, 0.0, 0.0, origin.scale_y,
		origin.translate_x, origin.translate_y});
	CGContextClipToRect(context, {{bounds.x, bounds.y},
		{bounds.width, bounds.height}});
	darling_objc_msgSend_void_rect1(view, sel_registerName("drawRect:"),
		local_dirty_rect);
	for (id child : subviews)
		DrawViewTree(child, context, dirty_window_rect, depth + 1);
	CGContextRestoreGState(context);
	g_current_text_color = saved_text_color;
	g_current_text_alpha = saved_text_alpha;
}

void NSViewSetNeedsDisplay(id self, SEL, bool needs_display)
{
	if (!needs_display) return;
	NSViewSetNeedsDisplayInRect(self, nullptr, NSViewBounds(self, nullptr));
}

void NSViewSetNeedsDisplayInRect(id self, SEL, DarlingObjcRect dirty_rect)
{
	if (self == nullptr || !std::isfinite(dirty_rect.x) ||
		!std::isfinite(dirty_rect.y) || !std::isfinite(dirty_rect.width) ||
		!std::isfinite(dirty_rect.height) || dirty_rect.width <= 0.0 ||
		dirty_rect.height <= 0.0) return;
	const ViewCoordinateOrigin origin = NSViewOriginInWindow(self);
	if (!origin.valid) return;
	HWND window = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(origin.window);
		if (found == g_windows.end()) return;
		window = found->second.handle;
	}
	if (window == nullptr) return;
	RECT client{};
	if (!GetClientRect(window, &client)) return;
	const double x0 = origin.scale_x * dirty_rect.x + origin.translate_x;
	const double x1 = origin.scale_x * (dirty_rect.x + dirty_rect.width) +
		origin.translate_x;
	const double y0 = origin.scale_y * dirty_rect.y + origin.translate_y;
	const double y1 = origin.scale_y * (dirty_rect.y + dirty_rect.height) +
		origin.translate_y;
	if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) ||
		!std::isfinite(y1)) return;
	const auto clamp_coordinate = [](double value, LONG low, LONG high) {
		return static_cast<LONG>(std::clamp(value, static_cast<double>(low),
			static_cast<double>(high)));
	};
	RECT native_rect{
		clamp_coordinate(std::floor((std::min)(x0, x1)), client.left, client.right),
		clamp_coordinate(std::floor(client.bottom - (std::max)(y0, y1)),
			client.top, client.bottom),
		clamp_coordinate(std::ceil((std::max)(x0, x1)), client.left, client.right),
		clamp_coordinate(std::ceil(client.bottom - (std::min)(y0, y1)),
			client.top, client.bottom)};
	if (native_rect.right <= native_rect.left ||
		native_rect.bottom <= native_rect.top) return;
	InvalidateRect(window, &native_rect, FALSE);
}

DarlingObjcPoint NSViewConvertPointToView(id self, SEL,
	DarlingObjcPoint point, id target_view)
{
	const ViewCoordinateOrigin source = NSViewOriginInWindow(self);
	if (!source.valid) return point;
	const ViewCoordinateOrigin target = target_view == nullptr ?
		ViewCoordinateOrigin{source.window, 1.0, 1.0, 0.0, 0.0, true} :
		NSViewOriginInWindow(target_view);
	if (!target.valid || target.window != source.window ||
		target.scale_x == 0.0 || target.scale_y == 0.0) return point;
	point.x = (source.scale_x * point.x + source.translate_x -
		target.translate_x) / target.scale_x;
	point.y = (source.scale_y * point.y + source.translate_y -
		target.translate_y) / target.scale_y;
	return point;
}

DarlingObjcPoint NSViewConvertPointFromView(id self, SEL,
	DarlingObjcPoint point, id source_view)
{
	const ViewCoordinateOrigin target = NSViewOriginInWindow(self);
	if (!target.valid) return point;
	const ViewCoordinateOrigin source = source_view == nullptr ?
		ViewCoordinateOrigin{target.window, 1.0, 1.0, 0.0, 0.0, true} :
		NSViewOriginInWindow(source_view);
	if (!source.valid || source.window != target.window ||
		target.scale_x == 0.0 || target.scale_y == 0.0) return point;
	point.x = (source.scale_x * point.x + source.translate_x -
		target.translate_x) / target.scale_x;
	point.y = (source.scale_y * point.y + source.translate_y -
		target.translate_y) / target.scale_y;
	return point;
}

DarlingObjcRect NSViewConvertRectToView(id self, SEL,
	DarlingObjcRect rect, id target_view)
{
	const DarlingObjcPoint corners[] = {
		{rect.x, rect.y}, {rect.x + rect.width, rect.y},
		{rect.x, rect.y + rect.height},
		{rect.x + rect.width, rect.y + rect.height}};
	DarlingObjcPoint converted[4]{};
	for (std::size_t index = 0; index < 4; ++index) {
		converted[index] = NSViewConvertPointToView(self, nullptr,
			corners[index], target_view);
		if (!std::isfinite(converted[index].x) ||
			!std::isfinite(converted[index].y)) return rect;
	}
	const auto [minimum_x, maximum_x] = std::minmax_element(std::begin(converted),
		std::end(converted), [](const auto& left, const auto& right) {
			return left.x < right.x;
		});
	const auto [minimum_y, maximum_y] = std::minmax_element(std::begin(converted),
		std::end(converted), [](const auto& left, const auto& right) {
			return left.y < right.y;
		});
	return {minimum_x->x, minimum_y->y, maximum_x->x - minimum_x->x,
		maximum_y->y - minimum_y->y};
}

DarlingObjcRect NSViewConvertRectFromView(id self, SEL,
	DarlingObjcRect rect, id source_view)
{
	const DarlingObjcPoint corners[] = {
		{rect.x, rect.y}, {rect.x + rect.width, rect.y},
		{rect.x, rect.y + rect.height},
		{rect.x + rect.width, rect.y + rect.height}};
	DarlingObjcPoint converted[4]{};
	for (std::size_t index = 0; index < 4; ++index) {
		converted[index] = NSViewConvertPointFromView(self, nullptr,
			corners[index], source_view);
		if (!std::isfinite(converted[index].x) ||
			!std::isfinite(converted[index].y)) return rect;
	}
	const auto [minimum_x, maximum_x] = std::minmax_element(std::begin(converted),
		std::end(converted), [](const auto& left, const auto& right) {
			return left.x < right.x;
		});
	const auto [minimum_y, maximum_y] = std::minmax_element(std::begin(converted),
		std::end(converted), [](const auto& left, const auto& right) {
			return left.y < right.y;
		});
	return {minimum_x->x, minimum_y->y, maximum_x->x - minimum_x->x,
		maximum_y->y - minimum_y->y};
}

double ResizeAutoresizingAxis(double origin, double size, double old_extent,
	double new_extent, double bounds_origin, std::uint64_t mask,
	std::uint64_t min_bit,
	std::uint64_t size_bit, std::uint64_t max_bit, double& resized_size)
{
	const double delta = new_extent - old_extent;
	const double leading = origin - bounds_origin;
	const double trailing = old_extent - leading - size;
	double flexible = 0.0;
	if (mask & min_bit) flexible += (std::max)(0.0, leading);
	if (mask & size_bit) flexible += (std::max)(0.0, size);
	if (mask & max_bit) flexible += (std::max)(0.0, trailing);
	if (flexible <= 0.0 || delta == 0.0) {
		resized_size = size;
		return origin;
	}
	const double min_delta = (mask & min_bit) ?
		delta * (std::max)(0.0, leading) / flexible : 0.0;
	const double size_delta = (mask & size_bit) ?
		delta * (std::max)(0.0, size) / flexible : 0.0;
	resized_size = (std::max)(0.0, size + size_delta);
	return origin + min_delta;
}

void ApplyAutoresizingMask(id parent, double old_width, double old_height,
	double new_width, double new_height)
{
	std::vector<id> children;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(parent);
		if (found == g_views.end()) return;
		children = found->second.subviews;
	}
	if (old_width == new_width && old_height == new_height) return;
	const SEL selector = sel_registerName("resizeWithOldSuperviewSize:");
	for (id child : children)
		darling_objc_msgSend_void1_size(child, selector,
			DarlingObjcPoint{old_width, old_height});
}

void NSViewResizeWithOldSuperviewSize(id self, SEL,
	DarlingObjcPoint old_size)
{
	id parent = nullptr;
	DarlingObjcRect frame{};
	std::uint64_t mask = 0;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		parent = found->second.superview;
		frame = found->second.frame;
		mask = found->second.autoresizing_mask;
	}
	if (parent == nullptr) return;
	const DarlingObjcRect parent_bounds = NSViewBounds(parent, nullptr);
	frame.x = ResizeAutoresizingAxis(frame.x, frame.width, old_size.x,
		parent_bounds.width, parent_bounds.x, mask, 1ull, 2ull, 4ull,
		frame.width);
	frame.y = ResizeAutoresizingAxis(frame.y, frame.height, old_size.y,
		parent_bounds.height, parent_bounds.y, mask, 8ull, 16ull, 32ull,
		frame.height);
	const DarlingObjcRect old_frame = NSViewFrame(self, nullptr);
	if (frame.x != old_frame.x || frame.y != old_frame.y ||
		frame.width != old_frame.width || frame.height != old_frame.height)
		NSViewSetFrame(self, nullptr, frame);
}

void NSViewResizeSubviewsWithOldSize(id self, SEL,
	DarlingObjcPoint old_size)
{
	const DarlingObjcRect bounds = NSViewBounds(self, nullptr);
	ApplyAutoresizingMask(self, old_size.x, old_size.y, bounds.width,
		bounds.height);
}

void ResizeAppKitSubviews(id parent, double old_width, double old_height,
	double new_width, double new_height)
{
	if (old_width == new_width && old_height == new_height) return;
	const SEL selector = sel_registerName("resizeSubviewsWithOldSize:");
	const Method method = class_getInstanceMethod(object_getClass(parent), selector);
	const IMP implementation = method == nullptr ? nullptr :
		method_getImplementation(method);
	if (implementation != nullptr && implementation !=
		reinterpret_cast<IMP>(&NSViewResizeSubviewsWithOldSize)) {
		darling_objc_msgSend_void1_size(parent, selector,
			DarlingObjcPoint{old_width, old_height});
		return;
	}
	ApplyAutoresizingMask(parent, old_width, old_height, new_width,
		new_height);
}

std::uint64_t NSViewAutoresizingMask(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0 : found->second.autoresizing_mask;
}

void NSViewSetAutoresizingMask(id self, SEL, std::uint64_t mask)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	g_views[self].autoresizing_mask = mask & 63ull;
}

void NSViewSetBounds(id self, SEL, DarlingObjcRect bounds)
{
	DarlingObjcRect previous{};
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		previous = state.bounds;
		state.bounds = bounds;
		state.has_custom_bounds = true;
	}
	ResizeAppKitSubviews(self, previous.width, previous.height,
		bounds.width, bounds.height);
	RepositionViewTree(self);
	RefreshAncestorScrollView(self);
	InvalidateAppKitView(self);
}

id NSScrollViewInitWithFrame(id self, SEL, DarlingObjcRect frame)
{
	self = NSViewInitWithFrame(self, nullptr, frame);
	if (self == nullptr) return nullptr;
	Class clip_class = objc_getClass("NSClipView");
	if (clip_class == nullptr) return self;
	id clip_view = class_createInstance(clip_class, 0);
	if (clip_view == nullptr) return self;
	clip_view = NSViewInitWithFrame(clip_view, nullptr,
		DarlingObjcRect{0.0, 0.0, frame.width, frame.height});
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].clip_view = clip_view;
	}
	NSViewAddSubview(self, nullptr, clip_view);
	objc_release(clip_view);
	return self;
}

id NSScrollViewContentView(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? nullptr : found->second.clip_view;
}

void NSScrollViewSetContentView(id self, SEL, id clip_view)
{
	if (clip_view != nullptr && !IsViewKindOf(clip_view, "NSClipView")) return;
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		previous = found->second.clip_view;
		found->second.clip_view = clip_view;
	}
	if (previous == clip_view) return;
	if (previous != nullptr) NSViewRemoveFromSuperview(previous, nullptr);
	if (clip_view != nullptr) NSViewAddSubview(self, nullptr, clip_view);
	if (clip_view != nullptr)
		NSViewSetFrame(self, nullptr, NSViewFrame(self, nullptr));
	PositionScrollViewScrollbars(self);
}

id NSClipViewDocumentView(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.subviews.empty() ? nullptr :
		found->second.subviews.front();
}

void NSClipViewSetDocumentView(id self, SEL, id document_view)
{
	const id previous = NSClipViewDocumentView(self, nullptr);
	if (previous == document_view) return;
	if (previous != nullptr) NSViewRemoveFromSuperview(previous, nullptr);
	if (document_view != nullptr) NSViewAddSubview(self, nullptr, document_view);
}

DarlingObjcPoint NSClipViewConstrainScrollPoint(id self, SEL,
	DarlingObjcPoint point)
{
	DarlingObjcRect bounds = NSViewBounds(self, nullptr);
	const id document_view = NSClipViewDocumentView(self, nullptr);
	if (document_view == nullptr) return DarlingObjcPoint{bounds.x, bounds.y};
	const DarlingObjcRect frame = NSViewFrame(document_view, nullptr);
	const double minimum_x = frame.x;
	const double minimum_y = frame.y;
	const double maximum_x = (std::max)(minimum_x,
		frame.x + frame.width - bounds.width);
	const double maximum_y = (std::max)(minimum_y,
		frame.y + frame.height - bounds.height);
	point.x = std::clamp(point.x, minimum_x, maximum_x);
	point.y = std::clamp(point.y, minimum_y, maximum_y);
	return point;
}

void NSClipViewScrollToPoint(id self, SEL, DarlingObjcPoint point)
{
	DarlingObjcRect bounds = NSViewBounds(self, nullptr);
	point = NSClipViewConstrainScrollPoint(self, nullptr, point);
	bounds.x = point.x;
	bounds.y = point.y;
	NSViewSetBounds(self, nullptr, bounds);
	id parent = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found != g_views.end()) parent = found->second.superview;
	}
	if (parent != nullptr) PositionScrollViewScrollbars(parent);
}

DarlingObjcRect NSClipViewDocumentVisibleRect(id self, SEL)
{
	const DarlingObjcRect bounds = NSViewBounds(self, nullptr);
	const id document_view = NSClipViewDocumentView(self, nullptr);
	if (document_view == nullptr) return {};
	const ViewCoordinateOrigin clip_origin = NSViewOriginInWindow(self);
	const ViewCoordinateOrigin document_origin =
		NSViewOriginInWindow(document_view);
	if (!clip_origin.valid || !document_origin.valid ||
		clip_origin.window != document_origin.window ||
		document_origin.scale_x == 0.0 || document_origin.scale_y == 0.0)
		return {};
	const double world_x0 = clip_origin.scale_x * bounds.x +
		clip_origin.translate_x;
	const double world_x1 = clip_origin.scale_x * (bounds.x + bounds.width) +
		clip_origin.translate_x;
	const double world_y0 = clip_origin.scale_y * bounds.y +
		clip_origin.translate_y;
	const double world_y1 = clip_origin.scale_y * (bounds.y + bounds.height) +
		clip_origin.translate_y;
	const double local_x0 = (world_x0 - document_origin.translate_x) /
		document_origin.scale_x;
	const double local_x1 = (world_x1 - document_origin.translate_x) /
		document_origin.scale_x;
	const double local_y0 = (world_y0 - document_origin.translate_y) /
		document_origin.scale_y;
	const double local_y1 = (world_y1 - document_origin.translate_y) /
		document_origin.scale_y;
	const DarlingObjcRect document_bounds = NSViewBounds(document_view, nullptr);
	const double x0 = (std::max)(document_bounds.x,
		(std::min)(local_x0, local_x1));
	const double y0 = (std::max)(document_bounds.y,
		(std::min)(local_y0, local_y1));
	const double x1 = (std::min)(document_bounds.x + document_bounds.width,
		(std::max)(local_x0, local_x1));
	const double y1 = (std::min)(document_bounds.y + document_bounds.height,
		(std::max)(local_y0, local_y1));
	return {x0, y0, (std::max)(0.0, x1 - x0),
		(std::max)(0.0, y1 - y0)};
}

DarlingObjcRect NSClipViewDocumentRect(id self, SEL)
{
	const DarlingObjcRect bounds = NSViewBounds(self, nullptr);
	const id document_view = NSClipViewDocumentView(self, nullptr);
	if (document_view == nullptr)
		return {bounds.x, bounds.y, bounds.width, bounds.height};
	DarlingObjcRect frame = NSViewFrame(document_view, nullptr);
	frame.width = (std::max)(frame.width, bounds.width);
	frame.height = (std::max)(frame.height, bounds.height);
	return frame;
}

void NSViewScrollPoint(id self, SEL, DarlingObjcPoint point)
{
	id clip_view = nullptr;
	id document_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		id current = self;
		for (std::size_t depth = 0; current != nullptr && depth <= g_views.size();
			++depth) {
			const auto found = g_views.find(current);
			if (found == g_views.end()) break;
			if (IsViewKindOf(current, "NSClipView")) {
				clip_view = current;
				if (!found->second.subviews.empty())
					document_view = found->second.subviews.front();
				break;
			}
			current = found->second.superview;
		}
	}
	if (clip_view == nullptr || document_view == nullptr) return;
	const ViewCoordinateOrigin source = NSViewOriginInWindow(self);
	const ViewCoordinateOrigin document = NSViewOriginInWindow(document_view);
	if (!source.valid || !document.valid || source.window != document.window ||
		document.scale_x == 0.0 || document.scale_y == 0.0) return;
	point.x = (source.scale_x * point.x + source.translate_x -
		document.translate_x) / document.scale_x;
	point.y = (source.scale_y * point.y + source.translate_y -
		document.translate_y) / document.scale_y;
	NSClipViewScrollToPoint(clip_view, nullptr, point);
}

DarlingObjcRect NSViewVisibleRect(id self, SEL)
{
	const DarlingObjcRect bounds = NSViewBounds(self, nullptr);
	std::vector<id> clipping_ancestors;
	bool hidden = false;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		id current = self;
		std::size_t depth = 0;
		while (current != nullptr && depth++ <= g_views.size()) {
			const auto found = g_views.find(current);
			if (found == g_views.end()) break;
			hidden = hidden || found->second.hidden;
			if (current != self) clipping_ancestors.push_back(current);
			current = found->second.superview;
		}
	}
	if (hidden) return {};
	const ViewCoordinateOrigin source = NSViewOriginInWindow(self);
	if (!source.valid) return bounds;
	double left = bounds.x;
	double bottom = bounds.y;
	double right = bounds.x + bounds.width;
	double top = bounds.y + bounds.height;
	for (id ancestor : clipping_ancestors) {
		const ViewCoordinateOrigin ancestor_origin = NSViewOriginInWindow(ancestor);
		const DarlingObjcRect ancestor_bounds = NSViewBounds(ancestor, nullptr);
		if (!ancestor_origin.valid || ancestor_origin.window != source.window ||
			source.scale_x == 0.0 || source.scale_y == 0.0) return {};
		const double x0 = (ancestor_origin.scale_x * ancestor_bounds.x +
			ancestor_origin.translate_x - source.translate_x) / source.scale_x;
		const double x1 = (ancestor_origin.scale_x *
			(ancestor_bounds.x + ancestor_bounds.width) + ancestor_origin.translate_x -
			source.translate_x) / source.scale_x;
		const double y0 = (ancestor_origin.scale_y * ancestor_bounds.y +
			ancestor_origin.translate_y - source.translate_y) / source.scale_y;
		const double y1 = (ancestor_origin.scale_y *
			(ancestor_bounds.y + ancestor_bounds.height) + ancestor_origin.translate_y -
			source.translate_y) / source.scale_y;
		left = (std::max)(left, (std::min)(x0, x1));
		bottom = (std::max)(bottom, (std::min)(y0, y1));
		right = (std::min)(right, (std::max)(x0, x1));
		top = (std::min)(top, (std::max)(y0, y1));
	}
	return {left, bottom, (std::max)(0.0, right - left),
		(std::max)(0.0, top - bottom)};
}

bool NSViewScrollRectToVisible(id self, SEL, DarlingObjcRect rect)
{
	if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
		!std::isfinite(rect.width) || !std::isfinite(rect.height) ||
		rect.width < 0.0 || rect.height < 0.0) return false;
	id clip_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		id current = self;
		std::size_t depth = 0;
		while (current != nullptr && depth++ <= g_views.size()) {
			const auto found = g_views.find(current);
			if (found == g_views.end()) break;
			if (current != self && IsViewKindOf(current, "NSClipView")) {
				clip_view = current;
				break;
			}
			current = found->second.superview;
		}
	}
	if (clip_view == nullptr) return false;
	const ViewCoordinateOrigin source = NSViewOriginInWindow(self);
	const ViewCoordinateOrigin clip_origin = NSViewOriginInWindow(clip_view);
	if (!source.valid || !clip_origin.valid || source.window != clip_origin.window ||
		clip_origin.scale_x == 0.0 || clip_origin.scale_y == 0.0) return false;
	const double x0 = (source.scale_x * rect.x + source.translate_x -
		clip_origin.translate_x) / clip_origin.scale_x;
	const double x1 = (source.scale_x * (rect.x + rect.width) +
		source.translate_x - clip_origin.translate_x) / clip_origin.scale_x;
	const double y0 = (source.scale_y * rect.y + source.translate_y -
		clip_origin.translate_y) / clip_origin.scale_y;
	const double y1 = (source.scale_y * (rect.y + rect.height) +
		source.translate_y - clip_origin.translate_y) / clip_origin.scale_y;
	const double rect_left = (std::min)(x0, x1);
	const double rect_right = (std::max)(x0, x1);
	const double rect_bottom = (std::min)(y0, y1);
	const double rect_top = (std::max)(y0, y1);
	const DarlingObjcRect clip_bounds = NSViewBounds(clip_view, nullptr);
	DarlingObjcPoint target{clip_bounds.x, clip_bounds.y};
	if (rect_left < clip_bounds.x) target.x = rect_left;
	else if (rect_right > clip_bounds.x + clip_bounds.width)
		target.x = rect_right - clip_bounds.width;
	if (rect_bottom < clip_bounds.y) target.y = rect_bottom;
	else if (rect_top > clip_bounds.y + clip_bounds.height)
		target.y = rect_top - clip_bounds.height;
	if (target.x == clip_bounds.x && target.y == clip_bounds.y) return false;
	NSClipViewScrollToPoint(clip_view, nullptr, target);
	return true;
}

id NSScrollViewDocumentView(id self, SEL)
{
	const id clip_view = NSScrollViewContentView(self, nullptr);
	return clip_view == nullptr ? nullptr :
		NSClipViewDocumentView(clip_view, nullptr);
}

void NSScrollViewSetDocumentView(id self, SEL, id document_view)
{
	const id clip_view = NSScrollViewContentView(self, nullptr);
	if (clip_view != nullptr) {
		NSClipViewSetDocumentView(clip_view, nullptr, document_view);
		PositionScrollViewScrollbars(self);
	}
}

DarlingObjcRect NSScrollViewDocumentVisibleRect(id self, SEL)
{
	const id clip_view = NSScrollViewContentView(self, nullptr);
	return clip_view == nullptr ? DarlingObjcRect{} :
		NSClipViewDocumentVisibleRect(clip_view, nullptr);
}

void NSScrollViewReflectScrolledClipView(id self, SEL, id clip_view)
{
	if (clip_view == nullptr || NSScrollViewContentView(self, nullptr) == clip_view)
		PositionScrollViewScrollbars(self);
}

bool NSScrollViewHasVerticalScroller(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.has_vertical_scroller;
}

void NSScrollViewSetHasVerticalScroller(id self, SEL, bool value)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].has_vertical_scroller = value;
	}
	NSViewSetFrame(self, nullptr, NSViewFrame(self, nullptr));
}

bool NSScrollViewHasHorizontalScroller(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.has_horizontal_scroller;
}

void NSScrollViewSetHasHorizontalScroller(id self, SEL, bool value)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].has_horizontal_scroller = value;
	}
	NSViewSetFrame(self, nullptr, NSViewFrame(self, nullptr));
}

bool NSScrollViewAutohidesScrollers(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.autohides_scrollers;
}

void NSScrollViewSetAutohidesScrollers(id self, SEL, bool value)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].autohides_scrollers = value;
	}
	PositionScrollViewScrollbars(self);
}

void PositionScrollViewScrollbars(id view)
{
	if (!IsViewKindOf(view, "NSScrollView")) return;
	HWND parent = nullptr;
	HWND vertical = nullptr;
	HWND horizontal = nullptr;
	bool show_vertical = false;
	bool show_horizontal = false;
	bool autohides = false;
	bool hidden = false;
	id window_object = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto state = g_views.find(view);
		if (state == g_views.end()) return;
		window_object = state->second.window;
		vertical = state->second.vertical_scroller;
		horizontal = state->second.horizontal_scroller;
		show_vertical = state->second.has_vertical_scroller;
		show_horizontal = state->second.has_horizontal_scroller;
		autohides = state->second.autohides_scrollers;
		id ancestor = view;
		std::size_t depth = 0;
		while (ancestor != nullptr && depth++ <= g_views.size()) {
			const auto found = g_views.find(ancestor);
			if (found == g_views.end()) break;
			hidden = hidden || found->second.hidden;
			ancestor = found->second.superview;
		}
		const auto window = g_windows.find(window_object);
		if (window != g_windows.end()) parent = window->second.handle;
	}
	const auto destroy_scrollbar = [&](HWND& scrollbar) {
		if (scrollbar == nullptr) return;
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			g_scrollbar_controls.erase(scrollbar);
			const auto state = g_views.find(view);
			if (state != g_views.end()) {
				if (state->second.vertical_scroller == scrollbar)
					state->second.vertical_scroller = nullptr;
				if (state->second.horizontal_scroller == scrollbar)
					state->second.horizontal_scroller = nullptr;
			}
		}
		DestroyWindow(scrollbar);
		scrollbar = nullptr;
	};
	if (parent == nullptr || !show_vertical) destroy_scrollbar(vertical);
	if (parent == nullptr || !show_horizontal) destroy_scrollbar(horizontal);
	if (parent == nullptr) return;
	const auto create_scrollbar = [&](bool vertical_axis, HWND& scrollbar) {
		if (scrollbar != nullptr) return;
		scrollbar = CreateWindowExW(0, L"SCROLLBAR", L"",
			WS_CHILD | (vertical_axis ? SBS_VERT : SBS_HORZ),
			0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (scrollbar == nullptr) return;
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto state = g_views.find(view);
		if (state == g_views.end() || state->second.window != window_object) {
			DestroyWindow(scrollbar);
			scrollbar = nullptr;
			return;
		}
		HWND& stored = vertical_axis ? state->second.vertical_scroller :
			state->second.horizontal_scroller;
		if (stored != nullptr) {
			DestroyWindow(scrollbar);
			scrollbar = stored;
			return;
		}
		stored = scrollbar;
		g_scrollbar_controls[scrollbar] = view;
	};
	if (show_vertical) create_scrollbar(true, vertical);
	if (show_horizontal) create_scrollbar(false, horizontal);
	if ((!show_vertical || vertical == nullptr) &&
		(!show_horizontal || horizontal == nullptr)) return;
	RECT client{};
	if (!GetClientRect(parent, &client)) return;
	const DarlingObjcRect bounds = NSViewBounds(view, nullptr);
	const ViewCoordinateOrigin origin = NSViewOriginInWindow(view);
	if (!origin.valid) return;
	const int vertical_width = show_vertical ?
		(std::max)(8, GetSystemMetrics(SM_CXVSCROLL)) : 0;
	const int horizontal_height = show_horizontal ?
		(std::max)(8, GetSystemMetrics(SM_CYHSCROLL)) : 0;
	const auto place = [&](HWND scrollbar, double local_x, double local_y,
		double local_width, double local_height, bool is_vertical) {
		if (scrollbar == nullptr) return;
		const double x0 = origin.scale_x * local_x + origin.translate_x;
		const double x1 = origin.scale_x * (local_x + local_width) +
			origin.translate_x;
		const double y0 = origin.scale_y * local_y + origin.translate_y;
		const double y1 = origin.scale_y * (local_y + local_height) +
			origin.translate_y;
		const int x = ClampWindowCoordinate((std::min)(x0, x1), -16000, 16000);
		const int y = ClampWindowCoordinate(client.bottom - (std::max)(y0, y1),
			-16000, 16000);
		const int width = (std::max)(1, ClampWindowCoordinate(
			std::abs(x1 - x0), 1, 10000));
		const int height = (std::max)(1, ClampWindowCoordinate(
			std::abs(y1 - y0), 1, 10000));
		SetWindowPos(scrollbar, nullptr, x, y, width, height,
			SWP_NOZORDER | SWP_NOACTIVATE);
		const id clip = NSScrollViewContentView(view, nullptr);
		const id document = clip == nullptr ? nullptr :
			NSClipViewDocumentView(clip, nullptr);
		SCROLLINFO info{};
		info.cbSize = sizeof(info);
		info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		if (clip != nullptr && document != nullptr) {
			const DarlingObjcRect clip_bounds = NSViewBounds(clip, nullptr);
			const DarlingObjcRect document_frame = NSViewFrame(document, nullptr);
			const bool flipped = darling_objc_msgSend_bool0(document,
				sel_registerName("isFlipped"));
			const double content = is_vertical ? document_frame.height :
				document_frame.width;
			const double visible = is_vertical ? clip_bounds.height :
				clip_bounds.width;
			const double maximum_offset = (std::max)(0.0, content - visible);
			const double current_offset = is_vertical ?
				(flipped ? clip_bounds.y - document_frame.y :
					document_frame.y + maximum_offset - clip_bounds.y) :
				clip_bounds.x - document_frame.x;
			const int page = (std::max)(1, static_cast<int>(std::ceil(visible)));
			info.nMin = 0;
			info.nMax = (std::max)(page - 1,
				static_cast<int>(std::ceil(content)) - 1);
			info.nPage = static_cast<UINT>(page);
			info.nPos = static_cast<int>(std::clamp(std::round(current_offset),
				0.0, maximum_offset));
			ShowWindow(scrollbar, hidden || (autohides && maximum_offset <= 0.0) ?
				SW_HIDE : SW_SHOWNA);
		} else {
			info.nMin = 0;
			info.nMax = 0;
			info.nPage = 1;
			info.nPos = 0;
			ShowWindow(scrollbar, hidden || autohides ? SW_HIDE : SW_SHOWNA);
		}
		SetScrollInfo(scrollbar, SB_CTL, &info, TRUE);
	};
	if (show_vertical)
		place(vertical, bounds.x + bounds.width - vertical_width, bounds.y,
			vertical_width, bounds.height - horizontal_height, true);
	if (show_horizontal)
		place(horizontal, bounds.x, bounds.y, bounds.width - vertical_width,
			horizontal_height, false);
}

bool DispatchNativeSliderMessage(HWND, UINT message, WPARAM wparam,
	LPARAM lparam)
{
	if ((message != WM_VSCROLL && message != WM_HSCROLL) || lparam == 0)
		return false;
	const HWND control = reinterpret_cast<HWND>(lparam);
	id slider = nullptr;
	double minimum = 0.0;
	double maximum = 1.0;
	bool continuous = true;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_controls.find(control);
		if (found == g_controls.end() || !IsViewKindOf(found->second, "NSSlider"))
			return false;
		slider = found->second;
		const auto state = g_views.find(slider);
		if (state == g_views.end()) return true;
		minimum = state->second.slider_min_value;
		maximum = state->second.slider_max_value;
		continuous = state->second.slider_continuous;
	}
	const int code = LOWORD(wparam);
	if (code != SB_THUMBTRACK && code != SB_THUMBPOSITION &&
		code != SB_ENDSCROLL && code != SB_LINEUP && code != SB_LINEDOWN &&
		code != SB_PAGEUP && code != SB_PAGEDOWN && code != SB_TOP &&
		code != SB_BOTTOM) return true;
	const int position = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
	const double value = minimum + (maximum - minimum) *
		static_cast<double>(std::clamp(position, 0, 10000)) / 10000.0;
	bool dispatch_action = false;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto state = g_views.find(slider);
		if (state == g_views.end()) return true;
		state->second.slider_value = std::clamp(value, minimum, maximum);
		if (continuous) {
			dispatch_action = code != SB_ENDSCROLL;
		} else if (code == SB_THUMBTRACK) {
			state->second.slider_pending_change = true;
		} else if (code == SB_ENDSCROLL) {
			dispatch_action = state->second.slider_pending_change;
			state->second.slider_pending_change = false;
		} else {
			dispatch_action = true;
		}
	}
	if (dispatch_action) DispatchControlAction(control);
	return true;
}

bool DispatchNativeStepperMessage(HWND, UINT message, WPARAM, LPARAM lparam)
{
	if (message != WM_NOTIFY || lparam == 0) return false;
	auto* change = reinterpret_cast<NMUPDOWN*>(lparam);
	if (change->hdr.code != UDN_DELTAPOS) return false;
	id stepper = nullptr;
	double value = 0.0;
	double minimum = 0.0;
	double maximum = 0.0;
	double increment = 1.0;
	bool value_wraps = true;
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_controls.find(change->hdr.hwndFrom);
		if (found == g_controls.end() ||
			!IsViewKindOf(found->second, "NSStepper")) return false;
		stepper = found->second;
		const auto state = g_views.find(stepper);
		if (state == g_views.end()) return true;
		if (!state->second.stepper_autorepeat &&
			state->second.stepper_mouse_pressed) return true;
		value = state->second.stepper_value;
		minimum = state->second.stepper_min_value;
		maximum = state->second.stepper_max_value;
		increment = state->second.stepper_increment;
		value_wraps = state->second.stepper_value_wraps;
		const double requested = value +
			static_cast<double>(change->iDelta) * increment;
		const double next = value_wraps && requested > maximum ?
			minimum : value_wraps && requested < minimum ? maximum :
			std::clamp(requested, minimum, maximum);
		changed = next != value;
		state->second.stepper_value = next;
		value = state->second.stepper_value;
	}
	const double span = maximum - minimum;
	const double fraction = span > 0.0 ?
		std::clamp((value - minimum) / span, 0.0, 1.0) : 0.0;
	SendMessageW(change->hdr.hwndFrom, UDM_SETPOS32, 0,
		static_cast<LPARAM>(std::lround(fraction * 10000.0)));
	if (changed) DispatchControlAction(change->hdr.hwndFrom);
	return true;
}

void StepNSStepper(HWND control, int delta)
{
	bool changed = false;
	double value = 0.0;
	double minimum = 0.0;
	double maximum = 0.0;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_controls.find(control);
		if (found == g_controls.end() ||
			!IsViewKindOf(found->second, "NSStepper")) return;
		const auto state = g_views.find(found->second);
		if (state == g_views.end()) return;
		value = state->second.stepper_value;
		minimum = state->second.stepper_min_value;
		maximum = state->second.stepper_max_value;
		const double requested = value + static_cast<double>(delta) *
			state->second.stepper_increment;
		const double next = state->second.stepper_value_wraps &&
			requested > maximum ? minimum :
			state->second.stepper_value_wraps && requested < minimum ? maximum :
			std::clamp(requested, minimum, maximum);
		changed = next != value;
		state->second.stepper_value = next;
		value = next;
	}
	const double span = maximum - minimum;
	const double fraction = span > 0.0 ?
		std::clamp((value - minimum) / span, 0.0, 1.0) : 0.0;
	SendMessageW(control, UDM_SETPOS32, 0,
		static_cast<LPARAM>(std::lround(fraction * 10000.0)));
	if (changed) DispatchControlAction(control);
}

bool DispatchScrollBarMessage(HWND, UINT message, WPARAM wparam, LPARAM lparam)
{
	if ((message != WM_VSCROLL && message != WM_HSCROLL) || lparam == 0)
		return false;
	const HWND scrollbar = reinterpret_cast<HWND>(lparam);
	id view = nullptr;
	bool vertical = false;
	id clip = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto control = g_scrollbar_controls.find(scrollbar);
		if (control == g_scrollbar_controls.end()) return false;
		view = control->second;
		const auto state = g_views.find(view);
		if (state == g_views.end()) return true;
		vertical = state->second.vertical_scroller == scrollbar;
		clip = state->second.clip_view;
	}
	const id document = clip == nullptr ? nullptr :
		NSClipViewDocumentView(clip, nullptr);
	if (document == nullptr) return true;
	DarlingObjcRect bounds = NSViewBounds(clip, nullptr);
	const DarlingObjcRect frame = NSViewFrame(document, nullptr);
	const bool flipped = darling_objc_msgSend_bool0(document,
		sel_registerName("isFlipped"));
	const double content = vertical ? frame.height : frame.width;
	const double visible = vertical ? bounds.height : bounds.width;
	const double maximum_offset = (std::max)(0.0, content - visible);
	double offset = vertical ?
		(flipped ? bounds.y - frame.y : frame.y + maximum_offset - bounds.y) :
		bounds.x - frame.x;
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_TRACKPOS;
	GetScrollInfo(scrollbar, SB_CTL, &info);
	const int code = LOWORD(wparam);
	const double line = 40.0;
	const double page = (std::max)(line, visible * 0.9);
	switch (code) {
	case SB_LINEUP:
	case SB_PAGEUP:
		if (vertical && !flipped) offset += code == SB_LINEUP ? line : page;
		else offset -= code == SB_LINEUP ? line : page;
		break;
	case SB_LINEDOWN:
	case SB_PAGEDOWN:
		if (vertical && !flipped) offset -= code == SB_LINEDOWN ? line : page;
		else offset += code == SB_LINEDOWN ? line : page;
		break;
	case SB_TOP:
		offset = vertical && !flipped ? maximum_offset : 0.0;
		break;
	case SB_BOTTOM:
		offset = vertical && !flipped ? 0.0 : maximum_offset;
		break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		offset = info.nTrackPos;
		break;
	case SB_ENDSCROLL:
		return true;
	default:
		return true;
	}
	offset = std::clamp(offset, 0.0, maximum_offset);
	if (vertical) bounds.y = flipped ? frame.y + offset :
		frame.y + maximum_offset - offset;
	else bounds.x = frame.x + offset;
	NSClipViewScrollToPoint(clip, nullptr,
		DarlingObjcPoint{bounds.x, bounds.y});
	PositionScrollViewScrollbars(view);
	return true;
}

id NSViewWindow(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? nullptr : found->second.window;
}

id NSViewSuperview(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? nullptr : found->second.superview;
}

id NSViewEnclosingScrollView(id self, SEL)
{
	id scroll_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto initial = g_views.find(self);
		id current = initial == g_views.end() ? nullptr :
			initial->second.superview;
		std::size_t depth = 0;
		while (current != nullptr && depth++ <= g_views.size()) {
			const auto found = g_views.find(current);
			if (found == g_views.end()) break;
			if (IsViewKindOf(current, "NSScrollView")) {
				scroll_view = current;
				break;
			}
			current = found->second.superview;
		}
	}
	return scroll_view == nullptr ? nullptr : objc_autorelease(
		objc_retain(scroll_view));
}

id NSViewNextResponder(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	if (found == g_views.end()) return nullptr;
	return found->second.superview != nullptr ? found->second.superview :
		found->second.window;
}

id NSViewNextKeyView(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? nullptr : found->second.next_key_view;
}

void NSViewSetNextKeyView(id self, SEL, id next_view)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	if (g_views.find(self) == g_views.end()) return;
	g_views[self].next_key_view = next_view;
}

id NSViewSubviews(id self, SEL)
{
	std::vector<id> children;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return objc_autorelease(
			CreateNSArray(objc_getClass("NSArray"), nullptr, 0));
		children = found->second.subviews;
	}
	return objc_autorelease(CreateNSArray(objc_getClass("NSArray"),
		children.data(), children.size()));
}

bool NSViewIsDescendantOf(id self, SEL, id ancestor)
{
	if (self == nullptr || ancestor == nullptr || self == ancestor) return false;
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	id current = self;
	for (std::size_t depth = 0; current != nullptr && depth <= g_views.size();
		++depth) {
		auto found = g_views.find(current);
		current = found == g_views.end() ? nullptr : found->second.superview;
		if (current == ancestor) return true;
	}
	return false;
}

void NSViewRemoveFromSuperview(id self, SEL)
{
	bool release_parent_ownership = false;
	HWND focus_handle = nullptr;
	HiddenStateSnapshot before;
	HiddenStateSnapshot after;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		auto view = g_views.find(self);
		if (view == g_views.end()) return;
		const id window_object = view->second.window;
		if (window_object != nullptr) {
			auto window = g_windows.find(window_object);
			if (window != g_windows.end() && IsInViewSubtreeLocked(
				window->second.first_responder, self)) {
				window->second.first_responder = window_object;
				focus_handle = window->second.handle;
			}
		}
		const id parent_object = view->second.superview;
		if (parent_object != nullptr) {
			before.reserve(g_views.size());
			after.reserve(g_views.size());
			CaptureHiddenSubtreeLocked(self, before);
			auto parent = g_views.find(parent_object);
			if (parent != g_views.end()) {
				auto& siblings = parent->second.subviews;
				auto child = std::find(siblings.begin(), siblings.end(), self);
				if (child != siblings.end()) {
					siblings.erase(child);
					release_parent_ownership = true;
				}
			}
			view->second.superview = nullptr;
			CaptureHiddenSubtreeLocked(self, after);
		}
	}
	if (focus_handle != nullptr) SetFocus(focus_handle);
	InvalidateAppKitView(self);
	DetachViewTree(self);
	NotifyHiddenStateChanges(before, after);
	if (release_parent_ownership) objc_release(self);
}

void NSViewSetFrame(id self, SEL, DarlingObjcRect frame)
{
	id clip_view = nullptr;
	double old_bounds_width = 0.0;
	double old_bounds_height = 0.0;
	double new_bounds_width = 0.0;
	double new_bounds_height = 0.0;
	bool has_vertical_scroller = false;
	bool has_horizontal_scroller = false;
	bool autohides_scrollers = false;
	id document_view = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		old_bounds_width = state.bounds.width;
		old_bounds_height = state.bounds.height;
		state.frame = frame;
		clip_view = state.clip_view;
		has_vertical_scroller = state.has_vertical_scroller;
		has_horizontal_scroller = state.has_horizontal_scroller;
		autohides_scrollers = state.autohides_scrollers;
		if (clip_view != nullptr) {
			const auto clip = g_views.find(clip_view);
			if (clip != g_views.end() && !clip->second.subviews.empty())
				document_view = clip->second.subviews.front();
		}
		if (!state.has_custom_bounds) {
			state.bounds.width = frame.width;
			state.bounds.height = frame.height;
		}
		new_bounds_width = state.bounds.width;
		new_bounds_height = state.bounds.height;
	}
	if (old_bounds_width != new_bounds_width ||
		old_bounds_height != new_bounds_height)
		ResizeAppKitSubviews(self, old_bounds_width, old_bounds_height,
			new_bounds_width, new_bounds_height);
	if (clip_view != nullptr) {
		if (autohides_scrollers && document_view != nullptr) {
			const DarlingObjcRect document_frame = NSViewFrame(document_view, nullptr);
			const double vertical_width = static_cast<double>((std::max)(8,
				GetSystemMetrics(SM_CXVSCROLL)));
			const double horizontal_height = static_cast<double>((std::max)(8,
				GetSystemMetrics(SM_CYHSCROLL)));
			for (unsigned iteration = 0; iteration < 2; ++iteration) {
				if (has_vertical_scroller)
					has_vertical_scroller = document_frame.height > frame.height -
					(has_horizontal_scroller ? horizontal_height : 0.0);
				if (has_horizontal_scroller)
					has_horizontal_scroller = document_frame.width > frame.width -
					(has_vertical_scroller ? vertical_width : 0.0);
			}
		}
		const double vertical_width = has_vertical_scroller ?
			static_cast<double>((std::max)(8, GetSystemMetrics(SM_CXVSCROLL))) : 0.0;
		const double horizontal_height = has_horizontal_scroller ?
			static_cast<double>((std::max)(8, GetSystemMetrics(SM_CYHSCROLL))) : 0.0;
		NSViewSetFrame(clip_view, nullptr,
			DarlingObjcRect{0.0, 0.0,
				(std::max)(0.0, frame.width - vertical_width),
				(std::max)(0.0, frame.height - horizontal_height)});
	}
	RepositionViewTree(self);
	RefreshAncestorScrollView(self);
	InvalidateAppKitView(self);
}

void NSViewAddSubview(id self, SEL, id child)
{
	if (child == nullptr || self == child) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		id current = self;
		for (std::size_t depth = 0; current != nullptr && depth <= g_views.size();
			++depth) {
			if (current == child) return;
			auto found = g_views.find(current);
			current = found == g_views.end() ? nullptr : found->second.superview;
		}
	}
	objc_retain(child);
	NSViewRemoveFromSuperview(child, nullptr);
	id window = nullptr;
	bool attached = false;
	HiddenStateSnapshot before;
	HiddenStateSnapshot after;
	{
		try {
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			AppKitViewState& parent = g_views[self];
			AppKitViewState& child_state = g_views[child];
			before.reserve(g_views.size());
			after.reserve(g_views.size());
			CaptureHiddenSubtreeLocked(child, before);
			parent.subviews.push_back(child);
			child_state.superview = self;
			window = parent.window;
			CaptureHiddenSubtreeLocked(child, after);
			attached = true;
		} catch (...) {
			attached = false;
		}
	}
	if (!attached) { objc_release(child); return; }
	if (window != nullptr) AttachViewTree(child, window);
	NotifyHiddenStateChanges(before, after);
	InvalidateAppKitView(self);
}

id NSWindowContentView(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() ? nullptr : found->second.content_view;
}

void NSWindowSetContentView(id self, SEL, id view)
{
	if (view != nullptr) objc_retain(view);
	if (view != nullptr) NSViewRemoveFromSuperview(view, nullptr);
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitWindowState& window = g_windows[self];
		previous = window.content_view;
		window.content_view = view;
	}
	if (previous != nullptr && previous != view) DetachViewTree(previous);
	if (previous != nullptr) objc_release(previous);
	if (view != nullptr) AttachViewTree(view, self);
	InvalidateAppKitWindow(self);
}

void NSButtonSetTitle(id self, SEL, id title)
{
	if (title != nullptr) objc_retain(title);
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		previous = g_views[self].title;
		g_views[self].title = title;
	}
	if (previous != nullptr) objc_release(previous);
	PositionNativeControl(self);
}

id NSPopUpButtonCreateMenuItem(id title)
{
	const Class cls = objc_getClass("NSMenuItem");
	if (cls == nullptr) return nullptr;
	id item = class_createInstance(cls, 0);
	if (item == nullptr) return nullptr;
	if (NSMenuItemInitWithTitle(item, nullptr, title, nullptr, nullptr) == nullptr) {
		objc_release(item);
		return nullptr;
	}
	return item;
}

void NSPopUpButtonAddItemWithTitle(id self, SEL, id title)
{
	const char* utf8 = NSStringUTF8String(title, nullptr);
	if (utf8 == nullptr) return;
	std::wstring item = Utf8ToWide(utf8);
	id item_object = NSPopUpButtonCreateMenuItem(title);
	if (item_object == nullptr) return;
	try {
		std::lock_guard lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.popup_titles.reserve(state.popup_titles.size() + 1);
		state.popup_items.reserve(state.popup_items.size() + 1);
		const auto [owner, inserted] = g_popup_item_owners.emplace(item_object, self);
		(void)owner;
		if (!inserted) throw std::runtime_error("duplicate popup menu item");
		state.popup_titles.push_back(std::move(item));
		state.popup_items.push_back(item_object);
		if (state.popup_selected_index < 0) state.popup_selected_index = 0;
	} catch (...) {
		{
			std::lock_guard lock(g_windows_mutex);
			g_popup_item_owners.erase(item_object);
		}
		objc_release(item_object);
		return;
	}
	PositionNativeControl(self);
}

void NSPopUpButtonInsertItemWithTitleAtIndex(id self, SEL, id title,
	std::int64_t index)
{
	const char* utf8 = NSStringUTF8String(title, nullptr);
	if (utf8 == nullptr || index < 0) return;
	std::wstring item = Utf8ToWide(utf8);
	id item_object = NSPopUpButtonCreateMenuItem(title);
	if (item_object == nullptr) return;
	try {
		std::lock_guard lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		const auto position = static_cast<std::size_t>(std::min<std::uint64_t>(
			static_cast<std::uint64_t>(index), state.popup_titles.size()));
		state.popup_titles.reserve(state.popup_titles.size() + 1);
		state.popup_items.reserve(state.popup_items.size() + 1);
		const auto [owner, inserted] = g_popup_item_owners.emplace(item_object, self);
		(void)owner;
		if (!inserted) throw std::runtime_error("duplicate popup menu item");
		state.popup_titles.insert(state.popup_titles.begin() + position,
			std::move(item));
		state.popup_items.insert(state.popup_items.begin() + position, item_object);
		if (state.popup_selected_index < 0) state.popup_selected_index = 0;
		else if (state.popup_selected_index >= index) ++state.popup_selected_index;
	} catch (...) {
		{
			std::lock_guard lock(g_windows_mutex);
			g_popup_item_owners.erase(item_object);
		}
		objc_release(item_object);
		return;
	}
	PositionNativeControl(self);
}

void NSPopUpButtonRemoveAllItems(id self, SEL)
{
	std::vector<id> removed;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		found->second.popup_titles.clear();
		removed.swap(found->second.popup_items);
		for (id item : removed) g_popup_item_owners.erase(item);
		found->second.popup_selected_index = -1;
	}
	for (id item : removed) objc_release(item);
	PositionNativeControl(self);
}

void NSPopUpButtonRemoveItemAtIndex(id self, SEL, std::int64_t index)
{
	id removed = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end() || index < 0 ||
			static_cast<std::uint64_t>(index) >= found->second.popup_titles.size())
			return;
		auto& state = found->second;
		state.popup_titles.erase(state.popup_titles.begin() + index);
		removed = state.popup_items[static_cast<std::size_t>(index)];
		state.popup_items.erase(state.popup_items.begin() + index);
		g_popup_item_owners.erase(removed);
		if (state.popup_titles.empty()) state.popup_selected_index = -1;
		else if (state.popup_selected_index == index)
			state.popup_selected_index = std::min<std::int64_t>(index,
				static_cast<std::int64_t>(state.popup_titles.size()) - 1);
		else if (state.popup_selected_index > index) --state.popup_selected_index;
	}
	if (removed != nullptr) objc_release(removed);
	PositionNativeControl(self);
}

void NSPopUpButtonRemoveItemWithTitle(id self, SEL, id title)
{
	const char* utf8 = NSStringUTF8String(title, nullptr);
	if (utf8 == nullptr) return;
	const std::wstring requested = Utf8ToWide(utf8);
	std::int64_t index = -1;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		const auto item = std::find(found->second.popup_titles.begin(),
			found->second.popup_titles.end(), requested);
		if (item == found->second.popup_titles.end()) return;
		index = static_cast<std::int64_t>(item - found->second.popup_titles.begin());
	}
	NSPopUpButtonRemoveItemAtIndex(self, nullptr, index);
}

std::int64_t NSPopUpButtonNumberOfItems(id self, SEL)
{
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0 : found->second.popup_titles.size();
}

std::int64_t NSPopUpButtonIndexOfSelectedItem(id self, SEL)
{
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? -1 : found->second.popup_selected_index;
}

id NSPopUpButtonItemTitleAtIndex(id self, SEL, std::int64_t index)
{
	std::wstring title;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end() || index < 0 ||
			static_cast<std::uint64_t>(index) >= found->second.popup_titles.size())
			return nullptr;
		title = found->second.popup_titles[static_cast<std::size_t>(index)];
	}
	std::string utf8;
	if (!ConvertWideToUtf8(title, utf8)) return nullptr;
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		utf8.c_str()));
}

id NSPopUpButtonTitleOfSelectedItem(id self, SEL)
{
	return NSPopUpButtonItemTitleAtIndex(self, nullptr,
		NSPopUpButtonIndexOfSelectedItem(self, nullptr));
}

id NSPopUpButtonItemAtIndex(id self, SEL, std::int64_t index)
{
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_views.find(self);
	if (found == g_views.end() || index < 0 ||
		static_cast<std::uint64_t>(index) >= found->second.popup_items.size())
		return nullptr;
	return objc_retainAutorelease(
		found->second.popup_items[static_cast<std::size_t>(index)]);
}

id NSPopUpButtonSelectedItem(id self, SEL)
{
	return NSPopUpButtonItemAtIndex(self, nullptr,
		NSPopUpButtonIndexOfSelectedItem(self, nullptr));
}

id NSPopUpButtonItemArray(id self, SEL)
{
	const std::int64_t count = NSPopUpButtonNumberOfItems(self, nullptr);
	if (count < 0 || count > 65536) return nullptr;
	std::vector<id> items;
	try {
		items.reserve(static_cast<std::size_t>(count));
		for (std::int64_t index = 0; index < count; ++index) {
			id item = NSPopUpButtonItemAtIndex(self, nullptr, index);
			if (item == nullptr) return nullptr;
			items.push_back(item);
		}
	} catch (...) { return nullptr; }
	return objc_autorelease(CreateNSArray(objc_getClass("NSArray"), items.data(),
		items.size()));
}

id NSPopUpButtonItemTitles(id self, SEL)
{
	const std::int64_t count = NSPopUpButtonNumberOfItems(self, nullptr);
	if (count < 0 || count > 65536) return nullptr;
	std::vector<id> titles;
	try {
		titles.reserve(static_cast<std::size_t>(count));
		for (std::int64_t index = 0; index < count; ++index) {
			id title = NSPopUpButtonItemTitleAtIndex(self, nullptr, index);
			if (title == nullptr) return nullptr;
			titles.push_back(title);
		}
	} catch (...) { return nullptr; }
	return objc_autorelease(CreateNSArray(objc_getClass("NSArray"), titles.data(),
		titles.size()));
}

std::int64_t NSPopUpButtonIndexOfItemWithTitle(id self, SEL, id title)
{
	const char* utf8 = NSStringUTF8String(title, nullptr);
	if (utf8 == nullptr) return -1;
	const std::wstring requested = Utf8ToWide(utf8);
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_views.find(self);
	if (found == g_views.end()) return -1;
	const auto item = std::find(found->second.popup_titles.begin(),
		found->second.popup_titles.end(), requested);
	return item == found->second.popup_titles.end() ? -1 :
		static_cast<std::int64_t>(item - found->second.popup_titles.begin());
}

std::int64_t NSPopUpButtonIndexOfItem(id self, SEL, id item)
{
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found != g_views.end()) {
			const auto at = std::find(found->second.popup_items.begin(),
				found->second.popup_items.end(), item);
			if (at != found->second.popup_items.end())
				return static_cast<std::int64_t>(at -
					found->second.popup_items.begin());
		}
	}
	id title = NSMenuItemTitle(item, nullptr);
	return title == nullptr ? -1 : NSPopUpButtonIndexOfItemWithTitle(self,
		nullptr, title);
}

id NSPopUpButtonItemWithTitle(id self, SEL, id title)
{
	const std::int64_t index = NSPopUpButtonIndexOfItemWithTitle(self, nullptr,
		title);
	return NSPopUpButtonItemAtIndex(self, nullptr, index);
}

id NSPopUpButtonLastItem(id self, SEL)
{
	return NSPopUpButtonItemAtIndex(self, nullptr,
		NSPopUpButtonNumberOfItems(self, nullptr) - 1);
}

void NSPopUpButtonSelectItemWithTitle(id self, SEL, id title);

void NSPopUpButtonSelectItem(id self, SEL, id item)
{
	bool selected = false;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found != g_views.end()) {
			const auto at = std::find(found->second.popup_items.begin(),
				found->second.popup_items.end(), item);
			if (at != found->second.popup_items.end()) {
				found->second.popup_selected_index = static_cast<std::int64_t>(
					at - found->second.popup_items.begin());
				selected = true;
			}
		}
	}
	if (selected) {
		PositionNativeControl(self);
		return;
	}
	id title = NSMenuItemTitle(item, nullptr);
	if (title == nullptr) return;
	NSPopUpButtonSelectItemWithTitle(self, nullptr, title);
}

void NSPopUpButtonSelectItemAtIndex(id self, SEL, std::int64_t index)
{
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end() || index < 0 ||
			static_cast<std::uint64_t>(index) >= found->second.popup_titles.size())
			return;
		found->second.popup_selected_index = index;
	}
	PositionNativeControl(self);
}

void NSPopUpButtonSelectItemWithTitle(id self, SEL, id title)
{
	const char* utf8 = NSStringUTF8String(title, nullptr);
	if (utf8 == nullptr) return;
	const std::wstring requested = Utf8ToWide(utf8);
	std::int64_t index = -1;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		const auto item = std::find(found->second.popup_titles.begin(),
			found->second.popup_titles.end(), requested);
		if (item == found->second.popup_titles.end()) return;
		index = static_cast<std::int64_t>(item - found->second.popup_titles.begin());
		found->second.popup_selected_index = index;
	}
	PositionNativeControl(self);
}

void NSPopUpButtonSetTitle(id self, SEL, id title)
{
	const std::int64_t selected = NSPopUpButtonIndexOfSelectedItem(self, nullptr);
	if (selected < 0) {
		NSPopUpButtonAddItemWithTitle(self, nullptr, title);
		return;
	}
	id item = NSPopUpButtonItemAtIndex(self, nullptr, selected);
	if (item == nullptr) return;
	NSMenuItemSetTitle(item, sel_registerName("setTitle:"), title);
}

id NSPopUpButtonInitWithFramePullsDown(id self, SEL, DarlingObjcRect frame,
	bool pulls_down)
{
	id result = NSViewInitWithFrame(self, sel_registerName("initWithFrame:"),
		frame);
	if (result == nullptr) return nullptr;
	std::lock_guard lock(g_windows_mutex);
	g_views[result].popup_pulls_down = pulls_down;
	return result;
}

bool NSPopUpButtonPullsDown(id self, SEL)
{
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.popup_pulls_down;
}

void NSPopUpButtonSetPullsDown(id self, SEL, bool value)
{
	std::lock_guard lock(g_windows_mutex);
	g_views[self].popup_pulls_down = value;
}

void NSPopUpButtonSyncItemTitle(id item, id title)
{
	std::string utf8_title;
	if (title != nullptr) {
		const char* utf8 = NSStringUTF8String(title, nullptr);
		if (utf8 == nullptr) return;
		utf8_title.assign(utf8);
	}
	const std::wstring wide_title = Utf8ToWide(utf8_title.c_str());
	id owner = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto owner_entry = g_popup_item_owners.find(item);
		if (owner_entry == g_popup_item_owners.end()) return;
		owner = owner_entry->second;
		const auto state = g_views.find(owner);
		if (state == g_views.end()) return;
		const auto entry = std::find(state->second.popup_items.begin(),
			state->second.popup_items.end(), item);
		if (entry == state->second.popup_items.end()) return;
		state->second.popup_titles[static_cast<std::size_t>(
			entry - state->second.popup_items.begin())] = wide_title;
	}
	PositionNativeControl(owner);
}

void NSButtonSetTarget(id self, SEL, id target)
{
	if (target != nullptr) objc_retain(target);
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		previous = g_views[self].target;
		g_views[self].target = target;
	}
	if (previous != nullptr) objc_release(previous);
}

void NSButtonSetAction(id self, SEL, SEL action)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	g_views[self].action = action;
}

id NSButtonKeyEquivalent(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.key_equivalent == nullptr ?
		nullptr : objc_retainAutorelease(found->second.key_equivalent);
}

void NSButtonSetKeyEquivalent(id self, SEL, id key_equivalent)
{
	if (key_equivalent != nullptr) objc_retain(key_equivalent);
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) {
			if (key_equivalent != nullptr) objc_release(key_equivalent);
			return;
		}
		previous = std::exchange(found->second.key_equivalent, key_equivalent);
	}
	if (previous != nullptr) objc_release(previous);
}

std::uint64_t NSButtonKeyEquivalentModifierMask(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? (1ull << 20) :
		found->second.key_equivalent_modifier_mask;
}

void NSButtonSetKeyEquivalentModifierMask(id self, SEL,
	std::uint64_t modifier_mask)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	if (found != g_views.end()) found->second.key_equivalent_modifier_mask =
		modifier_mask;
}

id NSButtonInitWithCoder(id self, SEL, id coder)
{
	if (self == nullptr || coder == nullptr ||
		NSViewInitWithCoder(self, nullptr, coder) == nullptr) return nullptr;
	auto make_key = [](const char* name) {
		return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
			name));
	};
	id key = make_key("NSKeyEquiv");
	if (key != nullptr && g_active_keyed_archive_decoder != nullptr &&
		g_active_keyed_archive_decoder->ContainsKey(key)) {
		id equivalent = g_active_keyed_archive_decoder->DecodeForKey(key);
		if (equivalent != nullptr) {
			NSButtonSetKeyEquivalent(self, sel_registerName("setKeyEquivalent:"),
				equivalent);
			objc_release(equivalent);
		}
	}
	key = make_key("NSKeyEquivModMask");
	if (key != nullptr && g_active_keyed_archive_decoder != nullptr &&
		g_active_keyed_archive_decoder->ContainsKey(key))
		NSButtonSetKeyEquivalentModifierMask(self,
			sel_registerName("setKeyEquivalentModifierMask:"),
			static_cast<std::uint64_t>(NSKeyedUnarchiverDecodeIntForKey(coder,
				nullptr, key)));
	return self;
}

bool NSButtonPerformKeyEquivalent(id self, SEL, id event)
{
	if (event == nullptr || NSEventType(event, nullptr) != 10 ||
		!NSButtonIsEnabled(self, nullptr) ||
		NSViewIsHiddenOrHasHiddenAncestor(self, nullptr)) return false;
	id key_equivalent = NSButtonKeyEquivalent(self, nullptr);
	if (key_equivalent == nullptr) return false;
	const char* expected = NSStringUTF8String(key_equivalent, nullptr);
	id characters = NSEventCharactersIgnoringModifiers(event, nullptr);
	const char* actual = NSStringUTF8String(characters, nullptr);
	const std::uint64_t required = NSButtonKeyEquivalentModifierMask(self,
		nullptr) & ((1ull << 17) | (1ull << 18) | (1ull << 19) | (1ull << 20));
	const std::uint64_t actual_flags = NSEventModifierFlags(event, nullptr);
	// Windows Ctrl is commonly used for macOS Command shortcuts; the Win key
	// remains available as the literal Command equivalent.
	const bool required_command = (required & (1ull << 20)) != 0;
	const bool required_control = (required & (1ull << 18)) != 0;
	const bool actual_command = (actual_flags & (1ull << 20)) != 0;
	const bool actual_control = (actual_flags & (1ull << 18)) != 0;
	const bool command_control_match = required_command && required_control ?
		(actual_command && actual_control) :
		(required_command ? (actual_command || actual_control) :
		(required_control ? (actual_control && !actual_command) :
		(!actual_command && !actual_control)));
	const bool other_modifiers_match =
		(required & ((1ull << 17) | (1ull << 19))) ==
		(actual_flags & ((1ull << 17) | (1ull << 19)));
	const bool matches = expected != nullptr && expected[0] != '\0' &&
		actual != nullptr &&
		std::strcmp(expected, actual) == 0 && command_control_match &&
		other_modifiers_match;
	if (characters != nullptr) objc_release(characters);
	objc_release(key_equivalent);
	if (!matches) return false;
	HWND control = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found != g_views.end() && found->second.enabled)
			control = found->second.control;
	}
	if (control == nullptr) return false;
	DispatchControlAction(control);
	return true;
}

void NSButtonPerformClick(id self, SEL, id)
{
	if (self == nullptr || !NSButtonIsEnabled(self, nullptr) ||
		NSViewIsHiddenOrHasHiddenAncestor(self, nullptr)) return;
	HWND control = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found != g_views.end() && found->second.enabled)
			control = found->second.control;
	}
	if (control != nullptr) DispatchControlAction(control);
}

std::uint64_t NSButtonButtonType(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0 : found->second.button_type;
}

void NSButtonSetButtonType(id self, SEL, std::uint64_t type)
{
	if (type > 9) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		found->second.button_type = type;
	}
	PositionNativeControl(self);
}

std::int64_t NSButtonState(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0 : found->second.button_state;
}

void NSButtonSetState(id self, SEL, std::int64_t state)
{
	state = std::clamp<std::int64_t>(state, -1, 1);
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		found->second.button_state = state;
	}
	PositionNativeControl(self);
}

bool NSButtonAllowsMixedState(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.button_allows_mixed_state;
}

void NSButtonSetAllowsMixedState(id self, SEL, bool allows_mixed_state)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		found->second.button_allows_mixed_state = allows_mixed_state;
		if (!allows_mixed_state && found->second.button_state < 0)
			found->second.button_state = 0;
	}
	PositionNativeControl(self);
}

id NSButtonCreateWithTitleTargetAction(Class cls, SEL selector, id title,
	id target, SEL action)
{
	if (cls == nullptr) return nullptr;
	id button = class_createInstance(cls, 0);
	if (button == nullptr) return nullptr;
	button = NSViewInitWithFrame(button, sel_registerName("initWithFrame:"),
		DarlingObjcRect{0.0, 0.0, 80.0, 24.0});
	if (button == nullptr) return nullptr;
	NSButtonSetTitle(button, sel_registerName("setTitle:"), title);
	NSButtonSetTarget(button, sel_registerName("setTarget:"), target);
	NSButtonSetAction(button, sel_registerName("setAction:"), action);
	(void)selector;
	return objc_autorelease(button);
}

id NSButtonTitle(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? nullptr : found->second.title;
}

std::int64_t NSControlIntegerValue(id self, SEL);

double NSControlDoubleValue(id self, SEL)
{
	const id value = NSButtonTitle(self, nullptr);
	if (value == nullptr) return 0.0;
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	if (utf8 == nullptr) return 0.0;
	errno = 0;
	char* end = nullptr;
	const double parsed = std::strtod(utf8, &end);
	return end == utf8 ? 0.0 : parsed;
}

float NSControlFloatValue(id self, SEL)
{
	return static_cast<float>(NSControlDoubleValue(self, nullptr));
}

int NSControlIntValue(id self, SEL)
{
	const std::int64_t value = NSControlIntegerValue(self, nullptr);
	return static_cast<int>(std::clamp<std::int64_t>(value, INT_MIN, INT_MAX));
}

std::int64_t NSControlIntegerValue(id self, SEL)
{
	const id value = NSButtonTitle(self, nullptr);
	if (value == nullptr) return 0;
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	if (utf8 == nullptr) return 0;
	errno = 0;
	char* end = nullptr;
	const long long parsed = std::strtoll(utf8, &end, 10);
	return end == utf8 ? 0 : static_cast<std::int64_t>(parsed);
}

bool NSControlBoolValue(id self, SEL)
{
	const id value = NSButtonTitle(self, nullptr);
	if (value == nullptr) return false;
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	if (utf8 == nullptr) return false;
	while (*utf8 == ' ' || *utf8 == '\t' || *utf8 == '\r' || *utf8 == '\n')
		++utf8;
	if (*utf8 == '\0') return false;
	if (*utf8 == 'Y' || *utf8 == 'y' || *utf8 == 'T' || *utf8 == 't')
		return true;
	char* end = nullptr;
	const double parsed = std::strtod(utf8, &end);
	return end != utf8 && parsed != 0.0;
}

void NSControlSetStringFromNumber(id self, const char* text)
{
	id value = CreateDynamicNSString(objc_getClass("NSString"), text);
	if (value == nullptr) return;
	NSButtonSetTitle(self, sel_registerName("setStringValue:"), value);
	objc_release(value);
}

void NSControlSetDoubleValue(id self, SEL, double value)
{
	char text[64]{};
	const int length = std::snprintf(text, sizeof(text), "%.17g", value);
	if (length > 0 && static_cast<std::size_t>(length) < sizeof(text))
		NSControlSetStringFromNumber(self, text);
}

void NSControlSetFloatValue(id self, SEL, float value)
{
	char text[64]{};
	const int length = std::snprintf(text, sizeof(text), "%.9g",
		static_cast<double>(value));
	if (length > 0 && static_cast<std::size_t>(length) < sizeof(text))
		NSControlSetStringFromNumber(self, text);
}

void NSControlSetIntegerValue(id self, SEL, std::int64_t value)
{
	char text[64]{};
	const int length = std::snprintf(text, sizeof(text), "%lld",
		static_cast<long long>(value));
	if (length > 0 && static_cast<std::size_t>(length) < sizeof(text))
		NSControlSetStringFromNumber(self, text);
}

void NSControlSetIntValue(id self, SEL, int value)
{
	NSControlSetIntegerValue(self, nullptr, value);
}

void NSControlSetBoolValue(id self, SEL, bool value)
{
	NSControlSetStringFromNumber(self, value ? "YES" : "NO");
}

id NSTextFieldPlaceholderString(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? nullptr : found->second.placeholder;
}

void NSTextFieldSetPlaceholderString(id self, SEL, id placeholder)
{
	if (placeholder != nullptr) objc_retain(placeholder);
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		previous = state.placeholder;
		state.placeholder = placeholder;
	}
	if (previous != nullptr) objc_release(previous);
	PositionNativeControl(self);
}

void NSTextFieldSetEditable(id self, SEL, bool editable)
{
	HWND control = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.editable = editable;
		control = state.control;
	}
	if (control != nullptr)
		SendMessageW(control, EM_SETREADONLY, editable ? FALSE : TRUE, 0);
}

bool NSTextFieldIsEditable(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.editable;
}

id NSTextViewString(id self, SEL)
{
	const id value = NSButtonTitle(self, nullptr);
	return value != nullptr ? value : objc_autorelease(
		CreateDynamicNSString(objc_getClass("NSString"), ""));
}

void NSTextViewSetString(id self, SEL, id value)
{
	NSButtonSetTitle(self, sel_registerName("setString:"), value);
}

double NSProgressIndicatorMinValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0.0 : found->second.progress_min_value;
}

void NSProgressIndicatorSetMinValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.progress_min_value = value;
		if (state.progress_max_value < value) state.progress_max_value = value;
		state.progress_value = std::clamp(state.progress_value,
			state.progress_min_value, state.progress_max_value);
	}
	PositionNativeControl(self);
}

double NSProgressIndicatorMaxValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 100.0 : found->second.progress_max_value;
}

void NSProgressIndicatorSetMaxValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.progress_max_value = value;
		if (state.progress_min_value > value) state.progress_min_value = value;
		state.progress_value = std::clamp(state.progress_value,
			state.progress_min_value, state.progress_max_value);
	}
	PositionNativeControl(self);
}

double NSProgressIndicatorDoubleValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0.0 : found->second.progress_value;
}

void NSProgressIndicatorSetDoubleValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.progress_value = std::clamp(value, state.progress_min_value,
			state.progress_max_value);
	}
	PositionNativeControl(self);
}

void NSProgressIndicatorIncrementBy(id self, SEL, double delta)
{
	if (!std::isfinite(delta)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.progress_value = std::clamp(state.progress_value + delta,
			state.progress_min_value, state.progress_max_value);
	}
	PositionNativeControl(self);
}

bool NSProgressIndicatorIsIndeterminate(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.progress_indeterminate;
}

void NSProgressIndicatorSetIndeterminate(id self, SEL, bool value)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].progress_indeterminate = value;
	}
	PositionNativeControl(self);
}

std::uint64_t NSProgressIndicatorStyle(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0 : found->second.progress_style;
}

void NSProgressIndicatorSetStyle(id self, SEL, std::uint64_t style)
{
	if (style > 1) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].progress_style = style;
	}
	PositionNativeControl(self);
}

void NSProgressIndicatorStartAnimation(id self, SEL, id)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].progress_animating = true;
	}
	PositionNativeControl(self);
}

void NSProgressIndicatorStopAnimation(id self, SEL, id)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].progress_animating = false;
	}
	PositionNativeControl(self);
}

double NSSliderMinValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0.0 : found->second.slider_min_value;
}

void NSSliderSetMinValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.slider_min_value = value;
		if (state.slider_max_value < value) state.slider_max_value = value;
		state.slider_value = std::clamp(state.slider_value,
			state.slider_min_value, state.slider_max_value);
	}
	PositionNativeControl(self);
}

double NSSliderMaxValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 1.0 : found->second.slider_max_value;
}

void NSSliderSetMaxValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.slider_max_value = value;
		if (state.slider_min_value > value) state.slider_min_value = value;
		state.slider_value = std::clamp(state.slider_value,
			state.slider_min_value, state.slider_max_value);
	}
	PositionNativeControl(self);
}

double NSSliderDoubleValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0.0 : found->second.slider_value;
}

void NSSliderSetDoubleValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.slider_value = std::clamp(value, state.slider_min_value,
			state.slider_max_value);
	}
	PositionNativeControl(self);
}

id NSSliderStringValue(id self, SEL)
{
	char text[64]{};
	const int length = std::snprintf(text, sizeof(text), "%.17g",
		NSSliderDoubleValue(self, nullptr));
	if (length < 0 || static_cast<std::size_t>(length) >= sizeof(text))
		return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), "0"));
	return objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"), text));
}

void NSSliderSetStringValue(id self, SEL, id value)
{
	if (value == nullptr) return;
	const char* text = static_cast<const char*>(darling_objc_msgSend_pointer0(
		value, sel_registerName("UTF8String")));
	if (text == nullptr) return;
	char* end = nullptr;
	errno = 0;
	const double number = std::strtod(text, &end);
	if (end != text && std::isfinite(number))
		NSSliderSetDoubleValue(self, nullptr, number);
}

std::int64_t NSSliderIntegerValue(id self, SEL)
{
	const double value = NSSliderDoubleValue(self, nullptr);
	return static_cast<std::int64_t>(std::clamp(value,
		static_cast<double>(INT64_MIN), static_cast<double>(INT64_MAX)));
}

void NSSliderSetIntegerValue(id self, SEL, std::int64_t value)
{
	NSSliderSetDoubleValue(self, nullptr, static_cast<double>(value));
}

float NSSliderFloatValue(id self, SEL)
{
	return static_cast<float>(NSSliderDoubleValue(self, nullptr));
}

void NSSliderSetFloatValue(id self, SEL, float value)
{
	NSSliderSetDoubleValue(self, nullptr, static_cast<double>(value));
}

int NSSliderIntValue(id self, SEL)
{
	const auto value = NSSliderIntegerValue(self, nullptr);
	return static_cast<int>(std::clamp<std::int64_t>(value, INT_MIN, INT_MAX));
}

void NSSliderSetIntValue(id self, SEL, int value)
{
	NSSliderSetDoubleValue(self, nullptr, static_cast<double>(value));
}

double NSStepperMinValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0.0 : found->second.stepper_min_value;
}

void NSStepperSetMinValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.stepper_min_value = value;
		if (state.stepper_max_value < value) state.stepper_max_value = value;
		state.stepper_value = std::clamp(state.stepper_value,
			state.stepper_min_value, state.stepper_max_value);
	}
	PositionNativeControl(self);
}

double NSStepperMaxValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 59.0 : found->second.stepper_max_value;
}

void NSStepperSetMaxValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.stepper_max_value = value;
		if (state.stepper_min_value > value) state.stepper_min_value = value;
		state.stepper_value = std::clamp(state.stepper_value,
			state.stepper_min_value, state.stepper_max_value);
	}
	PositionNativeControl(self);
}

double NSStepperIncrement(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 1.0 : found->second.stepper_increment;
}

void NSStepperSetIncrement(id self, SEL, double value)
{
	if (!std::isfinite(value) || value <= 0.0) return;
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	g_views[self].stepper_increment = value;
}

bool NSStepperValueWraps(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.stepper_value_wraps;
}

void NSStepperSetValueWraps(id self, SEL, bool value)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	g_views[self].stepper_value_wraps = value;
}

bool NSStepperAutorepeat(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.stepper_autorepeat;
}

void NSStepperSetAutorepeat(id self, SEL, bool value)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	g_views[self].stepper_autorepeat = value;
}

double NSStepperDoubleValue(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() ? 0.0 : found->second.stepper_value;
}

void NSStepperSetDoubleValue(id self, SEL, double value)
{
	if (!std::isfinite(value)) return;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.stepper_value = std::clamp(value, state.stepper_min_value,
			state.stepper_max_value);
	}
	PositionNativeControl(self);
}

bool NSSliderIsVertical(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.slider_vertical;
}

void NSSliderSetVertical(id self, SEL, bool value)
{
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_views[self].slider_vertical = value;
	}
	PositionNativeControl(self);
}

bool NSSliderIsContinuous(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.slider_continuous;
}

void NSSliderSetContinuous(id self, SEL, bool value)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	g_views[self].slider_continuous = value;
}

DarlingObjcRange NSTextViewSelectedRange(id self, SEL)
{
	HWND control = nullptr;
	DarlingObjcRange selected{};
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return {};
		control = found->second.control;
		selected = found->second.selected_range;
	}
	if (control == nullptr) return selected;
	DWORD start = 0;
	DWORD end = 0;
	SendMessageW(control, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
		reinterpret_cast<LPARAM>(&end));
	selected = {start, end >= start ? end - start : 0};
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found != g_views.end()) found->second.selected_range = selected;
	}
	return selected;
}

void NSTextViewSetSelectedRange(id self, SEL, DarlingObjcRange selected)
{
	HWND control = nullptr;
	id string = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		control = found->second.control;
		string = found->second.title;
		if (string != nullptr) objc_retain(string);
	}
	std::uint64_t length = control != nullptr ? static_cast<std::uint64_t>(
		std::max(0, GetWindowTextLengthW(control))) :
		string != nullptr ? NSStringLength(string, nullptr) : 0;
	if (string != nullptr) objc_release(string);
	selected.location = std::min(selected.location, length);
	selected.length = std::min(selected.length, length - selected.location);
	selected.location = std::min<std::uint64_t>(selected.location, INT_MAX);
	selected.length = std::min<std::uint64_t>(selected.length,
		static_cast<std::uint64_t>(INT_MAX) - selected.location);
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_views.find(self);
		if (found == g_views.end()) return;
		found->second.selected_range = selected;
	}
	if (control != nullptr)
		SendMessageW(control, EM_SETSEL, static_cast<WPARAM>(selected.location),
			static_cast<LPARAM>(selected.location + selected.length));
}

void NSButtonSetEnabled(id self, SEL, bool enabled)
{
	HWND control = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		state.enabled = enabled;
		control = state.control;
	}
	if (control != nullptr) EnableWindow(control, enabled ? TRUE : FALSE);
}

bool NSButtonIsEnabled(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found == g_views.end() || found->second.enabled;
}

bool NSViewIsHidden(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_views.find(self);
	return found != g_views.end() && found->second.hidden;
}

bool NSViewIsHiddenOrHasHiddenAncestor(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	id current = self;
	for (std::size_t depth = 0; current != nullptr && depth <= g_views.size();
		++depth) {
		const auto found = g_views.find(current);
		if (found == g_views.end()) return false;
		if (found->second.hidden) return true;
		current = found->second.superview;
	}
	return false;
}

void NSViewSetHidden(id self, SEL, bool hidden)
{
	HiddenStateSnapshot before;
	HiddenStateSnapshot after;
	HWND focus_handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitViewState& state = g_views[self];
		before.reserve(g_views.size());
		after.reserve(g_views.size());
		CaptureHiddenSubtreeLocked(self, before);
		state.hidden = hidden;
		CaptureHiddenSubtreeLocked(self, after);
		if (hidden && state.window != nullptr) {
			auto window = g_windows.find(state.window);
			if (window != g_windows.end() && IsInViewSubtreeLocked(
				window->second.first_responder, self)) {
				id replacement = FindFocusableControlLocked(
					window->second.content_view);
				window->second.first_responder = replacement != nullptr ?
					replacement : state.window;
				focus_handle = replacement != nullptr ?
					g_views[replacement].control : window->second.handle;
			}
		}
	}
	if (focus_handle != nullptr) SetFocus(focus_handle);
	RepositionViewTree(self);
	NotifyHiddenStateChanges(before, after);
	InvalidateAppKitView(self);
}

id NSWindowInit(id self, SEL selector)
{
	constexpr std::uint64_t default_style = (1ull << 0) | (1ull << 1) |
		(1ull << 2) | (1ull << 3);
	return NSWindowInitWithContentRect(self, selector,
		DarlingObjcRect{0.0, 0.0, 640.0, 480.0}, default_style, 2, 0);
}

id NSWindowInitWithCoder(id self, SEL, id)
{
	auto decode_key = [](const char* utf8) -> id {
		return CreateDynamicNSString(objc_getClass("NSString"), utf8);
	};
	id rect_key = decode_key("NSWindowRect");
	DarlingObjcRect content_rect = NSKeyedArchiveDecodeRectForKey(rect_key);
	if (rect_key != nullptr) objc_release(rect_key);
	if (content_rect.width <= 0.0 || content_rect.height <= 0.0)
		content_rect = DarlingObjcRect{0.0, 0.0, 640.0, 480.0};
	id style_key = decode_key("NSWindowStyleMask");
	std::int64_t decoded_style = 15;
	if (style_key != nullptr) {
		decoded_style = NSKeyedUnarchiverDecodeIntegerForKey(nullptr, nullptr,
			style_key);
		objc_release(style_key);
	}
	id initialized = NSWindowInitWithContentRect(self,
		sel_registerName("initWithContentRect:styleMask:backing:defer:"),
		content_rect, static_cast<std::uint64_t>(decoded_style), 2, 0);
	if (initialized == nullptr) return nullptr;
	// NSWindowRect describes the archived outer frame. The initializer takes
	// a content rect, so restore the frame after creating the native HWND.
	NSWindowSetFrame(initialized, sel_registerName("setFrame:display:"),
		content_rect, false);
	auto decode_size = [&decode_key](const char* key_name,
		DarlingObjcPoint& size) {
		id key = decode_key(key_name);
		id encoded_size = key == nullptr ? nullptr :
			NSKeyedUnarchiverDecodeObjectForKey(nullptr, nullptr, key);
		if (key != nullptr) objc_release(key);
		const char* text = NSStringUTF8String(encoded_size, nullptr);
		double width = 0.0;
		double height = 0.0;
		const bool valid = ParseKeyedSizeString(text, width, height);
		if (encoded_size != nullptr) objc_release(encoded_size);
		if (valid) size = DarlingObjcPoint{width, height};
		return valid;
	};
	DarlingObjcPoint min_size{};
	if (decode_size("NSWindowContentMinSize", min_size) ||
		decode_size("NSMinSize", min_size))
		NSWindowSetMinSize(initialized,
			sel_registerName("setMinSize:"), min_size);
	DarlingObjcPoint max_size{};
	if (decode_size("NSWindowContentMaxSize", max_size) ||
		decode_size("NSMaxSize", max_size))
		NSWindowSetMaxSize(initialized,
			sel_registerName("setMaxSize:"), max_size);
	id title_key = decode_key("NSWindowTitle");
	if (title_key != nullptr) {
		id title = NSKeyedUnarchiverDecodeObjectForKey(nullptr, nullptr, title_key);
		objc_release(title_key);
		if (title != nullptr) {
			NSWindowSetTitle(initialized, sel_registerName("setTitle:"), title);
			objc_release(title);
		}
	}
	id content_view_key = decode_key("NSWindowView");
	if (content_view_key != nullptr) {
		id content_view = NSKeyedUnarchiverDecodeObjectForKey(nullptr, nullptr,
			content_view_key);
		objc_release(content_view_key);
		if (content_view != nullptr && IsViewKindOf(content_view, "NSView")) {
			NSWindowSetContentView(initialized,
				sel_registerName("setContentView:"), content_view);
		}
		if (content_view != nullptr) objc_release(content_view);
	}
	return initialized;
}

void NSWindowSetTitle(id self, SEL, id title)
{
	if (title != nullptr) objc_retain(title);
	HWND handle = nullptr;
	id previous = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		AppKitWindowState& state = g_windows[self];
		handle = state.handle;
		previous = state.title;
		state.title = title;
	}
	if (previous != nullptr) objc_release(previous);
	if (handle == nullptr || title == nullptr) return;
	const char* utf8 = static_cast<const char*>(darling_objc_msgSend_pointer0(
		title, sel_registerName("UTF8String")));
	if (utf8 == nullptr) return;
	const int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		utf8, -1, nullptr, 0);
	if (wide_length <= 0) return;
	std::vector<wchar_t> wide_title(static_cast<std::size_t>(wide_length));
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
		wide_title.data(), wide_length) == wide_length)
		SetWindowTextW(handle, wide_title.data());
}

id NSWindowTitle(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() ? nullptr : found->second.title;
}

DarlingObjcRect NSWindowFrame(id self, SEL)
{
	HWND handle = nullptr;
	DarlingObjcRect content_rect{};
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return {};
		handle = found->second.handle;
		content_rect = found->second.content_rect;
	}
	RECT frame{};
	if (handle == nullptr || !GetWindowRect(handle, &frame))
		return content_rect;
	const double screen_height = GetSystemMetrics(SM_CYSCREEN);
	return DarlingObjcRect{static_cast<double>(frame.left),
		screen_height - static_cast<double>(frame.bottom),
		static_cast<double>(frame.right - frame.left),
		static_cast<double>(frame.bottom - frame.top)};
}

bool NSWindowContentOriginOnScreen(id self, double& x, double& y)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return false;
		handle = found->second.handle;
	}
	if (handle == nullptr) return false;
	RECT client{};
	POINT origin{0, 0};
	if (!GetClientRect(handle, &client) || !ClientToScreen(handle, &origin))
		return false;
	const int primary_height = GetSystemMetrics(SM_CYSCREEN);
	if (primary_height <= 0) return false;
	x = static_cast<double>(origin.x);
	y = static_cast<double>(primary_height - origin.y - (client.bottom - client.top));
	return std::isfinite(x) && std::isfinite(y);
}

DarlingObjcRect NSWindowConvertRectToScreen(id self, SEL, DarlingObjcRect rect)
{
	double origin_x = 0.0;
	double origin_y = 0.0;
	if (!NSWindowContentOriginOnScreen(self, origin_x, origin_y)) return rect;
	return {rect.x + origin_x, rect.y + origin_y, rect.width, rect.height};
}

DarlingObjcRect NSWindowConvertRectFromScreen(id self, SEL, DarlingObjcRect rect)
{
	double origin_x = 0.0;
	double origin_y = 0.0;
	if (!NSWindowContentOriginOnScreen(self, origin_x, origin_y)) return rect;
	return {rect.x - origin_x, rect.y - origin_y, rect.width, rect.height};
}

void NSWindowSetFrame(id self, SEL, DarlingObjcRect frame, bool display)
{
	if (!std::isfinite(frame.x) || !std::isfinite(frame.y) ||
		!std::isfinite(frame.width) || !std::isfinite(frame.height) ||
		frame.width <= 0.0 || frame.height <= 0.0) return;
	const int outer_width = std::max(1,
		ClampWindowCoordinate(frame.width, 1, 10000));
	const int outer_height = std::max(1,
		ClampWindowCoordinate(frame.height, 1, 10000));
	HWND handle = nullptr;
	std::uint64_t style_mask = 0;
	id content_view = nullptr;
	double old_root_width = 0.0;
	double old_root_height = 0.0;
	double new_root_width = 0.0;
	double new_root_height = 0.0;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		handle = found->second.handle;
		style_mask = found->second.style_mask;
		content_view = found->second.content_view;
	}
	const DWORD style = Win32WindowStyle(style_mask);
	RECT adjusted{0, 0, 1, 1};
	if (!AdjustWindowRectEx(&adjusted, style, FALSE, 0)) return;
	const int left_inset = -adjusted.left;
	const int right_inset = adjusted.right - 1;
	const int top_inset = -adjusted.top;
	const int bottom_inset = adjusted.bottom - 1;
	const int content_width = std::max(1, outer_width - left_inset - right_inset);
	const int content_height = std::max(1, outer_height - top_inset - bottom_inset);
	if (handle != nullptr) {
		const int screen_height = GetSystemMetrics(SM_CYSCREEN);
		const int x = ClampWindowCoordinate(frame.x, -16000, 16000);
		const int y = ClampWindowCoordinate(screen_height - frame.y - outer_height,
			-16000, 16000);
		if (!SetWindowPos(handle, nullptr, x, y, outer_width, outer_height,
			SWP_NOZORDER | SWP_NOACTIVATE)) return;
	}
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		found->second.content_rect = DarlingObjcRect{
			frame.x + left_inset, frame.y + bottom_inset,
			static_cast<double>(content_width),
			static_cast<double>(content_height)};
		const auto root = g_views.find(content_view);
		if (root != g_views.end()) {
			old_root_width = root->second.bounds.width;
			old_root_height = root->second.bounds.height;
			root->second.frame.width = content_width;
			root->second.frame.height = content_height;
			if (!root->second.has_custom_bounds) {
				root->second.bounds.width = content_width;
				root->second.bounds.height = content_height;
			}
			new_root_width = root->second.bounds.width;
			new_root_height = root->second.bounds.height;
		}
	}
	if (content_view != nullptr)
		ResizeAppKitSubviews(content_view, old_root_width, old_root_height,
			new_root_width, new_root_height);
	if (content_view != nullptr) RepositionViewTree(content_view);
	InvalidateAppKitWindow(self);
	if (display && handle != nullptr) UpdateWindow(handle);
}

DarlingObjcPoint NSWindowContentSize(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() ? DarlingObjcPoint{} :
		DarlingObjcPoint{found->second.content_rect.width,
			found->second.content_rect.height};
}

void NSWindowSetContentSize(id self, SEL, DarlingObjcPoint size)
{
	if (!std::isfinite(size.x) || !std::isfinite(size.y)) return;
	const int width = std::max(1, ClampWindowCoordinate(size.x, 1, 10000));
	const int height = std::max(1, ClampWindowCoordinate(size.y, 1, 10000));
	HWND handle = nullptr;
	std::uint64_t style_mask = 0;
	DarlingObjcRect content_rect{};
	id content_view = nullptr;
	double old_root_width = 0.0;
	double old_root_height = 0.0;
	double new_root_width = 0.0;
	double new_root_height = 0.0;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		handle = found->second.handle;
		style_mask = found->second.style_mask;
		content_rect = found->second.content_rect;
		content_view = found->second.content_view;
	}
	if (handle != nullptr) {
		const DWORD style = Win32WindowStyle(style_mask);
		RECT frame{0, 0, width, height};
		if (!AdjustWindowRectEx(&frame, style, FALSE, 0)) return;
		const int screen_height = GetSystemMetrics(SM_CYSCREEN);
		const int x = ClampWindowCoordinate(content_rect.x + frame.left,
			-16000, 16000);
		const int y = ClampWindowCoordinate(screen_height - content_rect.y - height +
			frame.top, -16000, 16000);
		if (!SetWindowPos(handle, nullptr, x, y, frame.right - frame.left,
			frame.bottom - frame.top, SWP_NOZORDER | SWP_NOACTIVATE)) return;
	}
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		found->second.content_rect.width = width;
		found->second.content_rect.height = height;
		const auto root = g_views.find(content_view);
		if (root != g_views.end()) {
			old_root_width = root->second.bounds.width;
			old_root_height = root->second.bounds.height;
			root->second.frame.width = width;
			root->second.frame.height = height;
			if (!root->second.has_custom_bounds) {
				root->second.bounds.width = width;
				root->second.bounds.height = height;
			}
			new_root_width = root->second.bounds.width;
			new_root_height = root->second.bounds.height;
		}
	}
	if (content_view != nullptr)
		ResizeAppKitSubviews(content_view, old_root_width, old_root_height,
			new_root_width, new_root_height);
	if (content_view != nullptr) RepositionViewTree(content_view);
	InvalidateAppKitWindow(self);
}

void NSWindowMakeKeyAndOrderFront(id self, SEL, id)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		handle = found->second.handle;
		found->second.visible = handle != nullptr;
	}
	if (handle != nullptr) {
		ShowWindow(handle, SW_SHOWNORMAL);
		SetForegroundWindow(handle);
		UpdateWindow(handle);
	}
}

id NSWindowFirstResponder(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() || found->second.first_responder == nullptr ?
		self : found->second.first_responder;
}

bool NSWindowMakeFirstResponder(id self, SEL, id responder)
{
	const id candidate = responder == nullptr ? self : responder;
	if (candidate != self) {
		if (!IsViewKindOf(candidate, "NSView")) return false;
		{
			std::lock_guard<std::mutex> lock(g_windows_mutex);
			const auto window = g_windows.find(self);
			const auto view = g_views.find(candidate);
			if (window == g_windows.end() || view == g_views.end() ||
				view->second.window != self) return false;
		}
		if (NSViewIsHiddenOrHasHiddenAncestor(candidate, nullptr) ||
			!darling_objc_msgSend_bool0(candidate,
				sel_registerName("acceptsFirstResponder"))) return false;
	}
	HWND focus_handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto window = g_windows.find(self);
		if (window == g_windows.end()) return false;
		window->second.first_responder = candidate;
		focus_handle = candidate == self ? window->second.handle :
			g_views[candidate].control;
		if (focus_handle == nullptr) focus_handle = window->second.handle;
	}
	if (focus_handle != nullptr) SetFocus(focus_handle);
	return true;
}

void NSWindowOrderOut(id self, SEL, id)
{
	HWND handle = nullptr;
	bool was_key_window = false;
	bool was_main_window = false;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		handle = found->second.handle;
		found->second.visible = false;
		g_application_hidden_windows.erase(self);
		if (g_key_appkit_window == self) {
			g_key_appkit_window = nullptr;
			was_key_window = true;
		}
		if (g_main_appkit_window == self) {
			g_main_appkit_window = nullptr;
			was_main_window = true;
		}
	}
	if (was_key_window)
		NSWindowNotifyDelegate(self, "windowDidResignKey:");
	if (was_main_window)
		NSWindowNotifyDelegate(self, "windowDidResignMain:");
	if (handle != nullptr) ShowWindow(handle, SW_HIDE);
}

bool NSWindowIsKeyWindow(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	return g_key_appkit_window == self;
}

bool NSWindowIsMainWindow(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	return g_main_appkit_window == self;
}

bool NSWindowCanBecomeKeyWindow(id, SEL)
{
	return true;
}

bool NSWindowCanBecomeMainWindow(id, SEL)
{
	return true;
}

void NSWindowMakeKeyWindow(id self, SEL)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end() || found->second.handle == nullptr) return;
		handle = found->second.handle;
	}
	SetForegroundWindow(handle);
}

void NSWindowMakeMainWindow(id self, SEL)
{
	id previous = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		if (g_windows.find(self) == g_windows.end() ||
			g_main_appkit_window == self) return;
		previous = g_main_appkit_window;
		g_main_appkit_window = self;
	}
	if (previous != nullptr)
		NSWindowNotifyDelegate(previous, "windowDidResignMain:");
	NSWindowNotifyDelegate(self, "windowDidBecomeMain:");
}

void NSWindowClose(id self, SEL)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	if (handle != nullptr) SendMessageW(handle, WM_CLOSE, 0, 0);
}

bool NSWindowIsVisible(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found != g_windows.end() && found->second.visible;
}

bool NSWindowAcceptsMouseMovedEvents(id self, SEL)
{
	std::lock_guard lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found != g_windows.end() && found->second.accepts_mouse_moved_events;
}

void NSWindowSetAcceptsMouseMovedEvents(id self, SEL, bool accepts)
{
	std::lock_guard lock(g_windows_mutex);
	auto found = g_windows.find(self);
	if (found != g_windows.end()) found->second.accepts_mouse_moved_events = accepts;
}

DarlingObjcPoint NSWindowMinSize(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() ? DarlingObjcPoint{} : found->second.min_size;
}

void NSWindowSetMinSize(id self, SEL, DarlingObjcPoint size)
{
	if (!std::isfinite(size.x) || !std::isfinite(size.y)) return;
	size.x = std::clamp(size.x, 0.0, 10000.0);
	size.y = std::clamp(size.y, 0.0, 10000.0);
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	if (found == g_windows.end()) return;
	found->second.min_size = size;
	found->second.max_size.x = std::max(found->second.max_size.x, size.x);
	found->second.max_size.y = std::max(found->second.max_size.y, size.y);
}

DarlingObjcPoint NSWindowMaxSize(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() ? DarlingObjcPoint{} : found->second.max_size;
}

void NSWindowSetMaxSize(id self, SEL, DarlingObjcPoint size)
{
	if (!std::isfinite(size.x) || !std::isfinite(size.y)) return;
	size.x = std::clamp(size.x, 0.0, 10000.0);
	size.y = std::clamp(size.y, 0.0, 10000.0);
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	if (found == g_windows.end()) return;
	found->second.max_size.x = std::max(size.x, found->second.min_size.x);
	found->second.max_size.y = std::max(size.y, found->second.min_size.y);
}

void NSWindowCenter(id self, SEL)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found == g_windows.end()) return;
		handle = found->second.handle;
	}
	if (handle == nullptr) return;
	RECT frame{};
	RECT work{};
	if (!GetWindowRect(handle, &frame) ||
		!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) return;
	const int width = frame.right - frame.left;
	const int height = frame.bottom - frame.top;
	const int x = static_cast<int>(work.left + std::max<LONG>(0,
		(work.right - work.left - width) / 2));
	const int y = static_cast<int>(work.top + std::max<LONG>(0,
		(work.bottom - work.top - height) / 2));
	SetWindowPos(handle, nullptr, x, y, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void NSWindowMiniaturize(id self, SEL, id)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	if (handle != nullptr) ShowWindow(handle, SW_MINIMIZE);
}

void NSWindowDeminiaturize(id self, SEL, id)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	if (handle != nullptr) {
		ShowWindow(handle, SW_RESTORE);
		SetForegroundWindow(handle);
	}
}

bool NSWindowIsMiniaturized(id self, SEL)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	return handle != nullptr && IsIconic(handle);
}

void NSWindowZoom(id self, SEL, id)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	if (handle != nullptr) ShowWindow(handle,
		IsZoomed(handle) ? SW_RESTORE : SW_MAXIMIZE);
}

bool NSWindowIsZoomed(id self, SEL)
{
	HWND handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) handle = found->second.handle;
	}
	return handle != nullptr && IsZoomed(handle);
}

id NSWindowDelegate(id self, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	const auto found = g_windows.find(self);
	return found == g_windows.end() || found->second.delegate == nullptr ? nullptr :
		objc_retainAutorelease(found->second.delegate);
}

void NSWindowSetDelegate(id self, SEL, id delegate)
{
	if (delegate != nullptr) objc_retain(delegate);
	id previous = nullptr;
	bool found_window = false;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(self);
		if (found != g_windows.end()) {
			previous = std::exchange(found->second.delegate, delegate);
			found_window = true;
		}
	}
	if (!found_window && delegate != nullptr) objc_release(delegate);
	if (previous != nullptr) objc_release(previous);
}

bool NSWindowShouldClose(id window)
{
	id delegate = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(window);
		if (found != g_windows.end() && found->second.delegate != nullptr)
			delegate = objc_retain(found->second.delegate);
	}
	if (delegate == nullptr) return true;
	const SEL selector = sel_registerName("windowShouldClose:");
	const bool should_close = class_getInstanceMethod(object_getClass(delegate),
		selector) == nullptr || darling_objc_msgSend_bool_object1(delegate,
			selector, window);
	objc_release(delegate);
	return should_close;
}

void NSWindowNotifyDelegate(id window, const char* selector_name)
{
	if (window == nullptr || selector_name == nullptr) return;
	const char* notification_symbol = nullptr;
	id notification_name = nullptr;
	if (std::strcmp(selector_name, "windowWillClose:") == 0) {
		notification_symbol = "NSWindowWillCloseNotification";
		notification_name = NSWindowWillCloseNotification;
	} else if (std::strcmp(selector_name, "windowDidResize:") == 0) {
		notification_symbol = "NSWindowDidResizeNotification";
		notification_name = NSWindowDidResizeNotification;
	} else if (std::strcmp(selector_name, "windowDidBecomeKey:") == 0) {
		notification_symbol = "NSWindowDidBecomeKeyNotification";
		notification_name = NSWindowDidBecomeKeyNotification;
	} else if (std::strcmp(selector_name, "windowDidResignKey:") == 0) {
		notification_symbol = "NSWindowDidResignKeyNotification";
		notification_name = NSWindowDidResignKeyNotification;
	} else if (std::strcmp(selector_name, "windowDidBecomeMain:") == 0) {
		notification_symbol = "NSWindowDidBecomeMainNotification";
		notification_name = NSWindowDidBecomeMainNotification;
	} else if (std::strcmp(selector_name, "windowDidResignMain:") == 0) {
		notification_symbol = "NSWindowDidResignMainNotification";
		notification_name = NSWindowDidResignMainNotification;
	}
	if (notification_name == nullptr && notification_symbol != nullptr)
		notification_name = CreateDynamicNSString(objc_getClass("NSString"),
			notification_symbol);
	const bool owns_name = notification_name != nullptr &&
		notification_name != NSWindowWillCloseNotification &&
		notification_name != NSWindowDidResizeNotification &&
		notification_name != NSWindowDidBecomeKeyNotification &&
		notification_name != NSWindowDidResignKeyNotification &&
		notification_name != NSWindowDidBecomeMainNotification &&
		notification_name != NSWindowDidResignMainNotification;
	id notification = notification_name == nullptr ? nullptr :
		NSNotificationFactory(objc_getClass("NSNotification"), nullptr,
			notification_name, window);
	if (notification != nullptr) NSNotificationCenterDispatch(notification);
	id delegate = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		const auto found = g_windows.find(window);
		if (found != g_windows.end() && found->second.delegate != nullptr)
			delegate = objc_retain(found->second.delegate);
	}
	if (delegate != nullptr) {
		const SEL selector = sel_registerName(selector_name);
		if (class_getInstanceMethod(object_getClass(delegate), selector) != nullptr)
			darling_objc_msgSend_void1_object(delegate, selector,
				notification == nullptr ? window : notification);
		objc_release(delegate);
	}
	if (owns_name) objc_release(notification_name);
}

void NSApplicationFinishLaunching(id, SEL)
{
	if (g_application_launched) return;
	g_application_launched = true;
	id will_notification = NSNotificationFactory(
		objc_getClass("NSNotification"), nullptr,
		NSApplicationWillFinishLaunchingNotification, NSApp);
	NSNotificationCenterDispatch(will_notification);
	if (g_application_delegate != nullptr) {
		const SEL will_finish = sel_registerName("applicationWillFinishLaunching:");
		darling_objc_msgSend_void1_object(g_application_delegate, will_finish,
			will_notification);
	}
	id did_notification = NSNotificationFactory(
		objc_getClass("NSNotification"), nullptr,
		NSApplicationDidFinishLaunchingNotification, NSApp);
	NSNotificationCenterDispatch(did_notification);
	if (g_application_delegate != nullptr) {
		const SEL did_finish = sel_registerName("applicationDidFinishLaunching:");
		darling_objc_msgSend_void1_object(g_application_delegate, did_finish,
			did_notification);
	}
}

void NSApplicationSetActiveState(bool active)
{
	if (g_application_active_transition.exchange(true,
		std::memory_order_acq_rel)) return;
	struct TransitionScope final {
		~TransitionScope()
		{
			g_application_active_transition.store(false,
				std::memory_order_release);
		}
	} transition_scope;
	if (g_application_active.load(std::memory_order_acquire) == active) {
		return;
	}
	const id application = g_shared_application;
	const id will_name = active ? NSApplicationWillBecomeActiveNotification :
		NSApplicationWillResignActiveNotification;
	const id did_name = active ? NSApplicationDidBecomeActiveNotification :
		NSApplicationDidResignActiveNotification;
	const char* will_selector_name = active ? "applicationWillBecomeActive:" :
		"applicationWillResignActive:";
	const char* did_selector_name = active ? "applicationDidBecomeActive:" :
		"applicationDidResignActive:";
	auto deliver = [application](id name, const char* selector_name) {
		id notification = NSNotificationFactory(objc_getClass("NSNotification"),
			nullptr, name, application);
		if (notification != nullptr) NSNotificationCenterDispatch(notification);
		id delegate = g_application_delegate;
		if (delegate != nullptr) objc_retain(delegate);
		if (delegate != nullptr) {
			const SEL selector = sel_registerName(selector_name);
			if (class_getInstanceMethod(object_getClass(delegate), selector) != nullptr)
				darling_objc_msgSend_void1_object(delegate, selector, notification);
			objc_release(delegate);
		}
	};
	deliver(will_name, will_selector_name);
	g_application_active.store(active, std::memory_order_release);
	deliver(did_name, did_selector_name);
}

void NSApplicationActivateIgnoringOtherApps(id, SEL, bool)
{
	HWND window = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		for (const auto& [object, state] : g_windows) {
			if (state.handle != nullptr && state.visible) {
				window = state.handle;
				break;
			}
		}
	}
	if (window != nullptr) {
		ShowWindow(window, SW_SHOW);
		SetForegroundWindow(window);
	}
}

bool NSApplicationIsActive(id, SEL)
{
	return g_application_active.load(std::memory_order_acquire);
}

id NSApplicationWindows(id, SEL)
{
	std::vector<id> windows;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		for (const auto& [object, state] : g_windows)
			if (state.handle != nullptr) windows.push_back(object);
	}
	id result = CreateNSArray(objc_getClass("NSArray"), windows.data(),
		windows.size());
	return objc_autorelease(result);
}

void NSApplicationDispatchLifecycleEvent(id notification_name,
	const char* delegate_selector_name)
{
	const id application = g_shared_application;
	id notification = NSNotificationFactory(objc_getClass("NSNotification"),
		nullptr, notification_name, application);
	if (notification != nullptr) NSNotificationCenterDispatch(notification);
	id delegate = g_application_delegate;
	if (delegate != nullptr) objc_retain(delegate);
	if (delegate != nullptr) {
		const SEL selector = sel_registerName(delegate_selector_name);
		if (class_getInstanceMethod(object_getClass(delegate), selector) != nullptr)
			darling_objc_msgSend_void1_object(delegate, selector, notification);
		objc_release(delegate);
	}
}

bool NSApplicationIsHidden(id, SEL)
{
	std::lock_guard<std::mutex> lock(g_windows_mutex);
	return g_application_hidden;
}

void NSApplicationHide(id, SEL, id)
{
	if (g_application_hidden_transition.exchange(true,
		std::memory_order_acq_rel)) return;
	struct TransitionScope final {
		~TransitionScope()
		{
			g_application_hidden_transition.store(false,
				std::memory_order_release);
		}
	} transition_scope;
	{
		std::lock_guard lock(g_windows_mutex);
		if (g_application_hidden) return;
	}
	NSApplicationDispatchLifecycleEvent(NSApplicationWillHideNotification,
		"applicationWillHide:");
	std::vector<HWND> windows;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_application_hidden = true;
		g_application_hidden_windows.clear();
		for (auto& [object, state] : g_windows) {
			if (state.handle == nullptr || !state.visible) continue;
			state.visible = false;
			g_application_hidden_windows.insert(object);
			windows.push_back(state.handle);
		}
	}
	for (HWND window : windows) ShowWindow(window, SW_HIDE);
	NSApplicationDispatchLifecycleEvent(NSApplicationDidHideNotification,
		"applicationDidHide:");
}

void NSApplicationUnhideWithoutActivation(id, SEL)
{
	if (g_application_hidden_transition.exchange(true,
		std::memory_order_acq_rel)) return;
	struct TransitionScope final {
		~TransitionScope()
		{
			g_application_hidden_transition.store(false,
				std::memory_order_release);
		}
	} transition_scope;
	{
		std::lock_guard lock(g_windows_mutex);
		if (!g_application_hidden) return;
	}
	NSApplicationDispatchLifecycleEvent(NSApplicationWillUnhideNotification,
		"applicationWillUnhide:");
	std::vector<HWND> windows;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		g_application_hidden = false;
		for (id object : g_application_hidden_windows) {
			const auto found = g_windows.find(object);
			if (found == g_windows.end() || found->second.handle == nullptr) continue;
			found->second.visible = true;
			windows.push_back(found->second.handle);
		}
		g_application_hidden_windows.clear();
	}
	for (HWND window : windows) ShowWindow(window, SW_SHOWNOACTIVATE);
	NSApplicationDispatchLifecycleEvent(NSApplicationDidUnhideNotification,
		"applicationDidUnhide:");
}

void NSApplicationUnhide(id self, SEL, id)
{
	NSApplicationUnhideWithoutActivation(self, nullptr);
	HWND window = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_windows_mutex);
		for (const auto& [object, state] : g_windows)
			if (state.handle != nullptr && state.visible) { window = state.handle; break; }
	}
	if (window != nullptr) SetForegroundWindow(window);
}

void NSApplicationRun(id self, SEL selector)
{
	if (g_application_running.load(std::memory_order_acquire)) return;
	darling_windows_CFRunLoopUseCurrentAsMain();
	g_appkit_main_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
	g_application_termination_notified.store(false, std::memory_order_release);
	g_application_active.store(false, std::memory_order_release);
	g_application_active_transition.store(false, std::memory_order_release);
	g_application_hidden_transition.store(false, std::memory_order_release);
	g_application_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
	MSG queue_message{};
	PeekMessageW(&queue_message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
	g_application_running.store(true, std::memory_order_release);
	darling_windows_dispatch_main_queue_set_thread(GetCurrentThreadId());
	struct ApplicationRunScope final {
		~ApplicationRunScope()
		{
			g_application_running.store(false, std::memory_order_release);
			g_application_thread_id.store(0, std::memory_order_release);
			g_application_termination_pending.store(false, std::memory_order_release);
		}
	} run_scope;
	NSApplicationFinishLaunching(self, selector);
	HWND fallback_window = nullptr;
	if (!HasVisibleAppKitWindow()) {
		fallback_window = CreateAppKitWindow(nullptr, L"Wintosh AppKit bootstrap");
		if (fallback_window == nullptr) {
			g_application_exit_code = 1;
			return;
		}
	}
	if (fallback_window != nullptr) {
		ShowWindow(fallback_window, SW_SHOWNORMAL);
		UpdateWindow(fallback_window);
	}
	struct EventAutoreleasePool final {
		void* token{};
		EventAutoreleasePool() noexcept
		{
			try { token = objc_autoreleasePoolPush(); }
			catch (...) { token = nullptr; }
		}
		~EventAutoreleasePool() noexcept
		{
			if (token != nullptr) objc_autoreleasePoolPop(token);
		}
	};
	MSG message{};
	for (;;) {
		EventAutoreleasePool event_pool;
		if (DispatchOneAppKitCallback()) continue;
		if (darling_windows_CFRunLoopRunInModeForLoop(
		darling_windows_CFRunLoopGetCurrent(), "kCFRunLoopDefaultMode",
			0.0, true) == 0) continue;
		if (id event = TakeMatchingAppKitEvent((std::numeric_limits<std::uint64_t>::max)(),
			true)) {
			DispatchAppKitEventToApplication(g_shared_application, event);
			continue;
		}
		const BOOL result = GetMessageW(&message, nullptr, 0, 0);
		if (result == 0) {
			g_application_exit_code = static_cast<int>(message.wParam);
			break;
		}
		if (result == -1) {
			g_application_exit_code = 1;
			break;
		}
		if (message.message == kApplicationStopMessage) {
			PostQuitMessage(g_application_exit_code);
			continue;
		}
		if (message.message == kApplicationTerminateMessage) {
			NSApplicationNotifyWillTerminate(g_shared_application);
			PostQuitMessage(g_application_exit_code);
			continue;
		}
		if (message.message == kApplicationEventMessage) continue;
		if (darling_windows_dispatch_main_queue_handle_message(message.message))
			continue;
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

bool EnqueueAppKitCallback(std::function<void()> callback)
{
	if (!callback) return false;
	try {
		std::lock_guard lock(g_appkit_callback_mutex);
		g_appkit_callbacks.push_back(std::move(callback));
	} catch (...) {
		return false;
	}
	const DWORD thread = g_application_thread_id.load(std::memory_order_acquire);
	if (thread != 0)
		PostThreadMessageW(thread, kApplicationEventMessage, 0, 0);
	return true;
}

bool DispatchOneAppKitCallback()
{
	std::function<void()> callback;
	{
		std::lock_guard lock(g_appkit_callback_mutex);
		if (g_appkit_callbacks.empty()) return false;
		callback = std::move(g_appkit_callbacks.front());
		g_appkit_callbacks.pop_front();
	}
	try { callback(); }
	catch (...) { }
	return true;
}

bool NSApplicationIsRunning(id, SEL)
{
	return g_application_running.load(std::memory_order_acquire);
}

void NSApplicationPostQuit()
{
	if (!g_application_running.load(std::memory_order_acquire)) return;
	const DWORD application_thread = g_application_thread_id.load(std::memory_order_acquire);
	if (application_thread == 0) return;
	if (GetCurrentThreadId() == application_thread) {
		PostQuitMessage(g_application_exit_code);
		return;
	}
	PostThreadMessageW(application_thread, kApplicationStopMessage, 0, 0);
}

void NSApplicationNotifyWillTerminate(id application)
{
	if (g_application_termination_notified.exchange(true,
		std::memory_order_acq_rel)) return;
	id notification = NSNotificationFactory(objc_getClass("NSNotification"),
		nullptr, NSApplicationWillTerminateNotification, application);
	if (notification != nullptr) NSNotificationCenterDispatch(notification);
	id delegate = g_application_delegate;
	if (delegate != nullptr) objc_retain(delegate);
	if (delegate != nullptr) {
		const SEL selector = sel_registerName("applicationWillTerminate:");
		if (class_getInstanceMethod(object_getClass(delegate), selector) != nullptr)
			darling_objc_msgSend_void1_object(delegate, selector, notification);
		objc_release(delegate);
	}
}

void NSApplicationRequestTermination(id application)
{
	if (!g_application_running.load(std::memory_order_acquire)) return;
	const DWORD application_thread = g_application_thread_id.load(
		std::memory_order_acquire);
	if (application_thread == 0) return;
	if (GetCurrentThreadId() == application_thread) {
		NSApplicationNotifyWillTerminate(application);
		PostQuitMessage(g_application_exit_code);
		return;
	}
	PostThreadMessageW(application_thread, kApplicationTerminateMessage, 0, 0);
}

void NSApplicationStop(id, SEL, id)
{
	NSApplicationPostQuit();
}

bool NSApplicationShouldTerminateAfterLastWindowClosed()
{
	id delegate = g_application_delegate;
	if (delegate == nullptr) return false;
	objc_retain(delegate);
	const SEL selector = sel_registerName(
		"applicationShouldTerminateAfterLastWindowClosed:");
	if (class_getInstanceMethod(object_getClass(delegate), selector) == nullptr) {
		objc_release(delegate);
		return false;
	}
	id application = g_shared_application;
	if (application == nullptr)
		application = NSApplicationSharedApplication(objc_getClass("NSApplication"),
			nullptr);
	const bool should_terminate = application != nullptr &&
		darling_objc_msgSend_bool_object1(delegate, selector, application);
	objc_release(delegate);
	return should_terminate;
}

void NSApplicationTerminate(id self, SEL, id)
{
	if (g_application_termination_pending.load(std::memory_order_acquire) ||
		!g_application_running.load(std::memory_order_acquire)) return;
	id delegate = g_application_delegate;
	if (delegate != nullptr) objc_retain(delegate);
	const SEL selector = sel_registerName("applicationShouldTerminate:");
	std::int64_t reply = 2; // NSTerminateNow
	if (delegate != nullptr && class_getInstanceMethod(object_getClass(delegate),
		selector) != nullptr) {
		reply = darling_objc_msgSend_int64_object1(delegate, selector, self);
	}
	if (delegate != nullptr) objc_release(delegate);
	switch (reply) {
	case 0: // NSTerminateCancel
		return;
	case 1: // NSTerminateLater
		g_application_termination_pending.store(true, std::memory_order_release);
		return;
	case 2: // NSTerminateNow
		NSApplicationRequestTermination(self);
		return;
	default:
		return;
	}
}

void NSApplicationReplyToShouldTerminate(id, SEL, bool should_terminate)
{
	if (!g_application_termination_pending.exchange(false,
		std::memory_order_acq_rel)) return;
	if (should_terminate) {
		id application = g_shared_application;
		NSApplicationRequestTermination(application);
	}
}

void NSNotificationFinalize(id object)
{
	AppKitNotificationState state{};
	{
		std::lock_guard lock(g_notifications_mutex);
		const auto found = g_notifications.find(object);
		if (found == g_notifications.end()) return;
		state = found->second;
		g_notifications.erase(found);
	}
	if (state.name != nullptr) objc_release(state.name);
	if (state.object != nullptr) objc_release(state.object);
	if (state.user_info != nullptr) objc_release(state.user_info);
}

id NSNotificationInit(id self, SEL, id name, id object, id user_info)
{
	if (self == nullptr || name == nullptr ||
		NSStringUTF8String(name, nullptr) == nullptr) return nullptr;
	AppKitNotificationState replacement{
		objc_retain(name), object == nullptr ? nullptr : objc_retain(object),
		user_info == nullptr ? nullptr : objc_retain(user_info)};
	bool needs_finalizer = false;
	AppKitNotificationState previous{};
	try {
		std::lock_guard lock(g_notifications_mutex);
		auto found = g_notifications.find(self);
		if (found == g_notifications.end()) {
			g_notifications.emplace(self, replacement);
			needs_finalizer = true;
		} else {
			previous = std::exchange(found->second, replacement);
		}
	} catch (...) {
		objc_release(replacement.name);
		if (replacement.object != nullptr) objc_release(replacement.object);
		if (replacement.user_info != nullptr) objc_release(replacement.user_info);
		return nullptr;
	}
	if (previous.name != nullptr) objc_release(previous.name);
	if (previous.object != nullptr) objc_release(previous.object);
	if (previous.user_info != nullptr) objc_release(previous.user_info);
	if (needs_finalizer && !darling_objc_register_object_finalizer(self,
		&NSNotificationFinalize)) {
		NSNotificationFinalize(self);
		return nullptr;
	}
	return self;
}

id NSNotificationFactory(Class cls, SEL selector, id name, id object)
{
	if (cls == nullptr) return nullptr;
	id instance = class_createInstance(cls, 0);
	if (instance == nullptr) return nullptr;
	instance = NSNotificationInit(instance, selector, name, object, nullptr);
	return instance == nullptr ? nullptr : objc_autorelease(instance);
}

id NSNotificationFactoryWithUserInfo(Class cls, SEL selector, id name,
	id object, id user_info)
{
	if (cls == nullptr) return nullptr;
	id instance = class_createInstance(cls, 0);
	if (instance == nullptr) return nullptr;
	instance = NSNotificationInit(instance, selector, name, object, user_info);
	return instance == nullptr ? nullptr : objc_autorelease(instance);
}

id NSNotificationName(id self, SEL)
{
	std::lock_guard lock(g_notifications_mutex);
	const auto found = g_notifications.find(self);
	return found == g_notifications.end() ? nullptr :
		objc_retainAutorelease(found->second.name);
}

id NSNotificationObject(id self, SEL)
{
	std::lock_guard lock(g_notifications_mutex);
	const auto found = g_notifications.find(self);
	return found == g_notifications.end() || found->second.object == nullptr ?
		nullptr : objc_retainAutorelease(found->second.object);
}

id NSNotificationUserInfo(id self, SEL)
{
	std::lock_guard lock(g_notifications_mutex);
	const auto found = g_notifications.find(self);
	return found == g_notifications.end() || found->second.user_info == nullptr ?
		nullptr : objc_retainAutorelease(found->second.user_info);
}

void NSEventFinalize(id object)
{
	id window = nullptr;
	id characters = nullptr;
	id characters_ignoring_modifiers = nullptr;
	{
		std::lock_guard lock(g_events_mutex);
		const auto found = g_events.find(object);
		if (found == g_events.end()) return;
		window = found->second.window;
		characters = found->second.characters;
		characters_ignoring_modifiers = found->second.characters_ignoring_modifiers;
		g_events.erase(found);
	}
	if (window != nullptr) objc_release(window);
	if (characters != nullptr) objc_release(characters);
	if (characters_ignoring_modifiers != nullptr)
		objc_release(characters_ignoring_modifiers);
}

id NSEventCreateMouse(Class cls, std::uint64_t type,
	DarlingObjcPoint location, std::uint64_t modifier_flags,
	id window, std::int64_t button_number, std::int64_t click_count)
{
	if (cls == nullptr || window == nullptr) return nullptr;
	id event = class_createInstance(cls, 0);
	if (event == nullptr) return nullptr;
	id retained_window = objc_retain(window);
	try {
		std::lock_guard lock(g_events_mutex);
		g_events.emplace(event, AppKitEventState{type, location, modifier_flags,
			static_cast<double>(GetTickCount64()) / 1000.0, retained_window,
			button_number, click_count});
	} catch (...) {
		objc_release(retained_window);
		objc_release(event);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(event, &NSEventFinalize)) {
		NSEventFinalize(event);
		objc_release(event);
		return nullptr;
	}
	return event;
}

id NSEventCreateKeyboard(Class cls, std::uint64_t type, id window,
	std::uint64_t modifier_flags, std::uint16_t key_code, bool repeat,
	const char* characters, const char* characters_ignoring_modifiers)
{
	if (cls == nullptr || window == nullptr || characters == nullptr ||
		characters_ignoring_modifiers == nullptr) return nullptr;
	id event = class_createInstance(cls, 0);
	if (event == nullptr) return nullptr;
	id retained_window = objc_retain(window);
	id text = CreateDynamicNSString(objc_getClass("NSString"), characters);
	id unmodified_text = CreateDynamicNSString(objc_getClass("NSString"),
		characters_ignoring_modifiers);
	if (text == nullptr || unmodified_text == nullptr) {
		objc_release(retained_window);
		if (text != nullptr) objc_release(text);
		if (unmodified_text != nullptr) objc_release(unmodified_text);
		objc_release(event);
		return nullptr;
	}
	try {
		std::lock_guard lock(g_events_mutex);
		g_events.emplace(event, AppKitEventState{type, {}, modifier_flags,
			static_cast<double>(GetTickCount64()) / 1000.0, retained_window,
			-1, 0, key_code, repeat, text, unmodified_text});
	} catch (...) {
		objc_release(retained_window);
		objc_release(text);
		objc_release(unmodified_text);
		objc_release(event);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(event, &NSEventFinalize)) {
		NSEventFinalize(event);
		objc_release(event);
		return nullptr;
	}
	return event;
}

std::uint64_t NSEventType(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0 : found->second.type;
}

bool EnqueueAppKitEvent(id event, std::uint64_t type, bool at_start,
	bool wake_application_thread)
{
	if (event == nullptr) return false;
	double timestamp = 0.0;
	{
		std::lock_guard lock(g_events_mutex);
		const auto found = g_events.find(event);
		if (found != g_events.end()) timestamp = found->second.timestamp;
	}
	objc_retain(event);
	try {
		std::lock_guard lock(g_appkit_event_queue_mutex);
		if (at_start) g_appkit_event_queue.push_front({event, type, timestamp});
		else g_appkit_event_queue.push_back({event, type, timestamp});
	} catch (...) {
		objc_release(event);
		return false;
	}
	if (wake_application_thread &&
		g_application_running.load(std::memory_order_acquire)) {
		const DWORD thread = g_application_thread_id.load(std::memory_order_acquire);
		if (thread != 0)
			PostThreadMessageW(thread, kApplicationEventMessage, 0, 0);
	}
	return true;
}

bool QueueAppKitEventWhenPolling(id event, std::uint64_t type)
{
	return g_appkit_event_polling.load(std::memory_order_acquire) &&
		EnqueueAppKitEvent(event, type, false, false);
}

id TakeMatchingAppKitEvent(std::uint64_t event_mask, bool dequeue)
{
	std::lock_guard lock(g_appkit_event_queue_mutex);
	for (auto it = g_appkit_event_queue.begin(); it != g_appkit_event_queue.end(); ++it) {
		const std::uint64_t event_bit = it->type < 64 ? (1ull << it->type) : 0;
		if ((event_mask & event_bit) == 0) continue;
		id event = it->event;
		if (dequeue) g_appkit_event_queue.erase(it);
		else objc_retain(event);
		return objc_autorelease(event);
	}
	return nullptr;
}

void NSApplicationPostEvent(id self, SEL, id event, bool at_start)
{
	if (self == nullptr || event == nullptr) return;
	EnqueueAppKitEvent(event, NSEventType(event, nullptr), at_start, true);
}

void NSApplicationDiscardEventsMatchingMask(id, SEL, std::uint64_t event_mask,
	id before_event)
{
	if (before_event == nullptr) return;
	const double before_timestamp = NSEventTimestamp(before_event, nullptr);
	if (!std::isfinite(before_timestamp)) return;
	std::lock_guard lock(g_appkit_event_queue_mutex);
	for (auto it = g_appkit_event_queue.begin(); it != g_appkit_event_queue.end();) {
		const std::uint64_t event_bit = it->type < 64 ? (1ull << it->type) : 0;
		if ((event_mask & event_bit) != 0 && it->timestamp < before_timestamp) {
			id discarded = it->event;
			it = g_appkit_event_queue.erase(it);
			objc_release(discarded);
		} else {
			++it;
		}
	}
}

id NSApplicationNextEventMatchingMask(id, SEL, std::uint64_t event_mask,
	id until_date, id mode, bool dequeue)
{
	struct PollingScope final {
		bool previous = g_appkit_event_polling.exchange(true,
			std::memory_order_acq_rel);
		~PollingScope() {
			g_appkit_event_polling.store(previous, std::memory_order_release);
		}
	} polling_scope;
	(void)mode;
	const SEL interval_selector = sel_registerName("timeIntervalSinceNow");
	if (until_date != nullptr && class_getInstanceMethod(
		object_getClass(until_date), interval_selector) == nullptr) return nullptr;
	for (;;) {
		if (DispatchOneAppKitCallback()) continue;
		if (id event = TakeMatchingAppKitEvent(event_mask, dequeue)) return event;
		DWORD timeout = INFINITE;
		if (until_date != nullptr) {
			const double seconds = darling_objc_msgSend_double0(until_date,
				interval_selector);
			if (!std::isfinite(seconds) || seconds <= 0.0) return nullptr;
			const double milliseconds = std::ceil(seconds * 1000.0);
			timeout = static_cast<DWORD>((std::min)(milliseconds,
				static_cast<double>(MAXDWORD - 1)));
		}
		const DWORD wait = MsgWaitForMultipleObjectsEx(0, nullptr, timeout,
			QS_ALLINPUT, MWMO_INPUTAVAILABLE);
		if (wait == WAIT_FAILED) return nullptr;
		if (wait == WAIT_TIMEOUT) {
			if (until_date == nullptr) return nullptr;
			const double remaining = darling_objc_msgSend_double0(until_date,
				interval_selector);
			if (!std::isfinite(remaining) || remaining <= 0.0) return nullptr;
			continue;
		}
		MSG message{};
		if (!PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) continue;
		if (message.message == WM_QUIT) {
			PostQuitMessage(static_cast<int>(message.wParam));
			return nullptr;
		}
		if (message.message == kApplicationStopMessage) {
			PostQuitMessage(g_application_exit_code);
			return nullptr;
		}
		if (message.message == kApplicationTerminateMessage) {
			NSApplicationNotifyWillTerminate(g_shared_application);
			PostQuitMessage(g_application_exit_code);
			return nullptr;
		}
		if (message.message == kApplicationEventMessage) continue;
		if (darling_windows_dispatch_main_queue_handle_message(message.message))
			continue;
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
}

DarlingObjcPoint NSEventLocationInWindow(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? DarlingObjcPoint{} : found->second.location;
}

std::uint64_t NSEventModifierFlags(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0 : found->second.modifier_flags;
}

double NSEventTimestamp(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0.0 : found->second.timestamp;
}

id NSEventWindow(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() || found->second.window == nullptr ? nullptr :
		objc_retainAutorelease(found->second.window);
}

std::int64_t NSEventButtonNumber(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? -1 : found->second.button_number;
}

std::int64_t NSEventClickCount(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0 : found->second.click_count;
}

id NSEventCharacters(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() || found->second.characters == nullptr ? nullptr :
		objc_retainAutorelease(found->second.characters);
}

id NSEventCharactersIgnoringModifiers(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ||
		found->second.characters_ignoring_modifiers == nullptr ? nullptr :
		objc_retainAutorelease(found->second.characters_ignoring_modifiers);
}

std::uint16_t NSEventKeyCode(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0 : found->second.key_code;
}

bool NSEventIsARepeat(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found != g_events.end() && found->second.repeat;
}

double NSEventDeltaX(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0.0 : found->second.delta_x;
}

double NSEventDeltaY(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0.0 : found->second.delta_y;
}

double NSEventScrollingDeltaX(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0.0 : found->second.scrolling_delta_x;
}

double NSEventScrollingDeltaY(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found == g_events.end() ? 0.0 : found->second.scrolling_delta_y;
}

bool NSEventHasPreciseScrollingDeltas(id self, SEL)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(self);
	return found != g_events.end() && found->second.precise_scrolling_deltas;
}

bool AppKitHasOverride(id object, SEL selector)
{
	if (object == nullptr || selector == nullptr) return false;
	const Method method = class_getInstanceMethod(object_getClass(object), selector);
	return method != nullptr && method_getImplementation(method) != nullptr;
}

id AppKitResponderForSelector(id target, SEL selector)
{
	std::unordered_set<id> visited;
	for (unsigned depth = 0; target != nullptr && depth < 64; ++depth) {
		if (!visited.insert(target).second) break;
		if (AppKitHasOverride(target, selector)) return target;
		const SEL next_responder = sel_registerName("nextResponder");
		if (!AppKitHasOverride(target, next_responder)) break;
		target = darling_objc_msgSend_object0(target, next_responder);
	}
	return nullptr;
}

void NSEventMarkKeyboardHandled(id event)
{
	std::lock_guard lock(g_events_mutex);
	const auto found = g_events.find(event);
	if (found != g_events.end()) found->second.keyboard_handled = true;
}

bool AppKitApplicationOverridesSendEvent()
{
	id application = g_shared_application;
	if (application == nullptr) return false;
	const SEL selector = sel_registerName("sendEvent:");
	const Method method = class_getInstanceMethod(object_getClass(application), selector);
	return method != nullptr && method_getImplementation(method) !=
		reinterpret_cast<IMP>(&NSApplicationSendEvent);
}

id NSApplicationCurrentEvent(id, SEL)
{
	return g_current_appkit_event == nullptr ? nullptr :
		objc_retainAutorelease(g_current_appkit_event);
}

void DispatchAppKitEventToApplication(id application, id event)
{
	if (application == nullptr || event == nullptr) return;
	AppKitCurrentEventScope current_event(event);
	darling_objc_msgSend_void1_object(application,
		sel_registerName("sendEvent:"), event);
}

void NSApplicationSendEvent(id, SEL, id event)
{
	AppKitCurrentEventScope current_event(event);
	AppKitEventState event_state{};
	{
		std::lock_guard lock(g_events_mutex);
		const auto found = g_events.find(event);
		if (found == g_events.end()) return;
		event_state = found->second;
	}
	id root = nullptr;
	id first_responder = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_windows.find(event_state.window);
		if (found == g_windows.end()) return;
		root = found->second.content_view;
		first_responder = found->second.first_responder;
	}
	id target = nullptr;
	if (event_state.type == kEventKeyDown || event_state.type == kEventKeyUp ||
		event_state.type == kEventFlagsChanged)
		target = first_responder == nullptr ? root : first_responder;
	HWND host_window = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto window = g_windows.find(event_state.window);
		if (window != g_windows.end()) host_window = window->second.handle;
		if (event_state.type >= kEventLeftMouseDown &&
			event_state.type <= kEventRightMouseDragged) {
			const auto tracking = g_mouse_tracking.find(host_window);
			if (tracking != g_mouse_tracking.end()) target = tracking->second.view;
		}
	}
	if (target == nullptr)
		target = FindAppKitViewAtPoint(root, event_state.location.x,
			event_state.location.y);
	if (event_state.type == kEventKeyDown) {
		if (darling_objc_msgSend_bool_object1(event_state.window,
			sel_registerName("performKeyEquivalent:"), event)) {
			NSEventMarkKeyboardHandled(event);
			return;
		}
		if (event_state.keyboard_virtual_key != 0 &&
			DispatchAppKitMenuKey(event_state.keyboard_virtual_key)) {
			NSEventMarkKeyboardHandled(event);
			return;
		}
	}
	const char* selector_name = nullptr;
	switch (event_state.type) {
	case kEventLeftMouseDown: selector_name = "mouseDown:"; break;
	case kEventLeftMouseUp: selector_name = "mouseUp:"; break;
	case kEventRightMouseDown: selector_name = "rightMouseDown:"; break;
	case kEventRightMouseUp: selector_name = "rightMouseUp:"; break;
	case kEventMouseMoved: selector_name = "mouseMoved:"; break;
	case kEventLeftMouseDragged: selector_name = "mouseDragged:"; break;
	case kEventRightMouseDragged: selector_name = "rightMouseDragged:"; break;
	case kEventKeyDown: selector_name = "keyDown:"; break;
	case kEventKeyUp: selector_name = "keyUp:"; break;
	case kEventFlagsChanged: selector_name = "flagsChanged:"; break;
	case kEventScrollWheel: selector_name = "scrollWheel:"; break;
	default: return;
	}
	const SEL callback = sel_registerName(selector_name);
	target = AppKitResponderForSelector(target, callback);
	if (target != nullptr) {
		darling_objc_msgSend_void1_object(target, callback, event);
		if (event_state.type == kEventKeyDown || event_state.type == kEventKeyUp ||
			event_state.type == kEventFlagsChanged)
			NSEventMarkKeyboardHandled(event);
	}
	if (event_state.type == kEventLeftMouseUp ||
		event_state.type == kEventRightMouseUp)
		ClearAppKitMouseTracking(host_window);
}

void ClearAppKitMouseTracking(HWND window)
{
	id view = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_mouse_tracking.find(window);
		if (found == g_mouse_tracking.end()) return;
		view = found->second.view;
		g_mouse_tracking.erase(found);
	}
	if (view != nullptr) objc_release(view);
	if (GetCapture() == window) ReleaseCapture();
}

bool DispatchAppKitMouseEvent(HWND host_window, UINT message, WPARAM,
	LPARAM lparam)
{
	const auto window_object = reinterpret_cast<id>(GetWindowLongPtrW(host_window,
		GWLP_USERDATA));
	if (window_object == nullptr) return false;
	POINT client{static_cast<short>(LOWORD(lparam)),
		static_cast<short>(HIWORD(lparam))};
	RECT client_rect{};
	if (!GetClientRect(host_window, &client_rect)) return false;
	id root = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_windows.find(window_object);
		if (found == g_windows.end()) return false;
		root = found->second.content_view;
	}
	AppKitMouseTrackingState tracking{};
	bool accepts_mouse_moved_events = false;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_mouse_tracking.find(host_window);
		if (found != g_mouse_tracking.end()) tracking = found->second;
		const auto appkit_window = g_windows.find(window_object);
		if (appkit_window != g_windows.end())
			accepts_mouse_moved_events = appkit_window->second.accepts_mouse_moved_events;
	}
	const DarlingObjcPoint location{static_cast<double>(client.x),
		static_cast<double>(client_rect.bottom - client.y)};
	std::uint64_t type = 0;
	std::int64_t button = -1;
	std::int64_t click_count = 1;
	switch (message) {
	case WM_LBUTTONDOWN: type = kEventLeftMouseDown; button = 0; break;
	case WM_LBUTTONDBLCLK: type = kEventLeftMouseDown; button = 0; click_count = 2; break;
	case WM_LBUTTONUP: type = kEventLeftMouseUp; button = 0; break;
	case WM_RBUTTONDOWN: type = kEventRightMouseDown; button = 1; break;
	case WM_RBUTTONDBLCLK: type = kEventRightMouseDown; button = 1; click_count = 2; break;
	case WM_RBUTTONUP: type = kEventRightMouseUp; button = 1; break;
	case WM_MOUSEMOVE:
		if (tracking.view != nullptr) {
			button = tracking.button;
			type = button == 1 ? kEventRightMouseDragged : kEventLeftMouseDragged;
		} else type = kEventMouseMoved;
		break;
	default: return false;
	}
	if (type == kEventMouseMoved && !accepts_mouse_moved_events) return false;
	if ((type == kEventLeftMouseUp || type == kEventRightMouseUp) && tracking.view != nullptr &&
		tracking.button == button)
		click_count = tracking.click_count;
	id target = tracking.view != nullptr ? tracking.view :
		FindAppKitViewAtPoint(root, location.x, location.y);
	const char* selector_name = type == kEventLeftMouseDown ? "mouseDown:" :
		type == kEventLeftMouseUp ? "mouseUp:" :
		type == kEventRightMouseDown ? "rightMouseDown:" :
		type == kEventRightMouseUp ? "rightMouseUp:" :
		type == kEventLeftMouseDragged ? "mouseDragged:" :
		type == kEventRightMouseDragged ? "rightMouseDragged:" : "mouseMoved:";
	const SEL callback = sel_registerName(selector_name);
	const bool mouse_up = type == kEventLeftMouseUp || type == kEventRightMouseUp;
	if (!g_appkit_event_polling.load(std::memory_order_acquire) &&
		(target == nullptr || (AppKitResponderForSelector(target, callback) == nullptr &&
		!AppKitApplicationOverridesSendEvent()))) {
		if (mouse_up) ClearAppKitMouseTracking(host_window);
		return false;
	}
	if (type == kEventLeftMouseDown || type == kEventRightMouseDown) {
		id previous_view = nullptr;
		{
			std::lock_guard lock(g_windows_mutex);
			const auto previous = g_mouse_tracking.find(host_window);
			if (previous != g_mouse_tracking.end()) previous_view = previous->second.view;
			objc_retain(target);
			g_mouse_tracking[host_window] = {target, button, click_count};
		}
		if (previous_view != nullptr) objc_release(previous_view);
		SetCapture(host_window);
	}
	std::uint64_t flags = 0;
	if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) flags |= 1ull << 17;
	if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) flags |= 1ull << 18;
	if ((GetKeyState(VK_MENU) & 0x8000) != 0) flags |= 1ull << 19;
	if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
		(GetKeyState(VK_RWIN) & 0x8000) != 0) flags |= 1ull << 20;
	if ((GetKeyState(VK_CAPITAL) & 1) != 0) flags |= 1ull << 16;
	id event = NSEventCreateMouse(objc_getClass("NSEvent"), type, location,
		flags, window_object, button, click_count);
	if (event == nullptr) {
		if (mouse_up) ClearAppKitMouseTracking(host_window);
		return false;
	}
	if (QueueAppKitEventWhenPolling(event, type)) {
		objc_release(event);
		return true;
	}
	id application = g_shared_application;
	if (application != nullptr) DispatchAppKitEventToApplication(application, event);
	objc_release(event);
	if (mouse_up) ClearAppKitMouseTracking(host_window);
	return true;
}

bool DispatchAppKitScrollEvent(HWND host_window, UINT message, WPARAM wparam,
	LPARAM lparam)
{
	const auto window_object = reinterpret_cast<id>(GetWindowLongPtrW(host_window,
		GWLP_USERDATA));
	if (window_object == nullptr || (message != WM_MOUSEWHEEL &&
		message != WM_MOUSEHWHEEL)) return false;
	POINT local{static_cast<short>(LOWORD(lparam)),
		static_cast<short>(HIWORD(lparam))};
	if (!ScreenToClient(host_window, &local)) return false;
	RECT client{};
	if (!GetClientRect(host_window, &client)) return false;
	const DarlingObjcPoint location{static_cast<double>(local.x),
		static_cast<double>(client.bottom - local.y)};
	id root = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto found = g_windows.find(window_object);
		if (found == g_windows.end()) return false;
		root = found->second.content_view;
	}
	const id hit = FindAppKitViewAtPoint(root, location.x, location.y);
	const SEL selector = sel_registerName("scrollWheel:");
	if (hit == nullptr || (AppKitResponderForSelector(hit, selector) == nullptr &&
		!AppKitApplicationOverridesSendEvent())) return false;
	const double raw_delta = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) /
		static_cast<double>(WHEEL_DELTA) * 40.0;
	const double delta_x = message == WM_MOUSEHWHEEL ? raw_delta : 0.0;
	const double delta_y = message == WM_MOUSEWHEEL ? raw_delta : 0.0;
	id event = NSEventCreateMouse(objc_getClass("NSEvent"), kEventScrollWheel,
		location, 0, window_object, -1, 0);
	if (event == nullptr) return false;
	{
		std::lock_guard lock(g_events_mutex);
		const auto found = g_events.find(event);
		if (found != g_events.end()) {
			found->second.delta_x = delta_x;
			found->second.delta_y = delta_y;
			found->second.scrolling_delta_x = delta_x;
			found->second.scrolling_delta_y = delta_y;
		}
	}
	if (QueueAppKitEventWhenPolling(event, kEventScrollWheel)) {
		objc_release(event);
		return true;
	}
	id application = g_shared_application;
	if (application != nullptr) DispatchAppKitEventToApplication(application, event);
	objc_release(event);
	return true;
}

std::uint16_t DarlingKeyCode(WPARAM virtual_key, LPARAM key_data)
{
	const bool extended = (key_data & (1ll << 24)) != 0;
	const auto scan = static_cast<std::uint8_t>(
		(static_cast<std::uintptr_t>(key_data) >> 16) & 0xff);
	// AppKit keyCode identifies the physical key, so prefer the Set 1 scan
	// position over the layout-dependent virtual-key character.
	if (extended) {
		switch (scan) {
		case 0x1c: return 0x4c; // keypad enter
		case 0x1d: return 0x3e; // right control
		case 0x38: return 0x3d; // right option
		case 0x47: return 0x73; // home
		case 0x48: return 0x7e; // up
		case 0x49: return 0x74; // page up
		case 0x4b: return 0x7b; // left
		case 0x4d: return 0x7c; // right
		case 0x4f: return 0x77; // end
		case 0x50: return 0x7d; // down
		case 0x51: return 0x79; // page down
		case 0x52: return 0x72; // insert
		case 0x53: return 0x75; // forward delete
		case 0x5b: return 0x37; // left command
		case 0x5c: return 0x36; // right command
		default: break;
		}
	}
	switch (scan) {
	case 0x01: return 0x35;
	case 0x02: return 0x12; case 0x03: return 0x13;
	case 0x04: return 0x14; case 0x05: return 0x15;
	case 0x06: return 0x17; case 0x07: return 0x16;
	case 0x08: return 0x1a; case 0x09: return 0x1d;
	case 0x0a: return 0x19; case 0x0b: return 0x1c;
	case 0x0c: return 0x1b; case 0x0d: return 0x18;
	case 0x0e: return 0x33; case 0x0f: return 0x30;
	case 0x10: return 0x0c; case 0x11: return 0x0d;
	case 0x12: return 0x0e; case 0x13: return 0x0f;
	case 0x14: return 0x11; case 0x15: return 0x10;
	case 0x16: return 0x20; case 0x17: return 0x22;
	case 0x18: return 0x1f; case 0x19: return 0x23;
	case 0x1a: return 0x21; case 0x1b: return 0x1e;
	case 0x1c: return 0x24; case 0x1d: return 0x3b;
	case 0x1e: return 0x00; case 0x1f: return 0x01;
	case 0x20: return 0x02; case 0x21: return 0x03;
	case 0x22: return 0x05; case 0x23: return 0x04;
	case 0x24: return 0x26; case 0x25: return 0x25;
	case 0x26: return 0x28; case 0x27: return 0x29;
	case 0x28: return 0x27; case 0x29: return 0x32;
	case 0x2a: return 0x38; case 0x2b: return 0x2a;
	case 0x2c: return 0x06; case 0x2d: return 0x07;
	case 0x2e: return 0x08; case 0x2f: return 0x09;
	case 0x30: return 0x0b; case 0x31: return 0x2e;
	case 0x32: return 0x2d; case 0x33: return 0x2b;
	case 0x34: return 0x2f; case 0x35: return 0x2c;
	case 0x36: return 0x3c; case 0x38: return 0x3a;
	case 0x39: return 0x31;
	default: break;
	}
	if (virtual_key >= 'A' && virtual_key <= 'Z') {
		static constexpr std::pair<UINT, std::uint16_t> keys[] = {
			{'A', 0x00}, {'S', 0x01}, {'D', 0x02}, {'F', 0x03}, {'H', 0x04},
			{'G', 0x05}, {'Z', 0x06}, {'X', 0x07}, {'C', 0x08}, {'V', 0x09},
			{'B', 0x0b}, {'Q', 0x0c}, {'W', 0x0d}, {'E', 0x0e}, {'R', 0x0f},
			{'Y', 0x10}, {'T', 0x11}, {'O', 0x1f}, {'U', 0x20}, {'I', 0x22},
			{'P', 0x23}, {'L', 0x25}, {'J', 0x26}, {'K', 0x28}, {'N', 0x2d},
			{'M', 0x2e}};
		for (const auto& [key, code] : keys)
			if (key == virtual_key) return code;
	}
	switch (virtual_key) {
	case '1': return 0x12; case '2': return 0x13; case '3': return 0x14;
	case '4': return 0x15; case '6': return 0x16; case '5': return 0x17;
	case '9': return 0x19; case '7': return 0x1a; case '8': return 0x1c;
	case '0': return 0x1d;
	case VK_OEM_PLUS: return 0x18; case VK_OEM_MINUS: return 0x1b;
	case VK_OEM_6: return 0x1e; case VK_OEM_4: return 0x21;
	case VK_OEM_7: return 0x27; case VK_OEM_1: return 0x29;
	case VK_OEM_5: return 0x2a; case VK_OEM_COMMA: return 0x2b;
	case VK_OEM_2: return 0x2c; case VK_OEM_PERIOD: return 0x2f;
	case VK_TAB: return 0x30; case VK_SPACE: return 0x31;
	case VK_OEM_3: return 0x32; case VK_BACK: return 0x33;
	case VK_ESCAPE: return 0x35; case VK_RETURN: return 0x24;
	case VK_LWIN: return 0x37; case VK_RWIN: return 0x36;
	case VK_LSHIFT: return 0x38; case VK_RSHIFT: return 0x3c;
	case VK_SHIFT: return ((key_data >> 16) & 0xff) == 0x36 ? 0x3c : 0x38;
	case VK_CAPITAL: return 0x39;
	case VK_LMENU: return 0x3a; case VK_RMENU: return 0x3d;
	case VK_MENU: return extended ? 0x3d : 0x3a;
	case VK_LCONTROL: return 0x3b; case VK_RCONTROL: return 0x3e;
	case VK_CONTROL: return extended ? 0x3e : 0x3b;
	case VK_LEFT: return 0x7b; case VK_RIGHT: return 0x7c;
	case VK_DOWN: return 0x7d; case VK_UP: return 0x7e;
	case VK_HOME: return 0x73; case VK_END: return 0x77;
	case VK_PRIOR: return 0x74; case VK_NEXT: return 0x79;
	case VK_DELETE: return 0x75; case VK_INSERT: return 0x72;
	default: return extended ? 0xffff : static_cast<std::uint16_t>(virtual_key);
	}
}

std::string KeyCharacters(UINT virtual_key, LPARAM key_data,
	bool ignore_modifiers)
{
	BYTE state[256]{};
	if (!GetKeyboardState(state)) return {};
	if (ignore_modifiers) {
		for (const UINT key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL,
			VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU,
			VK_LWIN, VK_RWIN}) state[key] = 0;
	}
	wchar_t wide[8]{};
	const UINT scan = static_cast<UINT>((static_cast<std::uintptr_t>(key_data) >> 16) & 0xff);
	const int count = ToUnicodeEx(virtual_key, scan, state, wide,
		8, 4, GetKeyboardLayout(0));
	if (count <= 0) return {};
	std::string utf8;
	return ConvertWideToUtf8(std::wstring_view(wide,
			static_cast<std::size_t>((std::min)(count, 8))), utf8) ? utf8 : std::string{};
}

bool DispatchAppKitKeyEvent(HWND source_window, UINT message, WPARAM virtual_key,
	LPARAM key_data)
{
	HWND host_window = GetAncestor(source_window, GA_ROOT);
	if (host_window == nullptr) host_window = source_window;
	const auto window_object = reinterpret_cast<id>(GetWindowLongPtrW(host_window,
		GWLP_USERDATA));
	if (window_object == nullptr) return false;
	id source_view = nullptr;
	id event_target = nullptr;
	{
		std::lock_guard lock(g_windows_mutex);
		const auto control = g_controls.find(source_window);
		const auto window = g_windows.find(window_object);
		if (window != g_windows.end()) {
			if (control != g_controls.end()) {
				source_view = control->second;
				window->second.first_responder = source_view;
			}
			event_target = window->second.first_responder == nullptr ?
				window->second.content_view : window->second.first_responder;
		}
	}
	const bool key_down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	const bool modifier = virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT ||
		virtual_key == VK_RSHIFT || virtual_key == VK_CONTROL ||
		virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL ||
		virtual_key == VK_MENU || virtual_key == VK_LMENU ||
		virtual_key == VK_RMENU || virtual_key == VK_LWIN || virtual_key == VK_RWIN;
	const std::uint64_t type = modifier ? kEventFlagsChanged :
		key_down ? kEventKeyDown : kEventKeyUp;
	const char* selector_name = modifier ? "flagsChanged:" :
		key_down ? "keyDown:" : "keyUp:";
	const SEL callback = sel_registerName(selector_name);
	const bool app_overrides_send_event = AppKitApplicationOverridesSendEvent();
	if (!g_appkit_event_polling.load(std::memory_order_acquire) &&
		AppKitResponderForSelector(event_target, callback) == nullptr &&
		!app_overrides_send_event && !key_down) return false;
	std::uint64_t flags = 0;
	if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) flags |= 1ull << 17;
	if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) flags |= 1ull << 18;
	if ((GetKeyState(VK_MENU) & 0x8000) != 0) flags |= 1ull << 19;
	if ((GetKeyState(VK_LWIN) & 0x8000) != 0 ||
		(GetKeyState(VK_RWIN) & 0x8000) != 0) flags |= 1ull << 20;
	if ((GetKeyState(VK_CAPITAL) & 1) != 0) flags |= 1ull << 16;
	const auto code = DarlingKeyCode(virtual_key, key_data);
	const auto characters = KeyCharacters(static_cast<UINT>(virtual_key), key_data, false);
	const auto unmodified = KeyCharacters(static_cast<UINT>(virtual_key), key_data, true);
	id event = NSEventCreateKeyboard(objc_getClass("NSEvent"), type,
		window_object, flags, code, key_down && (key_data & (1ll << 30)) != 0,
		characters.c_str(), unmodified.c_str());
	if (event == nullptr) return false;
	{
		std::lock_guard lock(g_events_mutex);
		const auto found = g_events.find(event);
		if (found != g_events.end())
			found->second.keyboard_virtual_key = static_cast<UINT>(virtual_key);
	}
	if (QueueAppKitEventWhenPolling(event, type)) {
		objc_release(event);
		return true;
	}
	id application = g_shared_application;
	if (application != nullptr) DispatchAppKitEventToApplication(application, event);
	bool handled = false;
	{
		std::lock_guard lock(g_events_mutex);
		const auto found = g_events.find(event);
		if (found != g_events.end()) handled = found->second.keyboard_handled;
	}
	objc_release(event);
	return handled || app_overrides_send_event;
}

id NSNotificationCenterDefaultCenter(Class cls, SEL)
{
	std::lock_guard lock(g_notification_center_mutex);
	if (g_default_notification_center == nullptr && cls != nullptr)
		g_default_notification_center = class_createInstance(cls, 0);
	return g_default_notification_center == nullptr ? nullptr :
		objc_retainAutorelease(g_default_notification_center);
}

void NSNotificationCenterAddObserver(id self, SEL, id observer, SEL callback,
	id name, id object)
{
	if (self == nullptr || observer == nullptr || callback == nullptr ||
		self != g_default_notification_center) return;
	auto record = std::shared_ptr<AppKitNotificationObserver>();
	try {
		record = std::make_shared<AppKitNotificationObserver>();
		objc_initWeak(&record->observer, observer);
		objc_initWeak(&record->object_filter, object);
		record->selector = callback;
		record->has_object_filter = object != nullptr;
		if (name != nullptr) {
			const char* utf8 = NSStringUTF8String(name, nullptr);
			if (utf8 == nullptr) return;
			record->name = utf8;
			record->has_name = true;
		}
		std::lock_guard lock(g_notification_center_mutex);
		g_notification_observers.push_back(std::move(record));
	} catch (...) {
	}
}

void NSNotificationCenterRemoveObservers(id observer, id name, id object)
{
	const char* filter_name = name == nullptr ? nullptr :
		NSStringUTF8String(name, nullptr);
	if (name != nullptr && filter_name == nullptr) return;
	std::vector<std::shared_ptr<AppKitNotificationObserver>> snapshot;
	{
		std::lock_guard lock(g_notification_center_mutex);
		snapshot = g_notification_observers;
	}
	std::vector<std::shared_ptr<AppKitNotificationObserver>> removed;
	for (const auto& record : snapshot) {
		id registered_observer = objc_loadWeakRetained(&record->observer);
		const bool observer_matches = observer == nullptr ||
			registered_observer == observer;
		if (registered_observer != nullptr) objc_release(registered_observer);
		if (!observer_matches) continue;
		if (filter_name != nullptr && (!record->has_name ||
			record->name != filter_name)) continue;
		if (object != nullptr) {
			if (!record->has_object_filter) continue;
			id registered_object = objc_loadWeakRetained(&record->object_filter);
			const bool object_matches = registered_object == object;
			if (registered_object != nullptr) objc_release(registered_object);
			if (!object_matches) continue;
		}
		removed.push_back(record);
	}
	if (removed.empty()) return;
	{
		std::lock_guard lock(g_notification_center_mutex);
		std::erase_if(g_notification_observers, [&removed](const auto& record) {
			return std::find(removed.begin(), removed.end(), record) != removed.end();
		});
	}
}

void NSNotificationCenterRemoveObserver(id, SEL, id observer)
{
	if (observer == nullptr) return;
	NSNotificationCenterRemoveObservers(observer, nullptr, nullptr);
}

void NSNotificationCenterRemoveObserverForName(id, SEL, id observer,
	id name, id object)
{
	if (observer == nullptr) return;
	NSNotificationCenterRemoveObservers(observer, name, object);
}

void NSNotificationCenterDispatch(id notification)
{
	AppKitNotificationState state{};
	{
		std::lock_guard lock(g_notifications_mutex);
		const auto found = g_notifications.find(notification);
		if (found == g_notifications.end()) return;
		state = {objc_retain(found->second.name),
			found->second.object == nullptr ? nullptr : objc_retain(found->second.object),
			found->second.user_info == nullptr ? nullptr :
				objc_retain(found->second.user_info)};
	}
	const char* notification_name = NSStringUTF8String(state.name, nullptr);
	if (notification_name == nullptr) {
		objc_release(state.name);
		if (state.object != nullptr) objc_release(state.object);
		if (state.user_info != nullptr) objc_release(state.user_info);
		return;
	}
	const std::string name_copy(notification_name);
	std::vector<std::shared_ptr<AppKitNotificationObserver>> observers;
	{
		std::lock_guard lock(g_notification_center_mutex);
		observers = g_notification_observers;
	}
	for (const auto& record : observers) {
		if (record->has_name && record->name != name_copy) continue;
		id target = objc_loadWeakRetained(&record->observer);
		if (target == nullptr) continue;
		bool matches = true;
		if (record->has_object_filter) {
			id filter = objc_loadWeakRetained(&record->object_filter);
			matches = filter != nullptr && filter == state.object;
			if (filter != nullptr) objc_release(filter);
		}
		if (matches)
			darling_objc_msgSend_void1_object(target, record->selector, notification);
		objc_release(target);
	}
	objc_release(state.name);
	if (state.object != nullptr) objc_release(state.object);
	if (state.user_info != nullptr) objc_release(state.user_info);
}

void NSNotificationCenterPostNotification(id self, SEL, id notification)
{
	if (self == g_default_notification_center && notification != nullptr)
		NSNotificationCenterDispatch(notification);
}

void NSNotificationCenterPostNotificationName(id self, SEL, id name,
	id object)
{
	if (self != g_default_notification_center) return;
	id notification = NSNotificationFactory(objc_getClass("NSNotification"),
		nullptr, name, object);
	NSNotificationCenterDispatch(notification);
}

void NSNotificationCenterPostNotificationNameUserInfo(id self, SEL, id name,
	id object, id user_info)
{
	if (self != g_default_notification_center) return;
	id notification = NSNotificationFactoryWithUserInfo(
		objc_getClass("NSNotification"), nullptr, name, object, user_info);
	NSNotificationCenterDispatch(notification);
}

Class RegisterHostClass(const char* name, Class superclass)
{
	Class cls = objc_getClass(name);
	if (cls != nullptr) return cls;
	cls = objc_allocateClassPair(superclass, name, 0);
	if (cls == nullptr) return nullptr;
	objc_registerClassPair(cls);
	return cls;
}

void RegisterAppKitClasses()
{
	std::call_once(g_appkit_classes_once, [] {
		Class root = RegisterHostClass("NSObject", nullptr);
		if (root == nullptr) return;
		class_addClassMethod(root, sel_registerName("alloc"),
			reinterpret_cast<IMP>(&NSObjectAlloc), "@@:");
		class_addMethod(root, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSObjectInit), "@@:");
		class_addMethod(root, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSObjectInitWithCoder), "@@:@");
		class_addMethod(root, sel_registerName("retain"),
			reinterpret_cast<IMP>(&NSObjectRetain), "@@:");
		class_addMethod(root, sel_registerName("release"),
			reinterpret_cast<IMP>(&NSObjectRelease), "v@:");
		class_addMethod(root, sel_registerName("autorelease"),
			reinterpret_cast<IMP>(&NSObjectAutorelease), "@@:");

		Class autorelease_pool = RegisterHostClass("NSAutoreleasePool", root);
		if (autorelease_pool == nullptr) return;
		class_addMethod(autorelease_pool, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSAutoreleasePoolInit), "@@:");
		class_addMethod(autorelease_pool, sel_registerName("drain"),
			reinterpret_cast<IMP>(&NSAutoreleasePoolDrain), "v@:");
		class_addClassMethod(autorelease_pool, sel_registerName("new"),
			reinterpret_cast<IMP>(&NSAutoreleasePoolNew), "@@:");

		Class responder = RegisterHostClass("NSResponder", root);
		if (responder == nullptr) return;
		class_addMethod(responder, sel_registerName("nextResponder"),
			reinterpret_cast<IMP>(&NSResponderNextResponder), "@@:");
		class_addMethod(responder, sel_registerName("setNextResponder:"),
			reinterpret_cast<IMP>(&NSResponderSetNextResponder), "v@:@");
		class_addMethod(responder, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSResponderAcceptsFirstResponder), "B@:");
		class_addMethod(responder, sel_registerName("tryToPerform:with:"),
			reinterpret_cast<IMP>(&NSResponderTryToPerform), "B@::@");
		class_addMethod(responder, sel_registerName("performKeyEquivalent:"),
			reinterpret_cast<IMP>(&NSResponderPerformKeyEquivalent), "B@:@");
		class_addMethod(root, sel_registerName("class"),
			reinterpret_cast<IMP>(&NSObjectInstanceClass), "@@:");
		class_addMethod(root, sel_registerName("isKindOfClass:"),
			reinterpret_cast<IMP>(&NSObjectIsKindOfClass), "B@:#");
		class_addMethod(root, sel_registerName("isMemberOfClass:"),
			reinterpret_cast<IMP>(&NSObjectIsMemberOfClass), "B@:#");
		class_addMethod(root, sel_registerName("respondsToSelector:"),
			reinterpret_cast<IMP>(&NSObjectRespondsToSelector), "B@::");
		class_addMethod(root, sel_registerName("conformsToProtocol:"),
			reinterpret_cast<IMP>(&NSObjectConformsToProtocol), "B@:^v");
		class_addMethod(root, sel_registerName("isEqual:"),
			reinterpret_cast<IMP>(&NSObjectIsEqual), "B@:@");
		class_addMethod(root, sel_registerName("hash"),
			reinterpret_cast<IMP>(&NSObjectHash), "Q@:");
		class_addMethod(root, sel_registerName("description"),
			reinterpret_cast<IMP>(&NSObjectDescription), "@@:");
		class_addMethod(root, sel_registerName("debugDescription"),
			reinterpret_cast<IMP>(&NSObjectDescription), "@@:");
		class_addMethod(root, sel_registerName("copyWithZone:"),
			reinterpret_cast<IMP>(&NSObjectCopyWithZone), "@@:^v");
		class_addClassMethod(root, sel_registerName("class"),
			reinterpret_cast<IMP>(&NSObjectClassMethod), "@@:");
		class_addClassMethod(root, sel_registerName("allocWithZone:"),
			reinterpret_cast<IMP>(&NSObjectAllocWithZone), "@@:^v");
		class_addClassMethod(root, sel_registerName("new"),
			reinterpret_cast<IMP>(&NSObjectNew), "@@:");
		class_addClassMethod(root, sel_registerName("description"),
			reinterpret_cast<IMP>(&NSObjectClassDescription), "@@:");

		Class string = RegisterHostClass("NSString", root);
		if (string == nullptr) return;
		Class process_info = RegisterHostClass("NSProcessInfo", root);
		if (process_info == nullptr) return;
		class_addClassMethod(process_info, sel_registerName("processInfo"),
			reinterpret_cast<IMP>(&NSProcessInfoSingleton), "@@:");
		class_addMethod(process_info, sel_registerName("arguments"),
			reinterpret_cast<IMP>(&NSProcessInfoArguments), "@@:");
		class_addMethod(process_info, sel_registerName("environment"),
			reinterpret_cast<IMP>(&NSProcessInfoEnvironment), "@@:");
		class_addMethod(process_info, sel_registerName("processName"),
			reinterpret_cast<IMP>(&NSProcessInfoProcessName), "@@:");
		class_addMethod(process_info, sel_registerName("hostName"),
			reinterpret_cast<IMP>(&NSProcessInfoHostName), "@@:");
		class_addMethod(process_info, sel_registerName("processIdentifier"),
			reinterpret_cast<IMP>(&NSProcessInfoProcessIdentifier), "i@:");
		class_addMethod(process_info, sel_registerName("processorCount"),
			reinterpret_cast<IMP>(&NSProcessInfoProcessorCount), "Q@:");
		class_addMethod(process_info, sel_registerName("activeProcessorCount"),
			reinterpret_cast<IMP>(&NSProcessInfoActiveProcessorCount), "Q@:");
		class_addMethod(process_info, sel_registerName("systemUptime"),
			reinterpret_cast<IMP>(&NSProcessInfoSystemUptime), "d@:");
		class_addMethod(process_info, sel_registerName("userName"),
			reinterpret_cast<IMP>(&NSProcessInfoUserName), "@@:");
		class_addMethod(process_info, sel_registerName("fullUserName"),
			reinterpret_cast<IMP>(&NSProcessInfoFullUserName), "@@:");
		class_addMethod(process_info, sel_registerName("physicalMemory"),
			reinterpret_cast<IMP>(&NSProcessInfoPhysicalMemory), "Q@:");
		class_addMethod(process_info, sel_registerName("operatingSystemVersion"),
			reinterpret_cast<IMP>(&NSProcessInfoOperatingSystemVersion),
			"{NSOperatingSystemVersion=qqq}@:");
		class_addMethod(process_info, sel_registerName("operatingSystemVersionString"),
			reinterpret_cast<IMP>(&NSProcessInfoOperatingSystemVersionString), "@@:");
		class_addMethod(process_info, sel_registerName("operatingSystem"),
			reinterpret_cast<IMP>(&NSProcessInfoOperatingSystem), "Q@:");
		class_addMethod(process_info, sel_registerName("operatingSystemName"),
			reinterpret_cast<IMP>(&NSProcessInfoOperatingSystemName), "@@:");
		class_addMethod(process_info, sel_registerName("isOperatingSystemAtLeastVersion:"),
			reinterpret_cast<IMP>(&NSProcessInfoIsOperatingSystemAtLeastVersion),
			"B@:{NSOperatingSystemVersion=qqq}");
		Class user_defaults = RegisterHostClass("NSUserDefaults", root);
		if (user_defaults == nullptr) return;
		class_addClassMethod(user_defaults, sel_registerName("standardUserDefaults"),
			reinterpret_cast<IMP>(&NSUserDefaultsStandard), "@@:");
		class_addMethod(user_defaults, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSUserDefaultsInit), "@@:");
		class_addMethod(user_defaults, sel_registerName("initWithSuiteName:"),
			reinterpret_cast<IMP>(&NSUserDefaultsInitWithSuiteName), "@@:@");
		class_addMethod(user_defaults, sel_registerName("objectForKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsObjectForKey), "@@:@");
		class_addMethod(user_defaults, sel_registerName("stringForKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsStringForKey), "@@:@");
		class_addMethod(user_defaults, sel_registerName("integerForKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsIntegerForKey), "q@:@");
		class_addMethod(user_defaults, sel_registerName("doubleForKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsDoubleForKey), "d@:@");
		class_addMethod(user_defaults, sel_registerName("boolForKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsBoolForKey), "B@:@");
		class_addMethod(user_defaults, sel_registerName("setObject:forKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsSetObject), "v@:@@");
		class_addMethod(user_defaults, sel_registerName("setInteger:forKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsSetInteger), "v@:q@");
		class_addMethod(user_defaults, sel_registerName("setDouble:forKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsSetDouble), "v@:d@");
		class_addMethod(user_defaults, sel_registerName("setBool:forKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsSetBool), "v@:B@");
		class_addMethod(user_defaults, sel_registerName("removeObjectForKey:"),
			reinterpret_cast<IMP>(&NSUserDefaultsRemoveObject), "v@:@");
		class_addMethod(user_defaults, sel_registerName("synchronize"),
			reinterpret_cast<IMP>(&NSUserDefaultsSynchronize), "B@:");
		class_addMethod(user_defaults, sel_registerName("registerDefaults:"),
			reinterpret_cast<IMP>(&NSUserDefaultsRegisterDefaults), "v@:@");
		class_addMethod(user_defaults, sel_registerName("volatileDomainForName:"),
			reinterpret_cast<IMP>(&NSUserDefaultsVolatileDomainForName), "@@:@");
		class_addMethod(user_defaults, sel_registerName("setVolatileDomain:forName:"),
			reinterpret_cast<IMP>(&NSUserDefaultsSetVolatileDomain), "v@:@@");
		class_addMethod(user_defaults, sel_registerName("removeVolatileDomainForName:"),
			reinterpret_cast<IMP>(&NSUserDefaultsRemoveVolatileDomain), "v@:@");
		class_addMethod(user_defaults, sel_registerName("volatileDomainNames"),
			reinterpret_cast<IMP>(&NSUserDefaultsVolatileDomainNames), "@@:");
		if (NSApplicationWillFinishLaunchingNotification == nullptr) {
			NSApplicationWillFinishLaunchingNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationWillFinishLaunchingNotification"));
		}
		if (NSApplicationDidFinishLaunchingNotification == nullptr) {
			NSApplicationDidFinishLaunchingNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationDidFinishLaunchingNotification"));
		}
		if (NSApplicationWillTerminateNotification == nullptr) {
			NSApplicationWillTerminateNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationWillTerminateNotification"));
		}
		if (NSApplicationWillBecomeActiveNotification == nullptr)
			NSApplicationWillBecomeActiveNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationWillBecomeActiveNotification"));
		if (NSApplicationDidBecomeActiveNotification == nullptr)
			NSApplicationDidBecomeActiveNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationDidBecomeActiveNotification"));
		if (NSApplicationWillResignActiveNotification == nullptr)
			NSApplicationWillResignActiveNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationWillResignActiveNotification"));
		if (NSApplicationDidResignActiveNotification == nullptr)
			NSApplicationDidResignActiveNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationDidResignActiveNotification"));
		if (NSApplicationWillHideNotification == nullptr)
			NSApplicationWillHideNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationWillHideNotification"));
		if (NSApplicationDidHideNotification == nullptr)
			NSApplicationDidHideNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationDidHideNotification"));
		if (NSApplicationWillUnhideNotification == nullptr)
			NSApplicationWillUnhideNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationWillUnhideNotification"));
		if (NSApplicationDidUnhideNotification == nullptr)
			NSApplicationDidUnhideNotification = objc_retain(
				NSStringStringWithUTF8String(string, nullptr,
					"NSApplicationDidUnhideNotification"));
		if (NSWindowWillCloseNotification == nullptr)
			NSWindowWillCloseNotification = objc_retain(NSStringStringWithUTF8String(
				string, nullptr, "NSWindowWillCloseNotification"));
		if (NSWindowDidResizeNotification == nullptr)
			NSWindowDidResizeNotification = objc_retain(NSStringStringWithUTF8String(
				string, nullptr, "NSWindowDidResizeNotification"));
		if (NSWindowDidBecomeKeyNotification == nullptr)
			NSWindowDidBecomeKeyNotification = objc_retain(NSStringStringWithUTF8String(
				string, nullptr, "NSWindowDidBecomeKeyNotification"));
		if (NSWindowDidResignKeyNotification == nullptr)
			NSWindowDidResignKeyNotification = objc_retain(NSStringStringWithUTF8String(
				string, nullptr, "NSWindowDidResignKeyNotification"));
		if (NSWindowDidBecomeMainNotification == nullptr)
			NSWindowDidBecomeMainNotification = objc_retain(NSStringStringWithUTF8String(
				string, nullptr, "NSWindowDidBecomeMainNotification"));
		if (NSWindowDidResignMainNotification == nullptr)
			NSWindowDidResignMainNotification = objc_retain(NSStringStringWithUTF8String(
				string, nullptr, "NSWindowDidResignMainNotification"));
		Class notification = RegisterHostClass("NSNotification", root);
		if (notification == nullptr) return;
		class_addClassMethod(notification, sel_registerName("notificationWithName:object:"),
			reinterpret_cast<IMP>(&NSNotificationFactory), "@@:@@");
		class_addClassMethod(notification,
			sel_registerName("notificationWithName:object:userInfo:"),
			reinterpret_cast<IMP>(&NSNotificationFactoryWithUserInfo), "@@:@@@");
		class_addMethod(notification,
			sel_registerName("initWithName:object:userInfo:"),
			reinterpret_cast<IMP>(&NSNotificationInit), "@@:@@@");
		class_addMethod(notification, sel_registerName("name"),
			reinterpret_cast<IMP>(&NSNotificationName), "@@:");
		class_addMethod(notification, sel_registerName("object"),
			reinterpret_cast<IMP>(&NSNotificationObject), "@@:");
		class_addMethod(notification, sel_registerName("userInfo"),
			reinterpret_cast<IMP>(&NSNotificationUserInfo), "@@:");
		Class event = RegisterHostClass("NSEvent", root);
		if (event == nullptr) return;
		class_addMethod(event, sel_registerName("type"),
			reinterpret_cast<IMP>(&NSEventType), "Q@:");
		class_addMethod(event, sel_registerName("locationInWindow"),
			reinterpret_cast<IMP>(&NSEventLocationInWindow), "{CGPoint=dd}@:");
		class_addMethod(event, sel_registerName("modifierFlags"),
			reinterpret_cast<IMP>(&NSEventModifierFlags), "Q@:");
		class_addMethod(event, sel_registerName("timestamp"),
			reinterpret_cast<IMP>(&NSEventTimestamp), "d@:");
		class_addMethod(event, sel_registerName("window"),
			reinterpret_cast<IMP>(&NSEventWindow), "@@:");
		class_addMethod(event, sel_registerName("buttonNumber"),
			reinterpret_cast<IMP>(&NSEventButtonNumber), "q@:");
		class_addMethod(event, sel_registerName("clickCount"),
			reinterpret_cast<IMP>(&NSEventClickCount), "q@:");
		class_addMethod(event, sel_registerName("characters"),
			reinterpret_cast<IMP>(&NSEventCharacters), "@@:");
		class_addMethod(event, sel_registerName("charactersIgnoringModifiers"),
			reinterpret_cast<IMP>(&NSEventCharactersIgnoringModifiers), "@@:");
		class_addMethod(event, sel_registerName("keyCode"),
			reinterpret_cast<IMP>(&NSEventKeyCode), "S@:");
		class_addMethod(event, sel_registerName("isARepeat"),
			reinterpret_cast<IMP>(&NSEventIsARepeat), "B@:");
		class_addMethod(event, sel_registerName("deltaX"),
			reinterpret_cast<IMP>(&NSEventDeltaX), "d@:");
		class_addMethod(event, sel_registerName("deltaY"),
			reinterpret_cast<IMP>(&NSEventDeltaY), "d@:");
		class_addMethod(event, sel_registerName("scrollingDeltaX"),
			reinterpret_cast<IMP>(&NSEventScrollingDeltaX), "d@:");
		class_addMethod(event, sel_registerName("scrollingDeltaY"),
			reinterpret_cast<IMP>(&NSEventScrollingDeltaY), "d@:");
		class_addMethod(event, sel_registerName("hasPreciseScrollingDeltas"),
			reinterpret_cast<IMP>(&NSEventHasPreciseScrollingDeltas), "B@:");
		Class notification_center = RegisterHostClass("NSNotificationCenter", root);
		if (notification_center == nullptr) return;
		class_addClassMethod(notification_center, sel_registerName("defaultCenter"),
			reinterpret_cast<IMP>(&NSNotificationCenterDefaultCenter), "@@:");
		class_addMethod(notification_center,
			sel_registerName("addObserver:selector:name:object:"),
			reinterpret_cast<IMP>(&NSNotificationCenterAddObserver), "v@:@:@@");
		class_addMethod(notification_center, sel_registerName("removeObserver:"),
			reinterpret_cast<IMP>(&NSNotificationCenterRemoveObserver), "v@:@");
		class_addMethod(notification_center,
			sel_registerName("removeObserver:name:object:"),
			reinterpret_cast<IMP>(&NSNotificationCenterRemoveObserverForName),
			"v@:@@@");
		class_addMethod(notification_center, sel_registerName("postNotification:"),
			reinterpret_cast<IMP>(&NSNotificationCenterPostNotification), "v@:@");
		class_addMethod(notification_center,
			sel_registerName("postNotificationName:object:"),
			reinterpret_cast<IMP>(&NSNotificationCenterPostNotificationName),
			"v@:@@");
		class_addMethod(notification_center,
			sel_registerName("postNotificationName:object:userInfo:"),
			reinterpret_cast<IMP>(&NSNotificationCenterPostNotificationNameUserInfo),
			"v@:@@@");
	class_addMethod(string, sel_registerName("drawAtPoint:"),
		reinterpret_cast<IMP>(&NSStringDrawAtPoint), "v@:{CGPoint=dd}");
	class_addMethod(string, sel_registerName("drawAtPoint:withAttributes:"),
		reinterpret_cast<IMP>(&NSStringDrawAtPointWithAttributes),
		"v@:{CGPoint=dd}@");
	if (NSFontAttributeName == nullptr) {
		NSFontAttributeName = objc_retain(NSStringStringWithUTF8String(
			string, nullptr, "NSFont"));
	}
	if (NSForegroundColorAttributeName == nullptr) {
		NSForegroundColorAttributeName = objc_retain(NSStringStringWithUTF8String(
			string, nullptr, "NSColor"));
	}
		class_addMethod(string, sel_registerName("UTF8String"),
		reinterpret_cast<IMP>(&NSStringUTF8String), "*@:");
		class_addMethod(string, sel_registerName("length"),
		reinterpret_cast<IMP>(&NSStringLength), "Q@:");
	class_addMethod(string, sel_registerName("initWithUTF8String:"),
		reinterpret_cast<IMP>(&NSStringInitWithUTF8String), "@@:*");
	class_addClassMethod(string, sel_registerName("stringWithUTF8String:"),
		reinterpret_cast<IMP>(&NSStringStringWithUTF8String), "@@:*");
	class_addMethod(string, sel_registerName("initWithString:"),
		reinterpret_cast<IMP>(&NSStringInitWithString), "@@:@");
	class_addClassMethod(string, sel_registerName("stringWithString:"),
		reinterpret_cast<IMP>(&NSStringStringWithString), "@@:@");
	class_addMethod(string, sel_registerName("isEqualToString:"),
		reinterpret_cast<IMP>(&NSStringIsEqualToString), "B@:@");
	class_addMethod(string, sel_registerName("isEqual:"),
		reinterpret_cast<IMP>(&NSStringIsEqualToString), "B@:@");
	class_addMethod(string, sel_registerName("hash"),
		reinterpret_cast<IMP>(&NSStringHash), "Q@:");
	class_addMethod(string, sel_registerName("stringByAppendingString:"),
		reinterpret_cast<IMP>(&NSStringStringByAppendingString), "@@:@");
	class_addMethod(string, sel_registerName("hasPrefix:"),
		reinterpret_cast<IMP>(&NSStringHasPrefix), "B@:@");
	class_addMethod(string, sel_registerName("hasSuffix:"),
		reinterpret_cast<IMP>(&NSStringHasSuffix), "B@:@");
	class_addMethod(string, sel_registerName("substringWithRange:"),
		reinterpret_cast<IMP>(&NSStringSubstringWithRange),
		"@@:{_NSRange=QQ}");
	class_addMethod(string, sel_registerName("substringFromIndex:"),
		reinterpret_cast<IMP>(&NSStringSubstringFromIndex), "@@:Q");
	class_addMethod(string, sel_registerName("substringToIndex:"),
		reinterpret_cast<IMP>(&NSStringSubstringToIndex), "@@:Q");
	class_addMethod(string, sel_registerName("copyWithZone:"),
		reinterpret_cast<IMP>(&NSStringCopyWithZone), "@@:^v");
		class_addMethod(string, sel_registerName("description"),
			reinterpret_cast<IMP>(&NSStringDescription), "@@:");
		class_addMethod(string, sel_registerName("debugDescription"),
			reinterpret_cast<IMP>(&NSStringDescription), "@@:");
	class_addMethod(string, sel_registerName("mutableCopyWithZone:"),
		reinterpret_cast<IMP>(&NSStringMutableCopyWithZone), "@@:^v");

	Class attributed_string = RegisterHostClass("NSAttributedString", root);
	if (attributed_string == nullptr) return;
	class_addClassMethod(attributed_string,
		sel_registerName("attributedStringWithString:attributes:"),
		reinterpret_cast<IMP>(&NSAttributedStringFactory), "@@:@@");
	class_addMethod(attributed_string,
		sel_registerName("initWithString:attributes:"),
		reinterpret_cast<IMP>(&NSAttributedStringInit), "@@:@@");
	class_addMethod(attributed_string, sel_registerName("string"),
		reinterpret_cast<IMP>(&NSAttributedStringValue), "@@:");
	class_addMethod(attributed_string, sel_registerName("length"),
		reinterpret_cast<IMP>(&NSAttributedStringLength), "Q@:");
	class_addMethod(attributed_string, sel_registerName("drawAtPoint:"),
		reinterpret_cast<IMP>(&NSAttributedStringDrawAtPoint),
		"v@:{CGPoint=dd}");

	Class mutable_attributed_string = RegisterHostClass(
		"NSMutableAttributedString", attributed_string);
	if (mutable_attributed_string == nullptr) return;
	class_addClassMethod(mutable_attributed_string,
		sel_registerName("mutableAttributedStringWithString:attributes:"),
		reinterpret_cast<IMP>(&NSMutableAttributedStringFactory), "@@:@@");
	class_addMethod(mutable_attributed_string,
		sel_registerName("initWithString:attributes:"),
		reinterpret_cast<IMP>(&NSMutableAttributedStringInit), "@@:@@");
	class_addMethod(mutable_attributed_string, sel_registerName("mutableString"),
		reinterpret_cast<IMP>(&NSMutableAttributedStringMutableString), "@@:");
	class_addMethod(mutable_attributed_string,
		sel_registerName("replaceCharactersInRange:withString:"),
		reinterpret_cast<IMP>(&NSMutableAttributedStringReplaceCharactersInRange),
		"v@:{_NSRange=QQ}@");
	class_addMethod(mutable_attributed_string,
		sel_registerName("setAttributes:range:"),
		reinterpret_cast<IMP>(&NSMutableAttributedStringSetAttributes),
		"v@:@{_NSRange=QQ}");

	Class font = RegisterHostClass("NSFont", root);
	if (font == nullptr) return;
	class_addClassMethod(font, sel_registerName("systemFontOfSize:"),
		reinterpret_cast<IMP>(&NSFontSystemFontOfSize), "@@:d");
	class_addClassMethod(font, sel_registerName("boldSystemFontOfSize:"),
		reinterpret_cast<IMP>(&NSFontBoldSystemFontOfSize), "@@:d");
	class_addClassMethod(font, sel_registerName("fontWithName:size:"),
		reinterpret_cast<IMP>(&NSFontWithNameSize), "@@:@d");
	class_addMethod(font, sel_registerName("set"),
		reinterpret_cast<IMP>(&NSFontSet), "v@:");

	Class mutable_string = RegisterHostClass("NSMutableString", string);
	if (mutable_string == nullptr) return;
	class_addClassMethod(mutable_string, sel_registerName("string"),
		reinterpret_cast<IMP>(&NSMutableStringString), "@@:");
	class_addClassMethod(mutable_string, sel_registerName("stringWithUTF8String:"),
		reinterpret_cast<IMP>(&NSMutableStringStringWithUTF8String), "@@:*");
	class_addClassMethod(mutable_string, sel_registerName("stringWithString:"),
		reinterpret_cast<IMP>(&NSMutableStringStringWithString), "@@:@");
	class_addMethod(mutable_string, sel_registerName("initWithUTF8String:"),
		reinterpret_cast<IMP>(&NSMutableStringInitWithUTF8String), "@@:*");
	class_addMethod(mutable_string, sel_registerName("initWithString:"),
		reinterpret_cast<IMP>(&NSMutableStringInitWithString), "@@:@");
	class_addMethod(mutable_string, sel_registerName("appendString:"),
		reinterpret_cast<IMP>(&NSMutableStringAppendString), "v@:@");
	class_addMethod(mutable_string, sel_registerName("setString:"),
		reinterpret_cast<IMP>(&NSMutableStringSetString), "v@:@");
	class_addMethod(mutable_string, sel_registerName("deleteCharactersInRange:"),
		reinterpret_cast<IMP>(&NSMutableStringDeleteCharactersInRange),
		"v@:{_NSRange=QQ}");
	class_addMethod(mutable_string, sel_registerName("replaceCharactersInRange:withString:"),
		reinterpret_cast<IMP>(&NSMutableStringReplaceCharactersInRange),
		"v@:{_NSRange=QQ}@");
	class_addMethod(mutable_string, sel_registerName("insertString:atIndex:"),
		reinterpret_cast<IMP>(&NSMutableStringInsertString), "v@:@Q");
	class_addMethod(mutable_string, sel_registerName("copyWithZone:"),
		reinterpret_cast<IMP>(&NSStringCopyWithZone), "@@:^v");
		Class constant_string = RegisterHostClass("NSConstantString", string);
		if (constant_string == nullptr) return;

		Class array = RegisterHostClass("NSArray", root);
		if (array == nullptr) return;
		class_addClassMethod(array, sel_registerName("array"),
			reinterpret_cast<IMP>(&NSArrayArray), "@@:");
		class_addClassMethod(array, sel_registerName("arrayWithObjects:count:"),
			reinterpret_cast<IMP>(&NSArrayArrayWithObjectsCount), "@@:^vQ");
		class_addClassMethod(array, sel_registerName("arrayWithObject:"),
			reinterpret_cast<IMP>(&NSArrayArrayWithObject), "@@:@");
		class_addMethod(array, sel_registerName("count"),
			reinterpret_cast<IMP>(&NSArrayCount), "Q@:");
		class_addMethod(array, sel_registerName("initWithObjects:count:"),
			reinterpret_cast<IMP>(&NSArrayInitWithObjectsCount), "@@:^vQ");
		class_addMethod(array, sel_registerName("objectAtIndex:"),
			reinterpret_cast<IMP>(&NSArrayObjectAtIndex), "@@:Q");
		class_addMethod(array, sel_registerName("containsObject:"),
			reinterpret_cast<IMP>(&NSArrayContainsObject), "B@:@");
		Class mutable_array = RegisterHostClass("NSMutableArray", array);
		if (mutable_array == nullptr) return;
		class_addMethod(mutable_array, sel_registerName("addObject:"),
			reinterpret_cast<IMP>(&NSMutableArrayAddObject), "v@:@");
		class_addMethod(mutable_array, sel_registerName("insertObject:atIndex:"),
			reinterpret_cast<IMP>(&NSMutableArrayInsertObjectAtIndex), "v@:@Q");
		class_addMethod(mutable_array, sel_registerName("removeObjectAtIndex:"),
			reinterpret_cast<IMP>(&NSMutableArrayRemoveObjectAtIndex), "v@:Q");
		class_addMethod(mutable_array, sel_registerName("replaceObjectAtIndex:withObject:"),
			reinterpret_cast<IMP>(&NSMutableArrayReplaceObjectAtIndex), "v@:Q@");
		class_addMethod(mutable_array, sel_registerName("removeAllObjects"),
			reinterpret_cast<IMP>(&NSMutableArrayRemoveAllObjects), "v@:");

		Class set = RegisterHostClass("NSSet", root);
		if (set == nullptr) return;
		class_addClassMethod(set, sel_registerName("set"),
			reinterpret_cast<IMP>(&NSSetSet), "@@:");
		class_addClassMethod(set, sel_registerName("setWithObjects:count:"),
			reinterpret_cast<IMP>(&NSSetSetWithObjectsCount), "@@:^vQ");
		class_addClassMethod(set, sel_registerName("setWithObject:"),
			reinterpret_cast<IMP>(&NSSetSetWithObject), "@@:@");
		class_addMethod(set, sel_registerName("count"),
			reinterpret_cast<IMP>(&NSArrayCount), "Q@:");
		class_addMethod(set, sel_registerName("containsObject:"),
			reinterpret_cast<IMP>(&NSArrayContainsObject), "B@:@");
		class_addMethod(set, sel_registerName("member:"),
			reinterpret_cast<IMP>(&NSSetMember), "@@:@");
		class_addMethod(set, sel_registerName("allObjects"),
			reinterpret_cast<IMP>(&NSSetAllObjects), "@@:");
		Class mutable_set = RegisterHostClass("NSMutableSet", set);
		if (mutable_set == nullptr) return;
		class_addMethod(mutable_set, sel_registerName("addObject:"),
			reinterpret_cast<IMP>(&NSMutableSetAddObject), "v@:@");
		class_addMethod(mutable_set, sel_registerName("removeObject:"),
			reinterpret_cast<IMP>(&NSMutableSetRemoveObject), "v@:@");
		class_addMethod(mutable_set, sel_registerName("removeAllObjects"),
			reinterpret_cast<IMP>(&NSMutableSetRemoveAllObjects), "v@:");

		Class dictionary = RegisterHostClass("NSDictionary", root);
		if (dictionary == nullptr) return;
		class_addClassMethod(dictionary, sel_registerName("dictionary"),
			reinterpret_cast<IMP>(&NSDictionaryDictionary), "@@:");
		class_addClassMethod(dictionary,
			sel_registerName("dictionaryWithObjects:forKeys:count:"),
			reinterpret_cast<IMP>(&NSDictionaryDictionaryWithObjectsKeysCount),
			"@@:^v^vQ");
		class_addMethod(dictionary, sel_registerName("count"),
			reinterpret_cast<IMP>(&NSDictionaryCount), "Q@:");
		class_addMethod(dictionary,
			sel_registerName("initWithObjects:forKeys:count:"),
			reinterpret_cast<IMP>(&NSDictionaryInitWithObjectsKeysCount),
			"@@:^v^vQ");
		class_addMethod(dictionary, sel_registerName("objectForKey:"),
			reinterpret_cast<IMP>(&NSDictionaryObjectForKey), "@@:@");
		class_addMethod(dictionary, sel_registerName("allKeys"),
			reinterpret_cast<IMP>(&NSDictionaryAllKeys), "@@:");
		class_addMethod(dictionary, sel_registerName("allValues"),
			reinterpret_cast<IMP>(&NSDictionaryAllValues), "@@:");
		Class mutable_dictionary = RegisterHostClass("NSMutableDictionary",
			dictionary);
		if (mutable_dictionary == nullptr) return;
		class_addMethod(mutable_dictionary, sel_registerName("setObject:forKey:"),
			reinterpret_cast<IMP>(&NSMutableDictionarySetObjectForKey), "v@:@@");
		class_addMethod(mutable_dictionary, sel_registerName("removeObjectForKey:"),
			reinterpret_cast<IMP>(&NSMutableDictionaryRemoveObjectForKey), "v@:@");
		class_addMethod(mutable_dictionary, sel_registerName("removeAllObjects"),
			reinterpret_cast<IMP>(&NSMutableDictionaryRemoveAllObjects), "v@:");

		Class data = RegisterHostClass("NSData", root);
		if (data == nullptr) return;
		class_addClassMethod(data, sel_registerName("dataWithBytes:length:"),
			reinterpret_cast<IMP>(&NSDataWithBytesLength), "@@:^vQ");
		class_addMethod(data, sel_registerName("initWithBytes:length:"),
			reinterpret_cast<IMP>(&NSDataInitWithBytesLength), "@@:^vQ");
		class_addMethod(data, sel_registerName("bytes"),
			reinterpret_cast<IMP>(&NSDataBytes), "^v@:");
		class_addMethod(data, sel_registerName("getBytes:length:"),
			reinterpret_cast<IMP>(&NSDataGetBytesLength), "v@:^vQ");
		class_addMethod(data, sel_registerName("subdataWithRange:"),
			reinterpret_cast<IMP>(&NSDataSubdataWithRange),
			"@@:{_NSRange=QQ}");
		class_addMethod(data, sel_registerName("length"),
			reinterpret_cast<IMP>(&NSDataLength), "Q@:");
		Class mutable_data = RegisterHostClass("NSMutableData", data);
		if (mutable_data == nullptr) return;
		class_addClassMethod(mutable_data, sel_registerName("data"),
			reinterpret_cast<IMP>(&NSMutableDataData), "@@:");
		class_addClassMethod(mutable_data, sel_registerName("dataWithLength:"),
			reinterpret_cast<IMP>(&NSMutableDataDataWithLength), "@@:Q");
		class_addMethod(mutable_data, sel_registerName("setLength:"),
			reinterpret_cast<IMP>(&NSMutableDataSetLength), "v@:Q");
		class_addMethod(mutable_data, sel_registerName("appendBytes:length:"),
			reinterpret_cast<IMP>(&NSMutableDataAppendBytesLength), "v@:^vQ");
		class_addMethod(mutable_data, sel_registerName("mutableBytes"),
			reinterpret_cast<IMP>(&NSMutableDataMutableBytes), "^v@:");
		class_addMethod(mutable_data,
			sel_registerName("replaceBytesInRange:withBytes:length:"),
			reinterpret_cast<IMP>(&NSMutableDataReplaceBytesInRange),
			"v@:{_NSRange=QQ}^vQ");

		Class keyed_unarchiver = RegisterHostClass("NSKeyedUnarchiver", root);
		if (keyed_unarchiver == nullptr) return;
		class_addClassMethod(keyed_unarchiver,
			sel_registerName("unarchiveObjectWithData:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverUnarchiveObjectWithData),
			"@@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("allowsKeyedCoding"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverAllowsKeyedCoding), "B@:");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeObjectForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeObjectForKey), "@@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("containsValueForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverContainsValueForKey), "B@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeBoolForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeBoolForKey), "B@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeIntForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeIntForKey), "i@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeIntegerForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeIntegerForKey), "q@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeDoubleForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeDoubleForKey), "d@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeFloatForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeFloatForKey), "f@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeInt32ForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeInt32ForKey), "i@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeInt64ForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeInt64ForKey), "q@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeUInt32ForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeUInt32ForKey), "I@:@");
		class_addMethod(keyed_unarchiver, sel_registerName("decodeUInt64ForKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeUInt64ForKey), "Q@:@");
		class_addMethod(keyed_unarchiver,
			sel_registerName("decodeObjectOfClass:forKey:"),
			reinterpret_cast<IMP>(&NSKeyedUnarchiverDecodeObjectOfClassForKey),
			"@@:#@");

		Class number = RegisterHostClass("NSNumber", root);
		if (number == nullptr) return;
		class_addClassMethod(number, sel_registerName("numberWithInt:"),
			reinterpret_cast<IMP>(&NSNumberWithInt), "@@:i");
		class_addClassMethod(number, sel_registerName("numberWithInteger:"),
			reinterpret_cast<IMP>(&NSNumberWithInteger), "@@:q");
		class_addClassMethod(number, sel_registerName("numberWithUnsignedInteger:"),
			reinterpret_cast<IMP>(&NSNumberWithUnsignedInteger), "@@:Q");
		class_addClassMethod(number, sel_registerName("numberWithDouble:"),
			reinterpret_cast<IMP>(&NSNumberWithDouble), "@@:d");
		class_addClassMethod(number, sel_registerName("numberWithBool:"),
			reinterpret_cast<IMP>(&NSNumberWithBool), "@@:B");
		class_addMethod(number, sel_registerName("intValue"),
			reinterpret_cast<IMP>(&NSNumberIntValue), "i@:");
		class_addMethod(number, sel_registerName("integerValue"),
			reinterpret_cast<IMP>(&NSNumberIntegerValue), "q@:");
		class_addMethod(number, sel_registerName("unsignedIntegerValue"),
			reinterpret_cast<IMP>(&NSNumberUnsignedIntegerValue), "Q@:");
		class_addMethod(number, sel_registerName("doubleValue"),
			reinterpret_cast<IMP>(&NSNumberDoubleValue), "d@:");
		class_addMethod(number, sel_registerName("boolValue"),
			reinterpret_cast<IMP>(&NSNumberBoolValue), "B@:");
		class_addMethod(number, sel_registerName("stringValue"),
			reinterpret_cast<IMP>(&NSNumberStringValue), "@@:");
		class_addMethod(number, sel_registerName("isEqual:"),
			reinterpret_cast<IMP>(&NSNumberIsEqual), "B@:@");

		Class date = RegisterHostClass("NSDate", root);
		if (date == nullptr) return;
		class_addClassMethod(date,
			sel_registerName("dateWithTimeIntervalSinceReferenceDate:"),
			reinterpret_cast<IMP>(&NSDateWithReferenceTime), "@@:d");
		class_addClassMethod(date, sel_registerName("date"),
			reinterpret_cast<IMP>(&NSDateNow), "@@:");
		class_addClassMethod(date, sel_registerName("distantFuture"),
			reinterpret_cast<IMP>(&NSDateDistantFuture), "@@:");
		class_addClassMethod(date, sel_registerName("distantPast"),
			reinterpret_cast<IMP>(&NSDateDistantPast), "@@:");
		class_addClassMethod(date,
			sel_registerName("timeIntervalSinceReferenceDate"),
			reinterpret_cast<IMP>(&NSDateClassReferenceTime), "d@:");
		class_addClassMethod(date,
			sel_registerName("dateWithTimeIntervalSinceNow:"),
			reinterpret_cast<IMP>(&NSDateWithTimeIntervalSinceNow), "@@:d");
		class_addClassMethod(date,
			sel_registerName("dateWithTimeIntervalSince1970:"),
			reinterpret_cast<IMP>(&NSDateWithTimeIntervalSince1970), "@@:d");
		class_addClassMethod(date,
			sel_registerName("dateWithTimeInterval:sinceDate:"),
			reinterpret_cast<IMP>(&NSDateWithTimeIntervalSinceDate), "@@:d@");
		class_addMethod(date,
			sel_registerName("initWithTimeIntervalSinceReferenceDate:"),
			reinterpret_cast<IMP>(&NSDateInitWithReferenceTime), "@@:d");
		class_addMethod(date,
			sel_registerName("initWithTimeIntervalSinceNow:"),
			reinterpret_cast<IMP>(&NSDateInitWithTimeIntervalSinceNow), "@@:d");
		class_addMethod(date,
			sel_registerName("initWithTimeIntervalSince1970:"),
			reinterpret_cast<IMP>(&NSDateInitWithTimeIntervalSince1970), "@@:d");
		class_addMethod(date,
			sel_registerName("initWithTimeInterval:sinceDate:"),
			reinterpret_cast<IMP>(&NSDateInitWithTimeIntervalSinceDate), "@@:d@");
		class_addMethod(date,
			sel_registerName("timeIntervalSinceReferenceDate"),
			reinterpret_cast<IMP>(&NSDateReferenceTime), "d@:");
		class_addMethod(date, sel_registerName("timeIntervalSinceNow"),
			reinterpret_cast<IMP>(&NSDateTimeIntervalSinceNow), "d@:");
		class_addMethod(date, sel_registerName("timeIntervalSince1970"),
			reinterpret_cast<IMP>(&NSDateTimeIntervalSince1970), "d@:");
		class_addMethod(date, sel_registerName("timeIntervalSinceDate:"),
			reinterpret_cast<IMP>(&NSDateTimeIntervalSinceDate), "d@:@");
		class_addMethod(date, sel_registerName("dateByAddingTimeInterval:"),
			reinterpret_cast<IMP>(&NSDateByAddingTimeInterval), "@@:d");
		class_addMethod(date, sel_registerName("earlierDate:"),
			reinterpret_cast<IMP>(&NSDateEarlierDate), "@@:@");
		class_addMethod(date, sel_registerName("laterDate:"),
			reinterpret_cast<IMP>(&NSDateLaterDate), "@@:@");
		class_addMethod(date, sel_registerName("compare:"),
			reinterpret_cast<IMP>(&NSDateCompare), "q@:@");
		class_addMethod(date, sel_registerName("isEqual:"),
			reinterpret_cast<IMP>(&NSDateIsEqual), "B@:@");
		class_addMethod(date, sel_registerName("isEqualToDate:"),
			reinterpret_cast<IMP>(&NSDateIsEqual), "B@:@");

		Class timer = RegisterHostClass("NSTimer", root);
		if (timer == nullptr) return;
		class_addClassMethod(timer,
			sel_registerName("timerWithTimeInterval:target:selector:userInfo:repeats:"),
			reinterpret_cast<IMP>(&NSTimerWithTimeInterval), "@@:d@::@B");
		class_addClassMethod(timer,
			sel_registerName("scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:"),
			reinterpret_cast<IMP>(&NSTimerScheduledWithTimeInterval), "@@:d@::@B");
		class_addMethod(timer, sel_registerName("fire"),
			reinterpret_cast<IMP>(&NSTimerFire), "v@:");
		class_addMethod(timer, sel_registerName("invalidate"),
			reinterpret_cast<IMP>(&NSTimerInvalidate), "v@:");
		class_addMethod(timer, sel_registerName("isValid"),
			reinterpret_cast<IMP>(&NSTimerIsValid), "B@:");
		class_addMethod(timer, sel_registerName("timeInterval"),
			reinterpret_cast<IMP>(&NSTimerTimeInterval), "d@:");
		class_addMethod(timer, sel_registerName("fireDate"),
			reinterpret_cast<IMP>(&NSTimerFireDate), "@@:");
		class_addMethod(timer, sel_registerName("setFireDate:"),
			reinterpret_cast<IMP>(&NSTimerSetFireDate), "v@:@");
		class_addMethod(timer, sel_registerName("userInfo"),
			reinterpret_cast<IMP>(&NSTimerUserInfo), "@@:");

		Class run_loop = RegisterHostClass("NSRunLoop", root);
		if (run_loop == nullptr) return;
		class_addClassMethod(run_loop, sel_registerName("currentRunLoop"),
			reinterpret_cast<IMP>(&NSRunLoopCurrent), "@@:");
		class_addClassMethod(run_loop, sel_registerName("mainRunLoop"),
			reinterpret_cast<IMP>(&NSRunLoopMain), "@@:");
		class_addClassMethod(run_loop, sel_registerName("defaultMode"),
			reinterpret_cast<IMP>(&NSRunLoopDefaultMode), "@@:");
		class_addClassMethod(run_loop, sel_registerName("commonModes"),
			reinterpret_cast<IMP>(&NSRunLoopCommonModes), "@@:");
		class_addMethod(run_loop, sel_registerName("getCFRunLoop"),
			reinterpret_cast<IMP>(&NSRunLoopGetCFRunLoop), "^v@:");
		class_addMethod(run_loop, sel_registerName("run"),
			reinterpret_cast<IMP>(&NSRunLoopRun), "v@:");
		class_addMethod(run_loop, sel_registerName("runUntilDate:"),
			reinterpret_cast<IMP>(&NSRunLoopRunUntilDate), "v@:@");
		class_addMethod(run_loop, sel_registerName("runMode:beforeDate:"),
			reinterpret_cast<IMP>(&NSRunLoopRunMode), "B@:@@");
		class_addMethod(run_loop,
			sel_registerName("acceptInputForMode:beforeDate:"),
			reinterpret_cast<IMP>(&NSRunLoopRunMode), "B@:@@");
		class_addMethod(run_loop,
			sel_registerName("performSelector:target:argument:order:modes:"),
			reinterpret_cast<IMP>(&NSRunLoopPerformSelector), "v@::@@Q@");

		Class null_class = RegisterHostClass("NSNull", root);
		if (null_class == nullptr) return;
		class_addClassMethod(null_class, sel_registerName("null"),
			reinterpret_cast<IMP>(&NSNullSingleton), "@@:");

		Class bundle = RegisterHostClass("NSBundle", root);
		if (bundle == nullptr) return;
		class_addClassMethod(bundle, sel_registerName("mainBundle"),
			reinterpret_cast<IMP>(&NSBundleMainBundle), "@@:");
		class_addClassMethod(bundle, sel_registerName("loadNibNamed:owner:"),
			reinterpret_cast<IMP>(&NSBundleLoadNibNamedClass), "B@:@@");
		class_addMethod(bundle,
			sel_registerName("loadNibNamed:owner:topLevelObjects:"),
			reinterpret_cast<IMP>(&NSBundleLoadNibNamedInstance),
			"B@:@@^@");
		Class nib = RegisterHostClass("NSNib", root);
		if (nib == nullptr) return;
		class_addMethod(nib, sel_registerName("initWithNibNamed:bundle:"),
			reinterpret_cast<IMP>(&NSNibInitWithNameBundle), "@@:@@");
		class_addMethod(nib, sel_registerName("initWithNibData:bundle:"),
			reinterpret_cast<IMP>(&NSNibInitWithDataBundle), "@@:@@");
		class_addMethod(nib,
			sel_registerName("instantiateWithOwner:topLevelObjects:"),
			reinterpret_cast<IMP>(&NSNibInstantiateWithOwner), "B@:@^@");
		class_addClassMethod(bundle, sel_registerName("bundleWithPath:"),
			reinterpret_cast<IMP>(&NSBundleWithPath), "@@:@");
		class_addClassMethod(bundle, sel_registerName("bundleWithURL:"),
			reinterpret_cast<IMP>(&NSBundleWithURL), "@@:@");
		class_addClassMethod(bundle, sel_registerName("bundleWithIdentifier:"),
			reinterpret_cast<IMP>(&NSBundleWithIdentifier), "@@:@");
		class_addMethod(bundle, sel_registerName("initWithPath:"),
			reinterpret_cast<IMP>(&NSBundleInitWithPath), "@@:@");
		class_addMethod(bundle, sel_registerName("initWithURL:"),
			reinterpret_cast<IMP>(&NSBundleInitWithURL), "@@:@");
		class_addMethod(bundle, sel_registerName("bundlePath"),
			reinterpret_cast<IMP>(&NSBundleBundlePath), "@@:");
		class_addMethod(bundle, sel_registerName("bundleURL"),
			reinterpret_cast<IMP>(&NSBundleBundleURL), "@@:");
		class_addMethod(bundle, sel_registerName("executablePath"),
			reinterpret_cast<IMP>(&NSBundleExecutablePath), "@@:");
		class_addMethod(bundle, sel_registerName("executableURL"),
			reinterpret_cast<IMP>(&NSBundleExecutableURL), "@@:");
		class_addMethod(bundle, sel_registerName("executableArchitectures"),
			reinterpret_cast<IMP>(&NSBundleExecutableArchitectures), "@@:");
		class_addMethod(bundle, sel_registerName("resourcePath"),
			reinterpret_cast<IMP>(&NSBundleResourcePath), "@@:");
		class_addMethod(bundle, sel_registerName("resourceURL"),
			reinterpret_cast<IMP>(&NSBundleResourceURL), "@@:");
		class_addMethod(bundle, sel_registerName("bundleIdentifier"),
			reinterpret_cast<IMP>(&NSBundleIdentifier), "@@:");
		class_addMethod(bundle, sel_registerName("infoDictionary"),
			reinterpret_cast<IMP>(&NSBundleInfoDictionary), "@@:");
		class_addMethod(bundle, sel_registerName("principalClass"),
			reinterpret_cast<IMP>(&NSBundlePrincipalClass), "#@:");
		class_addMethod(bundle, sel_registerName("classNamed:"),
			reinterpret_cast<IMP>(&NSBundleClassNamed), "#@:@");
		class_addMethod(bundle, sel_registerName("objectForInfoDictionaryKey:"),
			reinterpret_cast<IMP>(&NSBundleObjectForInfoDictionaryKey), "@@:@");
		class_addMethod(bundle, sel_registerName("pathForResource:ofType:"),
			reinterpret_cast<IMP>(&NSBundlePathForResourceType), "@@:@@");
		class_addMethod(bundle,
			sel_registerName("pathForResource:ofType:inDirectory:"),
			reinterpret_cast<IMP>(&NSBundlePathForResourceTypeInDirectory),
			"@@:@@@");
		class_addMethod(bundle, sel_registerName("URLForResource:withExtension:"),
			reinterpret_cast<IMP>(&NSBundleURLForResource), "@@:@@");
		class_addMethod(bundle,
			sel_registerName("URLForResource:withExtension:subdirectory:"),
			reinterpret_cast<IMP>(&NSBundleURLForResource), "@@:@@@");
		class_addMethod(bundle,
			sel_registerName("localizedStringForKey:value:table:"),
			reinterpret_cast<IMP>(&NSBundleLocalizedString), "@@:@@@");
		class_addMethod(bundle, sel_registerName("load"),
			reinterpret_cast<IMP>(&NSBundleLoad), "B@:");
		class_addMethod(bundle, sel_registerName("preflightAndReturnError:"),
			reinterpret_cast<IMP>(&NSBundlePreflightAndReturnError), "B@:^@");
		class_addMethod(bundle, sel_registerName("loadAndReturnError:"),
			reinterpret_cast<IMP>(&NSBundleLoadAndReturnError), "B@:^@");
		class_addMethod(bundle, sel_registerName("isLoaded"),
			reinterpret_cast<IMP>(&NSBundleIsLoaded), "B@:");
		class_addMethod(bundle, sel_registerName("unload"),
			reinterpret_cast<IMP>(&NSBundleUnload), "v@:");

		Class graphics_context = RegisterHostClass("NSGraphicsContext", root);
		if (graphics_context == nullptr) return;
		class_addClassMethod(graphics_context,
			sel_registerName("currentContext"),
			reinterpret_cast<IMP>(&NSGraphicsContextCurrentContext), "@@:");
		class_addClassMethod(graphics_context,
			sel_registerName("saveGraphicsState"),
			reinterpret_cast<IMP>(&NSGraphicsContextSaveGraphicsState), "v@:");
		class_addClassMethod(graphics_context,
			sel_registerName("restoreGraphicsState"),
			reinterpret_cast<IMP>(&NSGraphicsContextRestoreGraphicsState), "v@:");
		class_addMethod(graphics_context, sel_registerName("CGContext"),
			reinterpret_cast<IMP>(&NSGraphicsContextCGContext), "^v@:");

		Class color = RegisterHostClass("NSColor", root);
		if (color == nullptr) return;
		for (const char* name : {"blackColor", "whiteColor", "redColor",
			"greenColor", "blueColor", "grayColor", "lightGrayColor",
			"darkGrayColor", "yellowColor", "cyanColor", "magentaColor",
			"orangeColor", "purpleColor", "brownColor", "clearColor"})
			class_addClassMethod(color, sel_registerName(name),
				reinterpret_cast<IMP>(&NSColorNamed), "@@:");
		class_addClassMethod(color,
			sel_registerName("colorWithCalibratedRed:green:blue:alpha:"),
			reinterpret_cast<IMP>(&NSColorWithComponents), "@@:dddd");
		class_addClassMethod(color,
			sel_registerName("colorWithDeviceRed:green:blue:alpha:"),
			reinterpret_cast<IMP>(&NSColorWithComponents), "@@:dddd");
		class_addClassMethod(color,
			sel_registerName("colorWithSRGBRed:green:blue:alpha:"),
			reinterpret_cast<IMP>(&NSColorWithComponents), "@@:dddd");
		for (const char* name : {"colorWithCalibratedWhite:alpha:",
			"colorWithDeviceWhite:alpha:", "colorWithWhite:alpha:"})
			class_addClassMethod(color, sel_registerName(name),
				reinterpret_cast<IMP>(&NSColorWithWhite), "@@:dd");
		for (const char* name : {
			"colorWithCalibratedHue:saturation:brightness:alpha:",
			"colorWithDeviceHue:saturation:brightness:alpha:"})
			class_addClassMethod(color, sel_registerName(name),
				reinterpret_cast<IMP>(&NSColorWithHSB), "@@:dddd");
		class_addClassMethod(color,
			sel_registerName("colorWithDeviceCyan:magenta:yellow:black:alpha:"),
			reinterpret_cast<IMP>(&NSColorWithCMYK), "@@:ddddd");
		class_addMethod(color, sel_registerName("colorWithAlphaComponent:"),
			reinterpret_cast<IMP>(&NSColorWithAlphaComponent), "@@:d");
		class_addMethod(color,
			sel_registerName("blendedColorWithFraction:ofColor:"),
		reinterpret_cast<IMP>(&NSColorBlendedWithFraction), "@@:d@");
		class_addMethod(color, sel_registerName("set"),
			reinterpret_cast<IMP>(&NSColorSet), "v@:");
		class_addMethod(color, sel_registerName("setFill"),
			reinterpret_cast<IMP>(&NSColorSetFill), "v@:");
		class_addMethod(color, sel_registerName("setStroke"),
			reinterpret_cast<IMP>(&NSColorSetStroke), "v@:");
		for (const char* name : {"redComponent", "greenComponent",
			"blueComponent", "alphaComponent", "whiteComponent",
			"hueComponent", "saturationComponent", "brightnessComponent"})
			class_addMethod(color, sel_registerName(name),
				reinterpret_cast<IMP>(&NSColorComponent), "d@:");
		class_addMethod(color,
			sel_registerName("getRed:green:blue:alpha:"),
			reinterpret_cast<IMP>(&NSColorGetComponents),
			"B@:o^do^do^do^d");
		class_addMethod(color, sel_registerName("getWhite:alpha:"),
			reinterpret_cast<IMP>(&NSColorGetWhite), "B@:o^do^d");
		class_addMethod(color,
			sel_registerName("getHue:saturation:brightness:alpha:"),
			reinterpret_cast<IMP>(&NSColorGetHSB), "B@:o^do^do^do^d");
		class_addMethod(color,
			sel_registerName("getCyan:magenta:yellow:black:alpha:"),
			reinterpret_cast<IMP>(&NSColorGetCMYK), "B@:o^do^do^do^do^d");
		class_addMethod(color, sel_registerName("numberOfComponents"),
			reinterpret_cast<IMP>(&NSColorNumberOfComponents), "Q@:");
		class_addMethod(color, sel_registerName("getComponents:"),
			reinterpret_cast<IMP>(&NSColorGetComponentArray), "v@:^d");

		Class bezier_path = RegisterHostClass("NSBezierPath", root);
		if (bezier_path == nullptr) return;
		class_addClassMethod(bezier_path, sel_registerName("bezierPath"),
			reinterpret_cast<IMP>(&NSBezierPathFactory), "@@:");
		class_addClassMethod(bezier_path,
			sel_registerName("bezierPathWithRect:"),
			reinterpret_cast<IMP>(&NSBezierPathRectPathFactory),
			"@@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addClassMethod(bezier_path,
			sel_registerName("bezierPathWithOvalInRect:"),
			reinterpret_cast<IMP>(&NSBezierPathRectPathFactory),
			"@@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addClassMethod(bezier_path,
			sel_registerName("bezierPathWithRoundedRect:xRadius:yRadius:"),
			reinterpret_cast<IMP>(&NSBezierPathRoundedRectFactory),
			"@@:{CGRect={CGPoint=dd}{CGSize=dd}}dd");
		class_addMethod(bezier_path, sel_registerName("moveToPoint:"),
			reinterpret_cast<IMP>(&NSBezierPathMoveToPoint),
			"v@:{CGPoint=dd}");
		class_addMethod(bezier_path, sel_registerName("lineToPoint:"),
			reinterpret_cast<IMP>(&NSBezierPathLineToPoint),
			"v@:{CGPoint=dd}");
		class_addMethod(bezier_path,
			sel_registerName("curveToPoint:controlPoint1:controlPoint2:"),
			reinterpret_cast<IMP>(&NSBezierPathCurveToPoint),
			"v@:{CGPoint=dd}{CGPoint=dd}{CGPoint=dd}");
		class_addMethod(bezier_path, sel_registerName("closePath"),
			reinterpret_cast<IMP>(&NSBezierPathClose), "v@:");
		class_addMethod(bezier_path,
			sel_registerName("appendBezierPathWithRect:"),
			reinterpret_cast<IMP>(&NSBezierPathAppendRect),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(bezier_path,
			sel_registerName("appendBezierPathWithOvalInRect:"),
			reinterpret_cast<IMP>(&NSBezierPathAppendOval),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(bezier_path,
			sel_registerName("appendBezierPathWithRoundedRect:xRadius:yRadius:"),
			reinterpret_cast<IMP>(&NSBezierPathAppendRoundedRect),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}dd");
		class_addMethod(bezier_path, sel_registerName("appendBezierPath:"),
			reinterpret_cast<IMP>(&NSBezierPathAppendPath), "v@:@");
		class_addMethod(bezier_path, sel_registerName("removeAllPoints"),
			reinterpret_cast<IMP>(&NSBezierPathRemoveAllPoints), "v@:");
		class_addMethod(bezier_path, sel_registerName("setLineWidth:"),
			reinterpret_cast<IMP>(&NSBezierPathSetLineWidth), "v@:d");
		class_addMethod(bezier_path, sel_registerName("lineWidth"),
			reinterpret_cast<IMP>(&NSBezierPathLineWidth), "d@:");
		class_addMethod(bezier_path, sel_registerName("setLineCapStyle:"),
			reinterpret_cast<IMP>(&NSBezierPathSetLineCapStyle), "v@:Q");
		class_addMethod(bezier_path, sel_registerName("lineCapStyle"),
			reinterpret_cast<IMP>(&NSBezierPathLineCapStyle), "Q@:");
		class_addMethod(bezier_path, sel_registerName("setLineJoinStyle:"),
			reinterpret_cast<IMP>(&NSBezierPathSetLineJoinStyle), "v@:Q");
		class_addMethod(bezier_path, sel_registerName("lineJoinStyle"),
			reinterpret_cast<IMP>(&NSBezierPathLineJoinStyle), "Q@:");
		class_addMethod(bezier_path, sel_registerName("bounds"),
			reinterpret_cast<IMP>(&NSBezierPathBounds),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(bezier_path, sel_registerName("controlPointBounds"),
			reinterpret_cast<IMP>(&NSBezierPathControlPointBounds),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(bezier_path, sel_registerName("isEmpty"),
			reinterpret_cast<IMP>(&NSBezierPathIsEmpty), "B@:");
		class_addMethod(bezier_path, sel_registerName("fill"),
			reinterpret_cast<IMP>(&NSBezierPathFill), "v@:");
		class_addMethod(bezier_path, sel_registerName("evenOddFill"),
			reinterpret_cast<IMP>(&NSBezierPathEvenOddFill), "v@:");
		class_addMethod(bezier_path, sel_registerName("stroke"),
			reinterpret_cast<IMP>(&NSBezierPathStroke), "v@:");

		Class menu = RegisterHostClass("NSMenu", root);
		if (menu == nullptr) return;
		class_addClassMethod(menu, sel_registerName("menuWithTitle:"),
			reinterpret_cast<IMP>(&NSMenuMenuWithTitle), "@@:@");
		class_addClassMethod(menu,
			sel_registerName("popUpContextMenu:withEvent:forView:"),
			reinterpret_cast<IMP>(&NSMenuPopUpContextMenu), "v@:@@@");
		class_addClassMethod(menu,
			sel_registerName("popUpContextMenu:withEvent:forView:withFont:"),
			reinterpret_cast<IMP>(&NSMenuPopUpContextMenuWithFont), "v@:@@@@");
		class_addMethod(menu, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSMenuInit), "@@:");
		class_addMethod(menu, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSMenuInitWithCoder), "@@:@");
		class_addMethod(menu, sel_registerName("initWithTitle:"),
			reinterpret_cast<IMP>(&NSMenuInitWithTitle), "@@:@");
		class_addMethod(menu, sel_registerName("title"),
			reinterpret_cast<IMP>(&NSMenuTitle), "@@:");
		class_addMethod(menu, sel_registerName("setTitle:"),
			reinterpret_cast<IMP>(&NSMenuSetTitle), "v@:@");
		class_addMethod(menu, sel_registerName("autoenablesItems"),
			reinterpret_cast<IMP>(&NSMenuAutoenablesItems), "B@:");
		class_addMethod(menu, sel_registerName("setAutoenablesItems:"),
			reinterpret_cast<IMP>(&NSMenuSetAutoenablesItems), "v@:B");
		class_addMethod(menu, sel_registerName("addItem:"),
			reinterpret_cast<IMP>(&NSMenuAddItem), "v@:@");
		class_addMethod(menu, sel_registerName("insertItem:atIndex:"),
			reinterpret_cast<IMP>(&NSMenuInsertItemAtIndex), "v@:@q");
		class_addMethod(menu, sel_registerName("removeItem:"),
			reinterpret_cast<IMP>(&NSMenuRemoveItem), "v@:@");
		class_addMethod(menu, sel_registerName("numberOfItems"),
			reinterpret_cast<IMP>(&NSMenuNumberOfItems), "q@:");
		class_addMethod(menu, sel_registerName("itemAtIndex:"),
			reinterpret_cast<IMP>(&NSMenuItemAtIndex), "@@:q");
		class_addMethod(menu, sel_registerName("itemArray"),
			reinterpret_cast<IMP>(&NSMenuItemArray), "@@:");
		class_addMethod(menu, sel_registerName("indexOfItem:"),
			reinterpret_cast<IMP>(&NSMenuIndexOfItem), "q@:@");
		class_addMethod(menu, sel_registerName("itemWithTitle:"),
			reinterpret_cast<IMP>(&NSMenuItemWithTitle), "@@:@");
		class_addMethod(menu,
			sel_registerName("addItemWithTitle:action:keyEquivalent:"),
			reinterpret_cast<IMP>(&NSMenuAddItemWithTitle), "@@:@::@");

		Class menu_item = RegisterHostClass("NSMenuItem", root);
		if (menu_item == nullptr) return;
		class_addClassMethod(menu_item,
			sel_registerName("menuItemWithTitle:action:keyEquivalent:"),
			reinterpret_cast<IMP>(&NSMenuItemWithTitleFactory), "@@:@::@");
		class_addMethod(menu_item, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSMenuItemInit), "@@:");
		class_addMethod(menu_item, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSMenuItemInitWithCoder), "@@:@");
		class_addClassMethod(menu_item, sel_registerName("separatorItem"),
			reinterpret_cast<IMP>(&NSMenuItemSeparatorItem), "@@:");
		class_addMethod(menu_item, sel_registerName("initWithTitle:action:keyEquivalent:"),
			reinterpret_cast<IMP>(&NSMenuItemInitWithTitle), "@@:@:@");
		class_addMethod(menu_item, sel_registerName("title"),
			reinterpret_cast<IMP>(&NSMenuItemTitle), "@@:");
		class_addMethod(menu_item, sel_registerName("setTitle:"),
			reinterpret_cast<IMP>(&NSMenuItemSetTitle), "v@:@");
		class_addMethod(menu_item, sel_registerName("target"),
			reinterpret_cast<IMP>(&NSMenuItemTarget), "@@:");
		class_addMethod(menu_item, sel_registerName("setTarget:"),
			reinterpret_cast<IMP>(&NSMenuItemSetTarget), "v@:@");
		class_addMethod(menu_item, sel_registerName("action"),
			reinterpret_cast<IMP>(&NSMenuItemAction), ":@:");
		class_addMethod(menu_item, sel_registerName("setAction:"),
			reinterpret_cast<IMP>(&NSMenuItemSetAction), "v@::");
		class_addMethod(menu_item, sel_registerName("isEnabled"),
			reinterpret_cast<IMP>(&NSMenuItemIsEnabled), "B@:");
		class_addMethod(menu_item, sel_registerName("setEnabled:"),
			reinterpret_cast<IMP>(&NSMenuItemSetEnabled), "v@:B");
		class_addMethod(menu_item, sel_registerName("submenu"),
			reinterpret_cast<IMP>(&NSMenuItemSubmenu), "@@:");
		class_addMethod(menu_item, sel_registerName("setSubmenu:"),
			reinterpret_cast<IMP>(&NSMenuItemSetSubmenu), "v@:@");
		class_addMethod(menu_item, sel_registerName("keyEquivalent"),
			reinterpret_cast<IMP>(&NSMenuItemKeyEquivalent), "@@:");
		class_addMethod(menu_item, sel_registerName("setKeyEquivalent:"),
			reinterpret_cast<IMP>(&NSMenuItemSetKeyEquivalent), "v@:@");
		class_addMethod(menu_item,
			sel_registerName("keyEquivalentModifierMask"),
			reinterpret_cast<IMP>(&NSMenuItemKeyEquivalentModifierMask), "Q@:");
		class_addMethod(menu_item,
			sel_registerName("setKeyEquivalentModifierMask:"),
			reinterpret_cast<IMP>(&NSMenuItemSetKeyEquivalentModifierMask), "v@:Q");
		class_addMethod(menu_item, sel_registerName("isSeparatorItem"),
			reinterpret_cast<IMP>(&NSMenuItemIsSeparator), "B@:");
		class_addMethod(menu_item, sel_registerName("state"),
			reinterpret_cast<IMP>(&NSMenuItemState), "q@:");
		class_addMethod(menu_item, sel_registerName("setState:"),
			reinterpret_cast<IMP>(&NSMenuItemSetState), "v@:q");
		class_addMethod(menu_item, sel_registerName("tag"),
			reinterpret_cast<IMP>(&NSMenuItemTag), "q@:");
		class_addMethod(menu_item, sel_registerName("setTag:"),
			reinterpret_cast<IMP>(&NSMenuItemSetTag), "v@:q");

		Class application = RegisterHostClass("NSApplication", responder);
		if (application == nullptr) return;
		class_addClassMethod(application, sel_registerName("sharedApplication"),
			reinterpret_cast<IMP>(&NSApplicationSharedApplication), "@@:");
		class_addMethod(application, sel_registerName("setDelegate:"),
			reinterpret_cast<IMP>(&NSApplicationSetDelegate), "v@:@");
		class_addMethod(application, sel_registerName("delegate"),
			reinterpret_cast<IMP>(&NSApplicationDelegate), "@@:");
		class_addMethod(application, sel_registerName("mainMenu"),
			reinterpret_cast<IMP>(&NSApplicationMainMenu), "@@:");
		class_addMethod(application, sel_registerName("setMainMenu:"),
			reinterpret_cast<IMP>(&NSApplicationSetMainMenu), "v@:@");
		class_addMethod(application, sel_registerName("sendAction:to:from:"),
			reinterpret_cast<IMP>(&NSApplicationSendAction), "B@::@@");
		class_addMethod(application, sel_registerName("targetForAction:"),
			reinterpret_cast<IMP>(&NSApplicationTargetForActionSimple), "@@::");
		class_addMethod(application,
			sel_registerName("targetForAction:to:from:"),
			reinterpret_cast<IMP>(&NSApplicationTargetForAction), "@@::@@");
		class_addMethod(application, sel_registerName("run"),
			reinterpret_cast<IMP>(&NSApplicationRun), "v@:");
		class_addMethod(application, sel_registerName("sendEvent:"),
			reinterpret_cast<IMP>(&NSApplicationSendEvent), "v@:@");
		class_addMethod(application, sel_registerName("currentEvent"),
			reinterpret_cast<IMP>(&NSApplicationCurrentEvent), "@@:");
		class_addMethod(application, sel_registerName("postEvent:atStart:"),
			reinterpret_cast<IMP>(&NSApplicationPostEvent), "v@:@B");
		class_addMethod(application,
			sel_registerName("discardEventsMatchingMask:beforeEvent:"),
			reinterpret_cast<IMP>(&NSApplicationDiscardEventsMatchingMask), "v@:Q@");
		class_addMethod(application,
			sel_registerName("nextEventMatchingMask:untilDate:inMode:dequeue:"),
			reinterpret_cast<IMP>(&NSApplicationNextEventMatchingMask),
			"@@:Q@@B");
		class_addMethod(application, sel_registerName("stop:"),
			reinterpret_cast<IMP>(&NSApplicationStop), "v@:@");
		class_addMethod(application, sel_registerName("finishLaunching"),
			reinterpret_cast<IMP>(&NSApplicationFinishLaunching), "v@:");
		class_addMethod(application,
			sel_registerName("activateIgnoringOtherApps:"),
			reinterpret_cast<IMP>(&NSApplicationActivateIgnoringOtherApps),
			"v@:B");
		class_addMethod(application, sel_registerName("isActive"),
			reinterpret_cast<IMP>(&NSApplicationIsActive), "B@:");
		class_addMethod(application, sel_registerName("isHidden"),
			reinterpret_cast<IMP>(&NSApplicationIsHidden), "B@:");
		class_addMethod(application, sel_registerName("hide:"),
			reinterpret_cast<IMP>(&NSApplicationHide), "v@:@");
		class_addMethod(application, sel_registerName("unhide:"),
			reinterpret_cast<IMP>(&NSApplicationUnhide), "v@:@");
		class_addMethod(application, sel_registerName("unhideWithoutActivation"),
			reinterpret_cast<IMP>(&NSApplicationUnhideWithoutActivation), "v@:");
		class_addMethod(application, sel_registerName("windows"),
			reinterpret_cast<IMP>(&NSApplicationWindows), "@@:");
		class_addMethod(application, sel_registerName("keyWindow"),
			reinterpret_cast<IMP>(&NSApplicationKeyWindow), "@@:");
		class_addMethod(application, sel_registerName("mainWindow"),
			reinterpret_cast<IMP>(&NSApplicationMainWindow), "@@:");
		class_addMethod(application, sel_registerName("isRunning"),
			reinterpret_cast<IMP>(&NSApplicationIsRunning), "B@:");
		class_addMethod(application, sel_registerName("terminate:"),
			reinterpret_cast<IMP>(&NSApplicationTerminate), "v@:@");
		class_addMethod(application,
			sel_registerName("replyToApplicationShouldTerminate:"),
			reinterpret_cast<IMP>(&NSApplicationReplyToShouldTerminate), "v@:B");

		Class window = RegisterHostClass("NSWindow", responder);
		if (window == nullptr) return;
		class_addClassMethod(window, sel_registerName("frameRectForContentRect:styleMask:"),
			reinterpret_cast<IMP>(&NSWindowFrameRectForContentRect),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}Q");
		class_addClassMethod(window, sel_registerName("contentRectForFrameRect:styleMask:"),
			reinterpret_cast<IMP>(&NSWindowContentRectForFrameRect),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}Q");
		class_addMethod(window, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSWindowInit), "@@:");
		class_addMethod(window, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSWindowInitWithCoder), "@@:@");
		class_addMethod(window,
			sel_registerName("initWithContentRect:styleMask:backing:defer:"),
			reinterpret_cast<IMP>(&NSWindowInitWithContentRect),
			"@@:{CGRect={CGPoint=dd}{CGSize=dd}}QQc");
		class_addMethod(window, sel_registerName("setTitle:"),
			reinterpret_cast<IMP>(&NSWindowSetTitle), "v@:@");
		class_addMethod(window, sel_registerName("title"),
			reinterpret_cast<IMP>(&NSWindowTitle), "@@:");
		class_addMethod(window, sel_registerName("frame"),
			reinterpret_cast<IMP>(&NSWindowFrame),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(window, sel_registerName("convertRectToScreen:"),
			reinterpret_cast<IMP>(&NSWindowConvertRectToScreen),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(window, sel_registerName("convertRectFromScreen:"),
			reinterpret_cast<IMP>(&NSWindowConvertRectFromScreen),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(window, sel_registerName("setFrame:display:"),
			reinterpret_cast<IMP>(&NSWindowSetFrame),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}B");
		class_addMethod(window, sel_registerName("contentSize"),
			reinterpret_cast<IMP>(&NSWindowContentSize), "{CGSize=dd}@:");
		class_addMethod(window, sel_registerName("setContentSize:"),
			reinterpret_cast<IMP>(&NSWindowSetContentSize),
			"v@:{CGSize=dd}");
		class_addMethod(window, sel_registerName("minSize"),
			reinterpret_cast<IMP>(&NSWindowMinSize), "{CGSize=dd}@:");
		class_addMethod(window, sel_registerName("setMinSize:"),
			reinterpret_cast<IMP>(&NSWindowSetMinSize), "v@:{CGSize=dd}");
		class_addMethod(window, sel_registerName("maxSize"),
			reinterpret_cast<IMP>(&NSWindowMaxSize), "{CGSize=dd}@:");
		class_addMethod(window, sel_registerName("setMaxSize:"),
			reinterpret_cast<IMP>(&NSWindowSetMaxSize), "v@:{CGSize=dd}");
		class_addMethod(window, sel_registerName("center"),
			reinterpret_cast<IMP>(&NSWindowCenter), "v@:");
		class_addMethod(window, sel_registerName("miniaturize:"),
			reinterpret_cast<IMP>(&NSWindowMiniaturize), "v@:@");
		class_addMethod(window, sel_registerName("deminiaturize:"),
			reinterpret_cast<IMP>(&NSWindowDeminiaturize), "v@:@");
		class_addMethod(window, sel_registerName("isMiniaturized"),
			reinterpret_cast<IMP>(&NSWindowIsMiniaturized), "B@:");
		class_addMethod(window, sel_registerName("zoom:"),
			reinterpret_cast<IMP>(&NSWindowZoom), "v@:@");
		class_addMethod(window, sel_registerName("isZoomed"),
			reinterpret_cast<IMP>(&NSWindowIsZoomed), "B@:");
		class_addMethod(window, sel_registerName("makeKeyAndOrderFront:"),
			reinterpret_cast<IMP>(&NSWindowMakeKeyAndOrderFront), "v@:@");
		class_addMethod(window, sel_registerName("firstResponder"),
			reinterpret_cast<IMP>(&NSWindowFirstResponder), "@@:");
		class_addMethod(window, sel_registerName("makeFirstResponder:"),
			reinterpret_cast<IMP>(&NSWindowMakeFirstResponder), "B@:@");
		class_addMethod(window, sel_registerName("selectNextKeyView:"),
			reinterpret_cast<IMP>(&NSWindowSelectNextKeyView), "v@:@");
		class_addMethod(window, sel_registerName("selectPreviousKeyView:"),
			reinterpret_cast<IMP>(&NSWindowSelectPreviousKeyView), "v@:@");
		class_addMethod(window, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSWindowAcceptsFirstResponder), "B@:");
		class_addMethod(window, sel_registerName("orderOut:"),
			reinterpret_cast<IMP>(&NSWindowOrderOut), "v@:@");
		class_addMethod(window, sel_registerName("close"),
			reinterpret_cast<IMP>(&NSWindowClose), "v@:");
		class_addMethod(window, sel_registerName("isVisible"),
			reinterpret_cast<IMP>(&NSWindowIsVisible), "B@:");
		class_addMethod(window, sel_registerName("acceptsMouseMovedEvents"),
			reinterpret_cast<IMP>(&NSWindowAcceptsMouseMovedEvents), "B@:");
		class_addMethod(window, sel_registerName("setAcceptsMouseMovedEvents:"),
			reinterpret_cast<IMP>(&NSWindowSetAcceptsMouseMovedEvents), "v@:B");
	class_addMethod(window, sel_registerName("delegate"),
		reinterpret_cast<IMP>(&NSWindowDelegate), "@@:");
	class_addMethod(window, sel_registerName("setDelegate:"),
		reinterpret_cast<IMP>(&NSWindowSetDelegate), "v@:@");
		class_addMethod(window, sel_registerName("isKeyWindow"),
			reinterpret_cast<IMP>(&NSWindowIsKeyWindow), "B@:");
		class_addMethod(window, sel_registerName("isMainWindow"),
			reinterpret_cast<IMP>(&NSWindowIsMainWindow), "B@:");
		class_addMethod(window, sel_registerName("canBecomeKeyWindow"),
			reinterpret_cast<IMP>(&NSWindowCanBecomeKeyWindow), "B@:");
		class_addMethod(window, sel_registerName("canBecomeMainWindow"),
			reinterpret_cast<IMP>(&NSWindowCanBecomeMainWindow), "B@:");
		class_addMethod(window, sel_registerName("makeKeyWindow"),
			reinterpret_cast<IMP>(&NSWindowMakeKeyWindow), "v@:");
		class_addMethod(window, sel_registerName("makeMainWindow"),
			reinterpret_cast<IMP>(&NSWindowMakeMainWindow), "v@:");
		class_addMethod(window, sel_registerName("contentView"),
			reinterpret_cast<IMP>(&NSWindowContentView), "@@:");
		class_addMethod(window, sel_registerName("nextResponder"),
			reinterpret_cast<IMP>(&NSWindowNextResponder), "@@:");
		class_addMethod(window, sel_registerName("performKeyEquivalent:"),
			reinterpret_cast<IMP>(&NSWindowPerformKeyEquivalent), "B@:@");
		class_addMethod(window, sel_registerName("setContentView:"),
			reinterpret_cast<IMP>(&NSWindowSetContentView), "v@:@");

		Class view = RegisterHostClass("NSView", responder);
		if (view == nullptr) return;
		class_addMethod(view, sel_registerName("drawRect:"),
			reinterpret_cast<IMP>(&NSViewDrawRect),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(view, sel_registerName("setNeedsDisplay:"),
			reinterpret_cast<IMP>(&NSViewSetNeedsDisplay), "v@:B");
		class_addMethod(view, sel_registerName("setNeedsDisplayInRect:"),
			reinterpret_cast<IMP>(&NSViewSetNeedsDisplayInRect),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(view, sel_registerName("initWithFrame:"),
			reinterpret_cast<IMP>(&NSViewInitWithFrame),
			"@@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(view, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSViewInitWithCoder), "@@:@");
		class_addMethod(view, sel_registerName("menu"),
			reinterpret_cast<IMP>(&NSViewMenu), "@@:");
		class_addMethod(view, sel_registerName("setMenu:"),
			reinterpret_cast<IMP>(&NSViewSetMenu), "v@:@");
		class_addMethod(view, sel_registerName("setFrame:"),
			reinterpret_cast<IMP>(&NSViewSetFrame),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(view, sel_registerName("frameOrigin"),
			reinterpret_cast<IMP>(&NSViewFrameOrigin), "{CGPoint=dd}@:");
		class_addMethod(view, sel_registerName("frameSize"),
			reinterpret_cast<IMP>(&NSViewFrameSize), "{CGSize=dd}@:");
		class_addMethod(view, sel_registerName("autoresizingMask"),
			reinterpret_cast<IMP>(&NSViewAutoresizingMask), "Q@:");
		class_addMethod(view, sel_registerName("setAutoresizingMask:"),
			reinterpret_cast<IMP>(&NSViewSetAutoresizingMask), "v@:Q");
		class_addMethod(view, sel_registerName("resizeSubviewsWithOldSize:"),
			reinterpret_cast<IMP>(&NSViewResizeSubviewsWithOldSize),
			"v@:{CGSize=dd}");
		class_addMethod(view, sel_registerName("resizeWithOldSuperviewSize:"),
			reinterpret_cast<IMP>(&NSViewResizeWithOldSuperviewSize),
			"v@:{CGSize=dd}");
		class_addMethod(view, sel_registerName("setFrameOrigin:"),
			reinterpret_cast<IMP>(&NSViewSetFrameOrigin), "v@:{CGPoint=dd}");
		class_addMethod(view, sel_registerName("setFrameSize:"),
			reinterpret_cast<IMP>(&NSViewSetFrameSize), "v@:{CGSize=dd}");
		class_addMethod(view, sel_registerName("frame"),
			reinterpret_cast<IMP>(&NSViewFrame),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(view, sel_registerName("bounds"),
			reinterpret_cast<IMP>(&NSViewBounds),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(view, sel_registerName("visibleRect"),
			reinterpret_cast<IMP>(&NSViewVisibleRect),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(view, sel_registerName("scrollRectToVisible:"),
			reinterpret_cast<IMP>(&NSViewScrollRectToVisible),
			"B@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(view, sel_registerName("isFlipped"),
			reinterpret_cast<IMP>(&NSViewIsFlipped), "B@:");
		class_addMethod(view, sel_registerName("convertPoint:toView:"),
			reinterpret_cast<IMP>(&NSViewConvertPointToView),
			"{CGPoint=dd}@:{CGPoint=dd}@");
		class_addMethod(view, sel_registerName("convertPoint:fromView:"),
			reinterpret_cast<IMP>(&NSViewConvertPointFromView),
			"{CGPoint=dd}@:{CGPoint=dd}@");
		class_addMethod(view, sel_registerName("convertRect:toView:"),
			reinterpret_cast<IMP>(&NSViewConvertRectToView),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}@");
		class_addMethod(view, sel_registerName("convertRect:fromView:"),
			reinterpret_cast<IMP>(&NSViewConvertRectFromView),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:{CGRect={CGPoint=dd}{CGSize=dd}}@");
		class_addMethod(view, sel_registerName("setBounds:"),
			reinterpret_cast<IMP>(&NSViewSetBounds),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(view, sel_registerName("scrollPoint:"),
			reinterpret_cast<IMP>(&NSViewScrollPoint), "v@:{CGPoint=dd}");
		class_addMethod(view, sel_registerName("window"),
			reinterpret_cast<IMP>(&NSViewWindow), "@@:");
		class_addMethod(view, sel_registerName("superview"),
			reinterpret_cast<IMP>(&NSViewSuperview), "@@:");
		class_addMethod(view, sel_registerName("enclosingScrollView"),
			reinterpret_cast<IMP>(&NSViewEnclosingScrollView), "@@:");
		class_addMethod(view, sel_registerName("nextResponder"),
			reinterpret_cast<IMP>(&NSViewNextResponder), "@@:");
		class_addMethod(view, sel_registerName("performKeyEquivalent:"),
			reinterpret_cast<IMP>(&NSViewPerformKeyEquivalent), "B@:@");
		class_addMethod(view, sel_registerName("nextKeyView"),
			reinterpret_cast<IMP>(&NSViewNextKeyView), "@@:");
		class_addMethod(view, sel_registerName("setNextKeyView:"),
			reinterpret_cast<IMP>(&NSViewSetNextKeyView), "v@:@");
		class_addMethod(view, sel_registerName("subviews"),
			reinterpret_cast<IMP>(&NSViewSubviews), "@@:");
		class_addMethod(view, sel_registerName("isDescendantOf:"),
			reinterpret_cast<IMP>(&NSViewIsDescendantOf), "B@:@");
		class_addMethod(view, sel_registerName("isHidden"),
			reinterpret_cast<IMP>(&NSViewIsHidden), "B@:");
		class_addMethod(view, sel_registerName("setHidden:"),
			reinterpret_cast<IMP>(&NSViewSetHidden), "v@:B");
		class_addMethod(view, sel_registerName("isHiddenOrHasHiddenAncestor"),
			reinterpret_cast<IMP>(&NSViewIsHiddenOrHasHiddenAncestor), "B@:");
		class_addMethod(view, sel_registerName("addSubview:"),
			reinterpret_cast<IMP>(&NSViewAddSubview), "v@:@");
		class_addMethod(view, sel_registerName("removeFromSuperview"),
			reinterpret_cast<IMP>(&NSViewRemoveFromSuperview), "v@:");

		Class clip_view = RegisterHostClass("NSClipView", view);
		if (clip_view == nullptr) return;
		class_addMethod(clip_view, sel_registerName("documentView"),
			reinterpret_cast<IMP>(&NSClipViewDocumentView), "@@:");
		class_addMethod(clip_view, sel_registerName("setDocumentView:"),
			reinterpret_cast<IMP>(&NSClipViewSetDocumentView), "v@:@");
		class_addMethod(clip_view, sel_registerName("constrainScrollPoint:"),
			reinterpret_cast<IMP>(&NSClipViewConstrainScrollPoint),
			"{CGPoint=dd}@:{CGPoint=dd}");
		class_addMethod(clip_view, sel_registerName("scrollToPoint:"),
			reinterpret_cast<IMP>(&NSClipViewScrollToPoint),
			"v@:{CGPoint=dd}");
		class_addMethod(clip_view, sel_registerName("documentVisibleRect"),
			reinterpret_cast<IMP>(&NSClipViewDocumentVisibleRect),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(clip_view, sel_registerName("documentRect"),
			reinterpret_cast<IMP>(&NSClipViewDocumentRect),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");

		Class scroll_view = RegisterHostClass("NSScrollView", view);
		if (scroll_view == nullptr) return;
		class_addMethod(scroll_view, sel_registerName("initWithFrame:"),
			reinterpret_cast<IMP>(&NSScrollViewInitWithFrame),
			"@@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addMethod(scroll_view, sel_registerName("contentView"),
			reinterpret_cast<IMP>(&NSScrollViewContentView), "@@:");
		class_addMethod(scroll_view, sel_registerName("setContentView:"),
			reinterpret_cast<IMP>(&NSScrollViewSetContentView), "v@:@");
		class_addMethod(scroll_view, sel_registerName("documentView"),
			reinterpret_cast<IMP>(&NSScrollViewDocumentView), "@@:");
		class_addMethod(scroll_view, sel_registerName("setDocumentView:"),
			reinterpret_cast<IMP>(&NSScrollViewSetDocumentView), "v@:@");
		class_addMethod(scroll_view, sel_registerName("documentVisibleRect"),
			reinterpret_cast<IMP>(&NSScrollViewDocumentVisibleRect),
			"{CGRect={CGPoint=dd}{CGSize=dd}}@:");
		class_addMethod(scroll_view,
			sel_registerName("reflectScrolledClipView:"),
			reinterpret_cast<IMP>(&NSScrollViewReflectScrolledClipView), "v@:@");
		class_addMethod(scroll_view, sel_registerName("hasVerticalScroller"),
			reinterpret_cast<IMP>(&NSScrollViewHasVerticalScroller), "B@:");
		class_addMethod(scroll_view, sel_registerName("setHasVerticalScroller:"),
			reinterpret_cast<IMP>(&NSScrollViewSetHasVerticalScroller), "v@:B");
		class_addMethod(scroll_view, sel_registerName("hasHorizontalScroller"),
			reinterpret_cast<IMP>(&NSScrollViewHasHorizontalScroller), "B@:");
		class_addMethod(scroll_view, sel_registerName("setHasHorizontalScroller:"),
			reinterpret_cast<IMP>(&NSScrollViewSetHasHorizontalScroller), "v@:B");
		class_addMethod(scroll_view, sel_registerName("autohidesScrollers"),
			reinterpret_cast<IMP>(&NSScrollViewAutohidesScrollers), "B@:");
		class_addMethod(scroll_view, sel_registerName("setAutohidesScrollers:"),
			reinterpret_cast<IMP>(&NSScrollViewSetAutohidesScrollers), "v@:B");

		Class progress = RegisterHostClass("NSProgressIndicator", view);
		if (progress == nullptr) return;
		class_addMethod(progress, sel_registerName("minValue"),
			reinterpret_cast<IMP>(&NSProgressIndicatorMinValue), "d@:");
		class_addMethod(progress, sel_registerName("setMinValue:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorSetMinValue), "v@:d");
		class_addMethod(progress, sel_registerName("maxValue"),
			reinterpret_cast<IMP>(&NSProgressIndicatorMaxValue), "d@:");
		class_addMethod(progress, sel_registerName("setMaxValue:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorSetMaxValue), "v@:d");
		class_addMethod(progress, sel_registerName("doubleValue"),
			reinterpret_cast<IMP>(&NSProgressIndicatorDoubleValue), "d@:");
		class_addMethod(progress, sel_registerName("setDoubleValue:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorSetDoubleValue), "v@:d");
		class_addMethod(progress, sel_registerName("incrementBy:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorIncrementBy), "v@:d");
		class_addMethod(progress, sel_registerName("isIndeterminate"),
			reinterpret_cast<IMP>(&NSProgressIndicatorIsIndeterminate), "B@:");
		class_addMethod(progress, sel_registerName("setIndeterminate:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorSetIndeterminate), "v@:B");
		class_addMethod(progress, sel_registerName("style"),
			reinterpret_cast<IMP>(&NSProgressIndicatorStyle), "q@:");
		class_addMethod(progress, sel_registerName("setStyle:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorSetStyle), "v@:q");
		class_addMethod(progress, sel_registerName("startAnimation:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorStartAnimation), "v@:@");
		class_addMethod(progress, sel_registerName("stopAnimation:"),
			reinterpret_cast<IMP>(&NSProgressIndicatorStopAnimation), "v@:@");

		Class control = RegisterHostClass("NSControl", view);
		if (control == nullptr) return;
		class_addMethod(control, sel_registerName("setTarget:"),
			reinterpret_cast<IMP>(&NSButtonSetTarget), "v@:@");
		class_addMethod(control, sel_registerName("setAction:"),
			reinterpret_cast<IMP>(&NSButtonSetAction), "v@::");

		Class nib_connector = RegisterHostClass("NSNibConnector", root);
		if (nib_connector == nullptr) return;
		class_addMethod(nib_connector, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSNibConnectorInitWithCoder), "@@:@");
		class_addMethod(nib_connector,
			sel_registerName("establishConnection:"),
			reinterpret_cast<IMP>(&NSNibConnectorEstablishConnection), "v@:@");
		class_addMethod(nib_connector, sel_registerName("establishConnection"),
			reinterpret_cast<IMP>(&NSNibConnectorEstablishConnection), "v@:");
		Class outlet_connector = RegisterHostClass("NSNibOutletConnector",
			nib_connector);
		Class control_connector = RegisterHostClass("NSNibControlConnector",
			nib_connector);
		if (outlet_connector == nullptr || control_connector == nullptr) return;
		class_addMethod(control, sel_registerName("setEnabled:"),
			reinterpret_cast<IMP>(&NSButtonSetEnabled), "v@:B");
		class_addMethod(control, sel_registerName("isEnabled"),
			reinterpret_cast<IMP>(&NSButtonIsEnabled), "B@:");
		class_addMethod(control, sel_registerName("stringValue"),
			reinterpret_cast<IMP>(&NSButtonTitle), "@@:");
		class_addMethod(control, sel_registerName("setStringValue:"),
			reinterpret_cast<IMP>(&NSButtonSetTitle), "v@:@");
		class_addMethod(control, sel_registerName("intValue"),
			reinterpret_cast<IMP>(&NSControlIntValue), "i@:");
		class_addMethod(control, sel_registerName("integerValue"),
			reinterpret_cast<IMP>(&NSControlIntegerValue), "q@:");
		class_addMethod(control, sel_registerName("floatValue"),
			reinterpret_cast<IMP>(&NSControlFloatValue), "f@:");
		class_addMethod(control, sel_registerName("doubleValue"),
			reinterpret_cast<IMP>(&NSControlDoubleValue), "d@:");
		class_addMethod(control, sel_registerName("boolValue"),
			reinterpret_cast<IMP>(&NSControlBoolValue), "B@:");
		class_addMethod(control, sel_registerName("setIntValue:"),
			reinterpret_cast<IMP>(&NSControlSetIntValue), "v@:i");
		class_addMethod(control, sel_registerName("setIntegerValue:"),
			reinterpret_cast<IMP>(&NSControlSetIntegerValue), "v@:q");
		class_addMethod(control, sel_registerName("setFloatValue:"),
			reinterpret_cast<IMP>(&NSControlSetFloatValue), "v@:f");
		class_addMethod(control, sel_registerName("setDoubleValue:"),
			reinterpret_cast<IMP>(&NSControlSetDoubleValue), "v@:d");
		class_addMethod(control, sel_registerName("setBoolValue:"),
			reinterpret_cast<IMP>(&NSControlSetBoolValue), "v@:B");

		Class slider = RegisterHostClass("NSSlider", control);
		if (slider == nullptr) return;
		class_addMethod(slider, sel_registerName("minValue"),
			reinterpret_cast<IMP>(&NSSliderMinValue), "d@:");
		class_addMethod(slider, sel_registerName("setMinValue:"),
			reinterpret_cast<IMP>(&NSSliderSetMinValue), "v@:d");
		class_addMethod(slider, sel_registerName("maxValue"),
			reinterpret_cast<IMP>(&NSSliderMaxValue), "d@:");
		class_addMethod(slider, sel_registerName("setMaxValue:"),
			reinterpret_cast<IMP>(&NSSliderSetMaxValue), "v@:d");
		class_addMethod(slider, sel_registerName("doubleValue"),
			reinterpret_cast<IMP>(&NSSliderDoubleValue), "d@:");
		class_addMethod(slider, sel_registerName("setDoubleValue:"),
			reinterpret_cast<IMP>(&NSSliderSetDoubleValue), "v@:d");
		class_addMethod(slider, sel_registerName("stringValue"),
			reinterpret_cast<IMP>(&NSSliderStringValue), "@@:");
		class_addMethod(slider, sel_registerName("setStringValue:"),
			reinterpret_cast<IMP>(&NSSliderSetStringValue), "v@:@");
		class_addMethod(slider, sel_registerName("intValue"),
			reinterpret_cast<IMP>(&NSSliderIntValue), "i@:");
		class_addMethod(slider, sel_registerName("setIntValue:"),
			reinterpret_cast<IMP>(&NSSliderSetIntValue), "v@:i");
		class_addMethod(slider, sel_registerName("integerValue"),
			reinterpret_cast<IMP>(&NSSliderIntegerValue), "q@:");
		class_addMethod(slider, sel_registerName("setIntegerValue:"),
			reinterpret_cast<IMP>(&NSSliderSetIntegerValue), "v@:q");
		class_addMethod(slider, sel_registerName("floatValue"),
			reinterpret_cast<IMP>(&NSSliderFloatValue), "f@:");
		class_addMethod(slider, sel_registerName("setFloatValue:"),
			reinterpret_cast<IMP>(&NSSliderSetFloatValue), "v@:f");
		class_addMethod(slider, sel_registerName("isVertical"),
			reinterpret_cast<IMP>(&NSSliderIsVertical), "B@:");
		class_addMethod(slider, sel_registerName("setVertical:"),
			reinterpret_cast<IMP>(&NSSliderSetVertical), "v@:B");
		class_addMethod(slider, sel_registerName("isContinuous"),
			reinterpret_cast<IMP>(&NSSliderIsContinuous), "B@:");
		class_addMethod(slider, sel_registerName("setContinuous:"),
			reinterpret_cast<IMP>(&NSSliderSetContinuous), "v@:B");
		class_addMethod(slider, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSControlAcceptsFirstResponder), "B@:");

		Class stepper = RegisterHostClass("NSStepper", control);
		if (stepper == nullptr) return;
		class_addMethod(stepper, sel_registerName("minValue"),
			reinterpret_cast<IMP>(&NSStepperMinValue), "d@:");
		class_addMethod(stepper, sel_registerName("setMinValue:"),
			reinterpret_cast<IMP>(&NSStepperSetMinValue), "v@:d");
		class_addMethod(stepper, sel_registerName("maxValue"),
			reinterpret_cast<IMP>(&NSStepperMaxValue), "d@:");
		class_addMethod(stepper, sel_registerName("setMaxValue:"),
			reinterpret_cast<IMP>(&NSStepperSetMaxValue), "v@:d");
		class_addMethod(stepper, sel_registerName("increment"),
			reinterpret_cast<IMP>(&NSStepperIncrement), "d@:");
		class_addMethod(stepper, sel_registerName("setIncrement:"),
			reinterpret_cast<IMP>(&NSStepperSetIncrement), "v@:d");
		class_addMethod(stepper, sel_registerName("valueWraps"),
			reinterpret_cast<IMP>(&NSStepperValueWraps), "B@:");
		class_addMethod(stepper, sel_registerName("setValueWraps:"),
			reinterpret_cast<IMP>(&NSStepperSetValueWraps), "v@:B");
		class_addMethod(stepper, sel_registerName("autorepeat"),
			reinterpret_cast<IMP>(&NSStepperAutorepeat), "B@:");
		class_addMethod(stepper, sel_registerName("setAutorepeat:"),
			reinterpret_cast<IMP>(&NSStepperSetAutorepeat), "v@:B");
		class_addMethod(stepper, sel_registerName("doubleValue"),
			reinterpret_cast<IMP>(&NSStepperDoubleValue), "d@:");
		class_addMethod(stepper, sel_registerName("setDoubleValue:"),
			reinterpret_cast<IMP>(&NSStepperSetDoubleValue), "v@:d");
		class_addMethod(stepper, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSControlAcceptsFirstResponder), "B@:");

		Class button = RegisterHostClass("NSButton", control);
		if (button == nullptr) return;
		class_addMethod(button, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSButtonInitWithCoder), "@@:@");
		class_addMethod(button, sel_registerName("setTitle:"),
			reinterpret_cast<IMP>(&NSButtonSetTitle), "v@:@");
		class_addMethod(button, sel_registerName("title"),
			reinterpret_cast<IMP>(&NSButtonTitle), "@@:");
		class_addMethod(button, sel_registerName("buttonType"),
			reinterpret_cast<IMP>(&NSButtonButtonType), "Q@:");
		class_addMethod(button, sel_registerName("setButtonType:"),
			reinterpret_cast<IMP>(&NSButtonSetButtonType), "v@:Q");
		class_addMethod(button, sel_registerName("state"),
			reinterpret_cast<IMP>(&NSButtonState), "q@:");
		class_addMethod(button, sel_registerName("setState:"),
			reinterpret_cast<IMP>(&NSButtonSetState), "v@:q");
		class_addMethod(button, sel_registerName("allowsMixedState"),
			reinterpret_cast<IMP>(&NSButtonAllowsMixedState), "B@:");
		class_addMethod(button, sel_registerName("keyEquivalent"),
			reinterpret_cast<IMP>(&NSButtonKeyEquivalent), "@@:");
		class_addMethod(button, sel_registerName("setKeyEquivalent:"),
			reinterpret_cast<IMP>(&NSButtonSetKeyEquivalent), "v@:@");
		class_addMethod(button, sel_registerName("keyEquivalentModifierMask"),
			reinterpret_cast<IMP>(&NSButtonKeyEquivalentModifierMask), "Q@:");
		class_addMethod(button, sel_registerName("setKeyEquivalentModifierMask:"),
			reinterpret_cast<IMP>(&NSButtonSetKeyEquivalentModifierMask), "v@:Q");
		class_addMethod(button, sel_registerName("performKeyEquivalent:"),
			reinterpret_cast<IMP>(&NSButtonPerformKeyEquivalent), "B@:@");
		class_addMethod(button, sel_registerName("performClick:"),
			reinterpret_cast<IMP>(&NSButtonPerformClick), "v@:@");
		class_addMethod(button, sel_registerName("setAllowsMixedState:"),
			reinterpret_cast<IMP>(&NSButtonSetAllowsMixedState), "v@:B");
		class_addMethod(button, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSControlAcceptsFirstResponder), "B@:");
		class_addClassMethod(button,
			sel_registerName("buttonWithTitle:target:action:"),
			reinterpret_cast<IMP>(&NSButtonCreateWithTitleTargetAction), "@@:@@:");

		Class alert = RegisterHostClass("NSAlert", root);
		if (alert == nullptr) return;
		class_addMethod(alert, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSAlertInit), "@@:");
		class_addMethod(alert, sel_registerName("messageText"),
			reinterpret_cast<IMP>(&NSAlertMessageText), "@@:");
		class_addMethod(alert, sel_registerName("setMessageText:"),
			reinterpret_cast<IMP>(&NSAlertSetMessageText), "v@:@");
		class_addMethod(alert, sel_registerName("informativeText"),
			reinterpret_cast<IMP>(&NSAlertInformativeText), "@@:");
		class_addMethod(alert, sel_registerName("setInformativeText:"),
			reinterpret_cast<IMP>(&NSAlertSetInformativeText), "v@:@");
		class_addMethod(alert, sel_registerName("addButtonWithTitle:"),
			reinterpret_cast<IMP>(&NSAlertAddButtonWithTitle), "@@:@");
		class_addMethod(alert, sel_registerName("alertStyle"),
			reinterpret_cast<IMP>(&NSAlertStyle), "Q@:");
		class_addMethod(alert, sel_registerName("setAlertStyle:"),
			reinterpret_cast<IMP>(&NSAlertSetStyle), "v@:Q");
		class_addMethod(alert, sel_registerName("runModal"),
			reinterpret_cast<IMP>(&NSAlertRunModal), "q@:");

		Class url = RegisterHostClass("NSURL", root);
		if (url == nullptr) return;
		class_addMethod(url, sel_registerName("initFileURLWithPath:"),
			reinterpret_cast<IMP>(&NSURLInitWithFileSystemPath), "@@:@");
		class_addMethod(url, sel_registerName("path"),
			reinterpret_cast<IMP>(&NSURLPath), "@@:");
		class_addMethod(url, sel_registerName("absoluteString"),
			reinterpret_cast<IMP>(&NSURLAbsoluteString), "@@:");
		class_addMethod(url, sel_registerName("isFileURL"),
			reinterpret_cast<IMP>(&NSURLIsFileURL), "B@:");
		class_addMethod(url, sel_registerName("URLByAppendingPathComponent:"),
			reinterpret_cast<IMP>(&NSURLByAppendingPathComponent), "@@:@");
		class_addMethod(url, sel_registerName("URLByDeletingLastPathComponent"),
			reinterpret_cast<IMP>(&NSURLByDeletingLastPathComponent), "@@:");
		class_addMethod(url, sel_registerName("lastPathComponent"),
			reinterpret_cast<IMP>(&NSURLLastPathComponent), "@@:");
		class_addMethod(url, sel_registerName("pathExtension"),
			reinterpret_cast<IMP>(&NSURLPathExtension), "@@:");
		class_addMethod(url, sel_registerName("URLByDeletingPathExtension"),
			reinterpret_cast<IMP>(&NSURLByDeletingPathExtension), "@@:");
		class_addMethod(url, sel_registerName("standardizedFileURL"),
			reinterpret_cast<IMP>(&NSURLStandardizedFileURL), "@@:");
		class_addClassMethod(url, sel_registerName("fileURLWithPath:"),
			reinterpret_cast<IMP>(&NSURLFileURLWithPath), "@@:@");

		Class ut_type = RegisterHostClass("UTType", root);
		if (ut_type == nullptr) return;
		class_addMethod(ut_type, sel_registerName("identifier"),
			reinterpret_cast<IMP>(&UTTypeIdentifier), "@@:");
		class_addMethod(ut_type, sel_registerName("preferredFilenameExtension"),
			reinterpret_cast<IMP>(&UTTypePreferredFilenameExtension), "@@:");
		class_addMethod(ut_type, sel_registerName("conformsToType:"),
			reinterpret_cast<IMP>(&UTTypeConformsTo), "B@:@");
		class_addClassMethod(ut_type, sel_registerName("typeWithIdentifier:"),
			reinterpret_cast<IMP>(&UTTypeTypeWithIdentifier), "@@:@");
		class_addClassMethod(ut_type,
			sel_registerName("typeWithIdentifier:conformingToType:"),
			reinterpret_cast<IMP>(&UTTypeTypeWithIdentifierConformingToType),
			"@@:@@");
		class_addClassMethod(ut_type,
			sel_registerName("typeWithFilenameExtension:"),
			reinterpret_cast<IMP>(&UTTypeTypeWithFilenameExtension), "@@:@");
		class_addClassMethod(ut_type,
			sel_registerName("typeWithFilenameExtension:conformingToType:"),
			reinterpret_cast<IMP>(
				&UTTypeTypeWithFilenameExtensionConformingToType), "@@:@@");

		Class error_class = RegisterHostClass("NSError", root);
		if (error_class == nullptr) return;
		class_addMethod(error_class, sel_registerName("domain"),
			reinterpret_cast<IMP>(&NSErrorDomain), "@@:");
		class_addMethod(error_class, sel_registerName("initWithDomain:code:userInfo:"),
			reinterpret_cast<IMP>(&NSErrorInitWithDomainCodeUserInfo), "@@:@q@");
		class_addMethod(error_class, sel_registerName("code"),
			reinterpret_cast<IMP>(&NSErrorCode), "q@:");
		class_addMethod(error_class, sel_registerName("userInfo"),
			reinterpret_cast<IMP>(&NSErrorUserInfo), "@@:");
		class_addMethod(error_class, sel_registerName("localizedDescription"),
			reinterpret_cast<IMP>(&NSErrorLocalizedDescription), "@@:");
		class_addClassMethod(error_class,
			sel_registerName("errorWithDomain:code:userInfo:"),
			reinterpret_cast<IMP>(&NSErrorErrorWithDomainCodeUserInfo),
			"@@:@q@");

		Class file_manager = RegisterHostClass("NSFileManager", root);
		if (file_manager == nullptr) return;
		class_addClassMethod(file_manager, sel_registerName("defaultManager"),
			reinterpret_cast<IMP>(&NSFileManagerDefaultManager), "@@:");
		class_addMethod(file_manager, sel_registerName("fileExistsAtPath:"),
			reinterpret_cast<IMP>(&NSFileManagerFileExistsAtPath), "B@:@");
	class_addClassMethod(file_manager,
		sel_registerName("URLsForDirectory:inDomains:"),
		reinterpret_cast<IMP>(&NSFileManagerURLsForDirectory), "@@:QQ");
	class_addMethod(file_manager,
		sel_registerName("URLForDirectory:inDomain:appropriateForURL:create:error:"),
		reinterpret_cast<IMP>(&NSFileManagerURLForDirectory), "@@:QQ@B^@");
		class_addMethod(file_manager,
			sel_registerName("fileExistsAtPath:isDirectory:"),
			reinterpret_cast<IMP>(&NSFileManagerFileExistsAtPathIsDirectory),
			"B@:@^B");
		class_addMethod(file_manager,
			sel_registerName("contentsOfDirectoryAtPath:error:"),
			reinterpret_cast<IMP>(&NSFileManagerContentsOfDirectoryAtPath),
			"@@:@^@");
	class_addMethod(file_manager,
		sel_registerName("contentsOfDirectoryAtURL:includingPropertiesForKeys:options:error:"),
		reinterpret_cast<IMP>(&NSFileManagerContentsOfDirectoryAtURL),
		"@@:@@Q^@");
		class_addMethod(file_manager, sel_registerName("contentsAtPath:"),
			reinterpret_cast<IMP>(&NSFileManagerContentsAtPath), "@@:@");
		class_addMethod(file_manager,
			sel_registerName("attributesOfItemAtPath:error:"),
			reinterpret_cast<IMP>(&NSFileManagerAttributesOfItemAtPath), "@@:@^@");
		class_addMethod(file_manager,
			sel_registerName("attributesOfItemAtURL:error:"),
			reinterpret_cast<IMP>(&NSFileManagerAttributesOfItemAtURL), "@@:@^@");
		class_addMethod(file_manager,
			sel_registerName("attributesOfFileSystemForPath:error:"),
			reinterpret_cast<IMP>(&NSFileManagerAttributesOfFileSystemForPath),
			"@@:@^@");
		class_addMethod(file_manager,
			sel_registerName("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"),
			reinterpret_cast<IMP>(&NSFileManagerCreateDirectory), "B@:@B@^@");
	class_addMethod(file_manager,
		sel_registerName("createDirectoryAtURL:withIntermediateDirectories:attributes:error:"),
		reinterpret_cast<IMP>(&NSFileManagerCreateDirectoryAtURL), "B@:@B@^@");
		class_addMethod(file_manager, sel_registerName("removeItemAtPath:error:"),
			reinterpret_cast<IMP>(&NSFileManagerRemoveItem), "B@:@^@");
	class_addMethod(file_manager, sel_registerName("removeItemAtURL:error:"),
		reinterpret_cast<IMP>(&NSFileManagerRemoveItemAtURL), "B@:@^@");
		class_addMethod(file_manager,
			sel_registerName("copyItemAtPath:toPath:error:"),
			reinterpret_cast<IMP>(&NSFileManagerCopyItem), "B@:@@^@");
	class_addMethod(file_manager,
		sel_registerName("copyItemAtURL:toURL:error:"),
		reinterpret_cast<IMP>(&NSFileManagerCopyItemAtURL), "B@:@@^@");
		class_addMethod(file_manager,
			sel_registerName("moveItemAtPath:toPath:error:"),
			reinterpret_cast<IMP>(&NSFileManagerMoveItem), "B@:@@^@");
	class_addMethod(file_manager,
		sel_registerName("moveItemAtURL:toURL:error:"),
		reinterpret_cast<IMP>(&NSFileManagerMoveItemAtURL), "B@:@@^@");
		class_addMethod(file_manager,
			sel_registerName("createFileAtPath:contents:attributes:"),
			reinterpret_cast<IMP>(&NSFileManagerCreateFile), "B@:@@@");

		Class file_handle = RegisterHostClass("NSFileHandle", root);
		if (file_handle == nullptr) return;
		class_addMethod(file_handle,
			sel_registerName("initWithFileDescriptor:closeOnDealloc:"),
			reinterpret_cast<IMP>(&NSFileHandleInitWithFileDescriptor), "@@:iB");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleForReadingAtPath:"),
			reinterpret_cast<IMP>(&NSFileHandleForReadingAtPath), "@@:@");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleForWritingAtPath:"),
			reinterpret_cast<IMP>(&NSFileHandleForWritingAtPath), "@@:@");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleForUpdatingAtPath:"),
			reinterpret_cast<IMP>(&NSFileHandleForUpdatingAtPath), "@@:@");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleWithFileDescriptor:"),
			reinterpret_cast<IMP>(&NSFileHandleWithFileDescriptorDefault),
			"@@:i");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleWithFileDescriptor:closeOnDealloc:"),
			reinterpret_cast<IMP>(&NSFileHandleWithFileDescriptor), "@@:iB");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleWithStandardInput"),
			reinterpret_cast<IMP>(&NSFileHandleStandardInput), "@@:");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleWithStandardOutput"),
			reinterpret_cast<IMP>(&NSFileHandleStandardOutput), "@@:");
		class_addClassMethod(file_handle,
			sel_registerName("fileHandleWithStandardError"),
			reinterpret_cast<IMP>(&NSFileHandleStandardError), "@@:");
		class_addClassMethod(file_handle, sel_registerName("fileHandleWithNullDevice"),
			reinterpret_cast<IMP>(&NSFileHandleNullDevice), "@@:");
		class_addMethod(file_handle, sel_registerName("readDataOfLength:"),
			reinterpret_cast<IMP>(&NSFileHandleReadDataOfLength), "@@:Q");
		class_addMethod(file_handle, sel_registerName("readDataToEndOfFile"),
			reinterpret_cast<IMP>(&NSFileHandleReadDataToEndOfFile), "@@:");
		class_addMethod(file_handle, sel_registerName("availableData"),
			reinterpret_cast<IMP>(&NSFileHandleAvailableData), "@@:");
		class_addMethod(file_handle, sel_registerName("writeData:"),
			reinterpret_cast<IMP>(&NSFileHandleWriteData), "v@:@");
		class_addMethod(file_handle, sel_registerName("offsetInFile"),
			reinterpret_cast<IMP>(&NSFileHandleOffsetInFile), "Q@:");
		class_addMethod(file_handle, sel_registerName("seekToEndOfFile"),
			reinterpret_cast<IMP>(&NSFileHandleSeekToEndOfFile), "Q@:");
		class_addMethod(file_handle, sel_registerName("seekToFileOffset:"),
			reinterpret_cast<IMP>(&NSFileHandleSeekToFileOffset), "v@:Q");
		class_addMethod(file_handle, sel_registerName("truncateFileAtOffset:"),
			reinterpret_cast<IMP>(&NSFileHandleTruncateFileAtOffset), "v@:Q");
		class_addMethod(file_handle, sel_registerName("synchronizeFile"),
			reinterpret_cast<IMP>(&NSFileHandleSynchronizeFile), "v@:");
		class_addMethod(file_handle, sel_registerName("closeFile"),
			reinterpret_cast<IMP>(&NSFileHandleCloseFile), "v@:");

		Class save_panel = RegisterHostClass("NSSavePanel", root);
		if (save_panel == nullptr) return;
		class_addMethod(save_panel, sel_registerName("init"),
			reinterpret_cast<IMP>(&NSFilePanelInit), "@@:");
		class_addMethod(save_panel, sel_registerName("setTitle:"),
			reinterpret_cast<IMP>(&NSFilePanelSetTitle), "v@:@");
		class_addMethod(save_panel, sel_registerName("setAllowedFileTypes:"),
			reinterpret_cast<IMP>(&NSFilePanelSetAllowedFileTypes), "v@:@");
		class_addMethod(save_panel, sel_registerName("setAllowedContentTypes:"),
			reinterpret_cast<IMP>(&NSFilePanelSetAllowedContentTypes), "v@:@");
		class_addMethod(save_panel, sel_registerName("allowedContentTypes"),
			reinterpret_cast<IMP>(&NSFilePanelAllowedContentTypes), "@@:");
		class_addMethod(save_panel, sel_registerName("setNameFieldStringValue:"),
			reinterpret_cast<IMP>(&NSSavePanelSetNameFieldStringValue), "v@:@");
		class_addMethod(save_panel, sel_registerName("nameFieldStringValue"),
			reinterpret_cast<IMP>(&NSSavePanelNameFieldStringValue), "@@:");
		class_addMethod(save_panel, sel_registerName("runModal"),
			reinterpret_cast<IMP>(&NSSavePanelRunModal), "q@:");
		class_addMethod(save_panel, sel_registerName("URL"),
			reinterpret_cast<IMP>(&NSFilePanelURL), "@@:");
		class_addMethod(save_panel, sel_registerName("URLs"),
			reinterpret_cast<IMP>(&NSFilePanelURLs), "@@:");
		class_addMethod(save_panel, sel_registerName("filename"),
			reinterpret_cast<IMP>(&NSFilePanelFilename), "@@:");
		class_addMethod(save_panel, sel_registerName("filenames"),
			reinterpret_cast<IMP>(&NSFilePanelFilenames), "@@:");
		class_addClassMethod(save_panel, sel_registerName("savePanel"),
			reinterpret_cast<IMP>(&NSFilePanelFactory), "@@:");

		Class open_panel = RegisterHostClass("NSOpenPanel", save_panel);
		if (open_panel == nullptr) return;
		class_addMethod(open_panel,
			sel_registerName("setAllowsMultipleSelection:"),
			reinterpret_cast<IMP>(&NSOpenPanelSetAllowsMultipleSelection), "v@:B");
		class_addMethod(open_panel, sel_registerName("setCanChooseFiles:"),
			reinterpret_cast<IMP>(&NSOpenPanelSetCanChooseFiles), "v@:B");
		class_addMethod(open_panel,
			sel_registerName("setCanChooseDirectories:"),
			reinterpret_cast<IMP>(&NSOpenPanelSetCanChooseDirectories), "v@:B");
		class_addMethod(open_panel, sel_registerName("runModal"),
			reinterpret_cast<IMP>(&NSFilePanelRunModal), "q@:");
		class_addClassMethod(open_panel, sel_registerName("openPanel"),
			reinterpret_cast<IMP>(&NSFilePanelFactory), "@@:");

		Class popup_button = RegisterHostClass("NSPopUpButton", button);
		if (popup_button == nullptr) return;
		class_addMethod(popup_button, sel_registerName("initWithFrame:pullsDown:"),
			reinterpret_cast<IMP>(&NSPopUpButtonInitWithFramePullsDown),
			"@:{CGRect={CGPoint=dd}{CGSize=dd}}B");
		class_addMethod(popup_button, sel_registerName("addItemWithTitle:"),
			reinterpret_cast<IMP>(&NSPopUpButtonAddItemWithTitle), "v@:@");
		class_addMethod(popup_button,
			sel_registerName("insertItemWithTitle:atIndex:"),
			reinterpret_cast<IMP>(&NSPopUpButtonInsertItemWithTitleAtIndex),
			"v@:@q");
		class_addMethod(popup_button, sel_registerName("removeAllItems"),
			reinterpret_cast<IMP>(&NSPopUpButtonRemoveAllItems), "v@:");
		class_addMethod(popup_button, sel_registerName("removeItemAtIndex:"),
			reinterpret_cast<IMP>(&NSPopUpButtonRemoveItemAtIndex), "v@:q");
		class_addMethod(popup_button, sel_registerName("removeItemWithTitle:"),
			reinterpret_cast<IMP>(&NSPopUpButtonRemoveItemWithTitle), "v@:@");
		class_addMethod(popup_button, sel_registerName("numberOfItems"),
			reinterpret_cast<IMP>(&NSPopUpButtonNumberOfItems), "q@:");
		class_addMethod(popup_button, sel_registerName("indexOfSelectedItem"),
			reinterpret_cast<IMP>(&NSPopUpButtonIndexOfSelectedItem), "q@:");
		class_addMethod(popup_button, sel_registerName("itemTitleAtIndex:"),
			reinterpret_cast<IMP>(&NSPopUpButtonItemTitleAtIndex), "@@:q");
		class_addMethod(popup_button, sel_registerName("itemAtIndex:"),
			reinterpret_cast<IMP>(&NSPopUpButtonItemAtIndex), "@@:q");
		class_addMethod(popup_button, sel_registerName("itemArray"),
			reinterpret_cast<IMP>(&NSPopUpButtonItemArray), "@@:");
		class_addMethod(popup_button, sel_registerName("itemTitles"),
			reinterpret_cast<IMP>(&NSPopUpButtonItemTitles), "@@:");
		class_addMethod(popup_button, sel_registerName("itemWithTitle:"),
			reinterpret_cast<IMP>(&NSPopUpButtonItemWithTitle), "@@:@");
		class_addMethod(popup_button, sel_registerName("indexOfItemWithTitle:"),
			reinterpret_cast<IMP>(&NSPopUpButtonIndexOfItemWithTitle), "q@:@");
		class_addMethod(popup_button, sel_registerName("indexOfItem:"),
			reinterpret_cast<IMP>(&NSPopUpButtonIndexOfItem), "q@:@");
		class_addMethod(popup_button, sel_registerName("titleOfSelectedItem"),
			reinterpret_cast<IMP>(&NSPopUpButtonTitleOfSelectedItem), "@@:");
		class_addMethod(popup_button, sel_registerName("selectedItem"),
			reinterpret_cast<IMP>(&NSPopUpButtonSelectedItem), "@@:");
		class_addMethod(popup_button, sel_registerName("lastItem"),
			reinterpret_cast<IMP>(&NSPopUpButtonLastItem), "@@:");
		class_addMethod(popup_button, sel_registerName("selectItem:"),
			reinterpret_cast<IMP>(&NSPopUpButtonSelectItem), "v@:@");
		class_addMethod(popup_button, sel_registerName("selectItemAtIndex:"),
			reinterpret_cast<IMP>(&NSPopUpButtonSelectItemAtIndex), "v@:q");
		class_addMethod(popup_button, sel_registerName("selectItemWithTitle:"),
			reinterpret_cast<IMP>(&NSPopUpButtonSelectItemWithTitle), "v@:@");
		class_addMethod(popup_button, sel_registerName("title"),
			reinterpret_cast<IMP>(&NSPopUpButtonTitleOfSelectedItem), "@@:");
		class_addMethod(popup_button, sel_registerName("setTitle:"),
			reinterpret_cast<IMP>(&NSPopUpButtonSetTitle), "v@:@");
		class_addMethod(popup_button, sel_registerName("stringValue"),
			reinterpret_cast<IMP>(&NSPopUpButtonTitleOfSelectedItem), "@@:");
		class_addMethod(popup_button, sel_registerName("setStringValue:"),
			reinterpret_cast<IMP>(&NSPopUpButtonSetTitle), "v@:@");
		class_addMethod(popup_button, sel_registerName("pullsDown"),
			reinterpret_cast<IMP>(&NSPopUpButtonPullsDown), "B@:");
		class_addMethod(popup_button, sel_registerName("setPullsDown:"),
			reinterpret_cast<IMP>(&NSPopUpButtonSetPullsDown), "v@:B");

		Class text_field = RegisterHostClass("NSTextField", control);
		if (text_field == nullptr) return;
		class_addMethod(text_field, sel_registerName("placeholderString"),
			reinterpret_cast<IMP>(&NSTextFieldPlaceholderString), "@@:");
		class_addMethod(text_field, sel_registerName("setPlaceholderString:"),
			reinterpret_cast<IMP>(&NSTextFieldSetPlaceholderString), "v@:@");
		class_addMethod(text_field, sel_registerName("isEditable"),
			reinterpret_cast<IMP>(&NSTextFieldIsEditable), "B@:");
		class_addMethod(text_field, sel_registerName("setEditable:"),
			reinterpret_cast<IMP>(&NSTextFieldSetEditable), "v@:B");
		class_addMethod(text_field, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSControlAcceptsFirstResponder), "B@:");

		Class text_view = RegisterHostClass("NSTextView", view);
		if (text_view == nullptr) return;
		class_addMethod(text_view, sel_registerName("string"),
			reinterpret_cast<IMP>(&NSTextViewString), "@@:");
		class_addMethod(text_view, sel_registerName("setString:"),
			reinterpret_cast<IMP>(&NSTextViewSetString), "v@:@");
		class_addMethod(text_view, sel_registerName("selectedRange"),
			reinterpret_cast<IMP>(&NSTextViewSelectedRange),
			"{_NSRange=QQ}@:");
		class_addMethod(text_view, sel_registerName("setSelectedRange:"),
			reinterpret_cast<IMP>(&NSTextViewSetSelectedRange),
			"v@:{_NSRange=QQ}");
		class_addMethod(text_view, sel_registerName("isEditable"),
			reinterpret_cast<IMP>(&NSTextFieldIsEditable), "B@:");
		class_addMethod(text_view, sel_registerName("setEditable:"),
			reinterpret_cast<IMP>(&NSTextFieldSetEditable), "v@:B");
		class_addMethod(text_view, sel_registerName("acceptsFirstResponder"),
			reinterpret_cast<IMP>(&NSControlAcceptsFirstResponder), "B@:");

		Class image = RegisterHostClass("NSImage", root);
		if (image == nullptr) return;
		class_addMethod(image, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSImageInitWithCoder), "@@:@");
		class_addMethod(image, sel_registerName("initWithContentsOfFile:"),
			reinterpret_cast<IMP>(&NSImageInitWithContentsOfFile), "@@:@");
		class_addMethod(image, sel_registerName("initWithData:"),
			reinterpret_cast<IMP>(&NSImageInitWithData), "@@:@");
		class_addMethod(image, sel_registerName("size"),
			reinterpret_cast<IMP>(&NSImageSize), "{CGSize=dd}@:");
		class_addMethod(image, sel_registerName("isValid"),
			reinterpret_cast<IMP>(&NSImageIsValid), "B@:");
		class_addMethod(image, sel_registerName("drawAtPoint:"),
			reinterpret_cast<IMP>(&NSImageDrawAtPoint), "v@:{CGPoint=dd}");
		class_addMethod(image, sel_registerName("drawInRect:"),
			reinterpret_cast<IMP>(&NSImageDrawInRect),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
		class_addClassMethod(image, sel_registerName("imageWithContentsOfFile:"),
			reinterpret_cast<IMP>(&NSImageWithContentsOfFile), "@@:@");
		class_addClassMethod(image, sel_registerName("imageNamed:"),
			reinterpret_cast<IMP>(&NSImageNamed), "@@:@");

		Class image_rep = RegisterHostClass("NSImageRep", root);
		if (image_rep == nullptr) return;
		Class bitmap_rep = RegisterHostClass("NSBitmapImageRep", image_rep);
		if (bitmap_rep == nullptr) return;
		class_addMethod(bitmap_rep, sel_registerName("initWithData:"),
			reinterpret_cast<IMP>(&NSBitmapImageRepInitWithData), "@@:@");
		class_addMethod(bitmap_rep, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSBitmapImageRepInitWithCoder), "@@:@");
		class_addMethod(bitmap_rep, sel_registerName("pixelsWide"),
			reinterpret_cast<IMP>(&NSBitmapImageRepPixelsWide), "Q@:");
		class_addMethod(bitmap_rep, sel_registerName("pixelsHigh"),
			reinterpret_cast<IMP>(&NSBitmapImageRepPixelsHigh), "Q@:");
		class_addMethod(bitmap_rep, sel_registerName("size"),
			reinterpret_cast<IMP>(&NSBitmapImageRepSize), "{CGSize=dd}@:");
		class_addMethod(bitmap_rep, sel_registerName("setSize:"),
			reinterpret_cast<IMP>(&NSBitmapImageRepSetSize),
			"v@:{CGSize=dd}");
		class_addMethod(bitmap_rep, sel_registerName("CGImage"),
			reinterpret_cast<IMP>(&NSBitmapImageRepCGImage), "^{CGImage=}@:");
		class_addMethod(bitmap_rep, sel_registerName("draw"),
			reinterpret_cast<IMP>(&NSBitmapImageRepDraw), "v@:");
		class_addMethod(image, sel_registerName("representations"),
			reinterpret_cast<IMP>(&NSImageRepresentations), "@@:");

		Class image_view = RegisterHostClass("NSImageView", view);
		if (image_view == nullptr) return;
		class_addMethod(image_view, sel_registerName("initWithCoder:"),
			reinterpret_cast<IMP>(&NSImageViewInitWithCoder), "@@:@");
		class_addMethod(image_view, sel_registerName("image"),
			reinterpret_cast<IMP>(&NSImageViewImage), "@@:");
		class_addMethod(image_view, sel_registerName("setImage:"),
			reinterpret_cast<IMP>(&NSImageViewSetImage), "v@:@");
		class_addMethod(image_view, sel_registerName("drawRect:"),
			reinterpret_cast<IMP>(&NSImageViewDrawRect),
			"v@:{CGRect={CGPoint=dd}{CGSize=dd}}");

		if (NSCocoaErrorDomain == nullptr) {
			id domain = NSStringStringWithUTF8String(objc_getClass("NSString"),
				nullptr, "NSCocoaErrorDomain");
			NSCocoaErrorDomain = domain == nullptr ? nullptr : objc_retain(domain);
		}
		if (NSLocalizedDescriptionKey == nullptr) {
			id key = NSStringStringWithUTF8String(objc_getClass("NSString"),
				nullptr, "NSLocalizedDescription");
			NSLocalizedDescriptionKey = key == nullptr ? nullptr : objc_retain(key);
		}
		auto initialize_string = [](id& target, const char* value) {
			if (target != nullptr) return;
			id object = NSStringStringWithUTF8String(objc_getClass("NSString"),
				nullptr, value);
			target = object == nullptr ? nullptr : objc_retain(object);
		};
		initialize_string(NSFileType, "NSFileType");
		initialize_string(NSBundleDidLoadNotification,
			"NSBundleDidLoadNotification");
		initialize_string(NSLoadedClasses, "NSLoadedClasses");
		initialize_string(NSFileSize, "NSFileSize");
		initialize_string(NSFileModificationDate, "NSFileModificationDate");
		initialize_string(NSFileCreationDate, "NSFileCreationDate");
		initialize_string(NSFilePosixPermissions, "NSFilePosixPermissions");
		initialize_string(NSFileSystemSize, "NSFileSystemSize");
		initialize_string(NSFileSystemFreeSize, "NSFileSystemFreeSize");
		initialize_string(NSFileTypeRegular, "NSFileTypeRegular");
		initialize_string(NSFileTypeDirectory, "NSFileTypeDirectory");
		initialize_string(NSFileTypeSymbolicLink, "NSFileTypeSymbolicLink");
		initialize_string(NSFileTypeUnknown, "NSFileTypeUnknown");
	});
}
}

extern "C" void darling_windows_appkit_initialize()
{
	DWORD expected_thread = 0;
	(void)g_appkit_main_thread_id.compare_exchange_strong(expected_thread,
		GetCurrentThreadId(), std::memory_order_acq_rel);
	RegisterAppKitClasses();
	RegisterBootstrapWindowClass();
}

extern "C" std::uintptr_t darling_windows_appkit_symbol(const char* name)
{
	if (name == nullptr) return 0;
	if (name[0] == '_') ++name;
	darling_windows_appkit_initialize();
	if (std::strcmp(name, "NSApplicationMain") == 0) {
		return reinterpret_cast<std::uintptr_t>(&NSApplicationMain);
	}
	if (std::strcmp(name, "__CFConstantStringClassReference") == 0 ||
		std::strcmp(name, "_NSConstantStringClassReference") == 0)
		return reinterpret_cast<std::uintptr_t>(objc_getClass("NSConstantString"));
	constexpr char class_prefix[] = "OBJC_CLASS_$_";
	constexpr char metaclass_prefix[] = "OBJC_METACLASS_$_";
	if (std::strncmp(name, metaclass_prefix, sizeof(metaclass_prefix) - 1) == 0) {
		const Class cls = objc_getMetaClass(name + sizeof(metaclass_prefix) - 1);
		return reinterpret_cast<std::uintptr_t>(cls);
	}
	if (std::strncmp(name, class_prefix, sizeof(class_prefix) - 1) == 0) {
		const Class cls = objc_getClass(name + sizeof(class_prefix) - 1);
		return reinterpret_cast<std::uintptr_t>(cls);
	}
	if (std::strcmp(name, "NSApp") == 0) {
		return reinterpret_cast<std::uintptr_t>(&NSApp);
	}
	if (std::strcmp(name, "NSFontAttributeName") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFontAttributeName);
	if (std::strcmp(name, "NSForegroundColorAttributeName") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSForegroundColorAttributeName);
	if (std::strcmp(name, "NSCocoaErrorDomain") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSCocoaErrorDomain);
	if (std::strcmp(name, "NSLocalizedDescriptionKey") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSLocalizedDescriptionKey);
	if (std::strcmp(name, "NSFileType") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileType);
	if (std::strcmp(name, "NSFileSize") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileSize);
	if (std::strcmp(name, "NSFileModificationDate") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileModificationDate);
	if (std::strcmp(name, "NSFileCreationDate") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileCreationDate);
	if (std::strcmp(name, "NSFilePosixPermissions") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFilePosixPermissions);
	if (std::strcmp(name, "NSFileSystemSize") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileSystemSize);
	if (std::strcmp(name, "NSFileSystemFreeSize") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileSystemFreeSize);
	if (std::strcmp(name, "NSFileTypeRegular") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileTypeRegular);
	if (std::strcmp(name, "NSFileTypeDirectory") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileTypeDirectory);
	if (std::strcmp(name, "NSFileTypeSymbolicLink") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileTypeSymbolicLink);
	if (std::strcmp(name, "NSFileTypeUnknown") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSFileTypeUnknown);
	if (std::strcmp(name, "NSApplicationWillFinishLaunchingNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationWillFinishLaunchingNotification);
	if (std::strcmp(name, "NSApplicationDidFinishLaunchingNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationDidFinishLaunchingNotification);
	if (std::strcmp(name, "NSApplicationWillTerminateNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationWillTerminateNotification);
	if (std::strcmp(name, "NSApplicationWillBecomeActiveNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationWillBecomeActiveNotification);
	if (std::strcmp(name, "NSApplicationDidBecomeActiveNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationDidBecomeActiveNotification);
	if (std::strcmp(name, "NSApplicationWillResignActiveNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationWillResignActiveNotification);
	if (std::strcmp(name, "NSApplicationDidResignActiveNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(
			&NSApplicationDidResignActiveNotification);
	if (std::strcmp(name, "NSApplicationWillHideNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSApplicationWillHideNotification);
	if (std::strcmp(name, "NSApplicationDidHideNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSApplicationDidHideNotification);
	if (std::strcmp(name, "NSApplicationWillUnhideNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSApplicationWillUnhideNotification);
	if (std::strcmp(name, "NSApplicationDidUnhideNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSApplicationDidUnhideNotification);
	if (std::strcmp(name, "NSBundleDidLoadNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSBundleDidLoadNotification);
	if (std::strcmp(name, "NSLoadedClasses") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSLoadedClasses);
	if (std::strcmp(name, "NSExecutableErrorMinimum") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableErrorMinimum);
	if (std::strcmp(name, "NSExecutableNotLoadableError") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableNotLoadableError);
	if (std::strcmp(name, "NSExecutableArchitectureMismatchError") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableArchitectureMismatchError);
	if (std::strcmp(name, "NSExecutableRuntimeMismatchError") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableRuntimeMismatchError);
	if (std::strcmp(name, "NSExecutableLoadError") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableLoadError);
	if (std::strcmp(name, "NSExecutableLinkError") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableLinkError);
	if (std::strcmp(name, "NSExecutableErrorMaximum") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSExecutableErrorMaximum);
	if (std::strcmp(name, "NSBundleExecutableArchitectureI386") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSBundleExecutableArchitectureI386);
	if (std::strcmp(name, "NSBundleExecutableArchitectureX86_64") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSBundleExecutableArchitectureX86_64);
	if (std::strcmp(name, "NSBundleExecutableArchitectureARM64") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSBundleExecutableArchitectureARM64);
	if (std::strcmp(name, "NSBundleExecutableArchitecturePPC") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSBundleExecutableArchitecturePPC);
	if (std::strcmp(name, "NSBundleExecutableArchitecturePPC64") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSBundleExecutableArchitecturePPC64);
	if (std::strcmp(name, "NSWindowWillCloseNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSWindowWillCloseNotification);
	if (std::strcmp(name, "NSWindowDidResizeNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSWindowDidResizeNotification);
	if (std::strcmp(name, "NSWindowDidBecomeKeyNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSWindowDidBecomeKeyNotification);
	if (std::strcmp(name, "NSWindowDidResignKeyNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSWindowDidResignKeyNotification);
	if (std::strcmp(name, "NSWindowDidBecomeMainNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSWindowDidBecomeMainNotification);
	if (std::strcmp(name, "NSWindowDidResignMainNotification") == 0)
		return reinterpret_cast<std::uintptr_t>(&NSWindowDidResignMainNotification);
	return 0;
}

namespace {
Class NSApplicationPrincipalClass()
{
	const Class application_class = objc_getClass("NSApplication");
	if (application_class == nullptr) return nullptr;
	id bundle = NSBundleMainBundle(objc_getClass("NSBundle"), nullptr);
	if (bundle == nullptr) return application_class;
	if (!NSBundleLoad(bundle, nullptr)) return nullptr;
	id key = objc_autorelease(CreateDynamicNSString(objc_getClass("NSString"),
		"NSPrincipalClass"));
	if (key == nullptr) return application_class;
	id principal_name = NSBundleObjectForInfoDictionaryKey(bundle, nullptr, key);
	if (principal_name == nullptr) return application_class;
	const char* name = NSStringUTF8String(principal_name, nullptr);
	if (name == nullptr || name[0] == '\0') return nullptr;
	const Class principal_class = objc_getClass(name);
	for (Class current = principal_class; current != nullptr;
		current = class_getSuperclass(current)) {
		if (current == application_class) return principal_class;
	}
	return nullptr;
}

id NSBundleReadNibData(id bundle, id nib_name)
{
	if (bundle == nullptr || nib_name == nullptr) return nullptr;
	const char* name = NSStringUTF8String(nib_name, nullptr);
	if (name == nullptr || name[0] == '\0') return nullptr;
	id name_object = objc_retain(nib_name);
	id type = CreateDynamicNSString(objc_getClass("NSString"), "nib");
	if (name_object == nullptr || type == nullptr) {
		if (name_object != nullptr) objc_release(name_object);
		if (type != nullptr) objc_release(type);
		return nullptr;
	}
	id path_object = NSBundlePathForResourceType(bundle, nullptr,
		name_object, type);
	objc_release(name_object);
	objc_release(type);
	const char* path_text = NSStringUTF8String(path_object, nullptr);
	if (path_text == nullptr) return nullptr;
	try {
		std::filesystem::path path(Utf8ToWide(path_text));
		std::error_code error;
		if (std::filesystem::is_directory(path, error) && !error)
			path /= L"keyedobjects.nib";
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if (!stream) return nullptr;
		const auto end = stream.tellg();
		if (end <= 0 || static_cast<std::uint64_t>(end) >
			256ull * 1024 * 1024) return nullptr;
		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
		stream.seekg(0, std::ios::beg);
		if (!stream.read(reinterpret_cast<char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()))) return nullptr;
		return CreateNSData(objc_getClass("NSData"), bytes.data(), bytes.size());
	} catch (...) { return nullptr; }
}

bool NSBundleLoadNibNamed(id bundle, id nib_name, id owner,
	bool show_unlisted_windows = false,
	std::vector<id>* top_level_objects = nullptr,
	std::vector<id>* decoded_objects = nullptr,
	id preloaded_data = nullptr)
{
	AppKitNibLoadContext load_context;
	load_context.retain_graph_for_lifetime = top_level_objects == nullptr;
	auto clear_visible_windows = [&load_context] {
		for (id window : load_context.visible_windows) objc_release(window);
		load_context.visible_windows.clear();
		load_context.has_visible_windows = false;
	};
	auto clear_top_level_objects = [&load_context] {
		for (id object : load_context.top_level_objects) objc_release(object);
		load_context.top_level_objects.clear();
	};
	auto clear_decoded_objects = [&load_context] {
		for (id object : load_context.decoded_objects) objc_release(object);
		load_context.decoded_objects.clear();
	};
	id data = preloaded_data == nullptr ? NSBundleReadNibData(bundle, nib_name) :
		objc_retain(preloaded_data);
	if (data == nullptr) return false;
	AppKitNibLoadContext* previous_context = g_current_nib_load_context;
	const bool previous_loading = g_loading_nib;
	id previous_owner = g_nib_owner;
	g_current_nib_load_context = &load_context;
	g_loading_nib = true;
	g_nib_owner = owner;
	id objects = nullptr;
	try {
		objects = NSKeyedUnarchiverUnarchiveObjectWithData(
			objc_getClass("NSKeyedUnarchiver"),
			sel_registerName("unarchiveObjectWithData:"), data);
	} catch (...) { objects = nullptr; }
	g_loading_nib = previous_loading;
	g_nib_owner = previous_owner;
	g_current_nib_load_context = previous_context;
	objc_release(data);
	if (objects == nullptr) {
		clear_visible_windows();
		clear_top_level_objects();
		clear_decoded_objects();
		return false;
	}
	std::vector<id> windows;
	try {
		if (load_context.has_visible_windows) {
			windows.reserve(load_context.visible_windows.size());
			std::lock_guard lock(g_windows_mutex);
			for (id window : load_context.visible_windows) {
				const auto found = g_windows.find(window);
				if (found != g_windows.end() && found->second.handle != nullptr)
					windows.push_back(objc_retain(window));
			}
		} else if (show_unlisted_windows) {
			std::lock_guard lock(g_windows_mutex);
			windows.reserve(g_windows.size());
			for (const auto& [window, state] : g_windows) {
				if (state.handle != nullptr) windows.push_back(objc_retain(window));
			}
		}
	} catch (...) {
		for (id window : windows) objc_release(window);
		clear_visible_windows();
		clear_top_level_objects();
		clear_decoded_objects();
		return false;
	}
	for (id window : windows) {
		NSWindowMakeKeyAndOrderFront(window,
			sel_registerName("makeKeyAndOrderFront:"), nullptr);
		objc_release(window);
	}
	clear_visible_windows();
	if (top_level_objects != nullptr)
		top_level_objects->swap(load_context.top_level_objects);
	clear_top_level_objects();
	if (decoded_objects != nullptr)
		decoded_objects->swap(load_context.decoded_objects);
	clear_decoded_objects();
	return true;
}

bool NSBundleLoadNibNamedClass(Class, SEL, id nib_name, id owner)
{
	id bundle = NSBundleMainBundle(objc_getClass("NSBundle"), nullptr);
	return NSBundleLoadNibNamed(bundle, nib_name, owner);
}

bool NSBundleLoadNibNamedInstance(id bundle, SEL, id nib_name, id owner,
	id* top_level_objects)
{
	if (top_level_objects != nullptr) *top_level_objects = nullptr;
	std::vector<id> objects;
	std::vector<id> decoded_objects;
	if (!NSBundleLoadNibNamed(bundle, nib_name, owner, false,
		top_level_objects == nullptr ? nullptr : &objects,
		top_level_objects == nullptr ? nullptr : &decoded_objects)) return false;
	if (top_level_objects != nullptr) {
		id array = CreateNSArray(objc_getClass("NSArray"), objects.data(),
			objects.size());
		if (array == nullptr) {
			for (id object : objects) objc_release(object);
			for (id object : decoded_objects) objc_release(object);
			return false;
		}
		if (!NSNibArrayAttachGraph(array, decoded_objects)) {
			objc_release(array);
			for (id object : decoded_objects) objc_release(object);
			for (id object : objects) objc_release(object);
			return false;
		}
		*top_level_objects = objc_autorelease(array);
	}
	for (id object : objects) objc_release(object);
	return true;
}

void NSNibFinalize(id object)
{
	AppKitNibState state{};
	{
		std::lock_guard lock(g_nib_instances_mutex);
		auto found = g_nib_instances.find(object);
		if (found == g_nib_instances.end()) return;
		state = found->second;
		g_nib_instances.erase(found);
	}
	if (state.name != nullptr) objc_release(state.name);
	if (state.bundle != nullptr) objc_release(state.bundle);
	if (state.data != nullptr) objc_release(state.data);
}

id NSNibInitWithNameBundle(id self, SEL, id nib_name, id bundle)
{
	if (self == nullptr || nib_name == nullptr) return nullptr;
	if (bundle == nullptr)
		bundle = NSBundleMainBundle(objc_getClass("NSBundle"), nullptr);
	if (bundle == nullptr || NSBundleGetCFBundle(bundle) == nullptr)
		return nullptr;
	id data = NSBundleReadNibData(bundle, nib_name);
	if (data == nullptr) return nullptr;
	id result = NSNibInitWithDataBundle(self, nullptr, data, bundle);
	objc_release(data);
	if (result == nullptr) return nullptr;
	{
		std::lock_guard lock(g_nib_instances_mutex);
		auto found = g_nib_instances.find(self);
		if (found != g_nib_instances.end()) found->second.name = objc_retain(nib_name);
	}
	return result;
}

id NSNibInitWithDataBundle(id self, SEL, id nib_data, id bundle)
{
	if (self == nullptr || nib_data == nullptr) return nullptr;
	if (bundle == nullptr)
		bundle = NSBundleMainBundle(objc_getClass("NSBundle"), nullptr);
	if (bundle == nullptr || NSBundleGetCFBundle(bundle) == nullptr)
		return nullptr;
	AppKitNibState state{nullptr, objc_retain(bundle), objc_retain(nib_data)};
	if (state.bundle == nullptr || state.data == nullptr) {
		if (state.bundle != nullptr) objc_release(state.bundle);
		if (state.data != nullptr) objc_release(state.data);
		return nullptr;
	}
	try {
		std::lock_guard lock(g_nib_instances_mutex);
		if (!g_nib_instances.emplace(self, state).second) {
			objc_release(state.bundle);
			objc_release(state.data);
			return nullptr;
		}
	} catch (...) {
		objc_release(state.bundle);
		objc_release(state.data);
		return nullptr;
	}
	if (!darling_objc_register_object_finalizer(self, &NSNibFinalize)) {
		NSNibFinalize(self);
		return nullptr;
	}
	return self;
}

bool NSNibInstantiateWithOwner(id self, SEL, id owner,
	id* top_level_objects)
{
	if (top_level_objects != nullptr) *top_level_objects = nullptr;
	AppKitNibState state{};
	{
		std::lock_guard lock(g_nib_instances_mutex);
		auto found = g_nib_instances.find(self);
		if (found == g_nib_instances.end()) return false;
		state = {objc_retain(found->second.name),
			objc_retain(found->second.bundle), objc_retain(found->second.data)};
	}
	if (state.bundle == nullptr || state.data == nullptr) {
		if (state.name != nullptr) objc_release(state.name);
		if (state.bundle != nullptr) objc_release(state.bundle);
		if (state.data != nullptr) objc_release(state.data);
		return false;
	}
	std::vector<id> objects;
	std::vector<id> decoded_objects;
	const bool loaded = NSBundleLoadNibNamed(state.bundle, state.name, owner,
		false, top_level_objects == nullptr ? nullptr : &objects,
		top_level_objects == nullptr ? nullptr : &decoded_objects, state.data);
	objc_release(state.name);
	objc_release(state.bundle);
	objc_release(state.data);
	if (!loaded) return false;
	if (top_level_objects != nullptr) {
		id array = CreateNSArray(objc_getClass("NSArray"), objects.data(),
			objects.size());
		if (array == nullptr) {
			for (id object : objects) objc_release(object);
			for (id object : decoded_objects) objc_release(object);
			return false;
		}
		if (!NSNibArrayAttachGraph(array, decoded_objects)) {
			objc_release(array);
			for (id object : decoded_objects) objc_release(object);
			for (id object : objects) objc_release(object);
			return false;
		}
		*top_level_objects = objc_autorelease(array);
	}
	for (id object : objects) objc_release(object);
	return true;
}

void NSApplicationLoadMainNib()
{
	id bundle = NSBundleMainBundle(objc_getClass("NSBundle"), nullptr);
	id key = CreateDynamicNSString(objc_getClass("NSString"), "NSMainNibFile");
	if (bundle == nullptr || key == nullptr) {
		if (key != nullptr) objc_release(key);
		return;
	}
	id nib_name = NSBundleObjectForInfoDictionaryKey(bundle, nullptr, key);
	objc_release(key);
	if (nib_name != nullptr)
		NSBundleLoadNibNamed(bundle, nib_name, NSApp, true);
}
}

extern "C" DARLING_APPKIT_GUEST_ABI int NSApplicationMain(int argc, char** argv)
{
	try {
		std::lock_guard lock(g_process_info_mutex);
		g_process_arguments.clear();
		if (argc > 0 && argv != nullptr)
			for (int index = 0; index < argc; ++index)
				g_process_arguments.emplace_back(argv[index] == nullptr ? "" : argv[index]);
	} catch (...) {
		std::lock_guard lock(g_process_info_mutex);
		g_process_arguments.clear();
	}
	darling_windows_appkit_initialize();
	const Class pool_class = objc_getClass("NSAutoreleasePool");
	id pool = NSAutoreleasePoolNew(pool_class, nullptr);
	if (pool == nullptr) return 1;
	const Class application_class = NSApplicationPrincipalClass();
	if (application_class == nullptr) {
		objc_release(pool);
		return 1;
	}
	NSApp = darling_objc_msgSend_class0(application_class,
		sel_registerName("sharedApplication"));
	if (NSApp == nullptr) {
		objc_release(pool);
		return 1;
	}
	NSApplicationLoadMainNib();
	darling_objc_msgSend_void0(NSApp, sel_registerName("run"));
	const int exit_code = g_application_exit_code;
	objc_release(pool);
	return exit_code;
}
